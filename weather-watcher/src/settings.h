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

    // ── ntfy push notifications (WP4) ────────────────────────────────────
    bool     ntfyEnabled   = false;
    char     ntfyServer[64] = "https://ntfy.sh";
    char     ntfyTopic[64]  = "";
    // Write-only: never echoed back by /settings-data, never logged. A
    // non-empty value here means "keep the stored token" on save unless
    // explicitly replaced — see webserver.h's settings POST handler.
    char     ntfyToken[80]  = "";
    bool     ntfyVerifyCert = true;         // Decision A — off = setInsecure(), explicit opt-out only

    // ── Granular ntfy priorities ─────────────────────────────────────────
    // Each category is its own ntfy priority (1 = min ... 5 = max/urgent,
    // matching ntfy's own scale — see ntfy.h), or 0 to send nothing for
    // that category at all. Replaces the old plain on/off toggles (below,
    // kept only as migration source in load()) with per-category urgency.
    //
    // One field per alert category this board can classify into (see
    // AlertCategory in nws_client.h) — independent of whether that
    // category's siren mode is OFF, so "notify me but don't trigger" and
    // "trigger but don't notify" are both expressible.
    int8_t ntfyPriThunderstormBase        = 0;
    int8_t ntfyPriOtherExtreme            = 5;
    int8_t ntfyPriThunderstormConsiderable = 0;
    int8_t ntfyPriTornadoUnconfirmed       = 0;
    int8_t ntfyPriThunderstormDestructive  = 4;
    int8_t ntfyPriTornadoConfirmed         = 5;
    int8_t ntfyPriTornadoEmergency         = 5;

    // Display-only — Tornado/Severe T-storm Watches never trigger the
    // siren (see nws_client.h's classify()/categoryIsWatch()). Off by
    // default, same as the other lower-urgency categories above.
    int8_t ntfyPriTornadoWatch       = 0;
    int8_t ntfyPriSevereTstormWatch  = 0;

    // Fault conditions: NWS poll failures/recoveries and a trigger queue
    // disposition of NO_RESPONSE/GAVE_UP_* (the siren never actually
    // confirmed the command) all share this one priority, EXCEPT the
    // siren-didn't-respond case, which is always sent at the max priority
    // (5) whenever this is non-zero — a non-responding siren is always
    // more urgent than an NWS API hiccup, regardless of what's configured
    // here. 0 = no fault notifications of any kind, including that one.
    int8_t ntfyPriFaults = 3;

    // Any siren activation regardless of source (local button, web panel,
    // Meshtastic, or NWS auto-trigger) — one category, since the
    // notification text itself names the activation point. Replaces the
    // old three separate per-source toggles. Does not cover a
    // weather-category-triggered activation driven entirely by this
    // board's own trigger queue — see the per-category priorities above,
    // which already gate that independently (no double notification;
    // see nws_client.h's finalizeQueueHead()/handleSxEvent()).
    int8_t ntfyPriActivations = 0;   // off by default — "someone's using the siren", not a fault

    int8_t ntfyPriStop         = 3;  // siren stopped or a run's cycle completed
    int8_t ntfyPriLockedPress  = 0;  // a physical button pressed while locked out — off by default
    int8_t ntfyPriBoot         = 0;  // main board's boot announcement
    int8_t ntfyPriScheduledStatus = 0; // main board's scheduled status broadcast

    // NVS keys "ntfyCTsB"/"ntfyCOE"/.../"ntfyErr"/"ntfySrcL"/"ntfySrcW"/
    // "ntfySrcM"/"ntfySysEv"/"ntfyStop"/"ntfyStat" (the old plain on/off
    // toggles these fields replace) are no longer written, but load()
    // reads them once, if present, to migrate an existing install's
    // choices into the new ntfyPri* fields above without silently
    // changing anyone's notification behavior on update.
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

        s.ntfyEnabled    = p.getBool("ntfyOn", s.ntfyEnabled);
        loadStr(p, "ntfySrv", s.ntfyServer, sizeof(s.ntfyServer));
        loadStr(p, "ntfyTopic", s.ntfyTopic, sizeof(s.ntfyTopic));
        loadStr(p, "ntfyTok", s.ntfyToken, sizeof(s.ntfyToken));
        s.ntfyVerifyCert = p.getBool("ntfyVerCert", s.ntfyVerifyCert);

        loadPri(p, "ntfyPriThunderstormBase",        s.ntfyPriThunderstormBase,        "ntfyCTsB",  5);
        loadPri(p, "ntfyPriOtherExtreme",             s.ntfyPriOtherExtreme,             "ntfyCOE",   5);
        loadPri(p, "ntfyPriThunderstormConsiderable", s.ntfyPriThunderstormConsiderable, "ntfyCTsC",  5);
        loadPri(p, "ntfyPriTornadoUnconfirmed",       s.ntfyPriTornadoUnconfirmed,       "ntfyCToU",  5);
        loadPri(p, "ntfyPriThunderstormDestructive",  s.ntfyPriThunderstormDestructive,  "ntfyCTsD",  5);
        loadPri(p, "ntfyPriTornadoConfirmed",         s.ntfyPriTornadoConfirmed,         "ntfyCToC",  5);
        loadPri(p, "ntfyPriTornadoEmergency",         s.ntfyPriTornadoEmergency,         "ntfyCToE",  5);
        s.ntfyPriTornadoWatch      = (int8_t)p.getInt("ntfyPriTorWa", s.ntfyPriTornadoWatch);
        s.ntfyPriSevereTstormWatch = (int8_t)p.getInt("ntfyPriSvrWa", s.ntfyPriSevereTstormWatch);
        loadPri(p, "ntfyPriFaults",        s.ntfyPriFaults,        "ntfyErr",   3);
        loadPri(p, "ntfyPriStop",          s.ntfyPriStop,          "ntfyStop",  3);
        loadPri(p, "ntfyPriLockedPress",   s.ntfyPriLockedPress,   "ntfySysEv", 4);
        loadPri(p, "ntfyPriBoot",          s.ntfyPriBoot,          "ntfyStat",  3);
        loadPri(p, "ntfyPriScheduledStatus", s.ntfyPriScheduledStatus, "ntfyStat", 3);
        // Activations migrates from whichever of the three old per-source
        // toggles existed and was true — any one of them being on means
        // the merged category should come on too.
        if (p.isKey("ntfyPriActivations")) {
            s.ntfyPriActivations = (int8_t)p.getInt("ntfyPriActivations", s.ntfyPriActivations);
        } else if (p.isKey("ntfySrcL") || p.isKey("ntfySrcW") || p.isKey("ntfySrcM")) {
            bool anyOn = p.getBool("ntfySrcL", false) || p.getBool("ntfySrcW", false) || p.getBool("ntfySrcM", false);
            s.ntfyPriActivations = anyOn ? 5 : 0;
        }
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

        p.putBool("ntfyOn", s.ntfyEnabled);
        p.putString("ntfySrv", s.ntfyServer);
        p.putString("ntfyTopic", s.ntfyTopic);
        p.putString("ntfyTok", s.ntfyToken);
        p.putBool("ntfyVerCert", s.ntfyVerifyCert);
        p.putInt("ntfyPriThunderstormBase",        s.ntfyPriThunderstormBase);
        p.putInt("ntfyPriOtherExtreme",             s.ntfyPriOtherExtreme);
        p.putInt("ntfyPriThunderstormConsiderable", s.ntfyPriThunderstormConsiderable);
        p.putInt("ntfyPriTornadoUnconfirmed",       s.ntfyPriTornadoUnconfirmed);
        p.putInt("ntfyPriThunderstormDestructive",  s.ntfyPriThunderstormDestructive);
        p.putInt("ntfyPriTornadoConfirmed",         s.ntfyPriTornadoConfirmed);
        p.putInt("ntfyPriTornadoEmergency",         s.ntfyPriTornadoEmergency);
        p.putInt("ntfyPriTorWa", s.ntfyPriTornadoWatch);
        p.putInt("ntfyPriSvrWa", s.ntfyPriSevereTstormWatch);
        p.putInt("ntfyPriFaults",          s.ntfyPriFaults);
        p.putInt("ntfyPriActivations",     s.ntfyPriActivations);
        p.putInt("ntfyPriStop",            s.ntfyPriStop);
        p.putInt("ntfyPriLockedPress",     s.ntfyPriLockedPress);
        p.putInt("ntfyPriBoot",            s.ntfyPriBoot);
        p.putInt("ntfyPriScheduledStatus", s.ntfyPriScheduledStatus);
        p.end();
    }

private:
    static void loadStr(Preferences& p, const char* key, char* field, size_t fieldSize) {
        String v = p.getString(key, field);
        strlcpy(field, v.c_str(), fieldSize);
    }

    // Loads a new ntfyPri* field, migrating a pre-2.0.2 install's plain
    // on/off toggle the first time this runs after updating: if the new
    // key was never saved yet but the old boolean key exists, the field
    // becomes `onValue` (the priority that toggle used to imply) when the
    // old toggle was true, or 0 (off) when it was false -- so updating
    // never silently starts or stops a notification category that was
    // already set one way. A brand-new install (neither key present)
    // keeps the struct's own in-code default untouched.
    static void loadPri(Preferences& p, const char* newKey, int8_t& field, const char* legacyKey, int8_t onValue) {
        if (p.isKey(newKey)) {
            field = (int8_t)p.getInt(newKey, field);
        } else if (p.isKey(legacyKey)) {
            field = p.getBool(legacyKey, false) ? onValue : 0;
        }
    }
};

extern SettingsManager settingsMgr;
