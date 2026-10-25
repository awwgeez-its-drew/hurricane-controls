#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// How a run ended. COMPLETED = ran its full configured duration (or a Growl
// Test finished its last stage); STOPPED = someone stopped it (see the
// entry's stopSource); FAILSAFE = a web-UI Manual hold whose keep-alives
// stopped arriving (phone dropped off Wi-Fi / locked mid-hold); INTERRUPTED =
// the board lost power/rebooted mid-run — synthesized at the next boot from
// the start marker file (see RunLog::reconcileInterrupted()), never set live.
enum class RunEnd : uint8_t { COMPLETED, STOPPED, FAILSAFE, INTERRUPTED };

// One finished (or interrupted) run. mode/source/stopSource hold the raw
// RunMode/TriggerSource values from statemachine.h — stored as plain bytes
// so this header doesn't depend on it (statemachine.h includes this one).
struct RunEntry {
    uint32_t seq            = 0;   // monotonic, persisted — stable ordering/id across reboots
    uint32_t epoch          = 0;   // UTC start time, 0 = clock wasn't set yet
    uint32_t startUptimeSec = 0;   // uptime at start, for "x min ago" when epoch is 0
    uint32_t durationSec    = 0;   // trigger -> back to IDLE, incl. start/stop sequences (0 for INTERRUPTED — unknown)
    uint8_t  mode           = 0;
    uint8_t  source         = 0;
    uint8_t  end            = 0;
    uint8_t  stopSource     = 0;
    // Only set when source is NWS_ALERT (see weather_link.h) — the alert's
    // "TYPE - THREAT LEVEL" descriptor and its VTEC-derived tracking id,
    // kept for later troubleshooting/cross-reference. Empty otherwise.
    char     alertType[48]  = "";
    char     alertId[20]    = "";
};

// Recent runs, now backed by a LittleFS JSON-lines file (/runlog.jsonl) so
// they survive reboot/OTA, plus the pre-existing NVS lifetime totals (one
// write per finished run, unchanged). The RAM ring buffer (CAPACITY=20,
// same as before) is a read cache for the dashboard, refilled from the file
// at begin() — not the source of truth any more.
//
// Flash writes are deferred: trigger() (statemachine.h) calls markStarted()
// and record() only touches RAM + queues a flag; the actual LittleFS write
// happens in flushPending(), called once per main.cpp loop() tick *outside*
// sm.update()'s CtrlLock scope, so a slow flash write never stalls the
// locked button/state-machine/mesh/weather-link section (ground rule 3).
//
// Power-loss recovery: markStarted() also queues a tiny start-marker file
// write (/runlog.pending) containing just enough to reconstruct an
// INTERRUPTED entry; record() queues its deletion. If begin() finds that
// marker still present at boot, the previous run never reached record() —
// reconcileInterrupted() appends an INTERRUPTED entry for it and removes
// the marker.
class RunLog {
public:
    static constexpr uint8_t  CAPACITY     = 20;
    static constexpr uint32_t MAX_LINES    = 500;  // trim threshold for /runlog.jsonl
    static constexpr const char* LOG_PATH     = "/runlog.jsonl";
    static constexpr const char* PENDING_PATH = "/runlog.pending";

    void begin() {
        Preferences p;
        p.begin("stats", true);
        totalRuns_ = p.getUInt("runs", 0);
        totalSecs_ = p.getUInt("runSecs", 0);
        p.end();

        fsOk_ = LittleFS.begin(true); // format on first boot / after a partition-table change
        if (!fsOk_) {
            Serial.println("RunLog: LittleFS mount failed — persistent log disabled, RAM-only this boot");
            return;
        }
        loadRecent();
        reconcileInterrupted();
    }

    bool fsOk() const { return fsOk_; }

    // Called from StateMachine::trigger() — RAM-only (see class comment).
    void markStarted(uint8_t mode, uint8_t source, uint32_t epoch, uint32_t startUptimeSec) {
        nextSeq_++;
        pendingStart_.seq            = nextSeq_;
        pendingStart_.mode           = mode;
        pendingStart_.source         = source;
        pendingStart_.epoch          = epoch;
        pendingStart_.startUptimeSec = startUptimeSec;
        startPending_ = true;
    }

    void record(const RunEntry& eIn) {
        RunEntry e = eIn;
        e.seq = pendingStart_.seq ? pendingStart_.seq : ++nextSeq_;
        entries_[head_] = e;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;

        totalRuns_++;
        totalSecs_ += e.durationSec;
        Preferences p;
        p.begin("stats", false);
        p.putUInt("runs", totalRuns_);
        p.putUInt("runSecs", totalSecs_);
        p.end();

        pendingFinish_ = e;
        finishPending_ = true;
        startPending_  = false; // the completion write below supersedes the marker
    }

    // Called once per main.cpp loop() tick, outside the CtrlLock section.
    // At most one flash operation per call — there is never more than one
    // run active at a time, so start/finish events are already naturally
    // rate-limited to "occasionally", not "every tick".
    void flushPending() {
        if (!fsOk_) { startPending_ = false; finishPending_ = false; return; }
        if (startPending_) {
            writeStartMarker(pendingStart_);
            startPending_ = false;
        } else if (finishPending_) {
            appendEntry(pendingFinish_);
            LittleFS.remove(PENDING_PATH);
            finishPending_ = false;
            trimIfNeeded();
        }
    }

    uint8_t  count()     const { return count_; }
    uint32_t totalRuns() const { return totalRuns_; }
    uint32_t totalSecs() const { return totalSecs_; }

    // i = 0 is the most recent run.
    const RunEntry& recent(uint8_t i) const {
        return entries_[(head_ + CAPACITY - 1 - i) % CAPACITY];
    }

    // For CSV export (webserver.h) — streams the full persisted file, not
    // just the RAM cache's last CAPACITY entries. Caller owns the File and
    // must File::close() it. Returns an unopened (operator bool false) File
    // if the filesystem isn't mounted or the log doesn't exist yet.
    File openForExport() {
        if (!fsOk_) return File();
        return LittleFS.open(LOG_PATH, "r");
    }

private:
    RunEntry entries_[CAPACITY];
    uint8_t  head_      = 0;
    uint8_t  count_     = 0;
    uint32_t totalRuns_ = 0;
    uint32_t totalSecs_ = 0;

    bool     fsOk_         = false;
    uint32_t nextSeq_       = 0;
    RunEntry pendingStart_;
    RunEntry pendingFinish_;
    bool     startPending_  = false;
    bool     finishPending_ = false;

    static void toJson(const RunEntry& e, JsonDocument& doc) {
        doc["seq"]      = e.seq;
        doc["epoch"]    = e.epoch;
        doc["upSec"]    = e.startUptimeSec;
        doc["durSec"]   = e.durationSec;
        doc["mode"]     = e.mode;
        doc["source"]   = e.source;
        doc["end"]      = e.end;
        doc["stopSrc"]  = e.stopSource;
        if (e.alertType[0]) doc["at"] = e.alertType;
        if (e.alertId[0])   doc["ai"] = e.alertId;
    }

    static void fromJson(JsonDocument& doc, RunEntry& e) {
        e.seq            = doc["seq"]    | 0;
        e.epoch          = doc["epoch"]  | 0;
        e.startUptimeSec = doc["upSec"]  | 0;
        e.durationSec    = doc["durSec"] | 0;
        e.mode           = doc["mode"]   | 0;
        e.source         = doc["source"] | 0;
        e.end            = doc["end"]    | 0;
        e.stopSource     = doc["stopSrc"] | 0;
        strlcpy(e.alertType, doc["at"] | "", sizeof(e.alertType));
        strlcpy(e.alertId,   doc["ai"] | "", sizeof(e.alertId));
    }

    void appendEntry(const RunEntry& e) {
        File f = LittleFS.open(LOG_PATH, "a");
        if (!f) { Serial.println("RunLog: failed to open log for append"); return; }
        JsonDocument doc;
        toJson(e, doc);
        serializeJson(doc, f);
        f.print('\n');
        f.close();
    }

    void writeStartMarker(const RunEntry& e) {
        File f = LittleFS.open(PENDING_PATH, "w");
        if (!f) { Serial.println("RunLog: failed to write start marker"); return; }
        JsonDocument doc;
        toJson(e, doc);
        serializeJson(doc, f);
        f.close();
    }

    // Reads the whole file to count lines/load the RAM cache — fine at
    // begin() (once, before setup() finishes) even though it'd be too slow
    // for the main loop.
    void loadRecent() {
        File f = LittleFS.open(LOG_PATH, "r");
        if (!f) return; // no log yet — fresh install
        RunEntry buf[CAPACITY];
        uint8_t bufCount = 0, bufHead = 0;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (!line.length()) continue;
            JsonDocument doc;
            if (deserializeJson(doc, line)) continue; // skip a torn trailing line from a power-loss write
            RunEntry e;
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

    void reconcileInterrupted() {
        File f = LittleFS.open(PENDING_PATH, "r");
        if (!f) return;
        String line = f.readString();
        f.close();
        JsonDocument doc;
        if (deserializeJson(doc, line)) { LittleFS.remove(PENDING_PATH); return; }
        RunEntry e;
        fromJson(doc, e);
        e.durationSec = 0; // unknown — the board didn't get to finish timing it
        e.end = (uint8_t)RunEnd::INTERRUPTED;
        appendEntry(e);
        entries_[head_] = e;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;
        if (e.seq > nextSeq_) nextSeq_ = e.seq;
        LittleFS.remove(PENDING_PATH);
    }

    // Keeps /runlog.jsonl from growing unbounded — rewritten (temp file +
    // rename, so a power loss mid-trim never leaves a half-written log)
    // only once it actually crosses MAX_LINES, not on every write.
    void trimIfNeeded() {
        File f = LittleFS.open(LOG_PATH, "r");
        if (!f) return;
        uint32_t lineCount = 0;
        while (f.available()) { if (f.readStringUntil('\n').length()) lineCount++; }
        f.close();
        if (lineCount <= MAX_LINES) return;

        uint32_t toSkip = lineCount - (MAX_LINES / 2);
        f = LittleFS.open(LOG_PATH, "r");
        File tmp = LittleFS.open("/runlog.jsonl.tmp", "w");
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
        LittleFS.rename("/runlog.jsonl.tmp", LOG_PATH);
    }
};

extern RunLog runLog;
