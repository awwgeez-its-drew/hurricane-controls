#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include "config.h"
#include "settings.h"

// Polls api.weather.gov for active alerts covering settingsMgr.s.latitude/
// longitude, and sends a "WX <MODE>" command over the dedicated UART link to
// the Hurricane Controls main board when a NEW alert first qualifies as:
//
//   - Tornado Warning, with tornadoDetection "OBSERVED" (radar/spotter-
//     confirmed) OR any tornadoDamageThreat tag present at all
//     (CONSIDERABLE/CATASTROPHIC — there's no separate structured field for
//     "PDS" or "Tornado Emergency"; CATASTROPHIC is specifically reserved
//     for Tornado Emergency-tier events, and this tag only appears on the
//     most serious warnings, so it's the best available machine-readable
//     proxy), or
//   - Severe Thunderstorm Warning, with thunderstormDamageThreat
//     "CONSIDERABLE" or "DESTRUCTIVE".
//
// Both product types always carry a real storm-based polygon (not a county
// fallback), and api.weather.gov's point= query matches against that actual
// polygon — so no local point-in-polygon math is needed here.
//
// Any HTTP/TLS/JSON failure is treated as "no qualifying alert this cycle" —
// this never synthesizes a trigger from a failed or malformed response.
// De-duplicated by alert id so a still-active/extended warning doesn't
// re-trigger every poll; a genuinely new alert id does.
class NwsClient {
public:
    void begin() {
        Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
        pinMode(STATUS_LED, OUTPUT);
    }

    void update() {
        readReplies();
        uint32_t now = millis();
        uint32_t intervalMs = settingsMgr.s.pollIntervalSec * 1000UL;
        if (now - lastPollTs_ < intervalMs) return;
        lastPollTs_ = now;
        poll();
    }

private:
    uint32_t lastPollTs_ = 0;

    static constexpr uint8_t SEEN_CAP = 10;
    String  seenIds_[SEEN_CAP];
    uint8_t seenHead_ = 0;

    bool alreadySeen(const String& id) {
        for (uint8_t i = 0; i < SEEN_CAP; i++) if (seenIds_[i] == id) return true;
        return false;
    }

    void remember(const String& id) {
        seenIds_[seenHead_] = id;
        seenHead_ = (seenHead_ + 1) % SEEN_CAP;
    }

    void poll() {
        if (WiFi.status() != WL_CONNECTED) return;
        if (settingsMgr.s.latitude == 0.0f && settingsMgr.s.longitude == 0.0f) return; // not configured yet

        char url[128];
        snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f",
                 settingsMgr.s.latitude, settingsMgr.s.longitude);

        WiFiClientSecure client;
        client.setInsecure(); // no cert pinning — see docs/weather-watcher.md
        HTTPClient http;
        if (!http.begin(client, url)) return;
        String ua = String("(HurricaneControlsWeatherWatcher, ") + settingsMgr.s.userAgentContact + ")";
        http.addHeader("User-Agent", ua);
        http.addHeader("Accept", "application/geo+json");

        int code = http.GET();
        if (code != 200) { http.end(); return; }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return;

        JsonArray features = doc["features"].as<JsonArray>();
        for (JsonObject feature : features) {
            JsonObject props = feature["properties"];
            const char* id    = props["id"]    | "";
            const char* event = props["event"] | "";
            if (!*id || !*event) continue;

            const char* mode = qualifyingMode(event, props["parameters"]);
            if (!mode) continue;

            String sid(id);
            if (alreadySeen(sid)) continue;
            remember(sid);
            sendTrigger(mode);
        }
    }

    const char* qualifyingMode(const char* event, JsonObject params) {
        if (!strcmp(event, "Tornado Warning")) {
            const char* detection = params["tornadoDetection"][0] | "";
            bool hasDamageTag = params["tornadoDamageThreat"][0].is<const char*>();
            if (!strcmp(detection, "OBSERVED") || hasDamageTag) return settingsMgr.s.tornadoMode;
        } else if (!strcmp(event, "Severe Thunderstorm Warning")) {
            const char* damage = params["thunderstormDamageThreat"][0] | "";
            if (!strcmp(damage, "CONSIDERABLE") || !strcmp(damage, "DESTRUCTIVE")) return settingsMgr.s.thunderstormMode;
        }
        return nullptr;
    }

    void sendTrigger(const char* mode) {
        Serial.print("Qualifying alert — sending WX ");
        Serial.println(mode);
        digitalWrite(STATUS_LED, HIGH);
        Serial1.print("WX ");
        Serial1.println(mode);
    }

    // Logs (and briefly flashes off) on the main board's ack/err reply, sent
    // back over the same link — purely informational here, no retry logic.
    void readReplies() {
        while (Serial1.available()) {
            String line = Serial1.readStringUntil('\n');
            line.trim();
            if (line.length()) {
                Serial.print("Main board reply: ");
                Serial.println(line);
                digitalWrite(STATUS_LED, LOW);
            }
        }
    }
};

extern NwsClient nwsClient;
