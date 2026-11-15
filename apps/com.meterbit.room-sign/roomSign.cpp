/**
 * Room Sign: a meeting-room door sign from the room's calendar (an iCal link: Google, Outlook, Exchange, any). Green
 * FREE or red IN USE across the middle, with until when; amber when the meeting ends within five minutes or the next
 * starts within five; what is on now with a bar of the time gone, what is next, and the rest of the day below.
 *
 * A worker task reads the calendar every few minutes (rms_Ics.cpp reads it). Knob: turn to page through the coming
 * meetings, click to read the calendar now. NVS "rmsSave": the room's name, the link and the choices.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "rms_Ics.h"
#include "rms_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W           256
#define H           128
#define ROOM_LEN    32
#define URL_LEN     320
#define EVENTS      40
#define BODY_MAX    65536
#define NVS_KEY     "rmsSave"
#define SAVE_MAGIC  0x524D5301u
#define UA          "Pixlpal-RoomSign/1.0"
#define SOON_S      300

EXT_RAM_BSS_ATTR TaskHandle_t roomSign_Task_H = NULL;
static void roomSign_App_Task(void* dApplication);
static void rms_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* roomSign_App;
Mtb_Applications* roomSign_App_GetInstance(){
    if (!roomSign_App){
        roomSign_App = new Mtb_Applications_FullScreen(roomSign_App_Task, &roomSign_Task_H, "Room Sign", {0,0}, 8192);
        roomSign_App->mtb_App_Set_Mobile_UI(RMS_UI_MANIFEST, RMS_UI_API, nullptr, rms_Values);
    }
    return roomSign_App;
}
MTB_REGISTER_APP(roomSign_App, 0, 0)          // the catalogue gives the app its number

struct Rms_Save_t {
    uint32_t magic;
    char room[ROOM_LEN];
    char url[URL_LEN];
    uint8_t minutes, titles;
};

enum : uint8_t { F_NONE = 0, F_OK, F_NOT_ALLOWED, F_HTTP, F_NO_ANSWER, F_NOT_ICS };

struct Rms_Shared_t {
    Rms_Event_t ev[EVENTS];
    int n;
    uint8_t fetch;                      // F_...
    int code;                           // the HTTP status, for F_HTTP
    bool truncated;
    uint32_t fetchedAt;                 // Unix seconds of the last answer read
    uint32_t version;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Rms_Save_t save;
static EXT_RAM_BSS_ATTR Rms_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Rms_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 5; s.titles = 1; snprintf(s.room, sizeof(s.room), "Meeting Room"); }
    if (s.minutes < 1 || s.minutes > 30) s.minutes = 5;
    s.room[ROOM_LEN - 1] = s.url[URL_LEN - 1] = '\0';
}

static int32_t local_Offset(void){ pxl_time_t t; pxl_time(&t); return t.utc_offset_minutes * 60; }

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Room(JsonDocument& c){
    const char* v = c["room"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char room[ROOM_LEN];
    mtb_Kit_To_Ascii(room, sizeof(room), v);
    char* p = room;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = '\0';
    if (!n || strlen(v) > 60){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.room, sizeof(save.room), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Url(JsonDocument& c){
    const char* v = c["url"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    char url[URL_LEN];
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    if (n == 0) url[0] = '\0';
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
        for (const char* k = url; *k; k++) if ((unsigned char)*k <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
    }
    portENTER_CRITICAL(&lock);
    snprintf(save.url, sizeof(save.url), "%s", url);
    sh.n = 0; sh.fetch = F_NONE; sh.fetchedAt = 0; sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.readNow = true;
    phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.minutes = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Titles(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["titles"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.titles = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Read(JsonDocument&){
    if (!appOpen || !save.url[0]){ mtb_Ble_App_Cmd_Reject(); return; }
    sh.readNow = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static EXT_RAM_BSS_ATTR Rms_Event_t found[EVENTS];

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        Rms_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRead = millis() + s.minutes * 60000u;
        if (!s.url[0]) continue;
        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u){ nextRead = millis() + 5000; continue; }        // no time from the internet yet
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(s.url, nullptr, nullptr, 0, BODY_MAX, r, UA);
        if (sh.stop){ heap_caps_free(body); break; }
        uint8_t fetch = F_OK;
        int n = 0;
        Rms_Ics_Report_t rep = {};
        if (r.code == PXL_HTTP_NOT_ALLOWED) fetch = F_NOT_ALLOWED;
        else if (r.code <= 0) fetch = F_NO_ANSWER;
        else if (r.code < 200 || r.code >= 300) fetch = F_HTTP;
        else if (body){
            n = rms_Ics_Events(body, r.length, (int64_t)now - 12 * 3600, (int64_t)now + 8 * 86400, local_Offset(), found, EVENTS, &rep);
            if (!rep.calendar) fetch = F_NOT_ICS;
            for (int i = 0; i < n; i++){                                       // the panel's fonts are ASCII
                char a[RMS_TITLE_LEN];
                mtb_Kit_To_Ascii(a, sizeof(a), found[i].title);
                snprintf(found[i].title, sizeof(found[i].title), "%s", a);
            }
        }
        heap_caps_free(body);
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.url, save.url)){                                         // not changed meanwhile
            sh.fetch = fetch;
            sh.code = r.code;
            sh.truncated = r.truncated;
            if (fetch == F_OK){ memcpy(sh.ev, found, sizeof(found[0]) * n); sh.n = n; sh.fetchedAt = now; }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
        if (fetch != F_OK && fetch != F_NOT_ALLOWED) nextRead = millis() + (s.minutes < 2 ? 60000u : 120000u);   // try again sooner
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// The room now ---------------------------------------------------------------------------------------------------------------------
struct Rms_Now_t {
    int cur;                            // the meeting on now, -1 none
    int64_t busyUntil;                  // the end of back-to-back meetings from it
    int next;                           // the next meeting to start, -1 none
};

static Rms_Now_t room_Now(const Rms_Shared_t& d, int64_t now){
    Rms_Now_t r = {-1, 0, -1};
    for (int i = 0; i < d.n; i++){
        if (d.ev[i].start <= now && now < d.ev[i].end){ if (r.cur < 0 || d.ev[i].end > d.ev[r.cur].end) r.cur = i; }
        else if (d.ev[i].start > now && r.next < 0) r.next = i;
    }
    if (r.cur >= 0){
        r.busyUntil = d.ev[r.cur].end;
        for (bool grew = true; grew;){                                         // a meeting that starts as this ends goes on
            grew = false;
            for (int i = 0; i < d.n; i++) if (d.ev[i].start <= r.busyUntil + 60 && d.ev[i].end > r.busyUntil){ r.busyUntil = d.ev[i].end; grew = true; }
        }
        r.next = -1;
        for (int i = 0; i < d.n; i++) if (d.ev[i].start >= r.busyUntil){ r.next = i; break; }
    }
    return r;
}

static void clock_Text(char* t, size_t n, int64_t utc, bool withDay){
    struct tm lt;
    time_t x = (time_t)utc;
    localtime_r(&x, &lt);
    char hm[12];
    if (mtb_Clock_24h()) snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(hm, sizeof(hm), "%d:%02d%s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
    static const char* const D[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (withDay) snprintf(t, n, "%s %s", D[lt.tm_wday], hm); else snprintf(t, n, "%s", hm);
}

static int64_t local_Day(int64_t utc){ const int64_t l = utc + local_Offset(); return l >= 0 ? l / 86400 : (l - 86399) / 86400; }

static void span_Text(char* t, size_t n, int64_t seconds){
    const int64_t m = (seconds + 59) / 60;
    if (m < 60) snprintf(t, n, "%lld MIN", (long long)m);
    else if (m % 60 == 0) snprintf(t, n, "%lld H", (long long)(m / 60));
    else snprintf(t, n, "%lldH %02lldM", (long long)(m / 60), (long long)(m % 60));
}

static void title_Of(const Rms_Event_t& e, bool titles, char* t, size_t n){
    if (!titles || !e.title[0]) snprintf(t, n, "BUSY");
    else { snprintf(t, n, "%s", e.title); mtb_Kit_Upper(t); }
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 12);
static const uint16_t C_FREE = MTB_KIT_RGB(30, 190, 80), C_BUSY = MTB_KIT_RGB(220, 40, 40), C_SOON = MTB_KIT_RGB(255, 160, 0);

static void draw_Sign(const Rms_Save_t& s, const Rms_Shared_t& d, int page, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const int64_t now = (int64_t)time(nullptr);
    char t[64], fit[64], a[24], b[24];

    // The room's name, and the time.
    char room[ROOM_LEN + 2];
    snprintf(room, sizeof(room), "%s", s.room);
    mtb_Kit_Upper(room);
    clock_Text(a, sizeof(a), now, false);
    const int clockW = mtb_Kit_Text_Width(Terminal6x8, a) + 8;
    // The largest face the name fits: PT Sans Narrow 34 px, its bold at 22 px, or two lines.
    const int roomW = W - 8 - clockW;
    if (mtb_Kit_Text_Width(PT_Sans_Narrow38x34, room) <= roomW) mtb_Kit_Text(5, 1, PT_Sans_Narrow38x34, KIT_TEXT, room, C_BG);
    else if (mtb_Kit_Text_Width(PT_Sans_Narrow_Bold18x22, room) <= roomW) mtb_Kit_Text(5, 8, PT_Sans_Narrow_Bold18x22, KIT_TEXT, room, C_BG);
    else {
        char lines[2][ROOM_LEN + 2];
        const int n = mtb_Kit_Wrap(room, Terminal8x12, roomW, &lines[0][0], sizeof(lines[0]), 2);
        for (int i = 0; i < n; i++) mtb_Kit_Text(5, n == 1 ? 12 : 4 + i * 15, Terminal8x12, KIT_TEXT, lines[i], C_BG);
    }
    mtb_Kit_Text_Right(W - 4, 3, Terminal6x8, KIT_LABEL, a, C_BG);

    if (!s.url[0] || d.fetch == F_NONE || (d.fetch != F_OK && !d.fetchedAt)){
        const char* l1 = "NO CALENDAR YET";
        const char* l2 = "GIVE THE ROOM'S ICAL LINK IN THE PHONE APP";
        if (s.url[0] && d.fetch == F_NONE){ l1 = "READING THE CALENDAR"; l2 = ""; }
        else if (d.fetch == F_NOT_ALLOWED){ l1 = "CALENDAR NOT ALLOWED YET"; l2 = "ALLOW ITS SITE IN THE PHONE APP"; }
        else if (d.fetch == F_HTTP){ snprintf(t, sizeof(t), "THE CALENDAR ANSWERED %d", d.code); l1 = t; l2 = "CHECK THE LINK IN THE PHONE APP"; }
        else if (d.fetch == F_NO_ANSWER){ l1 = "THE CALENDAR DIDN'T ANSWER"; l2 = "TRYING AGAIN SOON"; }
        else if (d.fetch == F_NOT_ICS){ l1 = "THAT LINK ISN'T A CALENDAR"; l2 = "USE ITS ICAL (.ICS) ADDRESS"; }
        mtb_Panel_Fill_Rect(0, 40, W - 1, 71, MTB_KIT_RGB(30, 34, 46));
        mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, l1, MTB_KIT_RGB(30, 34, 46));
        mtb_Kit_Text_Centre(W / 2, 84, Terminal4x6, KIT_LABEL, l2, C_BG);
        return;
    }

    const Rms_Now_t r = room_Now(d, now);
    const bool busy = r.cur >= 0;
    const bool soon = busy ? r.busyUntil - now <= SOON_S : (r.next >= 0 && d.ev[r.next].start - now <= SOON_S);
    const uint16_t band = soon ? C_SOON : busy ? C_BUSY : C_FREE;
    const char* word = busy ? (soon ? "ENDING SOON" : "IN USE") : (soon ? "STARTING SOON" : "FREE");
    if (busy){
        if (d.ev[r.cur].allDay && local_Day(r.busyUntil - 1) > local_Day(now)) snprintf(t, sizeof(t), "ALL DAY");
        else { clock_Text(a, sizeof(a), r.busyUntil, local_Day(r.busyUntil) != local_Day(now)); snprintf(t, sizeof(t), "UNTIL %s", a); }
    } else if (r.next < 0) snprintf(t, sizeof(t), "ALL WEEK");
    else {
        const int64_t gap = d.ev[r.next].start - now;
        if (local_Day(d.ev[r.next].start) != local_Day(now)){
            clock_Text(a, sizeof(a), d.ev[r.next].start, true);
            snprintf(t, sizeof(t), "UNTIL %s", a);
        } else if (gap < 2 * 3600){ span_Text(a, sizeof(a), gap); snprintf(t, sizeof(t), soon ? "IN %s" : "FOR %s", a); }
        else { clock_Text(a, sizeof(a), d.ev[r.next].start, false); snprintf(t, sizeof(t), "UNTIL %s", a); }
    }
    mtb_Panel_Fill_Rect(0, 38, W - 1, 69, band);
    if (soon && (ms / 600) % 2) mtb_Panel_Fill_Rect(0, 38, W - 1, 39, KIT_TEXT);
    mtb_Kit_Text(7, 46, Terminal10x17, BLACK, word, band);
    mtb_Kit_Text_Right(W - 7, 49, Terminal8x12, BLACK, t, band);

    // Now, or next.
    int y = 74;
    if (busy){
        const Rms_Event_t& e = d.ev[r.cur];
        title_Of(e, s.titles, t, sizeof(t));
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - 10);
        mtb_Kit_Text(5, y, Terminal8x12, KIT_TEXT, fit, C_BG);
        const float f = e.end > e.start ? (float)(now - e.start) / (float)(e.end - e.start) : 1.0f;
        mtb_Kit_Bar(5, y + 14, W - 10, 3, f, band, KIT_LINE);
        y += 22;
    } else if (r.next >= 0){
        const Rms_Event_t& e = d.ev[r.next];
        clock_Text(a, sizeof(a), e.start, local_Day(e.start) != local_Day(now));
        title_Of(e, s.titles, t, sizeof(t));
        mtb_Kit_Text(5, y + 3, Terminal6x8, KIT_LABEL, "NEXT", C_BG);
        const int x = 5 + mtb_Kit_Text_Width(Terminal6x8, "NEXT") + 6;
        mtb_Kit_Text(x, y, Terminal8x12, KIT_CYAN, a, C_BG);
        const int x2 = x + mtb_Kit_Text_Width(Terminal8x12, a) + 8;
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - x2 - 5);
        mtb_Kit_Text(x2, y, Terminal8x12, KIT_TEXT, fit, C_BG);
        y += 20;
    } else {
        mtb_Kit_Text(5, y + 2, Terminal6x8, KIT_LABEL, "NOTHING BOOKED THIS WEEK", C_BG);
        y += 20;
    }

    // Later: the meetings after the one shown, from the knob's page.
    int list[EVENTS], ln = 0;
    const int shown = busy ? r.cur : r.next;
    for (int i = 0; i < d.n; i++) if (d.ev[i].end > now && i != shown && (busy ? d.ev[i].start >= now : true) && d.ev[i].start > (shown >= 0 ? d.ev[shown].start - 1 : now)) list[ln++] = i;
    const int rows = (H - y) / 11;
    if (ln && rows > 0){
        const int pages = (ln + rows - 1) / rows;
        const int pg = ((page % pages) + pages) % pages;
        for (int k = 0; k < rows && pg * rows + k < ln; k++){
            const Rms_Event_t& e = d.ev[list[pg * rows + k]];
            const bool otherDay = local_Day(e.start) != local_Day(now);
            clock_Text(a, sizeof(a), e.start, otherDay);
            clock_Text(b, sizeof(b), e.end, false);
            if (e.allDay) snprintf(a, sizeof(a), "%s", otherDay ? "" : "TODAY");
            if (e.allDay && otherDay){ clock_Text(a, sizeof(a), e.start, true); char* sp = strchr(a, ' '); if (sp) *sp = '\0'; }
            char when[32];
            if (e.allDay) snprintf(when, sizeof(when), "%s ALL DAY", a);
            else snprintf(when, sizeof(when), "%s-%s", a, b);
            title_Of(e, s.titles, t, sizeof(t));
            const int yy = y + k * 11;
            mtb_Kit_Text(5, yy, Terminal6x8, otherDay ? KIT_DIM : KIT_LABEL, when, C_BG);
            const int x = 5 + 104;
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - x - 5);
            mtb_Kit_Text(x, yy, Terminal6x8, otherDay ? KIT_LABEL : KIT_TEXT, fit, C_BG);
        }
        if (pages > 1){ snprintf(t, sizeof(t), "%d/%d", pg + 1, pages); mtb_Kit_Text_Right(W - 3, H - 7, Terminal4x6, KIT_DIM, t, C_BG); }
    }
    if (d.fetch != F_OK){                                                       // showing the last good read
        mtb_Panel_Fill_Rect(W - 6, 38, W - 1, 43, KIT_ORANGE);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void roomSign_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Room, cmd_Url, cmd_Minutes, cmd_Titles, cmd_Read);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "rmsNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Rms_Shared_t* d = (Rms_Shared_t*)heap_caps_calloc(1, sizeof(Rms_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0;
    int page = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; page += t; redraw = true; }
        if (clickRequested){ clickRequested = false; sh.readNow = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        if (redraw || ms - lastDraw >= 1000){
            redraw = false;
            lastDraw = ms;
            Rms_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw_Sign(s, *d, page, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
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
static void rms_Values(JsonObject values){
    Rms_Save_t* s = (Rms_Save_t*)heap_caps_malloc(sizeof(Rms_Save_t), MALLOC_CAP_SPIRAM);
    Rms_Shared_t* d = (Rms_Shared_t*)heap_caps_malloc(sizeof(Rms_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    values["room"] = s->room;
    values["url_set"] = s->url[0] ? 1 : 0;
    values["minutes"] = s->minutes;
    values["titles"] = s->titles;
    JsonArray rows = values["status"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = s->room;
    char t[96], a[24];
    const int64_t now = (int64_t)time(nullptr);
    if (!s->url[0]){ r["value"] = "No calendar link yet: give it below"; r["state"] = "warn"; }
    else if (!appOpen){ r["value"] = "Shown while Room Sign is open"; r["state"] = "off"; }
    else if (d->fetch == F_NOT_ALLOWED){ r["value"] = "Allow the calendar's site when the phone app asks"; r["state"] = "warn"; }
    else if (d->fetch == F_HTTP){ snprintf(t, sizeof(t), "The calendar answered %d: check the link", d->code); r["value"] = t; r["state"] = "error"; }
    else if (d->fetch == F_NO_ANSWER && !d->fetchedAt){ r["value"] = "The calendar didn't answer"; r["state"] = "error"; }
    else if (d->fetch == F_NOT_ICS){ r["value"] = "That link isn't an iCal calendar"; r["state"] = "error"; }
    else if (d->fetch == F_NONE){ r["value"] = "Reading the calendar"; r["state"] = "info"; }
    else {
        const Rms_Now_t n = room_Now(*d, now);
        if (n.cur >= 0){
            clock_Text(a, sizeof(a), n.busyUntil, local_Day(n.busyUntil) != local_Day(now));
            snprintf(t, sizeof(t), "In use until %s", a);
            r["state"] = "error";
        } else if (n.next >= 0){
            clock_Text(a, sizeof(a), d->ev[n.next].start, local_Day(d->ev[n.next].start) != local_Day(now));
            snprintf(t, sizeof(t), "Free until %s", a);
            r["state"] = "ok";
        } else { snprintf(t, sizeof(t), "Free, nothing booked this week"); r["state"] = "ok"; }
        r["value"] = t;
        JsonObject u = rows.add<JsonObject>();
        u["label"] = "Calendar";
        snprintf(t, sizeof(t), "%d meeting%s in the coming week%s", d->n, d->n == 1 ? "" : "s", d->truncated ? " (the calendar was cut at 64 KB)" : "");
        u["value"] = t;
        u["state"] = d->truncated ? "warn" : "ok";
        u["ago"] = "fetchedAt";
        values["fetchedAt"] = d->fetchedAt;
    }
    heap_caps_free(s);
    heap_caps_free(d);
}
