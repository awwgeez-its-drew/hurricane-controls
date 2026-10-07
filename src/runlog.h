#pragma once
#include <Arduino.h>
#include <Preferences.h>

// How a run ended. COMPLETED = ran its full configured duration (or a Growl
// Test finished its last stage); STOPPED = someone stopped it (see the
// entry's stopSource); FAILSAFE = a web-UI Manual hold whose keep-alives
// stopped arriving (phone dropped off Wi-Fi / locked mid-hold).
enum class RunEnd : uint8_t { COMPLETED, STOPPED, FAILSAFE };

// One finished run. mode/source/stopSource hold the raw RunMode/
// TriggerSource values from statemachine.h — stored as plain bytes so this
// header doesn't depend on it (statemachine.h includes this one).
struct RunEntry {
    uint32_t epoch          = 0;   // UTC start time, 0 = clock wasn't set yet
    uint32_t startUptimeSec = 0;   // uptime at start, for "x min ago" when epoch is 0
    uint32_t durationSec    = 0;   // trigger -> back to IDLE, incl. start/stop sequences
    uint8_t  mode           = 0;
    uint8_t  source         = 0;
    uint8_t  end            = 0;
    uint8_t  stopSource     = 0;
};

// Recent runs (RAM only — cleared on reboot) plus lifetime totals (NVS,
// namespace "stats", one write per finished run).
class RunLog {
public:
    static constexpr uint8_t CAPACITY = 20;

    void begin() {
        Preferences p;
        p.begin("stats", true);
        totalRuns_ = p.getUInt("runs", 0);
        totalSecs_ = p.getUInt("runSecs", 0);
        p.end();
    }

    void record(const RunEntry& e) {
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
    }

    uint8_t  count()     const { return count_; }
    uint32_t totalRuns() const { return totalRuns_; }
    uint32_t totalSecs() const { return totalSecs_; }

    // i = 0 is the most recent run.
    const RunEntry& recent(uint8_t i) const {
        return entries_[(head_ + CAPACITY - 1 - i) % CAPACITY];
    }

private:
    RunEntry entries_[CAPACITY];
    uint8_t  head_      = 0;
    uint8_t  count_     = 0;
    uint32_t totalRuns_ = 0;
    uint32_t totalSecs_ = 0;
};

extern RunLog runLog;
