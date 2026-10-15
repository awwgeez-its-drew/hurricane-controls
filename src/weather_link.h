#pragma once
#include <Arduino.h>
#include <cctype>
#include <cstring>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "buttons.h"
#include "clock.h"

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

    // "TYPE - THREAT LEVEL" string from the most recent "WX <MODE> <id> <descriptor>"
    // trigger (e.g. "TORNADO EMERGENCY - CATASTROPHIC") — see nws_client.h's
    // categoryDescriptor(). Empty if none has arrived yet. Read by mesh.h to
    // enrich its activation broadcast when the trigger source is NWS_ALERT.
    const char* lastAlertDescriptor() const { return lastAlertDesc_; }

    // The alert's VTEC-derived tracking id (e.g. "KOUN.TO.W.0023") from that
    // same trigger — NWS's own identifier for the specific warning, kept for
    // later troubleshooting/cross-reference. Empty if none has arrived yet.
    const char* lastAlertId() const { return lastAlertId_; }

private:
    char    line_[96];
    uint8_t lineLen_ = 0;

    bool everReported_ = false;
    bool ok_            = false;
    bool pending_       = false;
    char detail_[64]    = "Not yet reported";
    char lastAlertDesc_[64] = "";
    char lastAlertId_[20]   = "";

    void reply(const char* msg) { Serial1.println(msg); }

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
        char* cmd = line + 2;
        while (*cmd == ' ') cmd++;

        if (!strcmp(cmd, "PING")) { reply("OK: pong"); return; }

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
        else { reply("ERR: unknown command"); return; }

        if (!settingsMgr.s.weatherAutoTriggerEnabled) { reply("ERR: disabled"); return; }
        if (buttons.testModeActive) { reply("ERR: test mode active"); return; }

        strlcpy(lastAlertDesc_, desc ? desc : "", sizeof(lastAlertDesc_));
        strlcpy(lastAlertId_, alertId ? alertId : "", sizeof(lastAlertId_));
        if (sm.trigger(mode, TriggerSource::NWS_ALERT, lastAlertDesc_, lastAlertId_)) reply("OK: triggered");
        else reply("ERR: busy");
    }
};

extern WeatherLink weatherLink;
