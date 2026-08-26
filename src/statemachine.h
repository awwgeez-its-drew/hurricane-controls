#pragma once
#include <Arduino.h>
#include "motors.h"
#include "settings.h"

enum class State : uint8_t {
    IDLE,
    STARTING,          // independent per-component startup delays, mirrors STOPPING
    RUN_WAIL,
    RUN_ATTACK_ON,      // chopper ON (blower/rotator steady on)
    RUN_ATTACK_OFF,     // chopper OFF, waiting attackOffTime
    RUN_ATTACK_PREON,   // chopper still OFF, waiting attackChopperDelay before re-energising
    RUN_FASTWAIL_ON,    // chopper ON (blower/rotator steady on)
    RUN_FASTWAIL_OFF,   // chopper OFF, waiting fastWailOffTime
    RUN_FASTWAIL_PREON, // chopper still OFF, waiting fastWailChopperDelay before re-energising
    RUN_MANUAL,
    STOPPING,
};

enum class RunMode : uint8_t { NONE, WAIL, ATTACK, FAST_WAIL, MANUAL };

struct TimerInfo {
    uint32_t totalElapsedMs   = 0;
    uint32_t totalRemainingMs = 0;  // wail/attack/fast-wail: time left
    bool     hasRemaining     = false;
};

class StateMachine {
public:
    State   state   = State::IDLE;
    RunMode runMode = RunMode::NONE;

    void begin() {
        allOff();
        digitalWrite(STATUS_LED, LOW);
        state       = State::IDLE;
        runMode     = RunMode::NONE;
        stateTs     = 0;
        runStartTs_ = 0;
    }

    bool trigger(RunMode mode) {
        if (state != State::IDLE) return false;
        runMode     = mode;
        runStartTs_ = 0;
        stateTs     = millis();
        chopperStarted_ = blowerStarted_ = rotStarted_ = false;
        state       = State::STARTING;
        ledBlinkTs_ = millis();
        ledOn_      = true;
        digitalWrite(STATUS_LED, HIGH);
        return true;
    }

    void stop() {
        if (state == State::IDLE) return;
        stateTs         = millis();
        state           = State::STOPPING;
        runMode         = RunMode::NONE;
        chopperStopped_ = blowerStopped_ = rotStopped_ = false;
        doneTs_         = 0;
        digitalWrite(STATUS_LED, HIGH);  // steady on while stopping
    }

    void update() {
        uint32_t now     = millis();
        uint32_t elapsed = now - stateTs;
        const Settings& s = settingsMgr.s;

        // Flash STATUS_LED while running; steady on while STOPPING; off in IDLE
        if (state != State::IDLE && state != State::STOPPING &&
            now - ledBlinkTs_ >= LED_BLINK_MS) {
            ledBlinkTs_ = now;
            ledOn_      = !ledOn_;
            digitalWrite(STATUS_LED, ledOn_ ? HIGH : LOW);
        }

        // Auto-terminate timed modes when their total duration expires
        if (runMode == RunMode::ATTACK && runStartTs_ > 0 &&
            (now - runStartTs_) >= s.attackDuration) {
            stop();
            return;
        }
        if (runMode == RunMode::FAST_WAIL && runStartTs_ > 0 &&
            (now - runStartTs_) >= s.fastWailDuration) {
            stop();
            return;
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
                case RunMode::WAIL:      state = State::RUN_WAIL;        break;
                case RunMode::ATTACK:    state = State::RUN_ATTACK_ON;   break;
                case RunMode::FAST_WAIL: state = State::RUN_FASTWAIL_ON; break;
                case RunMode::MANUAL:    state = State::RUN_MANUAL;      break;
                default:                 stop(); break;
                }
            }
            break;
        }

        // ── Run ───────────────────────────────────────────────────────────
        case State::RUN_WAIL:
            if (elapsed >= s.wailDuration) stop();
            break;

        // Chopper cycles; blower/rotator stay steady on
        case State::RUN_ATTACK_ON:
            if (elapsed >= s.attackOnTime) {
                chopperOff();
                stateTs = now;
                state   = State::RUN_ATTACK_OFF;
            }
            break;

        case State::RUN_ATTACK_OFF:
            if (elapsed >= s.attackOffTime) {
                stateTs = now;
                state   = State::RUN_ATTACK_PREON;
            }
            break;

        case State::RUN_ATTACK_PREON:
            if (elapsed >= s.attackChopperDelay) {
                chopperOn();
                stateTs = now;
                state   = State::RUN_ATTACK_ON;
            }
            break;

        // Chopper cycles; blower/rotator stay steady on
        case State::RUN_FASTWAIL_ON:
            if (elapsed >= s.fastWailOnTime) {
                chopperOff();
                stateTs = now;
                state   = State::RUN_FASTWAIL_OFF;
            }
            break;

        case State::RUN_FASTWAIL_OFF:
            if (elapsed >= s.fastWailOffTime) {
                stateTs = now;
                state   = State::RUN_FASTWAIL_PREON;
            }
            break;

        case State::RUN_FASTWAIL_PREON:
            if (elapsed >= s.fastWailChopperDelay) {
                chopperOn();
                stateTs = now;
                state   = State::RUN_FASTWAIL_ON;
            }
            break;

        case State::RUN_MANUAL:
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
        case State::RUN_ATTACK_ON:     return "attack_on";
        case State::RUN_ATTACK_OFF:
        case State::RUN_ATTACK_PREON:  return "attack_off";
        case State::RUN_FASTWAIL_ON:   return "fastwail_on";
        case State::RUN_FASTWAIL_OFF:
        case State::RUN_FASTWAIL_PREON: return "fastwail_off";
        case State::RUN_MANUAL:        return "manual";
        case State::STOPPING:          return "stop";
        default:                       return "idle";
        }
    }

    const char* modeName() const {
        switch (runMode) {
        case RunMode::WAIL:      return "wail";
        case RunMode::ATTACK:    return "attack";
        case RunMode::FAST_WAIL: return "fastwail";
        case RunMode::MANUAL:    return "manual";
        default:                 return "idle";
        }
    }

    TimerInfo getTimerInfo() const {
        TimerInfo t;
        if (state == State::IDLE || runStartTs_ == 0) return t;

        uint32_t now = millis();
        const Settings& s = settingsMgr.s;
        t.totalElapsedMs = now - runStartTs_;

        switch (state) {
        case State::RUN_WAIL: {
            t.hasRemaining     = true;
            t.totalRemainingMs = (t.totalElapsedMs < s.wailDuration)
                                 ? s.wailDuration - t.totalElapsedMs : 0;
            break;
        }
        case State::RUN_ATTACK_ON:
        case State::RUN_ATTACK_OFF:
        case State::RUN_ATTACK_PREON:
            t.hasRemaining     = true;
            t.totalRemainingMs = (t.totalElapsedMs < s.attackDuration)
                                 ? s.attackDuration - t.totalElapsedMs : 0;
            break;
        case State::RUN_FASTWAIL_ON:
        case State::RUN_FASTWAIL_OFF:
        case State::RUN_FASTWAIL_PREON:
            t.hasRemaining     = true;
            t.totalRemainingMs = (t.totalElapsedMs < s.fastWailDuration)
                                 ? s.fastWailDuration - t.totalElapsedMs : 0;
            break;
        default:
            break;
        }
        return t;
    }

private:
    static constexpr uint32_t LED_BLINK_MS = 250;

    uint32_t stateTs        = 0;
    uint32_t runStartTs_    = 0;
    bool     chopperStarted_ = false;
    bool     blowerStarted_  = false;
    bool     rotStarted_     = false;
    bool     chopperStopped_ = false;
    bool     blowerStopped_  = false;
    bool     rotStopped_     = false;
    uint32_t doneTs_         = 0;
    uint32_t ledBlinkTs_     = 0;
    bool     ledOn_          = false;
};

extern StateMachine sm;
