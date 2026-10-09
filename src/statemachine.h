#pragma once
#include <Arduino.h>
#include <time.h>
#include "motors.h"
#include "settings.h"
#include "runlog.h"

enum class State : uint8_t {
    IDLE,
    STARTING,          // independent per-component startup delays, mirrors STOPPING
    RUN_WAIL,
    RUN_PULSE_ON,       // Attack/Fast Wail: chopper ON (blower/rotator steady on)
    RUN_PULSE_OFF,      // Attack/Fast Wail: chopper OFF, waiting the mode's OFF time
    RUN_MANUAL,
    RUN_GROWL_BLOWER,   // Growl Test: blower alone
    RUN_GROWL_ROTATOR,  // Growl Test: rotator alone
    RUN_GROWL_CHOPPER,  // Growl Test: chopper alone
    STOPPING,
};

enum class RunMode : uint8_t { NONE, WAIL, ATTACK, FAST_WAIL, MANUAL, GROWL };

// Who invoked trigger()/stop() — lets mesh.h report "activation point" on its
// broadcasts without needing to know about buttons.h/webserver.h (would be a
// circular include, since mesh.h already includes buttons.h). AUTO marks an
// internal/automatic call (duration expiry, defensive fallback) that should
// never be reported as a user-invoked "STOP ACTIVATED".
enum class TriggerSource : uint8_t { LOCAL, WEB, MESH, NWS_ALERT, AUTO };

struct TimerInfo {
    uint32_t totalElapsedMs   = 0;
    uint32_t totalRemainingMs = 0;  // wail/attack/fast-wail/growl: time left
    bool     hasRemaining     = false;
};

class StateMachine {
public:
    State   state   = State::IDLE;
    RunMode runMode = RunMode::NONE;
    TriggerSource lastTriggerSource = TriggerSource::LOCAL;
    TriggerSource lastStopSource    = TriggerSource::AUTO;
    uint32_t      stopCallSeq       = 0;   // bumped once per external stop() call, even a no-op one

    // Set by the OTA upload handler while new firmware is being written —
    // refuses every new run (web, mesh, buttons, NWS) so nothing starts only
    // to be cut off by the post-update reboot.
    volatile bool otaActive = false;

    void begin() {
        allOff();
        digitalWrite(STATUS_LED, LOW);
        state       = State::IDLE;
        runMode     = RunMode::NONE;
        stateTs     = 0;
        runStartTs_ = 0;
        heartbeatTs_ = 0;
        heartbeatOn_ = false;
        meshFlashActive_ = false;
    }

    bool trigger(RunMode mode, TriggerSource source = TriggerSource::LOCAL) {
        if (state != State::IDLE || otaActive) return false;
        lastTriggerSource = source;
        runMode     = mode;
        stateTs     = millis();
        ledStrobePhaseTs_ = stateTs;
        ledStrobePulseTs_ = stateTs;
        ledStrobeInBurst_ = true;
        ledOn_            = true;
        digitalWrite(STATUS_LED, HIGH);

        // Run-log bookkeeping — captured now, since runMode is cleared the
        // moment stop() runs, well before the run actually reaches IDLE.
        logMode_        = mode;
        logStartMs_     = stateTs;
        time_t t        = time(nullptr);
        logEpoch_       = (t > 1600000000) ? (uint32_t)t : 0;  // 0 = clock not set yet
        failsafeStop_   = false;
        webKeepaliveTs_ = stateTs;

        if (mode == RunMode::GROWL) {
            // Growl Test bypasses the independent-delay STARTING sequence
            // entirely — only one component is ever on at a time, by design.
            runStartTs_ = stateTs;
            blowerOn();
            state = State::RUN_GROWL_BLOWER;
        } else {
            runStartTs_ = 0;
            chopperStarted_ = blowerStarted_ = rotStarted_ = false;
            state = State::STARTING;
        }
        return true;
    }

    void stop(TriggerSource source = TriggerSource::AUTO) {
        lastStopSource = source;
        if (source != TriggerSource::AUTO) stopCallSeq++;
        if (state == State::IDLE) return;
        // Record how the run ended only on the first stop() of a run — a
        // second stop() during STOPPING just restarts the shutdown sequence.
        if (state != State::STOPPING) {
            logEnd_ = failsafeStop_ ? RunEnd::FAILSAFE
                    : (source == TriggerSource::AUTO) ? RunEnd::COMPLETED : RunEnd::STOPPED;
            logStopSource_ = source;
        }
        stateTs         = millis();
        state           = State::STOPPING;
        runMode         = RunMode::NONE;
        chopperStopped_ = blowerStopped_ = rotStopped_ = false;
        doneTs_         = 0;
        digitalWrite(STATUS_LED, HIGH);  // steady on while stopping
    }

    // Web-UI Manual is momentary: the page refreshes this every ~500 ms while
    // the button is held. If the phone drops off Wi-Fi or locks mid-hold,
    // the release never arrives — update() stops the run once these stop.
    void webKeepalive() { webKeepaliveTs_ = millis(); }

    // Quick double-flash confirming a validly-addressed mesh command was
    // received, regardless of what happens with it next. Overlays whatever
    // the LED is currently doing (idle, mid-run, stopping) for ~240ms and
    // then lets normal behavior resume.
    void flashMeshAck() {
        meshFlashActive_ = true;
        meshFlashStep_   = 0;
        meshFlashTs_     = millis();
        digitalWrite(STATUS_LED, HIGH);
    }

    void update() {
        uint32_t now     = millis();
        uint32_t elapsed = now - stateTs;
        const Settings& s = settingsMgr.s;

        updateStatusLed_(now);

        // Web Manual failsafe — see webKeepalive(). Uses an AUTO stop, so the
        // mesh reports "MANUAL CYCLE COMPLETED" rather than a user STOP.
        if (runMode == RunMode::MANUAL && lastTriggerSource == TriggerSource::WEB &&
            now - webKeepaliveTs_ >= WEB_KEEPALIVE_TIMEOUT_MS) {
            failsafeStop_ = true;
            stop();
            return;
        }

        // Auto-terminate timed modes when their total duration expires.
        // (Growl Test ends itself when its last stage finishes.)
        if (runMode != RunMode::GROWL && runStartTs_ > 0) {
            uint32_t dur = modeDuration();
            if (dur && (now - runStartTs_) >= dur) {
                stop();
                return;
            }
        }

        switch (state) {

        // ── Startup — independent per-component delays from trigger() ────
        case State::STARTING: {
            uint32_t el = now - stateTs;
            if (!chopperStarted_ && el >= s.chopperDelay) { chopperOn(); chopperStarted_ = true; }
            if (!blowerStarted_  && el >= s.blowerDelay)  { blowerOn();  blowerStarted_  = true; }
            if (!rotStarted_     && el >= s.rotatorDelay) { rotatorOn(); rotStarted_     = true; }
            if (chopperStarted_ && blowerStarted_ && rotStarted_) {
                runStartTs_ = now;
                stateTs     = now;
                switch (runMode) {
                case RunMode::WAIL:      state = State::RUN_WAIL;     break;
                case RunMode::ATTACK:
                case RunMode::FAST_WAIL: state = State::RUN_PULSE_ON; break;
                case RunMode::MANUAL:    state = State::RUN_MANUAL;   break;
                default:                 stop(); break;
                }
            }
            break;
        }

        // ── Run ───────────────────────────────────────────────────────────
        // Wail's duration is enforced by the auto-terminate check above.
        case State::RUN_WAIL:
            break;

        // Attack/Fast Wail: chopper cycles; blower/rotator stay steady on
        case State::RUN_PULSE_ON:
            if (elapsed >= pulseOnTime()) {
                chopperOff();
                stateTs = now;
                state   = State::RUN_PULSE_OFF;
            }
            break;

        case State::RUN_PULSE_OFF:
            if (elapsed >= pulseOffTime()) {
                chopperOn();
                stateTs = now;
                state   = State::RUN_PULSE_ON;
            }
            break;

        case State::RUN_MANUAL:
            break;

        // ── Growl Test — one component at a time, no repeat ──────────────
        case State::RUN_GROWL_BLOWER:
            if (elapsed >= s.growlBlowerTime) {
                blowerOff();
                rotatorOn();
                stateTs = now;
                state   = State::RUN_GROWL_ROTATOR;
            }
            break;

        case State::RUN_GROWL_ROTATOR:
            if (elapsed >= s.growlRotatorTime) {
                rotatorOff();
                chopperOn();
                stateTs = now;
                state   = State::RUN_GROWL_CHOPPER;
            }
            break;

        case State::RUN_GROWL_CHOPPER:
            if (elapsed >= s.growlChopperTime) stop();
            break;

        // ── Shutdown — independent per-component delays from stop() ──────
        case State::STOPPING: {
            uint32_t el = now - stateTs;
            if (!chopperStopped_ && el >= s.stopChopperDelay) { chopperOff(); chopperStopped_ = true; }
            if (!blowerStopped_  && el >= s.stopBlowerDelay)  { blowerOff();  blowerStopped_  = true; }
            if (!rotStopped_     && el >= s.stopRotDelay)     { rotatorOff(); rotStopped_     = true; }
            if (chopperStopped_ && blowerStopped_ && rotStopped_) {
                if (doneTs_ == 0) doneTs_ = now;
                if (now - doneTs_ >= 100) {
                    state = State::IDLE;
                    digitalWrite(STATUS_LED, LOW);
                    heartbeatTs_ = now;
                    heartbeatOn_ = false;
                    recordRun(now);
                }
            }
            break;
        }

        case State::IDLE:
        default:
            break;
        }
    }

    bool isIdle()   const { return state == State::IDLE; }
    bool isActive() const { return state != State::IDLE; }

    const char* stateName() const {
        switch (state) {
        case State::STARTING:          return "seq";
        case State::RUN_WAIL:          return "wail";
        case State::RUN_PULSE_ON:      return runMode == RunMode::FAST_WAIL ? "fastwail_on"  : "attack_on";
        case State::RUN_PULSE_OFF:     return runMode == RunMode::FAST_WAIL ? "fastwail_off" : "attack_off";
        case State::RUN_MANUAL:        return "manual";
        case State::RUN_GROWL_BLOWER:
        case State::RUN_GROWL_ROTATOR:
        case State::RUN_GROWL_CHOPPER: return "growl";
        case State::STOPPING:          return "stop";
        default:                       return "idle";
        }
    }

    const char* modeName() const { return modeNameOf(runMode); }

    static const char* modeNameOf(RunMode m) {
        switch (m) {
        case RunMode::WAIL:      return "wail";
        case RunMode::ATTACK:    return "attack";
        case RunMode::FAST_WAIL: return "fastwail";
        case RunMode::MANUAL:    return "manual";
        case RunMode::GROWL:     return "growl";
        default:                 return "idle";
        }
    }

    static const char* sourceNameOf(TriggerSource s) {
        switch (s) {
        case TriggerSource::LOCAL:     return "local";
        case TriggerSource::WEB:       return "web";
        case TriggerSource::MESH:      return "mesh";
        case TriggerSource::NWS_ALERT: return "nws";
        default:                       return "auto";
        }
    }

    TimerInfo getTimerInfo() const {
        TimerInfo t;
        if (state == State::IDLE || runStartTs_ == 0) return t;

        t.totalElapsedMs = millis() - runStartTs_;

        bool timedRunState = state == State::RUN_WAIL || state == State::RUN_PULSE_ON ||
                             state == State::RUN_PULSE_OFF || state == State::RUN_GROWL_BLOWER ||
                             state == State::RUN_GROWL_ROTATOR || state == State::RUN_GROWL_CHOPPER;
        if (timedRunState) {
            uint32_t total     = modeDuration();
            t.hasRemaining     = true;
            t.totalRemainingMs = (t.totalElapsedMs < total) ? total - t.totalElapsedMs : 0;
        }
        return t;
    }

private:
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;  // idle "still alive" flash every 30s
    static constexpr uint32_t HEARTBEAT_FLASH_MS    = 150;    // flash duration
    static constexpr uint32_t WEB_KEEPALIVE_TIMEOUT_MS = 2000;
    static constexpr uint32_t MESH_FLASH_PULSE_MS = 60;  // per-step duration of the mesh-ack double-flash

    // Police-light strobe while a run is active: a burst of rapid pulses,
    // then a dark gap, repeating.
    static constexpr uint32_t LED_STROBE_ON_MS    = 40;
    static constexpr uint32_t LED_STROBE_OFF_MS   = 40;
    static constexpr uint32_t LED_STROBE_BURST_MS = 750;
    static constexpr uint32_t LED_STROBE_GAP_MS   = 500;

    // Drives STATUS_LED for the current state: a mesh-ack flash takes
    // priority and overlays everything else; otherwise steady-on while
    // STOPPING, a strobe burst/gap pattern while a run is active, and an
    // idle heartbeat blip otherwise.
    void updateStatusLed_(uint32_t now) {
        if (meshFlashActive_) {
            if (now - meshFlashTs_ >= MESH_FLASH_PULSE_MS) {
                meshFlashTs_ = now;
                meshFlashStep_++;
                switch (meshFlashStep_) {
                case 1: digitalWrite(STATUS_LED, LOW);  break;
                case 2: digitalWrite(STATUS_LED, HIGH); break;
                case 3: digitalWrite(STATUS_LED, LOW); meshFlashActive_ = false; break;
                }
            }
            return;
        }

        if (state == State::STOPPING) {
            digitalWrite(STATUS_LED, HIGH);  // steady on while stopping
            return;
        }

        if (state == State::IDLE) {
            // Idle "heartbeat": a brief flash every HEARTBEAT_INTERVAL_MS so
            // an idle (LED-off) controller doesn't look indistinguishable
            // from one that's powered off.
            if (!heartbeatOn_ && now - heartbeatTs_ >= HEARTBEAT_INTERVAL_MS) {
                heartbeatTs_ = now;
                heartbeatOn_ = true;
                digitalWrite(STATUS_LED, HIGH);
            } else if (heartbeatOn_ && now - heartbeatTs_ >= HEARTBEAT_FLASH_MS) {
                heartbeatTs_ = now;
                heartbeatOn_ = false;
                digitalWrite(STATUS_LED, LOW);
            }
            return;
        }

        // Active run (STARTING or any RUN_* state): strobe.
        if (ledStrobeInBurst_) {
            if (now - ledStrobePhaseTs_ >= LED_STROBE_BURST_MS) {
                ledStrobeInBurst_ = false;
                ledStrobePhaseTs_ = now;
                digitalWrite(STATUS_LED, LOW);
            } else if (now - ledStrobePulseTs_ >= (ledOn_ ? LED_STROBE_ON_MS : LED_STROBE_OFF_MS)) {
                ledStrobePulseTs_ = now;
                ledOn_ = !ledOn_;
                digitalWrite(STATUS_LED, ledOn_ ? HIGH : LOW);
            }
        } else if (now - ledStrobePhaseTs_ >= LED_STROBE_GAP_MS) {
            ledStrobeInBurst_ = true;
            ledStrobePhaseTs_ = now;
            ledStrobePulseTs_ = now;
            ledOn_            = true;
            digitalWrite(STATUS_LED, HIGH);
        }
    }

    // Total run length for the current mode, 0 = untimed (Manual).
    uint32_t modeDuration() const {
        const Settings& s = settingsMgr.s;
        switch (runMode) {
        case RunMode::WAIL:      return s.wailDuration;
        case RunMode::ATTACK:    return s.attackDuration;
        case RunMode::FAST_WAIL: return s.fastWailDuration;
        case RunMode::GROWL:     return s.growlBlowerTime + s.growlRotatorTime + s.growlChopperTime;
        default:                 return 0;
        }
    }

    uint32_t pulseOnTime() const {
        return runMode == RunMode::FAST_WAIL ? settingsMgr.s.fastWailOnTime : settingsMgr.s.attackOnTime;
    }
    uint32_t pulseOffTime() const {
        return runMode == RunMode::FAST_WAIL ? settingsMgr.s.fastWailOffTime : settingsMgr.s.attackOffTime;
    }

    void recordRun(uint32_t now) {
        RunEntry e;
        e.epoch          = logEpoch_;
        e.startUptimeSec = logStartMs_ / 1000;
        e.durationSec    = (now - logStartMs_ + 500) / 1000;
        e.mode           = (uint8_t)logMode_;
        e.source         = (uint8_t)lastTriggerSource;
        e.end            = (uint8_t)logEnd_;
        e.stopSource     = (uint8_t)logStopSource_;
        runLog.record(e);
    }

    uint32_t stateTs        = 0;
    uint32_t runStartTs_    = 0;
    bool     chopperStarted_ = false;
    bool     blowerStarted_  = false;
    bool     rotStarted_     = false;
    bool     chopperStopped_ = false;
    bool     blowerStopped_  = false;
    bool     rotStopped_     = false;
    uint32_t doneTs_         = 0;
    uint32_t ledStrobePhaseTs_ = 0;
    uint32_t ledStrobePulseTs_ = 0;
    bool     ledStrobeInBurst_ = true;
    bool     ledOn_          = false;
    uint32_t heartbeatTs_    = 0;
    bool     heartbeatOn_    = false;
    uint32_t webKeepaliveTs_ = 0;
    bool     failsafeStop_   = false;
    bool     meshFlashActive_ = false;
    uint8_t  meshFlashStep_   = 0;
    uint32_t meshFlashTs_     = 0;

    RunMode       logMode_       = RunMode::NONE;
    uint32_t      logStartMs_    = 0;
    uint32_t      logEpoch_      = 0;
    RunEnd        logEnd_        = RunEnd::COMPLETED;
    TriggerSource logStopSource_ = TriggerSource::AUTO;
};

extern StateMachine sm;
