/**
 * Events Directory: a hotel or venue lobby's sign of today's events. Each event with its time, its name, its room and
 * an arrow the way to it (from the directions set for each room); what is on now is marked NOW. Up to three events
 * show large; more show as a list that pages by itself. When the day's events are over it shows tomorrow's first.
 *
 * Events come from up to two iCal calendars (a worker task, evd_Ics.cpp; the room is the event's LOCATION) and up to
 * eight typed on the phone ("18:00-23:00 Okafor Wedding @ Ballroom"). Knob: turn to page, click to read the calendars
 * now. NVS "evdSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "evd_Ics.h"
#include "evd_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define CALS        2
#define URL_LEN     320
#define TYPED       8
#define WAYS        8
#define EVENTS      32
#define FROM_CAL    24
#define NVS_KEY     "evdSave"
#define SAVE_MAGIC  0x45564401u
#define UA          "Pixlpal-EventsDirectory/1.0"
#define PAGE_MS     8000

EXT_RAM_BSS_ATTR TaskHandle_t eventsDirectory_Task_H = NULL;
static void eventsDirectory_App_Task(void* dApplication);
static void evd_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* eventsDirectory_App;
Mtb_Applications* eventsDirectory_App_GetInstance(){
    if (!eventsDirectory_App){
        eventsDirectory_App = new Mtb_Applications_FullScreen(eventsDirectory_App_Task, &eventsDirectory_Task_H, "Events Directory", {0,0}, 8192);
        eventsDirectory_App->mtb_App_Set_Mobile_UI(EVD_UI_MANIFEST, EVD_UI_API, nullptr, evd_Values);
    }
    return eventsDirectory_App;
}
MTB_REGISTER_APP(eventsDirectory_App, 0, 0)   // the catalogue gives the app its number

enum : uint8_t { WAY_NONE = 0, WAY_LEFT, WAY_RIGHT, WAY_AHEAD, WAY_BACK, WAY_UP, WAY_DOWN };

struct Evd_Typed_t { int32_t day; uint16_t from, to; char title[40]; char place[EVD_PLACE_LEN]; };    // day: local date
struct Evd_Way_t { char place[EVD_PLACE_LEN]; uint8_t way; };

struct Evd_Save_t {
    uint32_t magic;
    char url[CALS][URL_LEN];
    Evd_Typed_t typed[TYPED];
    Evd_Way_t way[WAYS];
    char venue[40];
    uint8_t minutes, titles;
};

enum : uint8_t { F_NONE = 0, F_OK, F_NOT_ALLOWED, F_HTTP, F_NO_ANSWER, F_NOT_ICS };

struct Evd_Shared_t {
    Evd_Event_t cal[FROM_CAL];          // from the calendars
    int calN;
    uint8_t fetch[CALS];
    int code[CALS];
    uint32_t version;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Evd_Save_t save;
static EXT_RAM_BSS_ATTR Evd_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Evd_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 10; s.titles = 1; }
    if (s.minutes < 1 || s.minutes > 30) s.minutes = 10;
    for (int i = 0; i < CALS; i++) s.url[i][URL_LEN - 1] = '\0';
    for (int i = 0; i < TYPED; i++){ s.typed[i].title[sizeof(s.typed[i].title) - 1] = s.typed[i].place[EVD_PLACE_LEN - 1] = '\0'; }
    for (int i = 0; i < WAYS; i++){ s.way[i].place[EVD_PLACE_LEN - 1] = '\0'; if (s.way[i].way > WAY_DOWN) s.way[i].way = WAY_NONE; }
    s.venue[sizeof(s.venue) - 1] = '\0';
}

static int32_t local_Offset(void){ pxl_time_t t; pxl_time(&t); return t.utc_offset_minutes * 60; }
static int64_t local_Day(int64_t utc){ const int64_t l = utc + local_Offset(); return l >= 0 ? l / 86400 : (l - 86399) / 86400; }

/** The way to an event's room: the first room set whose name starts the event's place. */
static uint8_t way_To(const Evd_Save_t& s, const char* place){
    if (!place[0]) return WAY_NONE;
    for (int i = 0; i < WAYS; i++){
        const size_t n = strlen(s.way[i].place);
        if (n && !strncasecmp(place, s.way[i].place, n)) return s.way[i].way;
    }
    return WAY_NONE;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Url(JsonDocument& c){
    for (int k = 0; k < CALS; k++){
        char key[6];
        snprintf(key, sizeof(key), "url%d", k);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        char url[URL_LEN];
        size_t n = strlen(v);
        while (n && v[n - 1] == ' ') n--;
        if (!n) url[0] = '\0';
        else {
            const char* rest = nullptr;
            bool plain = false;
            if (!strncasecmp(v, "webcal://", 9)) rest = v + 9;
            else if (!strncasecmp(v, "webcals://", 10)) rest = v + 10;
            else if (!strncasecmp(v, "https://", 8)) rest = v + 8;
            else if (!strncasecmp(v, "http://", 7)){ rest = v + 7; plain = true; }
            if (!rest || n - (size_t)(rest - v) + 8 >= URL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(url, sizeof(url), "%s%.*s", plain ? "http://" : "https://", (int)(n - (size_t)(rest - v)), rest);
            const char* host = strstr(url, "://") + 3;
            if (!*host || *host == '/' || !strchr(host, '.')){ mtb_Ble_App_Cmd_Reject(); return; }
            for (const char* q = url; *q; q++) if ((unsigned char)*q <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
        }
        portENTER_CRITICAL(&lock); snprintf(save.url[k], URL_LEN, "%s", url); sh.fetch[k] = F_NONE; portEXIT_CRITICAL(&lock);
        sh.readNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}

/** "8:05" or "08.05": minutes after midnight, -1 if not a time. Moves @p p past it. */
static int read_Clock(const char*& p){
    while (*p == ' ') p++;
    if (!isdigit((unsigned char)*p)) return -1;
    int h = 0;
    while (isdigit((unsigned char)*p)) h = h * 10 + (*p++ - '0');
    if ((*p != ':' && *p != '.') || !isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2])) return -1;
    const int m = (p[1] - '0') * 10 + (p[2] - '0');
    p += 3;
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

static void cmd_Event(JsonDocument& c){
    for (int k = 0; k < TYPED; k++){
        char key[8];
        snprintf(key, sizeof(key), "event%d", k);
        if (c[key].isNull()) continue;
        char text[128];
        mtb_Kit_To_Ascii(text, sizeof(text), c[key] | "");
        Evd_Typed_t e = {};
        const char* p = text;
        while (*p == ' ') p++;
        if (*p){
            pxl_time_t t; pxl_time(&t);
            int32_t day = mtb_Kit_Days_From_Civil(t.year, t.month, t.day);
            int y, mo, d;
            if (!strncasecmp(p, "tomorrow", 8)){ day++; p += 8; }
            else if (sscanf(p, "%4d-%2d-%2d", &y, &mo, &d) == 3 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31){ day = mtb_Kit_Days_From_Civil(y, mo, d); p += 10; }
            const int from = read_Clock(p);
            while (*p == ' ') p++;
            if (from < 0 || *p != '-'){ mtb_Ble_App_Cmd_Reject(); return; }
            p++;
            const int to = read_Clock(p);
            if (to < 0 || to == from){ mtb_Ble_App_Cmd_Reject(); return; }
            while (*p == ' ') p++;
            const char* at = strstr(p, " @ ");
            if (!at) at = strchr(p, '@');
            size_t tl = at ? (size_t)(at - p) : strlen(p);
            while (tl && p[tl - 1] == ' ') tl--;
            if (!tl){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(e.title, sizeof(e.title), "%.*s", (int)(tl < sizeof(e.title) - 1 ? tl : sizeof(e.title) - 1), p);
            if (at){ const char* q = at + (at[0] == '@' ? 1 : 3); while (*q == ' ' || *q == '@') q++; snprintf(e.place, sizeof(e.place), "%s", q); }
            e.day = day; e.from = (uint16_t)from; e.to = (uint16_t)to;
        }
        portENTER_CRITICAL(&lock); save.typed[k] = e; sh.version++; portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}

static void cmd_Way(JsonDocument& c){
    static const char* const WORDS[] = {"", "left", "right", "ahead", "back", "upstairs", "downstairs"};
    for (int k = 0; k < WAYS; k++){
        char key[6];
        snprintf(key, sizeof(key), "dir%d", k);
        if (c[key].isNull()) continue;
        char text[64];
        mtb_Kit_To_Ascii(text, sizeof(text), c[key] | "");
        size_t n = strlen(text);
        while (n && text[n - 1] == ' ') text[--n] = '\0';
        Evd_Way_t w = {};
        if (n){
            char* sp = strrchr(text, ' ');
            if (!sp){ mtb_Ble_App_Cmd_Reject(); return; }
            const char* word = sp + 1;
            for (int i = 1; i <= WAY_DOWN; i++) if (!strcasecmp(word, WORDS[i])) w.way = (uint8_t)i;
            if (!strcasecmp(word, "straight") || !strcasecmp(word, "up")) w.way = !strcasecmp(word, "up") ? WAY_UP : WAY_AHEAD;
            if (!strcasecmp(word, "down")) w.way = WAY_DOWN;
            if (!w.way){ mtb_Ble_App_Cmd_Reject(); return; }
            *sp = '\0';
            const char* q = text; while (*q == ' ') q++;
            if (!*q){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(w.place, sizeof(w.place), "%s", q);
        }
        portENTER_CRITICAL(&lock); save.way[k] = w; sh.version++; portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Venue(JsonDocument& c){
    const char* v = c["venue"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    portENTER_CRITICAL(&lock); snprintf(save.venue, sizeof(save.venue), "%s", t); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 1, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint8_t)v; phoneChanged = true; }
static void cmd_Read(JsonDocument&){ if (!appOpen || (!save.url[0][0] && !save.url[1][0])){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }
static void cmd_Titles(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["titles"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.titles = (uint8_t)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker: the calendars ---------------------------------------------------------------------------------------------------------
static EXT_RAM_BSS_ATTR Evd_Event_t got[FROM_CAL];

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        Evd_Save_t* s = (Evd_Save_t*)heap_caps_malloc(sizeof(Evd_Save_t), MALLOC_CAP_SPIRAM);
        if (!s){ nextRead = millis() + 10000; continue; }
        portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
        nextRead = millis() + s->minutes * 60000u;
        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u){ heap_caps_free(s); nextRead = millis() + 5000; continue; }
        // Today and tomorrow, local.
        const int64_t today = local_Day(now), from = today * 86400 - local_Offset(), to = from + 2 * 86400;
        int n = 0;
        uint8_t fetch[CALS] = {F_NONE, F_NONE};
        int code[CALS] = {0, 0};
        for (int k = 0; k < CALS && !sh.stop; k++){
            if (!s->url[k][0]) continue;
            Mtb_Kit_Http_t r = {};
            char* body = mtb_Kit_Http_Get(s->url[k], nullptr, nullptr, 0, 65536, r, UA);
            code[k] = r.code;
            if (r.code == PXL_HTTP_NOT_ALLOWED) fetch[k] = F_NOT_ALLOWED;
            else if (r.code <= 0) fetch[k] = F_NO_ANSWER;
            else if (r.code < 200 || r.code >= 300) fetch[k] = F_HTTP;
            else if (body){
                Evd_Ics_Report_t rep = {};
                const int m = evd_Ics_Events(body, r.length, from, to, local_Offset(), got + n, FROM_CAL - n, &rep);
                fetch[k] = rep.calendar ? F_OK : F_NOT_ICS;
                for (int i = n; i < n + m; i++){                                 // the panel's fonts are ASCII
                    char a[EVD_TITLE_LEN]; mtb_Kit_To_Ascii(a, sizeof(a), got[i].title); snprintf(got[i].title, sizeof(got[i].title), "%s", a);
                    char b[EVD_PLACE_LEN]; mtb_Kit_To_Ascii(b, sizeof(b), got[i].place); snprintf(got[i].place, sizeof(got[i].place), "%s", b);
                }
                if (rep.calendar) n += m;
            }
            heap_caps_free(body);
        }
        portENTER_CRITICAL(&lock);
        bool same = true;
        for (int k = 0; k < CALS; k++) if (strcmp(s->url[k], save.url[k])) same = false;
        if (same){
            memcpy(sh.cal, got, sizeof(got[0]) * n);
            sh.calN = n;
            for (int k = 0; k < CALS; k++){ sh.fetch[k] = fetch[k]; sh.code[k] = code[k]; }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
        heap_caps_free(s);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Today's events -------------------------------------------------------------------------------------------------------------------
struct Evd_Item_t { int64_t start, end; char title[48]; char place[EVD_PLACE_LEN]; };

/** The events of @p day (local) not over at @p now, from the calendars and typed here, by start. */
static int day_Events(const Evd_Save_t& s, const Evd_Shared_t& d, int64_t day, int64_t now, Evd_Item_t* out, int max){
    int n = 0;
    const int32_t off = local_Offset();
    auto add = [&](int64_t a, int64_t b, const char* title, const char* place){
        if (b <= now || local_Day(a) != day || n == max) return;
        int i = n++;
        while (i > 0 && out[i - 1].start > a){ out[i] = out[i - 1]; i--; }
        out[i].start = a; out[i].end = b;
        snprintf(out[i].title, sizeof(out[i].title), "%s", title);
        snprintf(out[i].place, sizeof(out[i].place), "%s", place);
    };
    for (int i = 0; i < d.calN; i++) if (!d.cal[i].allDay) add(d.cal[i].start, d.cal[i].end, d.cal[i].title, d.cal[i].place);
    for (int i = 0; i < TYPED; i++){
        const Evd_Typed_t& e = s.typed[i];
        if (!e.title[0] || e.day != day) continue;
        const int64_t a = (int64_t)e.day * 86400 + e.from * 60 - off;
        int64_t b = (int64_t)e.day * 86400 + e.to * 60 - off;
        if (b <= a) b += 86400;                                                 // ends after midnight
        add(a, b, e.title, e.place);
    }
    return n;
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 14), C_HEAD = MTB_KIT_RGB(18, 16, 10), C_GOLD = MTB_KIT_RGB(230, 185, 90), C_ROW = MTB_KIT_RGB(14, 16, 26);

static void clock_Text(char* t, size_t n, int64_t utc){
    struct tm lt;
    time_t x = (time_t)utc;
    localtime_r(&x, &lt);
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(t, n, "%d:%02d%s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

/** An arrow the way to a room, in a box @p size wide, centred at (cx, cy). Stairs get a step under the arrow. */
static void arrow(int cx, int cy, int size, uint8_t way, uint16_t c){
    if (way == WAY_NONE) return;
    const int h = size / 2, t = size / 6 < 2 ? 2 : size / 6;
    switch (way){
        case WAY_RIGHT: mtb_Panel_Fill_Rect((int16_t)(cx - h), (int16_t)(cy - t / 2), (int16_t)cx, (int16_t)(cy + t / 2), c);
                        mtb_Panel_Fill_Triangle((int16_t)cx, (int16_t)(cy - h), (int16_t)(cx + h), (int16_t)cy, (int16_t)cx, (int16_t)(cy + h), c); break;
        case WAY_LEFT:  mtb_Panel_Fill_Rect((int16_t)cx, (int16_t)(cy - t / 2), (int16_t)(cx + h), (int16_t)(cy + t / 2), c);
                        mtb_Panel_Fill_Triangle((int16_t)cx, (int16_t)(cy - h), (int16_t)(cx - h), (int16_t)cy, (int16_t)cx, (int16_t)(cy + h), c); break;
        case WAY_AHEAD: case WAY_UP:
                        mtb_Panel_Fill_Rect((int16_t)(cx - t / 2), (int16_t)cy, (int16_t)(cx + t / 2), (int16_t)(cy + h), c);
                        mtb_Panel_Fill_Triangle((int16_t)(cx - h), (int16_t)cy, (int16_t)cx, (int16_t)(cy - h), (int16_t)(cx + h), (int16_t)cy, c); break;
        case WAY_BACK: case WAY_DOWN:
                        mtb_Panel_Fill_Rect((int16_t)(cx - t / 2), (int16_t)(cy - h), (int16_t)(cx + t / 2), (int16_t)cy, c);
                        mtb_Panel_Fill_Triangle((int16_t)(cx - h), (int16_t)cy, (int16_t)cx, (int16_t)(cy + h), (int16_t)(cx + h), (int16_t)cy, c); break;
    }
    if (way == WAY_UP || way == WAY_DOWN){                                      // stairs: a step beside the arrow
        const int x = cx + h + 2, y = cy + h;
        mtb_Panel_Draw_HLine((int16_t)y, (int16_t)x, (int16_t)(x + 3), c);
        mtb_Panel_Draw_VLine((int16_t)(x + 3), (int16_t)(y - 3), (int16_t)y, c);
        mtb_Panel_Draw_HLine((int16_t)(y - 3), (int16_t)(x + 3), (int16_t)(x + 6), c);
        mtb_Panel_Draw_VLine((int16_t)(x + 6), (int16_t)(y - 6), (int16_t)(y - 3), c);
    }
}

static void draw_Sign(const Evd_Save_t& s, const Evd_Shared_t& d, int page, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const int64_t now = (int64_t)time(nullptr);
    char t[64], fit[64], a[16], b[16];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), s.venue[0] ? "TODAY AT %s" : "TODAY'S EVENTS", s.venue);
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 46);
    mtb_Kit_Text(3, 2, Terminal6x8, C_GOLD, fit, C_HEAD);
    if (now > 1700000000){ clock_Text(a, sizeof(a), now); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD); }

    static Evd_Item_t items[EVENTS];
    const int64_t today = local_Day(now);
    int n = day_Events(s, d, today, now, items, EVENTS);
    bool tomorrow = false;
    if (!n){ n = day_Events(s, d, today + 1, now, items, EVENTS); tomorrow = n > 0; }
    if (!n){
        bool anySource = s.url[0][0] || s.url[1][0];
        for (int i = 0; i < TYPED; i++) if (s.typed[i].title[0]) anySource = true;
        mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, anySource ? "NO MORE EVENTS TODAY" : "NO EVENTS YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, anySource ? "" : "TYPE THEM OR GIVE THE EVENTS CALENDAR IN THE PHONE APP", C_BG);
        return;
    }
    const bool large = n <= 3;
    const int rowH = large ? (n == 1 ? 104 : n == 2 ? 52 : 35) : 14;
    const int rows = large ? n : (H - 13) / rowH;
    const int pages = (n + rows - 1) / rows;
    const int pg = ((page % pages) + pages) % pages;
    if (tomorrow){ mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD); mtb_Kit_Text(3, 2, Terminal6x8, C_GOLD, "TOMORROW", C_HEAD); if (now > 1700000000){ clock_Text(a, sizeof(a), now); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD); } }
    for (int k = 0; k < rows && pg * rows + k < n; k++){
        const Evd_Item_t& e = items[pg * rows + k];
        const bool on = e.start <= now && now < e.end;
        const int y = 13 + k * (rowH + 1);
        const uint16_t bg = on ? MTB_KIT_RGB(12, 34, 20) : C_ROW;
        mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 1), bg);
        if (on) mtb_Panel_Fill_Rect(0, (int16_t)y, 2, (int16_t)(y + rowH - 1), KIT_GREEN);
        clock_Text(a, sizeof(a), e.start); clock_Text(b, sizeof(b), e.end);
        char title[48], place[EVD_PLACE_LEN + 2];
        snprintf(title, sizeof(title), "%s", s.titles && e.title[0] ? e.title : "PRIVATE EVENT"); mtb_Kit_Upper(title);
        snprintf(place, sizeof(place), "%s", e.place); mtb_Kit_Upper(place);
        const uint8_t way = way_To(s, e.place);
        if (n == 1){                                                            // one event: the whole screen
            snprintf(t, sizeof(t), "%s-%s", a, b);
            if (on){ mtb_Panel_Fill_Rect(6, (int16_t)(y + 5), 28, (int16_t)(y + 13), KIT_GREEN); mtb_Kit_Text(8, (int16_t)(y + 6), Terminal6x8, BLACK, "NOW", KIT_GREEN); }
            mtb_Kit_Text(on ? 34 : 6, (int16_t)(y + 6), Terminal6x8, on ? KIT_GREEN : KIT_CYAN, t, bg);
            char lines[2][48];
            const uint8_t* f = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, title) <= W - 12 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
            const int ln = f == PT_Sans_Narrow_Bold28x34 ? 1 : mtb_Kit_Wrap(title, f, W - 12, &lines[0][0], sizeof(lines[0]), 2);
            if (ln == 1 && f == PT_Sans_Narrow_Bold28x34) mtb_Kit_Text(6, (int16_t)(y + 20), f, KIT_TEXT, title, bg);
            else for (int i = 0; i < ln; i++) mtb_Kit_Text(6, (int16_t)(y + 20 + i * 24), f, KIT_TEXT, lines[i], bg);
            if (place[0]){ mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, place, W - 52); mtb_Kit_Text(6, (int16_t)(y + rowH - 18), Terminal8x12, C_GOLD, fit, bg); }
            arrow(W - 24, y + rowH - 16, 26, way, C_GOLD);
            continue;
        }
        if (large){
            const int mid = y + rowH / 2;
            snprintf(t, sizeof(t), "%s-%s", a, b);
            const int textTop = mid - 17;
            if (on){ mtb_Panel_Fill_Rect(6, (int16_t)(textTop - 1), 28, (int16_t)(textTop + 7), KIT_GREEN); mtb_Kit_Text(8, (int16_t)textTop, Terminal6x8, BLACK, "NOW", KIT_GREEN); }
            mtb_Kit_Text(on ? 34 : 6, (int16_t)textTop, Terminal6x8, on ? KIT_GREEN : KIT_CYAN, t, bg);
            const int right = way ? W - 34 : W - 6;
            mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, title, right - 10);
            mtb_Kit_Text(6, (int16_t)(textTop + 10), PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, bg);
            if (place[0]){ mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, place, 120); mtb_Kit_Text_Right((int16_t)right, (int16_t)textTop, Terminal6x8, C_GOLD, fit, bg); }
            arrow(W - 18, mid, rowH >= 50 ? 24 : 18, way, C_GOLD);
        } else {
            snprintf(t, sizeof(t), "%s", a);
            mtb_Kit_Text(5, (int16_t)(y + 4), Terminal4x6, on ? KIT_GREEN : KIT_CYAN, on ? "NOW" : t, bg);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, title, 138);
            mtb_Kit_Text(38, (int16_t)(y + 3), Terminal6x8, KIT_TEXT, fit, bg);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, place, 54);
            mtb_Kit_Text_Right(W - 18, (int16_t)(y + 4), Terminal4x6, C_GOLD, fit, bg);
            arrow(W - 9, y + rowH / 2, 10, way, C_GOLD);
        }
    }
    if (pages > 1){
        for (int i = 0; i < pages; i++) mtb_Panel_Fill_Rect((int16_t)(W / 2 - pages * 4 + i * 8), 125, (int16_t)(W / 2 - pages * 4 + i * 8 + 4), 126, i == pg ? KIT_TEXT : KIT_DIM);
    }
    (void)ms;
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void eventsDirectory_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Url, cmd_Event, cmd_Way, cmd_Venue, cmd_Minutes, cmd_Read, cmd_Titles);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "evdNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Evd_Shared_t* d = (Evd_Shared_t*)heap_caps_calloc(1, sizeof(Evd_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, pageAt = millis();
    int page = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){ turns -= t; page += t; pageAt = ms; lastDraw = 0; }
        if (ms - pageAt >= PAGE_MS){ pageAt = ms; page++; lastDraw = 0; }        // pages by itself
        if (clickRequested){ clickRequested = false; sh.readNow = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; push = true; lastDraw = 0; }
        portEXIT_CRITICAL(&lock);
        if (ms - lastDraw >= 1000){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw_Sign(save, *d, page, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push || ms - pushedMs >= 60000) && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
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
static void evd_Values(JsonObject values){
    Evd_Save_t* s = (Evd_Save_t*)heap_caps_malloc(sizeof(Evd_Save_t), MALLOC_CAP_SPIRAM);
    Evd_Shared_t* d = (Evd_Shared_t*)heap_caps_malloc(sizeof(Evd_Shared_t), MALLOC_CAP_SPIRAM);
    Evd_Item_t* items = (Evd_Item_t*)heap_caps_malloc(sizeof(Evd_Item_t) * EVENTS, MALLOC_CAP_SPIRAM);
    if (!s || !d || !items){ heap_caps_free(s); heap_caps_free(d); heap_caps_free(items); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    char t[128], key[8];
    static const char* const WORDS[] = {"", "left", "right", "ahead", "back", "upstairs", "downstairs"};
    const int32_t off = local_Offset();
    for (int i = 0; i < TYPED; i++){
        snprintf(key, sizeof(key), "event%d", i);
        const Evd_Typed_t& e = s->typed[i];
        if (!e.title[0]){ values[key] = ""; continue; }
        int64_t z = e.day; int y, m, dd;
        { const int64_t z0 = z + 719468, era = (z0 >= 0 ? z0 : z0 - 146096) / 146097, doe = z0 - era * 146097;
          const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
          dd = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); y = (int)(yoe + era * 400 + (m <= 2)); }
        snprintf(t, sizeof(t), "%04d-%02d-%02d %02d:%02d-%02d:%02d %s%s%s", y, m, dd, e.from / 60, e.from % 60, e.to / 60, e.to % 60, e.title, e.place[0] ? " @ " : "", e.place);
        values[key] = t;
    }
    for (int i = 0; i < WAYS; i++){
        snprintf(key, sizeof(key), "dir%d", i);
        if (s->way[i].place[0]){ snprintf(t, sizeof(t), "%s %s", s->way[i].place, WORDS[s->way[i].way]); values[key] = t; } else values[key] = "";
    }
    values["url0_set"] = s->url[0][0] ? 1 : 0;
    values["url1_set"] = s->url[1][0] ? 1 : 0;
    values["venue"] = s->venue;
    values["minutes"] = s->minutes;
    values["titles"] = s->titles;
    JsonArray rows = values["today"].to<JsonArray>();
    const int64_t now = (int64_t)time(nullptr);
    const int n = day_Events(*s, *d, local_Day(now), now, items, EVENTS);
    for (int i = 0; i < n; i++){
        JsonObject r = rows.add<JsonObject>();
        char a[8], b[8];
        const int64_t la = items[i].start + off, lb = items[i].end + off;
        snprintf(a, sizeof(a), "%02d:%02d", (int)((la / 60) % 1440 / 60), (int)((la / 60) % 60));
        snprintf(b, sizeof(b), "%02d:%02d", (int)((lb / 60) % 1440 / 60), (int)((lb / 60) % 60));
        snprintf(t, sizeof(t), "%s-%s %s", a, b, items[i].title);
        r["label"] = t;
        snprintf(t, sizeof(t), "%s%s", items[i].place[0] ? items[i].place : "No room given", items[i].start <= now && now < items[i].end ? ", on now" : "");
        r["value"] = t;
        r["state"] = items[i].start <= now && now < items[i].end ? "ok" : "info";
    }
    for (int k = 0; k < CALS; k++){
        if (!s->url[k][0] || !appOpen || d->fetch[k] == F_OK || d->fetch[k] == F_NONE) continue;
        JsonObject r = rows.add<JsonObject>();
        snprintf(t, sizeof(t), "Calendar %d", k + 1);
        r["label"] = t;
        if (d->fetch[k] == F_NOT_ALLOWED) r["value"] = "Allow its site when the phone app asks";
        else if (d->fetch[k] == F_NOT_ICS) r["value"] = "That link isn't an iCal calendar";
        else if (d->fetch[k] == F_HTTP){ snprintf(t, sizeof(t), "It answered %d: check the link", d->code[k]); r["value"] = t; }
        else r["value"] = "It didn't answer";
        r["state"] = "error";
    }
    heap_caps_free(s); heap_caps_free(d); heap_caps_free(items);
}
