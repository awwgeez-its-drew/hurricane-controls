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
// against settingsMgr.s.meshWhitelist. Commands also take an optional
// trailing password token ("SIREN WAIL PASS123"), checked against
// settingsMgr.s.meshPassword when one is configured; PING is exempt.
//
// Beyond replying to commands, this class also proactively broadcasts
// activation/status messages (mode started/stopped, lockout changes, a
// boot announcement, and a periodic status line) regardless of whether the
// triggering action came from the mesh, the web UI, or a physical button —
// see update()'s edge-detection below. None of these outgoing messages
// start with "SIREN", so they can't be mistaken for a command by another
// unit sharing the channel.
class MeshBridge {
public:
    void begin() {
        Serial2.begin(MESH_BAUD, SERIAL_8N1, MESH_RX_PIN, MESH_TX_PIN);
        wasActive_       = sm.isActive();
        wasLocked_       = buttons.locked;
        lastSeenStopSeq_ = sm.stopCallSeq;
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

        // Cache the last real run mode every tick, before it can go stale —
        // runMode is cleared to NONE the instant stop() runs, well before
        // state actually reaches IDLE (it sits in STOPPING for the
        // configured shutdown delays first).
        if (sm.runMode != RunMode::NONE) lastKnownRunMode_ = sm.runMode;

        // Run started (any source) / run cycle completed (any reason).
        // Leading "\x07" (BEL) on the activation broadcast marks it as an
        // alert to compatible Meshtastic apps, distinct from a normal
        // silent message — reserved for events that need attention right
        // now, not the informational CYCLE COMPLETED/STATUS lines.
        bool active = sm.isActive();
        if (!wasActive_ && active) {
            reply(String("\x07") + modeWord(sm.runMode) + " ACTIVATED (activation point: " +
                  sourceWord(sm.lastTriggerSource) + ")");
        }
        if (wasActive_ && !active) {
            reply(String(modeWord(lastKnownRunMode_)) + " CYCLE COMPLETED - SIREN STOPPED");
        }
        wasActive_ = active;

        // Stop explicitly invoked (any source), even if it was a no-op —
        // detected via a sequence counter since a no-op stop() produces no
        // observable state change to edge-detect against. Also carries the
        // alert-bell prefix, same rationale as the activation broadcast.
        if (sm.stopCallSeq != lastSeenStopSeq_) {
            lastSeenStopSeq_ = sm.stopCallSeq;
            reply(String("\x07STOP ACTIVATED (activation point: ") + sourceWord(sm.lastStopSource) + ")");
        }

        // Physical-button lockout state changed.
        if (buttons.locked != wasLocked_) {
            wasLocked_ = buttons.locked;
            reply(wasLocked_ ? "LOCAL BUTTON LOCKOUT ACTIVE" : "LOCAL BUTTON LOCKOUT INACTIVE");
        }

        // Periodic status line.
        uint32_t now = millis();
        if (now - lastStatusTs_ >= STATUS_INTERVAL_MS) {
            lastStatusTs_ = now;
            reply(buildStatusMessage());
        }
    }

    // Called once from main.cpp, at the very end of setup() (after WiFi and
    // the web UI are also up), so this reflects a fully-ready device.
    void announceStartup() {
        reply("STARTUP COMPLETE");
        lastStatusTs_ = millis();
        reply(buildStatusMessage());
    }

private:
    static constexpr uint32_t STATUS_INTERVAL_MS = 12UL * 60 * 60 * 1000;  // 12 hours

    char     line_[64];
    uint8_t  lineLen_          = 0;
    bool     wasActive_        = false;
    RunMode  lastKnownRunMode_ = RunMode::NONE;
    uint32_t lastSeenStopSeq_  = 0;
    bool     wasLocked_        = false;
    uint32_t lastStatusTs_     = 0;

    void reply(const char* msg) { Serial2.println(msg); }
    void reply(const String& msg) { Serial2.println(msg); }

    static const char* modeWord(RunMode m) {
        switch (m) {
        case RunMode::WAIL:      return "WAIL";
        case RunMode::ATTACK:    return "ATTACK";
        case RunMode::FAST_WAIL: return "FASTWAIL";
        case RunMode::MANUAL:    return "MANUAL";
        default:                 return "UNKNOWN";
        }
    }

    static const char* sourceWord(TriggerSource s) {
        switch (s) {
        case TriggerSource::LOCAL: return "LOCAL";
        case TriggerSource::WEB:   return "WEB";
        case TriggerSource::MESH:  return "MESH";
        default:                   return "LOCAL";
        }
    }

    static String formatUptime(uint32_t ms) {
        uint32_t sec = ms / 1000;
        uint32_t d = sec / 86400;
        uint32_t h = (sec % 86400) / 3600;
        uint32_t m = (sec % 3600) / 60;
        String out;
        if (d) out += String(d) + "d ";
        if (d || h) out += String(h) + "h ";
        out += String(m) + "m";
        return out;
    }

    static String modeOrStandby() { return sm.isIdle() ? "STANDBY" : String(modeWord(sm.runMode)); }

    // ESP32's own internal die-temperature sensor — not ambient, and known
    // to be a rough reading, but needs no extra hardware.
    static int cpuTempF() {
        return (int)(temperatureRead() * 9.0f / 5.0f + 32.0f);
    }

    static String buildStatusMessage() {
        return "STATUS: " + modeOrStandby() + " - LOCAL CONTROL " + (buttons.locked ? "LOCKED" : "UNLOCKED") +
               " // UPTIME: " + formatUptime(millis()) + " // CPU TEMP: " + String(cpuTempF()) + "F";
    }

    static String buildPingReply() {
        return "MODE: " + modeOrStandby() + " // LOCAL CONTROLS " + (buttons.locked ? "LOCKED" : "UNLOCKED") +
               " // UPTIME: " + formatUptime(millis()) + " // CPU TEMP: " + String(cpuTempF()) + "F";
    }

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

    // pw is the (already-uppercased) token typed after the command word, or
    // nullptr if none was given. An empty configured password means none is
    // required at all. Comparison is case-insensitive since pw always
    // arrives uppercased from handleCommand()'s earlier pass over the line.
    static bool isPasswordCorrect(const char* pw) {
        const char* configured = settingsMgr.s.meshPassword;
        if (!*configured) return true;
        if (!pw || !*pw) return false;
        size_t len = strlen(configured);
        if (strlen(pw) != len) return false;
        for (size_t i = 0; i < len; i++) {
            if (toupper((unsigned char)configured[i]) != pw[i]) return false;
        }
        return true;
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

        // Split off an optional trailing password token: "WAIL PASS123" ->
        // cmd="WAIL", pw="PASS123". pw is already uppercase, since the whole
        // body was uppercased above before this split.
        char* pw = nullptr;
        char* sp = strchr(cmd, ' ');
        if (sp) {
            *sp = '\0';
            pw = sp + 1;
            while (*pw == ' ') pw++;
        }

        // Password gate — every command except PING requires it (when one is
        // configured). Same silent-ignore treatment as the whitelist/prefix
        // checks above: a wrong or missing password gets no reply at all.
        if (strcmp(cmd, "PING") != 0 && !isPasswordCorrect(pw)) return;

        bool testBlocked = buttons.testModeActive;

        if (!strcmp(cmd, "WAIL") || !strcmp(cmd, "ATTACK") || !strcmp(cmd, "FASTWAIL")) {
            if (testBlocked) { reply("ERR: test mode active"); return; }
            RunMode m = !strcmp(cmd, "WAIL")   ? RunMode::WAIL :
                        !strcmp(cmd, "ATTACK") ? RunMode::ATTACK : RunMode::FAST_WAIL;
            if (!sm.trigger(m, TriggerSource::MESH)) reply("ERR: busy");
            // else: the generic "<MODE> ACTIVATED (activation point: MESH)"
            // broadcast in update() covers the success case, with no
            // perceptible delay since it fires later in this same tick.
        } else if (!strcmp(cmd, "STOP")) {
            if (testBlocked) { reply("ERR: test mode active"); return; }
            sm.stop(TriggerSource::MESH);
            // "STOP ACTIVATED (activation point: MESH)" is broadcast
            // generically in update(), covering this case too.
        } else if (!strcmp(cmd, "LOCK")) {
            buttons.setLocked(true, false);
            // "LOCAL BUTTON LOCKOUT ACTIVE" is broadcast generically in
            // update() whenever buttons.locked changes, covering this too.
        } else if (!strcmp(cmd, "UNLOCK")) {
            buttons.setLocked(false, false);
            // "LOCAL BUTTON LOCKOUT INACTIVE" covers this the same way.
        } else if (!strcmp(cmd, "REBOOT")) {
            reply("OK: rebooting");
            Serial2.flush();
            delay(100);
            ESP.restart();
        } else if (!strcmp(cmd, "PING")) {
            reply(buildPingReply());
        } else {
            reply("ERR: unknown command");
        }
    }
};

extern MeshBridge meshBridge;
