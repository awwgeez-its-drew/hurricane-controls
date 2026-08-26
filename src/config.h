#pragma once
#include <cstdint>

// GPIO assignments
constexpr uint8_t RELAY_CHOPPER  = 25;
constexpr uint8_t SSR_BLOWER     = 26;
constexpr uint8_t RELAY_ROTATOR  = 27;
constexpr uint8_t STATUS_LED     = 2;
constexpr uint8_t BTN_WAIL       = 32;  // dual-function: short=Wail, hold=Manual
constexpr uint8_t BTN_ATTACK     = 33;
constexpr uint8_t BTN_STOP       = 4;   // INPUT_PULLUP, active LOW

// WiFi AP
constexpr char WIFI_SSID[] = "HurricaneControls";
constexpr char WIFI_PASS[] = "";         // open AP
constexpr char MDNS_NAME[] = "hurricane";

constexpr char FW_VERSION[] = "1.3.0";
