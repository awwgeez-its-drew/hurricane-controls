#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"

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

    // Schedule an ESP.restart() after delayMs (gives the HTTP response time to send)
    void scheduleRestart(uint32_t delayMs = 1500) {
        restartAt_      = millis() + delayMs;
        restartPending_ = true;
    }

    void update() {
        if (restartPending_ && millis() >= restartAt_) ESP.restart();
    }

    Mode   getMode()     const { return mode_; }
    bool   hasCreds()    const { return ssid_.length() > 0; }
    String getSSID()     const { return ssid_; }
    bool   isConnected() const { return mode_ == Mode::STA && WiFi.status() == WL_CONNECTED; }
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
    String   ssid_;
    String   pass_;
    String   hostname_;       // empty = not set yet -> falls back to MDNS_NAME
    Mode     mode_           = Mode::AP;
    bool     restartPending_ = false;
    uint32_t restartAt_      = 0;

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
        mode_ = Mode::STA;
        MDNS.begin(effectiveHostname());
        Serial.print("WiFi STA IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }

    void startAP() {
        WiFi.mode(WIFI_AP);
        WiFi.softAPsetHostname(effectiveHostname().c_str());
        WiFi.softAP(WIFI_SSID);
        mode_ = Mode::AP;
        Serial.print("WiFi AP IP: ");
        Serial.println(WiFi.softAPIP());
    }

    void loadCreds() {
        Preferences p;
        p.begin("wifi_cfg", true);
        ssid_     = p.getString("ssid", "");
        pass_     = p.getString("pass", "");
        hostname_ = p.getString("hostname", "");
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
