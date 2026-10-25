#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Persistent, exportable record of every trigger-queue disposition this
// board reaches (WP1's finalizeQueueHead()) plus NOTIFY_ONLY crossings
// while auto-trigger is OFF — the Weather Watcher's half of WP3, parallel
// to the main board's RunLog (src/runlog.h). Same pattern: a small RAM
// ring buffer (CAPACITY=20) as a read cache for the dashboard, backed by a
// LittleFS JSON-lines file so the full history survives a reboot, and a
// deferred write (record() only touches RAM; flushPending(), called once
// per main.cpp loop() tick, does the actual flash write) so a slow write
// never stalls nwsClient.update()'s own work.
struct AlertLogEntry {
    uint32_t seq          = 0;
    uint32_t epoch        = 0;
    char     vtecKey[20]  = "";
    char     event[41]    = "";
    uint8_t  category     = 0;
    char     sirenMode[10] = "";
    char     disposition[24] = ""; // TRIGGERED / RETRIED_TRIGGERED / ALREADY_ACTIVE / AUTO_OFF /
                                   // BADCMD / GAVE_UP_<CODE> / NO_RESPONSE / NOTIFY_ONLY
    uint8_t  attempts     = 0;
    bool     escalation   = false; // a repeat-on-upgrade re-trigger, not this event's first crossing
    bool     notified     = false; // WP4 — set once an ntfy push has gone out for this entry
    char     detail[64]   = "";
};

class AlertLog {
public:
    static constexpr uint8_t  CAPACITY  = 20;
    static constexpr uint32_t MAX_LINES = 500;
    static constexpr const char* LOG_PATH = "/alertlog.jsonl";

    void begin() {
        fsOk_ = LittleFS.begin(true);
        if (!fsOk_) { Serial.println("AlertLog: LittleFS mount failed — RAM-only this boot"); return; }
        loadRecent();
    }

    bool fsOk() const { return fsOk_; }

    void record(const AlertLogEntry& eIn) {
        AlertLogEntry e = eIn;
        e.seq = ++nextSeq_;
        entries_[head_] = e;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;
        pending_      = e;
        pendingValid_ = true;
    }

    void flushPending() {
        if (!fsOk_ || !pendingValid_) return;
        appendEntry(pending_);
        pendingValid_ = false;
        trimIfNeeded();
    }

    uint8_t count() const { return count_; }
    // i = 0 is the most recent entry.
    const AlertLogEntry& recent(uint8_t i) const {
        return entries_[(head_ + CAPACITY - 1 - i) % CAPACITY];
    }

    File openForExport() {
        if (!fsOk_) return File();
        return LittleFS.open(LOG_PATH, "r");
    }

private:
    AlertLogEntry entries_[CAPACITY];
    uint8_t  head_  = 0;
    uint8_t  count_ = 0;
    bool     fsOk_  = false;
    uint32_t nextSeq_ = 0;
    AlertLogEntry pending_;
    bool pendingValid_ = false;

    static void toJson(const AlertLogEntry& e, JsonDocument& doc) {
        doc["seq"]  = e.seq;
        doc["ep"]   = e.epoch;
        doc["vk"]   = e.vtecKey;
        doc["ev"]   = e.event;
        doc["cat"]  = e.category;
        doc["mode"] = e.sirenMode;
        doc["disp"] = e.disposition;
        doc["att"]  = e.attempts;
        doc["esc"]  = e.escalation;
        doc["ntf"]  = e.notified;
        if (e.detail[0]) doc["det"] = e.detail;
    }

    static void fromJson(JsonDocument& doc, AlertLogEntry& e) {
        e.seq      = doc["seq"] | 0;
        e.epoch    = doc["ep"]  | 0;
        strlcpy(e.vtecKey,  doc["vk"]   | "", sizeof(e.vtecKey));
        strlcpy(e.event,    doc["ev"]   | "", sizeof(e.event));
        e.category = doc["cat"]  | 0;
        strlcpy(e.sirenMode,    doc["mode"] | "", sizeof(e.sirenMode));
        strlcpy(e.disposition,  doc["disp"] | "", sizeof(e.disposition));
        e.attempts   = doc["att"] | 0;
        e.escalation = doc["esc"] | false;
        e.notified   = doc["ntf"] | false;
        strlcpy(e.detail, doc["det"] | "", sizeof(e.detail));
    }

    void appendEntry(const AlertLogEntry& e) {
        File f = LittleFS.open(LOG_PATH, "a");
        if (!f) { Serial.println("AlertLog: failed to open log for append"); return; }
        JsonDocument doc;
        toJson(e, doc);
        serializeJson(doc, f);
        f.print('\n');
        f.close();
    }

    void loadRecent() {
        File f = LittleFS.open(LOG_PATH, "r");
        if (!f) return;
        AlertLogEntry buf[CAPACITY];
        uint8_t bufCount = 0, bufHead = 0;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (!line.length()) continue;
            JsonDocument doc;
            if (deserializeJson(doc, line)) continue;
            AlertLogEntry e;
            fromJson(doc, e);
            buf[bufHead] = e;
            bufHead = (bufHead + 1) % CAPACITY;
            if (bufCount < CAPACITY) bufCount++;
            if (e.seq > nextSeq_) nextSeq_ = e.seq;
        }
        f.close();
        for (uint8_t i = 0; i < bufCount; i++)
            entries_[i] = buf[(bufHead + CAPACITY - bufCount + i) % CAPACITY];
        head_  = bufCount % CAPACITY;
        count_ = bufCount;
    }

    void trimIfNeeded() {
        File f = LittleFS.open(LOG_PATH, "r");
        if (!f) return;
        uint32_t lineCount = 0;
        while (f.available()) { if (f.readStringUntil('\n').length()) lineCount++; }
        f.close();
        if (lineCount <= MAX_LINES) return;

        uint32_t toSkip = lineCount - (MAX_LINES / 2);
        f = LittleFS.open(LOG_PATH, "r");
        File tmp = LittleFS.open("/alertlog.jsonl.tmp", "w");
        if (!f || !tmp) { if (f) f.close(); if (tmp) tmp.close(); return; }
        uint32_t i = 0;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (!line.length()) continue;
            if (i++ < toSkip) continue;
            tmp.print(line);
            tmp.print('\n');
        }
        f.close();
        tmp.close();
        LittleFS.remove(LOG_PATH);
        LittleFS.rename("/alertlog.jsonl.tmp", LOG_PATH);
    }
};

extern AlertLog alertLog;
