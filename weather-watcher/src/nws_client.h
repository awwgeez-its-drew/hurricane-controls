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
#include "alertlog.h"
#include "ntfy.h"
#include <Preferences.h>

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

// "TYPE - THREAT LEVEL" string sent to the main board alongside a trigger,
// for its mesh activation broadcast (e.g. "TORNADO EMERGENCY - CATASTROPHIC").
// OTHER_EXTREME has no fixed type (it covers whichever NWS event types the
// owner opted into otherExtremeIncluded), so it uses the alert's own event
// name instead.
inline String categoryDescriptor(AlertCategory c, const char* event) {
    switch (c) {
    case AlertCategory::TORNADO_EMERGENCY:         return "TORNADO EMERGENCY - CATASTROPHIC";
    case AlertCategory::TORNADO_CONFIRMED:         return "TORNADO WARNING - CONFIRMED";
    case AlertCategory::TORNADO_UNCONFIRMED:       return "TORNADO WARNING - UNCONFIRMED";
    case AlertCategory::THUNDERSTORM_DESTRUCTIVE:  return "SEVERE THUNDERSTORM - DESTRUCTIVE";
    case AlertCategory::THUNDERSTORM_CONSIDERABLE: return "SEVERE THUNDERSTORM - CONSIDERABLE";
    case AlertCategory::THUNDERSTORM_BASE:         return "SEVERE THUNDERSTORM - BASE";
    case AlertCategory::OTHER_EXTREME: {
        String ev = event;
        ev.toUpperCase();
        return ev + " - EXTREME";
    }
    default: return "";
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
    char   vtecKey[20] = ""; // same id used for dedupe/the trigger queue/the log
    uint32_t expEpoch  = 0;  // 0 if unparseable or this board's clock isn't synced
};

struct RecentTrigger {
    String   event;
    String   mode;
    uint32_t epoch = 0;  // 0 = time wasn't synced yet when this fired
};

// Polls api.weather.gov for active alerts covering settingsMgr.s.latitude/
// longitude, classifies each into an AlertCategory, and sends a
// "WX <MODE> <descriptor>" command over the dedicated UART link to the
// Hurricane Controls main board the first time a tracked warning event
// crosses that category's configured mode (anything other than "OFF") —
// <descriptor> is a human-readable "TYPE - THREAT LEVEL" string (see
// categoryDescriptor() below), which the main board relays on its own
// mesh activation broadcast so a remote operator sees not just that a
// weather alert fired, but what kind and how severe.
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
        loadTracked();
    }

    void update() {
        readReplies();
        checkLinkTestTimeout();
        updateTriggerQueue(millis());

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
        // nextPollDelayMs() implements the failure backoff (15/15/30/60s then
        // the configured interval) — it collapses to the plain interval once
        // polls are succeeding, so this looks the same as before in the
        // common case.
        if (!pollSoon_ && everPolled_ && now - lastPollTs_ < nextPollDelayMs()) return;
        pollSoon_   = false;
        lastPollTs_ = now;
        poll();

        // Report the fresh result to the main board immediately, so it hears
        // the first real OK/ERROR within seconds of the first poll (its boot
        // announcement waits for exactly that).
        lastStatusPushTs_ = millis();
        sendStatusToController();
    }

    // stale: the dashboard's Current Alerts list hasn't been refreshed by a
    // *successful* poll recently enough to trust — shown greyed-out with an
    // age indicator rather than silently going out of date. Deliberately not
    // the same thing as lastPollSuccess_, which only reflects the single
    // most recent attempt (a backoff retry due any second still counts as
    // "not stale yet" as long as a success landed reasonably recently).
    bool stale() const {
        if (!everPolled_) return false; // nothing to be stale about yet — poll() hasn't run
        uint32_t maxAgeMs = settingsMgr.s.pollIntervalSec * 2000UL;
        if (maxAgeMs < 90000) maxAgeMs = 90000;
        return millis() - lastSuccessMs_ >= maxAgeMs;
    }
    uint32_t dataAgeSec() const {
        return lastSuccessMs_ ? (millis() - lastSuccessMs_) / 1000 : 0;
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
    // True once we've heard a bare "OK: pong" (pre-ACK-protocol siren
    // firmware) rather than "OK: pong 2" — lets the Settings UI warn that
    // the paired main board predates the trigger-ACK protocol.
    bool   sirenLegacyProtocol() const { return sirenLegacyProtocol_; }
    const char* lastSxEvent()    const { return lastSxEvent_; }
    uint32_t    lastSxEventMs()  const { return lastSxEventMs_; }

    uint8_t currentAlertCount() const { return currentAlertCount_; }
    const CurrentAlert& currentAlert(uint8_t i) const { return currentAlerts_[i]; }

    uint8_t recentTriggerCount() const { return recentCount_; }
    const RecentTrigger& recentTrigger(uint8_t i) const { return recent_[i]; }

private:
    uint32_t lastPollTs_ = 0;
    uint32_t lastSuccessMs_ = 0;  // millis() of the last successful poll — see stale()

    bool     pollSoon_         = false;
    bool     polledOnline_     = false;  // a poll has actually reached the network

    // ── Poll failure backoff: 15s, 15s, 30s, 60s, then the configured
    // interval — never slower than the interval itself. A Retry-After
    // header on a 429/503 overrides the schedule for that one wait (capped).
    // Any success resets back to the start of the schedule.
    static constexpr uint32_t BACKOFF_SCHEDULE_MS[4] = {15000, 15000, 30000, 60000};
    uint8_t  consecutiveFailures_ = 0;
    uint32_t retryAfterMs_        = 0;  // 0 = none in effect

    uint32_t nextPollDelayMs() const {
        uint32_t intervalMs = settingsMgr.s.pollIntervalSec * 1000UL;
        if (retryAfterMs_) return retryAfterMs_ < intervalMs ? retryAfterMs_ : intervalMs;
        if (consecutiveFailures_ == 0) return intervalMs;
        uint32_t backoff = BACKOFF_SCHEDULE_MS[
            consecutiveFailures_ - 1 < 4 ? consecutiveFailures_ - 1 : 3];
        return backoff < intervalMs ? backoff : intervalMs;
    }

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
    bool     sirenLegacyProtocol_ = false;
    char     lastSxEvent_[40]   = "";
    uint32_t lastSxEventMs_     = 0;
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
    // Persisted to NVS (one blob, loadTracked()/saveTracked()) so a reboot
    // mid-warning can't re-trigger or re-notify for the same event — see
    // docs/uart-protocol.md WP1 item 4.
    struct TrackedEvent {
        char     vtecKey[20] = {0};
        uint8_t  lastActedCategory   = 0;  // AlertCategory::NONE — highest category a final disposition has been reached for
        uint8_t  lastNotifiedCategory = 0; // highest category an ntfy notification has gone out for (WP4)
        uint32_t lastSeenMs  = 0;   // RAM-only (millis() doesn't survive reboot) — used for LRU eviction pre-NTP-sync
        uint32_t firstSeenEpoch = 0; // 0 = unknown (clock wasn't synced when first seen) — used for the 48h prune
        bool     used    = false;
        bool     pending = false;  // a trigger for this event is currently queued/in flight — don't enqueue a second
        uint8_t  pendingCategory = 0;
    };
    static constexpr uint8_t MAX_TRACKED = 16;
    TrackedEvent tracked_[MAX_TRACKED];
    bool trackedDirty_ = false;  // set on any change; saveTracked() only writes NVS when true

    TrackedEvent* findOrCreateTracked(const char* key, uint32_t now) {
        TrackedEvent* oldest = nullptr;
        for (auto& e : tracked_) {
            if (e.used && !strcmp(e.vtecKey, key)) { e.lastSeenMs = now; return &e; }
            if (!oldest || e.lastSeenMs < oldest->lastSeenMs) oldest = &e;
        }
        for (auto& e : tracked_) {
            if (!e.used) {
                strlcpy(e.vtecKey, key, sizeof(e.vtecKey));
                e.used = true;
                e.lastActedCategory = 0;
                e.lastNotifiedCategory = 0;
                e.lastSeenMs = now;
                e.firstSeenEpoch = nowEpoch();
                e.pending = false;
                trackedDirty_ = true;
                return &e;
            }
        }
        // Table full (>16 concurrent tracked events for one point — should
        // never happen in practice) — evict the least-recently-seen entry.
        strlcpy(oldest->vtecKey, key, sizeof(oldest->vtecKey));
        oldest->lastActedCategory = 0;
        oldest->lastNotifiedCategory = 0;
        oldest->lastSeenMs = now;
        oldest->firstSeenEpoch = nowEpoch();
        oldest->pending = false;
        trackedDirty_ = true;
        return oldest;
    }

    TrackedEvent* findTracked(const char* key) {
        for (auto& e : tracked_) if (e.used && !strcmp(e.vtecKey, key)) return &e;
        return nullptr;
    }

    // Drops entries whose warning expired at least 48h ago, once the clock
    // is synced (can't tell age without it, so entries just sit until then —
    // matches today's effective behavior when the clock is never synced).
    // Called once per poll; cheap (16-entry scan, no I/O unless something
    // actually gets pruned).
    void pruneTracked() {
        uint32_t now = nowEpoch();
        if (!now) return;
        for (auto& e : tracked_) {
            if (e.used && !e.pending && e.firstSeenEpoch &&
                now - e.firstSeenEpoch > 48UL * 3600) {
                e.used = false;
                trackedDirty_ = true;
            }
        }
    }

    // One blob, "wxwatch"/"tracked" (separate key from Settings' own data in
    // the same namespace). ~28 bytes/entry x 16 = well under NVS's practical
    // per-entry limits. Written only when trackedDirty_ (on change), not
    // every poll.
    void loadTracked() {
        Preferences p;
        p.begin("wxwatch", true);
        size_t n = p.getBytesLength("tracked");
        if (n == sizeof(tracked_)) p.getBytes("tracked", tracked_, sizeof(tracked_));
        p.end();
        // lastSeenMs is a millis() timestamp from a previous boot — meaningless
        // now, and would otherwise make every loaded entry look ancient for
        // eviction purposes. Reset to "now" so a fresh boot doesn't instantly
        // start evicting everything it just loaded.
        for (auto& e : tracked_) if (e.used) e.lastSeenMs = millis();
    }

    void saveTrackedIfDirty() {
        if (!trackedDirty_) return;
        Preferences p;
        p.begin("wxwatch", false);
        p.putBytes("tracked", tracked_, sizeof(tracked_));
        p.end();
        trackedDirty_ = false;
    }

    // Extracts "office.phenomena.significance.etn" from a raw P-VTEC string
    // like "/O.NEW.KOUN.TO.W.0123.2601010000Z-2601010100Z/". This stays
    // constant across a warning's entire lifecycle (NEW/CON/EXT/UPG/CAN),
    // unlike properties.id which changes on every single update.
    // NWS's "expires" field is ISO8601 with an explicit numeric offset
    // (e.g. "2026-10-25T18:30:00-05:00"), occasionally "Z" for UTC. Returns
    // 0 on anything unparseable rather than guessing.
    static uint32_t parseIso8601(const char* s) {
        if (!s || strlen(s) < 19) return 0;
        int y, mo, d, h, mi, se;
        if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
        struct tm tmv = {};
        tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
        tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = se;
        time_t utc = timegm(&tmv);
        const char* off = s + 19;
        int offSec = 0;
        if (*off == '+' || *off == '-') {
            int oh = 0, om = 0;
            sscanf(off + 1, "%d:%d", &oh, &om);
            offSec = ((*off == '-') ? -1 : 1) * (oh * 3600 + om * 60);
        }
        return (uint32_t)(utc - offSec);
    }

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

    static bool categoryNotifyEnabled(AlertCategory c) {
        switch (c) {
        case AlertCategory::THUNDERSTORM_BASE:         return settingsMgr.s.ntfyCatThunderstormBase;
        case AlertCategory::OTHER_EXTREME:             return settingsMgr.s.ntfyCatOtherExtreme;
        case AlertCategory::THUNDERSTORM_CONSIDERABLE: return settingsMgr.s.ntfyCatThunderstormConsiderable;
        case AlertCategory::TORNADO_UNCONFIRMED:        return settingsMgr.s.ntfyCatTornadoUnconfirmed;
        case AlertCategory::THUNDERSTORM_DESTRUCTIVE:   return settingsMgr.s.ntfyCatThunderstormDestructive;
        case AlertCategory::TORNADO_CONFIRMED:           return settingsMgr.s.ntfyCatTornadoConfirmed;
        case AlertCategory::TORNADO_EMERGENCY:           return settingsMgr.s.ntfyCatTornadoEmergency;
        default:                                         return false;
        }
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
        bool wasOk = !everPolled_ || lastPollSuccess_;
        uint8_t failedAttempts = consecutiveFailures_;
        lastPollEpoch_   = nowEpoch();
        everPolled_      = true;
        lastPollSuccess_ = ok;
        lastPollError_   = err;
        if (ok) {
            consecutiveFailures_ = 0;
            retryAfterMs_        = 0;
            lastSuccessMs_       = millis();
        } else if (!retryAfterMs_ && consecutiveFailures_ < 255) {
            consecutiveFailures_++;
        }
        // Edge-detected, not every poll — the backoff schedule already
        // keeps a sustained outage from being chatty on its own.
        if (settingsMgr.s.ntfyNotifyErrors) {
            if (wasOk && !ok) ntfy.notify(4, "NWS poll failing", err);
            else if (!wasOk && ok) ntfy.notify(3, "NWS poll recovered", "Back to normal after " + String(failedAttempts) + " failed attempt(s)");
        }
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
            // Honor a rate-limit Retry-After on 429/503 for exactly one wait
            // (capped at 300s) instead of the normal backoff schedule.
            if (code == 429 || code == 503) {
                String ra = http.header("Retry-After");
                long seconds = ra.length() ? ra.toInt() : 0;
                if (seconds > 0) retryAfterMs_ = (uint32_t)(min(seconds, 300L)) * 1000UL;
            }
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
                                            "urgency", "certainty", "headline", "areaDesc", "expires"};
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

            const char* mode = modeForCategory(category);
            uint8_t categoryVal = (uint8_t)category;
            // "acted" (lastActedCategory set) only once a disposition is
            // final — see finalizeQueueHead()/the OFF-mode branch just below
            // — not at enqueue time, so a still-pending retry doesn't look
            // like "already handled" and get silently skipped. `pending`
            // covers the in-between window so the same crossing can't be
            // queued twice while its first attempt is still resolving.
            bool firstTime = te->lastActedCategory == 0;
            bool escalated = settingsMgr.s.repeatOnUpgrade &&
                              te->lastActedCategory > 0 &&
                              categoryVal > te->lastActedCategory;
            if ((firstTime || escalated) && !te->pending) {
                if (!strcmp(mode, "OFF")) {
                    // Awareness-only crossing: no siren command, but still a
                    // real disposition (NOTIFY_ONLY) for the log/ntfy (WP3/
                    // WP4) to pick up, and still marks this category acted
                    // so it isn't re-evaluated every poll.
                    te->lastActedCategory = categoryVal;
                    trackedDirty_ = true;
                    Serial.printf("%s: category crossed with siren mode OFF -> NOTIFY_ONLY\n", key);
                    AlertLogEntry le;
                    le.epoch = nowEpoch();
                    strlcpy(le.vtecKey, key, sizeof(le.vtecKey));
                    strlcpy(le.event, event, sizeof(le.event));
                    le.category = categoryVal;
                    strlcpy(le.sirenMode, "OFF", sizeof(le.sirenMode));
                    strlcpy(le.disposition, "NOTIFY_ONLY", sizeof(le.disposition));
                    le.escalation = escalated;
                    alertLog.record(le);
                    if (categoryNotifyEnabled(category)) {
                        ntfy.notify(3, "Weather alert (auto-trigger off)",
                            String(event) + " — " + categoryDescriptor(category, event) +
                            " (siren mode OFF, awareness only)");
                    }
                } else {
                    te->pending         = true;
                    te->pendingCategory = categoryVal;
                    enqueueTrigger(mode, key, categoryDescriptor(category, event), event, categoryVal, escalated);
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
                strlcpy(ca.vtecKey, key, sizeof(ca.vtecKey));
                ca.expEpoch = parseIso8601(props["expires"] | "");
            }
        }
        sendAlertsSnapshot(fresh, freshCount);
        pruneTracked();
        saveTrackedIfDirty();

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

    // ── Trigger retry queue (docs/uart-protocol.md WP1 item 2) ──────────────
    // vtecKey doubles as the alert's cross-referenceable unique ID sent to
    // the main board (no spaces, already NWS's own identifier). Only
    // queue_[0] is ever "in flight" — a strict FIFO, shifted down on
    // completion; MAX_QUEUED=8 is generous for how many distinct warnings
    // could plausibly be mid-retry at once for a single point.
    struct QueuedTrigger {
        bool     active      = false;
        char     vtecKey[20] = "";
        char     mode[10]    = "";
        String   descriptor;
        String   event;
        uint32_t firstSentMs = 0;
        uint32_t lastSentMs  = 0;
        uint8_t  sendCount   = 0;
        char     blockCode[12] = ""; // last retryable ACK code ("" = still awaiting any first ACK)
        uint8_t  category    = 0;
        bool     escalation  = false;
    };
    static constexpr uint8_t MAX_QUEUED = 8;
    QueuedTrigger queue_[MAX_QUEUED];

    void enqueueTrigger(const char* mode, const char* vtecKey, const String& descriptor, const char* event,
                         uint8_t category, bool escalation) {
        // A higher-category escalation for an already-queued event replaces
        // rather than duplicates — restart its attempt counter too, since
        // this is effectively a fresh trigger attempt.
        for (auto& q : queue_) {
            if (q.active && !strcmp(q.vtecKey, vtecKey)) {
                strlcpy(q.mode, mode, sizeof(q.mode));
                q.descriptor  = descriptor;
                q.event       = event;
                q.sendCount   = 0;
                q.blockCode[0] = '\0';
                q.category    = category;
                q.escalation  = escalation;
                return;
            }
        }
        for (auto& q : queue_) {
            if (!q.active) {
                q.active = true;
                strlcpy(q.vtecKey, vtecKey, sizeof(q.vtecKey));
                strlcpy(q.mode, mode, sizeof(q.mode));
                q.descriptor  = descriptor;
                q.event       = event;
                q.sendCount   = 0;
                q.blockCode[0] = '\0';
                q.category    = category;
                q.escalation  = escalation;
                return;
            }
        }
        Serial.printf("Trigger queue full (%u) — dropping %s %s\n", MAX_QUEUED, vtecKey, mode);
    }

    void transmitQueued(QueuedTrigger& q, uint32_t now) {
        Serial.printf("Trigger queue: sending WX %s %s %s (attempt %u)\n",
            q.mode, q.vtecKey, q.descriptor.c_str(), q.sendCount + 1);
        digitalWrite(STATUS_LED, HIGH);
        Serial1.print("WX ");
        Serial1.print(q.mode);
        Serial1.print(' ');
        Serial1.print(q.vtecKey);
        Serial1.print(' ');
        Serial1.println(q.descriptor);
        if (q.sendCount == 0) q.firstSentMs = now;
        q.lastSentMs = now;
        q.sendCount++;
    }

    void updateTriggerQueue(uint32_t now) {
        if (!queue_[0].active) return;
        QueuedTrigger& q = queue_[0];
        if (q.sendCount == 0) { transmitQueued(q, now); return; }
        if (q.blockCode[0]) {
            // A retryable block (STOPPING/JOG/OTA/TESTMODE) was ACKed —
            // keep retrying every 3s for up to 30s total from the first send.
            if (now - q.firstSentMs >= 30000) {
                char disp[24];
                snprintf(disp, sizeof(disp), "GAVE_UP_%s", q.blockCode);
                finalizeQueueHead(disp);
                return;
            }
            if (now - q.lastSentMs >= 3000) transmitQueued(q, now);
            return;
        }
        // No ACK at all yet for the most recent send — resend up to 3 times
        // (4 sends total) at 3s intervals, then give up.
        if (now - q.lastSentMs >= 3000) {
            if (q.sendCount >= 4) { finalizeQueueHead("NO_RESPONSE"); return; }
            transmitQueued(q, now);
        }
    }

    // "ACK <vtecKey> <CODE> [detail]" — only ever matched against the
    // current queue head, since that's the only trigger ever in flight.
    void handleAck(char* line) {
        char* p = line + 3; // skip "ACK"
        while (*p == ' ') p++;
        char* vtec = p;
        p = strchr(p, ' ');
        if (!p) return;
        *p = '\0'; p++;
        while (*p == ' ') p++;
        char* code = p;
        p = strchr(p, ' ');
        if (p) { *p = '\0'; p++; while (*p == ' ') p++; }

        if (!queue_[0].active || strcmp(queue_[0].vtecKey, vtec) != 0) return;
        QueuedTrigger& q = queue_[0];
        if      (!strcmp(code, "TRIGGERED")) finalizeQueueHead(q.sendCount > 1 ? "RETRIED_TRIGGERED" : "TRIGGERED");
        else if (!strcmp(code, "ACTIVE"))    finalizeQueueHead("ALREADY_ACTIVE");
        else if (!strcmp(code, "DISABLED"))  finalizeQueueHead("AUTO_OFF");
        else if (!strcmp(code, "BADCMD"))    finalizeQueueHead("BADCMD");
        else if (!strcmp(code, "STOPPING") || !strcmp(code, "JOG") ||
                 !strcmp(code, "OTA") || !strcmp(code, "TESTMODE")) {
            strlcpy(q.blockCode, code, sizeof(q.blockCode));
        }
    }

    void finalizeQueueHead(const char* disposition) {
        QueuedTrigger& q = queue_[0];
        Serial.printf("Trigger queue: %s %s -> %s\n", q.vtecKey, q.mode, disposition);
        if (!strcmp(disposition, "TRIGGERED") || !strcmp(disposition, "RETRIED_TRIGGERED"))
            recordRecentTrigger(q.event.c_str(), q.mode);
        TrackedEvent* te = findTracked(q.vtecKey);
        if (te) {
            te->pending       = false;
            te->lastActedCategory = te->pendingCategory;
            trackedDirty_ = true;
        }
        AlertLogEntry le;
        le.epoch = nowEpoch();
        strlcpy(le.vtecKey, q.vtecKey, sizeof(le.vtecKey));
        strlcpy(le.event, q.event.c_str(), sizeof(le.event));
        le.category = q.category;
        strlcpy(le.sirenMode, q.mode, sizeof(le.sirenMode));
        strlcpy(le.disposition, disposition, sizeof(le.disposition));
        le.attempts   = q.sendCount;
        le.escalation = q.escalation;
        alertLog.record(le);

        bool isFailure = !strcmp(disposition, "NO_RESPONSE") || !strncmp(disposition, "GAVE_UP_", 8);
        if (isFailure && settingsMgr.s.ntfyNotifyErrors) {
            ntfy.notify(5, "Siren did not respond",
                String(q.event.c_str()) + " — " + q.mode + " command " + disposition +
                " (" + q.sendCount + " attempt" + (q.sendCount == 1 ? "" : "s") + ")");
        } else if ((!strcmp(disposition, "TRIGGERED") || !strcmp(disposition, "RETRIED_TRIGGERED")) &&
                   categoryNotifyEnabled((AlertCategory)q.category)) {
            ntfy.notify(4, String(q.mode) + " activated",
                String(q.event.c_str()) + " — " + q.descriptor);
        }

        for (uint8_t i = 1; i < MAX_QUEUED; i++) queue_[i - 1] = queue_[i];
        queue_[MAX_QUEUED - 1] = QueuedTrigger{};
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
    // Pushes the current alert list to the main board (docs/uart-protocol.md
    // section 2.1: "WX ALERTS <n>" / "WX ALERT ..." / "WX ALERTS END"),
    // feeding its active-warning banner (WP7). Called once per *successful*
    // poll, even when the list is empty or unchanged — re-sending an empty
    // list clears a stale banner, and the main board's own received-at
    // timestamp (weather_link.h's alertsReceivedMs()) only advances on an
    // actual send, which is what it uses to grey out/expire the banner if
    // this link stalls while a poll failure keeps happening instead.
    void sendAlertsSnapshot(const CurrentAlert* list, uint8_t count) {
        Serial1.print("WX ALERTS ");
        Serial1.println(count);
        for (uint8_t i = 0; i < count; i++) {
            const CurrentAlert& a = list[i];
            Serial1.print("WX ALERT ");
            Serial1.print(a.vtecKey[0] ? a.vtecKey : "-");
            Serial1.print(' ');
            Serial1.print((unsigned)a.category);
            Serial1.print(' ');
            Serial1.print(a.expEpoch);
            Serial1.print(' ');
            Serial1.println(a.event);
        }
        Serial1.println("WX ALERTS END");
    }

    void sendStatusToController() {
        bool wifiOk = WiFi.status() == WL_CONNECTED;
        const char* state;
        String detail;
        if (!wifiOk && !polledOnline_ && millis() < STARTUP_GRACE_MS) { state = "PENDING"; detail = "Connecting to Wi-Fi"; }
        else if (!wifiOk)            { state = "ERROR";   detail = "WiFi not connected"; }
        else if (!polledOnline_)     { state = "PENDING"; detail = "Waiting for first poll"; }
        else if (!lastPollSuccess_)  { state = "ERROR";   detail = lastPollError_; }
        else if (stale())            { state = "ERROR";   detail = "NWS data stale (" + String(dataAgeSec() / 60) + " min old)"; }
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

    // Despite the name, one line this recognizes isn't a reply to anything
    // this board sent: a bare "REBOOT" is an unprompted command from the
    // main board (see src/weather_link.h's requestReboot(), sent for the
    // mesh "SIREN WXREBOOT" command) — the only main->WW direction on this
    // otherwise WW-initiated link.
    void handleReply(char* line) {
        while (*line == ' ') line++;
        size_t len = strlen(line);
        while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
        if (!len) return;
        Serial.print("Main board reply: ");
        Serial.println(line);
        digitalWrite(STATUS_LED, LOW);
        if (!strcasecmp(line, "OK: pong") || !strcasecmp(line, "OK: pong 2")) {
            CtrlLock lock;
            sirenLegacyProtocol_ = !strcasecmp(line, "OK: pong");
            if (linkTestInProgress_) {
                linkTestInProgress_ = false;
                everTestedLink_     = true;
                linkOk_             = true;
                linkDetail_         = "Status OK";
            }
        } else if (!strncasecmp(line, "ACK ", 4)) {
            handleAck(line);
        } else if (!strncasecmp(line, "SX ", 3)) {
            // Unsolicited siren-activity event (main board's weather_link.h
            // sendStatusEvents(), docs/uart-protocol.md section 2.3) — a
            // WEB/MESH/button-initiated run the WW didn't cause itself.
            {
                CtrlLock lock;
                strlcpy(lastSxEvent_, line + 3, sizeof(lastSxEvent_));
                lastSxEventMs_ = millis();
            }
            // "SX ACTIVATED <MODE> <SOURCE>" — the three non-NWS activation
            // sources each have their own opt-in toggle (off by default,
            // since this is "someone's using the siren", not a fault).
            char rest[40];
            strlcpy(rest, line + 3, sizeof(rest));
            char* tok = strtok(rest, " ");
            if (tok && !strcmp(tok, "ACTIVATED")) {
                char* modeW = strtok(nullptr, " ");
                char* srcW  = strtok(nullptr, " ");
                bool wanted = srcW && (
                    (!strcmp(srcW, "LOCAL") && settingsMgr.s.ntfyNotifySrcLocal) ||
                    (!strcmp(srcW, "WEB")   && settingsMgr.s.ntfyNotifySrcWeb) ||
                    (!strcmp(srcW, "MESH")  && settingsMgr.s.ntfyNotifySrcMesh));
                if (wanted) {
                    ntfy.notify(3, "Siren activated",
                        String(modeW ? modeW : "?") + " — activated from " + srcW);
                }
            }
        } else if (!strcasecmp(line, "REBOOT")) {
            Serial.println("Main board requested a reboot — restarting");
            delay(100);
            ESP.restart();
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
