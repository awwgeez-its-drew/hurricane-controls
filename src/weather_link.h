#pragma once
#include <Arduino.h>
#include <cctype>
#include <cstring>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "buttons.h"

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

private:
    char    line_[32];
    uint8_t lineLen_ = 0;

    void reply(const char* msg) { Serial1.println(msg); }

    // Two commands are understood: "WX PING" (a pure link-health check, no
    // gating at all — used both at the Weather Watcher's own startup and
    // on-demand from its dashboard) and "WX <MODE>", MODE one of
    // WAIL/ATTACK/FASTWAIL. MANUAL is excluded (it needs momentary-hold
    // semantics that don't fit an autonomous trigger) and so is GROWL (a
    // diagnostic test mode, not a warning tone).
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

        RunMode mode;
        if      (!strcmp(cmd, "WAIL"))     mode = RunMode::WAIL;
        else if (!strcmp(cmd, "ATTACK"))   mode = RunMode::ATTACK;
        else if (!strcmp(cmd, "FASTWAIL")) mode = RunMode::FAST_WAIL;
        else { reply("ERR: unknown command"); return; }

        if (!settingsMgr.s.weatherAutoTriggerEnabled) { reply("ERR: disabled"); return; }
        if (buttons.testModeActive) { reply("ERR: test mode active"); return; }

        if (sm.trigger(mode, TriggerSource::NWS_ALERT)) reply("OK: triggered");
        else reply("ERR: busy");
    }
};

extern WeatherLink weatherLink;
