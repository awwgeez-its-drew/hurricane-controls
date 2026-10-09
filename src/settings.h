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
    uint32_t stopBlowerDelay  = 0;      // ms from stop() before blower off
    uint32_t stopChopperDelay = 2000;   // ms from stop() before chopper off
    uint32_t stopRotDelay     = 3000;   // ms from stop() before rotator off
    uint32_t longPressMs          = 800;   // shared long-press threshold: WAIL->MANUAL and ATTACK->FAST_WAIL
    uint32_t buttonDebounceMs     = 50;    // ms any physical button must be held LOW before its press is trusted (filters transient/EMI glitches)
    uint32_t growlBlowerTime  = 5000;   // Growl Test: blower-alone duration
    uint32_t growlRotatorTime = 5000;   // Growl Test: rotator-alone duration
    uint32_t growlChopperTime = 5000;   // Growl Test: chopper-alone duration
    char     webPassword[33] = "Siren123!";
    char     meshWhitelist[128] = "3A3C";  // comma-separated Meshtastic sender IDs (hex, case-insensitive) allowed to issue mesh commands; empty = block all
    char     meshPassword[64] = "";        // plaintext, optional trailing token on mesh commands ("SIREN WAIL <password>"); empty = not required. PING is exempt.
    bool     weatherAutoTriggerEnabled = true;  // dedicated kill-switch for the Weather Watcher link, independent of buttons.locked

    // NTP fallback — this board normally gets its clock from the Weather
    // Watcher's "WX TIME" push over the UART link; these settings only
    // matter if that link is offline/unpaired and the board has to sync
    // its own clock. Same fields/format as the Weather Watcher's own NTP
    // settings.
    char     ntpServer[64] = "pool.ntp.org";
    char     timeZone[16]  = "EASTERN";   // EASTERN/CENTRAL/MOUNTAIN/ARIZONA/PACIFIC/ALASKA/HAWAII
    bool     autoDst        = true;
    uint32_t ntpUpdateHours = 12;
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
        s.stopBlowerDelay  = prefs.getUInt("stopBlwDelay",  s.stopBlowerDelay);
        s.stopChopperDelay = prefs.getUInt("stopChpDelay",  s.stopChopperDelay);
        s.stopRotDelay     = prefs.getUInt("stopRotDelay",  s.stopRotDelay);
        s.longPressMs          = prefs.getUInt("longPressMs",          s.longPressMs);
        s.buttonDebounceMs     = prefs.getUInt("btnDebounceMs",        s.buttonDebounceMs);
        s.growlBlowerTime  = prefs.getUInt("growlBlwTime", s.growlBlowerTime);
        s.growlRotatorTime = prefs.getUInt("growlRotTime", s.growlRotatorTime);
        s.growlChopperTime = prefs.getUInt("growlChpTime", s.growlChopperTime);
        String pw = prefs.getString("webPwd", "Siren123!");
        strlcpy(s.webPassword, pw.c_str(), sizeof(s.webPassword));
        String wl = prefs.getString("meshWL", s.meshWhitelist);
        strlcpy(s.meshWhitelist, wl.c_str(), sizeof(s.meshWhitelist));
        String mpw = prefs.getString("meshPW", s.meshPassword);
        strlcpy(s.meshPassword, mpw.c_str(), sizeof(s.meshPassword));
        s.weatherAutoTriggerEnabled = prefs.getBool("wxAutoTrig", s.weatherAutoTriggerEnabled);
        String ntpSrv = prefs.getString("ntpServer", s.ntpServer);
        strlcpy(s.ntpServer, ntpSrv.c_str(), sizeof(s.ntpServer));
        String tz = prefs.getString("timeZone", s.timeZone);
        strlcpy(s.timeZone, tz.c_str(), sizeof(s.timeZone));
        s.autoDst        = prefs.getBool("autoDst",  s.autoDst);
        s.ntpUpdateHours = prefs.getUInt("ntpUpdHrs", s.ntpUpdateHours);
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
        prefs.putUInt("stopBlwDelay",  s.stopBlowerDelay);
        prefs.putUInt("stopChpDelay",  s.stopChopperDelay);
        prefs.putUInt("stopRotDelay",  s.stopRotDelay);
        prefs.putUInt("longPressMs",    s.longPressMs);
        prefs.putUInt("btnDebounceMs",  s.buttonDebounceMs);
        prefs.putUInt("growlBlwTime", s.growlBlowerTime);
        prefs.putUInt("growlRotTime", s.growlRotatorTime);
        prefs.putUInt("growlChpTime", s.growlChopperTime);
        prefs.putString("webPwd",       s.webPassword);
        prefs.putString("meshWL",       s.meshWhitelist);
        prefs.putString("meshPW",       s.meshPassword);
        prefs.putBool("wxAutoTrig",     s.weatherAutoTriggerEnabled);
        prefs.putString("ntpServer",    s.ntpServer);
        prefs.putString("timeZone",     s.timeZone);
        prefs.putBool("autoDst",        s.autoDst);
        prefs.putUInt("ntpUpdHrs",      s.ntpUpdateHours);
        // Retired in v1.9.0 (the old "Chopper re-on delay" settings) — clear
        // any stale copies; a no-op once they're gone.
        prefs.remove("atkChopDelay");
        prefs.remove("fwChopDelay");
        prefs.end();
    }
};

extern SettingsManager settingsMgr;
