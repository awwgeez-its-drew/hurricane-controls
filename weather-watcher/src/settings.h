#pragma once
#include <Preferences.h>
#include <cstring>

struct Settings {
    float    latitude  = 0.0f;
    float    longitude = 0.0f;
    uint32_t pollIntervalSec = 120;    // how often to poll api.weather.gov

    // Per-category trigger modes: "OFF"/"WAIL"/"ATTACK"/"FASTWAIL". OFF means
    // this category is shown on the dashboard for awareness but never sends
    // a command to the main board. Defaults preserve the original behavior
    // (only confirmed+ tornado and damage-tagged severe t-storm trigger).
    char tornadoEmergencyMode[16]     = "WAIL";  // CATASTROPHIC damage tag
    char tornadoConfirmedMode[16]     = "WAIL";  // OBSERVED, or a damage tag below CATASTROPHIC
    char tornadoUnconfirmedMode[16]   = "OFF";   // base/radar-indicated Tornado Warning
    char thunderstormDestructiveMode[16]  = "WAIL"; // DESTRUCTIVE damage tag
    char thunderstormConsiderableMode[16] = "WAIL"; // CONSIDERABLE damage tag
    char thunderstormBaseMode[16]         = "OFF";  // no damage tag

    // "Other Extreme Emergency": any alert with severity=Extreme AND
    // urgency=Immediate AND certainty=Observed (NWS's own markers for "this
    // is happening right now and it's as serious as it gets") — covers
    // things like Fire Warning, Civil Emergency Message, Hazmat/Radiological/
    // Nuclear warnings, which don't carry a dedicated tag the way Tornado/
    // Severe T-storm do. One shared siren mode for the whole category, but
    // each specific NWS event type must be individually opted in via
    // otherExtremeIncluded (comma-separated candidate codes — see
    // nws_client.h's OTHER_EXTREME_CANDIDATES) — nothing is included by
    // default, so an irrelevant-to-you type (e.g. Flood Warning, if you're
    // not in a flood-prone area) never triggers just because it happens to
    // meet the severity/urgency/certainty bar.
    char otherExtremeMode[16]      = "OFF";
    char otherExtremeIncluded[200] = "";

    char     userAgentContact[64] = "";     // e.g. an email — api.weather.gov requires a descriptive User-Agent
    char     webPassword[33] = "Weather123!"; // login password for this board's own web UI
    bool     repeatOnUpgrade = false;       // re-trigger if an already-triggered event's category escalates further
                                             // (e.g. confirmed Tornado Warning -> Tornado Emergency)
    char     ntpServer[64] = "pool.ntp.org";
    char     timeZone[16]   = "EASTERN";    // EASTERN/CENTRAL/MOUNTAIN/PACIFIC/ALASKA/HAWAII
    bool     autoDst        = true;         // adjust the clock for daylight saving automatically
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

        loadStr(p, "torEmMode",  s.tornadoEmergencyMode,         sizeof(s.tornadoEmergencyMode));
        loadStr(p, "torCfMode",  s.tornadoConfirmedMode,         sizeof(s.tornadoConfirmedMode));
        loadStr(p, "torUcMode",  s.tornadoUnconfirmedMode,       sizeof(s.tornadoUnconfirmedMode));
        loadStr(p, "svrDeMode",  s.thunderstormDestructiveMode,  sizeof(s.thunderstormDestructiveMode));
        loadStr(p, "svrCoMode",  s.thunderstormConsiderableMode, sizeof(s.thunderstormConsiderableMode));
        loadStr(p, "svrBaMode",  s.thunderstormBaseMode,         sizeof(s.thunderstormBaseMode));
        loadStr(p, "otherMode",  s.otherExtremeMode,             sizeof(s.otherExtremeMode));
        loadStr(p, "otherIncl",  s.otherExtremeIncluded,         sizeof(s.otherExtremeIncluded));

        loadStr(p, "uaContact",  s.userAgentContact, sizeof(s.userAgentContact));
        loadStr(p, "webPwd",     s.webPassword,       sizeof(s.webPassword));
        s.repeatOnUpgrade = p.getBool("repeatUpg", s.repeatOnUpgrade);
        loadStr(p, "ntpServer",  s.ntpServer, sizeof(s.ntpServer));
        loadStr(p, "timeZone",   s.timeZone,  sizeof(s.timeZone));
        s.autoDst        = p.getBool("autoDst", s.autoDst);
        s.ntpUpdateHours = p.getUInt("ntpUpdHrs", s.ntpUpdateHours);
        p.end();
    }

    void save() {
        Preferences p;
        p.begin("wxwatch", false);
        p.putFloat("lat", s.latitude);
        p.putFloat("lon", s.longitude);
        p.putUInt("pollSec", s.pollIntervalSec);

        p.putString("torEmMode", s.tornadoEmergencyMode);
        p.putString("torCfMode", s.tornadoConfirmedMode);
        p.putString("torUcMode", s.tornadoUnconfirmedMode);
        p.putString("svrDeMode", s.thunderstormDestructiveMode);
        p.putString("svrCoMode", s.thunderstormConsiderableMode);
        p.putString("svrBaMode", s.thunderstormBaseMode);
        p.putString("otherMode", s.otherExtremeMode);
        p.putString("otherIncl", s.otherExtremeIncluded);

        p.putString("uaContact", s.userAgentContact);
        p.putString("webPwd", s.webPassword);
        p.putBool("repeatUpg", s.repeatOnUpgrade);
        p.putString("ntpServer", s.ntpServer);
        p.putString("timeZone", s.timeZone);
        p.putBool("autoDst", s.autoDst);
        p.putUInt("ntpUpdHrs", s.ntpUpdateHours);
        p.end();
    }

private:
    static void loadStr(Preferences& p, const char* key, char* field, size_t fieldSize) {
        String v = p.getString(key, field);
        strlcpy(field, v.c_str(), fieldSize);
    }
};

extern SettingsManager settingsMgr;
