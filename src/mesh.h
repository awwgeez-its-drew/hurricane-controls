#pragma once
#include <Arduino.h>
#include <cctype>
#include "config.h"
#include "statemachine.h"
#include "buttons.h"

// Bridges a Meshtastic node (wired via UART2, running its Serial Module in
// plain-text mode) to the state machine, so the siren can be triggered/
// stopped remotely over LoRa mesh and report back a confirmation. Every
// command funnels through the same sm.trigger()/sm.stop()/buttons.setLocked()
// calls the web UI and physical buttons already use, so idle-gating and TEST
// MODE exclusivity apply here automatically with no separate safety logic.
class MeshBridge {
public:
    void begin() {
        Serial2.begin(MESH_BAUD, SERIAL_8N1, MESH_RX_PIN, MESH_TX_PIN);
        wasActive_ = sm.isActive();
    }

    void update() {
        while (Serial2.available()) {
            char c = Serial2.read();
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

        // Report once whenever a run ends, regardless of why (mesh STOP, web
        // STOP, physical STOP, or a timed mode simply running out).
        bool active = sm.isActive();
        if (wasActive_ && !active) reply("SIREN STOPPED");
        wasActive_ = active;
    }

private:
    char    line_[64];
    uint8_t lineLen_   = 0;
    bool    wasActive_ = false;

    void reply(const char* msg) { Serial2.println(msg); }

    void handleCommand(char* raw) {
        // Trim leading/trailing whitespace, uppercase in place.
        char* line = raw;
        while (*line == ' ') line++;
        for (char* p = line; *p; p++) *p = toupper((unsigned char)*p);
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';

        // Only act on lines addressed to this device — anything else (another
        // siren's commands/replies, unrelated mesh chat) is silently ignored
        // so a shared channel doesn't get flooded with "unknown command" replies.
        size_t prefixLen = strlen(MESH_COMMAND_PREFIX);
        if (strncmp(line, MESH_COMMAND_PREFIX, prefixLen) != 0) return;
        char* cmd = line + prefixLen;
        while (*cmd == ' ') cmd++;

        bool testBlocked = buttons.testModeActive;

        if (!strcmp(cmd, "WAIL") || !strcmp(cmd, "ATTACK") || !strcmp(cmd, "FASTWAIL")) {
            if (testBlocked) { reply("ERR: test mode active"); return; }
            RunMode m = !strcmp(cmd, "WAIL")   ? RunMode::WAIL :
                        !strcmp(cmd, "ATTACK") ? RunMode::ATTACK : RunMode::FAST_WAIL;
            if (sm.trigger(m)) {
                String r = String(cmd) + " START received";
                reply(r.c_str());
            } else {
                reply("ERR: busy");
            }
        } else if (!strcmp(cmd, "STOP")) {
            if (testBlocked) { reply("ERR: test mode active"); return; }
            sm.stop();
            reply("STOP received");
        } else if (!strcmp(cmd, "LOCK")) {
            buttons.setLocked(true, false);
            reply("OK: locked");
        } else if (!strcmp(cmd, "UNLOCK")) {
            buttons.setLocked(false, false);
            reply("OK: unlocked");
        } else if (!strcmp(cmd, "REBOOT")) {
            reply("OK: rebooting");
            Serial2.flush();
            delay(100);
            ESP.restart();
        } else if (!strcmp(cmd, "PING")) {
            reply("PONG");
        } else {
            reply("ERR: unknown command");
        }
    }
};

extern MeshBridge meshBridge;
