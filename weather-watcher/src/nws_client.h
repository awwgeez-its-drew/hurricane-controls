#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>
#include "config.h"
#include "sync.h"
#include "settings.h"
#include "other_extreme_types.h"

// Alert categories for display (Current Alerts card) and for deciding
// whether/when to trigger the main board. Numeric order matters: higher
// value = more severe, used both for sort order and for escalation
// comparisons (settingsMgr.s.repeatOnUpgrade re-triggers only on a move to a
// strictly higher category for the same tracked warning).
enum class AlertCategory : uint8_t {
    NONE = 0,
    THUNDERSTORM_BASE,
    OTHER_EXTREME,
    THUNDERSTORM_CONSIDERABLE,
    TORNADO_UNCONFIRMED,
    THUNDERSTORM_DESTRUCTIVE,
    TORNADO_CONFIRMED,
    TORNADO_EMERGENCY,
};

inline const char* categoryColor(AlertCategory c) {
    switch (c) {
    case AlertCategory::TORNADO_EMERGENCY:
    case AlertCategory::TORNADO_CONFIRMED:
    case AlertCategory::OTHER_EXTREME:        return "purple";
    case AlertCategory::TORNADO_UNCONFIRMED:
    case AlertCategory::THUNDERSTORM_DESTRUCTIVE:
    case AlertCategory::THUNDERSTORM_CONSIDERABLE: return "red";
    case AlertCategory::THUNDERSTORM_BASE:    return "orange";
    default:                                  return "";
    }
}

// One alert currently in the active feed that's severe enough to display.
// Rebuilt from scratch on every poll — an alert that's no longer in the
// NWS feed simply stops appearing here on the next cycle.
struct CurrentAlert {
    String id;            // full api.weather.gov URL — used as the "view on NWS" link
    String event;         // e.g. "Tornado Warning", "Fire Warning"
    String headline;      // NWS-provided headline, falls back to event name
    String areaDesc;
    AlertCategory category = AlertCategory::NONE;
    bool   triggeredSiren = false;
};

struct RecentTrigger {
    String   event;
    String   mode;
    uint32_t epoch = 0;  // 0 = time wasn't synced yet when this fired
};

// Polls api.weather.gov for active alerts covering settingsMgr.s.latitude/
// longitude, classifies each into an AlertCategory, and sends a "WX <MODE>"
// command over the dedicated UART link to the Hurricane Controls main board
// the first time a tracked warning event crosses that category's configured
// mode (anything other than "OFF"):
//
//   - Tornado Warning: CATASTROPHIC damage tag -> TORNADO_EMERGENCY;
//     tornadoDetection "OBSERVED" or any damage tag at all -> TORNADO_CONFIRMED
//     (there's no separate structured "PDS" field, so "any damage-threat tag"
//     is the proxy); otherwise -> TORNADO_UNCONFIRMED.
//   - Severe Thunderstorm Warning: DESTRUCTIVE damage tag -> THUNDERSTORM_DESTRUCTIVE;
//     CONSIDERABLE -> THUNDERSTORM_CONSIDERABLE; otherwise -> THUNDERSTORM_BASE.
//   - Anything else: only classified as OTHER_EXTREME if severity=="Extreme" AND
//     urgency=="Immediate" AND certainty=="Observed" AND its event type has been
//     explicitly opted into settingsMgr.s.otherExtremeIncluded (see
//     other_extreme_types.h) — an unlisted or not-opted-in event type is never
//     classified at all, however severe, so e.g. a Flood Warning stays silent
//     for an owner who isn't in a flood-prone area unless they add it themselves.
//
// Each of the 7 non-NONE categories maps 1:1 to its own configurable mode
// field in Settings (OFF/WAIL/ATTACK/FASTWAIL) — a category only ever
// triggers if its mode isn't OFF.
//
// Active-alerts-only safeguard: an alert is skipped entirely unless
// properties.status=="Actual" and properties.messageType isn't "Cancel" or
// "Error" — a defensive check against a stale/historical/test record. This
// is a status/messageType check only, not a full ISO8601 `expires` timestamp
// comparison — a deliberate, documented simplification (see docs/weather-watcher.md).
//
// De-duplication/escalation tracking is keyed by the warning's stable VTEC
// event identifier (office + phenomena + significance + ETN, parsed from
// properties.parameters.VTEC), not by properties.id — NWS issues a brand
// new id for every single update to an ongoing warning (continuations,
// upgrades, cancellations), even though it's still fundamentally the same
// warning event. Tracking by VTEC key means: a warning only ever triggers
// once by default, and — if settingsMgr.s.repeatOnUpgrade is enabled — can
// trigger again later if it escalates to a strictly higher category (e.g. a
// confirmed Tornado Warning that's later upgraded to a Tornado Emergency).
class NwsClient {
public:
    void begin() {
        Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
        pinMode(STATUS_LED, OUTPUT);
    }

    void update() {
        readReplies();
        checkLinkTestTimeout();

        // Pushed to the main board independently of the NWS poll cadence
        // (which can be as slow as minutes) so a WiFi drop shows up there
        // reasonably quickly. The first push goes out on the very first
        // update() call.
        uint32_t now = millis();
        if (!statusPushedOnce_ || now - lastStatusPushTs_ >= STATUS_PUSH_INTERVAL_MS) {
            statusPushedOnce_ = true;
            lastStatusPushTs_ = now;
            sendStatusToController();
        }

        // First poll right away at boot (and right after a late Wi-Fi join —
        // see requestPollSoon()) rather than a full poll interval later.
        uint32_t intervalMs = settingsMgr.s.pollIntervalSec * 1000UL;
        if (!pollSoon_ && everPolled_ && now - lastPollTs_ < intervalMs) return;
        pollSoon_   = false;
        lastPollTs_ = now;
        poll();

        // Report the fresh result to the main board immediately, so it hears
        // the first real OK/ERROR within seconds of the first poll (its boot
        // announcement waits for exactly that).
        lastStatusPushTs_ = millis();
        sendStatusToController();
    }

    // Called from main.cpp when Wi-Fi (re)joins the home network.
    void requestPollSoon() { pollSoon_ = true; }

    // Sends "WX PING" over the dedicated UART link and waits (non-blocking,
    // checked in update()) for the main board's "OK: pong" reply. Called
    // once at startup and on-demand from the dashboard's Controller Link
    // status row. A request while one is already in flight is ignored
    // rather than queued — the in-flight one will resolve within
    // LINK_TEST_TIMEOUT_MS either way.
    void requestLinkTest() {
        CtrlLock lock;  // also called from the web server's task
        if (linkTestInProgress_) return;
        linkTestInProgress_ = true;
        linkTestSentMs_     = millis();
        Serial1.println("WX PING");
    }

    // ── Status accessors (for the dashboard's /status-data endpoint) ───────
    bool   everPolled()      const { return everPolled_; }
    bool   lastPollSuccess() const { return lastPollSuccess_; }
    String lastPollError()   const { return lastPollError_; }
    uint32_t lastPollEpoch() const { return lastPollEpoch_; }
    bool   locationConfigured() const { return settingsMgr.s.latitude != 0.0f || settingsMgr.s.longitude != 0.0f; }

    bool   everTestedLink()    const { return everTestedLink_; }
    bool   linkTestInProgress() const { return linkTestInProgress_; }
    bool   linkOk()            const { return linkOk_; }
    String linkDetail()        const { return linkDetail_; }

    uint8_t currentAlertCount() const { return currentAlertCount_; }
    const CurrentAlert& currentAlert(uint8_t i) const { return currentAlerts_[i]; }

    uint8_t recentTriggerCount() const { return recentCount_; }
    const RecentTrigger& recentTrigger(uint8_t i) const { return recent_[i]; }

private:
    uint32_t lastPollTs_ = 0;

    bool     pollSoon_         = false;
    bool     polledOnline_     = false;  // a poll has actually reached the network

    // ── Periodic "WX STATUS <OK|ERROR|PENDING> <detail>" push to the main board
    uint32_t lastStatusPushTs_ = 0;
    bool     statusPushedOnce_ = false;
    static constexpr uint32_t STATUS_PUSH_INTERVAL_MS = 30000; // 30s
    // While Wi-Fi hasn't come up yet after boot (e.g. router still booting
    // after a power cut), report PENDING rather than ERROR for this long.
    static constexpr uint32_t STARTUP_GRACE_MS = 120000;

    // ── Non-blocking line reader for main-board replies ─────────────────────
    char    rxLine_[96];
    uint8_t rxLen_ = 0;

    bool     everPolled_       = false;
    bool     lastPollSuccess_  = false;
    String   lastPollError_;
    uint32_t lastPollEpoch_    = 0;

    // ── Controller link test (WX PING / OK: pong) ───────────────────────────
    bool     everTestedLink_    = false;
    bool     linkTestInProgress_ = false;
    uint32_t linkTestSentMs_    = 0;
    bool     linkOk_            = false;
    String   linkDetail_        = "Not tested yet";
    static constexpr uint32_t LINK_TEST_TIMEOUT_MS = 3000;

    // ── Current Alerts (rebuilt every poll) ─────────────────────────────────
    static constexpr uint8_t MAX_CURRENT = 8;
    static constexpr unsigned MAX_TEXT_LEN = 160;   // headline/areaDesc cap per alert
    CurrentAlert currentAlerts_[MAX_CURRENT];
    uint8_t      currentAlertCount_ = 0;

    // ── Recent triggers (most-recent-first, fixed size 5) ───────────────────
    static constexpr uint8_t MAX_RECENT = 5;
    RecentTrigger recent_[MAX_RECENT];
    uint8_t       recentCount_ = 0;

    // ── Tracked warning events, keyed by VTEC office.phenomena.sig.etn ──────
    struct TrackedEvent {
        char     vtecKey[20] = {0};
        uint8_t  lastActedCategory = 0;  // AlertCategory::NONE — highest category ever acted on (triggered) for this event
        uint32_t lastSeenMs = 0;
        bool     used = false;
    };
    static constexpr uint8_t MAX_TRACKED = 16;
    TrackedEvent tracked_[MAX_TRACKED];

    TrackedEvent* findOrCreateTracked(const char* key, uint32_t now) {
        TrackedEvent* oldest = nullptr;
        for (auto& e : tracked_) {
            if (e.used && !strcmp(e.vtecKey, key)) return &e;
            if (!oldest || e.lastSeenMs < oldest->lastSeenMs) oldest = &e;
        }
        for (auto& e : tracked_) {
            if (!e.used) {
                strlcpy(e.vtecKey, key, sizeof(e.vtecKey));
                e.used = true;
                e.lastActedCategory = 0;
                return &e;
            }
        }
        // Table full (>16 concurrent tracked events for one point — should
        // never happen in practice) — evict the least-recently-seen entry.
        strlcpy(oldest->vtecKey, key, sizeof(oldest->vtecKey));
        oldest->lastActedCategory = 0;
        return oldest;
    }

    // Extracts "office.phenomena.significance.etn" from a raw P-VTEC string
    // like "/O.NEW.KOUN.TO.W.0123.2601010000Z-2601010100Z/". This stays
    // constant across a warning's entire lifecycle (NEW/CON/EXT/UPG/CAN),
    // unlike properties.id which changes on every single update.
    static bool parseVtecKey(const char* vtec, char* out, size_t outSize) {
        if (!vtec || !*vtec) return false;
        char buf[96];
        strlcpy(buf, vtec, sizeof(buf));
        size_t len = strlen(buf);
        if (len && buf[0] == '/') memmove(buf, buf + 1, len--);
        if (len && buf[len - 1] == '/') buf[len - 1] = '\0';

        const char* office = nullptr;
        const char* phen    = nullptr;
        const char* sig     = nullptr;
        const char* etn     = nullptr;
        int idx = 0;
        char* saveptr = nullptr;
        char* tok = strtok_r(buf, ".", &saveptr);
        while (tok) {
            if      (idx == 2) office = tok;
            else if (idx == 3) phen   = tok;
            else if (idx == 4) sig    = tok;
            else if (idx == 5) etn    = tok;
            idx++;
            tok = strtok_r(nullptr, ".", &saveptr);
        }
        if (!office || !phen || !sig || !etn) return false;
        snprintf(out, outSize, "%s.%s.%s.%s", office, phen, sig, etn);
        return true;
    }

    static AlertCategory classify(const char* event, JsonObject params,
                                   const char* severity, const char* urgency, const char* certainty) {
        if (!strcmp(event, "Tornado Warning")) {
            const char* detection = params["tornadoDetection"][0] | "";
            const char* damage    = params["tornadoDamageThreat"][0] | "";
            bool hasDamageTag = params["tornadoDamageThreat"][0].is<const char*>();
            if (!strcmp(damage, "CATASTROPHIC")) return AlertCategory::TORNADO_EMERGENCY;
            if (!strcmp(detection, "OBSERVED") || hasDamageTag) return AlertCategory::TORNADO_CONFIRMED;
            return AlertCategory::TORNADO_UNCONFIRMED;
        }
        if (!strcmp(event, "Severe Thunderstorm Warning")) {
            const char* damage = params["thunderstormDamageThreat"][0] | "";
            bool hasDamageTag = params["thunderstormDamageThreat"][0].is<const char*>();
            if (!strcmp(damage, "DESTRUCTIVE")) return AlertCategory::THUNDERSTORM_DESTRUCTIVE;
            if (hasDamageTag) return AlertCategory::THUNDERSTORM_CONSIDERABLE;
            return AlertCategory::THUNDERSTORM_BASE;
        }
        if (!strcmp(severity, "Extreme") && !strcmp(urgency, "Immediate") && !strcmp(certainty, "Observed")) {
            const char* code = otherExtremeCodeForEvent(event);
            if (code && otherExtremeCodeIncluded(settingsMgr.s.otherExtremeIncluded, code))
                return AlertCategory::OTHER_EXTREME;
        }
        return AlertCategory::NONE;
    }

    static const char* modeForCategory(AlertCategory c) {
        switch (c) {
        case AlertCategory::TORNADO_UNCONFIRMED:        return settingsMgr.s.tornadoUnconfirmedMode;
        case AlertCategory::TORNADO_CONFIRMED:          return settingsMgr.s.tornadoConfirmedMode;
        case AlertCategory::TORNADO_EMERGENCY:          return settingsMgr.s.tornadoEmergencyMode;
        case AlertCategory::THUNDERSTORM_BASE:          return settingsMgr.s.thunderstormBaseMode;
        case AlertCategory::THUNDERSTORM_CONSIDERABLE:  return settingsMgr.s.thunderstormConsiderableMode;
        case AlertCategory::THUNDERSTORM_DESTRUCTIVE:   return settingsMgr.s.thunderstormDestructiveMode;
        case AlertCategory::OTHER_EXTREME:              return settingsMgr.s.otherExtremeMode;
        default:                                        return "OFF";
        }
    }

    void setPollResult(bool ok, const String& err) {
        CtrlLock lock;
        lastPollEpoch_   = nowEpoch();
        everPolled_      = true;
        lastPollSuccess_ = ok;
        lastPollError_   = err;
    }

    static String capped(const char* str) {
        String out(str);
        if (out.length() > MAX_TEXT_LEN) out = out.substring(0, MAX_TEXT_LEN - 3) + "...";
        return out;
    }

    void poll() {
        if (WiFi.status() != WL_CONNECTED) { setPollResult(false, "Wi-Fi not connected"); return; }

        // Snapshot what this poll needs — the web server can rewrite
        // settings from its own task at any moment.
        char url[128];
        String ua;
        bool located;
        {
            CtrlLock lock;
            located = locationConfigured();
            snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f",
                     settingsMgr.s.latitude, settingsMgr.s.longitude);
            ua = String("(HurricaneControlsWeatherWatcher, ") + settingsMgr.s.userAgentContact + ")";
        }
        if (!located) {
            setPollResult(false, "Location not configured — set latitude/longitude in Alert Location");
            return;
        }
        polledOnline_ = true;

        WiFiClientSecure client;
        client.setInsecure(); // no cert pinning — see docs/weather-watcher.md
        HTTPClient http;
        if (!http.begin(client, url)) { setPollResult(false, "Could not start HTTPS request"); return; }
        http.useHTTP10(true);          // no chunked encoding — getStream() is then the raw JSON body
        http.setConnectTimeout(10000);
        http.setTimeout(10000);
        http.addHeader("User-Agent", ua);
        http.addHeader("Accept", "application/geo+json");

        int code = http.GET();
        if (code != 200) {
            setPollResult(false, (code > 0)
                ? ("HTTP " + String(code) + " from api.weather.gov")
                : ("Connection failed (error " + String(code) + ") — check Wi-Fi/internet"));
            http.end();
            return;
        }

        // Keep only the fields classify()/the dashboard use. Each alert's
        // description/instruction text alone can run to kilobytes, and an
        // outbreak can list dozens of alerts — unfiltered, the document could
        // exhaust the heap on top of the ~40KB the TLS session already holds.
        JsonDocument filter;
        static const char* const PROPS[] = {"id", "event", "status", "messageType", "severity",
                                            "urgency", "certainty", "headline", "areaDesc"};
        static const char* const PARAMS[] = {"VTEC", "tornadoDetection", "tornadoDamageThreat",
                                             "thunderstormDamageThreat"};
        for (const char* k : PROPS)  filter["features"][0]["properties"][k] = true;
        for (const char* k : PARAMS) filter["features"][0]["properties"]["parameters"][k] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        http.end();
        if (err) {
            setPollResult(false, String("Failed to parse response: ") + err.c_str());
            return;
        }

        // Everything below is quick (no network) — hold the lock so the
        // dashboard never reads the alert lists mid-rewrite.
        CtrlLock lock;
        lastPollEpoch_   = nowEpoch();
        everPolled_      = true;
        lastPollSuccess_ = true;
        lastPollError_   = "";

        uint32_t now = millis();
        CurrentAlert fresh[MAX_CURRENT];
        uint8_t freshCount = 0;

        JsonArray features = doc["features"].as<JsonArray>();
        for (JsonObject feature : features) {
            JsonObject props = feature["properties"];
            const char* id     = props["id"]          | "";
            const char* event  = props["event"]       | "";
            const char* status = props["status"]      | "";
            const char* msgType = props["messageType"] | "";
            if (!*id || !*event) continue;

            // Active-alerts-only safeguard: never act on a non-current record.
            if (strcmp(status, "Actual") != 0) continue;
            if (!strcmp(msgType, "Cancel") || !strcmp(msgType, "Error")) continue;

            const char* severity = props["severity"]  | "";
            const char* urgency  = props["urgency"]   | "";
            const char* certainty = props["certainty"] | "";

            JsonObject params = props["parameters"];
            AlertCategory category = classify(event, params, severity, urgency, certainty);
            if (category == AlertCategory::NONE) continue;

            char key[20];
            if (!parseVtecKey(params["VTEC"][0] | "", key, sizeof(key)))
                strlcpy(key, id, sizeof(key)); // fallback if VTEC is ever missing

            TrackedEvent* te = findOrCreateTracked(key, now);
            te->lastSeenMs = now;

            const char* mode = modeForCategory(category);
            uint8_t categoryVal = (uint8_t)category;
            if (strcmp(mode, "OFF") != 0) {
                bool firstTime = te->lastActedCategory == 0;
                bool escalated = settingsMgr.s.repeatOnUpgrade &&
                                  te->lastActedCategory > 0 &&
                                  categoryVal > te->lastActedCategory;
                if (firstTime || escalated) {
                    te->lastActedCategory = categoryVal;
                    sendTrigger(mode);
                    recordRecentTrigger(event, mode);
                }
            }

            if (freshCount < MAX_CURRENT) {
                CurrentAlert& ca = fresh[freshCount++];
                ca.id       = id;
                ca.event    = event;
                ca.headline = capped(props["headline"].is<const char*>() ? (const char*)props["headline"] : event);
                ca.areaDesc = capped(props["areaDesc"].is<const char*>() ? (const char*)props["areaDesc"] : "");
                ca.category = category;
                ca.triggeredSiren = (te->lastActedCategory > 0);
            }
        }

        // Sort by severity, highest first (simple insertion sort — at most
        // MAX_CURRENT=8 entries, no need for anything fancier).
        for (uint8_t i = 1; i < freshCount; i++) {
            CurrentAlert key = fresh[i];
            int j = i - 1;
            while (j >= 0 && (uint8_t)fresh[j].category < (uint8_t)key.category) {
                fresh[j + 1] = fresh[j];
                j--;
            }
            fresh[j + 1] = key;
        }
        for (uint8_t i = 0; i < freshCount; i++) currentAlerts_[i] = fresh[i];
        currentAlertCount_ = freshCount;
    }

    void recordRecentTrigger(const char* event, const char* mode) {
        for (int i = MAX_RECENT - 1; i > 0; i--) recent_[i] = recent_[i - 1];
        recent_[0].event = event;
        recent_[0].mode  = mode;
        recent_[0].epoch = nowEpoch();
        if (recentCount_ < MAX_RECENT) recentCount_++;
    }

    void sendTrigger(const char* mode) {
        Serial.print("Qualifying alert — sending WX ");
        Serial.println(mode);
        digitalWrite(STATUS_LED, HIGH);
        Serial1.print("WX ");
        Serial1.println(mode);
    }

    // Unprompted, fire-and-forget push of this board's own WiFi/NWS API
    // health, so the main board's Settings page can show something more
    // useful than just "the link is alive" — mirrors the exact classification
    // already used for the dashboard's own status lights (see buildStatusJson()
    // in webserver.h), condensed into one line. Truncated well under the main
    // board's line buffer, since wifiDetail/lastPollError_ can run long.
    //
    // PENDING ("still starting up, nothing to report yet") is sent until a
    // poll has actually reached the network — the main board holds its
    // mesh boot announcement until it hears a real OK/ERROR (or gives up
    // after 2 minutes). An older main board just shows PENDING as an error
    // for those few seconds.
    //
    // Also pushes this board's NTP-synced clock as "WX TIME <epoch>", which
    // the main board uses for its run log when it has no NTP of its own.
    void sendStatusToController() {
        bool wifiOk = WiFi.status() == WL_CONNECTED;
        const char* state;
        String detail;
        if (!wifiOk && !polledOnline_ && millis() < STARTUP_GRACE_MS) { state = "PENDING"; detail = "Connecting to Wi-Fi"; }
        else if (!wifiOk)            { state = "ERROR";   detail = "WiFi not connected"; }
        else if (!polledOnline_)     { state = "PENDING"; detail = "Waiting for first poll"; }
        else if (!lastPollSuccess_)  { state = "ERROR";   detail = lastPollError_; }
        else                         { state = "OK";      detail = "All systems normal"; }
        if (detail.length() > 48) detail = detail.substring(0, 48);
        Serial1.print("WX STATUS ");
        Serial1.print(state);
        Serial1.print(' ');
        Serial1.println(detail);

        uint32_t t = nowEpoch();
        if (t) {
            Serial1.print("WX TIME ");
            Serial1.println(t);
        }
    }

    // Non-blocking: readStringUntil() would stall the loop for up to a
    // second whenever a partial line was sitting in the buffer.
    void readReplies() {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n' || c == '\r') {
                if (rxLen_ > 0) {
                    rxLine_[rxLen_] = '\0';
                    rxLen_ = 0;
                    handleReply(rxLine_);
                }
            } else if (rxLen_ < sizeof(rxLine_) - 1) {
                rxLine_[rxLen_++] = c;
            }
        }
    }

    void handleReply(char* line) {
        while (*line == ' ') line++;
        size_t len = strlen(line);
        while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
        if (!len) return;
        Serial.print("Main board reply: ");
        Serial.println(line);
        digitalWrite(STATUS_LED, LOW);
        if (!strcasecmp(line, "OK: pong")) {
            CtrlLock lock;
            if (linkTestInProgress_) {
                linkTestInProgress_ = false;
                everTestedLink_     = true;
                linkOk_             = true;
                linkDetail_         = "Status OK";
            }
        }
    }

    void checkLinkTestTimeout() {
        CtrlLock lock;
        if (!linkTestInProgress_) return;
        if (millis() - linkTestSentMs_ < LINK_TEST_TIMEOUT_MS) return;
        linkTestInProgress_ = false;
        everTestedLink_     = true;
        linkOk_             = false;
        linkDetail_         = "No response from main board — check wiring/baud rate (UART1, pins 16/17 by default)";
    }

    static uint32_t nowEpoch() {
        time_t t = time(nullptr);
        return (t > 1000000000UL) ? (uint32_t)t : 0; // 0 = not yet NTP-synced
    }
};

extern NwsClient nwsClient;
