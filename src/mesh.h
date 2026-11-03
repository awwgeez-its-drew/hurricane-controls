#pragma once
#include <Arduino.h>
#include <cctype>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "buttons.h"
#include "weather_link.h"
#include "mesh_health.h"
#include "wifi_manager.h"
#include "runlog.h"
#include "clock.h"

// Bridges a Meshtastic node (wired via UART2, running its Serial Module in
// Text Message mode) to the state machine, so the siren can be triggered/
// stopped remotely over LoRa mesh and report back a confirmation. Every
// command funnels through the same sm.trigger()/sm.stop()/buttons.setLocked()
// calls the web UI and physical buttons already use, so idle-gating and TEST
// MODE exclusivity apply here automatically with no separate safety logic.
//
// Text Message mode prefixes every incoming line with the sender's short
// name — the node's user-configurable 1-4 character display label (NOT a
// hex node ID; see SerialModule.cpp in the Meshtastic firmware), e.g.
// "VSR: SIREN WAIL" — handleCommand() strips that prefix before parsing
// the command, and also uses it as the whitelist check against
// settingsMgr.s.meshWhitelist. Commands also take an optional
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
        wasActive_              = sm.isActive();
        wasLocked_              = buttons.locked;
        lastSeenStopSeq_        = sm.stopCallSeq;
        lastSeenLockedPressSeq_ = buttons.lockedPressSeq;
        wasWeatherEnabled_      = settingsMgr.s.weatherAutoTriggerEnabled;
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
            if (settingsMgr.s.meshNotifyActivation) {
                String msg = String("\x07") + modeWord(sm.runMode) + " ACTIVATED (activation point: " +
                             sourceWord(sm.lastTriggerSource) + ")";
                // NWS-triggered activations carry a "TYPE - THREAT LEVEL"
                // descriptor from the Weather Watcher (e.g. "TORNADO EMERGENCY
                // - CATASTROPHIC") — see weather_link.h's lastAlertDescriptor().
                if (sm.lastTriggerSource == TriggerSource::NWS_ALERT && weatherLink.lastAlertDescriptor()[0]) {
                    msg += " | ";
                    msg += weatherLink.lastAlertDescriptor();
                }
                reply(msg);
            }
        }
        if (wasActive_ && !active) {
            if (settingsMgr.s.meshNotifyActivation)
                reply(String(modeWord(lastKnownRunMode_)) + " CYCLE COMPLETED - SIREN STOPPED");
        }
        wasActive_ = active;

        // Stop explicitly invoked (any source), even if it was a no-op —
        // detected via a sequence counter since a no-op stop() produces no
        // observable state change to edge-detect against. Also carries the
        // alert-bell prefix, same rationale as the activation broadcast.
        if (sm.stopCallSeq != lastSeenStopSeq_) {
            lastSeenStopSeq_ = sm.stopCallSeq;
            if (settingsMgr.s.meshNotifyActivation)
                reply(String("\x07STOP ACTIVATED (activation point: ") + sourceWord(sm.lastStopSource) + ")");
        }

        // Physical-button lockout state changed.
        if (buttons.locked != wasLocked_) {
            wasLocked_ = buttons.locked;
            if (settingsMgr.s.meshNotifySystemEvents)
                reply(wasLocked_ ? "LOCAL BUTTON LOCKOUT ACTIVE" : "LOCAL BUTTON LOCKOUT INACTIVE");
        }

        // Someone physically pressed a button while it's locked out — alert,
        // since this means a person is at the unit right now trying to use
        // disabled controls.
        if (buttons.lockedPressSeq != lastSeenLockedPressSeq_) {
            lastSeenLockedPressSeq_ = buttons.lockedPressSeq;
            if (settingsMgr.s.meshNotifySystemEvents)
                reply(String("\x07") + lockedButtonWord(buttons.lastLockedPress) + " PRESSED - LOCKED OUT");
        }

        // Automatic weather-triggered activation toggled — regardless of
        // whether it came from a mesh command or the Settings page, same
        // "broadcast on change, from any source" treatment as the lockout.
        if (settingsMgr.s.weatherAutoTriggerEnabled != wasWeatherEnabled_) {
            wasWeatherEnabled_ = settingsMgr.s.weatherAutoTriggerEnabled;
            if (settingsMgr.s.meshNotifySystemEvents)
                reply(wasWeatherEnabled_ ? "WEATHER TRIGGER ENABLED" : "WEATHER TRIGGER DISABLED");
        }

        // Weather Watcher went quiet / came back — a fault condition worth
        // an immediate alert either way, same alert-bell treatment as the
        // activation/stop broadcasts above.
        bool wwLost = weatherLink.lost();
        if (wwLost != wasWwLost_) {
            wasWwLost_ = wwLost;
            if (settingsMgr.s.meshNotifyWwHealth)
                reply(String("\x07WEATHER WATCHER ") + (wwLost ? "LOST" : "RECOVERED"));
        }

        uint32_t now = millis();

        // Weather Watcher self-reported an ERROR (not LOST — it's still
        // talking, just unhappy, e.g. WiFi/NWS API trouble) — re-announce
        // every 10 minutes so an ongoing fault isn't a single easily-missed
        // message, and once more immediately on recovery.
        bool wwError = weatherLink.everReported() && !weatherLink.lost() &&
                       !weatherLink.pending() && !weatherLink.reportedOk();
        if (wwError && (now - lastWwErrorTs_ >= WW_ERROR_REANNOUNCE_MS)) {
            lastWwErrorTs_ = now;
            if (settingsMgr.s.meshNotifyWwHealth)
                reply(String("\x07WEATHER WATCHER ERROR: ") + weatherLink.detail());
        }
        if (wasWwError_ && !wwError) {
            if (settingsMgr.s.meshNotifyWwHealth)
                reply("WEATHER WATCHER RECOVERED");
        }
        wasWwError_ = wwError;

        // New Tornado/Severe T-storm Watch in the Weather Watcher's
        // mirrored alert list (WX ALERTS batch) — a one-time proactive
        // ping, same spirit as the WW health announcements above, even
        // though a Watch never triggers the siren (display-only; see
        // weather-watcher/src/nws_client.h's classify()). category 8/9
        // are AlertCategory::TORNADO_WATCH/SEVERE_TSTORM_WATCH, duplicated
        // here as raw numbers since the two firmwares don't share headers
        // (same convention as src/webserver.h's alertCategoryColor()).
        // Dedup by VTEC key in a small ring so an ongoing Watch doesn't
        // re-announce every time this batch refreshes.
        if (settingsMgr.s.meshNotifyWatches) {
            for (uint8_t i = 0; i < weatherLink.alertCount(); i++) {
                const WeatherLink::LinkedAlert& a = weatherLink.alert(i);
                if (a.category != 8 && a.category != 9) continue;
                if (!a.vtecKey[0]) continue;
                bool alreadyAnnounced = false;
                for (uint8_t k = 0; k < WeatherLink::MAX_LINKED_ALERTS; k++) {
                    if (!strcmp(announcedWatchKeys_[k], a.vtecKey)) { alreadyAnnounced = true; break; }
                }
                if (alreadyAnnounced) continue;
                strlcpy(announcedWatchKeys_[announcedWatchNext_], a.vtecKey, sizeof(announcedWatchKeys_[0]));
                announcedWatchNext_ = (announcedWatchNext_ + 1) % WeatherLink::MAX_LINKED_ALERTS;
                reply(String("\x07") + (a.category == 8 ? "TORNADO WATCH" : "SEVERE THUNDERSTORM WATCH") +
                      " ISSUED: " + a.event);
            }
        }

        // Deferred boot announcement — see announceStartup(). Only the
        // STARTUP COMPLETE/STATUS pair waits; every activation/stop/lockout
        // broadcast above still goes out immediately during this window.
        // STARTUP's own SX push (weatherLink.sendSxStatus() inside
        // sendStatusEvents()) is independent of this — it fires as soon as
        // the Weather Watcher hears from this board at all, not gated on
        // Status Broadcast scheduling.
        if (startupPending_) {
            bool ready = !settingsMgr.s.weatherAutoTriggerEnabled ||   // WW: OFF
                         weatherLink.finalReported() ||                // WW: OK / ERROR
                         now - bootTs_ >= STARTUP_ANNOUNCE_MAX_WAIT_MS; // give up waiting
            if (ready) {
                startupPending_ = false;
                markStatusFiredNow_();
                if (settingsMgr.s.meshNotifyStatus) {
                    reply("STARTUP COMPLETE");
                    reply(buildStatusMessage());
                }
            }
            return;
        }

        // Status Broadcast: fires at most once per matching minute, at up
        // to 3 configured daily local times (Settings → Status Broadcast),
        // replacing the old fixed 12-hour interval. Needs a valid local
        // clock; does nothing until one is set (same as every other
        // clock-dependent feature on this board). Drives both channels —
        // the Meshtastic STATUS line (gated by meshNotifyStatus) and the
        // SX STATUS push to the Weather Watcher (always sent; the Weather
        // Watcher's own ntfyPriBoot/ntfyPriScheduledStatus decide whether
        // it turns into a push on that side) — so they report on the same
        // schedule even though they're two different wire messages.
        if (clockValid()) {
            time_t t = time(nullptr);
            struct tm lt;
            localtime_r(&t, &lt);
            int32_t minuteKey = (int32_t)lt.tm_yday * 1440 + lt.tm_hour * 60 + lt.tm_min;
            if (minuteKey != lastStatusFiredMinuteKey_) {
                const Settings& s = settingsMgr.s;
                bool fire = statusTimeMatches(s.statusTime1, lt.tm_hour, lt.tm_min) ||
                            statusTimeMatches(s.statusTime2, lt.tm_hour, lt.tm_min) ||
                            statusTimeMatches(s.statusTime3, lt.tm_hour, lt.tm_min);
                if (fire) {
                    lastStatusFiredMinuteKey_ = minuteKey;
                    if (settingsMgr.s.meshNotifyStatus)
                        reply(buildStatusMessage());
                    weatherLink.sendSxStatus();
                }
            }
        }
    }

    // Called once from main.cpp, at the very end of setup(). The actual
    // STARTUP COMPLETE + STATUS broadcast is held back (in update()) until
    // the Weather Watcher has reported a real OK/ERROR from its first NWS
    // poll — it boots, joins Wi-Fi and polls on its own schedule, so at the
    // end of this board's setup() it has never reported yet and the STATUS
    // line would always say "WW: N/A". Capped at STARTUP_ANNOUNCE_MAX_WAIT_MS
    // so a missing/stuck Weather Watcher never suppresses the announcement.
    void announceStartup() {
        startupPending_ = true;
        bootTs_         = millis();
    }

    // Parses "HH:MM" and reports whether it matches the given local hour/
    // minute; an empty/malformed string (an unused slot) never matches.
    static bool statusTimeMatches(const char* hhmm, int curH, int curM) {
        if (!hhmm || !hhmm[0]) return false;
        int h, m;
        if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return false;
        return h == curH && m == curM;
    }

private:
    static constexpr uint32_t STARTUP_ANNOUNCE_MAX_WAIT_MS = 120000;       // 2 minutes
    static constexpr uint32_t WW_ERROR_REANNOUNCE_MS = 10UL * 60 * 1000;   // 10 minutes

    // Marks "now" as already handled for the Status Broadcast schedule, so
    // the STARTUP announcement (which always sends its own STATUS line)
    // doesn't also immediately re-fire the scheduled one if it happens to
    // land in the same minute as a configured time. A no-op (leaves the
    // key at its default) until the clock is valid — scheduling simply
    // doesn't start until then.
    void markStatusFiredNow_() {
        if (!clockValid()) return;
        time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        lastStatusFiredMinuteKey_ = (int32_t)lt.tm_yday * 1440 + lt.tm_hour * 60 + lt.tm_min;
    }

    char     line_[64];
    uint8_t  lineLen_          = 0;
    bool     wasActive_        = false;
    RunMode  lastKnownRunMode_ = RunMode::NONE;
    uint32_t lastSeenStopSeq_  = 0;
    bool     wasLocked_        = false;
    int32_t  lastStatusFiredMinuteKey_ = -1;
    uint32_t lastSeenLockedPressSeq_ = 0;
    bool     wasWeatherEnabled_ = false;
    bool     startupPending_   = false;
    uint32_t bootTs_           = 0;
    bool     wasWwLost_        = false;
    bool     wasWwError_       = false;
    uint32_t lastWwErrorTs_    = 0;
    char     announcedWatchKeys_[WeatherLink::MAX_LINKED_ALERTS][20] = {};
    uint8_t  announcedWatchNext_ = 0;

    void reply(const char* msg) { Serial2.println(msg); meshHealth.lastTxMs = millis(); }
    void reply(const String& msg) { Serial2.println(msg); meshHealth.lastTxMs = millis(); }

    static const char* modeWord(RunMode m) {
        switch (m) {
        case RunMode::WAIL:      return "WAIL";
        case RunMode::ATTACK:    return "ATTACK";
        case RunMode::FAST_WAIL: return "FASTWAIL";
        case RunMode::MANUAL:    return "MANUAL";
        case RunMode::GROWL:     return "GROWL";
        default:                 return "UNKNOWN";
        }
    }

    static const char* sourceWord(TriggerSource s) {
        switch (s) {
        case TriggerSource::LOCAL:     return "LOCAL";
        case TriggerSource::WEB:       return "WEB";
        case TriggerSource::MESH:      return "MESH";
        case TriggerSource::NWS_ALERT: return "NWS ALERT";
        default:                       return "LOCAL";
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

    static String modeOrIdle() { return sm.isIdle() ? "IDLE" : String(modeWord(sm.runMode)); }

    // OFF = the owner's kill-switch is off; N/A = enabled but nothing has
    // ever been heard from the Weather Watcher board (link never wired, or
    // it's not powered); LOST = it was heard from before but has gone quiet
    // for 90s+ (wired and working, then stopped — see weather_link.h's
    // lost()); STARTING = it's alive but hasn't finished its first NWS poll
    // yet; OK/ERROR = its own self-reported WiFi/NWS API health, pushed
    // unprompted over the dedicated UART link (see weather_link.h's
    // "WX STATUS" handling).
    static const char* weatherWatcherWord() {
        if (!settingsMgr.s.weatherAutoTriggerEnabled) return "OFF";
        if (!weatherLink.everReported()) return "N/A";
        if (weatherLink.lost()) return "LOST";
        if (weatherLink.pending()) return "STARTING";
        return weatherLink.reportedOk() ? "OK" : "ERROR";
    }

    // ESP32's own internal die-temperature sensor — not ambient, and known
    // to be a rough reading, but needs no extra hardware.
    static int cpuTempF() {
        return (int)(temperatureRead() * 9.0f / 5.0f + 32.0f);
    }

    // "COMPLETED"/"STOPPED"/"FAILSAFE"/"INTERRUPTED" — matches RunEnd's
    // declaration order in runlog.h.
    static const char* runEndWord(uint8_t end) {
        static const char* w[] = {"COMPLETED", "STOPPED", "FAILSAFE", "INTERRUPTED"};
        return end < 4 ? w[end] : "?";
    }

    // Status Broadcast (Settings page card): each field below is only
    // included if its statusInclude* toggle is on, so the message can be
    // as terse or as detailed as the owner wants — see settings.h. Always
    // starts with "STATUS:" even if every toggle happens to be off.
    static String buildStatusMessage() {
        const Settings& s = settingsMgr.s;
        String msg = "STATUS:";
        bool first = true;
        auto add = [&](const String& part) {
            msg += first ? " " : " // ";
            msg += part;
            first = false;
        };
        if (s.statusIncludeMode)
            add(modeOrIdle());
        if (s.statusIncludeLock)
            add(String("LOCAL CONTROL ") + (buttons.locked ? "LOCKED" : "UNLOCKED"));
        if (s.statusIncludeUptime)
            add("UPTIME: " + formatUptime(millis()));
        if (s.statusIncludeCpuTemp)
            add("CPU TEMP: " + String(cpuTempF()) + "F");
        if (s.statusIncludeWwHealth)
            add(String("WW: ") + weatherWatcherWord());
        if (s.statusIncludeWifi)
            add(String("WIFI: ") + (wifiMgr.isConnected() ? "OK" : "DOWN"));
        if (s.statusIncludeTime && clockValid()) {
            time_t t = time(nullptr);
            char buf[12];
            strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
            add(String("TIME: ") + buf);
        }
        if (s.statusIncludeLastRun) {
            if (runLog.count() > 0) {
                const RunEntry& e = runLog.recent(0);
                add(String("LAST RUN: ") + modeWord((RunMode)e.mode) + " from " +
                    sourceWord((TriggerSource)e.source) + " (" + runEndWord(e.end) + ")");
            } else {
                add("LAST RUN: NONE");
            }
        }
        if (s.statusIncludeAlerts) {
            uint8_t n = weatherLink.alertCount();
            if (n == 0) add("ALERTS: NONE");
            else add("ALERTS: " + String(n) + " (" + weatherLink.alert(0).event + ")");
        }
        return msg;
    }

    static String buildPingReply() {
        return "MODE: " + modeOrIdle() + " // LOCAL CONTROLS " + (buttons.locked ? "LOCKED" : "UNLOCKED") +
               " // UPTIME: " + formatUptime(millis()) + " // CPU TEMP: " + String(cpuTempF()) + "F" +
               " // WW: " + weatherWatcherWord();
    }

    // Comma-separated, case-insensitive, whitespace-trimmed match against
    // settingsMgr.s.meshWhitelist — entries are Meshtastic short names
    // (the sender's 1-4 character display label, whatever it's set to),
    // not hex node IDs. An empty whitelist blocks everything (fail-safe
    // default) rather than allowing everything.
    //
    // settingsMgr.s.meshWhitelistEnabled is the master on/off for this
    // check — when false, every properly-addressed (SIREN-prefixed),
    // password-correct command is accepted regardless of sender, same as
    // if every short name were whitelisted. This is a deliberate, opt-in
    // reduction in security (anyone transmitting on the Meshtastic channel
    // can control the siren), surfaced with a warning on the Settings
    // page; defaults true (whitelist enforced) so existing installs are
    // unaffected.
    static bool isSenderAllowed(const char* id) {
        if (!settingsMgr.s.meshWhitelistEnabled) return true;
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
        // short name (an arbitrary 1-4 character label set on that node,
        // NOT a hex node ID — Meshtastic's default auto-generated short
        // names just happen to often look hex, which is what led to that
        // wrong assumption here originally), e.g. "VSR: SIREN WAIL" — pull
        // that out before parsing the command, both to feed the whitelist
        // check below and so the SIREN prefix match isn't thrown off by a
        // leading "VSR: ". If no such pattern is found (e.g. a bare line
        // with no prefix at all), senderId stays empty and the whitelist
        // check below will reject it.
        char senderId[9] = {0};
        char* body = line;
        char* colon = strchr(line, ':');
        if (colon && colon > line && (size_t)(colon - line) < sizeof(senderId)) {
            size_t idLen = (size_t)(colon - line);
            memcpy(senderId, line, idLen);
            senderId[idLen] = '\0';
            body = colon + 1;
            while (*body == ' ') body++;
        }

        // Any line at all proves the node is powered and its serial link to
        // this board works — recorded before the addressing checks below,
        // since unrelated channel chatter counts just as much.
        meshHealth.lastRxMs = millis();
        strlcpy(meshHealth.lastSender, senderId, sizeof(meshHealth.lastSender));

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

        // Addressed to us, whitelisted, and password-correct — confirm
        // receipt with a quick LED double-flash regardless of what happens
        // next (busy/test-mode rejection, etc.).
        sm.flashMeshAck();

        bool testBlocked = buttons.testModeActive;

        if (!strcmp(cmd, "WAIL") || !strcmp(cmd, "ATTACK") || !strcmp(cmd, "FASTWAIL") || !strcmp(cmd, "GROWL")) {
            if (testBlocked) { reply("ERR: test mode active"); return; }
            RunMode m = !strcmp(cmd, "WAIL")     ? RunMode::WAIL :
                        !strcmp(cmd, "ATTACK")   ? RunMode::ATTACK :
                        !strcmp(cmd, "FASTWAIL") ? RunMode::FAST_WAIL : RunMode::GROWL;
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
        } else if (!strcmp(cmd, "WEATHERON")) {
            settingsMgr.s.weatherAutoTriggerEnabled = true;
            settingsMgr.save();
            // "WEATHER TRIGGER ENABLED" is broadcast generically in update()
            // whenever the setting changes, covering this too.
        } else if (!strcmp(cmd, "WEATHEROFF")) {
            settingsMgr.s.weatherAutoTriggerEnabled = false;
            settingsMgr.save();
            // "WEATHER TRIGGER DISABLED" covers this the same way.
        } else if (!strcmp(cmd, "REBOOT")) {
            reply("OK: rebooting");
            Serial2.flush();
            delay(100);
            ESP.restart();
        } else if (!strcmp(cmd, "WXREBOOT")) {
            // Fire-and-forget over the dedicated UART link — see
            // weather_link.h's requestReboot(). No confirmation comes back
            // (the Weather Watcher has no mesh connectivity of its own), so
            // this just confirms the request was sent, not that it landed.
            weatherLink.requestReboot();
            reply("OK: weather watcher reboot requested");
        } else if (!strcmp(cmd, "PING")) {
            reply(buildPingReply());
            meshHealth.lastPingMs = millis();
        } else {
            reply("ERR: unknown command");
        }
    }
};

extern MeshBridge meshBridge;
