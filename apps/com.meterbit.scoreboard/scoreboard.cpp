/**
 * Scoreboard: a scoreboard for games people play, readable across the hall. Two sides with names and colours, the
 * score in large figures; for football and basketball a game clock (counting up, or down for basketball) with
 * periods and a buzzer at each period's end; for volleyball, table tennis, badminton and tennis the rules in
 * sb_Rules.cpp, with the sets, games and who serves. Points come from the knob or the phone; undo goes back up to 20
 * steps. The match is saved (NVS "sbSave") so closing the app keeps it. No internet.
 *
 * Knob: turn, choose the side (underlined); click, a point to it; double click, undo; long press, start or stop the
 * clock (racket sports: a new match once one is won).
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "sb_Mobile_Ui.h"
#include "sb_Rules.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define W           256
#define H           128
#define NVS_KEY     "sbSave"
#define SAVE_MAGIC  0x53424401u
#define NAME_LEN    14
#define UNDO        20

EXT_RAM_BSS_ATTR TaskHandle_t scoreboard_Task_H = NULL;
static void scoreboard_App_Task(void* dApplication);
static void sb_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* scoreboard_App;
Mtb_Applications* scoreboard_App_GetInstance(){
    if (!scoreboard_App){
        scoreboard_App = new Mtb_Applications_FullScreen(scoreboard_App_Task, &scoreboard_Task_H, "Scoreboard", {0,0}, 8192);
        scoreboard_App->mtb_App_Set_Mobile_UI(SB_UI_MANIFEST, SB_UI_API, nullptr, sb_Values);
    }
    return scoreboard_App;
}
MTB_REGISTER_APP(scoreboard_App, 0, 0)       // the catalogue gives the app its number

struct Sb_Save_t {
    uint32_t magic;
    uint8_t sport, bestOf, minutes, periods, sound;
    char name[2][NAME_LEN];
    uint32_t argb[2];
    Sb_Match_t m;
    uint8_t period;                     // 0-based
    uint32_t clockMs;                   // played in this period
    uint8_t periodOver;                 // the period's time is up (waits for the next start)
};

static EXT_RAM_BSS_ATTR Sb_Save_t save;
static EXT_RAM_BSS_ATTR Sb_Match_t undoStack[UNDO];
static int undoN = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool phoneChanged = false, appOpen = false;

// Requests from the phone and the knob, done in the app's loop.
static volatile int pointSide = -1, pointN = 1, clockRequest = -1;
static volatile bool undoRequested = false, newRequested = false;

static bool timed(uint8_t sport){ return sport == SB_FOOTBALL || sport == SB_BASKETBALL; }

static void defaults(Sb_Save_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = SAVE_MAGIC; s.sport = SB_FOOTBALL; s.bestOf = 5; s.minutes = 45; s.periods = 2; s.sound = 1;
    snprintf(s.name[0], NAME_LEN, "HOME"); snprintf(s.name[1], NAME_LEN, "AWAY");
    s.argb[0] = 0xFFE53935; s.argb[1] = 0xFF1E88E5;
}

static void read_Save(Sb_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC) defaults(s);
    if (s.sport >= SB_SPORTS) s.sport = SB_FOOTBALL;
    if (!(s.bestOf == 1 || s.bestOf == 3 || s.bestOf == 5 || s.bestOf == 7)) s.bestOf = 5;
    if (s.minutes < 1 || s.minutes > 60) s.minutes = 45;
    if (s.periods < 1 || s.periods > 4) s.periods = 2;
    for (int i = 0; i < 2; i++) s.name[i][NAME_LEN - 1] = '\0';
}

static uint16_t color_Of(uint32_t argb){ return MTB_KIT_RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255); }

static void new_Match(Sb_Save_t& s){ sb_New_Match(s.m); s.period = 0; s.clockMs = 0; s.periodOver = 0; undoN = 0; }

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Point(JsonDocument& c){
    long side, n = 1;
    if (!appOpen || !mtb_Kit_Json_Range(c["point"], 0, 1, side)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["n"].isNull() && !mtb_Kit_Json_Range(c["n"], 1, 3, n)){ mtb_Ble_App_Cmd_Reject(); return; }
    pointN = (int)n; pointSide = (int)side;
}
static void cmd_Undo(JsonDocument&){ if (!appOpen || !undoN){ mtb_Ble_App_Cmd_Reject(); return; } undoRequested = true; }
static void cmd_Clock(JsonDocument& c){
    long v;
    if (!appOpen || !mtb_Kit_Json_Range(c["clock"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    clockRequest = (int)v;
}
static void cmd_New(JsonDocument&){
    if (appOpen) newRequested = true;
    else { portENTER_CRITICAL(&lock); new_Match(save); portEXIT_CRITICAL(&lock); mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
    phoneChanged = true;
}
static void cmd_Sport(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sport"], 0, SB_SPORTS - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (save.sport != (uint8_t)v){
        portENTER_CRITICAL(&lock);
        save.sport = (uint8_t)v;
        if (v == SB_BASKETBALL){ save.minutes = 10; save.periods = 4; }
        else if (v == SB_FOOTBALL){ save.minutes = 45; save.periods = 2; }
        else if (v == SB_BADMINTON) save.bestOf = 3;
        else if (v == SB_TENNIS) save.bestOf = 3;
        else if (v == SB_VOLLEYBALL || v == SB_TABLE_TENNIS) save.bestOf = 5;
        new_Match(save);
        portEXIT_CRITICAL(&lock);
    }
    phoneChanged = true;
}
static void cmd_Side(JsonDocument& c){
    for (int i = 0; i < 2; i++){
        const char* nk = i ? "away" : "home";
        const char* ck = i ? "awayColor" : "homeColor";
        if (c[nk].isNull() && c[ck].isNull()) continue;
        portENTER_CRITICAL(&lock);
        if (!c[nk].isNull()){
            char n[NAME_LEN];
            mtb_Kit_To_Ascii(n, sizeof(n), c[nk] | "");
            mtb_Kit_Upper(n);
            snprintf(save.name[i], NAME_LEN, "%s", n[0] ? n : (i ? "AWAY" : "HOME"));
        }
        if (!c[ck].isNull()){
            const char* v = c[ck] | "";
            char* end = nullptr;
            const unsigned long argb = strtoul(v, &end, 16);
            if (end && end != v) save.argb[i] = (uint32_t)argb | 0xFF000000u;
        }
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 1, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint8_t)v; phoneChanged = true; }
static void cmd_Periods(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["periods"], 1, 4, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.periods = (uint8_t)v; if (save.period >= v) save.period = (uint8_t)(v - 1); phoneChanged = true; }
static void cmd_Best_Of(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["bestOf"], 1, 7, v) || !(v & 1)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.bestOf = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile uint8_t clicks = 0;
static volatile bool longRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicks = (uint8_t)b.count;
    else if (b.type == BUTTON_PRESSED_LONG) longRequested = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 6, 10);

static void clock_Text(char* t, size_t n, const Sb_Save_t& s){
    const uint32_t len = s.minutes * 60000u;
    uint32_t shown = s.sport == SB_BASKETBALL ? (s.clockMs >= len ? 0 : len - s.clockMs) : s.clockMs;
    if (s.sport == SB_BASKETBALL && shown < 60000u) snprintf(t, n, "%lu.%lu", (unsigned long)(shown / 1000), (unsigned long)(shown % 1000 / 100));
    else snprintf(t, n, "%02lu:%02lu", (unsigned long)(shown / 60000), (unsigned long)(shown / 1000 % 60));
}

static void period_Text(char* t, size_t n, const Sb_Save_t& s){
    if (s.sport == SB_FOOTBALL){
        if (s.periods == 2) snprintf(t, n, "%s", s.period ? "2ND HALF" : "1ST HALF");
        else snprintf(t, n, "PERIOD %u", (unsigned)s.period + 1);
    } else snprintf(t, n, "Q%u", (unsigned)s.period + 1);
}

// A big number: PT_Sans 28x34 at twice its size when it fits.
static void big(int cx, int y, const char* t, uint16_t c){
    const int w2 = mtb_Kit_Big_Text_Width(PT_Sans_Narrow_Bold28x34, 2, t);
    if (w2 <= 118) mtb_Kit_Big_Text(cx - w2 / 2, y, PT_Sans_Narrow_Bold28x34, 2, c, t);
    else mtb_Kit_Text_Centre(cx, y + 16, PT_Sans_Narrow_Bold28x34, c, t, C_BG);
}

static void draw(const Sb_Save_t& s, int selected, bool running, uint32_t ms, uint32_t flashUntil, int flashSide){
    mtb_Panel_Fill_Screen(C_BG);
    const bool racket = sb_Racket(s.sport);
    char t[24];
    for (int i = 0; i < 2; i++){
        const int x = i * 130, cx = x + 63;
        const uint16_t c = color_Of(s.argb[i]);
        // The name bar.
        mtb_Panel_Fill_Rect(x, 0, x + 125, 13, c);
        char fit[NAME_LEN];
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, s.name[i], 110);
        mtb_Kit_Text_Centre(cx, 1, Terminal8x12, (s.argb[i] & 0xFF) + ((s.argb[i] >> 8) & 0xFF) + ((s.argb[i] >> 16) & 0xFF) > 500 ? BLACK : KIT_TEXT, fit, c);
        if (selected == i) mtb_Panel_Fill_Rect(x + 20, 15, x + 105, 16, KIT_TEXT);
        // The score.
        if (s.sport == SB_TENNIS) sb_Tennis_Points(s.m, (uint8_t)i, t, sizeof(t));
        else snprintf(t, sizeof(t), "%u", (unsigned)s.m.pts[i]);
        const bool flash = flashSide == i && (int32_t)(flashUntil - ms) > 0 && (ms / 120) % 2;
        big(cx, racket ? 20 : 22, t, flash ? KIT_GOLD : KIT_TEXT);
        if (racket){
            // Who serves, and the sets (and games).
            if (!s.m.winner && s.m.server == i) mtb_Panel_Fill_Circle(i ? x + 120 : x + 6, 48, 3, KIT_GOLD);
            if (s.sport == SB_TENNIS) snprintf(t, sizeof(t), "SETS %u  GAMES %u", s.m.sets[i], s.m.games[i]);
            else snprintf(t, sizeof(t), "%s %u", s.sport == SB_VOLLEYBALL ? "SETS" : "GAMES", s.m.sets[i]);
            mtb_Kit_Text_Centre(cx, 94, Terminal6x8, KIT_LABEL, t, C_BG);
        }
    }
    mtb_Panel_Fill_Rect(127, 18, 128, 104, KIT_LINE);
    // The bottom line.
    if (timed(s.sport)){
        clock_Text(t, sizeof(t), s);
        const bool over = s.periodOver;
        const uint16_t cc = over ? KIT_RED : running ? KIT_GOLD : ((ms / 600) % 2 ? KIT_LABEL : KIT_DIM);
        mtb_Kit_Text_Centre(W / 2, 106, Terminal10x17, cc, t, C_BG);
        char p[24];
        if (over && s.period + 1 >= s.periods) snprintf(p, sizeof(p), "%s", s.sport == SB_FOOTBALL ? "FULL TIME" : "FINAL");
        else period_Text(p, sizeof(p), s);
        mtb_Kit_Text(4, 112, Terminal6x8, KIT_LABEL, p, C_BG);
        if (!running) mtb_Kit_Text_Right(W - 4, 112, Terminal4x6, KIT_DIM, over ? "LONG PRESS: NEXT" : "LONG PRESS: START", C_BG);
    } else if (racket){
        // The finished sets, small.
        char line[64] = "";
        for (int k = 0; k < s.m.setsPlayed && k < SB_SETS_SHOWN; k++)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%u-%u", k ? "  " : "", s.m.setScore[k][0], s.m.setScore[k][1]);
        if (s.m.tiebreak) snprintf(line + strlen(line), sizeof(line) - strlen(line), "%sTIEBREAK", line[0] ? "  " : "");
        if (line[0]) mtb_Kit_Text_Centre(W / 2, 108, Terminal6x8, KIT_LABEL, line, C_BG);
        else { snprintf(t, sizeof(t), "BEST OF %u", s.bestOf); mtb_Kit_Text_Centre(W / 2, 108, Terminal6x8, KIT_DIM, t, C_BG); }
    }
    if (s.m.winner){
        const uint16_t c = color_Of(s.argb[s.m.winner - 1]);
        mtb_Panel_Fill_Rect(20, 40, W - 21, 84, c);
        char w[40];
        snprintf(w, sizeof(w), "%s WINS", s.name[s.m.winner - 1]);
        mtb_Kit_Text_Centre(W / 2, 52, PT_Sans_Narrow_Bold18x22, (s.argb[s.m.winner - 1] & 0xFF) + ((s.argb[s.m.winner - 1] >> 8) & 0xFF) + ((s.argb[s.m.winner - 1] >> 16) & 0xFF) > 500 ? BLACK : KIT_TEXT, w, c);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void beep(int what){
    static const Snd_Note_t POINT[] = {{988, 60}};
    static const Snd_Note_t SET[] = {{784, 120}, {988, 120}, {1175, 240}};
    static const Snd_Note_t WIN[] = {{523, 140}, {659, 140}, {784, 140}, {1047, 420}};
    static const Snd_Note_t BUZZER[] = {{220, 1200}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (what == 0) snd_Play(POINT, 1, 0.15f, false);
    else if (what == 2) snd_Play(SET, 3, 0.25f, false);
    else if (what == 3) snd_Play(WIN, 4, 0.3f, false);
    else if (what == 4) snd_Play(BUZZER, 1, 0.5f, true);
}

static void scoreboard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Point, cmd_Undo, cmd_Clock, cmd_New, cmd_Sport, cmd_Side, cmd_Minutes, cmd_Periods, cmd_Best_Of, cmd_Sound);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clicks = 0; longRequested = false; turns = 0; pointSide = -1; clockRequest = -1; undoRequested = newRequested = false; phoneChanged = false;
    undoN = 0;
    appOpen = true;
    int selected = 0, flashSide = -1;
    bool running = false, push = true, saveNow = false, redraw = true;
    uint32_t lastTick = millis(), lastDraw = 0, pushedMs = 0, flashUntil = 0, savedMs = 0;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = redraw = true; }
        // The knob.
        const int t = turns;
        if (t){ turns -= t; selected = ((selected + t) % 2 + 2) % 2; redraw = true; }
        const uint8_t c = clicks;
        if (c){ clicks = 0; if (c == 1){ pointN = 1; pointSide = selected; } else if (undoN) undoRequested = true; }
        if (longRequested){
            longRequested = false;
            if (timed(save.sport)) clockRequest = running ? 0 : 1;
            else if (save.m.winner) newRequested = true;
        }
        // The requests.
        if (newRequested){ newRequested = false; portENTER_CRITICAL(&lock); new_Match(save); portEXIT_CRITICAL(&lock); running = false; saveNow = push = redraw = true; }
        if (pointSide >= 0){
            const int side = pointSide, n = pointN;
            pointSide = -1;
            if (!save.m.winner){
                if (undoN == UNDO){ memmove(undoStack, undoStack + 1, sizeof(undoStack[0]) * (UNDO - 1)); undoN--; }
                undoStack[undoN++] = save.m;
                portENTER_CRITICAL(&lock);
                const int r = sb_Point(save.m, save.sport, save.bestOf, (uint8_t)side, (uint8_t)n);
                portEXIT_CRITICAL(&lock);
                beep(r >= 2 ? r : 0);
                flashSide = side; flashUntil = ms + 900;
                saveNow = push = redraw = true;
            }
        }
        if (undoRequested){
            undoRequested = false;
            if (undoN){ portENTER_CRITICAL(&lock); save.m = undoStack[--undoN]; portEXIT_CRITICAL(&lock); saveNow = push = redraw = true; }
        }
        if (clockRequest >= 0){
            const int r = clockRequest;
            clockRequest = -1;
            if (timed(save.sport)){
                if (r && save.periodOver){                       // the next period
                    if (save.period + 1 < save.periods){ save.period++; save.clockMs = 0; save.periodOver = 0; running = true; }
                } else if (r && !save.periodOver) running = true;
                else running = false;
                lastTick = ms; saveNow = push = redraw = true;
            }
        }
        // The clock.
        if (running){
            save.clockMs += ms - lastTick;
            const uint32_t len = save.minutes * 60000u;
            if (save.clockMs >= len){ save.clockMs = len; save.periodOver = 1; running = false; beep(4); saveNow = push = true; }
            redraw = true;
            if (ms - savedMs >= 10000u){ savedMs = ms; saveNow = true; }
        }
        lastTick = ms;
        if (flashSide >= 0 && (int32_t)(ms - flashUntil) > 0){ flashSide = -1; redraw = true; }
        const bool blink = timed(save.sport) && !running && ms - lastDraw >= 300u;
        if (redraw || blink || (flashSide >= 0 && ms - lastDraw >= 60u) || (save.m.winner == 0 && ms - lastDraw >= 1000u)){
            redraw = false;
            lastDraw = ms;
            Sb_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, selected, running, ms, flashUntil, flashSide);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 700){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(running || flashSide >= 0 ? 40 : 60);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void sb_Values(JsonObject values){
    Sb_Save_t* s = (Sb_Save_t*)heap_caps_malloc(sizeof(Sb_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    JsonArray rows = values["score"].to<JsonArray>();
    char t[80], a[8], b[8];
    JsonObject o = rows.add<JsonObject>();
    snprintf(t, sizeof(t), "%s - %s", s->name[0], s->name[1]);
    o["label"] = t;
    if (s->sport == SB_TENNIS){ sb_Tennis_Points(s->m, 0, a, sizeof(a)); sb_Tennis_Points(s->m, 1, b, sizeof(b)); snprintf(t, sizeof(t), "%s-%s  (games %u-%u, sets %u-%u)", a, b, s->m.games[0], s->m.games[1], s->m.sets[0], s->m.sets[1]); }
    else if (sb_Racket(s->sport)) snprintf(t, sizeof(t), "%u-%u  (%s %u-%u)", s->m.pts[0], s->m.pts[1], s->sport == SB_VOLLEYBALL ? "sets" : "games", s->m.sets[0], s->m.sets[1]);
    else snprintf(t, sizeof(t), "%u-%u", s->m.pts[0], s->m.pts[1]);
    o["value"] = t;
    o["state"] = s->m.winner ? "ok" : "info";
    if (s->m.winner){ o = rows.add<JsonObject>(); o["label"] = "Winner"; o["value"] = s->name[s->m.winner - 1]; o["state"] = "ok"; }
    if (timed(s->sport)){
        o = rows.add<JsonObject>();
        char p[24], ck[16];
        period_Text(p, sizeof(p), *s);
        clock_Text(ck, sizeof(ck), *s);
        snprintf(t, sizeof(t), "%s, %s%s", p, ck, s->periodOver ? " (time up)" : "");
        o["label"] = "Clock"; o["value"] = t; o["state"] = s->periodOver ? "warn" : "info";
    }
    values["sport"] = s->sport; values["bestOf"] = s->bestOf; values["minutes"] = s->minutes; values["periods"] = s->periods; values["sound"] = s->sound;
    values["home"] = s->name[0]; values["away"] = s->name[1];
    snprintf(t, sizeof(t), "0x%08lX", (unsigned long)s->argb[0]); values["homeColor"] = t;
    snprintf(t, sizeof(t), "0x%08lX", (unsigned long)s->argb[1]); values["awayColor"] = t;
    heap_caps_free(s);
}
