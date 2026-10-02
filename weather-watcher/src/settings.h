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
    char     webPassword[33] = "Weather123!"; // login password for this board's own web UI
    bool     repeatOnUpgrade = false;       // re-trigger if an already-triggered event's tier escalates further
                                             // (e.g. confirmed Tornado Warning -> Tornado Emergency)
    char     ntpServer[64] = "pool.ntp.org";
    char     posixTz[48]   = "UTC0";        // POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0" for US
                                             // Eastern — encodes DST transition dates, so the clock
                                             // adjusts for DST automatically with no manual toggle
    uint32_t ntpUpdateHours = 12;           // how often to re-sync with the NTP server
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
        String pw = p.getString("webPwd", s.webPassword);
        strlcpy(s.webPassword, pw.c_str(), sizeof(s.webPassword));
        s.repeatOnUpgrade = p.getBool("repeatUpg", s.repeatOnUpgrade);
        String ntp = p.getString("ntpServer", s.ntpServer);
        strlcpy(s.ntpServer, ntp.c_str(), sizeof(s.ntpServer));
        String tz = p.getString("posixTz", s.posixTz);
        strlcpy(s.posixTz, tz.c_str(), sizeof(s.posixTz));
        s.ntpUpdateHours = p.getUInt("ntpUpdHrs", s.ntpUpdateHours);
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
        p.putString("webPwd", s.webPassword);
        p.putBool("repeatUpg", s.repeatOnUpgrade);
        p.putString("ntpServer", s.ntpServer);
        p.putString("posixTz", s.posixTz);
        p.putUInt("ntpUpdHrs", s.ntpUpdateHours);
        p.end();
    }
};

extern SettingsManager settingsMgr;
