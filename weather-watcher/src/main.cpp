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

// Also called from webserver.h after Settings are saved, so a new NTP
// server/timezone choice applies immediately without a reboot. Uses a POSIX
// TZ string (configTzTime), not a plain UTC offset — the TZ string encodes
// the DST transition dates for that zone, so the clock adjusts for DST on
// its own; no separate DST toggle to remember twice a year.
void applyTimeConfig() {
    if (!wifiMgr.isConnected()) return; // AP mode / no internet — clock stays unsynced
    configTzTime(settingsMgr.s.posixTz, settingsMgr.s.ntpServer);
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
