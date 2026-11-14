// Motorsport Live's model: OpenF1's rows into the schedule, results, standings, live timing and the circuit. No Arduino.
#include "mts_Model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <initializer_list>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Settings -----------------------------------------------------------------------------------------------------------
void mts_Settings_Default(Mts_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = MTS_SETTINGS_MAGIC;
    s.pages = (uint8_t)((1u << MTS_PAGES) - 1);
    s.cycle = 1;
    s.seconds = 15;
    s.hour24 = 1;
    s.liveFirst = 1;
    s.alertMinutes = 15;
    s.alertRacesOnly = 1;
    s.alertFlags = 1;
    s.sound = 1;
    snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

void mts_Settings_Clamp(Mts_Settings_t& s){
    s.magic = MTS_SETTINGS_MAGIC;
    s.user[sizeof(s.user) - 1] = '\0';
    s.pass[sizeof(s.pass) - 1] = '\0';
    if (s.favourite > 99) s.favourite = 0;
    s.pages &= (uint8_t)((1u << MTS_PAGES) - 1);
    if (!s.pages) s.pages = 1u << MTS_P_WEEKEND;
    s.cycle = s.cycle ? 1 : 0;
    if (s.seconds < 5 || s.seconds > 120) s.seconds = 15;
    s.hour24 = s.hour24 ? 1 : 0;
    s.liveFirst = s.liveFirst ? 1 : 0;
    if (s.alertMinutes > 120) s.alertMinutes = 15;
    s.alertRacesOnly = s.alertRacesOnly ? 1 : 0;
    s.alertFlags = s.alertFlags ? 1 : 0;
    s.sound = s.sound ? 1 : 0;
    s.phoneAlerts = s.phoneAlerts ? 1 : 0;
    for (uint8_t i = 0; i < NTF_CHANNELS; i++) s.notify.on[i] = s.notify.on[i] ? 1 : 0;
    s.notify.ntfyServer[sizeof(s.notify.ntfyServer) - 1] = '\0';
    if (!s.notify.ntfyServer[0]) snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
    s.notify.ntfyTopic[sizeof(s.notify.ntfyTopic) - 1] = '\0';
    s.notify.ntfyToken[sizeof(s.notify.ntfyToken) - 1] = '\0';
    s.notify.telegramToken[sizeof(s.notify.telegramToken) - 1] = '\0';
    s.notify.telegramChat[sizeof(s.notify.telegramChat) - 1] = '\0';
    s.notify.whatsappPhone[sizeof(s.notify.whatsappPhone) - 1] = '\0';
    s.notify.whatsappKey[sizeof(s.notify.whatsappKey) - 1] = '\0';
}

// Times and words -------------------------------------------------------------------------------------------------------
static int64_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint64_t mts_Parse_Time_Ms(const char* t){
    int y, mo, d, h, mi, s;
    if (!t || sscanf(t, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return 0;
    const char* p = t + 19;
    int ms = 0;
    if (*p == '.'){
        p++;
        int digits = 0;
        while (isdigit((unsigned char)*p)){ if (digits < 3){ ms = ms * 10 + (*p - '0'); } digits++; p++; }
        for (; digits < 3; digits++) ms *= 10;
    }
    int64_t off = 0;
    if (*p == '+' || *p == '-'){
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%d:%d", &oh, &om) >= 1) off = (int64_t)(oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    }
    const int64_t secs = days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - off;
    return (uint64_t)secs * 1000u + (uint64_t)ms;
}

void mts_Iso_Ms(char* dest, size_t size, uint64_t ms){
    const time_t tt = (time_t)(ms / 1000u);
    struct tm u;
    gmtime_r(&tt, &u);
    char base[24];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &u);
    snprintf(dest, size, "%s.%03u", base, (unsigned)(ms % 1000u));
}

int32_t mts_Parse_Offset(const char* text){
    if (!text || !*text) return 0;
    const int sign = text[0] == '-' ? -1 : 1;
    const char* p = (text[0] == '-' || text[0] == '+') ? text + 1 : text;
    int h = 0, m = 0, s = 0;
    if (sscanf(p, "%d:%d:%d", &h, &m, &s) < 1) return 0;
    return sign * (h * 3600 + m * 60 + s);
}

static bool has(const char* text, const char* word){ return text && strstr(text, word) != nullptr; }

uint8_t mts_Type_Of(const char* type, const char* name){
    if (has(name, "Sprint") && (has(name, "Qualifying") || has(name, "Shootout"))) return MTS_SPRINT_QUALI;
    if (has(name, "Sprint")) return MTS_SPRINT;
    if (has(type, "Qualifying") || has(name, "Qualifying")) return MTS_QUALIFYING;
    if (has(type, "Race")) return MTS_RACE;
    if (has(name, "Day")) return MTS_TESTING;
    return MTS_PRACTICE;
}

const char* mts_Type_Short(uint8_t type){
    switch (type){
        case MTS_QUALIFYING: return "QUALI";
        case MTS_SPRINT_QUALI: return "SPRINT QUALI";
        case MTS_SPRINT: return "SPRINT";
        case MTS_RACE: return "RACE";
        case MTS_TESTING: return "TESTING";
        default: return "PRACTICE";
    }
}

uint8_t mts_Compound_Of(const char* t){
    if (!t) return MTS_TYRE_UNKNOWN;
    if (!strcmp(t, "SOFT")) return MTS_TYRE_SOFT;
    if (!strcmp(t, "MEDIUM")) return MTS_TYRE_MEDIUM;
    if (!strcmp(t, "HARD")) return MTS_TYRE_HARD;
    if (!strcmp(t, "INTERMEDIATE")) return MTS_TYRE_INTER;
    if (!strcmp(t, "WET")) return MTS_TYRE_WET;
    return MTS_TYRE_UNKNOWN;
}

char mts_Compound_Letter(uint8_t c){ return "?SMHIW"[c < 6 ? c : 0]; }

uint16_t mts_Compound_Color(uint8_t c){
    switch (c){
        case MTS_TYRE_SOFT: return RGB(235, 40, 40);
        case MTS_TYRE_MEDIUM: return RGB(255, 210, 0);
        case MTS_TYRE_HARD: return RGB(235, 235, 235);
        case MTS_TYRE_INTER: return RGB(40, 200, 60);
        case MTS_TYRE_WET: return RGB(40, 120, 255);
        default: return RGB(110, 110, 110);
    }
}

uint16_t mts_Team_Color(const char* hex){
    unsigned v = 0x808080;
    if (hex && strlen(hex) == 6) v = (unsigned)strtoul(hex, nullptr, 16);
    int r = (int)((v >> 16) & 255), g = (int)((v >> 8) & 255), b = (int)(v & 255);
    // Dark team colours vanish on the panel: lift them.
    const int peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (peak < 110){
        const float k = peak ? 110.0f / (float)peak : 1.0f;
        r = (int)(r * k); g = (int)(g * k); b = (int)(b * k);
        if (!peak) r = g = b = 110;
    }
    return RGB(r, g, b);
}

void mts_Clock(char* dest, size_t size, uint32_t t, int32_t utcOffset, bool hour24){
    const int64_t local = (int64_t)t + utcOffset;
    const int s = (int)(((local % 86400) + 86400) % 86400);
    const int h = s / 3600, m = (s / 60) % 60;
    if (hour24) snprintf(dest, size, "%02d:%02d", h, m);
    else snprintf(dest, size, "%d:%02d%s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}

void mts_Countdown(char* dest, size_t size, uint32_t s){
    const uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60, x = s % 60;
    if (d) snprintf(dest, size, "%lu D %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)x);
    else if (h) snprintf(dest, size, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)x);
    else snprintf(dest, size, "%02lu:%02lu", (unsigned long)m, (unsigned long)x);
}

void mts_Lap_Time(char* dest, size_t size, float t){
    if (!(t > 0)){ snprintf(dest, size, "-"); return; }
    const int ms = (int)lroundf(t * 1000.0f);
    snprintf(dest, size, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
}

void mts_Gap_Text(char* dest, size_t size, float gap, int8_t lapsDown, bool leader){
    if (leader) snprintf(dest, size, "LEADER");
    else if (lapsDown > 0) snprintf(dest, size, "+%d LAP%s", lapsDown, lapsDown > 1 ? "S" : "");
    else if (isnan(gap)) snprintf(dest, size, "-");
    else if (gap < 60) snprintf(dest, size, "+%.3f", (double)gap);
    else snprintf(dest, size, "+%d:%06.3f", (int)(gap / 60), (double)fmodf(gap, 60.0f));
}

const char* mts_Flag_Text(uint8_t f){
    switch (f){
        case MTS_FLAG_GREEN: return "GREEN FLAG";
        case MTS_FLAG_YELLOW: return "YELLOW FLAG";
        case MTS_FLAG_VSC: return "VIRTUAL SAFETY CAR";
        case MTS_FLAG_SC: return "SAFETY CAR";
        case MTS_FLAG_SC_ENDING: return "SAFETY CAR IN";
        case MTS_FLAG_RED: return "RED FLAG";
        case MTS_FLAG_CHEQUERED: return "CHEQUERED FLAG";
        default: return "";
    }
}

uint16_t mts_Flag_Color(uint8_t f){
    switch (f){
        case MTS_FLAG_GREEN: return RGB(30, 200, 60);
        case MTS_FLAG_YELLOW: case MTS_FLAG_VSC: case MTS_FLAG_SC: case MTS_FLAG_SC_ENDING: return RGB(255, 200, 0);
        case MTS_FLAG_RED: return RGB(235, 30, 30);
        case MTS_FLAG_CHEQUERED: return RGB(240, 240, 240);
        default: return RGB(90, 90, 90);
    }
}

static void copy(char* d, size_t n, const char* s){ snprintf(d, n, "%s", s ? s : ""); }

// The schedule ----------------------------------------------------------------------------------------------------------
void mts_Filter_Sessions(JsonDocument& f){
    JsonObject o = f.add<JsonObject>();
    for (const char* k : {"session_key", "session_type", "session_name", "date_start", "date_end", "meeting_key", "circuit_key",
                          "circuit_short_name", "country_name", "location", "gmt_offset", "is_cancelled", "year", "meeting_name"}) o[k] = true;
}

int mts_Parse_Sessions(JsonVariantConst rows, Mts_Schedule_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    int added = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.count >= MTS_SESSIONS) break;
        Mts_Session_t s = {};
        s.key = r["session_key"] | 0u;
        s.meeting = r["meeting_key"] | 0u;
        s.start = (uint32_t)(mts_Parse_Time_Ms(r["date_start"] | "") / 1000u);
        s.end = (uint32_t)(mts_Parse_Time_Ms(r["date_end"] | "") / 1000u);
        if (!s.key || !s.start) continue;
        bool dup = false;
        for (int i = 0; i < out.count; i++) if (out.s[i].key == s.key) dup = true;
        if (dup) continue;
        if (s.end < s.start) s.end = s.start + 3600;
        s.gmtOffset = mts_Parse_Offset(r["gmt_offset"] | "");
        s.circuitKey = (uint16_t)(r["circuit_key"] | 0);
        s.type = mts_Type_Of(r["session_type"] | "", r["session_name"] | "");
        s.cancelled = (r["is_cancelled"] | false) ? 1 : 0;
        copy(s.name, sizeof(s.name), r["session_name"] | "");
        copy(s.circuit, sizeof(s.circuit), r["circuit_short_name"] | "");
        copy(s.location, sizeof(s.location), r["location"] | "");
        copy(s.country, sizeof(s.country), r["country_name"] | "");
        int pos = out.count;
        while (pos > 0 && out.s[pos - 1].start > s.start){ out.s[pos] = out.s[pos - 1]; pos--; }
        out.s[pos] = s;
        out.count++;
        added++;
    }
    return added;
}

void mts_Filter_Meetings(JsonDocument& f){
    JsonObject o = f.add<JsonObject>();
    o["meeting_key"] = true;
    o["meeting_name"] = true;
}

int mts_Parse_Meetings(JsonVariantConst rows, Mts_Schedule_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    int added = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.meetings >= MTS_MEETINGS) break;
        const uint32_t key = r["meeting_key"] | 0u;
        if (!key) continue;
        bool dup = false;
        for (int i = 0; i < out.meetings; i++) if (out.m[i].key == key) dup = true;
        if (dup) continue;
        out.m[out.meetings].key = key;
        copy(out.m[out.meetings].name, sizeof(out.m[0].name), r["meeting_name"] | "");
        out.meetings++;
        added++;
    }
    return added;
}

const char* mts_Meeting_Name(const Mts_Schedule_t& s, uint32_t meeting){
    for (int i = 0; i < s.meetings; i++) if (s.m[i].key == meeting) return s.m[i].name;
    for (int i = 0; i < s.count; i++) if (s.s[i].meeting == meeting) return s.s[i].location;
    return "";
}

int mts_Live_Session(const Mts_Schedule_t& s, uint32_t now){
    for (int i = 0; i < s.count; i++){
        const Mts_Session_t& x = s.s[i];
        if (x.cancelled) continue;
        if (now + MTS_LIVE_BEFORE >= x.start && now <= x.end + MTS_LIVE_AFTER) return i;
    }
    return -1;
}

int mts_Next_Session(const Mts_Schedule_t& s, uint32_t now){
    for (int i = 0; i < s.count; i++) if (!s.s[i].cancelled && s.s[i].start > now) return i;
    return -1;
}

int mts_Last_Session(const Mts_Schedule_t& s, uint32_t now, uint8_t type){
    int best = -1;
    for (int i = 0; i < s.count; i++){
        const Mts_Session_t& x = s.s[i];
        if (x.cancelled || x.end > now) continue;
        if (type != 0xFF && x.type != type) continue;
        best = i;
    }
    return best;
}

uint32_t mts_Weekend(const Mts_Schedule_t& s, uint32_t now){
    int i = mts_Live_Session(s, now);
    if (i < 0){
        // Between sessions of one weekend, that weekend; between weekends, the next one from three days before it.
        const int next = mts_Next_Session(s, now), last = mts_Last_Session(s, now, 0xFF);
        if (next >= 0 && last >= 0 && s.s[next].meeting == s.s[last].meeting) i = next;
        else if (next >= 0 && (last < 0 || s.s[next].start - now < 3 * 86400 || now - s.s[last].end > 2 * 86400)) i = next;
        else i = last;
    }
    return i >= 0 ? s.s[i].meeting : 0;
}

// Drivers, results, standings -----------------------------------------------------------------------------------------
void mts_Filter_Drivers(JsonDocument& f){
    JsonObject o = f.add<JsonObject>();
    for (const char* k : {"driver_number", "name_acronym", "last_name", "broadcast_name", "team_name", "team_colour"}) o[k] = true;
}

int mts_Parse_Drivers(JsonVariantConst rows, uint32_t session, Mts_Drivers_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    out.session = session;
    out.count = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.count >= MTS_DRIVERS) break;
        const int n = r["driver_number"] | 0;
        if (n <= 0 || n > 99 || mts_Driver(out, (uint8_t)n)) continue;
        Mts_Driver_t& d = out.d[out.count++];
        d.number = (uint8_t)n;
        copy(d.code, sizeof(d.code), r["name_acronym"] | "");
        const char* last = r["last_name"] | (const char*)nullptr;
        if (last && last[0]) copy(d.name, sizeof(d.name), last);
        else copy(d.name, sizeof(d.name), r["broadcast_name"] | d.code);
        copy(d.team, sizeof(d.team), r["team_name"] | "");
        d.color = mts_Team_Color(r["team_colour"] | "");
    }
    return out.count;
}

const Mts_Driver_t* mts_Driver(const Mts_Drivers_t& d, uint8_t number){
    for (int i = 0; i < d.count; i++) if (d.d[i].number == number) return &d.d[i];
    return nullptr;
}

uint16_t mts_Color_Of_Team(const Mts_Drivers_t& d, const char* team){
    for (int i = 0; i < d.count; i++) if (!strcmp(d.d[i].team, team)) return d.d[i].color;
    return RGB(110, 110, 110);
}

void mts_Filter_Result(JsonDocument& f){
    JsonObject o = f.add<JsonObject>();
    for (const char* k : {"position", "driver_number", "number_of_laps", "points", "dnf", "dns", "dsq", "duration", "gap_to_leader"}) o[k] = true;
}

/** "+1 LAP", "+2 LAPS": laps down; 0 if not one. */
static int8_t laps_Down(const char* text){
    int n = 0;
    if (text && sscanf(text, "+%d LAP", &n) == 1 && n > 0 && n < 100) return (int8_t)n;
    return 0;
}

int mts_Parse_Result(JsonVariantConst rows, uint32_t session, uint8_t type, Mts_Result_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    out.session = session;
    out.type = type;
    out.count = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.count >= MTS_DRIVERS) break;
        Mts_Result_Row_t x = {};
        x.number = (uint8_t)(r["driver_number"] | 0);
        if (!x.number) continue;
        x.position = (uint8_t)(r["position"] | 0);
        x.dnf = (r["dnf"] | false) ? 1 : 0;
        x.dns = (r["dns"] | false) ? 1 : 0;
        x.dsq = (r["dsq"] | false) ? 1 : 0;
        x.laps = (uint16_t)(r["number_of_laps"] | 0);
        x.points = r["points"] | 0.0f;
        x.gap = NAN;
        x.best = NAN;
        JsonVariantConst g = r["gap_to_leader"], d = r["duration"];
        if (g.is<JsonArrayConst>()){
            // Qualifying: Q1, Q2, Q3; the gap in the last part the driver reached, the best of the times.
            for (JsonVariantConst v : g.as<JsonArrayConst>()) if (v.is<float>()) x.gap = v.as<float>();
            for (JsonVariantConst v : d.as<JsonArrayConst>()) if (v.is<float>() && (isnan(x.best) || v.as<float>() < x.best)) x.best = v.as<float>();
        } else if (g.is<float>()) x.gap = g.as<float>();
        else if (g.is<const char*>()) x.lapsDown = laps_Down(g.as<const char*>());
        if (d.is<float>()) x.best = d.as<float>();
        out.r[out.count++] = x;
    }
    // Classified first, in order; the rest after.
    for (int i = 1; i < out.count; i++){
        Mts_Result_Row_t k = out.r[i];
        int j = i;
        const int pk = k.position ? k.position : 99;
        while (j > 0 && (out.r[j - 1].position ? out.r[j - 1].position : 99) > pk){ out.r[j] = out.r[j - 1]; j--; }
        out.r[j] = k;
    }
    return out.count;
}

int mts_Parse_Standings_Drivers(JsonVariantConst rows, Mts_Standings_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    out.drivers = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.drivers >= MTS_DRIVERS) break;
        const int n = r["driver_number"] | 0;
        if (n <= 0) continue;
        out.session = r["session_key"] | out.session;
        auto& d = out.d[out.drivers++];
        d.number = (uint8_t)n;
        d.position = (uint8_t)(r["position_current"] | 0);
        d.before = (uint8_t)(r["position_start"] | 0);
        d.points = r["points_current"] | 0.0f;
        d.gained = d.points - (r["points_start"] | d.points);
    }
    for (int i = 1; i < out.drivers; i++){
        auto k = out.d[i];
        int j = i;
        while (j > 0 && out.d[j - 1].position > k.position){ out.d[j] = out.d[j - 1]; j--; }
        out.d[j] = k;
    }
    return out.drivers;
}

int mts_Parse_Standings_Teams(JsonVariantConst rows, Mts_Standings_t& out){
    if (!rows.is<JsonArrayConst>()) return -1;
    out.teams = 0;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        if (out.teams >= MTS_TEAMS) break;
        const char* team = r["team_name"] | "";
        if (!team[0]) continue;
        auto& t = out.t[out.teams++];
        copy(t.team, sizeof(t.team), team);
        t.position = (uint8_t)(r["position_current"] | 0);
        t.before = (uint8_t)(r["position_start"] | 0);
        t.points = r["points_current"] | 0.0f;
        t.gained = t.points - (r["points_start"] | t.points);
    }
    for (int i = 1; i < out.teams; i++){
        auto k = out.t[i];
        int j = i;
        while (j > 0 && out.t[j - 1].position > k.position){ out.t[j] = out.t[j - 1]; j--; }
        out.t[j] = k;
    }
    return out.teams;
}

// Live timing -----------------------------------------------------------------------------------------------------------
void mts_Live_Reset(Mts_Live_t& l, uint32_t session, uint8_t type, const Mts_Drivers_t& drivers){
    memset(&l, 0, sizeof(l));
    l.session = session;
    l.type = type;
    l.fastestTime = NAN;
    l.air = l.track = NAN;
    for (int i = 0; i < drivers.count && l.count < MTS_DRIVERS; i++){
        Mts_Car_t& c = l.car[l.count++];
        c.number = drivers.d[i].number;
        c.gap = c.interval = c.best = c.last = NAN;
    }
}

static Mts_Car_t* car(Mts_Live_t& l, int number){
    if (number <= 0 || number > 99) return nullptr;
    for (int i = 0; i < l.count; i++) if (l.car[i].number == number) return &l.car[i];
    if (l.count >= MTS_DRIVERS) return nullptr;
    Mts_Car_t& c = l.car[l.count++];
    memset(&c, 0, sizeof(c));
    c.number = (uint8_t)number;
    c.gap = c.interval = c.best = c.last = NAN;
    return &c;
}

static void filter_Of(JsonDocument& f, std::initializer_list<const char*> keys){
    JsonObject o = f.add<JsonObject>();
    for (const char* k : keys) o[k] = true;
}
void mts_Filter_Position(JsonDocument& f){ filter_Of(f, {"date", "driver_number", "position"}); }
void mts_Filter_Interval(JsonDocument& f){ filter_Of(f, {"date", "driver_number", "gap_to_leader", "interval"}); }
void mts_Filter_Laps(JsonDocument& f){ filter_Of(f, {"date_start", "driver_number", "lap_number", "lap_duration", "is_pit_out_lap"}); }
void mts_Filter_Stints(JsonDocument& f){ filter_Of(f, {"driver_number", "stint_number", "compound", "lap_start", "lap_end", "tyre_age_at_start"}); }
void mts_Filter_Pit(JsonDocument& f){ filter_Of(f, {"date", "driver_number", "lap_number"}); }
void mts_Filter_Control(JsonDocument& f){ filter_Of(f, {"date", "category", "flag", "scope", "message", "lap_number"}); }
void mts_Filter_Weather(JsonDocument& f){ filter_Of(f, {"date", "air_temperature", "track_temperature", "rainfall"}); }

int mts_Live_Positions(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint64_t mark = l.markPosition;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date"] | "");
        if (!t || t <= l.markPosition) continue;
        Mts_Car_t* c = car(l, r["driver_number"] | 0);
        const int p = r["position"] | 0;
        if (!c || p <= 0 || p > 30) continue;
        if (t >= c->positionAt){ c->position = (uint8_t)p; c->positionAt = (uint32_t)(t / 1000u); }
        if (t > mark) mark = t;
        used++;
    }
    l.markPosition = mark;
    return used;
}

int mts_Live_Intervals(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint64_t mark = l.markInterval;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date"] | "");
        if (!t || t <= l.markInterval) continue;
        Mts_Car_t* c = car(l, r["driver_number"] | 0);
        if (!c) continue;
        JsonVariantConst g = r["gap_to_leader"], i = r["interval"];
        if (g.is<float>()){ c->gap = g.as<float>(); c->lapsDown = 0; }
        else if (g.is<const char*>()){ c->lapsDown = laps_Down(g.as<const char*>()); c->gap = NAN; }
        if (i.is<float>()) c->interval = i.as<float>();
        else if (i.is<const char*>()) c->interval = NAN;
        if (t > mark) mark = t;
        used++;
    }
    l.markInterval = mark;
    return used;
}

int mts_Live_Laps(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint64_t mark = l.markLap;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date_start"] | "");
        Mts_Car_t* c = car(l, r["driver_number"] | 0);
        const int n = r["lap_number"] | 0;
        if (!c || n <= 0) continue;
        // A lap's row comes first without a time and again when it is done: take both.
        if (n >= c->lap) c->lap = (uint16_t)n;
        if (n > l.lap) l.lap = (uint16_t)n;
        JsonVariantConst d = r["lap_duration"];
        if (d.is<float>()){
            const float v = d.as<float>();
            if (v > 20 && v < 600){
                if (n >= c->lap - 1) c->last = v;
                if (isnan(c->best) || v < c->best) c->best = v;
                if (isnan(l.fastestTime) || v < l.fastestTime){ l.fastestTime = v; l.fastest = c->number; }
            }
        }
        if (t > mark) mark = t;
        used++;
    }
    l.markLap = mark;
    return used;
}

int mts_Live_Stints(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint8_t latest[100] = {};
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const int n = r["driver_number"] | 0;
        Mts_Car_t* c = car(l, n);
        const int stint = r["stint_number"] | 0;
        if (!c || stint <= 0 || stint < latest[c->number]) continue;
        latest[c->number] = (uint8_t)stint;
        c->compound = mts_Compound_Of(r["compound"] | "");
        const int start = r["lap_start"] | 1, age = r["tyre_age_at_start"] | 0;
        const int on = (int)c->lap - start;
        c->tyreLaps = (uint8_t)((age + (on > 0 ? on : 0)) > 255 ? 255 : age + (on > 0 ? on : 0));
        used++;
    }
    return used;
}

int mts_Live_Pits(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint64_t mark = l.markPit;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date"] | "");
        if (!t || t <= l.markPit) continue;
        Mts_Car_t* c = car(l, r["driver_number"] | 0);
        if (!c) continue;
        if (c->pits < 99) c->pits++;
        if (t > mark) mark = t;
        used++;
    }
    l.markPit = mark;
    return used;
}

bool mts_Live_Control(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return false;
    const uint8_t before = l.flag;
    uint64_t mark = l.markControl;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date"] | "");
        if (!t || t <= l.markControl) continue;
        if (t > mark) mark = t;
        const char* cat = r["category"] | "";
        const char* flag = r["flag"] | "";
        const char* scope = r["scope"] | "";
        const char* msg = r["message"] | "";
        if (!strcmp(cat, "SafetyCar")){
            if (has(msg, "VIRTUAL SAFETY CAR DEPLOYED")) l.flag = MTS_FLAG_VSC;
            else if (has(msg, "SAFETY CAR DEPLOYED")) l.flag = MTS_FLAG_SC;
            else if (has(msg, "ENDING") || has(msg, "IN THIS LAP")) l.flag = MTS_FLAG_SC_ENDING;
        } else if (!strcmp(cat, "Flag")){
            if (!strcmp(flag, "RED")) l.flag = MTS_FLAG_RED;
            else if (!strcmp(flag, "CHEQUERED")){ l.flag = MTS_FLAG_CHEQUERED; l.finished = 1; }
            else if ((!strcmp(flag, "GREEN") || !strcmp(flag, "CLEAR")) && !strcmp(scope, "Track")) l.flag = MTS_FLAG_GREEN;
            else if ((!strcmp(flag, "YELLOW") || !strcmp(flag, "DOUBLE YELLOW")) && (l.flag == MTS_FLAG_GREEN || l.flag == MTS_FLAG_NONE)) l.flag = MTS_FLAG_YELLOW;
            else if (!strcmp(flag, "CLEAR") && l.flag == MTS_FLAG_YELLOW) l.flag = MTS_FLAG_GREEN;
        } else if (!strcmp(cat, "SessionStatus")){
            if (has(msg, "STARTED") && (l.flag == MTS_FLAG_NONE || l.flag == MTS_FLAG_RED)) l.flag = MTS_FLAG_GREEN;
            if (has(msg, "FINISHED") || has(msg, "ENDED")) l.finished = 1;
        }
        // The newest message worth reading (not the stream of blue flags).
        if (msg[0] && strcmp(flag, "BLUE") && t >= (uint64_t)l.messageAt * 1000u){
            copy(l.message, sizeof(l.message), msg);
            l.messageAt = (uint32_t)(t / 1000u);
        }
    }
    l.markControl = mark;
    return l.flag != before;
}

int mts_Live_Weather(JsonVariantConst rows, Mts_Live_t& l){
    if (!rows.is<JsonArrayConst>()) return 0;
    int used = 0;
    uint64_t mark = l.markWeather;
    for (JsonObjectConst r : rows.as<JsonArrayConst>()){
        const uint64_t t = mts_Parse_Time_Ms(r["date"] | "");
        if (!t || t <= l.markWeather) continue;
        if (t >= mark){
            mark = t;
            l.air = r["air_temperature"] | NAN;
            l.track = r["track_temperature"] | NAN;
            l.rain = (r["rainfall"] | 0) ? 1 : 0;
        }
        used++;
    }
    l.markWeather = mark;
    return used;
}

int mts_Live_Order(const Mts_Live_t& l, uint8_t* order, int max){
    int n = 0;
    for (int i = 0; i < l.count && n < max; i++) order[n++] = (uint8_t)i;
    const bool byLap = l.type == MTS_PRACTICE || l.type == MTS_TESTING;
    auto key = [&](int i) -> float {
        const Mts_Car_t& c = l.car[i];
        if (c.position) return (float)c.position;
        if (byLap && !isnan(c.best)) return 100.0f + c.best / 1000.0f;
        return 1000.0f + c.number;
    };
    for (int i = 1; i < n; i++){
        const uint8_t k = order[i];
        int j = i;
        while (j > 0 && key(order[j - 1]) > key(k)){ order[j] = order[j - 1]; j--; }
        order[j] = k;
    }
    return n;
}

// The circuit -----------------------------------------------------------------------------------------------------------
int mts_Parse_Track(JsonVariantConst rows, Mts_Track_t& out){
    out.n = 0;
    if (!rows.is<JsonArrayConst>()) return -1;
    JsonArrayConst a = rows.as<JsonArrayConst>();
    const int total = (int)a.size();
    if (total < 20) return 0;
    long minX = 1L << 30, minY = 1L << 30, maxX = -(1L << 30), maxY = -(1L << 30);
    for (JsonObjectConst r : a){
        const long x = r["x"] | 0L, y = r["y"] | 0L;
        if (x == 0 && y == 0) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    const long span = (maxX - minX) > (maxY - minY) ? (maxX - minX) : (maxY - minY);
    if (span <= 0) return 0;
    const int step = (total + MTS_TRACK_POINTS - 1) / MTS_TRACK_POINTS;
    int k = 0;
    for (JsonObjectConst r : a){
        if (k++ % step) continue;
        if (out.n >= MTS_TRACK_POINTS) break;
        const long x = r["x"] | 0L, y = r["y"] | 0L;
        if (x == 0 && y == 0) continue;
        // North up: the feed's y grows upwards; the panel's grows down.
        out.x[out.n] = (int16_t)((x - minX) * 1000 / span);
        out.y[out.n] = (int16_t)((maxY - y) * 1000 / span);
        out.n++;
    }
    return out.n;
}

bool mts_No_Rows(int httpCode, JsonVariantConst doc){
    if (httpCode != 404) return false;
    const char* d = doc["detail"] | "";
    return strstr(d, "No results") != nullptr;
}
