/**
 * Queue Board: a branch's queue display. The ticket just called ("A043") and its counter in large letters, flashing as
 * it is called with a chime and, if wanted, a spoken call ("Ticket A, zero four three. Counter three."). Below, each
 * service line (up to three: a letter and a name) with the number now serving, how many wait and about how long, and
 * along the bottom the last calls.
 *
 * Staff call from the phone (a line, a counter, call next) or at the desk with the knob: turn to choose the counter,
 * double click for its line, click to call the next ticket, hold to call it again. Waiting counts come from the
 * tickets taken so far (typed on the phone); the time from how quickly each line has moved. The numbers start again
 * each day. NVS "queSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "que_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define LINES       3
#define HISTORY     6
#define NVS_KEY     "queSave"
#define SAVE_MAGIC  0x51554501u
#define FLASH_MS    8000

EXT_RAM_BSS_ATTR TaskHandle_t queueBoard_Task_H = NULL;
static void queueBoard_App_Task(void* dApplication);
static void que_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* queueBoard_App;
Mtb_Applications* queueBoard_App_GetInstance(){
    if (!queueBoard_App){
        queueBoard_App = new Mtb_Applications_FullScreen(queueBoard_App_Task, &queueBoard_Task_H, "Queue Board", {0,0}, 8192);
        queueBoard_App->mtb_App_Set_Mobile_UI(QUE_UI_MANIFEST, QUE_UI_API, nullptr, que_Values);
    }
    return queueBoard_App;
}
MTB_REGISTER_APP(queueBoard_App, 0, 0)        // the catalogue gives the app its number

struct Que_Line_t { char letter; char name[20]; uint16_t serving, issued; uint32_t avgS; uint32_t lastCallAt; };     // avgS: seconds a ticket
struct Que_Call_t { uint8_t line; uint16_t number; uint8_t counter; uint32_t at; };

struct Que_Save_t {
    uint32_t magic;
    char place[36];
    Que_Line_t line[LINES];
    uint8_t lines, counters, counter, lineSel, voice;
    Que_Call_t history[HISTORY];        // newest first
    uint8_t historyN;
    int32_t day;                        // the local date the numbers are for
};

static EXT_RAM_BSS_ATTR Que_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile bool cmdCall = false, cmdAgain = false;
static volatile uint32_t calledSeq = 0;

static int32_t today(void){ pxl_time_t t; if (!pxl_time(&t)) return 0; return mtb_Kit_Days_From_Civil(t.year, t.month, t.day); }

static void read_Save(Que_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.counters = 4; s.counter = 1; s.voice = 1;
        s.lines = 2;
        s.line[0].letter = 'A'; snprintf(s.line[0].name, sizeof(s.line[0].name), "Cash");
        s.line[1].letter = 'B'; snprintf(s.line[1].name, sizeof(s.line[1].name), "Customer service");
    }
    if (s.lines > LINES) s.lines = 0;
    if (s.counters < 1 || s.counters > 12) s.counters = 4;
    if (s.counter < 1 || s.counter > s.counters) s.counter = 1;
    if (s.lineSel >= LINES) s.lineSel = 0;
    if (s.historyN > HISTORY) s.historyN = 0;
    s.place[sizeof(s.place) - 1] = '\0';
    for (int i = 0; i < LINES; i++) s.line[i].name[sizeof(s.line[i].name) - 1] = '\0';
}

/** A new day: the numbers start again. Under the lock. */
static void new_Day(Que_Save_t& s, int32_t day){
    for (int i = 0; i < LINES; i++){ s.line[i].serving = 0; s.line[i].issued = 0; s.line[i].lastCallAt = 0; }
    s.historyN = 0;
    s.day = day;
}

static int line_By_Letter(const Que_Save_t& s, char c){
    for (int i = 0; i < s.lines; i++) if (toupper((unsigned char)s.line[i].letter) == toupper((unsigned char)c)) return i;
    return -1;
}

/** The next ticket of @p line, to @p counter. Under the lock. */
static void call_Next(Que_Save_t& s, int line, int counter){
    Que_Line_t& l = s.line[line];
    const uint32_t now = (uint32_t)time(nullptr);
    if (l.lastCallAt && now > l.lastCallAt && now - l.lastCallAt < 3600){             // how quickly the line moves
        const uint32_t gap = now - l.lastCallAt;
        l.avgS = l.avgS ? (l.avgS * 3 + gap) / 4 : gap;
    }
    l.lastCallAt = now;
    l.serving++;
    if (l.issued && l.issued < l.serving) l.issued = l.serving;
    memmove(&s.history[1], &s.history[0], sizeof(s.history[0]) * (HISTORY - 1));
    s.history[0] = {(uint8_t)line, l.serving, (uint8_t)counter, now};
    if (s.historyN < HISTORY) s.historyN++;
    calledSeq++;
}

/** "A 57": the line by its letter, and the number. */
static bool letter_Number(const char* v, const Que_Save_t& s, int& line, long& n){
    if (!v) return false;
    while (*v == ' ') v++;
    line = line_By_Letter(s, *v);
    if (line < 0) return false;
    char* e;
    n = strtol(v + 1, &e, 10);
    while (*e == ' ') e++;
    return !*e && n >= 0 && n <= 9999;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Place(JsonDocument& c){
    const char* v = c["place"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    portENTER_CRITICAL(&lock); snprintf(save.place, sizeof(save.place), "%s", t); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Line(JsonDocument& c){
    for (int i = 0; i < LINES; i++){
        char key[8]; snprintf(key, sizeof(key), "line%d", i);
        if (c[key].isNull()) continue;
        char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), c[key] | "");
        char* p = t; while (*p == ' ') p++;
        size_t n = strlen(p); while (n && p[n - 1] == ' ') p[--n] = '\0';
        portENTER_CRITICAL(&lock);
        if (!n){
            if (i < save.lines){ memmove(&save.line[i], &save.line[i + 1], sizeof(save.line[0]) * (LINES - 1 - i)); memset(&save.line[LINES - 1], 0, sizeof(save.line[0])); save.lines--; }
        } else {
            if (!isalpha((unsigned char)p[0]) || (p[1] && p[1] != ' ')){ portEXIT_CRITICAL(&lock); mtb_Ble_App_Cmd_Reject(); return; }
            const int at = i < save.lines ? i : save.lines;
            if (at >= save.lines){ memset(&save.line[at], 0, sizeof(save.line[0])); save.lines++; }
            save.line[at].letter = (char)toupper((unsigned char)p[0]);
            const char* name = p + 1; while (*name == ' ') name++;
            snprintf(save.line[at].name, sizeof(save.line[at].name), "%s", name);
        }
        if (save.lineSel >= save.lines) save.lineSel = 0;
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Counters(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["counters"], 1, 12, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.counters = (uint8_t)v; if (save.counter > v) save.counter = (uint8_t)v; phoneChanged = true; }
static void cmd_Counter(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["counter"], 1, 12, v) || v > save.counters){ mtb_Ble_App_Cmd_Reject(); return; } save.counter = (uint8_t)v; phoneChanged = true; }
static void cmd_LineSel(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["line"], 0, LINES - 1, v) || v >= save.lines){ mtb_Ble_App_Cmd_Reject(); return; } save.lineSel = (uint8_t)v; phoneChanged = true; }
static void cmd_Call(JsonDocument&){ if (!appOpen || !save.lines){ mtb_Ble_App_Cmd_Reject(); return; } cmdCall = true; }
static void cmd_Again(JsonDocument&){ if (!appOpen || !save.historyN){ mtb_Ble_App_Cmd_Reject(); return; } cmdAgain = true; }
static void cmd_Issued(JsonDocument& c){
    int line; long n;
    if (!letter_Number(c["issued"] | (const char*)nullptr, save, line, n)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.line[line].issued = (uint16_t)n; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Voice(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["voice"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.voice = (uint8_t)v; phoneChanged = true; }
static void cmd_NewDay(JsonDocument&){ portENTER_CRITICAL(&lock); new_Day(save, today()); portEXIT_CRITICAL(&lock); phoneChanged = true; }
static void cmd_Serve(JsonDocument& c){
    int line; long n;
    if (!letter_Number(c["serve"] | (const char*)nullptr, save, line, n)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.line[line].serving = (uint16_t)n; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobDouble = false, knobHold = false;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED){ if (b.count >= 2) knobDouble = true; else knobClick = true; }
    else if (b.type == BUTTON_PRESSED_LONG) knobHold = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 10, 24), C_HEAD = MTB_KIT_RGB(12, 24, 52), C_ROW = MTB_KIT_RGB(10, 18, 38);
static const uint16_t LINE_COLOR[LINES] = {MTB_KIT_RGB(40, 200, 120), MTB_KIT_RGB(80, 160, 255), MTB_KIT_RGB(255, 170, 40)};

static void ticket(char* t, size_t n, const Que_Save_t& s, int line, int number){ snprintf(t, n, "%c%03d", s.line[line].letter, number); }

static void draw(const Que_Save_t& s, uint32_t ms, uint32_t flashSince, bool showDesk){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], fit[64];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.place[0] ? s.place : "PLEASE WAIT TO BE CALLED");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 46);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, fit, C_HEAD);
    pxl_time_t pt;
    if (pxl_time(&pt)){ snprintf(t, sizeof(t), "%02d:%02d", (int)pt.hour, (int)pt.minute); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD); }
    if (!s.lines){
        mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, "NO LINES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 68, Terminal4x6, KIT_LABEL, "SET THEM IN THE PHONE APP: A CASH", C_BG);
        return;
    }
    // The call: the ticket and the counter, flashing while it is new.
    const int top = 12, callH = 46;
    if (s.historyN){
        const Que_Call_t& c = s.history[0];
        const bool fresh = ms - flashSince < FLASH_MS;
        const bool on = fresh && (ms / 400) % 2;
        const uint16_t lc = LINE_COLOR[c.line < LINES ? c.line : 0];
        const uint16_t bg = on ? lc : C_BG;
        const uint16_t fg = on ? BLACK : KIT_TEXT;
        mtb_Panel_Fill_Rect(0, top, W - 1, top + callH - 1, bg);
        ticket(t, sizeof(t), s, c.line, c.number);
        mtb_Kit_Text(8, top + 6, PT_Sans_Narrow_Bold28x34, on ? BLACK : lc, t, bg);
        snprintf(t, sizeof(t), "%u", (unsigned)c.counter);
        const int nw = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, t);
        mtb_Kit_Text_Right(W - 8, top + 6, PT_Sans_Narrow_Bold28x34, fg, t, bg);
        mtb_Kit_Text_Right((int16_t)(W - 14 - nw), top + 18, Terminal6x8, fg, "COUNTER", bg);
        // An arrow from the ticket to the counter.
        const int ax = 8 + mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, "A000") + 10, bx = W - 14 - nw - 48 - 8, ay = top + 22;
        if (bx - ax > 12){
            mtb_Panel_Fill_Rect((int16_t)ax, (int16_t)(ay - 1), (int16_t)(bx - 6), (int16_t)(ay + 1), fg);
            mtb_Panel_Fill_Triangle((int16_t)(bx - 7), (int16_t)(ay - 5), (int16_t)bx, (int16_t)ay, (int16_t)(bx - 7), (int16_t)(ay + 5), fg);
        }
    } else {
        mtb_Kit_Text_Centre(W / 2, top + 12, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "WELCOME", C_BG);
    }
    // The lines.
    const int y0 = top + callH + 2;
    const int rowH = s.lines == 1 ? 26 : s.lines == 2 ? 22 : 17;
    for (int i = 0; i < s.lines; i++){
        const Que_Line_t& l = s.line[i];
        const int y = y0 + i * (rowH + 1);
        mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 1), C_ROW);
        mtb_Panel_Fill_Rect(0, (int16_t)y, 14, (int16_t)(y + rowH - 1), LINE_COLOR[i]);
        char letter[2] = {l.letter, 0};
        mtb_Kit_Text_Centre(7, (int16_t)(y + rowH / 2 - 4), Terminal6x8, BLACK, letter, LINE_COLOR[i]);
        char nm[24]; snprintf(nm, sizeof(nm), "%s", l.name); mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, 96);
        mtb_Kit_Text(19, (int16_t)(y + rowH / 2 - 4), Terminal6x8, KIT_TEXT, fit, C_ROW);
        if (l.serving){ ticket(t, sizeof(t), s, i, l.serving); } else snprintf(t, sizeof(t), "-");
        mtb_Kit_Text(120, (int16_t)(y + rowH / 2 - 6), Terminal8x12, LINE_COLOR[i], t, C_ROW);
        if (l.issued > l.serving){
            const unsigned wait = l.issued - l.serving;
            if (l.avgS){ const unsigned mins = (wait * l.avgS + 59) / 60; snprintf(t, sizeof(t), "%u WAITING  ~%u MIN", wait, mins < 1 ? 1 : mins); }
            else snprintf(t, sizeof(t), "%u WAITING", wait);
            mtb_Kit_Text_Right(W - 4, (int16_t)(y + rowH / 2 - 3), Terminal4x6, KIT_LABEL, t, C_ROW);
        }
    }
    // The last calls, or this desk's counter while it is being chosen.
    if (showDesk){
        snprintf(t, sizeof(t), "THIS DESK: COUNTER %u, LINE %c   CLICK TO CALL", (unsigned)s.counter, s.line[s.lineSel < s.lines ? s.lineSel : 0].letter);
        mtb_Panel_Fill_Rect(0, H - 9, W - 1, H - 1, MTB_KIT_RGB(60, 50, 0));
        mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_GOLD, t, MTB_KIT_RGB(60, 50, 0));
    } else if (s.historyN > 1){
        char line[96] = "BEFORE:";
        for (int k = 1; k < s.historyN; k++){
            char one[16]; ticket(one, sizeof(one), s, s.history[k].line, s.history[k].number);
            char seg[24]; snprintf(seg, sizeof(seg), "  %s>%u", one, (unsigned)s.history[k].counter);
            strlcat(line, seg, sizeof(line));
        }
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, line, W - 8);
        mtb_Kit_Text(4, H - 7, Terminal4x6, KIT_DIM, fit, C_BG);
    }
}

// The call, aloud ---------------------------------------------------------------------------------------------------------------------
static void spoken(const Que_Save_t& s, const Que_Call_t& c, char* out, size_t n){
    static const char* const DIG[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    char num[8]; snprintf(num, sizeof(num), "%03u", (unsigned)c.number);
    snprintf(out, n, "Ticket %c, %s %s %s. Counter %u.", s.line[c.line].letter, DIG[num[0] - '0'], DIG[num[1] - '0'], DIG[num[2] - '0'], (unsigned)c.counter);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void queueBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Place, cmd_Line, cmd_Counters, cmd_Counter, cmd_LineSel, cmd_Call, cmd_Again, cmd_Issued, cmd_Voice, cmd_NewDay, cmd_Serve);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; knobClick = knobDouble = knobHold = false; cmdCall = cmdAgain = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, flashSince = 0, deskUntil = 0, seenSeq = calledSeq;
    uint8_t announce = 0;               // 1: chime playing, 2: then speak
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int32_t day = today();
        portENTER_CRITICAL(&lock);
        if (day && day != save.day){ new_Day(save, day); saveNow = push = true; }
        const int t = turns;
        if (t){ turns -= t; save.counter = (uint8_t)(((int)save.counter - 1 + t) % save.counters + save.counters) % save.counters + 1; deskUntil = ms + 6000; lastDraw = 0; saveNow = push = true; }
        if (knobDouble && save.lines){ save.lineSel = (uint8_t)((save.lineSel + 1) % save.lines); deskUntil = ms + 6000; lastDraw = 0; saveNow = push = true; }
        const bool call = (knobClick || cmdCall) && save.lines;
        const bool again = (knobHold || cmdAgain) && save.historyN;
        knobClick = knobDouble = knobHold = false; cmdCall = cmdAgain = false;
        if (call) call_Next(save, save.lineSel < save.lines ? save.lineSel : 0, save.counter);
        portEXIT_CRITICAL(&lock);
        if (call || again){ flashSince = ms; announce = 1; saveNow = push = true; lastDraw = 0; seenSeq = calledSeq; }
        else if (calledSeq != seenSeq){ seenSeq = calledSeq; flashSince = ms; announce = 1; lastDraw = 0; }
        // The chime, then (when it has finished) the spoken call.
        if (announce == 1){
            static const Snd_Note_t DING[] = {{784, 220}, {0, 40}, {1047, 420}};
            if (!mtb_Silent_Mode()) snd_Play(DING, 3, 0.3f, false);
            announce = 2;
        } else if (announce == 2 && !snd_Busy()){
            announce = 0;
            if (save.voice && save.historyN && !mtb_Silent_Mode()){ char words[96]; spoken(save, save.history[0], words, sizeof(words)); snd_Say(words, "en", nullptr, 0, 0, false); }
        }
        if (ms - lastDraw >= (ms - flashSince < FLASH_MS ? 100u : 1000u)){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(save, ms, flashSince, (int32_t)(deskUntil - ms) > 0);
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
static void que_Values(JsonObject values){
    Que_Save_t* s = (Que_Save_t*)heap_caps_malloc(sizeof(Que_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    char t[96], key[8];
    values["place"] = s->place;
    for (int i = 0; i < LINES; i++){
        snprintf(key, sizeof(key), "line%d", i);
        if (i < s->lines){ snprintf(t, sizeof(t), "%c %s", s->line[i].letter, s->line[i].name); values[key] = t; } else values[key] = "";
    }
    values["counters"] = s->counters;
    values["counter"] = s->counter;
    values["line"] = s->lineSel;
    values["voice"] = s->voice;
    JsonArray rows = values["queue"].to<JsonArray>();
    if (s->historyN){
        JsonObject r = rows.add<JsonObject>();
        r["label"] = "Last call";
        char tk[8]; ticket(tk, sizeof(tk), *s, s->history[0].line, s->history[0].number);
        snprintf(t, sizeof(t), "%s to counter %u", tk, (unsigned)s->history[0].counter);
        r["value"] = t; r["state"] = "ok";
    }
    for (int i = 0; i < s->lines; i++){
        const Que_Line_t& l = s->line[i];
        JsonObject r = rows.add<JsonObject>();
        snprintf(t, sizeof(t), "Line %d: %c %s", i + 1, l.letter, l.name);
        r["label"] = t;
        if (l.issued > l.serving) snprintf(t, sizeof(t), "Serving %c%03u, %u waiting", l.letter, (unsigned)l.serving, (unsigned)(l.issued - l.serving));
        else snprintf(t, sizeof(t), l.serving ? "Serving %c%03u" : "Not started", l.letter, (unsigned)l.serving);
        r["value"] = t;
        r["state"] = "info";
    }
    heap_caps_free(s);
}
