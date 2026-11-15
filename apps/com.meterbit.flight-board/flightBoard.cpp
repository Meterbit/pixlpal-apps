/**
 * Flight Board: an airport's departures or arrivals as a split-flap board. Rows of time, flight, destination (or
 * origin), gate and status, in amber on black; when a page turns or the board updates, each letter flips through the
 * alphabet before it settles, left to right. Statuses in colour: ON TIME, DELAYED 15:40, BOARDING, GATE CLOSED,
 * DEPARTED, LANDED, CANCELLED.
 *
 * AeroDataBox's FIDS (GET /flights/airports/{icao|iata}/{code}, the owner's RapidAPI key) read by a worker task every
 * so often (each call costs 2 of the plan's units). A busy airport's answer can pass 64 KB: then the board shows fewer
 * hours ahead. Knob: turn to page, click to switch between departures and arrivals. NVS "fbdSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "adb_Common.h"
#include "fbd_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define FLIGHTS     40
#define KEY_LEN     72
#define NVS_KEY     "fbdSave"
#define SAVE_MAGIC  0x46424401u
#define UA          "Pixlpal-FlightBoard/1.0"
#define ROWS        8
#define PAGE_MS     9000
#define FLIP_MS     900

EXT_RAM_BSS_ATTR TaskHandle_t flightBoard_Task_H = NULL;
static void flightBoard_App_Task(void* dApplication);
static void fbd_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* flightBoard_App;
Mtb_Applications* flightBoard_App_GetInstance(){
    if (!flightBoard_App){
        flightBoard_App = new Mtb_Applications_FullScreen(flightBoard_App_Task, &flightBoard_Task_H, "Flight Board", {0,0}, 8192);
        flightBoard_App->mtb_App_Set_Mobile_UI(FBD_UI_MANIFEST, FBD_UI_API, nullptr, fbd_Values);
    }
    return flightBoard_App;
}
MTB_REGISTER_APP(flightBoard_App, 0, 0)       // the catalogue gives the app its number

struct Fbd_Save_t {
    uint32_t magic;
    char airport[5];
    uint8_t dir;                        // 0 departures, 1 arrivals
    char key[KEY_LEN];
    uint16_t minutes;
    uint8_t hours;
};

enum : uint8_t { S_UNKNOWN = 0, S_EXPECTED, S_ENROUTE, S_CHECKIN, S_BOARDING, S_GATECLOSED, S_DEPARTED, S_DELAYED, S_APPROACHING,
                 S_ARRIVED, S_CANCELED, S_DIVERTED };

struct Fbd_Flight_t {
    int64_t sched, revised;             // Unix seconds; revised 0 none
    int16_t schedLocal, revisedLocal;   // the airport's local clock, minutes
    char number[10];
    char place[24];                     // the other airport: its town, or name
    char gate[6];
    uint8_t status;
};

enum : uint8_t { F_NONE = 0, F_OK, F_FAIL };

struct Fbd_Shared_t {
    Fbd_Flight_t f[FLIGHTS];
    uint8_t n;
    uint8_t fetch;
    int code;
    bool cut;                           // the answer passed 64 KB
    uint8_t hoursShown;
    char airportName[32];
    uint32_t fetchedAt, version, callsToday, callsDay;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Fbd_Save_t save;
static EXT_RAM_BSS_ATTR Fbd_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Fbd_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 240; s.hours = 3; }
    if (s.minutes < 15 || s.minutes > 720) s.minutes = 240;
    if (s.hours < 1 || s.hours > 6) s.hours = 3;
    if (s.dir > 1) s.dir = 0;
    s.airport[4] = '\0'; s.key[KEY_LEN - 1] = '\0';
}

static uint8_t status_Of(const char* s){
    static const char* const N[] = {"Unknown", "Expected", "EnRoute", "CheckIn", "Boarding", "GateClosed", "Departed", "Delayed", "Approaching", "Arrived", "Canceled", "Diverted"};
    if (!s) return S_UNKNOWN;
    if (!strcmp(s, "CanceledUncertain")) return S_CANCELED;
    for (int i = 0; i < 12; i++) if (!strcmp(s, N[i])) return (uint8_t)i;
    return S_UNKNOWN;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Airport(JsonDocument& c){
    const char* v = c["airport"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    if (n != 3 && n != 4){ mtb_Ble_App_Cmd_Reject(); return; }
    char code[5] = "";
    for (size_t k = 0; k < n; k++){ if (!isalnum((unsigned char)v[k])){ mtb_Ble_App_Cmd_Reject(); return; } code[k] = (char)toupper((unsigned char)v[k]); }
    portENTER_CRITICAL(&lock); snprintf(save.airport, 5, "%s", code); sh.n = 0; sh.fetch = F_NONE; sh.version++; portEXIT_CRITICAL(&lock);
    sh.readNow = true; phoneChanged = true;
}
static void cmd_Dir(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["dir"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } portENTER_CRITICAL(&lock); save.dir = (uint8_t)v; sh.n = 0; sh.fetch = F_NONE; sh.version++; portEXIT_CRITICAL(&lock); sh.readNow = true; phoneChanged = true; }
static void cmd_Key(JsonDocument& c){
    const char* v = c["key"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    sh.readNow = true; phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 15, 720, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint16_t)v; phoneChanged = true; }
static void cmd_Hours(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["hours"], 1, 6, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.hours = (uint8_t)v; phoneChanged = true; }
static void cmd_Now(JsonDocument&){ if (!appOpen || !save.airport[0] || !save.key[0]){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static EXT_RAM_BSS_ATTR Fbd_Flight_t got[FLIGHTS];

/** One update: the flights from 30 minutes ago to @p hours ahead. The HTTP status; @p cut if the answer passed 64 KB. */
static int fetch_Board(const Fbd_Save_t& s, int hours, uint8_t& n, bool& cut, char* airportName, size_t nameSize){
    char path[200];
    snprintf(path, sizeof(path), "/flights/airports/%s/%s?offsetMinutes=-30&durationMinutes=%d&withLeg=false&direction=%s"
             "&withCancelled=true&withCodeshared=false&withCargo=false&withPrivate=false&withLocation=false",
             strlen(s.airport) == 4 ? "icao" : "iata", s.airport, hours * 60, s.dir ? "Arrival" : "Departure");
    Mtb_Kit_Http_t r = {};
    char* body = adb_Get(path, s.key, 65536, r, UA);
    n = 0;
    cut = r.truncated;
    if (r.code == 200 && body && !cut){
        JsonDocument filter, doc;
        JsonObject f = filter[s.dir ? "arrivals" : "departures"][0].to<JsonObject>();
        f["number"] = true; f["status"] = true;
        JsonObject m = f["movement"].to<JsonObject>();
        m["airport"]["name"] = true; m["airport"]["municipalityName"] = true; m["airport"]["iata"] = true;
        m["scheduledTime"]["utc"] = true; m["scheduledTime"]["local"] = true;
        m["revisedTime"]["utc"] = true; m["revisedTime"]["local"] = true;
        m["gate"] = true;
        Adb_Reader_t rd = {body, body + r.length};
        if (deserializeJson(doc, rd, DeserializationOption::Filter(filter)) == DeserializationError::Ok){
            for (JsonObjectConst x : doc[s.dir ? "arrivals" : "departures"].as<JsonArrayConst>()){
                if (n == FLIGHTS) break;
                Fbd_Flight_t& g = got[n];
                memset(&g, 0, sizeof(g));
                JsonObjectConst mv = x["movement"];
                const Adb_Time_t st = adb_Time(mv["scheduledTime"]["local"] | (const char*)nullptr);
                const Adb_Time_t su = adb_Time(mv["scheduledTime"]["utc"] | (const char*)nullptr);
                if (!st.ok) continue;
                g.sched = su.ok ? su.utc : st.utc;
                g.schedLocal = (int16_t)st.localMinute;
                const Adb_Time_t rt = adb_Time(mv["revisedTime"]["local"] | (const char*)nullptr);
                const Adb_Time_t ru = adb_Time(mv["revisedTime"]["utc"] | (const char*)nullptr);
                if (rt.ok){ g.revised = ru.ok ? ru.utc : rt.utc; g.revisedLocal = (int16_t)rt.localMinute; } else g.revisedLocal = -1;
                char tmp[48];
                mtb_Kit_To_Ascii(tmp, sizeof(tmp), x["number"] | ""); snprintf(g.number, sizeof(g.number), "%s", tmp);
                const char* town = mv["airport"]["municipalityName"] | (const char*)nullptr;
                mtb_Kit_To_Ascii(tmp, sizeof(tmp), town ? town : (mv["airport"]["name"] | (mv["airport"]["iata"] | "")));
                snprintf(g.place, sizeof(g.place), "%s", tmp);
                mtb_Kit_To_Ascii(tmp, sizeof(tmp), mv["gate"] | ""); snprintf(g.gate, sizeof(g.gate), "%s", tmp);
                g.status = status_Of(x["status"] | (const char*)nullptr);
                n++;
            }
            for (int i = 1; i < n; i++) for (int j = i; j > 0 && got[j].sched < got[j - 1].sched; j--){ const Fbd_Flight_t t = got[j]; got[j] = got[j - 1]; got[j - 1] = t; }
        }
    }
    heap_caps_free(body);
    (void)airportName; (void)nameSize;
    return r.code;
}

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        Fbd_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRead = millis() + s.minutes * 60000u;
        if (!s.airport[0] || !s.key[0]) continue;
        // A busy airport: fewer hours until the answer fits (each try is a paid call, so at most three).
        int hours = s.hours;
        uint8_t n = 0;
        bool cut = false;
        int code = 0;
        for (int tries = 0; tries < 3 && !sh.stop; tries++){
            code = fetch_Board(s, hours, n, cut, nullptr, 0);
            portENTER_CRITICAL(&lock);
            const uint32_t day = (uint32_t)(time(nullptr) / 86400);
            if (day != sh.callsDay){ sh.callsDay = day; sh.callsToday = 0; }
            sh.callsToday++;
            portEXIT_CRITICAL(&lock);
            if (!cut || hours == 1) break;
            hours = hours > 2 ? hours / 2 : 1;
        }
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.airport, save.airport) && s.dir == save.dir){
            sh.code = code;
            sh.cut = cut;
            if (code == 200 && !cut){ memcpy(sh.f, got, sizeof(got[0]) * n); sh.n = n; sh.fetch = F_OK; sh.fetchedAt = (uint32_t)time(nullptr); sh.hoursShown = (uint8_t)hours; }
            else if (code == 204){ sh.n = 0; sh.fetch = F_OK; sh.fetchedAt = (uint32_t)time(nullptr); sh.hoursShown = (uint8_t)hours; }
            else sh.fetch = F_FAIL;
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
        if (code != 200 && code != 204 && code != 401 && code != 403 && code != 429) nextRead = millis() + 15 * 60000u;   // try again sooner
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = BLACK, C_AMBER = MTB_KIT_RGB(255, 180, 40), C_HEAD = MTB_KIT_RGB(30, 26, 10), C_ROW2 = MTB_KIT_RGB(10, 10, 14);

static void hm(char* t, size_t n, int minute){
    if (minute < 0){ t[0] = '\0'; return; }
    snprintf(t, n, "%02d:%02d", minute / 60 % 24, minute % 60);
}

static void status_Text(const Fbd_Flight_t& f, bool arrivals, int64_t now, char* t, size_t n, uint16_t& c){
    char a[8];
    const bool late = f.revised && f.revised - f.sched >= 5 * 60;
    hm(a, sizeof(a), f.revisedLocal);
    c = C_AMBER;
    switch (f.status){
        case S_CANCELED: snprintf(t, n, "CANCELLED"); c = KIT_RED; return;
        case S_DIVERTED: snprintf(t, n, "DIVERTED"); c = KIT_RED; return;
        case S_BOARDING: snprintf(t, n, "BOARDING"); c = KIT_GREEN; return;
        case S_GATECLOSED: snprintf(t, n, "GATE CLOSED"); c = KIT_RED; return;
        case S_CHECKIN: snprintf(t, n, "CHECK-IN"); c = KIT_CYAN; return;
        case S_DEPARTED: case S_ENROUTE: if (arrivals){ snprintf(t, n, late ? "EXP %s" : "ON TIME", a); c = late ? KIT_GOLD : KIT_GREEN; } else { snprintf(t, n, a[0] ? "DEP %s" : "DEPARTED", a); c = KIT_DIM; } return;
        case S_APPROACHING: snprintf(t, n, "LANDING"); c = KIT_GREEN; return;
        case S_ARRIVED: snprintf(t, n, a[0] ? "ARR %s" : "LANDED", a); c = KIT_DIM; return;
        case S_DELAYED: snprintf(t, n, a[0] ? "DEL %s" : "DELAYED", a); c = KIT_GOLD; return;
        default:
            if (late){ snprintf(t, n, "DEL %s", a); c = KIT_GOLD; }
            else if (f.sched < now - 600 && !arrivals){ snprintf(t, n, "DEPARTED"); c = KIT_DIM; }
            else { snprintf(t, n, "ON TIME"); c = KIT_GREEN; }
            return;
    }
}

/** A cell of the board: its letters flip through the alphabet until each settles (later letters later). */
static void flap_Text(int x, int y, const char* text, uint16_t color, uint16_t bg, uint32_t sinceFlip, int cell){
    static const char FLAP[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char one[2] = {0, 0};
    for (int i = 0; text[i]; i++){
        const uint32_t settle = (uint32_t)(cell * 60 + i * 35 + 120);
        char ch = text[i];
        if (sinceFlip < settle && ch != ' '){
            const uint32_t k = (sinceFlip / 45 + (uint32_t)i * 7 + (uint32_t)cell * 13) % (sizeof(FLAP) - 1);
            ch = FLAP[k];
        }
        one[0] = ch;
        mtb_Kit_Text((int16_t)(x + i * 6), (int16_t)y, Terminal6x8, color, one, bg);
    }
}

static void draw(const Fbd_Save_t& s, const Fbd_Shared_t& d, int page, uint32_t sinceFlip){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48], a[16];
    const int64_t now = (int64_t)time(nullptr);
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s %s", s.dir ? "ARRIVALS" : "DEPARTURES", s.airport);
    mtb_Kit_Text(3, 2, Terminal6x8, C_AMBER, t, C_HEAD);
    pxl_time_t pt;
    if (pxl_time(&pt)){ hm(a, sizeof(a), pt.hour * 60 + pt.minute); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD); }
    if (d.fetchedAt && now >= d.fetchedAt){ char ago[16]; mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - d.fetchedAt)); mtb_Kit_Upper(ago); mtb_Kit_Text_Right(W - 40, 3, Terminal4x6, KIT_DIM, ago, C_HEAD); }

    if (!s.airport[0] || !s.key[0] || d.fetch != F_OK){
        const char* l1 = "FLIGHT BOARD";
        char l2[64] = "SET THE AIRPORT AND YOUR RAPIDAPI KEY IN THE PHONE APP";
        if (s.airport[0] && s.key[0]){
            if (d.fetch == F_NONE){ l1 = "UPDATING THE BOARD"; l2[0] = '\0'; }
            else { l1 = d.cut ? "TOO MANY FLIGHTS" : "THE BOARD COULDN'T UPDATE"; if (d.cut) snprintf(l2, sizeof(l2), "CHOOSE FEWER HOURS IN THE PHONE APP"); else adb_Why(d.code, l2, sizeof(l2)); }
        }
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, C_AMBER, l1, C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, l2, C_BG);
        return;
    }
    // The flights still to show: departures until 10 minutes after they left, arrivals until 20 minutes after landing.
    int list[FLIGHTS], n = 0;
    for (int i = 0; i < d.n; i++){
        const Fbd_Flight_t& f = d.f[i];
        const int64_t when = f.revised ? f.revised : f.sched;
        const int64_t keep = s.dir ? 20 * 60 : 10 * 60;
        if ((f.status == S_DEPARTED || f.status == S_ENROUTE || f.status == S_ARRIVED) && when < now - keep && !(s.dir && f.status == S_ENROUTE)) continue;
        list[n++] = i;
    }
    // Column headings.
    mtb_Kit_Text(3, 13, Terminal4x6, KIT_DIM, "TIME", C_BG);
    mtb_Kit_Text(36, 13, Terminal4x6, KIT_DIM, "FLIGHT", C_BG);
    mtb_Kit_Text(82, 13, Terminal4x6, KIT_DIM, s.dir ? "FROM" : "TO", C_BG);
    mtb_Kit_Text(162, 13, Terminal4x6, KIT_DIM, s.dir ? "" : "GATE", C_BG);
    mtb_Kit_Text_Right(W - 3, 13, Terminal4x6, KIT_DIM, "STATUS", C_BG);
    if (!n){ mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_LABEL, s.dir ? "NO MORE ARRIVALS IN THIS WINDOW" : "NO MORE DEPARTURES IN THIS WINDOW", C_BG); return; }
    const int pages = (n + ROWS - 1) / ROWS;
    const int pg = ((page % pages) + pages) % pages;
    for (int k = 0; k < ROWS && pg * ROWS + k < n; k++){
        const Fbd_Flight_t& f = d.f[list[pg * ROWS + k]];
        const int y = 21 + k * 13;
        const uint16_t bg = k % 2 ? C_ROW2 : C_BG;
        if (bg != C_BG) mtb_Panel_Fill_Rect(0, (int16_t)(y - 2), W - 1, (int16_t)(y + 9), bg);
        char tm[8], place[24], st[24];
        hm(tm, sizeof(tm), f.schedLocal);
        snprintf(place, sizeof(place), "%s", f.place); mtb_Kit_Upper(place);
        char fit[24]; mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, place, 76);
        uint16_t sc;
        status_Text(f, s.dir, now, st, sizeof(st), sc);
        char num[12]; snprintf(num, sizeof(num), "%s", f.number); mtb_Kit_Upper(num); num[7] = num[7] ? num[7] : 0;
        for (char* q = num; *q; q++) if (*q == ' ') { memmove(q, q + 1, strlen(q)); break; }            // "BA 75" -> "BA75": fits
        const uint32_t row = sinceFlip > (uint32_t)(k * 50) ? sinceFlip - k * 50 : 0;
        flap_Text(3, y, tm, C_AMBER, bg, row, 0);
        flap_Text(36, y, num, C_AMBER, bg, row, 1);
        flap_Text(82, y, fit, C_AMBER, bg, row, 2);
        if (!s.dir){ char g[5]; snprintf(g, sizeof(g), "%s", f.gate); mtb_Kit_Upper(g); flap_Text(162, y, g, C_AMBER, bg, row, 3); }
        const int sw = (int)strlen(st) * 6;
        flap_Text(W - 3 - sw, y, st, sc, bg, row, 4);
    }
    if (pages > 1){ snprintf(t, sizeof(t), "%d/%d", pg + 1, pages); mtb_Kit_Text_Right(W - 3, 122, Terminal4x6, KIT_DIM, t, C_BG); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void flightBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Airport, cmd_Dir, cmd_Key, cmd_Minutes, cmd_Hours, cmd_Now);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "fbdNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Fbd_Shared_t* d = (Fbd_Shared_t*)heap_caps_calloc(1, sizeof(Fbd_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, pageAt = millis(), flipAt = millis();
    int page = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){ turns -= t; page += t; pageAt = flipAt = ms; }
        if (clickRequested){
            clickRequested = false;
            portENTER_CRITICAL(&lock); save.dir = !save.dir; sh.n = 0; sh.fetch = F_NONE; sh.version++; portEXIT_CRITICAL(&lock);
            sh.readNow = true; saveNow = push = true; page = 0; flipAt = ms;
        }
        if (ms - pageAt >= PAGE_MS){ pageAt = flipAt = ms; page++; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; push = true; flipAt = ms; }
        portEXIT_CRITICAL(&lock);
        const uint32_t sinceFlip = ms - flipAt;
        if (ms - lastDraw >= (sinceFlip < FLIP_MS + 400 ? 45u : 1000u)){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(save, *d, page, sinceFlip);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 600 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void fbd_Values(JsonObject values){
    Fbd_Save_t s;
    Fbd_Shared_t* d = (Fbd_Shared_t*)heap_caps_malloc(sizeof(Fbd_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(s); memset(d, 0, sizeof(*d)); }
    values["airport"] = s.airport;
    values["dir"] = s.dir;
    values["key_set"] = s.key[0] ? 1 : 0;
    values["minutes"] = s.minutes;
    values["hours"] = s.hours;
    JsonArray rows = values["board"].to<JsonArray>();
    char t[120];
    JsonObject r = rows.add<JsonObject>();
    snprintf(t, sizeof(t), "%s %s", s.dir ? "Arrivals" : "Departures", s.airport[0] ? s.airport : "(no airport)");
    r["label"] = t;
    if (!s.key[0]){ r["value"] = "Give your RapidAPI key below"; r["state"] = "warn"; }
    else if (!s.airport[0]){ r["value"] = "Choose the airport below"; r["state"] = "warn"; }
    else if (!appOpen){ r["value"] = "Updated while Flight Board is open"; r["state"] = "off"; }
    else if (d->fetch == F_OK){
        snprintf(t, sizeof(t), "%u flights in the next %u h%s", (unsigned)d->n, (unsigned)d->hoursShown, d->hoursShown < s.hours ? " (fewer hours: a busy airport)" : "");
        r["value"] = t; r["state"] = "ok";
        if (d->fetchedAt){ values["fetchedAt"] = d->fetchedAt; r["ago"] = "fetchedAt"; }
    } else if (d->fetch == F_FAIL){ char w[64]; adb_Why(d->code, w, sizeof(w)); r["value"] = w; r["state"] = "error"; }
    else { r["value"] = "Updating"; r["state"] = "info"; }
    JsonObject q = rows.add<JsonObject>();
    q["label"] = "Calls a month";
    const unsigned calls = (unsigned)(30u * 24u * 60u / s.minutes);
    snprintf(t, sizeof(t), "About %u at this setting (%u units; the free plan has 400)", calls, calls * 2);
    q["value"] = t;
    q["state"] = calls * 2 <= 400 ? "ok" : "warn";
    heap_caps_free(d);
}
