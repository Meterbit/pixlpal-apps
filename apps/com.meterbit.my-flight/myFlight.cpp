/**
 * My Flight: one flight followed from home or a hotel. The route in large airport codes (a plane moves along the line
 * between them while it flies), the departure and arrival times at each end (a delayed time struck through, the new one
 * beside it), the status in its colour with terminal and gate, and the one thing to know now: when to leave for the
 * airport, the countdown to boarding, the time to landing, or the baggage belt.
 *
 * AeroDataBox's flight status (GET /flights/number/{flight}/{date}, the owner's RapidAPI key) read by a worker task:
 * every 6 hours until the day before, hourly on the day, every 15 minutes from 3 hours before departure until it lands
 * (each check costs 2 of the plan's units). Knob: click to check now. NVS "myfSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "adb_Common.h"
#include "myf_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define KEY_LEN     72
#define NVS_KEY     "myfSave"
#define SAVE_MAGIC  0x4D594601u
#define UA          "Pixlpal-MyFlight/1.0"
#define BOARD_MIN   40                  // boarding usually starts this long before departure

EXT_RAM_BSS_ATTR TaskHandle_t myFlight_Task_H = NULL;
static void myFlight_App_Task(void* dApplication);
static void myf_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* myFlight_App;
Mtb_Applications* myFlight_App_GetInstance(){
    if (!myFlight_App){
        myFlight_App = new Mtb_Applications_FullScreen(myFlight_App_Task, &myFlight_Task_H, "My Flight", {0,0}, 8192);
        myFlight_App->mtb_App_Set_Mobile_UI(MYF_UI_MANIFEST, MYF_UI_API, nullptr, myf_Values);
    }
    return myFlight_App;
}
MTB_REGISTER_APP(myFlight_App, 0, 0)          // the catalogue gives the app its number

struct Myf_Save_t {
    uint32_t magic;
    char flight[10];                    // "BA75"
    int16_t year; int8_t month, day;    // the day it leaves (local); year 0: none
    char key[KEY_LEN];
    uint16_t travel, early;             // minutes
};

enum : uint8_t { S_UNKNOWN = 0, S_EXPECTED, S_ENROUTE, S_CHECKIN, S_BOARDING, S_GATECLOSED, S_DEPARTED, S_DELAYED, S_APPROACHING,
                 S_ARRIVED, S_CANCELED, S_DIVERTED };

struct Myf_End_t {
    char iata[5], town[24];
    int64_t sched, revised, actual;     // Unix seconds; 0 none (actual: the runway time)
    int16_t schedLocal, revisedLocal, actualLocal;     // the airport's local clock, minutes; -1 none
    char terminal[8], gate[8], desk[16], belt[8];
};

struct Myf_Info_t {
    bool have;
    char number[12], airline[28], aircraft[28];
    uint8_t status;
    Myf_End_t dep, arr;
};

enum : uint8_t { F_NONE = 0, F_OK, F_FAIL };

struct Myf_Shared_t {
    Myf_Info_t f;
    uint8_t fetch;
    int code;
    uint32_t fetchedAt, version, calls;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Myf_Save_t save;
static EXT_RAM_BSS_ATTR Myf_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Myf_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.travel = 60; s.early = 120; }
    if (s.travel < 15 || s.travel > 300) s.travel = 60;
    if (s.early < 30 || s.early > 240) s.early = 120;
    s.flight[sizeof(s.flight) - 1] = s.key[KEY_LEN - 1] = '\0';
}

static uint8_t status_Of(const char* s){
    static const char* const N[] = {"Unknown", "Expected", "EnRoute", "CheckIn", "Boarding", "GateClosed", "Departed", "Delayed", "Approaching", "Arrived", "Canceled", "Diverted"};
    if (!s) return S_UNKNOWN;
    if (!strcmp(s, "CanceledUncertain")) return S_CANCELED;
    for (int i = 0; i < 12; i++) if (!strcmp(s, N[i])) return (uint8_t)i;
    return S_UNKNOWN;
}

static int64_t dep_Time(const Myf_Info_t& f){ return f.dep.actual ? f.dep.actual : f.dep.revised ? f.dep.revised : f.dep.sched; }
static int64_t arr_Time(const Myf_Info_t& f){ return f.arr.actual ? f.arr.actual : f.arr.revised ? f.arr.revised : f.arr.sched; }

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Flight(JsonDocument& c){
    const char* v = c["flight"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char f[10]; size_t n = 0;
    for (; *v; v++){
        if (*v == ' ' || *v == '-') continue;
        if (!isalnum((unsigned char)*v) || n >= sizeof(f) - 1){ mtb_Ble_App_Cmd_Reject(); return; }
        f[n++] = (char)toupper((unsigned char)*v);
    }
    f[n] = '\0';
    if (n < 3 || !isdigit((unsigned char)f[n - 1])){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.flight, sizeof(save.flight), "%s", f); memset(&sh.f, 0, sizeof(sh.f)); sh.fetch = F_NONE; sh.version++; portEXIT_CRITICAL(&lock);
    sh.readNow = true; phoneChanged = true;
}
static void cmd_Date(JsonDocument& c){
    const char* v = c["date"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    pxl_time_t t; pxl_time(&t);
    int y = t.year, m = t.month, d = t.day;
    if (!strncasecmp(v, "tomorrow", 8)){
        const int64_t days = adb_Days(y, m, d) + 1;
        time_t x = (time_t)(days * 86400); struct tm g; gmtime_r(&x, &g); y = g.tm_year + 1900; m = g.tm_mon + 1; d = g.tm_mday;
    } else if (strncasecmp(v, "today", 5) && (sscanf(v, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31 || y < 2020)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.year = (int16_t)y; save.month = (int8_t)m; save.day = (int8_t)d; memset(&sh.f, 0, sizeof(sh.f)); sh.fetch = F_NONE; sh.version++; portEXIT_CRITICAL(&lock);
    sh.readNow = true; phoneChanged = true;
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["key"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    sh.readNow = true; phoneChanged = true;
}
static void cmd_Travel(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["travel"], 15, 300, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.travel = (uint16_t)v; phoneChanged = true; }
static void cmd_Early(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["early"], 30, 240, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.early = (uint16_t)v; phoneChanged = true; }
static void cmd_Now(JsonDocument&){ if (!appOpen || !save.flight[0] || !save.key[0]){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t){}

// The worker -------------------------------------------------------------------------------------------------------------------------
static void read_End(JsonObjectConst e, Myf_End_t& o){
    memset(&o, 0, sizeof(o));
    char tmp[64];
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), e["airport"]["iata"] | (e["airport"]["icao"] | "")); snprintf(o.iata, sizeof(o.iata), "%s", tmp);
    const char* town = e["airport"]["municipalityName"] | (const char*)nullptr;
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), town ? town : (e["airport"]["shortName"] | (e["airport"]["name"] | ""))); snprintf(o.town, sizeof(o.town), "%s", tmp);
    auto when = [&](const char* key, int64_t& utc, int16_t& local){
        const Adb_Time_t u = adb_Time(e[key]["utc"] | (const char*)nullptr), l = adb_Time(e[key]["local"] | (const char*)nullptr);
        utc = u.ok ? u.utc : (l.ok ? l.utc : 0);
        local = l.ok ? (int16_t)l.localMinute : -1;
    };
    when("scheduledTime", o.sched, o.schedLocal);
    when("revisedTime", o.revised, o.revisedLocal);
    if (!o.revised){ when("predictedTime", o.revised, o.revisedLocal); }
    when("runwayTime", o.actual, o.actualLocal);
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), e["terminal"] | ""); snprintf(o.terminal, sizeof(o.terminal), "%s", tmp);
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), e["gate"] | ""); snprintf(o.gate, sizeof(o.gate), "%s", tmp);
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), e["checkInDesk"] | ""); snprintf(o.desk, sizeof(o.desk), "%s", tmp);
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), e["baggageBelt"] | ""); snprintf(o.belt, sizeof(o.belt), "%s", tmp);
}

/** When to check next: rarely while the flight is far off, often around it, not at all once it has long landed. */
static uint32_t next_Check_Ms(const Myf_Info_t& f){
    if (!f.have) return 6 * 3600000u;
    const int64_t now = (int64_t)time(nullptr), dep = dep_Time(f), arr = arr_Time(f);
    if (f.status == S_ARRIVED || f.status == S_CANCELED || (arr && now > arr + 3600)) return 24 * 3600000u;
    if (dep && now >= dep - 3 * 3600) return 15 * 60000u;
    if (dep && now >= dep - 24 * 3600) return 60 * 60000u;
    return 6 * 3600000u;
}

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        Myf_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRead = millis() + 6 * 3600000u;
        if (!s.flight[0] || !s.key[0]) continue;
        int y = s.year, m = s.month, d = s.day;
        if (!y){ pxl_time_t t; pxl_time(&t); y = t.year; m = t.month; d = t.day; }
        char path[96];
        snprintf(path, sizeof(path), "/flights/number/%s/%04d-%02d-%02d?withAircraftImage=false&withLocation=false", s.flight, y, m, d);
        Mtb_Kit_Http_t r = {};
        char* body = adb_Get(path, s.key, 32768, r, UA);
        Myf_Info_t f = {};
        if (r.code == 200 && body){
            JsonDocument doc;
            Adb_Reader_t rd = {body, body + r.length};
            if (deserializeJson(doc, rd) == DeserializationError::Ok){
                // The leg that leaves on the day chosen (a flight number can fly more than one leg).
                JsonObjectConst pick;
                for (JsonObjectConst x : doc.as<JsonArrayConst>()){
                    const Adb_Time_t l = adb_Time(x["departure"]["scheduledTime"]["local"] | (const char*)nullptr);
                    if (pick.isNull()) pick = x;
                    if (l.ok && l.y == y && l.m == m && l.d == d){ pick = x; break; }
                }
                if (!pick.isNull()){
                    f.have = true;
                    char tmp[64];
                    mtb_Kit_To_Ascii(tmp, sizeof(tmp), pick["number"] | ""); snprintf(f.number, sizeof(f.number), "%s", tmp);
                    mtb_Kit_To_Ascii(tmp, sizeof(tmp), pick["airline"]["name"] | ""); snprintf(f.airline, sizeof(f.airline), "%s", tmp);
                    mtb_Kit_To_Ascii(tmp, sizeof(tmp), pick["aircraft"]["model"] | ""); snprintf(f.aircraft, sizeof(f.aircraft), "%s", tmp);
                    f.status = status_Of(pick["status"] | (const char*)nullptr);
                    read_End(pick["departure"], f.dep);
                    read_End(pick["arrival"], f.arr);
                }
            }
        }
        heap_caps_free(body);
        portENTER_CRITICAL(&lock);
        sh.calls++;
        if (!strcmp(s.flight, save.flight) && s.year == save.year && s.month == save.month && s.day == save.day){
            sh.code = r.code;
            if (r.code == 200 && f.have){ sh.f = f; sh.fetch = F_OK; sh.fetchedAt = (uint32_t)time(nullptr); }
            else if (r.code == 204 || r.code == 404 || (r.code == 200 && !f.have)){ memset(&sh.f, 0, sizeof(sh.f)); sh.fetch = F_FAIL; sh.code = 204; }
            else sh.fetch = F_FAIL;
            sh.version++;
        }
        const Myf_Info_t now = sh.f;
        portEXIT_CRITICAL(&lock);
        nextRead = millis() + (r.code == 200 ? next_Check_Ms(now) : (r.code == 401 || r.code == 403 || r.code == 429 || r.code == 204 || r.code == 404) ? 6 * 3600000u : 15 * 60000u);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 18), C_HEAD = MTB_KIT_RGB(14, 18, 40), C_GOLD = MTB_KIT_RGB(255, 200, 60);

static void hm(char* t, size_t n, int minute){
    if (minute < 0){ t[0] = '\0'; return; }
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", minute / 60 % 24, minute % 60);
    else { const int h = minute / 60 % 24; snprintf(t, n, "%d:%02d%s", h % 12 ? h % 12 : 12, minute % 60, h < 12 ? "AM" : "PM"); }
}
static void here_Clock(char* t, size_t n, int64_t utc){
    pxl_time_t pt; pxl_time(&pt);
    const int64_t l = utc + pt.utc_offset_minutes * 60;
    hm(t, n, (int)(((l % 86400) + 86400) % 86400 / 60));
}
/** Days between two moments' dates in Pixlpal's time zone. */
static int day_Diff(int64_t later, int64_t earlier){
    pxl_time_t pt; pxl_time(&pt);
    const int64_t o = pt.utc_offset_minutes * 60;
    auto day = [](int64_t l){ return l >= 0 ? l / 86400 : (l - 86399) / 86400; };
    return (int)(day(later + o) - day(earlier + o));
}

static void span_Text(char* t, size_t n, int64_t s){
    if (s < 0) s = 0;
    const int64_t m = (s + 59) / 60;
    if (m < 60) snprintf(t, n, "%lld MIN", (long long)m);
    else if (m < 48 * 60) snprintf(t, n, "%lldH %02lldM", (long long)(m / 60), (long long)(m % 60));
    else snprintf(t, n, "%lld DAYS", (long long)(m / 1440));
}

static void status_Band(const Myf_Info_t& f, char* t, size_t n, uint16_t& c){
    const int64_t late = f.dep.revised && f.dep.sched ? (f.dep.revised - f.dep.sched) / 60 : 0;
    switch (f.status){
        case S_CANCELED: snprintf(t, n, "CANCELLED"); c = KIT_RED; return;
        case S_DIVERTED: snprintf(t, n, "DIVERTED"); c = KIT_RED; return;
        case S_CHECKIN: snprintf(t, n, "CHECK-IN OPEN"); c = KIT_CYAN; return;
        case S_BOARDING: snprintf(t, n, "BOARDING"); c = KIT_GREEN; return;
        case S_GATECLOSED: snprintf(t, n, "GATE CLOSED"); c = KIT_GOLD; return;
        case S_DEPARTED: case S_ENROUTE: snprintf(t, n, "IN THE AIR"); c = KIT_CYAN; return;
        case S_APPROACHING: snprintf(t, n, "LANDING SOON"); c = KIT_CYAN; return;
        case S_ARRIVED: snprintf(t, n, "LANDED"); c = KIT_GREEN; return;
        default:
            if (f.status == S_DELAYED || late >= 10){ if (late > 0) snprintf(t, n, "DELAYED %lld MIN", (long long)late); else snprintf(t, n, "DELAYED"); c = KIT_GOLD; }
            else { snprintf(t, n, "ON TIME"); c = KIT_GREEN; }
            return;
    }
}

static void end_Time(const Myf_End_t& e, int x, int y, bool right){
    char a[12], b[12];
    hm(a, sizeof(a), e.schedLocal);
    const bool late = e.revised && e.sched && e.revised - e.sched >= 5 * 60;
    if (!late){
        if (right) mtb_Kit_Text_Right((int16_t)x, (int16_t)y, Terminal8x12, KIT_TEXT, a, C_BG); else mtb_Kit_Text((int16_t)x, (int16_t)y, Terminal8x12, KIT_TEXT, a, C_BG);
        return;
    }
    hm(b, sizeof(b), e.revisedLocal);
    const int wa = mtb_Kit_Text_Width(Terminal4x6, a), wb = mtb_Kit_Text_Width(Terminal8x12, b);
    const int x0 = right ? x - wb - wa - 4 : x;
    mtb_Kit_Text((int16_t)x0, (int16_t)(y + 4), Terminal4x6, KIT_DIM, a, C_BG);
    mtb_Panel_Draw_HLine((int16_t)(y + 6), (int16_t)x0, (int16_t)(x0 + wa), KIT_RED);
    mtb_Kit_Text((int16_t)(x0 + wa + 4), (int16_t)y, Terminal8x12, KIT_GOLD, b, C_BG);
}

static void plane(int x, int y, uint16_t c){
    mtb_Panel_Fill_Rect((int16_t)(x - 6), (int16_t)(y - 1), (int16_t)(x + 6), (int16_t)(y + 1), c);        // body
    mtb_Panel_Fill_Triangle((int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(x + 2), (int16_t)(y - 1), (int16_t)(x - 3), (int16_t)(y - 7), c);   // wings
    mtb_Panel_Fill_Triangle((int16_t)(x - 1), (int16_t)(y + 1), (int16_t)(x + 2), (int16_t)(y + 1), (int16_t)(x - 3), (int16_t)(y + 7), c);
    mtb_Panel_Fill_Rect((int16_t)(x - 6), (int16_t)(y - 4), (int16_t)(x - 5), (int16_t)(y - 1), c);           // tail
    mtb_Panel_Fill_Triangle((int16_t)(x + 6), (int16_t)(y - 1), (int16_t)(x + 9), (int16_t)y, (int16_t)(x + 6), (int16_t)(y + 1), c);         // nose
}

static void draw(const Myf_Save_t& s, const Myf_Shared_t& d){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], a[24], b[24];
    const int64_t now = (int64_t)time(nullptr);
    const Myf_Info_t& f = d.f;
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s %s", f.have ? f.airline : "MY FLIGHT", f.have ? f.number : s.flight);
    mtb_Kit_Upper(t);
    char fit[64]; mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 46);
    mtb_Kit_Text(3, 2, Terminal6x8, C_GOLD, fit, C_HEAD);
    here_Clock(a, sizeof(a), now);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD);

    if (!s.flight[0] || !s.key[0] || !f.have){
        const char* l1 = "MY FLIGHT";
        char l2[64] = "SET YOUR FLIGHT AND RAPIDAPI KEY IN THE PHONE APP";
        if (s.flight[0] && s.key[0]){
            if (d.fetch == F_NONE){ l1 = "LOOKING UP THE FLIGHT"; l2[0] = '\0'; }
            else { l1 = d.code == 204 ? "FLIGHT NOT FOUND ON THAT DAY" : "COULDN'T CHECK THE FLIGHT"; if (d.code == 204) snprintf(l2, sizeof(l2), "CHECK THE NUMBER AND DATE IN THE PHONE APP"); else adb_Why(d.code, l2, sizeof(l2)); }
        }
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, l1, C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, l2, C_BG);
        return;
    }
    // The route: the codes, and a plane along the line between them.
    mtb_Kit_Text(4, 13, PT_Sans_Narrow_Bold28x34, KIT_TEXT, f.dep.iata, C_BG);
    mtb_Kit_Text_Right(W - 4, 13, PT_Sans_Narrow_Bold28x34, KIT_TEXT, f.arr.iata, C_BG);
    const int x0 = 4 + mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, f.dep.iata) + 10, x1 = W - 4 - mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, f.arr.iata) - 10;
    const int64_t dep = dep_Time(f), arr = arr_Time(f);
    float prog = 0;
    if (f.status == S_ARRIVED) prog = 1;
    else if ((f.status == S_DEPARTED || f.status == S_ENROUTE || f.status == S_APPROACHING) && arr > dep) prog = (float)(now - dep) / (float)(arr - dep);
    if (prog < 0) prog = 0;
    if (prog > 1) prog = 1;
    for (int x = x0; x < x1; x += 4) mtb_Panel_Draw_HLine(30, (int16_t)x, (int16_t)(x + 1), KIT_DIM);
    if (prog > 0) mtb_Panel_Fill_Rect((int16_t)x0, 30, (int16_t)(x0 + (int)((x1 - x0) * prog)), 30, KIT_CYAN);
    plane(x0 + (int)((x1 - x0) * prog), 30, prog > 0 && prog < 1 ? KIT_CYAN : KIT_TEXT);
    char town[28];
    snprintf(town, sizeof(town), "%s", f.dep.town); mtb_Kit_Upper(town); mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, town, 110);
    mtb_Kit_Text(4, 48, Terminal4x6, KIT_LABEL, fit, C_BG);
    snprintf(town, sizeof(town), "%s", f.arr.town); mtb_Kit_Upper(town); mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, town, 110);
    mtb_Kit_Text_Right(W - 4, 48, Terminal4x6, KIT_LABEL, fit, C_BG);
    // The times, each in its airport's own time.
    end_Time(f.dep, 4, 56, false);
    end_Time(f.arr, W - 4, 56, true);
    // The status.
    uint16_t c;
    status_Band(f, t, sizeof(t), c);
    char where[40] = "";
    if (prog > 0 && prog < 1){ if (f.arr.terminal[0]) snprintf(where, sizeof(where), "ARRIVES TERMINAL %s", f.arr.terminal); }
    else if (f.status != S_ARRIVED && f.status != S_CANCELED){
        if (f.dep.terminal[0] || f.dep.gate[0]) snprintf(where, sizeof(where), "%s%s%s%s", f.dep.terminal[0] ? "TERMINAL " : "", f.dep.terminal, f.dep.gate[0] ? "  GATE " : "", f.dep.gate);
    } else if (f.arr.belt[0]) snprintf(where, sizeof(where), "BELT %s", f.arr.belt);
    mtb_Panel_Fill_Rect(0, 72, W - 1, 88, c);
    mtb_Kit_Text(5, 75, Terminal8x12, BLACK, t, c);
    if (where[0]){ mtb_Kit_Upper(where); mtb_Kit_Text_Right(W - 5, 77, Terminal6x8, BLACK, where, c); }
    // The one thing to know now.
    const int64_t leave = dep - ((int64_t)s.early + s.travel) * 60, board = dep - BOARD_MIN * 60;
    const char* label;
    if (f.status == S_CANCELED){ label = "CHECK WITH THE AIRLINE"; a[0] = '\0'; }
    else if (f.status == S_ARRIVED){ label = f.arr.belt[0] ? "BAGGAGE BELT" : "LANDED AT"; if (f.arr.belt[0]) snprintf(a, sizeof(a), "%s", f.arr.belt); else hm(a, sizeof(a), f.arr.actualLocal >= 0 ? f.arr.actualLocal : f.arr.revisedLocal >= 0 ? f.arr.revisedLocal : f.arr.schedLocal); }
    else if (prog > 0){ label = "LANDS IN"; span_Text(a, sizeof(a), arr - now); }
    else if (now < leave){
        label = "LEAVE FOR THE AIRPORT BY";
        here_Clock(b, sizeof(b), leave);
        const int days = day_Diff(leave, now);
        static const char* const WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        if (days == 0) snprintf(a, sizeof(a), "%s", b);
        else if (days == 1) snprintf(a, sizeof(a), "TOMORROW %s", b);
        else { pxl_time_t pt; pxl_time(&pt); time_t x = (time_t)(leave + pt.utc_offset_minutes * 60); struct tm g; gmtime_r(&x, &g); snprintf(a, sizeof(a), "%s %s", WD[g.tm_wday], b); }
    }
    else if (now < board){ label = "BOARDING IN ABOUT"; span_Text(a, sizeof(a), board - now); }
    else if (now < dep){ label = "DEPARTS IN"; span_Text(a, sizeof(a), dep - now); }
    else { label = "DEPARTURE WAS DUE"; hm(a, sizeof(a), f.dep.revisedLocal >= 0 ? f.dep.revisedLocal : f.dep.schedLocal); }
    mtb_Kit_Text(5, 94, Terminal4x6, KIT_LABEL, label, C_BG);
    if (a[0]) mtb_Kit_Text(5, 102, PT_Sans_Narrow_Bold18x22, KIT_TEXT, a, C_BG);
    // Beside it: check-in desks before departure, else the aircraft.
    if (f.dep.desk[0] && now < dep && f.status != S_BOARDING){ snprintf(b, sizeof(b), "CHECK-IN %s", f.dep.desk); mtb_Kit_Upper(b); mtb_Kit_Text_Right(W - 5, 110, Terminal6x8, KIT_LABEL, b, C_BG); }
    else if (f.aircraft[0]){ snprintf(b, sizeof(b), "%s", f.aircraft); mtb_Kit_Upper(b); mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, b, 120); mtb_Kit_Text_Right(W - 5, 112, Terminal4x6, KIT_DIM, fit, C_BG); }
    if (d.fetchedAt && now >= d.fetchedAt){ char ago[16]; mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - d.fetchedAt)); mtb_Kit_Upper(ago); snprintf(b, sizeof(b), "CHECKED %s", ago); mtb_Kit_Text_Right(W - 5, 94, Terminal4x6, KIT_DIM, b, C_BG); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void myFlight_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Flight, cmd_Date, cmd_Key, cmd_Travel, cmd_Early, cmd_Now);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "myfNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Myf_Shared_t* d = (Myf_Shared_t*)heap_caps_calloc(1, sizeof(Myf_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        if (clickRequested){ clickRequested = false; sh.readNow = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; push = true; lastDraw = 0; }
        portEXIT_CRITICAL(&lock);
        if (ms - lastDraw >= 1000){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(save, *d);
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
static void myf_Values(JsonObject values){
    Myf_Save_t s;
    Myf_Shared_t* d = (Myf_Shared_t*)heap_caps_malloc(sizeof(Myf_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(s); memset(d, 0, sizeof(*d)); }
    char t[128], a[16], b[16];
    values["flight"] = s.flight;
    if (s.year){ snprintf(t, sizeof(t), "%04d-%02d-%02d", s.year, s.month, s.day); values["date"] = t; } else values["date"] = "";
    values["key_set"] = s.key[0] ? 1 : 0;
    values["travel"] = s.travel;
    values["early"] = s.early;
    JsonArray rows = values["status"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    const Myf_Info_t& f = d->f;
    snprintf(a, sizeof(a), "%s", s.flight[0] ? s.flight : "Flight");
    r["label"] = a;
    if (!s.key[0]){ r["value"] = "Give your RapidAPI key below"; r["state"] = "warn"; }
    else if (!s.flight[0]){ r["value"] = "Write your flight number below"; r["state"] = "warn"; }
    else if (!appOpen){ r["value"] = "Checked while My Flight is open"; r["state"] = "off"; }
    else if (!f.have){ if (d->fetch == F_NONE){ r["value"] = "Looking up the flight"; r["state"] = "info"; } else { char w[64]; if (d->code == 204) snprintf(w, sizeof(w), "Not found on that day: check the number and date"); else adb_Why(d->code, w, sizeof(w)); r["value"] = w; r["state"] = "error"; } }
    else {
        uint16_t c; char st[40];
        status_Band(f, st, sizeof(st), c);
        hm(a, sizeof(a), f.dep.revisedLocal >= 0 ? f.dep.revisedLocal : f.dep.schedLocal);
        hm(b, sizeof(b), f.arr.revisedLocal >= 0 ? f.arr.revisedLocal : f.arr.schedLocal);
        snprintf(t, sizeof(t), "%s to %s: %s, departs %s, arrives %s (local)%s%s", f.dep.iata, f.arr.iata, st, a, b, f.dep.gate[0] ? ", gate " : "", f.dep.gate);
        r["value"] = t;
        r["state"] = f.status == S_CANCELED || f.status == S_DIVERTED ? "error" : (f.status == S_DELAYED || (f.dep.revised && f.dep.revised - f.dep.sched >= 600)) ? "warn" : "ok";
        if (d->fetchedAt){ values["fetchedAt"] = d->fetchedAt; r["ago"] = "fetchedAt"; }
        const int64_t dep = dep_Time(f), leave = dep - ((int64_t)s.early + s.travel) * 60;
        if ((int64_t)time(nullptr) < leave){
            JsonObject q = rows.add<JsonObject>();
            q["label"] = "Leave by";
            here_Clock(a, sizeof(a), leave);
            snprintf(t, sizeof(t), "%s (your time), to be there %u min early", a, (unsigned)s.early);
            q["value"] = t; q["state"] = "info";
        }
    }
    heap_caps_free(d);
}
