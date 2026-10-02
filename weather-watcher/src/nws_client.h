#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>
#include "config.h"
#include "settings.h"

// Severity tiers for display (Current Alerts card) and for deciding whether/
// when to trigger the main board. Numeric order matters: higher value =
// more severe, used both for sort order and for escalation comparisons.
enum class Tier : uint8_t { IGNORED = 0, ORANGE = 1, RED = 2, PURPLE_CONFIRMED = 3, PURPLE_EMERGENCY = 4 };

inline const char* tierColor(Tier t) {
    switch (t) {
    case Tier::PURPLE_EMERGENCY:
    case Tier::PURPLE_CONFIRMED: return "purple";
    case Tier::RED:              return "red";
    case Tier::ORANGE:           return "orange";
    default:                     return "";
    }
}

// One alert currently in the active feed that's severe enough to display.
// Rebuilt from scratch on every poll — an alert that's no longer in the
// NWS feed simply stops appearing here on the next cycle.
struct CurrentAlert {
    String id;            // full api.weather.gov URL — used as the "view on NWS" link
    String event;         // "Tornado Warning" / "Severe Thunderstorm Warning"
    String headline;      // NWS-provided headline, falls back to event name
    String areaDesc;
    Tier   tier = Tier::IGNORED;
    bool   triggeredSiren = false;
};

struct RecentTrigger {
    String   event;
    String   mode;
    uint32_t epoch = 0;  // 0 = time wasn't synced yet when this fired
};

// Polls api.weather.gov for active alerts covering settingsMgr.s.latitude/
// longitude, classifies each into a severity Tier, and sends a "WX <MODE>"
// command over the dedicated UART link to the Hurricane Controls main board
// the first time a tracked warning event crosses its activation threshold:
//
//   - Tornado Warning reaches PURPLE_CONFIRMED (tornadoDetection "OBSERVED",
//     i.e. radar/spotter-confirmed, or any tornadoDamageThreat tag at all —
//     CONSIDERABLE/CATASTROPHIC; there's no separate structured field for
//     "PDS", so "any damage-threat tag" is the proxy) or higher
//     (PURPLE_EMERGENCY = CATASTROPHIC specifically, reserved for Tornado
//     Emergency-tier events).
//   - Severe Thunderstorm Warning reaches RED (thunderstormDamageThreat
//     "CONSIDERABLE" or "DESTRUCTIVE").
//
// Plain/unconfirmed Tornado Warnings (RED) and base Severe Thunderstorm
// Warnings with no damage tag (ORANGE) are shown in Current Alerts for
// situational awareness but never trigger the siren — only the two
// thresholds above do.
//
// De-duplication/escalation tracking is keyed by the warning's stable VTEC
// event identifier (office + phenomena + significance + ETN, parsed from
// properties.parameters.VTEC), not by properties.id — NWS issues a brand
// new id for every single update to an ongoing warning (continuations,
// upgrades, cancellations), even though it's still fundamentally the same
// warning event. Tracking by VTEC key means: a warning only ever triggers
// once by default, and — if settingsMgr.s.repeatOnUpgrade is enabled — can
// trigger again later if it escalates further (e.g. a confirmed Tornado
// Warning that's later upgraded to a Tornado Emergency).
class NwsClient {
public:
    void begin() {
        Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
        pinMode(STATUS_LED, OUTPUT);
    }

    void update() {
        readReplies();
        checkLinkTestTimeout();
        uint32_t now = millis();
        uint32_t intervalMs = settingsMgr.s.pollIntervalSec * 1000UL;
        if (now - lastPollTs_ < intervalMs) return;
        lastPollTs_ = now;
        poll();
    }

    // Sends "WX PING" over the dedicated UART link and waits (non-blocking,
    // checked in update()) for the main board's "OK: pong" reply. Called
    // once at startup and on-demand from the dashboard's Controller Link
    // status row. A request while one is already in flight is ignored
    // rather than queued — the in-flight one will resolve within
    // LINK_TEST_TIMEOUT_MS either way.
    void requestLinkTest() {
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
    CurrentAlert currentAlerts_[MAX_CURRENT];
    uint8_t      currentAlertCount_ = 0;

    // ── Recent triggers (most-recent-first, fixed size 5) ───────────────────
    static constexpr uint8_t MAX_RECENT = 5;
    RecentTrigger recent_[MAX_RECENT];
    uint8_t       recentCount_ = 0;

    // ── Tracked warning events, keyed by VTEC office.phenomena.sig.etn ──────
    struct TrackedEvent {
        char     vtecKey[20] = {0};
        uint8_t  lastActedTier = 0;  // Tier::IGNORED — highest tier ever acted on (triggered) for this event
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
                e.lastActedTier = 0;
                return &e;
            }
        }
        // Table full (>16 concurrent tracked events for one point — should
        // never happen in practice) — evict the least-recently-seen entry.
        strlcpy(oldest->vtecKey, key, sizeof(oldest->vtecKey));
        oldest->lastActedTier = 0;
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

    static Tier classify(const char* event, JsonObject params) {
        if (!strcmp(event, "Tornado Warning")) {
            const char* detection = params["tornadoDetection"][0] | "";
            const char* damage    = params["tornadoDamageThreat"][0] | "";
            bool hasDamageTag = params["tornadoDamageThreat"][0].is<const char*>();
            if (!strcmp(damage, "CATASTROPHIC")) return Tier::PURPLE_EMERGENCY;
            if (!strcmp(detection, "OBSERVED") || hasDamageTag) return Tier::PURPLE_CONFIRMED;
            return Tier::RED;
        }
        if (!strcmp(event, "Severe Thunderstorm Warning")) {
            bool hasDamageTag = params["thunderstormDamageThreat"][0].is<const char*>();
            return hasDamageTag ? Tier::RED : Tier::ORANGE;
        }
        return Tier::IGNORED;
    }

    static Tier activationThreshold(const char* event) {
        if (!strcmp(event, "Tornado Warning")) return Tier::PURPLE_CONFIRMED;
        if (!strcmp(event, "Severe Thunderstorm Warning")) return Tier::RED;
        return Tier::IGNORED;
    }

    void poll() {
        lastPollEpoch_ = nowEpoch();
        everPolled_    = true;

        if (WiFi.status() != WL_CONNECTED) {
            lastPollSuccess_ = false;
            lastPollError_   = "Wi-Fi not connected";
            return;
        }
        if (!locationConfigured()) {
            lastPollSuccess_ = false;
            lastPollError_   = "Location not configured — set latitude/longitude in Alert Location";
            return;
        }

        char url[128];
        snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f",
                 settingsMgr.s.latitude, settingsMgr.s.longitude);

        WiFiClientSecure client;
        client.setInsecure(); // no cert pinning — see docs/weather-watcher.md
        HTTPClient http;
        if (!http.begin(client, url)) {
            lastPollSuccess_ = false;
            lastPollError_   = "Could not start HTTPS request";
            return;
        }
        String ua = String("(HurricaneControlsWeatherWatcher, ") + settingsMgr.s.userAgentContact + ")";
        http.addHeader("User-Agent", ua);
        http.addHeader("Accept", "application/geo+json");

        int code = http.GET();
        if (code != 200) {
            lastPollSuccess_ = false;
            lastPollError_   = (code > 0)
                ? ("HTTP " + String(code) + " from api.weather.gov")
                : ("Connection failed (error " + String(code) + ") — check Wi-Fi/internet");
            http.end();
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) {
            lastPollSuccess_ = false;
            lastPollError_   = String("Failed to parse response: ") + err.c_str();
            return;
        }

        lastPollSuccess_ = true;
        lastPollError_   = "";

        uint32_t now = millis();
        CurrentAlert fresh[MAX_CURRENT];
        uint8_t freshCount = 0;

        JsonArray features = doc["features"].as<JsonArray>();
        for (JsonObject feature : features) {
            JsonObject props = feature["properties"];
            const char* id    = props["id"]    | "";
            const char* event = props["event"] | "";
            if (!*id || !*event) continue;

            JsonObject params = props["parameters"];
            Tier tier = classify(event, params);
            if (tier == Tier::IGNORED) continue;

            char key[20];
            if (!parseVtecKey(params["VTEC"][0] | "", key, sizeof(key)))
                strlcpy(key, id, sizeof(key)); // fallback if VTEC is ever missing

            TrackedEvent* te = findOrCreateTracked(key, now);
            te->lastSeenMs = now;

            Tier threshold = activationThreshold(event);
            uint8_t tierVal = (uint8_t)tier, thresholdVal = (uint8_t)threshold;
            if (tierVal >= thresholdVal) {
                bool firstTime  = te->lastActedTier < thresholdVal;
                bool escalated  = settingsMgr.s.repeatOnUpgrade &&
                                   te->lastActedTier >= thresholdVal &&
                                   tierVal > te->lastActedTier;
                if (firstTime || escalated) {
                    te->lastActedTier = tierVal;
                    const char* mode = !strcmp(event, "Tornado Warning")
                        ? settingsMgr.s.tornadoMode : settingsMgr.s.thunderstormMode;
                    sendTrigger(mode);
                    recordRecentTrigger(event, mode);
                }
            }

            if (freshCount < MAX_CURRENT) {
                CurrentAlert& ca = fresh[freshCount++];
                ca.id       = id;
                ca.event    = event;
                ca.headline = props["headline"].is<const char*>() ? (const char*)props["headline"] : event;
                ca.areaDesc = props["areaDesc"].is<const char*>() ? (const char*)props["areaDesc"] : "";
                ca.tier     = tier;
                ca.triggeredSiren = (te->lastActedTier > 0);
            }
        }

        // Sort by severity, highest first (simple insertion sort — at most
        // MAX_CURRENT=8 entries, no need for anything fancier).
        for (uint8_t i = 1; i < freshCount; i++) {
            CurrentAlert key = fresh[i];
            int j = i - 1;
            while (j >= 0 && (uint8_t)fresh[j].tier < (uint8_t)key.tier) {
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

    void readReplies() {
        while (Serial1.available()) {
            String line = Serial1.readStringUntil('\n');
            line.trim();
            if (line.length()) {
                Serial.print("Main board reply: ");
                Serial.println(line);
                digitalWrite(STATUS_LED, LOW);
                if (linkTestInProgress_ && line.equalsIgnoreCase("OK: pong")) {
                    linkTestInProgress_ = false;
                    everTestedLink_     = true;
                    linkOk_             = true;
                    linkDetail_         = "Status OK";
                }
            }
        }
    }

    void checkLinkTestTimeout() {
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
