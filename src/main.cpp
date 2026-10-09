#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "sync.h"
#include "clock.h"
#include "settings.h"
#include "motors.h"
#include "runlog.h"
#include "statemachine.h"
#include "buttons.h"
#include "wifi_manager.h"
#include "webserver.h"
#include "mesh.h"
#include "weather_link.h"

// ── Singletons ────────────────────────────────────────────────────────────────
SettingsManager settingsMgr;
RunLog          runLog;
StateMachine    sm;
ButtonHandler   buttons;
WiFiManager     wifiMgr;
WebUI           webUI;
MeshBridge      meshBridge;
WeatherLink     weatherLink;

// Loop stalls longer than this reboot the board. setup() drives every relay
// OFF before anything else, so a watchdog reset always fails safe.
static constexpr uint32_t WATCHDOG_TIMEOUT_S = 8;

// Also shown on the Settings page (webserver.h) — handy for spotting
// brownouts caused by relay/motor switching.
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

void setup() {
    // Outputs — assert the OFF level before enabling the driver, so there is no
    // window where a freshly-reset GPIO could sit at its post-reset default
    // (which for an active-LOW relay pin would read as energized) before the
    // first explicit write takes effect.
    digitalWrite(RELAY_CHOPPER, HIGH); pinMode(RELAY_CHOPPER, OUTPUT);
    digitalWrite(SSR_BLOWER,    LOW);  pinMode(SSR_BLOWER,    OUTPUT);
    digitalWrite(RELAY_ROTATOR, HIGH); pinMode(RELAY_ROTATOR, OUTPUT);
    pinMode(STATUS_LED, OUTPUT); digitalWrite(STATUS_LED, LOW);

    Serial.begin(115200);
    Serial.print("Reset reason: ");
    Serial.println(resetReasonName());

    settingsMgr.load();
    applyTimeZone(settingsMgr.s);
    runLog.begin();
    sm.begin();
    buttons.begin();
    meshBridge.begin();
    weatherLink.begin();

    // WiFi first (may block up to 12 s for STA attempt)
    wifiMgr.begin();
    webUI.begin();

    // Armed only after the (blocking) Wi-Fi attempt above.
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    Serial.println("Hurricane Controls ready.");
    meshBridge.announceStartup();
}

static uint32_t wifiConnectedAtMs_ = 0;

void loop() {
    esp_task_wdt_reset();
    {
        CtrlLock lock;
        buttons.update();
        sm.update();
        meshBridge.update();
        weatherLink.update();
    }
    webUI.update();
    wifiMgr.update();  // deferred ESP.restart() + home Wi-Fi retry from AP fallback
    if (wifiMgr.takeJustConnected()) wifiConnectedAtMs_ = millis();

    // Time: the Weather Watcher's "WX TIME" push (weather_link.h) is the
    // primary clock source. This board's own NTP only kicks in as a
    // fallback once updateNtpFallback() decides the WX link hasn't shown up.
    updateNtpFallback(settingsMgr.s, wifiMgr.isConnected(), millis() - wifiConnectedAtMs_);
}
