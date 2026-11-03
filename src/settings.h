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
    char     meshWhitelist[128] = "3A3C";  // comma-separated Meshtastic short names (case-insensitive) allowed to issue mesh commands; empty = block all. Text Message mode prefixes each line with the sender's short name, not a hex node ID -- "3A3C" is this test node's short name, which just happens to look hex.
    bool     meshWhitelistEnabled = true;  // master on/off for the sender whitelist above. Default true (enforced) -- a deliberate opt-in weakening when turned off: any sender transmitting a properly-addressed, password-correct command is accepted, bypassing meshWhitelist entirely.
    char     meshPassword[64] = "";        // plaintext, optional trailing token on mesh commands ("SIREN WAIL <password>"); empty = not required. PING is exempt.
    bool     weatherAutoTriggerEnabled = true;  // dedicated kill-switch for the Weather Watcher link, independent of buttons.locked

    // Per-event-type Meshtastic broadcast toggles (src/mesh.h's update()),
    // mirroring the grouping of the Weather Watcher's ntfy toggles
    // (weather-watcher/src/settings.h's ntfyNotify*) so the two notification
    // channels can be tuned independently. All default true — every one of
    // these has always broadcast unconditionally, so this only lets an
    // owner opt specific ones OUT, never silently changes existing behavior.
    bool meshNotifyActivation   = true;  // <MODE> ACTIVATED / CYCLE COMPLETED / STOP ACTIVATED
    bool meshNotifySystemEvents = true;  // lockout toggled, weather-trigger toggled, a locked button pressed
    bool meshNotifyWwHealth     = true;  // WEATHER WATCHER LOST/RECOVERED/ERROR
    bool meshNotifyStatus       = true;  // STARTUP COMPLETE + the periodic status broadcast (not the SIREN PING reply, which always answers)
    bool meshNotifyWatches      = true;  // a new Tornado/Severe T-storm Watch appears in the Weather Watcher's mirrored alert list (display-only — never a trigger)

    // NTP fallback — this board normally gets its clock from the Weather
    // Watcher's "WX TIME" push over the UART link; these settings only
    // matter if that link is offline/unpaired and the board has to sync
    // its own clock. Same fields/format as the Weather Watcher's own NTP
    // settings.
    char     ntpServer[64] = "pool.ntp.org";
    char     timeZone[16]  = "EASTERN";   // EASTERN/CENTRAL/MOUNTAIN/ARIZONA/PACIFIC/ALASKA/HAWAII
    bool     autoDst        = true;
    uint32_t ntpUpdateHours = 12;

    // Status Broadcast — replaces the old fixed 12-hour interval for both
    // the Meshtastic STATUS line (mesh.h's buildStatusMessage()) and the
    // SX STATUS push to the Weather Watcher (weather_link.h), which now
    // fire together on this same schedule. Up to 3 daily local times,
    // "HH:MM" 24h format; an empty slot is unused. Whether either channel
    // is actually sent at all is still controlled by meshNotifyStatus
    // (above) and the Weather Watcher's own ntfyPriBoot/
    // ntfyPriScheduledStatus — this only controls the timing and content.
    char statusTime1[6] = "08:00";
    char statusTime2[6] = "20:00";
    char statusTime3[6] = "";

    // Which data points the status message includes. The first 5 default
    // true, matching the fixed fields the old unconditional message always
    // carried — this redesign only makes them optional, it doesn't change
    // what a default install sends. The last 4 are genuinely new content
    // this message never carried before, so they default false.
    bool statusIncludeMode     = true;
    bool statusIncludeLock     = true;
    bool statusIncludeUptime   = true;
    bool statusIncludeCpuTemp  = true;
    bool statusIncludeWwHealth = true;
    bool statusIncludeWifi     = false;
    bool statusIncludeTime     = false;
    bool statusIncludeLastRun  = false;
    bool statusIncludeAlerts   = false;
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
        s.meshWhitelistEnabled = prefs.getBool("meshWLOn", s.meshWhitelistEnabled);
        String mpw = prefs.getString("meshPW", s.meshPassword);
        strlcpy(s.meshPassword, mpw.c_str(), sizeof(s.meshPassword));
        s.weatherAutoTriggerEnabled = prefs.getBool("wxAutoTrig", s.weatherAutoTriggerEnabled);
        s.meshNotifyActivation   = prefs.getBool("meshNtfAct",  s.meshNotifyActivation);
        s.meshNotifySystemEvents = prefs.getBool("meshNtfSys",  s.meshNotifySystemEvents);
        s.meshNotifyWwHealth     = prefs.getBool("meshNtfWw",   s.meshNotifyWwHealth);
        s.meshNotifyStatus       = prefs.getBool("meshNtfStat", s.meshNotifyStatus);
        s.meshNotifyWatches      = prefs.getBool("meshNtfWch",  s.meshNotifyWatches);
        String ntpSrv = prefs.getString("ntpServer", s.ntpServer);
        strlcpy(s.ntpServer, ntpSrv.c_str(), sizeof(s.ntpServer));
        String tz = prefs.getString("timeZone", s.timeZone);
        strlcpy(s.timeZone, tz.c_str(), sizeof(s.timeZone));
        s.autoDst        = prefs.getBool("autoDst",  s.autoDst);
        s.ntpUpdateHours = prefs.getUInt("ntpUpdHrs", s.ntpUpdateHours);
        String st1 = prefs.getString("statusTime1", s.statusTime1);
        strlcpy(s.statusTime1, st1.c_str(), sizeof(s.statusTime1));
        String st2 = prefs.getString("statusTime2", s.statusTime2);
        strlcpy(s.statusTime2, st2.c_str(), sizeof(s.statusTime2));
        String st3 = prefs.getString("statusTime3", s.statusTime3);
        strlcpy(s.statusTime3, st3.c_str(), sizeof(s.statusTime3));
        s.statusIncludeMode     = prefs.getBool("statIncMode", s.statusIncludeMode);
        s.statusIncludeLock     = prefs.getBool("statIncLock", s.statusIncludeLock);
        s.statusIncludeUptime   = prefs.getBool("statIncUp",   s.statusIncludeUptime);
        s.statusIncludeCpuTemp  = prefs.getBool("statIncTemp", s.statusIncludeCpuTemp);
        s.statusIncludeWwHealth = prefs.getBool("statIncWw",   s.statusIncludeWwHealth);
        s.statusIncludeWifi     = prefs.getBool("statIncWifi", s.statusIncludeWifi);
        s.statusIncludeTime     = prefs.getBool("statIncTime", s.statusIncludeTime);
        s.statusIncludeLastRun  = prefs.getBool("statIncRun",  s.statusIncludeLastRun);
        s.statusIncludeAlerts   = prefs.getBool("statIncAlrt", s.statusIncludeAlerts);
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
        prefs.putBool("meshWLOn",       s.meshWhitelistEnabled);
        prefs.putString("meshPW",       s.meshPassword);
        prefs.putBool("wxAutoTrig",     s.weatherAutoTriggerEnabled);
        prefs.putBool("meshNtfAct",     s.meshNotifyActivation);
        prefs.putBool("meshNtfSys",     s.meshNotifySystemEvents);
        prefs.putBool("meshNtfWw",      s.meshNotifyWwHealth);
        prefs.putBool("meshNtfStat",    s.meshNotifyStatus);
        prefs.putBool("meshNtfWch",     s.meshNotifyWatches);
        prefs.putString("ntpServer",    s.ntpServer);
        prefs.putString("timeZone",     s.timeZone);
        prefs.putBool("autoDst",        s.autoDst);
        prefs.putUInt("ntpUpdHrs",      s.ntpUpdateHours);
        prefs.putString("statusTime1", s.statusTime1);
        prefs.putString("statusTime2", s.statusTime2);
        prefs.putString("statusTime3", s.statusTime3);
        prefs.putBool("statIncMode", s.statusIncludeMode);
        prefs.putBool("statIncLock", s.statusIncludeLock);
        prefs.putBool("statIncUp",   s.statusIncludeUptime);
        prefs.putBool("statIncTemp", s.statusIncludeCpuTemp);
        prefs.putBool("statIncWw",   s.statusIncludeWwHealth);
        prefs.putBool("statIncWifi", s.statusIncludeWifi);
        prefs.putBool("statIncTime", s.statusIncludeTime);
        prefs.putBool("statIncRun",  s.statusIncludeLastRun);
        prefs.putBool("statIncAlrt", s.statusIncludeAlerts);
        // Retired in v1.9.0 (the old "Chopper re-on delay" settings) — clear
        // any stale copies; a no-op once they're gone.
        prefs.remove("atkChopDelay");
        prefs.remove("fwChopDelay");
        prefs.end();
    }
};

extern SettingsManager settingsMgr;
