#include <Arduino.h>
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

void setup() {
    Serial.begin(115200);
    settingsMgr.load();

    // WiFi first (may block up to 12s for STA attempt)
    wifiMgr.begin();
    nwsClient.begin();
    webUI.begin();

    Serial.println("Weather Watcher ready.");
}

void loop() {
    wifiMgr.update();
    nwsClient.update();
    webUI.update();
}
