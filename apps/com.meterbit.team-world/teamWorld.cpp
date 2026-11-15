/**
 * Team Around the World: up to eight teammates in other time zones, each with their local time and what they are
 * likely doing: working (green), the morning before work or the evening after (amber), asleep (dark blue, 22:00 to
 * 07:00), their weekend (grey; Friday and Saturday where that is the custom) or a public holiday where they are
 * (purple). The top line gives the next hours when everyone working that day overlaps, in Pixlpal's time.
 *
 * Time zones come from the city (World Clock's list and POSIX rules, wck_Model.cpp), holidays from Nager.Date
 * (a worker task, each country twice a day). Knob: turn to see one person with their next holidays.
 * NVS "tmwSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "wck_Model.h"
#include "tmw_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define PEOPLE      8
#define NAME_LEN    20
#define HOLIDAYS    6
#define NVS_KEY     "tmwSave"
#define SAVE_MAGIC  0x544D5701u
#define UA          "Pixlpal-TeamWorld/1.0"
#define SLEEP_FROM  (22 * 60)
#define SLEEP_TO    (7 * 60)

EXT_RAM_BSS_ATTR TaskHandle_t teamWorld_Task_H = NULL;
static void teamWorld_App_Task(void* dApplication);
static void tmw_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* teamWorld_App;
Mtb_Applications* teamWorld_App_GetInstance(){
    if (!teamWorld_App){
        teamWorld_App = new Mtb_Applications_FullScreen(teamWorld_App_Task, &teamWorld_Task_H, "Team Around the World", {0,0}, 8192);
        teamWorld_App->mtb_App_Set_Mobile_UI(TMW_UI_MANIFEST, TMW_UI_API, nullptr, tmw_Values);
    }
    return teamWorld_App;
}
MTB_REGISTER_APP(teamWorld_App, 0, 0)         // the catalogue gives the app its number

struct Tmw_Person_t { char name[NAME_LEN]; Wck_City_t city; };

struct Tmw_Save_t {
    uint32_t magic;
    Tmw_Person_t person[PEOPLE];
    uint16_t from, to;                  // working hours, minutes after midnight
};

struct Tmw_Holiday_t { int32_t day; char name[32]; };     // day: days since 1970-01-01 (the local date)

struct Tmw_Country_t {
    char code[3];
    uint8_t state;                      // 0 not fetched, 1 fetched, 2 not covered by Nager.Date, 3 failed
    uint32_t fetchedMs;
    Tmw_Holiday_t h[HOLIDAYS];
    uint8_t n;
};

struct Tmw_Shared_t {
    Tmw_Country_t country[PEOPLE];
    uint8_t countries;
    uint32_t version;
    volatile bool fetchNow, stop, running;
};

static EXT_RAM_BSS_ATTR Tmw_Save_t save;
static EXT_RAM_BSS_ATTR Tmw_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Tmw_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.from = 9 * 60; s.to = 17 * 60; }
    if (s.from >= 1440 || s.to >= 1440 || s.from == s.to){ s.from = 9 * 60; s.to = 17 * 60; }
    for (int i = 0; i < PEOPLE; i++){
        s.person[i].name[NAME_LEN - 1] = '\0';
        Wck_Tz_t tz;
        if (s.person[i].name[0] && !wck_Tz_Parse(s.person[i].city.tz, tz)) memset(&s.person[i], 0, sizeof(s.person[i]));
    }
}

static int64_t floor_Div(int64_t a, int64_t b){ int64_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
static int monday_Index(int64_t day){ int w = (int)((day + 3) % 7); return w < 0 ? w + 7 : w; }

/** Countries whose weekend is Friday and Saturday (Afghanistan: Thursday and Friday; Iran: Friday). */
static bool weekend_Day(const char* cc, int mondayIndex){
    static const char* const FRI_SAT = "sa qa kw bh om eg jo iq dz ly sy ye sd il bd mv";
    char k[3] = {cc[0], cc[1], 0};
    if (!strcmp(k, "af")) return mondayIndex == 3 || mondayIndex == 4;
    if (!strcmp(k, "ir")) return mondayIndex == 4;
    if (k[0] && strstr(FRI_SAT, k)) return mondayIndex == 4 || mondayIndex == 5;
    return mondayIndex >= 5;
}

static const Tmw_Country_t* country_Of(const Tmw_Shared_t& d, const char* cc){
    for (int i = 0; i < d.countries; i++) if (!strcmp(d.country[i].code, cc)) return &d.country[i];
    return nullptr;
}

static const Tmw_Holiday_t* holiday_On(const Tmw_Shared_t& d, const char* cc, int64_t day){
    const Tmw_Country_t* c = country_Of(d, cc);
    if (!c) return nullptr;
    for (int i = 0; i < c->n; i++) if (c->h[i].day == day) return &c->h[i];
    return nullptr;
}

// A person now ---------------------------------------------------------------------------------------------------------------------
enum : uint8_t { S_WORK = 0, S_MORNING, S_EVENING, S_ASLEEP, S_WEEKEND, S_HOLIDAY };

struct Tmw_Now_t {
    int32_t offset;                     // seconds east of UTC
    int64_t day;                        // their local date
    int minute;                         // their local minute of the day
    uint8_t state;
    const Tmw_Holiday_t* holiday;       // today's, if any
};

static bool in_Hours(int minute, int from, int to){ return from < to ? (minute >= from && minute < to) : (minute >= from || minute < to); }

static Tmw_Now_t person_Now(const Tmw_Person_t& p, const Tmw_Save_t& s, const Tmw_Shared_t& d, int64_t now){
    Tmw_Now_t r;
    r.offset = wck_Offset_Of(p.city.tz, now);
    const int64_t local = now + r.offset;
    r.day = floor_Div(local, 86400);
    r.minute = (int)((local - r.day * 86400) / 60);
    r.holiday = holiday_On(d, p.city.code, r.day);
    if (r.holiday) r.state = S_HOLIDAY;
    else if (weekend_Day(p.city.code, monday_Index(r.day))) r.state = S_WEEKEND;
    else if (in_Hours(r.minute, s.from, s.to)) r.state = S_WORK;
    else if (in_Hours(r.minute, SLEEP_FROM, SLEEP_TO)) r.state = S_ASLEEP;
    else r.state = (s.from < s.to ? r.minute < s.from : r.minute >= s.to && r.minute < s.from) ? S_MORNING : S_EVENING;
    return r;
}

static const char* state_Word(uint8_t s){
    static const char* const N[] = {"WORKING", "MORNING", "EVENING", "ASLEEP", "WEEKEND", "HOLIDAY"};
    return N[s < 6 ? s : 0];
}
static uint16_t state_Color(uint8_t s){
    switch (s){
        case S_WORK: return MTB_KIT_RGB(40, 205, 90);
        case S_MORNING: case S_EVENING: return MTB_KIT_RGB(255, 170, 30);
        case S_ASLEEP: return MTB_KIT_RGB(70, 90, 200);
        case S_HOLIDAY: return MTB_KIT_RGB(190, 90, 255);
        default: return MTB_KIT_RGB(120, 120, 130);
    }
}

/**
 * The next shared working hours: on each of the next seven days (each person's own date), the hours all those
 * working that day have in common, the first that hasn't ended. False if none.
 */
static bool shared_Hours(const Tmw_Save_t& s, const Tmw_Shared_t& d, int64_t now, int64_t& from, int64_t& to, int& off, int& count){
    if (s.from >= s.to) return false;                                       // night shifts: no common day
    for (int k = 0; k < 7; k++){
        int64_t a = INT64_MIN, b = INT64_MAX;
        int in = 0, out = 0;
        for (int i = 0; i < PEOPLE; i++){
            const Tmw_Person_t& p = s.person[i];
            if (!p.name[0]) continue;
            const int32_t o = wck_Offset_Of(p.city.tz, now);
            const int64_t day = floor_Div(now + o, 86400) + k;
            if (holiday_On(d, p.city.code, day) || weekend_Day(p.city.code, monday_Index(day))){ out++; continue; }
            const int64_t start = day * 86400 + s.from * 60 - o, end = day * 86400 + s.to * 60 - o;
            if (start > a) a = start;
            if (end < b) b = end;
            in++;
        }
        if (in >= 2 && a < b && b > now){ from = a; to = b; off = out; count = in; return true; }
    }
    return false;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Person(JsonDocument& c){
    for (int i = 0; i < PEOPLE; i++){
        char key[10];
        snprintf(key, sizeof(key), "person%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        char text[96];
        mtb_Kit_To_Ascii(text, sizeof(text), v);
        Tmw_Person_t p = {};
        char* t = text;
        while (*t == ' ') t++;
        if (*t){
            char* comma = strchr(t, ',');
            if (!comma){ mtb_Ble_App_Cmd_Reject(); return; }
            *comma = '\0';
            size_t n = strlen(t);
            while (n && t[n - 1] == ' ') t[--n] = '\0';
            const char* q = comma + 1;
            while (*q == ' ') q++;
            Wck_City_t found[1];
            if (!n || n >= NAME_LEN || !*q || wck_Search(q, found, 1) != 1){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(p.name, sizeof(p.name), "%s", t);
            p.city = found[0];
        }
        portENTER_CRITICAL(&lock);
        save.person[i] = p;
        portEXIT_CRITICAL(&lock);
        sh.fetchNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Hours(JsonDocument& c){
    const int from = mtb_Kit_Parse_Clock(c["from"] | ""), to = mtb_Kit_Parse_Clock(c["to"] | "");
    if (from < 0 || to < 0 || from >= 1440 || to >= 1440 || from == to){ mtb_Ble_App_Cmd_Reject(); return; }
    save.from = (uint16_t)from; save.to = (uint16_t)to;
    phoneChanged = true;
}
static void cmd_Fetch(JsonDocument&){
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); for (int i = 0; i < sh.countries; i++) sh.country[i].state = 0; portEXIT_CRITICAL(&lock);
    sh.fetchNow = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker: holidays --------------------------------------------------------------------------------------------------------------
static bool fetch_Country(const char* cc, Tmw_Country_t& out){
    char url[96], up[3] = {(char)toupper((unsigned char)cc[0]), (char)toupper((unsigned char)cc[1]), 0};
    snprintf(url, sizeof(url), "https://date.nager.at/api/v3/NextPublicHolidays/%s", up);
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 32768, r, UA);
    out.n = 0;
    out.fetchedMs = millis();
    if (r.code == 204 || r.code == 404){ heap_caps_free(body); out.state = 2; return true; }
    if (r.code != 200 || !body){ heap_caps_free(body); out.state = 3; return false; }
    JsonDocument filter, doc;
    filter[0]["date"] = true; filter[0]["name"] = true; filter[0]["global"] = true;
    const bool bad = deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter)) != DeserializationError::Ok;
    heap_caps_free(body);
    if (bad){ out.state = 3; return false; }
    for (JsonObject h : doc.as<JsonArray>()){
        if (!(h["global"] | false) || out.n == HOLIDAYS) continue;          // national holidays only
        int y, m, d;
        if (sscanf(h["date"] | "", "%d-%d-%d", &y, &m, &d) != 3) continue;
        out.h[out.n].day = mtb_Kit_Days_From_Civil(y, m, d);
        mtb_Kit_To_Ascii(out.h[out.n].name, sizeof(out.h[out.n].name), h["name"] | "");
        out.n++;
    }
    out.state = 1;
    return true;
}

static void worker(void*){
    while (!sh.stop){
        sh.fetchNow = false;
        // The countries the team is in.
        char codes[PEOPLE][3];
        int nc = 0;
        portENTER_CRITICAL(&lock);
        for (int i = 0; i < PEOPLE; i++){
            if (!save.person[i].name[0] || !save.person[i].city.code[0]) continue;
            bool seen = false;
            for (int k = 0; k < nc; k++) if (!strcmp(codes[k], save.person[i].city.code)) seen = true;
            if (!seen) snprintf(codes[nc++], 3, "%s", save.person[i].city.code);
        }
        portEXIT_CRITICAL(&lock);
        for (int k = 0; k < nc && !sh.stop; k++){
            Tmw_Country_t c = {};
            bool due = true;
            portENTER_CRITICAL(&lock);
            const Tmw_Country_t* had = country_Of(sh, codes[k]);
            if (had){
                c = *had;
                const uint32_t age = millis() - had->fetchedMs;
                due = had->state == 0 || (had->state == 3 && age > 10 * 60000u) || age > 12 * 3600000u;
            }
            portEXIT_CRITICAL(&lock);
            if (!due) continue;
            snprintf(c.code, sizeof(c.code), "%s", codes[k]);
            fetch_Country(codes[k], c);
            portENTER_CRITICAL(&lock);
            Tmw_Country_t* slot = (Tmw_Country_t*)country_Of(sh, codes[k]);
            if (!slot && sh.countries < PEOPLE) slot = &sh.country[sh.countries++];
            if (slot) *slot = c;
            sh.version++;
            portEXIT_CRITICAL(&lock);
        }
        for (int i = 0; i < 300 && !sh.stop && !sh.fetchNow; i++) vTaskDelay(pdMS_TO_TICKS(200));   // a minute
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(5, 7, 12), C_HEAD = MTB_KIT_RGB(14, 18, 28), C_ROW = MTB_KIT_RGB(12, 15, 24);

static void hm(char* t, size_t n, int minute){
    const int h = minute / 60, m = minute % 60;
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", h, m);
    else snprintf(t, n, "%d:%02d%s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}
static void pixlpal_Clock(char* t, size_t n, int64_t utc){
    pxl_time_t pt; pxl_time(&pt);
    const int64_t l = utc + pt.utc_offset_minutes * 60;
    hm(t, n, (int)((l - floor_Div(l, 86400) * 86400) / 60));
}
static const char* const DAYS[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
static const char* const MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void date_Text(char* t, size_t n, int64_t day){
    int y, m, dd;
    const int64_t z0 = day + 719468;
    const int64_t era = (z0 >= 0 ? z0 : z0 - 146096) / 146097;
    const int64_t doe = z0 - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    dd = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); y = (int)(yoe + era * 400 + (m <= 2));
    (void)y;
    snprintf(t, n, "%s %d %s", DAYS[monday_Index(day)], dd, MON[m - 1]);
}

static void header(const Tmw_Save_t& s, const Tmw_Shared_t& d, int64_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, "TEAM", C_HEAD);
    char t[64], a[16], b[16];
    int64_t from, to;
    int off, count;
    uint16_t c = KIT_LABEL;
    if (shared_Hours(s, d, now, from, to, off, count)){
        pixlpal_Clock(b, sizeof(b), to);
        if (from <= now){ snprintf(t, sizeof(t), "ALL AT WORK NOW, UNTIL %s", b); c = KIT_GREEN; }
        else {
            pixlpal_Clock(a, sizeof(a), from);
            pxl_time_t pt; pxl_time(&pt);
            const int64_t today = floor_Div(now + pt.utc_offset_minutes * 60, 86400), day = floor_Div(from + pt.utc_offset_minutes * 60, 86400);
            if (day == today) snprintf(t, sizeof(t), "ALL AT WORK %s-%s", a, b);
            else if (day == today + 1) snprintf(t, sizeof(t), "ALL AT WORK TOMORROW %s-%s", a, b);
            else snprintf(t, sizeof(t), "ALL AT WORK %s %s-%s", DAYS[monday_Index(day)], a, b);
            c = KIT_CYAN;
        }
        if (off){ char more[16]; snprintf(more, sizeof(more), " (%d OFF)", off); strlcat(t, more, sizeof(t)); }
    } else snprintf(t, sizeof(t), s.from >= s.to ? "NIGHT HOURS: NO SHARED DAY" : "NO HOURS WHEN ALL OVERLAP");
    char fit[64];
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 36);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, c, fit, C_HEAD);
}

static void draw_Team(const Tmw_Save_t& s, const Tmw_Shared_t& d){
    mtb_Panel_Fill_Screen(C_BG);
    const int64_t now = (int64_t)time(nullptr);
    int list[PEOPLE], used = 0;
    for (int i = 0; i < PEOPLE; i++) if (s.person[i].name[0]) list[used++] = i;
    if (!used){
        mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
        mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, "TEAM", C_HEAD);
        mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, "NO TEAMMATES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "ADD THEM IN THE PHONE APP: NAME, CITY", C_BG);
        return;
    }
    header(s, d, now);
    pxl_time_t pt; pxl_time(&pt);
    const int64_t myDay = floor_Div(now + pt.utc_offset_minutes * 60, 86400);
    const int rowH = used <= 4 ? 28 : 14;
    for (int k = 0; k < used; k++){
        const Tmw_Person_t& p = s.person[list[k]];
        const Tmw_Now_t n = person_Now(p, s, d, now);
        const uint16_t sc = state_Color(n.state);
        const int y = 13 + k * rowH;
        mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 2), C_ROW);
        mtb_Panel_Fill_Rect(0, (int16_t)y, 2, (int16_t)(y + rowH - 2), sc);
        char nm[NAME_LEN], fit[40], t[48], c[48];
        snprintf(nm, sizeof(nm), "%s", p.name); mtb_Kit_Upper(nm);
        snprintf(c, sizeof(c), "%s", p.city.name); mtb_Kit_Upper(c);
        char tm[16];
        hm(tm, sizeof(tm), n.minute);
        const char* dayMark = n.day > myDay ? "+1" : n.day < myDay ? "-1" : "";
        if (rowH >= 28){                                                    // four or fewer: large
            mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, nm, 104);
            mtb_Kit_Text(7, y + 3, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_ROW);
            mtb_Kit_Text(116, y + 3, Terminal10x17, KIT_TEXT, tm, C_ROW);
            if (*dayMark) mtb_Kit_Text(116 + mtb_Kit_Text_Width(Terminal10x17, tm) + 2, y + 3, Terminal4x6, KIT_LABEL, dayMark, C_ROW);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, c, 70);
            mtb_Kit_Text(116, y + 21, Terminal4x6, KIT_DIM, fit, C_ROW);
            mtb_Kit_Text_Right(W - 4, y + 4, Terminal6x8, sc, state_Word(n.state), C_ROW);
            if (n.holiday){ char hn[32]; snprintf(hn, sizeof(hn), "%s", n.holiday->name); mtb_Kit_Upper(hn); mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, hn, 66); mtb_Kit_Text_Right(W - 4, y + 15, Terminal4x6, sc, fit, C_ROW); }
        } else {
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, 74);
            mtb_Kit_Text(7, y + 3, Terminal6x8, KIT_TEXT, fit, C_ROW);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, c, 52);
            mtb_Kit_Text(84, y + 4, Terminal4x6, KIT_DIM, fit, C_ROW);
            snprintf(t, sizeof(t), "%s", tm);
            mtb_Kit_Text(140, y + 3, Terminal6x8, KIT_TEXT, t, C_ROW);
            if (*dayMark) mtb_Kit_Text(140 + mtb_Kit_Text_Width(Terminal6x8, t) + 2, y + 2, Terminal4x6, KIT_LABEL, dayMark, C_ROW);
            mtb_Kit_Text_Right(W - 4, y + 3, Terminal6x8, sc, state_Word(n.state), C_ROW);
        }
    }
}

static void draw_Person(const Tmw_Save_t& s, const Tmw_Shared_t& d, int i){
    mtb_Panel_Fill_Screen(C_BG);
    const int64_t now = (int64_t)time(nullptr);
    const Tmw_Person_t& p = s.person[i];
    const Tmw_Now_t n = person_Now(p, s, d, now);
    const uint16_t sc = state_Color(n.state);
    char t[64], fit[64];
    snprintf(t, sizeof(t), "%s", p.name); mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, t, 140);
    mtb_Kit_Text(4, 3, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_BG);
    snprintf(t, sizeof(t), "%s, %s", p.city.name, p.city.country); mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, 140);
    mtb_Kit_Text(4, 27, Terminal4x6, KIT_LABEL, fit, C_BG);
    hm(t, sizeof(t), n.minute);
    mtb_Kit_Text_Right(W - 4, 2, PT_Sans_Narrow_Bold28x34, KIT_TEXT, t, C_BG);
    pxl_time_t pt; pxl_time(&pt);
    const int diff = n.offset - pt.utc_offset_minutes * 60;
    if (diff == 0) snprintf(t, sizeof(t), "SAME TIME AS HERE");
    else if (diff % 3600 == 0) snprintf(t, sizeof(t), "%dH %s", (diff < 0 ? -diff : diff) / 3600, diff > 0 ? "AHEAD" : "BEHIND");
    else snprintf(t, sizeof(t), "%dH%02d %s", (diff < 0 ? -diff : diff) / 3600, ((diff < 0 ? -diff : diff) % 3600) / 60, diff > 0 ? "AHEAD" : "BEHIND");
    mtb_Kit_Text_Right(W - 4, 37, Terminal4x6, KIT_LABEL, t, C_BG);
    mtb_Panel_Fill_Rect(0, 46, W - 1, 64, sc);
    mtb_Kit_Text(6, 50, Terminal8x12, BLACK, state_Word(n.state), sc);
    if (n.holiday){ snprintf(t, sizeof(t), "%s", n.holiday->name); mtb_Kit_Upper(t); mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, 150); mtb_Kit_Text_Right(W - 6, 52, Terminal6x8, BLACK, fit, sc); }
    else {
        char a[16], b[16]; hm(a, sizeof(a), s.from); hm(b, sizeof(b), s.to);
        snprintf(t, sizeof(t), "WORKS %s-%s", a, b);
        mtb_Kit_Text_Right(W - 6, 52, Terminal6x8, BLACK, t, sc);
    }
    mtb_Kit_Text(4, 70, Terminal6x8, KIT_LABEL, "NEXT HOLIDAYS", C_BG);
    const Tmw_Country_t* c = country_Of(d, p.city.code);
    if (!c || c->state == 0) mtb_Kit_Text(4, 82, Terminal6x8, KIT_DIM, "FETCHING...", C_BG);
    else if (c->state == 2) mtb_Kit_Text(4, 82, Terminal6x8, KIT_DIM, "NOT LISTED FOR THIS COUNTRY", C_BG);
    else if (c->state == 3) mtb_Kit_Text(4, 82, Terminal6x8, KIT_DIM, "COULDN'T FETCH THEM", C_BG);
    else {
        int shown = 0;
        for (int k = 0; k < c->n && shown < 4; k++){
            if (c->h[k].day < n.day) continue;
            char dt[24], hn[32];
            date_Text(dt, sizeof(dt), c->h[k].day);
            snprintf(hn, sizeof(hn), "%s", c->h[k].name); mtb_Kit_Upper(hn);
            const int y = 82 + shown * 11;
            mtb_Kit_Text(4, y, Terminal6x8, c->h[k].day == n.day ? state_Color(S_HOLIDAY) : KIT_CYAN, dt, C_BG);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, hn, W - 90);
            mtb_Kit_Text(84, y, Terminal6x8, KIT_TEXT, fit, C_BG);
            shown++;
        }
        if (!shown) mtb_Kit_Text(4, 82, Terminal6x8, KIT_DIM, "NONE IN THE COMING MONTHS", C_BG);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void teamWorld_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Person, cmd_Hours, cmd_Fetch);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "tmwNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Tmw_Shared_t* d = (Tmw_Shared_t*)heap_caps_calloc(1, sizeof(Tmw_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0;
    int view = -1;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; if (view >= 0 && !save.person[view].name[0]) view = -1; }
        const int t = turns;
        if (t){
            turns -= t;
            int k = view;
            for (int step = 0; step < (t > 0 ? t : -t); step++)
                for (int n = 0; n <= PEOPLE; n++){
                    k += t > 0 ? 1 : -1;
                    if (k >= PEOPLE) k = -1;
                    if (k < -1) k = PEOPLE - 1;
                    if (k == -1 || save.person[k].name[0]) break;
                }
            view = k;
            redraw = true;
        }
        if (clickRequested){ clickRequested = false; view = -1; redraw = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        if (redraw || ms - lastDraw >= 2000){
            redraw = false;
            lastDraw = ms;
            Tmw_Save_t* s = &save;
            mtb_Panel_Begin_Update();
            if (view < 0) draw_Team(*s, *d); else draw_Person(*s, *d, view);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        if ((ms / 60000) != (pushedMs / 60000)) push = true;                // the phone's times follow the minutes
        delay(50);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void tmw_Values(JsonObject values){
    Tmw_Save_t* s = (Tmw_Save_t*)heap_caps_malloc(sizeof(Tmw_Save_t), MALLOC_CAP_SPIRAM);
    Tmw_Shared_t* d = (Tmw_Shared_t*)heap_caps_malloc(sizeof(Tmw_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    const int64_t now = (int64_t)time(nullptr);
    char t[128], a[16], b[16];
    hm(a, sizeof(a), s->from); hm(b, sizeof(b), s->to);
    char f[8], to[8];
    snprintf(f, sizeof(f), "%02d:%02d", s->from / 60, s->from % 60); snprintf(to, sizeof(to), "%02d:%02d", s->to / 60, s->to % 60);
    values["from"] = f; values["to"] = to;
    JsonArray team = values["team"].to<JsonArray>();
    for (int i = 0; i < PEOPLE; i++){
        char key[10];
        snprintf(key, sizeof(key), "person%d", i);
        Tmw_Person_t& p = s->person[i];        // not const: ArduinoJson copies char*, but keeps a const char* by pointer
        if (!p.name[0]){ values[key] = ""; continue; }
        snprintf(t, sizeof(t), "%s, %s, %s", p.name, p.city.name, p.city.country);
        values[key] = t;
        const Tmw_Now_t n = person_Now(p, *s, *d, now);
        char tm[16];
        hm(tm, sizeof(tm), n.minute);
        static const char* const WORD[] = {"working", "before work", "evening", "asleep", "weekend", "public holiday"};
        if (n.holiday) snprintf(t, sizeof(t), "%s in %s, %s (%s)", tm, p.city.name, WORD[n.state], n.holiday->name);
        else snprintf(t, sizeof(t), "%s in %s, %s", tm, p.city.name, WORD[n.state]);
        JsonObject r = team.add<JsonObject>();
        r["label"] = p.name;
        r["value"] = t;
        r["state"] = n.state == S_WORK ? "ok" : n.state == S_HOLIDAY ? "warn" : n.state == S_ASLEEP ? "off" : "info";
    }
    JsonArray together = values["together"].to<JsonArray>();
    JsonObject r = together.add<JsonObject>();
    r["label"] = "Everyone at work";
    int64_t from, upto;
    int off, count;
    if (shared_Hours(*s, *d, now, from, upto, off, count)){
        pixlpal_Clock(a, sizeof(a), from); pixlpal_Clock(b, sizeof(b), upto);
        if (from <= now) snprintf(t, sizeof(t), "Now, until %s (your time)%s", b, off ? ", some are off today" : "");
        else snprintf(t, sizeof(t), "Next %s to %s (your time)%s", a, b, off ? ", some are off that day" : "");
        r["state"] = from <= now ? "ok" : "info";
    } else { snprintf(t, sizeof(t), "No hours this week when everyone working overlaps"); r["state"] = "warn"; }
    r["value"] = t;
    heap_caps_free(s);
    heap_caps_free(d);
}
