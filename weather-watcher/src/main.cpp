#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"
#include "nws_client.h"
#include "webserver.h"

// ── Singletons ────────────────────────────────────────────────────────────────
SettingsManager settingsMgr;
WiFiManager     wifiMgr;
NwsClient       nwsClient;
WeatherWebUI    webUI;

static uint32_t lastNtpSyncMs_ = 0;

// Maps the friendly timeZone dropdown value + autoDst checkbox to a POSIX TZ
// string for configTzTime(). A POSIX TZ string with a DST rule (the
// "XXX#XDST#,Mm.w.d,Mm.w.d" form) makes the clock adjust for DST on its own
// twice a year; Arizona and Hawaii never observe DST, so autoDst is ignored
// for those two.
static const char* posixTzFor(const char* zone, bool dst) {
    if (!strcmp(zone, "EASTERN"))  return dst ? "EST5EDT,M3.2.0,M11.1.0" : "EST5";
    if (!strcmp(zone, "CENTRAL"))  return dst ? "CST6CDT,M3.2.0,M11.1.0" : "CST6";
    if (!strcmp(zone, "MOUNTAIN")) return dst ? "MST7MDT,M3.2.0,M11.1.0" : "MST7";
    if (!strcmp(zone, "ARIZONA"))  return "MST7";   // no DST
    if (!strcmp(zone, "PACIFIC"))  return dst ? "PST8PDT,M3.2.0,M11.1.0" : "PST8";
    if (!strcmp(zone, "ALASKA"))   return dst ? "AKST9AKDT,M3.2.0,M11.1.0" : "AKST9";
    if (!strcmp(zone, "HAWAII"))   return "HST10";  // no DST
    return "UTC0";
}

// Also called from webserver.h after Settings are saved, so a new NTP
// server/timezone choice applies immediately without a reboot.
void applyTimeConfig() {
    if (!wifiMgr.isConnected()) return; // AP mode / no internet — clock stays unsynced
    configTzTime(posixTzFor(settingsMgr.s.timeZone, settingsMgr.s.autoDst), settingsMgr.s.ntpServer);
    lastNtpSyncMs_ = millis();
}

void setup() {
    Serial.begin(115200);
    settingsMgr.load();

    // WiFi first (may block up to 12s for STA attempt)
    wifiMgr.begin();
    applyTimeConfig();
    nwsClient.begin();
    nwsClient.requestLinkTest(); // confirm the UART link to the main board on boot
    webUI.begin();

    Serial.println("Weather Watcher ready.");
}

void loop() {
    wifiMgr.update();
    nwsClient.update();
    webUI.update();

    uint32_t intervalMs = settingsMgr.s.ntpUpdateHours * 3600000UL;
    if (intervalMs > 0 && millis() - lastNtpSyncMs_ >= intervalMs) applyTimeConfig();
}
