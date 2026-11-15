/**
 * Exam Clock: an exam's time on the wall. Before: the exam's name and length. Reading time ("DO NOT WRITE YET"), then
 * the writing time left in large figures with the start and finish times, warnings at 30, 15 and 5 minutes, and then
 * extra time for those who have it, with its own finish. At the end "STOP WRITING" flashes. The room's rules show in
 * turn along the bottom, and the time of day stays in the corner.
 *
 * The exam's start and pauses are kept by the time of day (NVS), so closing and opening the app keeps its place.
 * Knob: click to start (or, at the end, back to the start), hold to pause and go on, turn before the start to change
 * the writing time. NVS "exmSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "exm_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W           256
#define H           128
#define RULES       4
#define RULE_LEN    44
#define NAME_LEN    36
#define NVS_KEY     "exmSave"
#define SAVE_MAGIC  0x45584D01u

EXT_RAM_BSS_ATTR TaskHandle_t examClock_Task_H = NULL;
static void examClock_App_Task(void* dApplication);
static void exm_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* examClock_App;
Mtb_Applications* examClock_App_GetInstance(){
    if (!examClock_App){
        examClock_App = new Mtb_Applications_FullScreen(examClock_App_Task, &examClock_Task_H, "Exam Clock", {0,0}, 8192);
        examClock_App->mtb_App_Set_Mobile_UI(EXM_UI_MANIFEST, EXM_UI_API, nullptr, exm_Values);
    }
    return examClock_App;
}
MTB_REGISTER_APP(examClock_App, 0, 0)         // the catalogue gives the app its number

struct Exm_Save_t {
    uint32_t magic;
    char name[NAME_LEN];
    uint16_t minutes;                   // writing time
    uint8_t reading, extra, warn, chime;
    char rule[RULES][RULE_LEN];
    // A running exam, by the time of day (Unix seconds).
    uint32_t startedAt;                 // 0: not started
    uint32_t pausedAt;                  // 0: not paused
    uint32_t pausedFor;                 // seconds paused so far
    uint16_t addedMin;                  // minutes added to the writing time
};

enum : uint8_t { X_SETUP = 0, X_READING, X_WRITING, X_EXTRA, X_DONE };

static EXT_RAM_BSS_ATTR Exm_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Exm_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 60; s.extra = 25; s.warn = 1; s.chime = 1;
        snprintf(s.name, sizeof(s.name), "Exam");
        static const char* const R[] = {"Phones off and away", "No talking", "Write your name on every page", "Raise your hand for help"};
        for (int i = 0; i < RULES; i++) snprintf(s.rule[i], RULE_LEN, "%s", R[i]);
    }
    if (s.minutes < 5 || s.minutes > 300) s.minutes = 60;
    if (s.reading > 30) s.reading = 0;
    if (s.extra > 100) s.extra = 25;
    s.name[NAME_LEN - 1] = '\0';
    for (int i = 0; i < RULES; i++) s.rule[i][RULE_LEN - 1] = '\0';
}

// The exam's times -------------------------------------------------------------------------------------------------------------------
struct Exm_Now_t {
    uint8_t phase;
    int32_t left;                       // seconds left in this phase
    uint32_t readingEnd, writingEnd, extraEnd;     // Unix seconds (from the start, pauses included)
    bool paused;
};

static Exm_Now_t exam_Now(const Exm_Save_t& s, uint32_t now){
    Exm_Now_t r = {X_SETUP, 0, 0, 0, 0, s.pausedAt != 0};
    if (!s.startedAt) return r;
    const uint32_t paused = s.pausedFor + (s.pausedAt ? now - s.pausedAt : 0);
    const int64_t elapsed = (int64_t)now - s.startedAt - paused;
    const int32_t reading = s.reading * 60, writing = (s.minutes + s.addedMin) * 60, extra = writing * s.extra / 100;
    const uint32_t shift = s.startedAt + (s.pausedAt ? now - s.pausedAt : 0) + s.pausedFor;
    r.readingEnd = shift + reading;
    r.writingEnd = r.readingEnd + writing;
    r.extraEnd = r.writingEnd + extra;
    if (elapsed < reading){ r.phase = X_READING; r.left = (int32_t)(reading - elapsed); }
    else if (elapsed < reading + writing){ r.phase = X_WRITING; r.left = (int32_t)(reading + writing - elapsed); }
    else if (elapsed < reading + writing + extra){ r.phase = X_EXTRA; r.left = (int32_t)(reading + writing + extra - elapsed); }
    else r.phase = X_DONE;
    return r;
}

static void clock_Text(char* t, size_t n, uint32_t utc){
    struct tm lt;
    time_t x = (time_t)utc;
    localtime_r(&x, &lt);
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(t, n, "%d:%02d%s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

/** "1:23:45", "45:12" (minutes and seconds under an hour). */
static void left_Text(char* t, size_t n, int32_t s){
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(t, n, "%ld:%02ld:%02ld", (long)(s / 3600), (long)(s % 3600 / 60), (long)(s % 60));
    else snprintf(t, n, "%ld:%02ld", (long)(s / 60), (long)(s % 60));
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static bool text_Into(JsonDocument& c, const char* key, char* dest, size_t size){
    if (c[key].isNull()) return false;
    char tmp[96];
    mtb_Kit_To_Ascii(tmp, sizeof(tmp), c[key] | "");
    char* p = tmp; while (*p == ' ') p++;
    size_t len = strlen(p); while (len && p[len - 1] == ' ') p[--len] = '\0';
    if (len >= size) p[size - 1] = '\0';
    portENTER_CRITICAL(&lock); snprintf(dest, size, "%s", p); portEXIT_CRITICAL(&lock);
    return true;
}
static void cmd_Name(JsonDocument& c){ if (!text_Into(c, "name", save.name, NAME_LEN)) { mtb_Ble_App_Cmd_Reject(); return; } phoneChanged = true; }
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 5, 300, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint16_t)v; phoneChanged = true; }
static void cmd_Reading(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["reading"], 0, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.reading = (uint8_t)v; phoneChanged = true; }
static void cmd_Extra(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["extra"], 0, 100, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.extra = (uint8_t)v; phoneChanged = true; }
static void cmd_Rule(JsonDocument& c){
    for (int i = 0; i < RULES; i++){ char k[8]; snprintf(k, sizeof(k), "rule%d", i); if (text_Into(c, k, save.rule[i], RULE_LEN)){ phoneChanged = true; return; } }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Warn(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["warn"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.warn = (uint8_t)v; phoneChanged = true; }
static void cmd_Chime(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["chime"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.chime = (uint8_t)v; phoneChanged = true; }

static volatile bool cmdStart = false, cmdPause = false, cmdStop = false, cmdMore = false;
static void cmd_Start(JsonDocument&){ if (!appOpen || save.startedAt){ mtb_Ble_App_Cmd_Reject(); return; } cmdStart = true; }
static void cmd_Pause(JsonDocument&){ if (!appOpen || !save.startedAt){ mtb_Ble_App_Cmd_Reject(); return; } cmdPause = true; }
static void cmd_Stop(JsonDocument&){ cmdStop = true; }
static void cmd_More(JsonDocument&){ if (!appOpen || !save.startedAt){ mtb_Ble_App_Cmd_Reject(); return; } cmdMore = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobHold = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; else if (b.type == BUTTON_PRESSED_LONG) knobHold = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 6, 10), C_HEAD = MTB_KIT_RGB(14, 18, 28);

static void header(const Exm_Save_t& s, uint32_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    char nm[NAME_LEN + 2], fit[NAME_LEN + 2], c[16];
    snprintf(nm, sizeof(nm), "%s", s.name); mtb_Kit_Upper(nm);
    clock_Text(c, sizeof(c), now);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, W - 50);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, fit, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, c, C_HEAD);
}

static void rules_Line(const Exm_Save_t& s, uint32_t ms){
    int used[RULES], n = 0;
    for (int i = 0; i < RULES; i++) if (s.rule[i][0]) used[n++] = i;
    if (!n) return;
    char r[RULE_LEN + 2], fit[RULE_LEN + 2];
    snprintf(r, sizeof(r), "%s", s.rule[used[(ms / 8000) % n]]);
    mtb_Kit_Upper(r);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, r, W - 8);
    mtb_Panel_Fill_Rect(0, 116, W - 1, 127, C_HEAD);
    mtb_Kit_Text_Centre(W / 2, 118, Terminal6x8, KIT_GOLD, fit, C_HEAD);
}

static void draw_Exam(const Exm_Save_t& s, uint32_t now, uint32_t ms, const char* banner, uint16_t bannerColor){
    mtb_Panel_Fill_Screen(C_BG);
    header(s, now);
    const Exm_Now_t x = exam_Now(s, now);
    char t[48], a[16], b[16];
    switch (x.phase){
        case X_SETUP: {
            char nm[NAME_LEN + 2], fit[NAME_LEN + 2];
            snprintf(nm, sizeof(nm), "%s", s.name); mtb_Kit_Upper(nm);
            mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, nm, W - 8);
            mtb_Kit_Text_Centre(W / 2, 18, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_BG);
            const int total = s.minutes;
            if (total >= 60) snprintf(a, sizeof(a), total % 60 ? "%dH %02dM" : "%dH", total / 60, total % 60); else snprintf(a, sizeof(a), "%d MIN", total);
            mtb_Kit_Text_Centre(W / 2, 46, Terminal10x17, KIT_CYAN, a, C_BG);
            if (s.reading) { snprintf(t, sizeof(t), "+ %d MIN READING TIME", s.reading); mtb_Kit_Text_Centre(W / 2, 67, Terminal6x8, KIT_LABEL, t, C_BG); }
            if (s.extra){ snprintf(t, sizeof(t), "EXTRA TIME %d%%: +%d MIN", s.extra, (s.minutes * s.extra + 99) / 100); mtb_Kit_Text_Centre(W / 2, 79, Terminal6x8, KIT_LABEL, t, C_BG); }
            mtb_Kit_Text_Centre(W / 2, 96, Terminal6x8, KIT_GREEN, "CLICK TO START", C_BG);
            mtb_Kit_Text_Centre(W / 2, 107, Terminal4x6, KIT_DIM, "TURN: WRITING TIME", C_BG);
            break;
        }
        case X_READING:
            mtb_Kit_Text_Centre(W / 2, 14, Terminal8x12, KIT_CYAN, "READING TIME", C_BG);
            left_Text(t, sizeof(t), x.left);
            mtb_Kit_Text_Centre(W / 2, 30, MS_Gothic31x60, KIT_CYAN, t, C_BG);
            mtb_Kit_Text_Centre(W / 2, 94, Terminal8x12, KIT_GOLD, "DO NOT WRITE YET", C_BG);
            rules_Line(s, ms);
            break;
        case X_WRITING:
        case X_EXTRA: {
            const bool extra = x.phase == X_EXTRA;
            left_Text(t, sizeof(t), x.left);
            const uint16_t c = x.left <= 300 ? KIT_GOLD : extra ? KIT_CYAN : KIT_TEXT;
            if (extra) mtb_Kit_Text_Centre(W / 2, 13, Terminal6x8, KIT_CYAN, "EXTRA TIME", C_BG);
            mtb_Kit_Text_Centre(W / 2, extra ? 24 : 18, MS_Gothic31x60, c, t, C_BG);
            clock_Text(a, sizeof(a), s.startedAt);
            clock_Text(b, sizeof(b), extra ? x.extraEnd : x.writingEnd);
            snprintf(t, sizeof(t), "STARTED %s   %s %s", a, extra ? "EXTRA ENDS" : "ENDS", b);
            mtb_Kit_Text_Centre(W / 2, 88, Terminal6x8, KIT_LABEL, t, C_BG);
            if (!extra && s.extra){ clock_Text(b, sizeof(b), x.extraEnd); snprintf(t, sizeof(t), "EXTRA TIME ENDS %s", b); mtb_Kit_Text_Centre(W / 2, 101, Terminal4x6, KIT_CYAN, t, C_BG); }
            else if (extra){ clock_Text(b, sizeof(b), x.writingEnd); snprintf(t, sizeof(t), "MAIN TIME ENDED %s: EXTRA TIME ONLY", b); mtb_Kit_Text_Centre(W / 2, 101, Terminal4x6, KIT_LABEL, t, C_BG); }
            rules_Line(s, ms);
            break;
        }
        case X_DONE: {
            const bool flash = now - x.extraEnd < 15 && (ms / 400) % 2;
            const uint16_t bg = flash ? KIT_RED : C_BG;
            mtb_Panel_Fill_Rect(0, 12, W - 1, 113, bg);
            mtb_Kit_Text_Centre(W / 2, 26, PT_Sans_Narrow_Bold28x34, flash ? BLACK : KIT_RED, "STOP WRITING", bg);
            mtb_Kit_Text_Centre(W / 2, 64, Terminal10x17, flash ? BLACK : KIT_TEXT, "PENS DOWN", bg);
            clock_Text(a, sizeof(a), s.startedAt); clock_Text(b, sizeof(b), s.extra ? x.extraEnd : x.writingEnd);
            snprintf(t, sizeof(t), "%s - %s", a, b);
            mtb_Kit_Text_Centre(W / 2, 90, Terminal6x8, flash ? BLACK : KIT_LABEL, t, bg);
            mtb_Kit_Text_Centre(W / 2, 104, Terminal4x6, flash ? BLACK : KIT_DIM, "CLICK WHEN DONE", bg);
            break;
        }
    }
    if (x.paused){
        const bool on = (ms / 600) % 2;
        mtb_Panel_Fill_Rect(30, 46, W - 31, 78, on ? KIT_ORANGE : MTB_KIT_RGB(70, 40, 0));
        mtb_Kit_Text_Centre(W / 2, 50, PT_Sans_Narrow_Bold18x22, on ? BLACK : KIT_ORANGE, "PAUSED", on ? KIT_ORANGE : MTB_KIT_RGB(70, 40, 0));
    } else if (banner){
        mtb_Panel_Fill_Rect(14, 44, W - 15, 80, bannerColor);
        mtb_Kit_Text_Centre(W / 2, 51, PT_Sans_Narrow_Bold18x22, BLACK, banner, bannerColor);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chime(int kind){
    if (!save.chime || mtb_Silent_Mode()) return;
    static const Snd_Note_t WARN[] = {{880, 200}, {0, 100}, {880, 200}};
    static const Snd_Note_t PHASE[] = {{659, 200}, {880, 380}};
    static const Snd_Note_t END[] = {{988, 300}, {0, 100}, {784, 300}, {0, 100}, {659, 600}};
    if (kind == 0) snd_Play(WARN, 3, 0.3f, false);
    else if (kind == 1) snd_Play(PHASE, 2, 0.3f, false);
    else snd_Play(END, 5, 0.35f, false);
}

static void examClock_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Name, cmd_Minutes, cmd_Reading, cmd_Extra, cmd_Rule, cmd_Warn, cmd_Chime, cmd_Start, cmd_Pause, cmd_Stop, cmd_More);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; knobClick = knobHold = false; cmdStart = cmdPause = cmdStop = cmdMore = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, bannerUntil = 0;
    char banner[32] = "";
    uint16_t bannerColor = KIT_GOLD;
    const uint32_t now0 = (uint32_t)time(nullptr);
    Exm_Now_t last = exam_Now(save, now0);
    int32_t lastLeft = last.left;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis(), now = (uint32_t)time(nullptr);
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){
            turns -= t;
            if (!save.startedAt){ int m = (int)save.minutes + t * 5; save.minutes = (uint16_t)(m < 5 ? 5 : m > 300 ? 300 : m); push = saveNow = true; lastDraw = 0; }
        }
        const Exm_Now_t cur = exam_Now(save, now);
        const bool start = cmdStart || (knobClick && cur.phase == X_SETUP);
        const bool back = cmdStop || (knobClick && cur.phase == X_DONE);
        const bool pause = cmdPause || (knobHold && save.startedAt && cur.phase != X_DONE);
        knobClick = knobHold = false; cmdStart = cmdPause = cmdStop = false;
        portENTER_CRITICAL(&lock);
        if (start && !save.startedAt){ save.startedAt = now; save.pausedAt = 0; save.pausedFor = 0; save.addedMin = 0; }
        if (back){ save.startedAt = 0; save.pausedAt = 0; save.pausedFor = 0; save.addedMin = 0; }
        if (pause && save.startedAt){
            if (save.pausedAt){ save.pausedFor += now - save.pausedAt; save.pausedAt = 0; } else save.pausedAt = now;
        }
        if (cmdMore && save.startedAt){ save.addedMin += 5; snprintf(banner, sizeof(banner), "FIVE MINUTES MORE"); bannerColor = KIT_CYAN; bannerUntil = ms + 4000; }
        cmdMore = false;
        portEXIT_CRITICAL(&lock);
        if (start || back || pause){ push = saveNow = true; lastDraw = 0; if (start) chime(1); }

        // Changes of phase, and the warnings.
        const Exm_Now_t x = exam_Now(save, now);
        if (!x.paused && x.phase != last.phase){
            if (x.phase == X_WRITING && last.phase == X_READING){ snprintf(banner, sizeof(banner), "START WRITING"); bannerColor = KIT_GREEN; bannerUntil = ms + 6000; chime(1); }
            else if (x.phase == X_EXTRA){ snprintf(banner, sizeof(banner), "MAIN TIME IS UP"); bannerColor = KIT_GOLD; bannerUntil = ms + 8000; chime(2); }
            else if (x.phase == X_DONE && last.phase != X_SETUP) chime(2);
            push = true;
        } else if (!x.paused && save.warn && x.phase == X_WRITING && x.phase == last.phase){
            static const int32_t MARK[] = {1800, 900, 300};
            for (int32_t m : MARK) if (lastLeft > m && x.left <= m){
                snprintf(banner, sizeof(banner), "%ld MINUTES LEFT", (long)(m / 60));
                bannerColor = m == 300 ? KIT_GOLD : KIT_CYAN; bannerUntil = ms + 6000; chime(0); push = true;
            }
        } else if (!x.paused && save.warn && x.phase == X_EXTRA && last.phase == X_EXTRA && lastLeft > 300 && x.left <= 300){
            snprintf(banner, sizeof(banner), "5 MINUTES LEFT"); bannerColor = KIT_GOLD; bannerUntil = ms + 6000; chime(0);
        }
        last = x; lastLeft = x.left;

        if (ms - lastDraw >= 200){
            lastDraw = ms;
            Exm_Save_t* s = &save;
            mtb_Panel_Begin_Update();
            draw_Exam(*s, now, ms, (int32_t)(bannerUntil - ms) > 0 ? banner : nullptr, bannerColor);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push || ms - pushedMs >= 15000) && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void exm_Values(JsonObject values){
    Exm_Save_t* s = (Exm_Save_t*)heap_caps_malloc(sizeof(Exm_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    values["name"] = s->name;
    values["minutes"] = s->minutes;
    values["reading"] = s->reading;
    values["extra"] = s->extra;
    values["warn"] = s->warn;
    values["chime"] = s->chime;
    char k[8];
    for (int i = 0; i < RULES; i++){ snprintf(k, sizeof(k), "rule%d", i); values[k] = s->rule[i]; }
    const uint32_t now = (uint32_t)time(nullptr);
    const Exm_Now_t x = exam_Now(*s, now);
    JsonArray rows = values["exam"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = s->name;
    char t[96], a[16], b[16];
    left_Text(a, sizeof(a), x.left);
    switch (x.phase){
        case X_SETUP: snprintf(t, sizeof(t), "Ready: %u min%s", (unsigned)s->minutes, s->reading ? ", with reading time" : ""); r["state"] = "info"; break;
        case X_READING: snprintf(t, sizeof(t), "Reading time, %s left", a); r["state"] = "info"; break;
        case X_WRITING: clock_Text(b, sizeof(b), x.writingEnd); snprintf(t, sizeof(t), "Writing, %s left (ends %s)", a, b); r["state"] = "ok"; break;
        case X_EXTRA: clock_Text(b, sizeof(b), x.extraEnd); snprintf(t, sizeof(t), "Extra time, %s left (ends %s)", a, b); r["state"] = "warn"; break;
        default: snprintf(t, sizeof(t), "Finished"); r["state"] = "off"; break;
    }
    if (x.paused) strlcat(t, ", PAUSED", sizeof(t));
    r["value"] = t;
    heap_caps_free(s);
}
