#pragma once
#include <Preferences.h>
#include <cstdint>
#include <cstring>

struct Settings {
    uint32_t chopperDelay   = 1500;   // ms after trigger() before chopper turns on
    uint32_t blowerDelay    = 0;      // ms after trigger() before blower turns on
    uint32_t rotatorDelay   = 0;      // ms after trigger() before rotator turns on
    uint32_t wailDuration   = 180000;
    uint32_t attackDuration = 180000;
    uint32_t attackOnTime   = 30000;
    uint32_t attackOffTime  = 8000;
    uint32_t fastWailDuration     = 180000;
    uint32_t fastWailOnTime       = 3000;   // 3s chopper ON
    uint32_t fastWailOffTime      = 3000;   // 3s chopper OFF
    uint32_t fastWailChopperDelay = 0;      // ms delay before chopper re-activates each fast-wail cycle
    uint32_t stopBlowerDelay  = 0;      // ms from stop() before blower off
    uint32_t stopChopperDelay = 2000;   // ms from stop() before chopper off
    uint32_t stopRotDelay     = 3000;   // ms from stop() before rotator off
    uint32_t longPressMs          = 800;   // shared long-press threshold: WAIL->MANUAL and ATTACK->FAST_WAIL
    uint32_t buttonDebounceMs     = 50;    // ms any physical button must be held LOW before its press is trusted (filters transient/EMI glitches)
    uint32_t attackChopperDelay   = 0;     // ms delay before chopper re-activates each attack cycle
    char     webPassword[33] = "Siren123!";
};

class SettingsManager {
public:
    Settings s;

    void load() {
        Preferences prefs;
        prefs.begin("siren", true);
        s.chopperDelay   = prefs.getUInt("chopperDelay",   s.chopperDelay);
        s.blowerDelay    = prefs.getUInt("blowerDelay",    s.blowerDelay);
        s.rotatorDelay   = prefs.getUInt("rotatorDelay",   s.rotatorDelay);
        s.wailDuration   = prefs.getUInt("wailDuration",   s.wailDuration);
        s.attackDuration = prefs.getUInt("attackDuration", s.attackDuration);
        s.attackOnTime   = prefs.getUInt("attackOnTime",   s.attackOnTime);
        s.attackOffTime  = prefs.getUInt("attackOffTime",  s.attackOffTime);
        s.fastWailDuration     = prefs.getUInt("fwDuration",  s.fastWailDuration);
        s.fastWailOnTime       = prefs.getUInt("fwOnTime",    s.fastWailOnTime);
        s.fastWailOffTime      = prefs.getUInt("fwOffTime",   s.fastWailOffTime);
        s.fastWailChopperDelay = prefs.getUInt("fwChopDelay", s.fastWailChopperDelay);
        s.stopBlowerDelay  = prefs.getUInt("stopBlwDelay",  s.stopBlowerDelay);
        s.stopChopperDelay = prefs.getUInt("stopChpDelay",  s.stopChopperDelay);
        s.stopRotDelay     = prefs.getUInt("stopRotDelay",  s.stopRotDelay);
        s.longPressMs          = prefs.getUInt("longPressMs",          s.longPressMs);
        s.buttonDebounceMs     = prefs.getUInt("btnDebounceMs",        s.buttonDebounceMs);
        s.attackChopperDelay   = prefs.getUInt("atkChopDelay",        s.attackChopperDelay);
        String pw = prefs.getString("webPwd", "Siren123!");
        strlcpy(s.webPassword, pw.c_str(), sizeof(s.webPassword));
        prefs.end();
    }

    void save() {
        Preferences prefs;
        prefs.begin("siren", false);
        prefs.putUInt("chopperDelay",   s.chopperDelay);
        prefs.putUInt("blowerDelay",    s.blowerDelay);
        prefs.putUInt("rotatorDelay",   s.rotatorDelay);
        prefs.putUInt("wailDuration",   s.wailDuration);
        prefs.putUInt("attackDuration", s.attackDuration);
        prefs.putUInt("attackOnTime",   s.attackOnTime);
        prefs.putUInt("attackOffTime",  s.attackOffTime);
        prefs.putUInt("fwDuration",  s.fastWailDuration);
        prefs.putUInt("fwOnTime",    s.fastWailOnTime);
        prefs.putUInt("fwOffTime",   s.fastWailOffTime);
        prefs.putUInt("fwChopDelay", s.fastWailChopperDelay);
        prefs.putUInt("stopBlwDelay",  s.stopBlowerDelay);
        prefs.putUInt("stopChpDelay",  s.stopChopperDelay);
        prefs.putUInt("stopRotDelay",  s.stopRotDelay);
        prefs.putUInt("longPressMs",    s.longPressMs);
        prefs.putUInt("btnDebounceMs",  s.buttonDebounceMs);
        prefs.putUInt("atkChopDelay",   s.attackChopperDelay);
        prefs.putString("webPwd",       s.webPassword);
        prefs.end();
    }
};

extern SettingsManager settingsMgr;
