#include "wck_Model.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const uint16_t CITY_COLORS[WCK_CITIES] = {
    MTB_KIT_RGB(80, 220, 120), MTB_KIT_RGB(255, 255, 255), MTB_KIT_RGB(255, 210, 60), MTB_KIT_RGB(80, 200, 255),
    MTB_KIT_RGB(255, 120, 200), MTB_KIT_RGB(255, 150, 60),
};

void wck_Settings_Default(Wck_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = WCK_SETTINGS_MAGIC;
    static const char* const DEFAULTS[][2] = {{"Lagos", "Nigeria"}, {"London", "United Kingdom"}, {"New York", "United States of America"},
                                              {"Dubai", "United Arab Emirates"}, {"Tokyo", "Japan"}, {"Sydney", "Australia"}};
    for (const auto& d : DEFAULTS){
        Wck_City_t city;
        if (s.count < WCK_CITIES && wck_Find(d[0], d[1], city)){ city.color = CITY_COLORS[s.count]; s.cities[s.count++] = city; }
    }
    s.view = WCK_VIEW_BOARD;
    s.hour24 = 0;
    s.cycle = 1;
    s.seconds = 15;
    s.officeFrom = 9 * 60;
    s.officeTo = 17 * 60;
}

struct Wck_Line_t { const char* field[4]; size_t length[4]; };
static bool wck_Next_Line(const char*& p, Wck_Line_t& line);
static void wck_Fill(Wck_City_t& c, const Wck_Line_t& line);

static bool wck_Control(char ch){ return (unsigned char)ch < 0x20 || ch == 0x7F; }

static bool wck_Has_Control(const char* t){
    for (; *t; t++) if (wck_Control(*t)) return true;
    return false;
}

// Removes control characters (a damaged save, which would also make the phone's JSON invalid).
static void wck_Strip(char* t){
    char* w = t;
    for (const char* r = t; *r; r++) if (!wck_Control(*r)) *w++ = *r;
    *w = '\0';
}

// A damaged name ("P\x01gos"): the list's city of the same country, time zone and length whose letters match where
// the name is not damaged (control characters, and at most two others, may differ). Only a single best match is used.
static bool wck_Repair_Name(Wck_City_t& c){
    const size_t n = strlen(c.name);
    const char* p = WCK_CITY_LIST;
    Wck_Line_t line;
    int best = 99, bestCount = 0;
    Wck_City_t found;
    while (wck_Next_Line(p, line)){
        if (line.length[0] != n) continue;
        if (line.length[1] != strlen(c.country) || strncasecmp(line.field[1], c.country, line.length[1]) != 0) continue;
        if (line.length[3] != strlen(c.tz) || strncmp(line.field[3], c.tz, line.length[3]) != 0) continue;
        int other = 0;
        for (size_t i = 0; i < n; i++){
            if (wck_Control(c.name[i])) continue;
            if (tolower((unsigned char)c.name[i]) != tolower((unsigned char)line.field[0][i])) other++;
        }
        if (other > 2) continue;
        if (other < best){ best = other; bestCount = 1; wck_Fill(found, line); }
        else if (other == best) bestCount++;
    }
    if (bestCount != 1) return false;
    memcpy(c.name, found.name, sizeof(c.name));
    return true;
}

static void wck_Hex_Bytes(char* out, size_t size, const char* t){
    size_t k = 0;
    out[0] = '\0';
    for (; *t && k + 4 < size; t++) k += (size_t)snprintf(out + k, size - k, "%s%02x", k ? " " : "", (unsigned char)*t);
}

int wck_Settings_Clamp(Wck_Settings_t& s, char* report, size_t reportSize){
    int repaired = 0;
    size_t rk = 0;
    if (report && reportSize) report[0] = '\0';
    if (s.count > WCK_CITIES) s.count = WCK_CITIES;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.count; i++){
        Wck_City_t c = s.cities[i];
        c.name[sizeof(c.name) - 1] = c.country[sizeof(c.country) - 1] = c.code[2] = c.tz[WCK_TZ_LEN - 1] = '\0';
        if (wck_Has_Control(c.name) || wck_Has_Control(c.country) || wck_Has_Control(c.code) || wck_Has_Control(c.tz)){
            char before[80];
            wck_Hex_Bytes(before, sizeof(before), c.name);
            wck_Strip(c.country);
            wck_Strip(c.code);
            wck_Strip(c.tz);
            const bool fixed = wck_Has_Control(c.name) && wck_Repair_Name(c);
            if (!fixed) wck_Strip(c.name);
            repaired++;
            if (report && rk + 1 < reportSize){
                const int w = snprintf(report + rk, reportSize - rk, "%scity %u name [%s] -> \"%s\"%s", rk ? "; " : "", i + 1, before, c.name,
                                       fixed ? " (from the city list)" : "");
                if (w > 0) rk += (size_t)w < reportSize - rk ? (size_t)w : reportSize - rk - 1;
            }
        }
        Wck_Tz_t tz;
        if (c.name[0] && wck_Tz_Parse(c.tz, tz)){ if (c.color == 0) c.color = CITY_COLORS[kept]; s.cities[kept++] = c; }
    }
    for (uint8_t i = kept; i < WCK_CITIES; i++) memset(&s.cities[i], 0, sizeof(s.cities[i]));
    s.count = kept;
    if (s.count == 0){ Wck_Settings_t d; wck_Settings_Default(d); memcpy(s.cities, d.cities, sizeof(s.cities)); s.count = d.count; }
    if (s.view >= WCK_VIEW_COUNT) s.view = WCK_VIEW_BOARD;
    s.hour24 = s.hour24 ? 1 : 0;
    s.cycle = s.cycle ? 1 : 0;
    if (s.seconds < 5) s.seconds = 5;
    if (s.seconds > 120) s.seconds = 120;
    if (s.officeFrom >= 1440) s.officeFrom = 9 * 60;
    if (s.officeTo >= 1440 || s.officeTo == s.officeFrom) s.officeTo = 17 * 60;
    return repaired;
}

//##############################################################################################################
// "EST", "<+0330>": the zone's name.
static const char* wck_Name(const char* p, char* dest, size_t size){
    size_t n = 0;
    if (*p == '<'){
        p++;
        while (*p && *p != '>'){ if (n + 1 < size) dest[n++] = *p; p++; }
        if (*p == '>') p++;
    } else while (isalpha((unsigned char)*p)){ if (n + 1 < size) dest[n++] = *p; p++; }
    dest[n] = '\0';
    return n ? p : nullptr;
}

// "[+-]hh[:mm[:ss]]" to seconds; nullptr if none.
static const char* wck_Time(const char* p, int32_t& seconds){
    int sign = 1;
    if (*p == '+' || *p == '-'){ if (*p == '-') sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return nullptr;
    int32_t h = 0, m = 0, s = 0;
    while (isdigit((unsigned char)*p)) h = h * 10 + (*p++ - '0');
    if (*p == ':'){ p++; while (isdigit((unsigned char)*p)) m = m * 10 + (*p++ - '0'); }
    if (*p == ':'){ p++; while (isdigit((unsigned char)*p)) s = s * 10 + (*p++ - '0'); }
    seconds = sign * (h * 3600 + m * 60 + s);
    return p;
}

static const char* wck_Rule(const char* p, Wck_Tz_t::Rule_t& rule){
    if (*p != 'M') return nullptr;       // The list only uses Mm.w.d rules.
    int m = 0, w = 0, d = 0;
    if (sscanf(p + 1, "%d.%d.%d", &m, &w, &d) != 3 || m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6) return nullptr;
    p++;
    while (*p && *p != '/' && *p != ',') p++;
    rule.month = (uint8_t)m; rule.week = (uint8_t)w; rule.weekday = (uint8_t)d;
    rule.time = 2 * 3600;
    if (*p == '/'){ p = wck_Time(p + 1, rule.time); if (p == nullptr) return nullptr; }
    return p;
}

bool wck_Tz_Parse(const char* text, Wck_Tz_t& tz){
    memset(&tz, 0, sizeof(tz));
    if (text == nullptr) return false;
    const char* p = wck_Name(text, tz.stdName, sizeof(tz.stdName));
    int32_t west;
    if (p == nullptr || (p = wck_Time(p, west)) == nullptr) return false;
    tz.stdOffset = -west;
    if (*p == '\0') return true;
    p = wck_Name(p, tz.dstName, sizeof(tz.dstName));
    if (p == nullptr) return false;
    tz.dstOffset = tz.stdOffset + 3600;
    if (*p && *p != ','){ if ((p = wck_Time(p, west)) == nullptr) return false; tz.dstOffset = -west; }
    if (*p != ',') return false;
    if ((p = wck_Rule(p + 1, tz.start)) == nullptr || *p != ',') return false;
    if ((p = wck_Rule(p + 1, tz.end)) == nullptr) return false;
    tz.hasDst = true;
    return *p == '\0';
}

// Unix time of the rule's moment in @p year, the rule's time being local time at @p offset.
static int64_t wck_Rule_Time(const Wck_Tz_t::Rule_t& r, int year, int32_t offset){
    const int days = mtb_Kit_Days_In_Month(year, r.month);
    const int firstWeekday = (mtb_Kit_Weekday(year, r.month, 1) + 1) % 7;    // 0 Sunday
    int day = 1 + (r.weekday - firstWeekday + 7) % 7 + (r.week - 1) * 7;
    if (r.week == 5) while (day > days) day -= 7;                             // "5": the last.
    return (int64_t)mtb_Kit_Days_From_Civil(year, r.month, day) * 86400 + r.time - offset;
}

int32_t wck_Tz_Offset(const Wck_Tz_t& tz, int64_t utc, bool* dst){
    if (dst) *dst = false;
    if (!tz.hasDst) return tz.stdOffset;
    Wck_Local_t t;
    wck_Local(utc, tz.stdOffset, t);
    // Summer time starts at a standard-time moment and ends at a summer-time one.
    const int64_t start = wck_Rule_Time(tz.start, t.year, tz.stdOffset);
    const int64_t end = wck_Rule_Time(tz.end, t.year, tz.dstOffset);
    const bool on = start < end ? (utc >= start && utc < end) : (utc >= start || utc < end);     // South: across New Year.
    if (dst) *dst = on;
    return on ? tz.dstOffset : tz.stdOffset;
}

int32_t wck_Offset_Of(const char* text, int64_t utc, bool* dst){
    Wck_Tz_t tz;
    if (!wck_Tz_Parse(text, tz)){ if (dst) *dst = false; return 0; }
    return wck_Tz_Offset(tz, utc, dst);
}

void wck_Local(int64_t utc, int32_t offset, Wck_Local_t& t){
    const int64_t local = utc + offset;
    int64_t days = local / 86400;
    int64_t rest = local % 86400;
    if (rest < 0){ rest += 86400; days--; }
    t.hour = (int)(rest / 3600); t.minute = (int)(rest / 60 % 60); t.second = (int)(rest % 60);
    t.weekday = (int)((days % 7 + 11) % 7);         // 1970-01-01 was a Thursday.
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    t.day = (int)(doy - (153 * mp + 2) / 5 + 1);
    t.month = (int)(mp < 10 ? mp + 3 : mp - 9);
    t.year = (int)(yoe + era * 400 + (t.month <= 2));
}

void wck_Clock_Text(const Wck_Local_t& t, bool hour24, char* time, size_t timeSize, char* meridiem, size_t meridiemSize){
    if (hour24){ snprintf(time, timeSize, "%02d:%02d", t.hour, t.minute); snprintf(meridiem, meridiemSize, "%s", ""); return; }
    const int h = t.hour % 12 == 0 ? 12 : t.hour % 12;
    snprintf(time, timeSize, "%d:%02d", h, t.minute);
    snprintf(meridiem, meridiemSize, "%s", t.hour < 12 ? "AM" : "PM");
}

void wck_Difference_Text(int32_t seconds, char* dest, size_t size){
    if (seconds == 0){ snprintf(dest, size, "SAME"); return; }
    const char sign = seconds > 0 ? '+' : '-';
    const int32_t a = seconds > 0 ? seconds : -seconds;
    if (a % 3600) snprintf(dest, size, "%c%d:%02d", sign, (int)(a / 3600), (int)(a % 3600 / 60));
    else snprintf(dest, size, "%c%dH", sign, (int)(a / 3600));
}

int wck_Day_Difference(int64_t utc, int32_t cityOffset, int32_t localOffset){
    Wck_Local_t a, b;
    wck_Local(utc, cityOffset, a);
    wck_Local(utc, localOffset, b);
    const int32_t da = mtb_Kit_Days_From_Civil(a.year, a.month, a.day), db = mtb_Kit_Days_From_Civil(b.year, b.month, b.day);
    return da > db ? 1 : da < db ? -1 : 0;
}

uint8_t wck_Period(int hour, int minute, uint16_t from, uint16_t to){
    const int m = hour * 60 + minute;
    const bool office = from < to ? (m >= from && m < to) : (m >= from || m < to);
    if (office) return WCK_OFFICE;
    if (m < 6 * 60 || m >= 22 * 60) return WCK_NIGHT;
    return m < 12 * 60 ? WCK_MORNING : WCK_EVENING;
}

const char* wck_Period_Name(uint8_t p){
    static const char* const NAMES[] = {"NIGHT", "MORNING", "WORKING HOURS", "EVENING"};
    return p < 4 ? NAMES[p] : "";
}

//##############################################################################################################

static bool wck_Next_Line(const char*& p, Wck_Line_t& line){
    if (*p == '\0') return false;
    const char* end = strchr(p, '\n');
    if (end == nullptr) return false;
    const char* start = p;
    for (int f = 0; f < 4; f++){
        const char* stop = f < 3 ? (const char*)memchr(start, '\t', (size_t)(end - start)) : end;
        if (stop == nullptr) return false;
        line.field[f] = start;
        line.length[f] = (size_t)(stop - start);
        start = stop + 1;
    }
    p = end + 1;
    return true;
}

static void wck_Copy(char* dest, size_t size, const char* text, size_t length){
    if (length >= size) length = size - 1;
    memcpy(dest, text, length);
    dest[length] = '\0';
}

static void wck_Fill(Wck_City_t& c, const Wck_Line_t& line){
    memset(&c, 0, sizeof(c));
    wck_Copy(c.name, sizeof(c.name), line.field[0], line.length[0]);
    wck_Copy(c.country, sizeof(c.country), line.field[1], line.length[1]);
    wck_Copy(c.code, sizeof(c.code), line.field[2], line.length[2]);
    wck_Copy(c.tz, sizeof(c.tz), line.field[3], line.length[3]);
}

static bool wck_Has_Word(const char* field, size_t length, const char* word, size_t n){
    for (size_t i = 0; i + n <= length; i++){
        const bool start = i == 0 || field[i - 1] == ' ' || field[i - 1] == '-';
        if (start && strncasecmp(field + i, word, n) == 0) return true;
    }
    return false;
}

int wck_Search(const char* query, Wck_City_t* results, int max){
    if (query == nullptr || max <= 0) return 0;
    int count = 0;
    // Two passes: cities named so first ("san" finds San Juan before cities in San Marino).
    for (int pass = 0; pass < 2 && count < max; pass++){
        const char* p = WCK_CITY_LIST;
        Wck_Line_t line;
        while (count < max && wck_Next_Line(p, line)){
            bool all = true, any = false, inName = true;
            const char* w = query;
            while (*w && all){
                while (*w == ' ' || *w == ',') w++;
                const size_t n = strcspn(w, " ,");
                if (n == 0) break;
                any = true;
                const bool name = wck_Has_Word(line.field[0], line.length[0], w, n);
                if (!name) inName = false;
                all = name || wck_Has_Word(line.field[1], line.length[1], w, n);
                w += n;
            }
            if (!any || !all || (pass == 0) != inName) continue;
            wck_Fill(results[count++], line);
        }
    }
    return count;
}

bool wck_Find(const char* name, const char* country, Wck_City_t& city){
    const char* p = WCK_CITY_LIST;
    Wck_Line_t line;
    const size_t n = strlen(name);
    while (wck_Next_Line(p, line)){
        if (line.length[0] != n || strncasecmp(line.field[0], name, n) != 0) continue;
        if (country && country[0] && (line.length[1] != strlen(country) || strncasecmp(line.field[1], country, line.length[1]) != 0)) continue;
        wck_Fill(city, line);
        return true;
    }
    return false;
}

// "City|Country|cc|TZ"
bool wck_Parse_Value(const char* value, Wck_City_t& c){
    memset(&c, 0, sizeof(c));
    if (value == nullptr) return false;
    const char* parts[4];
    size_t lengths[4];
    const char* p = value;
    for (int i = 0; i < 4; i++){
        const char* bar = i < 3 ? strchr(p, '|') : p + strlen(p);
        if (bar == nullptr) return false;
        parts[i] = p;
        lengths[i] = (size_t)(bar - p);
        p = bar + 1;
    }
    if (lengths[0] == 0 || lengths[0] >= sizeof(c.name) || lengths[1] >= sizeof(c.country) || lengths[2] >= sizeof(c.code) || lengths[3] >= WCK_TZ_LEN) return false;
    wck_Copy(c.name, sizeof(c.name), parts[0], lengths[0]);
    wck_Copy(c.country, sizeof(c.country), parts[1], lengths[1]);
    wck_Copy(c.code, sizeof(c.code), parts[2], lengths[2]);
    wck_Copy(c.tz, sizeof(c.tz), parts[3], lengths[3]);
    for (char* ch = c.code; *ch; ch++) if (!islower((unsigned char)*ch)) return false;
    Wck_Tz_t tz;
    return wck_Tz_Parse(c.tz, tz);
}

void wck_Value_Of(const Wck_City_t& c, char* dest, size_t size){ snprintf(dest, size, "%s|%s|%s|%s", c.name, c.country, c.code, c.tz); }
