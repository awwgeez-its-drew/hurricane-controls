#pragma once
#include <cstring>

// Candidate NWS event types for the "Other Extreme Emergency" category —
// alerts with severity=Extreme/urgency=Immediate/certainty=Observed that
// don't carry their own structured damage tag the way Tornado/Severe
// Thunderstorm Warnings do. None are included by default: a type only
// triggers once explicitly opted in via Settings (comma-separated codes in
// settingsMgr.s.otherExtremeIncluded) — e.g. Flood Warning stays silent for
// an owner above the flood plain, even though it can carry this severity
// combination, unless they add it themselves.
struct OtherExtremeCandidate {
    const char* code;   // short NVS-friendly token stored in otherExtremeIncluded
    const char* event;  // exact NWS properties.event string to match
    const char* label;  // UI display label
};

static const OtherExtremeCandidate OTHER_EXTREME_CANDIDATES[] = {
    {"FIRE",    "Fire Warning",                   "Fire Warning"},
    {"CEM",     "Civil Emergency Message",        "Civil Emergency Message"},
    {"CIVDNG",  "Civil Danger Warning",           "Civil Danger Warning"},
    {"HAZMAT",  "Hazardous Materials Warning",    "Hazardous Materials Warning"},
    {"RAD",     "Radiological Hazard Warning",    "Radiological Hazard Warning"},
    {"NUKE",    "Nuclear Power Plant Warning",    "Nuclear Power Plant Warning"},
    {"SHELTER", "Shelter In Place Warning",       "Shelter In Place Warning"},
    {"EVAC",    "Evacuation Immediate",           "Evacuation Immediate"},
    {"LAWENF",  "Law Enforcement Warning",        "Law Enforcement Warning"},
    {"LOCAL",   "Local Area Emergency",           "Local Area Emergency"},
    {"911",     "911 Telephone Outage Emergency", "911 Telephone Outage Emergency"},
    {"FFW",     "Flash Flood Warning",            "Flash Flood Warning"},
    {"FLW",     "Flood Warning",                  "Flood Warning"},
    {"XWIND",   "Extreme Wind Warning",           "Extreme Wind Warning"},
    {"TSUNAMI", "Tsunami Warning",                "Tsunami Warning"},
    {"VOLCANO", "Volcano Warning",                "Volcano Warning"},
    {"EQ",      "Earthquake Warning",             "Earthquake Warning"},
    {"DUST",    "Dust Storm Warning",             "Dust Storm Warning"},
};
static constexpr size_t OTHER_EXTREME_CANDIDATE_COUNT =
    sizeof(OTHER_EXTREME_CANDIDATES) / sizeof(OTHER_EXTREME_CANDIDATES[0]);

// Finds the candidate code for a raw NWS event string, or nullptr if this
// event isn't one of the known Other Extreme Emergency candidates at all
// (i.e. it can never qualify, included or not).
inline const char* otherExtremeCodeForEvent(const char* event) {
    for (const auto& c : OTHER_EXTREME_CANDIDATES)
        if (!strcmp(event, c.event)) return c.code;
    return nullptr;
}

// True if `code` appears as a whole comma-separated token inside `included`.
inline bool otherExtremeCodeIncluded(const char* included, const char* code) {
    if (!code || !*code || !included) return false;
    size_t codeLen = strlen(code);
    const char* p = included;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ',') p++;
        const char* end = p;
        while (end > start && end[-1] == ' ') end--;
        if ((size_t)(end - start) == codeLen && !strncmp(start, code, codeLen)) return true;
    }
    return false;
}
