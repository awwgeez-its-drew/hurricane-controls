#pragma once
#include <Preferences.h>
#include <cstring>

struct Settings {
    float    latitude  = 0.0f;
    float    longitude = 0.0f;
    uint32_t pollIntervalSec     = 120;    // how often to poll api.weather.gov
    char     tornadoMode[16]     = "WAIL";  // mode sent for a qualifying Tornado Warning: WAIL/ATTACK/FASTWAIL
    char     thunderstormMode[16] = "WAIL"; // mode sent for a qualifying Severe Thunderstorm Warning
    char     userAgentContact[64] = "";     // e.g. an email — api.weather.gov requires a descriptive User-Agent
};

class SettingsManager {
public:
    Settings s;

    void load() {
        Preferences p;
        p.begin("wxwatch", true);
        s.latitude        = p.getFloat("lat", s.latitude);
        s.longitude       = p.getFloat("lon", s.longitude);
        s.pollIntervalSec = p.getUInt("pollSec", s.pollIntervalSec);
        String tm = p.getString("torMode", s.tornadoMode);
        strlcpy(s.tornadoMode, tm.c_str(), sizeof(s.tornadoMode));
        String tsm = p.getString("tsMode", s.thunderstormMode);
        strlcpy(s.thunderstormMode, tsm.c_str(), sizeof(s.thunderstormMode));
        String ua = p.getString("uaContact", s.userAgentContact);
        strlcpy(s.userAgentContact, ua.c_str(), sizeof(s.userAgentContact));
        p.end();
    }

    void save() {
        Preferences p;
        p.begin("wxwatch", false);
        p.putFloat("lat", s.latitude);
        p.putFloat("lon", s.longitude);
        p.putUInt("pollSec", s.pollIntervalSec);
        p.putString("torMode", s.tornadoMode);
        p.putString("tsMode", s.thunderstormMode);
        p.putString("uaContact", s.userAgentContact);
        p.end();
    }
};

extern SettingsManager settingsMgr;
