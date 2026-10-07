#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "sync.h"

// Identical pattern to the main Hurricane Controls project's wifi_manager.h —
// kept as a separate copy since this is a separate PlatformIO project/board,
// not a shared library.
class WiFiManager {
public:
    enum class Mode { AP, STA };

    // Called once in setup() — blocks up to 12 s trying STA, then falls back to AP
    void begin() {
        loadCreds();
        if (ssid_.length() > 0 && connectSTA()) return;
        startAP();
    }

    // Persist new credentials; call scheduleRestart() to apply them
    void saveCredentials(const String& ssid, const String& pass) {
        ssid_ = ssid;
        pass_ = pass;
        saveCreds();
    }

    void clearCredentials() {
        ssid_ = "";
        pass_ = "";
        saveCreds();
    }

    // WPA2 requires 8-63 characters.
    static bool isValidApPassword(const char* pw) {
        size_t len = strlen(pw);
        return len >= 8 && len <= 63;
    }

    // Persist a new access-point password; call scheduleRestart() to apply it
    void saveApPassword(const String& pw) {
        apPass_ = pw;
        Preferences p;
        p.begin("wifi_cfg", false);
        p.putString("apPass", pw);
        p.end();
    }

    // Schedule an ESP.restart() after delayMs (gives the HTTP response time to send)
    void scheduleRestart(uint32_t delayMs = 1500) {
        restartAt_      = millis() + delayMs;
        restartPending_ = true;
    }

    void update() {
        if (restartPending_ && deadlinePassed(restartAt_)) ESP.restart();
        retryHomeWifi();
    }

    // True exactly once after joining the home network — at boot, or later
    // via the AP-fallback retry below. Used to (re)start NTP.
    bool takeJustConnected() {
        bool j = justConnected_;
        justConnected_ = false;
        return j;
    }

    Mode   getMode()     const { return mode_; }
    bool   hasCreds()    const { return ssid_.length() > 0; }
    String getSSID()     const { return ssid_; }
    bool   isConnected() const { return mode_ == Mode::STA && WiFi.status() == WL_CONNECTED; }
    bool   apSecured()   const { return apPass_.length() >= 8; }
    String getIP()       const {
        return (mode_ == Mode::STA) ? WiFi.localIP().toString()
                                    : WiFi.softAPIP().toString();
    }

    // Device name shown in a router's DHCP client list and used for the
    // ".local" mDNS address — these were previously two separate, both
    // hardcoded identities (nothing set WiFi.setHostname() at all, and
    // MDNS.begin() always used the compile-time MDNS_NAME); now both follow
    // this single configurable value.
    String getHostname() const { return effectiveHostname(); }

    void saveHostname(const String& h) {
        hostname_ = h;
        Preferences p;
        p.begin("wifi_cfg", false);
        p.putString("hostname", h);
        p.end();
    }

private:
    // After a power outage the router usually boots slower than the 12 s
    // STA window in begin(), so the board falls back to AP mode. With saved
    // credentials it keeps the AP up (AP+STA) and retries the home network
    // every STA_RETRY_INTERVAL_MS, for up to STA_RETRY_WINDOW_MS per try,
    // instead of staying stranded in AP mode until someone reboots it.
    static constexpr uint32_t STA_RETRY_INTERVAL_MS = 60000;
    static constexpr uint32_t STA_RETRY_WINDOW_MS   = 15000;

    String   ssid_;
    String   pass_;
    String   hostname_;       // empty = not set yet -> falls back to MDNS_NAME
    String   apPass_;         // < 8 chars = open AP (never lock the owner out)
    Mode     mode_           = Mode::AP;
    bool     restartPending_ = false;
    uint32_t restartAt_      = 0;
    bool     justConnected_  = false;
    bool     retrying_       = false;
    uint32_t retryTs_        = 0;

    String effectiveHostname() const {
        return hostname_.length() ? hostname_ : String(MDNS_NAME);
    }

    bool connectSTA() {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(effectiveHostname().c_str()); // must be set before WiFi.begin()
        WiFi.begin(ssid_.c_str(), pass_.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > 12000) {
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                return false;
            }
            delay(200);
        }
        onConnected();
        return true;
    }

    void onConnected() {
        mode_          = Mode::STA;
        justConnected_ = true;
        MDNS.begin(effectiveHostname().c_str());
        Serial.print("WiFi STA IP: ");
        Serial.println(WiFi.localIP());
    }

    void startAP() {
        // AP+STA when there's a home network to keep retrying (see above).
        WiFi.mode(ssid_.length() > 0 ? WIFI_AP_STA : WIFI_AP);
        WiFi.softAPsetHostname(effectiveHostname().c_str());
        WiFi.setHostname(effectiveHostname().c_str());
        if (apSecured()) WiFi.softAP(WIFI_SSID, apPass_.c_str());
        else             WiFi.softAP(WIFI_SSID);
        mode_    = Mode::AP;
        retryTs_ = millis();
        Serial.print("WiFi AP IP: ");
        Serial.println(WiFi.softAPIP());
    }

    void retryHomeWifi() {
        if (mode_ != Mode::AP || ssid_.length() == 0) return;
        uint32_t now = millis();
        if (!retrying_) {
            if (now - retryTs_ < STA_RETRY_INTERVAL_MS) return;
            retrying_ = true;
            retryTs_  = now;
            WiFi.begin(ssid_.c_str(), pass_.c_str());
        } else if (WiFi.status() == WL_CONNECTED) {
            retrying_ = false;
            WiFi.mode(WIFI_STA);   // drop the fallback AP now that we're home
            onConnected();
        } else if (now - retryTs_ >= STA_RETRY_WINDOW_MS) {
            retrying_ = false;
            retryTs_  = now;
            WiFi.disconnect();     // stop the driver's own reconnect loop until the next try
        }
    }

    void loadCreds() {
        Preferences p;
        p.begin("wifi_cfg", true);
        ssid_     = p.getString("ssid", "");
        pass_     = p.getString("pass", "");
        hostname_ = p.getString("hostname", "");
        apPass_   = p.getString("apPass", AP_PASS_DEFAULT);
        p.end();
    }

    void saveCreds() {
        Preferences p;
        p.begin("wifi_cfg", false);
        p.putString("ssid", ssid_);
        p.putString("pass", pass_);
        p.end();
    }
};

extern WiFiManager wifiMgr;
