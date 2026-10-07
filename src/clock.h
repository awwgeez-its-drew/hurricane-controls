#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

// Wall-clock time for the run log. Two sources, in priority order:
//   1. NTP, whenever this board is on a home Wi-Fi network with internet.
//   2. The Weather Watcher's own NTP-synced clock, pushed over the UART link
//      as "WX TIME <epoch>" (see weather_link.h) — covers AP mode, or a
//      network with no internet. Only applied while (1) hasn't synced.
// Kept in UTC; the browser converts to local time for display.

inline volatile bool& ntpSynced() {
    static volatile bool synced = false;
    return synced;
}

inline void onNtpSync(struct timeval*) { ntpSynced() = true; }

// Safe to call more than once (e.g. after a late Wi-Fi reconnect).
inline void startNtp() {
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

inline bool clockValid() { return time(nullptr) > 1600000000; }

inline void setClockFromLink(uint32_t epoch) {
    if (ntpSynced() || epoch < 1600000000UL) return;
    struct timeval tv = { (time_t)epoch, 0 };
    settimeofday(&tv, nullptr);
}
