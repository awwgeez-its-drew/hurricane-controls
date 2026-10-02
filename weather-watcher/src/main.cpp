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

// Also called from webserver.h after Settings are saved, so a new NTP
// server/UTC offset/DST choice applies immediately without a reboot.
void applyTimeConfig() {
    if (!wifiMgr.isConnected()) return; // AP mode / no internet — clock stays unsynced
    long gmtOffsetSec = (long)(settingsMgr.s.utcOffsetHours * 3600.0f);
    int  dstOffsetSec = settingsMgr.s.observeDst ? 3600 : 0;
    configTime(gmtOffsetSec, dstOffsetSec, settingsMgr.s.ntpServer);
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
}
