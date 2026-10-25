#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "settings.h"

// Push notifications via ntfy (https://ntfy.sh or a self-hosted instance) —
// WP4. A small, priority-ordered RAM queue (max 16); one POST in flight at
// a time, sent from flushPending() (called once per main.cpp loop() tick,
// right after nwsClient.update() — never concurrently with the NWS poll,
// since both run on the same single Arduino task and neither is called
// from inside the other). Skips (retries next tick) rather than sends if
// free heap is under 60KB, per ground rule 7.
//
// Cert verification (Decision A, default on): WiFiClientSecure::setCACert()
// with ISRG Root X1 embedded below — covers ntfy.sh and the overwhelming
// majority of self-hosted instances, which are almost always Let's
// Encrypt-issued. The "Verify certificate" setting is the explicit,
// visible opt-out (setInsecure()) for anything else (e.g. a self-signed
// local instance). The bearer token is write-only: never echoed back by
// /settings-data, never logged — see webserver.h's settings POST handler
// and buildSettingsJson().
static const char NTFY_ISRG_ROOT_X1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

class Ntfy {
public:
    // priority: ntfy's own 1 (min) - 5 (max/urgent) scale.
    void notify(uint8_t priority, const String& title, const String& message) {
        if (!settingsMgr.s.ntfyEnabled || !settingsMgr.s.ntfyTopic[0]) return;
        if (count_ >= MAX_QUEUED) {
            Serial.println("ntfy queue full — dropping lowest-priority pending message");
            dropLowestPriority();
        }
        // Simple priority-ordered insert (highest first) — small array,
        // insertion sort is plenty.
        uint8_t i = count_;
        while (i > 0 && queue_[i - 1].priority < priority) {
            queue_[i] = queue_[i - 1];
            i--;
        }
        queue_[i] = { priority, title, message };
        count_++;
    }

    // Called once per main.cpp loop() tick, right after nwsClient.update()
    // — never concurrently with the NWS poll (ground rule 7).
    void flushPending() {
        if (count_ == 0) return;
        if (ESP.getFreeHeap() < 60000) return; // skip this tick, retry next
        send(queue_[0]);
        for (uint8_t i = 1; i < count_; i++) queue_[i - 1] = queue_[i];
        count_--;
    }

    uint8_t queued() const { return count_; }
    bool    lastSendOk()   const { return lastSendOk_; }
    String  lastSendDetail() const { return lastSendDetail_; }

private:
    struct QueuedMsg { uint8_t priority = 3; String title; String message; };
    static constexpr uint8_t MAX_QUEUED = 16;
    QueuedMsg queue_[MAX_QUEUED];
    uint8_t   count_ = 0;
    bool      lastSendOk_ = false;
    String    lastSendDetail_ = "Not sent yet";

    void dropLowestPriority() {
        // queue_ is kept priority-sorted descending — the lowest is last.
        count_--;
    }

    void send(const QueuedMsg& m) {
        WiFiClientSecure client;
        if (settingsMgr.s.ntfyVerifyCert) client.setCACert(NTFY_ISRG_ROOT_X1);
        else                              client.setInsecure();

        String url = String(settingsMgr.s.ntfyServer) + "/" + settingsMgr.s.ntfyTopic;
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        if (!http.begin(client, url)) {
            lastSendOk_ = false;
            lastSendDetail_ = "Could not start HTTPS request";
            return;
        }
        http.addHeader("Title", m.title);
        http.addHeader("Priority", String(m.priority));
        if (settingsMgr.s.ntfyToken[0]) {
            http.addHeader("Authorization", String("Bearer ") + settingsMgr.s.ntfyToken);
        }
        int code = http.POST(m.message);
        if (code >= 200 && code < 300) {
            lastSendOk_ = true;
            lastSendDetail_ = "OK (" + String(code) + ")";
        } else {
            lastSendOk_ = false;
            lastSendDetail_ = code > 0 ? ("HTTP " + String(code)) : http.errorToString(code);
            Serial.printf("ntfy send failed: %s\n", lastSendDetail_.c_str());
        }
        http.end();
    }
};

extern Ntfy ntfy;
