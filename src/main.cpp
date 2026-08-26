#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "settings.h"
#include "motors.h"
#include "statemachine.h"
#include "buttons.h"
#include "wifi_manager.h"
#include "webserver.h"

// ── Singletons ────────────────────────────────────────────────────────────────
SettingsManager settingsMgr;
StateMachine    sm;
ButtonHandler   buttons;
WiFiManager     wifiMgr;
WebUI           webUI;

static void logResetReason() {
    Serial.print("Reset reason: ");
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  Serial.println("power-on");               break;
        case ESP_RST_BROWNOUT: Serial.println("BROWNOUT");               break;
        case ESP_RST_TASK_WDT: Serial.println("task watchdog");          break;
        case ESP_RST_INT_WDT:  Serial.println("interrupt watchdog");     break;
        case ESP_RST_PANIC:    Serial.println("panic/crash");            break;
        case ESP_RST_SW:       Serial.println("software (ESP.restart)"); break;
        default:                Serial.printf("other (%d)\n", (int)esp_reset_reason()); break;
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
    logResetReason();

    settingsMgr.load();
    sm.begin();
    buttons.begin();

    // WiFi first (may block up to 12 s for STA attempt)
    wifiMgr.begin();
    webUI.begin();

    Serial.println("Hurricane Controls ready.");
}

void loop() {
    buttons.update();
    sm.update();
    webUI.update();
    wifiMgr.update();  // handles deferred ESP.restart() after WiFi credential changes
}
