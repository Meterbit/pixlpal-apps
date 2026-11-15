/**
 * Stand-up Host: runs a team's daily stand-up. The team's names (up to 12) come from the phone; each stand-up gets a
 * new order (or keeps the written one), every person the same time with a warning near its end, and a summary at the
 * end: how long it took, each person's time, who went over (red) and who was away.
 *
 * Knob: click to start and for the next person; before the start, turn for a new order; while someone speaks, turn
 * for 15 s more or less; double click to skip someone (away today); hold to end early. NVS "sthSave": the team and
 * the timing. Works without the internet.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "sth_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>

#define W           256
#define H           128
#define PEOPLE      12
#define NAME_LEN    20
#define NVS_KEY     "sthSave"
#define SAVE_MAGIC  0x53544801u
#define STEP_MS     15000

EXT_RAM_BSS_ATTR TaskHandle_t standupHost_Task_H = NULL;
static void standupHost_App_Task(void* dApplication);
static void sth_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* standupHost_App;
Mtb_Applications* standupHost_App_GetInstance(){
    if (!standupHost_App){
        standupHost_App = new Mtb_Applications_FullScreen(standupHost_App_Task, &standupHost_Task_H, "Stand-up Host", {0,0}, 8192);
        standupHost_App->mtb_App_Set_Mobile_UI(STH_UI_MANIFEST, STH_UI_API, nullptr, sth_Values);
    }
    return standupHost_App;
}
MTB_REGISTER_APP(standupHost_App, 0, 0)       // the catalogue gives the app its number

struct Sth_Save_t {
    uint32_t magic;
    char name[PEOPLE][NAME_LEN];
    uint8_t count;
    uint16_t seconds;
    uint8_t warn, sound, shuffle;
};

enum : uint8_t { P_IDLE = 0, P_TALK, P_DONE };

struct Sth_Meeting_t {
    uint8_t phase;
    uint8_t order[PEOPLE], count;       // indices into the names, in speaking order
    uint8_t cur;                        // the position in order[] speaking now
    uint32_t meetingStartMs, turnStartMs, meetingMs;
    int32_t extraMs[PEOPLE];            // time given or taken with the knob
    uint32_t spentMs[PEOPLE];           // by position
    bool away[PEOPLE], unreached[PEOPLE], warned, overSounded;
    uint32_t version;
};

static EXT_RAM_BSS_ATTR Sth_Save_t save;
static EXT_RAM_BSS_ATTR Sth_Meeting_t mt;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile bool cmdNext = false, cmdSkip = false, cmdEnd = false;

static void read_Save(Sth_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.seconds = 120; s.warn = 15; s.sound = 1; s.shuffle = 1; }
    if (s.seconds < 30 || s.seconds > 300) s.seconds = 120;
    if (s.warn < 5 || s.warn > 60) s.warn = 15;
    if (s.count > PEOPLE) s.count = 0;
    for (int i = 0; i < PEOPLE; i++) s.name[i][NAME_LEN - 1] = '\0';
}

/** "1:05", or "+0:12" over. */
static void fmt_Clock(char* t, size_t n, int32_t ms, bool sign){
    const bool neg = ms < 0;
    uint32_t s = (uint32_t)((neg ? -ms : ms) + (neg ? 0 : 999)) / 1000;
    if (neg && sign) snprintf(t, n, "+%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else snprintf(t, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void new_Order(Sth_Meeting_t& m, const Sth_Save_t& s, bool shuffle){
    m.count = s.count;
    for (int i = 0; i < m.count; i++) m.order[i] = (uint8_t)i;
    if (shuffle) for (int i = m.count - 1; i > 0; i--){
        const int j = (int)(esp_random() % (uint32_t)(i + 1));
        const uint8_t t = m.order[i]; m.order[i] = m.order[j]; m.order[j] = t;
    }
}

static void reset_Meeting(bool shuffle){
    portENTER_CRITICAL(&lock);
    memset(&mt.extraMs, 0, sizeof(mt.extraMs));
    memset(&mt.spentMs, 0, sizeof(mt.spentMs));
    memset(&mt.away, 0, sizeof(mt.away));
    memset(&mt.unreached, 0, sizeof(mt.unreached));
    mt.phase = P_IDLE; mt.cur = 0; mt.meetingMs = 0;
    new_Order(mt, save, shuffle);
    mt.version++;
    portEXIT_CRITICAL(&lock);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Names(JsonDocument& c){
    const char* v = c["names"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char names[PEOPLE][NAME_LEN];
    int n = 0;
    const char* p = v;
    while (*p){
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '\n') p++;
        if (!*p) break;
        const char* e = p;
        while (*e && *e != ',' && *e != ';' && *e != '\n') e++;
        size_t len = (size_t)(e - p);
        while (len && p[len - 1] == ' ') len--;
        if (len){
            if (n == PEOPLE){ mtb_Ble_App_Cmd_Reject(); return; }
            char raw[64];
            snprintf(raw, sizeof(raw), "%.*s", (int)(len < 60 ? len : 60), p);
            mtb_Kit_To_Ascii(names[n], NAME_LEN, raw);
            names[n][NAME_LEN - 1] = '\0';
            n++;
        }
        p = e;
    }
    if (n == 0){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    memset(save.name, 0, sizeof(save.name));
    memcpy(save.name, names, sizeof(names[0]) * n);
    save.count = (uint8_t)n;
    portEXIT_CRITICAL(&lock);
    if (mt.phase != P_TALK) reset_Meeting(save.shuffle);
    phoneChanged = true;
}
static void cmd_Seconds(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["seconds"], 30, 300, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.seconds = (uint16_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Warn(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["warn"], 5, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.warn = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Shuffle(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["shuffle"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.shuffle = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Next(JsonDocument&){ if (!appOpen || !save.count){ mtb_Ble_App_Cmd_Reject(); return; } cmdNext = true; }
static void cmd_Skip(JsonDocument&){ if (!appOpen || mt.phase != P_TALK){ mtb_Ble_App_Cmd_Reject(); return; } cmdSkip = true; }
static void cmd_End(JsonDocument&){ if (!appOpen || mt.phase != P_TALK){ mtb_Ble_App_Cmd_Reject(); return; } cmdEnd = true; }

// Knob ---------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobDouble = false, knobHold = false;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED){ if (b.count >= 2) knobDouble = true; else knobClick = true; }
    else if (b.type == BUTTON_PRESSED_LONG) knobHold = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The meeting ----------------------------------------------------------------------------------------------------------------------
static void beep(int kind){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t WARN[] = {{988, 70}};
    static const Snd_Note_t OVER[] = {{659, 150}, {0, 70}, {523, 220}};
    static const Snd_Note_t DONE[] = {{523, 110}, {659, 110}, {784, 110}, {1047, 260}};
    static const Snd_Note_t NEXT[] = {{784, 60}};
    if (kind == 0) snd_Play(WARN, 1, 0.25f, false);
    else if (kind == 1) snd_Play(OVER, 3, 0.3f, false);
    else if (kind == 2) snd_Play(DONE, 4, 0.3f, false);
    else snd_Play(NEXT, 1, 0.2f, false);
}

/** The person speaking's time left (negative: over). */
static int32_t left_Ms(const Sth_Meeting_t& m, const Sth_Save_t& s, uint32_t now){
    return (int32_t)s.seconds * 1000 + m.extraMs[m.cur] - (int32_t)(now - m.turnStartMs);
}

// A skip in the first 10 s is someone away today; later, it only ends their turn.
static void finish_Turn(uint32_t now, bool away){
    if (away && now - mt.turnStartMs < 10000){ mt.away[mt.cur] = true; mt.spentMs[mt.cur] = 0; }
    else mt.spentMs[mt.cur] = now - mt.turnStartMs;
}

/** To the next person who isn't away, or the end. */
static void advance(uint32_t now, bool away){
    portENTER_CRITICAL(&lock);
    finish_Turn(now, away);
    if (mt.cur + 1 < mt.count){
        mt.cur++;
        mt.turnStartMs = now;
        mt.warned = mt.overSounded = false;
    } else {
        mt.phase = P_DONE;
        mt.meetingMs = now - mt.meetingStartMs;
    }
    mt.version++;
    portEXIT_CRITICAL(&lock);
    beep(mt.phase == P_DONE ? 2 : 3);
}

static void end_Now(uint32_t now){
    portENTER_CRITICAL(&lock);
    finish_Turn(now, false);
    for (int i = mt.cur + 1; i < mt.count; i++) mt.unreached[i] = true;
    mt.phase = P_DONE;
    mt.meetingMs = now - mt.meetingStartMs;
    mt.version++;
    portEXIT_CRITICAL(&lock);
    beep(2);
}

static void start(uint32_t now){
    portENTER_CRITICAL(&lock);
    if (mt.count != save.count) new_Order(mt, save, save.shuffle);
    memset(&mt.extraMs, 0, sizeof(mt.extraMs));
    memset(&mt.spentMs, 0, sizeof(mt.spentMs));
    memset(&mt.away, 0, sizeof(mt.away));
    memset(&mt.unreached, 0, sizeof(mt.unreached));
    mt.phase = P_TALK; mt.cur = 0;
    mt.meetingStartMs = mt.turnStartMs = now;
    mt.warned = mt.overSounded = false;
    mt.version++;
    portEXIT_CRITICAL(&lock);
    beep(3);
}

// Drawing ---------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 14), C_BAR = MTB_KIT_RGB(16, 20, 32);

static void header(const char* left, const char* right){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_BAR);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, left, C_BAR);
    if (right) mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_LABEL, right, C_BAR);
}

static void draw_Idle(const Sth_Meeting_t& m, const Sth_Save_t& s){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], each[12];
    fmt_Clock(each, sizeof(each), (int32_t)s.seconds * 1000, false);
    if (!s.count){
        header("STAND-UP", nullptr);
        mtb_Kit_Text_Centre(W / 2, 44, Terminal10x17, KIT_TEXT, "STAND-UP HOST", C_BG);
        mtb_Kit_Text_Centre(W / 2, 70, Terminal6x8, KIT_LABEL, "WRITE THE TEAM'S NAMES", C_BG);
        mtb_Kit_Text_Centre(W / 2, 82, Terminal6x8, KIT_LABEL, "IN THE PHONE APP", C_BG);
        return;
    }
    const uint32_t total = (uint32_t)s.count * s.seconds;
    snprintf(t, sizeof(t), "%u PEOPLE  %s EACH  ABOUT %lu MIN", (unsigned)s.count, each, (unsigned long)((total + 59) / 60));
    header("STAND-UP", t);
    const int rows = m.count > 6 ? (m.count + 1) / 2 : m.count;
    for (int k = 0; k < m.count; k++){
        const int col = k / (rows ? rows : 1), row = k % (rows ? rows : 1);
        const int x = 8 + col * 124, y = 16 + row * 14;
        char line[32], fit[32];
        snprintf(line, sizeof(line), "%s", s.name[m.order[k]]);
        mtb_Kit_Upper(line);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, line, 96);
        snprintf(t, sizeof(t), "%d", k + 1);
        mtb_Kit_Text_Right(x + 12, y, Terminal6x8, KIT_CYAN, t, C_BG);
        mtb_Kit_Text(x + 18, y, Terminal6x8, KIT_TEXT, fit, C_BG);
    }
    mtb_Panel_Fill_Rect(0, 116, W - 1, 127, C_BAR);
    mtb_Kit_Text_Centre(W / 2, 119, Terminal4x6, KIT_LABEL, "CLICK TO START   TURN FOR A NEW ORDER", C_BAR);
}

static void draw_Talk(const Sth_Meeting_t& m, const Sth_Save_t& s, uint32_t now){
    const int32_t left = left_Ms(m, s, now);
    const bool over = left < 0, warn = !over && left <= (int32_t)s.warn * 1000;
    const uint16_t bg = over ? ((now / 500) % 2 ? MTB_KIT_RGB(60, 8, 10) : MTB_KIT_RGB(40, 4, 6)) : C_BG;
    mtb_Panel_Fill_Screen(bg);
    char t[40], el[12];
    snprintf(t, sizeof(t), "%u OF %u", (unsigned)(m.cur + 1), (unsigned)m.count);
    fmt_Clock(el, sizeof(el), (int32_t)(now - m.meetingStartMs), false);
    char right[24];
    snprintf(right, sizeof(right), "MEETING %s", el);
    header(t, right);

    char name[NAME_LEN + 4], fit[NAME_LEN + 4];
    snprintf(name, sizeof(name), "%s", s.name[m.order[m.cur]]);
    mtb_Kit_Upper(name);
    if (mtb_Kit_Text_Width(PT_Sans_Narrow38x34, name) <= W - 8) mtb_Kit_Text_Centre(W / 2, 13, PT_Sans_Narrow38x34, KIT_TEXT, name, bg);
    else {
        mtb_Kit_Fit(fit, sizeof(fit), Terminal10x17, name, W - 8);
        mtb_Kit_Text_Centre(W / 2, 20, Terminal10x17, KIT_TEXT, fit, bg);
    }

    fmt_Clock(t, sizeof(t), left, true);
    const uint16_t tc = over ? KIT_RED : warn ? KIT_GOLD : KIT_GREEN;
    mtb_Kit_Text_Centre(W / 2, 48, MS_Gothic31x60, tc, t, bg);      // digits, ':' and '+' (it has 0x20 to 0x3C)

    const int32_t turn = (int32_t)s.seconds * 1000 + m.extraMs[m.cur];
    const float f = turn > 0 ? (float)(now - m.turnStartMs) / (float)turn : 1.0f;
    mtb_Kit_Bar(4, 110, W - 8, 4, f > 1 ? 1 : f, tc, KIT_LINE);
    int nx = m.cur + 1;
    if (nx < m.count){
        char nn[NAME_LEN + 8];
        snprintf(nn, sizeof(nn), "NEXT: %s", s.name[m.order[nx]]);
        mtb_Kit_Upper(nn);
        mtb_Kit_Text(4, 119, Terminal4x6, KIT_LABEL, nn, bg);
    } else mtb_Kit_Text(4, 119, Terminal4x6, KIT_LABEL, "LAST ONE", bg);
    if (m.extraMs[m.cur]){
        snprintf(t, sizeof(t), "%+lds", (long)(m.extraMs[m.cur] / 1000));
        mtb_Kit_Text_Right(W - 4, 119, Terminal4x6, KIT_CYAN, t, bg);
    }
}

static void draw_Done(const Sth_Meeting_t& m, const Sth_Save_t& s){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48], c[12];
    fmt_Clock(c, sizeof(c), (int32_t)m.meetingMs, false);
    int overN = 0;
    for (int k = 0; k < m.count; k++) if (!m.away[k] && !m.unreached[k] && m.spentMs[k] > (uint32_t)((int32_t)s.seconds * 1000 + m.extraMs[k])) overN++;
    snprintf(t, sizeof(t), overN ? "%d WENT OVER" : "NOBODY WENT OVER", overN);
    header("STAND-UP DONE", t);
    mtb_Kit_Text(4, 14, Terminal10x17, KIT_TEXT, c, C_BG);
    mtb_Kit_Text(4 + mtb_Kit_Text_Width(Terminal10x17, c) + 6, 21, Terminal6x8, KIT_LABEL, "IN ALL", C_BG);
    const int rows = m.count > 5 ? (m.count + 1) / 2 : m.count;
    for (int k = 0; k < m.count; k++){
        const int col = k / (rows ? rows : 1), row = k % (rows ? rows : 1);
        const int x = 4 + col * 130, y = 36 + row * 13;
        char nm[NAME_LEN + 2], fit[NAME_LEN + 2];
        snprintf(nm, sizeof(nm), "%s", s.name[m.order[k]]);
        mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, 78);
        const bool off = m.away[k] || m.unreached[k];
        const bool over = !off && m.spentMs[k] > (uint32_t)((int32_t)s.seconds * 1000 + m.extraMs[k]);
        mtb_Kit_Text(x, y, Terminal6x8, off ? KIT_DIM : KIT_TEXT, fit, C_BG);
        if (m.away[k]) snprintf(t, sizeof(t), "AWAY");
        else if (m.unreached[k]) snprintf(t, sizeof(t), "-");
        else fmt_Clock(t, sizeof(t), (int32_t)m.spentMs[k], false);
        mtb_Kit_Text_Right(x + 116, y, Terminal6x8, off ? KIT_DIM : over ? KIT_RED : KIT_GREEN, t, C_BG);
    }
    mtb_Panel_Fill_Rect(0, 116, W - 1, 127, C_BAR);
    mtb_Kit_Text_Centre(W / 2, 119, Terminal4x6, KIT_LABEL, "CLICK FOR A NEW STAND-UP", C_BAR);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void standupHost_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Names, cmd_Seconds, cmd_Warn, cmd_Sound, cmd_Shuffle, cmd_Next, cmd_Skip, cmd_End);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    memset(&mt, 0, sizeof(mt));
    reset_Meeting(save.shuffle);
    turns = 0; knobClick = knobDouble = knobHold = false; cmdNext = cmdSkip = cmdEnd = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, version = 0xFFFFFFFF;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; }
        const int t = turns;
        if (t){
            turns -= t;
            if (mt.phase == P_IDLE && save.count > 1){ reset_Meeting(true); beep(3); }
            else if (mt.phase == P_TALK){
                portENTER_CRITICAL(&lock);
                int32_t e = mt.extraMs[mt.cur] + t * STEP_MS;
                const int32_t floor = -(int32_t)save.seconds * 1000 + 15000;
                if (e < floor) e = floor;
                if (e > 600000) e = 600000;
                mt.extraMs[mt.cur] = e;
                if (left_Ms(mt, save, now) > (int32_t)save.warn * 1000) mt.warned = false;
                if (left_Ms(mt, save, now) >= 0) mt.overSounded = false;
                mt.version++;
                portEXIT_CRITICAL(&lock);
            }
        }
        const bool next = knobClick || cmdNext, skip = knobDouble || cmdSkip, end = knobHold || cmdEnd;
        knobClick = knobDouble = knobHold = false; cmdNext = cmdSkip = cmdEnd = false;
        if (next){
            if (mt.phase == P_TALK) advance(now, false);
            else if (save.count){ if (mt.phase == P_DONE) reset_Meeting(save.shuffle); start(now); }
        } else if (skip && mt.phase == P_TALK) advance(now, true);
        else if (end && mt.phase == P_TALK) end_Now(now);

        if (mt.phase == P_TALK){
            const int32_t left = left_Ms(mt, save, now);
            if (!mt.warned && left <= (int32_t)save.warn * 1000 && left > 0){ mt.warned = true; beep(0); }
            if (!mt.overSounded && left <= 0){ mt.overSounded = true; beep(1); push = true; }
        }
        if (mt.version != version){ version = mt.version; push = true; lastDraw = 0; }

        if (now - lastDraw >= (mt.phase == P_TALK ? 100u : 500u)){
            lastDraw = now;
            Sth_Save_t s;
            Sth_Meeting_t m;
            portENTER_CRITICAL(&lock); s = save; m = mt; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            if (m.phase == P_TALK) draw_Talk(m, s, now);
            else if (m.phase == P_DONE) draw_Done(m, s);
            else draw_Idle(m, s);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && now - pushedMs >= 1000){ push = false; pushedMs = now; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void sth_Values(JsonObject values){
    Sth_Save_t* s = (Sth_Save_t*)heap_caps_malloc(sizeof(Sth_Save_t), MALLOC_CAP_SPIRAM);
    Sth_Meeting_t* m = (Sth_Meeting_t*)heap_caps_malloc(sizeof(Sth_Meeting_t), MALLOC_CAP_SPIRAM);
    if (!s || !m){ heap_caps_free(s); heap_caps_free(m); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *m = mt; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(m, 0, sizeof(*m)); new_Order(*m, *s, false); }
    char names[PEOPLE * (NAME_LEN + 2)] = "";
    for (int i = 0; i < s->count; i++){ if (i) strlcat(names, ", ", sizeof(names)); strlcat(names, s->name[i], sizeof(names)); }
    values["names"] = names;
    values["seconds"] = s->seconds;
    values["warn"] = s->warn;
    values["sound"] = s->sound;
    values["shuffle"] = s->shuffle;

    const uint32_t now = millis();
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    char t[64], c[12];
    if (!appOpen){ r["label"] = "Stand-up"; r["value"] = "Open Stand-up Host on Pixlpal to run one"; r["state"] = "off"; }
    else if (m->phase == P_TALK){
        fmt_Clock(c, sizeof(c), left_Ms(*m, *s, now), true);
        r["label"] = s->name[m->order[m->cur]];
        snprintf(t, sizeof(t), "Speaking, %s %s (%u of %u)", c, left_Ms(*m, *s, now) < 0 ? "over" : "left", (unsigned)(m->cur + 1), (unsigned)m->count);
        r["value"] = t;
        r["state"] = left_Ms(*m, *s, now) < 0 ? "error" : "ok";
    } else if (m->phase == P_DONE){
        fmt_Clock(c, sizeof(c), (int32_t)m->meetingMs, false);
        snprintf(t, sizeof(t), "Done in %s", c);
        r["label"] = "Stand-up"; r["value"] = t; r["state"] = "info";
    } else { r["label"] = "Stand-up"; r["value"] = s->count ? "Ready: click the knob or Start" : "Write the team's names below"; r["state"] = "info"; }

    JsonArray order = values["order"].to<JsonArray>();
    for (int k = 0; k < m->count; k++){
        JsonObject o = order.add<JsonObject>();
        snprintf(t, sizeof(t), "%d. %s", k + 1, s->name[m->order[k]]);
        o["label"] = t;
        if (m->phase == P_IDLE){ o["value"] = "Waiting"; o["state"] = "off"; continue; }
        if (m->away[k]){ o["value"] = "Away"; o["state"] = "off"; continue; }
        if (m->unreached[k]){ o["value"] = "Not reached"; o["state"] = "off"; continue; }
        if (m->phase == P_TALK && k == m->cur){ o["value"] = "Speaking now"; o["state"] = "ok"; continue; }
        if (m->phase == P_TALK && k > m->cur){ o["value"] = "Waiting"; o["state"] = "off"; continue; }
        fmt_Clock(c, sizeof(c), (int32_t)m->spentMs[k], false);
        const bool over = m->spentMs[k] > (uint32_t)((int32_t)s->seconds * 1000 + m->extraMs[k]);
        snprintf(t, sizeof(t), "%s%s", c, over ? ", went over" : "");
        o["value"] = t;
        o["state"] = over ? "error" : "ok";
    }
    heap_caps_free(s);
    heap_caps_free(m);
}
