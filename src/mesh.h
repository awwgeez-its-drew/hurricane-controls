#pragma once
#include <Arduino.h>
#include <cctype>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "buttons.h"

// Bridges a Meshtastic node (wired via UART2, running its Serial Module in
// Text Message mode) to the state machine, so the siren can be triggered/
// stopped remotely over LoRa mesh and report back a confirmation. Every
// command funnels through the same sm.trigger()/sm.stop()/buttons.setLocked()
// calls the web UI and physical buttons already use, so idle-gating and TEST
// MODE exclusivity apply here automatically with no separate safety logic.
//
// Text Message mode prefixes every incoming line with the sender's short
// node id, e.g. "3a3c: SIREN WAIL" — handleCommand() strips that prefix
// before parsing the command, and also uses it as the whitelist check
// against settingsMgr.s.meshWhitelist.
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
        // STOP, physical STOP, or a timed mode simply running out). Leading
        // "." keeps this from matching MESH_COMMAND_PREFIX on other sirens
        // sharing the channel — otherwise they'd parse it as an (unknown)
        // command addressed to them and flood back "ERR: unknown command".
        bool active = sm.isActive();
        if (wasActive_ && !active) reply(".SIREN STOPPED");
        wasActive_ = active;
    }

private:
    char    line_[64];
    uint8_t lineLen_   = 0;
    bool    wasActive_ = false;

    void reply(const char* msg) { Serial2.println(msg); }

    // Comma-separated, case-insensitive, whitespace-trimmed match against
    // settingsMgr.s.meshWhitelist. An empty whitelist blocks everything
    // (fail-safe default) rather than allowing everything.
    static bool isSenderAllowed(const char* id) {
        if (!id || !*id) return false;
        const char* p = settingsMgr.s.meshWhitelist;
        while (*p) {
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != ',') p++;
            const char* end = p;
            while (end > start && end[-1] == ' ') end--;
            size_t len = (size_t)(end - start);
            if (len == strlen(id)) {
                bool eq = true;
                for (size_t i = 0; i < len; i++) {
                    if (toupper((unsigned char)start[i]) != toupper((unsigned char)id[i])) { eq = false; break; }
                }
                if (eq) return true;
            }
        }
        return false;
    }

    void handleCommand(char* raw) {
        char* line = raw;
        while (*line == ' ') line++;

        // Text Message mode prefixes every incoming line with the sender's
        // short node id, e.g. "3a3c: SIREN WAIL" — pull that out before
        // parsing the command, both to feed the whitelist check below and so
        // the SIREN prefix match isn't thrown off by a leading "3a3c: ".
        // If no such pattern is found (e.g. a bare line with no id prefix),
        // senderId stays empty and the whitelist check below will reject it.
        char senderId[9] = {0};
        char* body = line;
        char* colon = strchr(line, ':');
        if (colon && colon > line && (size_t)(colon - line) < sizeof(senderId)) {
            bool allHex = true;
            for (char* p = line; p < colon; p++) {
                if (!isxdigit((unsigned char)*p)) { allHex = false; break; }
            }
            if (allHex) {
                size_t idLen = (size_t)(colon - line);
                memcpy(senderId, line, idLen);
                senderId[idLen] = '\0';
                body = colon + 1;
                while (*body == ' ') body++;
            }
        }

        // Trim, uppercase in place.
        for (char* p = body; *p; p++) *p = toupper((unsigned char)*p);
        size_t len = strlen(body);
        while (len > 0 && isspace((unsigned char)body[len - 1])) body[--len] = '\0';

        // Only act on lines addressed to this device — anything else (another
        // siren's commands/replies, unrelated mesh chat) is silently ignored
        // so a shared channel doesn't get flooded with "unknown command" replies.
        size_t prefixLen = strlen(MESH_COMMAND_PREFIX);
        if (strncmp(body, MESH_COMMAND_PREFIX, prefixLen) != 0) return;

        // Whitelist gate — silently ignore commands from senders not on the
        // list, same "not addressed to us" treatment as an unprefixed line,
        // so an unlisted sender gets no reply/confirmation either way.
        if (!isSenderAllowed(senderId)) return;

        char* cmd = body + prefixLen;
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
