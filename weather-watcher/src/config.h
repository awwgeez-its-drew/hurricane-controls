#pragma once
#include <cstdint>

// UART link to the Hurricane Controls main board — cross-connect with its
// WEATHER_RX_PIN/WEATHER_TX_PIN (src/config.h in the parent project):
//   this board's LINK_TX_PIN  -> main board's WEATHER_RX_PIN
//   this board's LINK_RX_PIN  <- main board's WEATHER_TX_PIN
// plus a shared ground. This is a separate board, so these pin numbers don't
// collide with anything the main board itself uses.
constexpr uint8_t  LINK_RX_PIN = 16;
constexpr uint8_t  LINK_TX_PIN = 17;
constexpr uint32_t LINK_BAUD   = 38400;

constexpr uint8_t STATUS_LED = 2;

// WiFi AP (fallback when no saved credentials, or STA join fails)
constexpr char WIFI_SSID[] = "WeatherWatcher";
constexpr char WIFI_PASS[] = "";
constexpr char MDNS_NAME[] = "weather-watcher";

constexpr char FW_VERSION[] = "1.2.0";
