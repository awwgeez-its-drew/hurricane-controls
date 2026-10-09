#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "settings.h"

// Wall-clock time for the run log and the public status page's clock. Two
// sources:
//   1. The Weather Watcher's own NTP-synced clock, pushed over the UART link
//      as "WX TIME <epoch>" (see weather_link.h) — PRIMARY. Every push wins,
//      since the WW board typically has better internet access.
//   2. This board's own NTP sync — FALLBACK, used only if no WX time has
//      arrived within WX_FALLBACK_WAIT_MS of the last Wi-Fi connect. Once a
//      WX push has been received, the fallback stops re-syncing.
// Kept in UTC internally; applyTimeZone() below sets libc's local-time
// rules from the timezone/DST settings so strftime() with "%H:%M:%S" etc.
// shows the configured zone, regardless of which source set the clock.

constexpr uint32_t WX_FALLBACK_WAIT_MS = 20000;

inline volatile bool& wxSynced() {
    static volatile bool synced = false;
    return synced;
}

// Maps the friendly timeZone dropdown value + autoDst checkbox to a POSIX TZ
// string for configTzTime(). A POSIX TZ string with a DST rule (the
// "XXX#XDST#,Mm.w.d,Mm.w.d" form) makes the clock adjust for DST on its own
// twice a year; Arizona and Hawaii never observe DST, so autoDst is ignored
// for those two.
inline const char* posixTzFor(const char* zone, bool dst) {
    if (!strcmp(zone, "EASTERN"))  return dst ? "EST5EDT,M3.2.0,M11.1.0" : "EST5";
    if (!strcmp(zone, "CENTRAL"))  return dst ? "CST6CDT,M3.2.0,M11.1.0" : "CST6";
    if (!strcmp(zone, "MOUNTAIN")) return dst ? "MST7MDT,M3.2.0,M11.1.0" : "MST7";
    if (!strcmp(zone, "ARIZONA"))  return "MST7";   // no DST
    if (!strcmp(zone, "PACIFIC"))  return dst ? "PST8PDT,M3.2.0,M11.1.0" : "PST8";
    if (!strcmp(zone, "ALASKA"))   return dst ? "AKST9AKDT,M3.2.0,M11.1.0" : "AKST9";
    if (!strcmp(zone, "HAWAII"))   return "HST10";  // no DST
    return "UTC0";
}

// Sets libc's local-time rules from the timezone/DST settings, independent
// of which clock source is active — so strftime()-based local-time display
// (the status page clock, etc.) is correct whether the time itself came from
// the WX link or the NTP fallback. Safe to call anytime, including before
// either source has synced.
inline void applyTimeZone(const Settings& s) {
    setenv("TZ", posixTzFor(s.timeZone, s.autoDst), 1);
    tzset();
}

// Fallback NTP sync — only meaningful while the WX link hasn't provided a
// time yet (see updateNtpFallback() below, called from main.cpp's loop()).
inline void applyTimeConfig(const Settings& s) {
    applyTimeZone(s);
    configTime(0, 0, s.ntpServer);
}

// Drives the fallback NTP sync: starts it once the WX link has had
// WX_FALLBACK_WAIT_MS to provide a time and hasn't, then keeps re-syncing
// every ntpUpdateHours for as long as the WX link still hasn't reported one.
// Call this every loop() tick while Wi-Fi is connected; it no-ops once
// wxSynced() becomes true.
inline void updateNtpFallback(const Settings& s, bool wifiConnected, uint32_t msSinceWifiConnect) {
    static bool started = false;
    static uint32_t lastSyncMs = 0;
    if (!wifiConnected || wxSynced()) return;
    if (!started) {
        if (msSinceWifiConnect < WX_FALLBACK_WAIT_MS) return;
        started = true;
        lastSyncMs = millis();
        applyTimeConfig(s);
        return;
    }
    uint32_t intervalMs = s.ntpUpdateHours * 3600000UL;
    if (intervalMs > 0 && millis() - lastSyncMs >= intervalMs) {
        lastSyncMs = millis();
        applyTimeConfig(s);
    }
}

inline bool clockValid() { return time(nullptr) > 1600000000; }

inline void setClockFromLink(uint32_t epoch) {
    if (epoch < 1600000000UL) return;
    struct timeval tv = { (time_t)epoch, 0 };
    settimeofday(&tv, nullptr);
    wxSynced() = true;
}
