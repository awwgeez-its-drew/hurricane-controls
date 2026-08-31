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

// Meshtastic remote-control link (UART2, optional — wire to a Heltec V3 or
// similar Meshtastic node running its Serial Module in plain-text mode)
constexpr uint8_t  MESH_RX_PIN = 16;   // ESP32 RX2 — wire to Meshtastic node's serial TX
constexpr uint8_t  MESH_TX_PIN = 17;   // ESP32 TX2 — wire to Meshtastic node's serial RX
constexpr uint32_t MESH_BAUD   = 38400; // must match the Meshtastic Serial Module's baud setting
constexpr char     MESH_COMMAND_PREFIX[] = "SIREN"; // required prefix, case-insensitive; change per-unit if running multiple sirens on one channel

// WiFi AP
constexpr char WIFI_SSID[] = "HurricaneControls";
constexpr char WIFI_PASS[] = "";         // open AP
constexpr char MDNS_NAME[] = "hurricane";

constexpr char FW_VERSION[] = "1.3.0";
