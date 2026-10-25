#pragma once
#include <Arduino.h>
#include <cctype>
#include <cstring>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "buttons.h"
#include "clock.h"
#include "sync.h"

// Bridges a second, dedicated ESP32 ("Weather Watcher", see weather-watcher/
// in this repo) that polls National Weather Service alerts for a specific
// point and, on a qualifying Tornado/Severe Thunderstorm Warning, sends a
// trigger command over this directly-wired UART1 link. Unlike the Meshtastic
// and web command paths, there is no whitelist/password here — the physical
// wire itself is the trust boundary, the same model as the physical buttons.
//
// buttons.locked (the plain lock icon) only ever restricts the physical
// button scan in buttons.h — it was never checked here or in mesh.h/
// webserver.h's /cmd dispatch, so a weather trigger behaves exactly like an
// existing WEB or MESH trigger in that respect, nothing new. TEST MODE is
// the one hard gate that does apply: its whole purpose is exclusive
// hardware access for the Test page's own component buttons during a bench
// test, and that must not be interrupted by an autonomous trigger. A
// dedicated settingsMgr.s.weatherAutoTriggerEnabled toggle is the owner's
// kill-switch for this specific input, independent of everything else.
class WeatherLink {
public:
    void begin() {
        Serial1.begin(WEATHER_BAUD, SERIAL_8N1, WEATHER_RX_PIN, WEATHER_TX_PIN);
    }

    void update() {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n' || c == '\r') {
                if (lineLen_ > 0) {
                    line_[lineLen_] = '\0';
                    handleCommand(line_);
                    lineLen_ = 0;
                }
            } else if (lineLen_ < sizeof(line_) - 1) {
                line_[lineLen_++] = c;
            }
        }
        sendStatusEvents();
    }

    // ── Weather Watcher's self-reported health, pushed unprompted over this
    // same link (see "WX STATUS" below) — used for the Settings page status
    // indicator and folded into the mesh STATUS/PING messages. everReported()
    // is false until the first one arrives (e.g. link never wired, or the
    // Weather Watcher board isn't powered), distinct from an actual error.
    // pending() means it's alive but hasn't finished its first NWS poll yet
    // ("WX STATUS PENDING ..."), so neither OK nor an error is known.
    bool        everReported() const { return everReported_; }
    bool        reportedOk()   const { return ok_; }
    bool        pending()      const { return pending_; }
    bool        finalReported() const { return everReported_ && !pending_; }
    const char* detail()       const { return detail_; }

    // True once something has been heard at least once and then nothing for
    // 90s — the link is wired and was working, but has gone quiet (WW
    // crashed/rebooted/lost power without the main board noticing any other
    // way, since there's no separate heartbeat beyond ordinary traffic).
    // Distinct from !everReported(), which just means "never wired/powered".
    bool lost() const { return everReported_ && deadlinePassed(lastRxMs_ + 90000); }
    uint32_t lastRxAgeMs() const { return millis() - lastRxMs_; }

    // "TYPE - THREAT LEVEL" string from the most recent "WX <MODE> <id> <descriptor>"
    // trigger (e.g. "TORNADO EMERGENCY - CATASTROPHIC") — see nws_client.h's
    // categoryDescriptor(). Empty if none has arrived yet. Read by mesh.h to
    // enrich its activation broadcast when the trigger source is NWS_ALERT.
    const char* lastAlertDescriptor() const { return lastAlertDesc_; }

    // The alert's VTEC-derived tracking id (e.g. "KOUN.TO.W.0023") from that
    // same trigger — NWS's own identifier for the specific warning, kept for
    // later troubleshooting/cross-reference. Empty if none has arrived yet.
    const char* lastAlertId() const { return lastAlertId_; }

    // Unprompted main->WW command, the reverse direction of everything else
    // on this link (which is otherwise always WW-initiated). The Weather
    // Watcher's own link-reply reader (nws_client.h's handleReply()) treats
    // a bare "REBOOT" line as a restart request rather than a reply to
    // anything it sent. Fire-and-forget, same as every other message here —
    // see mesh.h's "WXREBOOT" command.
    void requestReboot() { Serial1.println("REBOOT"); }

    // One alert from the most recent "WX ALERTS .. WX ALERTS END" batch
    // (protocol v2, docs/uart-protocol.md section 2.1) — feeds the main
    // page's active-warning banner. event is truncated to fit; exp is 0 if
    // the Weather Watcher's own clock wasn't synced when it sent the batch.
    struct LinkedAlert {
        char     vtecKey[20] = "";
        uint8_t  category    = 0;
        uint32_t expEpoch    = 0;
        char     event[41]   = "";
    };
    static constexpr uint8_t MAX_LINKED_ALERTS = 8;

    uint8_t            alertCount()        const { return alertCount_; }
    const LinkedAlert& alert(uint8_t i)    const { return alerts_[i]; }
    uint32_t           alertsReceivedMs()  const { return lastAlertsRxMs_; }

private:
    char    line_[160];
    uint8_t lineLen_ = 0;

    uint32_t lastRxMs_ = 0;
    bool everReported_ = false;
    bool ok_            = false;
    bool pending_       = false;
    char detail_[64]    = "Not yet reported";
    char lastAlertDesc_[64] = "";
    char lastAlertId_[20]   = "";

    LinkedAlert alerts_[MAX_LINKED_ALERTS];
    uint8_t     alertCount_       = 0;
    LinkedAlert pendingAlerts_[MAX_LINKED_ALERTS];
    uint8_t     pendingCount_     = 0;
    bool        receivingAlerts_  = false;
    uint32_t    lastAlertsRxMs_   = 0;

    void reply(const char* msg) { Serial1.println(msg); }

    static const char* sourceWord(TriggerSource s) {
        switch (s) {
        case TriggerSource::LOCAL:     return "LOCAL";
        case TriggerSource::WEB:       return "WEB";
        case TriggerSource::MESH:      return "MESH";
        case TriggerSource::NWS_ALERT: return "NWS_ALERT";
        default:                       return "LOCAL";
        }
    }

    // Unprompted "SX ..." events to the Weather Watcher (docs/uart-protocol.md
    // section 2.3), so it can log/notify (WP3/WP4) on siren activity it
    // didn't itself cause — a WEB/MESH/button activation while an NWS
    // warning is still active is exactly the kind of thing worth a push.
    // Edge-detected the same way mesh.h does, off the same shared sm/
    // buttons/settingsMgr state — an independent "last seen" here doesn't
    // interfere with mesh.h's own edge-detection on those same fields.
    // This set mirrors every mesh.h broadcast except the ones *about* the
    // Weather Watcher's own health (WEATHER WATCHER LOST/RECOVERED/ERROR) —
    // LOST is informationally impossible to mirror over the very link
    // that's gone quiet, and ERROR/RECOVERED would just tell the Weather
    // Watcher what it already told the main board ("WX STATUS ERROR..."),
    // redundant with its own existing NWS-poll-failing/recovered pushes.
    bool     sxWasActive_         = false;
    RunMode  sxLastKnownRunMode_  = RunMode::NONE;
    uint32_t sxLastSeenStopSeq_   = 0;
    bool     sxWasLocked_         = false;
    uint32_t sxLastSeenLockedPressSeq_ = 0;
    bool     sxWasWeatherEnabled_ = false;
    bool     sxStartupSent_       = false;
    uint32_t sxLastStatusTs_      = 0;
    static constexpr uint32_t SX_STATUS_INTERVAL_MS = 12UL * 60 * 60 * 1000; // 12 hours

    void sendStatusEvents() {
        if (sm.runMode != RunMode::NONE) sxLastKnownRunMode_ = sm.runMode;

        bool active = sm.isActive();
        if (!sxWasActive_ && active) {
            Serial1.print("SX ACTIVATED ");
            Serial1.print(runModeWord(sm.runMode));
            Serial1.print(' ');
            Serial1.println(sourceWord(sm.lastTriggerSource));
        }
        if (sxWasActive_ && !active) {
            Serial1.print("SX STOPPED ");
            Serial1.println(runModeWord(sxLastKnownRunMode_));
        }
        sxWasActive_ = active;

        if (sm.stopCallSeq != sxLastSeenStopSeq_) {
            sxLastSeenStopSeq_ = sm.stopCallSeq;
            Serial1.print("SX STOP ");
            Serial1.println(sourceWord(sm.lastStopSource));
        }

        if (buttons.locked != sxWasLocked_) {
            sxWasLocked_ = buttons.locked;
            Serial1.println(sxWasLocked_ ? "SX LOCKOUT ON" : "SX LOCKOUT OFF");
        }

        if (buttons.lockedPressSeq != sxLastSeenLockedPressSeq_) {
            sxLastSeenLockedPressSeq_ = buttons.lockedPressSeq;
            Serial1.print("SX LOCKEDPRESS ");
            Serial1.println(lockedButtonWord(buttons.lastLockedPress));
        }

        if (settingsMgr.s.weatherAutoTriggerEnabled != sxWasWeatherEnabled_) {
            sxWasWeatherEnabled_ = settingsMgr.s.weatherAutoTriggerEnabled;
            Serial1.println(sxWasWeatherEnabled_ ? "SX WEATHERTRIG ON" : "SX WEATHERTRIG OFF");
        }

        uint32_t now = millis();
        if (!sxStartupSent_) {
            sxStartupSent_ = true;
            Serial1.println("SX STARTUP");
            sxLastStatusTs_ = now;
            sendSxStatus();
        } else if (now - sxLastStatusTs_ >= SX_STATUS_INTERVAL_MS) {
            sxLastStatusTs_ = now;
            sendSxStatus();
        }
    }

    static const char* lockedButtonWord(LockedButton b) {
        switch (b) {
        case LockedButton::STOP:   return "STOP";
        case LockedButton::WAIL:   return "WAIL";
        case LockedButton::ATTACK: return "ATTACK";
        default:                   return "UNKNOWN";
        }
    }

    // "SX STATUS <MODE|IDLE> <LOCKED:0|1> <UPTIME_SEC> <TEMP_F>" — sent once
    // right after SX STARTUP and then every SX_STATUS_INTERVAL_MS, mirroring
    // mesh.h's own periodic STATUS broadcast. Doesn't include the Weather
    // Watcher's own health the way mesh's STATUS does (it already knows its
    // own state) — just this board's.
    void sendSxStatus() {
        Serial1.print("SX STATUS ");
        Serial1.print(sm.isIdle() ? "IDLE" : runModeWord(sm.runMode));
        Serial1.print(' ');
        Serial1.print(buttons.locked ? 1 : 0);
        Serial1.print(' ');
        Serial1.print(millis() / 1000);
        Serial1.print(' ');
        Serial1.println((int)roundf(temperatureRead() * 9.0f / 5.0f + 32.0f));
    }

    static const char* runModeWord(RunMode m) {
        switch (m) {
        case RunMode::WAIL:      return "WAIL";
        case RunMode::ATTACK:    return "ATTACK";
        case RunMode::FAST_WAIL: return "FASTWAIL";
        case RunMode::MANUAL:    return "MANUAL";
        case RunMode::GROWL:     return "GROWL";
        default:                 return "NONE";
        }
    }

    // vtecKey may be empty (e.g. a malformed line) — sent as "-" so the ACK
    // line always has exactly the expected token count.
    void ackReply(const char* vtecKey, const char* code, const char* detail = nullptr) {
        Serial1.print("ACK ");
        Serial1.print((vtecKey && *vtecKey) ? vtecKey : "-");
        Serial1.print(' ');
        if (detail) {
            Serial1.print(code);
            Serial1.print(' ');
            Serial1.println(detail);
        } else {
            Serial1.println(code);
        }
    }

    // Five commands are understood: "WX PING" (a pure link-health check, no
    // gating at all — used both at the Weather Watcher's own startup and
    // on-demand from its dashboard), "WX STATUS <OK|ERROR|PENDING> <detail>"
    // (an unprompted periodic push of the Weather Watcher's own WiFi/NWS API
    // health — no reply expected, it's fire-and-forget), "WX TIME <epoch>"
    // (its NTP-synced clock, fire-and-forget — see clock.h), and
    // "WX <MODE> <id> <descriptor>", MODE one of WAIL/ATTACK/FASTWAIL, <id>
    // the alert's VTEC-derived tracking id (e.g. "KOUN.TO.W.0023", see
    // nws_client.h's sendTrigger()), and <descriptor> a human-readable
    // "TYPE - THREAT LEVEL" string (e.g. "TORNADO EMERGENCY - CATASTROPHIC",
    // see nws_client.h's categoryDescriptor()) — both carried through to the
    // mesh activation broadcast and the run log, for later troubleshooting/
    // cross-reference. MANUAL is excluded (it needs momentary-hold semantics
    // that don't fit an autonomous trigger) and so is GROWL (a diagnostic
    // test mode, not a warning tone).
    void handleCommand(char* raw) {
        char* line = raw;
        while (*line == ' ') line++;
        for (char* p = line; *p; p++) *p = toupper((unsigned char)*p);
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';

        if (strncmp(line, "WX", 2) != 0) return;
        lastRxMs_ = millis();
        char* cmd = line + 2;
        while (*cmd == ' ') cmd++;

        if (!strcmp(cmd, "PING")) { reply("OK: pong 2"); return; }

        if (!strncmp(cmd, "STATUS", 6)) {
            char* arg = cmd + 6;
            while (*arg == ' ') arg++;
            pending_ = !strncmp(arg, "PENDING", 7);
            ok_      = !pending_ && !strncmp(arg, "OK", 2);
            char* detail = strchr(arg, ' ');
            if (detail) { while (*detail == ' ') detail++; } else detail = arg + strlen(arg);
            strlcpy(detail_, detail, sizeof(detail_));
            everReported_ = true;
            return;
        }

        if (!strncmp(cmd, "TIME", 4)) {
            setClockFromLink((uint32_t)strtoul(cmd + 4, nullptr, 10));
            return;
        }

        // "WX ALERTS <n>" starts a staging batch (n is informational only —
        // END is what actually commits it), "WX ALERT <vtecKey> <cat>
        // <expEpoch|0> <event...>" appends one entry to it, and
        // "WX ALERTS END" atomically swaps it into alerts_ so a reader never
        // sees a partially-received list. Feeds the main page's
        // active-warning banner. Checked before the generic "ALERTS" prefix
        // since both start with it.
        if (!strncmp(cmd, "ALERTS END", 10)) {
            if (receivingAlerts_) {
                memcpy(alerts_, pendingAlerts_, sizeof(alerts_));
                alertCount_      = pendingCount_;
                lastAlertsRxMs_  = millis();
                receivingAlerts_ = false;
            }
            return;
        }
        if (!strncmp(cmd, "ALERTS", 6)) {
            receivingAlerts_ = true;
            pendingCount_    = 0;
            return;
        }
        if (!strncmp(cmd, "ALERT ", 6)) {
            if (!receivingAlerts_ || pendingCount_ >= MAX_LINKED_ALERTS) return;
            char* arg = cmd + 6;
            while (*arg == ' ') arg++;
            char* vtec = arg;
            char* p = strchr(arg, ' ');
            if (!p) return;
            *p = '\0'; p++;
            while (*p == ' ') p++;
            char* catStr = p;
            p = strchr(p, ' ');
            if (!p) return;
            *p = '\0'; p++;
            while (*p == ' ') p++;
            char* expStr = p;
            p = strchr(p, ' ');
            if (!p) return;
            *p = '\0'; p++;
            while (*p == ' ') p++;
            char* event = p; // rest of the line

            LinkedAlert& a = pendingAlerts_[pendingCount_++];
            strlcpy(a.vtecKey, vtec, sizeof(a.vtecKey));
            a.category = (uint8_t)strtoul(catStr, nullptr, 10);
            a.expEpoch = (uint32_t)strtoul(expStr, nullptr, 10);
            strlcpy(a.event, event, sizeof(a.event));
            return;
        }

        // Mode word is the first token, alert id the second; everything after
        // that (if any) is the "TYPE - THREAT LEVEL" descriptor, e.g.
        // "FASTWAIL KOUN.TO.W.0023 TORNADO EMERGENCY - CATASTROPHIC" ->
        // modeWord="FASTWAIL", alertId="KOUN.TO.W.0023", desc="TORNADO
        // EMERGENCY - CATASTROPHIC". The descriptor is kept as free text
        // rather than further tokenized, so its own spaces/hyphens pass
        // through untouched.
        char* modeWord = cmd;
        char* rest = strchr(cmd, ' ');
        if (rest) { *rest = '\0'; rest++; while (*rest == ' ') rest++; }
        char* alertId = rest;
        char* desc = rest ? strchr(rest, ' ') : nullptr;
        if (desc) { *desc = '\0'; desc++; while (*desc == ' ') desc++; }

        RunMode mode;
        if      (!strcmp(modeWord, "WAIL"))     mode = RunMode::WAIL;
        else if (!strcmp(modeWord, "ATTACK"))   mode = RunMode::ATTACK;
        else if (!strcmp(modeWord, "FASTWAIL")) mode = RunMode::FAST_WAIL;
        else { ackReply(alertId, "BADCMD"); return; }

        // Checked in this order (docs/uart-protocol.md section 2.2): BADCMD
        // (above), DISABLED, TESTMODE, OTA, JOG, then ACTIVE/STOPPING via
        // sm.state, then attempt trigger(). Each of these mirrors a read-only
        // StateMachine/ButtonHandler/Settings accessor — nothing here
        // reproduces trigger()'s own internal gating logic.
        if (!settingsMgr.s.weatherAutoTriggerEnabled) { ackReply(alertId, "DISABLED"); return; }
        if (buttons.testModeActive)                   { ackReply(alertId, "TESTMODE"); return; }
        if (sm.otaActive)                              { ackReply(alertId, "OTA"); return; }
        if (sm.rotatorJogActive)                       { ackReply(alertId, "JOG"); return; }
        if (sm.state == State::STOPPING)               { ackReply(alertId, "STOPPING"); return; }
        if (sm.state != State::IDLE) { ackReply(alertId, "ACTIVE", runModeWord(sm.runMode)); return; }

        strlcpy(lastAlertDesc_, desc ? desc : "", sizeof(lastAlertDesc_));
        strlcpy(lastAlertId_, alertId ? alertId : "", sizeof(lastAlertId_));
        if (sm.trigger(mode, TriggerSource::NWS_ALERT, lastAlertDesc_, lastAlertId_)) {
            ackReply(alertId, "TRIGGERED");
        } else {
            // Race: passed every check above but trigger() still refused
            // (something else claimed IDLE in between) — report it the same
            // way a pre-existing run would be.
            ackReply(alertId, "ACTIVE", runModeWord(sm.runMode));
        }
    }
};

extern WeatherLink weatherLink;
