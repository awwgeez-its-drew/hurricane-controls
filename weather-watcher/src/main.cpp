#include <Arduino.h>
#include <time.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "sync.h"
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

// Generous, because an HTTPS poll of api.weather.gov legitimately blocks the
// loop for several seconds. Anything longer is a real hang.
static constexpr uint32_t WATCHDOG_TIMEOUT_S = 30;

// Also shown on the Settings page (webserver.h).
const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_INT_WDT:  return "interrupt watchdog";
        case ESP_RST_PANIC:    return "panic/crash";
        case ESP_RST_SW:       return "software (ESP.restart)";
        case ESP_RST_EXT:      return "external reset";
        default:               return "other";
    }
}

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
    CtrlLock lock;
    configTzTime(posixTzFor(settingsMgr.s.timeZone, settingsMgr.s.autoDst), settingsMgr.s.ntpServer);
    lastNtpSyncMs_ = millis();
}

void setup() {
    Serial.begin(115200);
    Serial.print("Reset reason: ");
    Serial.println(resetReasonName());
    settingsMgr.load();

    // WiFi first (may block up to 12s for STA attempt)
    wifiMgr.begin();
    if (wifiMgr.takeJustConnected()) applyTimeConfig();
    nwsClient.begin();
    nwsClient.requestLinkTest(); // confirm the UART link to the main board on boot
    webUI.begin();

    // Armed only after the (blocking) Wi-Fi attempt above.
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    Serial.println("Weather Watcher ready.");
}

void loop() {
    esp_task_wdt_reset();
    wifiMgr.update();  // deferred ESP.restart() + home Wi-Fi retry from AP fallback
    if (wifiMgr.takeJustConnected()) {
        // Joined home Wi-Fi after an AP fallback (e.g. router was still
        // booting) — sync the clock and poll right away.
        applyTimeConfig();
        nwsClient.requestPollSoon();
    }
    nwsClient.update();
    webUI.update();

    uint32_t intervalMs = settingsMgr.s.ntpUpdateHours * 3600000UL;
    if (intervalMs > 0 && millis() - lastNtpSyncMs_ >= intervalMs) applyTimeConfig();
}
