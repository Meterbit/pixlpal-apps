/**
 * Gate Display: an airline gate or check-in desk sign. The flight number and gate in large letters, the destination and
 * departure time (struck through, with the new one, when delayed), and across the middle the call in its colour:
 * ON TIME, CHECK-IN OPEN, BOARDING SOON, BOARDING GROUP 2, FINAL CALL (flashing), GATE CLOSED, DELAYED, CANCELLED,
 * DEPARTED. Each change rings the airport ding-dong. A note runs along the bottom.
 *
 * Staff set it on the phone. Knob: click for the next step of boarding, turn to choose any call, hold to go back to
 * ON TIME. NVS "gatSave". No internet needed.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "gat_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "gatSave"
#define SAVE_MAGIC  0x47415401u

EXT_RAM_BSS_ATTR TaskHandle_t gateDisplay_Task_H = NULL;
static void gateDisplay_App_Task(void* dApplication);
static void gat_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* gateDisplay_App;
Mtb_Applications* gateDisplay_App_GetInstance(){
    if (!gateDisplay_App){
        gateDisplay_App = new Mtb_Applications_FullScreen(gateDisplay_App_Task, &gateDisplay_Task_H, "Gate Display", {0,0}, 8192);
        gateDisplay_App->mtb_App_Set_Mobile_UI(GAT_UI_MANIFEST, GAT_UI_API, nullptr, gat_Values);
    }
    return gateDisplay_App;
}
MTB_REGISTER_APP(gateDisplay_App, 0, 0)       // the catalogue gives the app its number

enum : uint8_t { G_ON_TIME = 0, G_CHECK_IN, G_SOON, G_BOARDING, G_FINAL, G_CLOSED, G_DELAYED, G_CANCELLED, G_DEPARTED, G_COUNT };

struct Gat_Save_t {
    uint32_t magic;
    char airline[28], flight[12], dest[32], gate[8], note[80];
    int16_t time, newTime;              // minutes after midnight, -1 none
    uint8_t state, group, groups, chime;
};

static EXT_RAM_BSS_ATTR Gat_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, stateChanged = false, cmdNext = false;

static void read_Save(Gat_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.time = s.newTime = -1; s.chime = 1; }
    if (s.state >= G_COUNT) s.state = G_ON_TIME;
    if (s.groups > 9) s.groups = 0;
    if (s.group > s.groups) s.group = 0;
    s.airline[sizeof(s.airline) - 1] = s.flight[sizeof(s.flight) - 1] = s.dest[sizeof(s.dest) - 1] = '\0';
    s.gate[sizeof(s.gate) - 1] = s.note[sizeof(s.note) - 1] = '\0';
}

static int parse_Clock(const char* v){
    int h, m;
    if (!v || sscanf(v, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}
static void hm(char* t, size_t n, int minute){
    if (minute < 0){ t[0] = '\0'; return; }
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", minute / 60, minute % 60);
    else snprintf(t, n, "%d:%02d%s", minute / 60 % 12 ? minute / 60 % 12 : 12, minute % 60, minute < 720 ? "AM" : "PM");
}

/** The next step of boarding: boarding soon, boarding (group by group), final call, gate closed, departed. */
static void next_Step(Gat_Save_t& s){
    switch (s.state){
        case G_ON_TIME: case G_CHECK_IN: case G_DELAYED: s.state = G_SOON; break;
        case G_SOON: s.state = G_BOARDING; s.group = s.groups ? 1 : 0; break;
        case G_BOARDING: if (s.groups && s.group < s.groups) s.group++; else s.state = G_FINAL; break;
        case G_FINAL: s.state = G_CLOSED; break;
        case G_CLOSED: s.state = G_DEPARTED; break;
        default: break;
    }
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static bool text_Into(JsonDocument& c, const char* key, char* dest, size_t size){
    const char* v = c[key] | (const char*)nullptr;
    if (!v) return false;
    char tmp[128];
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), v);
    char* p = tmp; while (*p == ' ') p++;
    size_t len = strlen(p); while (len && p[len - 1] == ' ') p[--len] = '\0';
    if (len >= size) return false;
    portENTER_CRITICAL(&lock); snprintf(dest, size, "%s", p); portEXIT_CRITICAL(&lock);
    return true;
}
#define TEXT_CMD(fn, key, field) static void fn(JsonDocument& c){ if (!text_Into(c, key, save.field, sizeof(save.field))){ mtb_Ble_App_Cmd_Reject(); return; } phoneChanged = true; }
TEXT_CMD(cmd_Airline, "airline", airline)
TEXT_CMD(cmd_Flight, "flight", flight)
TEXT_CMD(cmd_Dest, "dest", dest)
TEXT_CMD(cmd_Gate, "gate", gate)
TEXT_CMD(cmd_Note, "note", note)
static void cmd_Time(JsonDocument& c){ const int m = parse_Clock(c["time"] | ""); if (m < 0){ mtb_Ble_App_Cmd_Reject(); return; } save.time = (int16_t)m; phoneChanged = true; }
static void cmd_NewTime(JsonDocument& c){ const int m = parse_Clock(c["newTime"] | ""); if (m < 0){ mtb_Ble_App_Cmd_Reject(); return; } save.newTime = (int16_t)m; phoneChanged = true; }
static void cmd_State(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["state"], 0, G_COUNT - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    if (save.state != v){ save.state = (uint8_t)v; if (v == G_BOARDING) save.group = save.groups ? 1 : 0; stateChanged = true; }
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Groups(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["groups"], 0, 9, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.groups = (uint8_t)v; if (save.group > save.groups) save.group = save.groups; phoneChanged = true; }
static void cmd_Chime(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["chime"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.chime = (uint8_t)v; phoneChanged = true; }
static void cmd_Next(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } cmdNext = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobHold = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; else if (b.type == BUTTON_PRESSED_LONG) knobHold = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 20), C_HEAD = MTB_KIT_RGB(14, 18, 40), C_GOLD = MTB_KIT_RGB(255, 200, 60);

static void call_Of(const Gat_Save_t& s, char* t, size_t n, uint16_t& c, bool& flash){
    flash = false;
    char a[16];
    switch (s.state){
        case G_ON_TIME: snprintf(t, n, "ON TIME"); c = MTB_KIT_RGB(40, 200, 90); break;
        case G_CHECK_IN: snprintf(t, n, "CHECK-IN OPEN"); c = MTB_KIT_RGB(70, 200, 255); break;
        case G_SOON: snprintf(t, n, "BOARDING SOON"); c = MTB_KIT_RGB(70, 130, 255); break;
        case G_BOARDING: if (s.groups && s.group) snprintf(t, n, "BOARDING GROUP %u", (unsigned)s.group); else snprintf(t, n, "BOARDING"); c = MTB_KIT_RGB(40, 210, 90); break;
        case G_FINAL: snprintf(t, n, "FINAL CALL"); c = MTB_KIT_RGB(255, 170, 0); flash = true; break;
        case G_CLOSED: snprintf(t, n, "GATE CLOSED"); c = MTB_KIT_RGB(235, 40, 40); break;
        case G_DELAYED: hm(a, sizeof(a), s.newTime); snprintf(t, n, a[0] ? "DELAYED TO %s" : "DELAYED", a); c = MTB_KIT_RGB(255, 170, 0); break;
        case G_CANCELLED: snprintf(t, n, "CANCELLED"); c = MTB_KIT_RGB(235, 40, 40); break;
        default: snprintf(t, n, "DEPARTED"); c = MTB_KIT_RGB(120, 120, 130); break;
    }
}

static const char* default_Note(uint8_t state){
    switch (state){
        case G_CHECK_IN: return "CHECK-IN IS OPEN FOR THIS FLIGHT";
        case G_SOON: case G_BOARDING: return "PLEASE HAVE YOUR BOARDING PASS AND PASSPORT READY";
        case G_FINAL: return "FINAL CALL: PLEASE GO TO THE GATE NOW";
        case G_CLOSED: return "BOARDING HAS CLOSED FOR THIS FLIGHT";
        case G_DELAYED: return "WE ARE SORRY FOR THE DELAY";
        case G_CANCELLED: return "PLEASE SEE A MEMBER OF STAFF";
        default: return "";
    }
}

static void draw(const Gat_Save_t& s, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], fit[64], a[16];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.airline[0] ? s.airline : "GATE");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 50);
    mtb_Kit_Text(3, 2, Terminal6x8, C_GOLD, fit, C_HEAD);
    pxl_time_t pt;
    if (pxl_time(&pt)){ hm(a, sizeof(a), pt.hour * 60 + pt.minute); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD); }
    if (!s.flight[0] && !s.dest[0]){
        mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, "NO FLIGHT YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "SET THE FLIGHT IN THE PHONE APP", C_BG);
        return;
    }
    // The flight, and the gate.
    snprintf(t, sizeof(t), "%s", s.flight); mtb_Kit_Upper(t);
    mtb_Kit_Text(5, 14, PT_Sans_Narrow_Bold28x34, KIT_TEXT, t, C_BG);
    if (s.gate[0]){
        snprintf(t, sizeof(t), "%s", s.gate); mtb_Kit_Upper(t);
        const int gw = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, t);
        mtb_Kit_Text_Right(W - 5, 14, PT_Sans_Narrow_Bold28x34, C_GOLD, t, C_BG);
        mtb_Kit_Text_Right(W - 9 - gw, 16, Terminal4x6, KIT_LABEL, "GATE", C_BG);
    }
    // The destination, and the time.
    char tm[16]; hm(tm, sizeof(tm), s.time);
    const int tw = tm[0] ? mtb_Kit_Text_Width(Terminal10x17, tm) : 0;
    snprintf(t, sizeof(t), "%s", s.dest); mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, t, W - tw - 18);
    mtb_Kit_Text(5, 50, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_BG);
    if (tm[0]){
        const bool late = s.state == G_DELAYED && s.newTime >= 0;
        mtb_Kit_Text_Right(W - 5, 53, Terminal10x17, late ? KIT_DIM : KIT_TEXT, tm, C_BG);
        if (late) mtb_Panel_Fill_Rect((int16_t)(W - 6 - tw), 61, W - 5, 62, KIT_RED);                 // struck through
    }
    // The call.
    uint16_t c; bool flash;
    call_Of(s, t, sizeof(t), c, flash);
    const bool dark = flash && (ms / 500) % 2;
    const uint16_t band = dark ? mtb_Kit_Dim(c, 60) : c;
    mtb_Panel_Fill_Rect(0, 78, W - 1, 104, band);
    mtb_Kit_Text_Centre(W / 2, 81, PT_Sans_Narrow_Bold18x22, dark ? c : BLACK, t, band);
    // The note.
    const char* note = s.note[0] ? s.note : default_Note(s.state);
    if (note[0]){
        char up[96]; snprintf(up, sizeof(up), "%s", note); mtb_Kit_Upper(up);
        const int w = mtb_Kit_Text_Width(Terminal6x8, up);
        if (w <= W - 8) mtb_Kit_Text_Centre(W / 2, 112, Terminal6x8, KIT_LABEL, up, C_BG);
        else mtb_Kit_Text((int16_t)(W - (int)((ms / 25) % (uint32_t)(w + W))), 112, Terminal6x8, KIT_LABEL, up, C_BG);
    }
}

static void ding_Dong(void){
    if (!save.chime || mtb_Silent_Mode()) return;
    static const Snd_Note_t DING[] = {{784, 380}, {0, 40}, {659, 380}, {0, 40}, {523, 700}};
    snd_Play(DING, 5, 0.3f, false);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void gateDisplay_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Airline, cmd_Flight, cmd_Dest, cmd_Time, cmd_Gate, cmd_State, cmd_Groups, cmd_NewTime, cmd_Note, cmd_Chime, cmd_Next);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; knobClick = knobHold = false; cmdNext = false; stateChanged = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        portENTER_CRITICAL(&lock);
        if (t){ turns -= t; save.state = (uint8_t)(((int)save.state + t) % G_COUNT + G_COUNT) % G_COUNT; if (save.state == G_BOARDING) save.group = save.groups ? 1 : 0; stateChanged = true; }
        if (knobClick || cmdNext){ const uint8_t was = save.state, g = save.group; next_Step(save); if (save.state != was || save.group != g) stateChanged = true; }
        if (knobHold && save.state != G_ON_TIME){ save.state = G_ON_TIME; save.group = 0; stateChanged = true; }
        knobClick = knobHold = false; cmdNext = false;
        const bool changed = stateChanged;
        stateChanged = false;
        portEXIT_CRITICAL(&lock);
        if (changed){ ding_Dong(); push = saveNow = true; lastDraw = 0; }
        if (ms - lastDraw >= 40){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(save, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 800){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void gat_Values(JsonObject values){
    Gat_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); } else read_Save(s);
    char t[96], a[16];
    values["airline"] = s.airline;
    values["flight"] = s.flight;
    values["dest"] = s.dest;
    a[0] = '\0'; if (s.time >= 0) snprintf(a, sizeof(a), "%02d:%02d", s.time / 60, s.time % 60);
    values["time"] = a;
    char b[16] = ""; if (s.newTime >= 0) snprintf(b, sizeof(b), "%02d:%02d", s.newTime / 60, s.newTime % 60);
    values["newTime"] = b;
    values["gate"] = s.gate;
    values["state"] = s.state;
    values["groups"] = s.groups;
    values["note"] = s.note;
    values["chime"] = s.chime;
    JsonArray gate = values["gateNow"].to<JsonArray>();
    JsonObject r = gate.add<JsonObject>();
    uint16_t c; bool flash;
    call_Of(s, t, sizeof(t), c, flash);
    snprintf(a, sizeof(a), "%s", s.flight);
    r["label"] = s.flight[0] ? a : (char*)"Flight";
    char v[128];
    snprintf(v, sizeof(v), "%s%s%s, %s", s.dest, s.gate[0] ? ", gate " : "", s.gate, t);
    r["value"] = v;
    r["state"] = s.state == G_CLOSED || s.state == G_CANCELLED ? "error" : s.state == G_FINAL || s.state == G_DELAYED ? "warn" : s.state == G_DEPARTED ? "off" : "ok";
}
