/**
 * Breathe: guided breathing for adults. A circle on the left of the panel grows as you breathe in, stays while you
 * hold, and shrinks as you breathe out; beside it, what to do and the seconds left in it, and the session's progress.
 * Four patterns (box 4-4-4-4, 4-7-8, slow 5-5, longer out 4-6), one to ten minutes, four colours, and a soft tone at
 * each change if wanted. A session ends after its last full breath, with how long it was; the sessions are counted
 * (today, in a row, in all) and saved on Pixlpal (NVS "brtSave"). No internet.
 *
 * Knob: click, start or pause; turn (when not breathing), the pattern; long press, stop.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "brt_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "brtSave"
#define SAVE_MAGIC  0x42525401u
#define CX          66
#define CY          64
#define R_MIN       14
#define R_MAX       56

EXT_RAM_BSS_ATTR TaskHandle_t breathe_Task_H = NULL;
static void breathe_App_Task(void* dApplication);
static void brt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* breathe_App;
Mtb_Applications* breathe_App_GetInstance(){
    if (!breathe_App){
        breathe_App = new Mtb_Applications_FullScreen(breathe_App_Task, &breathe_Task_H, "Breathe", {0,0}, 8192);
        breathe_App->mtb_App_Set_Mobile_UI(BRT_UI_MANIFEST, BRT_UI_API, nullptr, brt_Values);
    }
    return breathe_App;
}
MTB_REGISTER_APP(breathe_App, 0, 0)       // the catalogue gives the app its number

struct Brt_Pattern_t { const char* name; const char* detail; uint8_t secs[4]; };     // in, hold, out, hold
static const Brt_Pattern_t PATTERN[] = {
    {"BOX", "IN 4  HOLD 4  OUT 4  HOLD 4", {4, 4, 4, 4}},
    {"4-7-8", "IN 4  HOLD 7  OUT 8", {4, 7, 8, 0}},
    {"SLOW", "IN 5  OUT 5", {5, 0, 5, 0}},
    {"LONGER OUT", "IN 4  OUT 6", {4, 0, 6, 0}},
};
#define PATTERNS 4
static const char* const PHASE[4] = {"BREATHE IN", "HOLD", "BREATHE OUT", "HOLD"};

struct Brt_Save_t {
    uint32_t magic;
    uint8_t pattern, minutes, sound, colour;
    int32_t lastDay;                    // the last day with a session (days since 1970, local)
    uint16_t today, streak;             // sessions on lastDay; days in a row up to lastDay
    uint32_t sessions, totalMinutes;
};

static EXT_RAM_BSS_ATTR Brt_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool phoneChanged = false, appOpen = false;
static volatile int startRequested = -1;     // 1 start, 0 stop

static void read_Save(Brt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.pattern = 2; s.minutes = 3; s.sound = 1; }
    if (s.pattern >= PATTERNS) s.pattern = 2;
    if (s.minutes < 1 || s.minutes > 10) s.minutes = 3;
    if (s.colour > 3) s.colour = 0;
}

static int32_t today_Day(void);
/** The days in a row, as of today: 0 once a whole day has passed without a session. */
static unsigned streak_Now(const Brt_Save_t& s){ const int32_t d = today_Day(); return d && s.lastDay && d - s.lastDay <= 1 ? s.streak : 0; }

static int32_t today_Day(void){
    const time_t now = time(nullptr);
    if (now < 1700000000) return 0;
    struct tm l;
    localtime_r(&now, &l);
    return mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Start(JsonDocument& c){
    long v;
    if (!appOpen || !mtb_Kit_Json_Range(c["start"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    startRequested = (int)v;
}
static void cmd_Byte(JsonDocument& c, const char* key, long low, long high, uint8_t& field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], low, high, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    field = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Pattern(JsonDocument& c){ cmd_Byte(c, "pattern", 0, PATTERNS - 1, save.pattern); }
static void cmd_Minutes(JsonDocument& c){ cmd_Byte(c, "minutes", 1, 10, save.minutes); }
static void cmd_Sound(JsonDocument& c){ cmd_Byte(c, "sound", 0, 1, save.sound); }
static void cmd_Colour(JsonDocument& c){ cmd_Byte(c, "colour", 0, 3, save.colour); }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false, longRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED) clickRequested = true;
    else if (b.type == BUTTON_PRESSED_LONG) longRequested = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(3, 6, 12);
static uint16_t theme(uint8_t c){
    static const uint16_t T[4] = {MTB_KIT_RGB(0, 170, 160), MTB_KIT_RGB(60, 130, 255), MTB_KIT_RGB(160, 100, 255), MTB_KIT_RGB(255, 160, 50)};
    return T[c & 3];
}

// A soft circle: rings fading out from the edge.
static void orb(int r, uint16_t c){
    mtb_Panel_Fill_Circle(CX, CY, r + 4, mtb_Kit_Dim(c, 30));
    mtb_Panel_Fill_Circle(CX, CY, r + 2, mtb_Kit_Dim(c, 70));
    mtb_Panel_Fill_Circle(CX, CY, r, c);
    mtb_Panel_Fill_Circle(CX - r / 4, CY - r / 4, r / 3, mtb_Kit_Blend(c, KIT_TEXT, 0.25f));
}

static void draw_Idle(const Brt_Save_t& s, bool paused, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const uint16_t c = theme(s.colour);
    // A slow idle glow.
    const float g = 0.5f - 0.5f * cosf((float)(ms % 8000) / 8000.0f * 6.2832f);
    orb(R_MIN + (int)(10 * g), mtb_Kit_Dim(c, 140));
    char t[64];
    const Brt_Pattern_t& p = PATTERN[s.pattern];
    mtb_Kit_Text(132, 10, Terminal6x8, KIT_LABEL, paused ? "PAUSED" : "BREATHE", C_BG);
    mtb_Kit_Text(132, 22, PT_Sans_Narrow_Bold18x22, KIT_TEXT, p.name, C_BG);
    mtb_Kit_Text(132, 48, Terminal4x6, c, p.detail, C_BG);
    snprintf(t, sizeof(t), "%u MIN", (unsigned)s.minutes);
    mtb_Kit_Text(132, 58, Terminal6x8, KIT_LABEL, t, C_BG);
    mtb_Kit_Text(132, 76, Terminal6x8, KIT_TEXT, paused ? "CLICK: GO ON" : "CLICK: BEGIN", C_BG);
    mtb_Kit_Text(132, 87, Terminal4x6, KIT_DIM, paused ? "LONG PRESS: STOP" : "TURN: ANOTHER PATTERN", C_BG);
    if (s.sessions){
        snprintf(t, sizeof(t), "TODAY %u  IN A ROW %u", (unsigned)(s.lastDay == today_Day() ? s.today : 0), streak_Now(s));
        mtb_Kit_Text(132, 110, Terminal4x6, KIT_DIM, t, C_BG);
    }
}

static void draw_Breath(const Brt_Save_t& s, int phase, float t01, int secsLeft, float progress, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const uint16_t c = theme(s.colour);
    // In: grows; out: shrinks; holds: stay, with a slow shimmer.
    const float e = 0.5f - 0.5f * cosf(t01 * 3.14159f);
    float f = phase == 0 ? e : phase == 2 ? 1.0f - e : phase == 1 ? 1.0f : 0.0f;
    const int r = R_MIN + (int)((R_MAX - R_MIN) * f);
    uint16_t oc = c;
    if (phase == 1 || phase == 3) oc = mtb_Kit_Blend(c, KIT_TEXT, 0.12f + 0.08f * sinf((float)ms / 300.0f));
    orb(r, oc);
    mtb_Kit_Text(132, 22, PT_Sans_Narrow_Bold18x22, KIT_TEXT, PHASE[phase], C_BG);
    char t[8];
    snprintf(t, sizeof(t), "%d", secsLeft);
    mtb_Kit_Text(132, 48, PT_Sans_Narrow_Bold28x34, c, t, C_BG);
    mtb_Kit_Text(132, 94, Terminal4x6, KIT_DIM, PATTERN[s.pattern].name, C_BG);
    mtb_Kit_Bar(132, 104, W - 140, 3, progress, mtb_Kit_Dim(c, 180));
}

static void draw_Done(const Brt_Save_t& s, uint32_t secs){
    mtb_Panel_Fill_Screen(C_BG);
    const uint16_t c = theme(s.colour);
    orb(R_MIN + 6, mtb_Kit_Dim(c, 160));
    char t[40];
    mtb_Kit_Text(132, 30, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "WELL DONE", C_BG);
    snprintf(t, sizeof(t), "%lu MIN %02lu S", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    mtb_Kit_Text(132, 58, Terminal8x12, c, t, C_BG);
    mtb_Kit_Text(132, 80, Terminal4x6, KIT_LABEL, "TAKE A MOMENT BEFORE YOU GO ON", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void tone(int phase){
    static const Snd_Note_t IN[] = {{392, 90}, {440, 110}};
    static const Snd_Note_t OUT[] = {{392, 90}, {330, 140}};
    static const Snd_Note_t HOLD[] = {{523, 70}};
    static const Snd_Note_t END[] = {{523, 160}, {659, 160}, {784, 320}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (phase == 0) snd_Play(IN, 2, 0.12f, false);
    else if (phase == 2) snd_Play(OUT, 2, 0.12f, false);
    else if (phase == 4) snd_Play(END, 3, 0.15f, false);
    else snd_Play(HOLD, 1, 0.08f, false);
}

static void count_Session(uint32_t secs){
    const int32_t day = today_Day();
    portENTER_CRITICAL(&lock);
    if (day){
        if (save.lastDay == day) save.today++;
        else { save.streak = save.lastDay && day - save.lastDay == 1 ? save.streak + 1 : 1; save.today = 1; save.lastDay = day; }
    }
    save.sessions++;
    save.totalMinutes += (secs + 30) / 60;
    portEXIT_CRITICAL(&lock);
}

static void breathe_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Start, cmd_Pattern, cmd_Minutes, cmd_Sound, cmd_Colour);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = longRequested = false; turns = 0; startRequested = -1; phoneChanged = false;
    appOpen = true;
    enum { IDLE, RUNNING, PAUSED, DONE } state = IDLE;
    uint32_t phaseStart = 0, elapsed = 0, lastTick = 0, doneSince = 0, pushedMs = 0, doneSecs = 0;
    int phase = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; if (state != RUNNING){ save.pattern = (uint8_t)(((save.pattern + t) % PATTERNS + PATTERNS) % PATTERNS); saveNow = push = true; if (state == PAUSED) state = IDLE; } }
        bool start = false, stop = false;
        if (clickRequested){ clickRequested = false; if (state == RUNNING){ state = PAUSED; push = true; } else start = true; }
        if (longRequested){ longRequested = false; stop = true; }
        if (startRequested >= 0){ if (startRequested) start = state != RUNNING; else stop = true; startRequested = -1; }
        if (start){
            if (state != PAUSED){ elapsed = 0; phase = 0; phaseStart = 0; }
            state = RUNNING; lastTick = ms; push = true;
            tone(phase);
        }
        if (stop && (state == RUNNING || state == PAUSED)){
            if (elapsed >= 60000u){ count_Session(elapsed / 1000); saveNow = true; }
            state = IDLE; push = true;
        }
        const Brt_Pattern_t& p = PATTERN[save.pattern];
        mtb_Panel_Begin_Update();
        if (state == RUNNING){
            elapsed += ms - lastTick;
            phaseStart += ms - lastTick;
            lastTick = ms;
            // The phase: skip the zero-length holds.
            while (p.secs[phase] == 0 || phaseStart >= p.secs[phase] * 1000u){
                if (p.secs[phase]) phaseStart -= p.secs[phase] * 1000u;
                phase = (phase + 1) % 4;
                if (p.secs[phase]){
                    // The session ends after the breath that passes its length (at the start of a new breath).
                    if (phase == 0 && elapsed >= save.minutes * 60000u){ state = DONE; doneSince = ms; doneSecs = elapsed / 1000; count_Session(doneSecs); saveNow = push = true; tone(4); break; }
                    tone(phase);
                }
            }
            if (state == RUNNING){
                const float t01 = (float)phaseStart / (p.secs[phase] * 1000.0f);
                const int left = p.secs[phase] - (int)(phaseStart / 1000);
                float progress = (float)elapsed / (save.minutes * 60000.0f);
                draw_Breath(save, phase, t01, left, progress > 1 ? 1 : progress, ms);
            }
        }
        if (state == DONE){
            draw_Done(save, doneSecs);
            if (ms - doneSince >= 8000u){ state = IDLE; }
        } else if (state != RUNNING) draw_Idle(save, state == PAUSED, ms);
        mtb_Panel_End_Update();
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 800){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void brt_Values(JsonObject values){
    Brt_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[80];
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Pattern";
    snprintf(t, sizeof(t), "%s, %u min", s.pattern == 0 ? "Box" : s.pattern == 1 ? "4-7-8" : s.pattern == 2 ? "Slow" : "Longer out", (unsigned)s.minutes);
    o["value"] = t; o["state"] = "info";
    o = rows.add<JsonObject>();
    o["label"] = "Sessions";
    const unsigned today = s.lastDay == today_Day() ? s.today : 0;
    snprintf(t, sizeof(t), "%u today, %u days in a row, %lu in all (%lu min)", today, streak_Now(s), (unsigned long)s.sessions, (unsigned long)s.totalMinutes);
    o["value"] = t; o["state"] = today ? "ok" : "off";
    values["pattern"] = s.pattern; values["minutes"] = s.minutes; values["sound"] = s.sound; values["colour"] = s.colour;
}
