/**
 * Room Sign's iCalendar reader (rms_Ics.h). Two passes over the text: the first finds the changed occurrences
 * (RECURRENCE-ID), the second reads each event and expands its repeats inside the window, leaving out the changed and
 * the excluded (EXDATE) occurrences. Nothing is allocated; the work is bounded (a repeat is expanded at most
 * 20000 steps).
 *
 * Repeats are expanded in the event's own frame: UTC for a "...Z" start, else the room's local time, so a weekly
 * meeting stays at 09:00 local. Pixlpal's present UTC offset is used for every date, so an occurrence on the far side
 * of a daylight-saving change shows an hour off until the change.
 */
#include "rms_Ics.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

extern "C" void pxl_compat_breathe(void);      // the compat layer: lets a long parse wait its turn

#define LINE_MAX_LEN    600
#define OVERRIDES       128
#define EXDATES         24
#define EXPAND_STEPS    20000

// Dates ---------------------------------------------------------------------------------------------------------------------
static int64_t days_From_Civil(int64_t y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_From_Days(int64_t z, int& y, int& m, int& d){
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}
static int days_In_Month(int y, int m){
    static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    return D[m - 1];
}
/** 0 Monday ... 6 Sunday. */
static int monday_Index(int64_t days){ int w = (int)((days + 3) % 7); return w < 0 ? w + 7 : w; }
static int64_t floor_Div(int64_t a, int64_t b){ int64_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

// Lines ---------------------------------------------------------------------------------------------------------------------
struct Reader_t {
    const char* p;
    const char* end;
    unsigned lines;
};

/** The next logical line (folded lines joined), without its end. False at the end of the text. */
static bool next_Line(Reader_t& r, char* out, size_t size){
    if (r.p >= r.end) return false;
    size_t n = 0;
    for (;;){
        while (r.p < r.end && *r.p != '\n' && *r.p != '\r'){ if (n + 1 < size) out[n++] = *r.p; r.p++; }
        if (r.p < r.end && *r.p == '\r') r.p++;
        if (r.p < r.end && *r.p == '\n') r.p++;
        if (r.p < r.end && (*r.p == ' ' || *r.p == '\t')){ r.p++; continue; }      // a folded line goes on
        break;
    }
    out[n] = '\0';
    if ((++r.lines & 63) == 0) pxl_compat_breathe();
    return true;
}

/** Splits "NAME;PARAMS:VALUE": @p name upper-cased, @p params ("" if none) and @p value point into @p line. */
static bool split(char* line, char*& name, char*& params, char*& value){
    name = line;
    char* p = line;
    while (*p && *p != ';' && *p != ':'){ *p = (char)toupper((unsigned char)*p); p++; }
    if (!*p) return false;
    params = (char*)"";
    if (*p == ';'){
        *p++ = '\0';
        params = p;
        bool quoted = false;
        while (*p && (quoted || *p != ':')){ if (*p == '"') quoted = !quoted; p++; }
        if (!*p) return false;
    }
    *p++ = '\0';
    value = p;
    return true;
}

static bool has_Param(const char* params, const char* what){
    for (const char* p = params; *p; p++) if ((p == params || p[-1] == ';') && !strncasecmp(p, what, strlen(what))) return true;
    return false;
}

// Times ---------------------------------------------------------------------------------------------------------------------
struct Ics_Time_t {
    int64_t utc;
    bool date, isUtc, ok;
};

static Ics_Time_t parse_Time(const char* params, const char* v, int32_t localOffset){
    Ics_Time_t t = {0, false, false, false};
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (strlen(v) < 8 || sscanf(v, "%4d%2d%2d", &y, &mo, &d) != 3) return t;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return t;
    if (v[8] == 'T'){
        if (sscanf(v + 9, "%2d%2d%2d", &h, &mi, &s) < 2) return t;
        t.isUtc = strchr(v + 9, 'Z') != nullptr;
    } else t.date = true;
    if (has_Param(params, "VALUE=DATE") && !has_Param(params, "VALUE=DATE-TIME")) t.date = true;
    const int64_t local = days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    t.utc = t.isUtc ? local : local - localOffset;
    t.ok = true;
    return t;
}

/** "PT1H30M", "P1D", "-PT15M": seconds. */
static bool parse_Duration(const char* v, int64_t& out){
    int sign = 1;
    if (*v == '+') v++;
    else if (*v == '-'){ sign = -1; v++; }
    if (*v++ != 'P') return false;
    int64_t total = 0;
    bool time = false;
    while (*v){
        if (*v == 'T'){ time = true; v++; continue; }
        char* e;
        const long n = strtol(v, &e, 10);
        if (e == v) return false;
        switch (*e){
            case 'W': total += n * 604800; break;
            case 'D': total += n * 86400; break;
            case 'H': if (!time) return false; total += n * 3600; break;
            case 'M': if (!time) return false; total += n * 60; break;
            case 'S': if (!time) return false; total += n; break;
            default: return false;
        }
        v = e + 1;
    }
    out = sign * total;
    return true;
}

static uint32_t hash_Text(const char* s){ uint32_t h = 2166136261u; while (*s){ h ^= (uint8_t)*s++; h *= 16777619u; } return h ? h : 1; }

/** SUMMARY text: escapes undone, line breaks made spaces, cut at a character's start. */
static void unescape(const char* v, char* out, size_t size){
    size_t n = 0;
    for (; *v && n + 1 < size; v++){
        char c = *v;
        if (c == '\\' && v[1]){
            v++;
            c = (*v == 'n' || *v == 'N') ? ' ' : *v;
        }
        out[n++] = c;
    }
    while (n && ((uint8_t)out[n - 1] & 0xC0) == 0x80) n--;          // a cut multi-byte character
    if (n && (uint8_t)out[n - 1] >= 0xC0) n--;
    while (n && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

// One VEVENT ----------------------------------------------------------------------------------------------------------------
struct Vevent_t {
    Ics_Time_t start, end, rid;
    int64_t duration;
    bool hasDuration;
    char title[RMS_TITLE_LEN];
    char rrule[200];
    int64_t ex[EXDATES];
    int exN;
    uint32_t uid;
    bool cancelled, transparent;
};

struct Override_t { uint32_t uid; int64_t at; };

struct Out_t {
    Rms_Event_t* list;
    int n, max;
    int64_t from, to;
};

static void emit(Out_t& o, int64_t start, int64_t end, bool allDay, const char* title){
    if (end <= start) end = start + 1;
    if (start >= o.to || end <= o.from) return;
    if (o.n == o.max && start >= o.list[o.n - 1].start) return;
    int i = o.n < o.max ? o.n++ : o.n - 1;
    while (i > 0 && o.list[i - 1].start > start){ o.list[i] = o.list[i - 1]; i--; }
    o.list[i].start = start;
    o.list[i].end = end;
    o.list[i].allDay = allDay;
    snprintf(o.list[i].title, RMS_TITLE_LEN, "%s", title);
}

struct Rule_t {
    char freq;                          // 'D', 'W', 'M', 'Y'
    int interval, count;
    bool hasUntil;
    int64_t until;                      // in the event's frame
    int byDayN;
    int8_t byDay[7], byDayOrd[7];       // 0 Monday ... 6 Sunday; ordinal 0 = every
    int byMonthDay;                     // 0: none
};

static int day_Index(const char* s){
    static const char* const N[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
    for (int i = 0; i < 7; i++) if (!strncmp(s, N[i], 2)) return i;
    return -1;
}

static bool parse_Rule(const char* text, int64_t frameOffset, int32_t localOffset, Rule_t& r){
    memset(&r, 0, sizeof(r));
    r.interval = 1;
    char buf[200];
    snprintf(buf, sizeof(buf), "%s", text);
    for (char* part = strtok(buf, ";"); part; part = strtok(nullptr, ";")){
        char* eq = strchr(part, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* v = eq + 1;
        for (char* k = part; *k; k++) *k = (char)toupper((unsigned char)*k);
        if (!strcmp(part, "FREQ")){
            if (!strcmp(v, "DAILY")) r.freq = 'D';
            else if (!strcmp(v, "WEEKLY")) r.freq = 'W';
            else if (!strcmp(v, "MONTHLY")) r.freq = 'M';
            else if (!strcmp(v, "YEARLY")) r.freq = 'Y';
            else return false;                                          // HOURLY and finer: not for rooms
        } else if (!strcmp(part, "INTERVAL")){ r.interval = atoi(v); if (r.interval < 1) r.interval = 1; }
        else if (!strcmp(part, "COUNT")) r.count = atoi(v);
        else if (!strcmp(part, "UNTIL")){
            Ics_Time_t u = parse_Time("", v, localOffset);
            if (!u.ok) return false;
            r.hasUntil = true;
            // A date: up to the end of that local day. A time: as given, in the event's frame.
            r.until = u.date ? u.utc + localOffset + 86399 : u.utc + frameOffset;
        } else if (!strcmp(part, "BYDAY")){
            const char* p = v;
            while (*p && r.byDayN < 7){
                char* e;
                long ord = strtol(p, &e, 10);
                if (e == p) ord = 0;
                const int d = day_Index(e);
                if (d < 0) return false;
                r.byDay[r.byDayN] = (int8_t)d;
                r.byDayOrd[r.byDayN] = (int8_t)ord;
                r.byDayN++;
                p = e + 2;
                if (*p == ',') p++;
            }
        } else if (!strcmp(part, "BYMONTHDAY")) r.byMonthDay = atoi(v);
    }
    return r.freq != 0;
}

/** Calls @p each for every occurrence start (frame seconds) from the event's start, in order, until it says stop. */
template <typename F>
static void expand(const Rule_t& r, int64_t f0, int64_t fromF, int64_t toF, F each){
    const int64_t d0 = floor_Div(f0, 86400), tod = f0 - d0 * 86400;
    const int64_t fromDay = floor_Div(fromF, 86400);
    int produced = 0, steps = 0;
    auto take = [&](int64_t day) -> bool {                          // false: stop
        const int64_t f = day * 86400 + tod;
        if (f < f0) return true;
        if (r.hasUntil && f > r.until) return false;
        if (r.count && produced >= r.count) return false;
        if (f >= toF) return false;
        produced++;
        each(f);
        return true;
    };
    if (r.freq == 'D'){
        int64_t k = 0;
        if (!r.count && fromDay - 1 > d0) k = (fromDay - 1 - d0) / r.interval;
        for (; steps < EXPAND_STEPS; k++, steps++) if (!take(d0 + k * r.interval)) return;
    } else if (r.freq == 'W'){
        int8_t days[7];
        int n = 0;
        if (r.byDayN) for (int i = 0; i < 7; i++) for (int j = 0; j < r.byDayN; j++) if (r.byDay[j] == i){ days[n++] = (int8_t)i; break; }
        if (!n) days[n++] = (int8_t)monday_Index(d0);
        const int64_t week0 = d0 - monday_Index(d0);
        int64_t w = 0;
        if (!r.count && fromDay - week0 > 14) w = (fromDay - week0) / (7 * r.interval) - 1;
        for (; steps < EXPAND_STEPS; w++, steps++){
            const int64_t ws = week0 + w * 7 * r.interval;
            for (int i = 0; i < n; i++) if (!take(ws + days[i])) return;
        }
    } else if (r.freq == 'M' || r.freq == 'Y'){
        int y0, m0, dd0;
        civil_From_Days(d0, y0, m0, dd0);
        const int stepMonths = r.freq == 'M' ? r.interval : 12 * r.interval;
        int64_t k = 0;
        if (!r.count){
            int fy, fm, fd;
            civil_From_Days(fromDay, fy, fm, fd);
            const int64_t months = (int64_t)(fy - y0) * 12 + (fm - m0);
            if (months > 2) k = months / stepMonths - 1;
        }
        for (; steps < EXPAND_STEPS; k++, steps++){
            const int64_t ym = (m0 - 1) + k * stepMonths;
            const int y = (int)(y0 + ym / 12), m = (int)(ym % 12) + 1;
            const int dim = days_In_Month(y, m);
            int64_t cand[8];
            int cn = 0;
            if (r.freq == 'M' && r.byDayN){
                const int64_t first = days_From_Civil(y, m, 1);
                for (int j = 0; j < r.byDayN && cn < 8; j++){
                    const int wd = r.byDay[j];
                    const int firstOf = (int)((wd - monday_Index(first) + 7) % 7) + 1;      // day of the month
                    const int ord = r.byDayOrd[j];
                    if (ord > 0){ const int day = firstOf + (ord - 1) * 7; if (day <= dim) cand[cn++] = first + day - 1; }
                    else if (ord < 0){
                        int last = firstOf; while (last + 7 <= dim) last += 7;
                        const int day = last + (ord + 1) * 7;
                        if (day >= 1) cand[cn++] = first + day - 1;
                    } else for (int day = firstOf; day <= dim && cn < 8; day += 7) cand[cn++] = first + day - 1;
                }
                for (int a = 1; a < cn; a++) for (int b = a; b > 0 && cand[b - 1] > cand[b]; b--){ const int64_t t = cand[b]; cand[b] = cand[b - 1]; cand[b - 1] = t; }
            } else {
                int day = r.byMonthDay ? r.byMonthDay : dd0;
                if (day < 0) day = dim + day + 1;
                if (day >= 1 && day <= dim) cand[cn++] = days_From_Civil(y, m, day);
            }
            for (int i = 0; i < cn; i++) if (!take(cand[i])) return;
        }
    }
}

static void read_Event(const Vevent_t& e, const Override_t* ov, int ovN, int32_t localOffset, Out_t& o){
    if (!e.start.ok || e.cancelled || e.transparent) return;
    int64_t dur;
    if (e.end.ok) dur = e.end.utc - e.start.utc;
    else if (e.hasDuration) dur = e.duration;
    else dur = e.start.date ? 86400 : 0;
    if (dur < 0) dur = 0;
    if (e.rid.ok){ emit(o, e.start.utc, e.start.utc + dur, e.start.date, e.title); return; }       // a changed occurrence
    if (!e.rrule[0]){ emit(o, e.start.utc, e.start.utc + dur, e.start.date, e.title); return; }
    const int64_t frameOffset = e.start.isUtc ? 0 : localOffset;
    Rule_t r;
    if (!parse_Rule(e.rrule, frameOffset, localOffset, r)){ emit(o, e.start.utc, e.start.utc + dur, e.start.date, e.title); return; }
    expand(r, e.start.utc + frameOffset, o.from - dur + frameOffset, o.to + frameOffset, [&](int64_t f){
        const int64_t at = f - frameOffset;
        for (int i = 0; i < e.exN; i++) if (e.ex[i] == at) return;
        for (int i = 0; i < ovN; i++) if (ov[i].uid == e.uid && ov[i].at == at) return;
        emit(o, at, at + dur, e.start.date, e.title);
    });
}

int rms_Ics_Events(const char* text, size_t length, int64_t from, int64_t to, int32_t localOffset,
                   Rms_Event_t* out, int max, Rms_Ics_Report_t* report){
    if (report) memset(report, 0, sizeof(*report));
    if (!text || !out || max <= 0) return 0;
    static char line[LINE_MAX_LEN];
    static Override_t ov[OVERRIDES];
    static Vevent_t e;
    int ovN = 0;

    // Pass 1: the changed occurrences.
    Reader_t r = {text, text + length, 0};
    bool in = false;
    uint32_t uid = 0;
    Ics_Time_t rid = {0, false, false, false};
    while (next_Line(r, line, sizeof(line))){
        char *name, *params, *value;
        if (!split(line, name, params, value)) continue;
        if (!strcmp(name, "BEGIN") && !strcasecmp(value, "VEVENT")){ in = true; uid = 0; rid.ok = false; }
        else if (!in) continue;
        else if (!strcmp(name, "END") && !strcasecmp(value, "VEVENT")){
            in = false;
            if (rid.ok && uid && ovN < OVERRIDES) ov[ovN++] = {uid, rid.utc};
        }
        else if (!strcmp(name, "UID")) uid = hash_Text(value);
        else if (!strcmp(name, "RECURRENCE-ID")) rid = parse_Time(params, value, localOffset);
    }

    // Pass 2: the events.
    Out_t o = {out, 0, max, from, to};
    r = {text, text + length, 0};
    in = false;
    int nested = 0;                                                   // a VALARM inside the event
    while (next_Line(r, line, sizeof(line))){
        char *name, *params, *value;
        if (!split(line, name, params, value)) continue;
        if (!strcmp(name, "BEGIN")){
            if (!strcasecmp(value, "VCALENDAR") && report) report->calendar = true;
            if (!strcasecmp(value, "VEVENT")){ in = true; nested = 0; memset(&e, 0, sizeof(e)); if (report) report->vevents++; }
            else if (in) nested++;
            continue;
        }
        if (!in) continue;
        if (!strcmp(name, "END")){
            if (nested){ nested--; continue; }
            if (!strcasecmp(value, "VEVENT")){ in = false; read_Event(e, ov, ovN, localOffset, o); }
            continue;
        }
        if (nested) continue;
        if (!strcmp(name, "DTSTART")) e.start = parse_Time(params, value, localOffset);
        else if (!strcmp(name, "DTEND")) e.end = parse_Time(params, value, localOffset);
        else if (!strcmp(name, "DURATION")) e.hasDuration = parse_Duration(value, e.duration);
        else if (!strcmp(name, "SUMMARY")) unescape(value, e.title, sizeof(e.title));
        else if (!strcmp(name, "RRULE")) snprintf(e.rrule, sizeof(e.rrule), "%s", value);
        else if (!strcmp(name, "UID")) e.uid = hash_Text(value);
        else if (!strcmp(name, "RECURRENCE-ID")) e.rid = parse_Time(params, value, localOffset);
        else if (!strcmp(name, "STATUS")) e.cancelled = !strcasecmp(value, "CANCELLED");
        else if (!strcmp(name, "TRANSP")) e.transparent = !strcasecmp(value, "TRANSPARENT");
        else if (!strcmp(name, "EXDATE")){
            for (char* v = strtok(value, ","); v && e.exN < EXDATES; v = strtok(nullptr, ",")){
                const Ics_Time_t x = parse_Time(params, v, localOffset);
                if (x.ok) e.ex[e.exN++] = x.utc;
            }
        }
    }
    return o.n;
}
