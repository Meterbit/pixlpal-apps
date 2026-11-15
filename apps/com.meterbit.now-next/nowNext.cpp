/**
 * Now & Next: a picture routine. What we are doing now fills the top: a large picture, its name, the minutes left and
 * a bar that shrinks as the time goes. What comes next is below, smaller, with dots for the steps done and to come.
 * When a step's time is up a gentle chime plays and "TIME FOR ..." shows the next picture; the routine moves on when
 * the teacher clicks (or by itself, if chosen). After the last step: "ALL DONE!".
 *
 * Up to ten steps from the phone ("Reading 15"); the picture follows the name (nxt_Icons.h). Knob: click to start and
 * for the next step, turn back or forward, hold to start again. NVS "nxtSave". Works without the internet.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_image.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "nxt_Icons.h"
#include "nxt_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define W           256
#define H           128
#define STEPS       10
#define NAME_LEN    24
#define NVS_KEY     "nxtSave"
#define SAVE_MAGIC  0x4E585401u

EXT_RAM_BSS_ATTR TaskHandle_t nowNext_Task_H = NULL;
static void nowNext_App_Task(void* dApplication);
static void nxt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* nowNext_App;
Mtb_Applications* nowNext_App_GetInstance(){
    if (!nowNext_App){
        nowNext_App = new Mtb_Applications_FullScreen(nowNext_App_Task, &nowNext_Task_H, "Now & Next", {0,0}, 8192);
        nowNext_App->mtb_App_Set_Mobile_UI(NXT_UI_MANIFEST, NXT_UI_API, nullptr, nxt_Values);
    }
    return nowNext_App;
}
MTB_REGISTER_APP(nowNext_App, 0, 0)           // the catalogue gives the app its number

struct Nxt_Step_t { char name[NAME_LEN]; uint8_t minutes; uint8_t icon; };       // minutes 0: until moved on

struct Nxt_Save_t {
    uint32_t magic;
    Nxt_Step_t step[STEPS];
    uint8_t n, autoNext, sound;
};

enum : uint8_t { R_READY = 0, R_RUNNING, R_DONE };

struct Nxt_Run_t {
    uint8_t state;
    uint8_t cur;
    uint32_t since;                     // millis() the step started
    bool timeUp;
    uint32_t version;
};

static EXT_RAM_BSS_ATTR Nxt_Save_t save;
static Nxt_Run_t run;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile bool cmdNext = false, cmdBack = false, cmdAgain = false;

static void read_Save(Nxt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.sound = 1; }
    if (s.n > STEPS) s.n = 0;
    for (int i = 0; i < STEPS; i++){ s.step[i].name[NAME_LEN - 1] = '\0'; if (s.step[i].icon >= NXT_ICON_COUNT) s.step[i].icon = NXT_ICON_DEFAULT; }
}

/** The picture for a step's name: the last of its words that a picture lists ("Play outside": the tree). */
static uint8_t icon_For(const char* name){
    char words[8][24];
    int n = 0;
    for (const char* p = name; *p && n < 8;){
        while (*p && !isalpha((unsigned char)*p)) p++;
        int k = 0;
        while (*p && isalpha((unsigned char)*p)){ if (k < 23) words[n][k++] = (char)tolower((unsigned char)*p); p++; }
        if (k){ words[n][k] = '\0'; n++; }
    }
    for (int w = n - 1; w >= 0; w--){
        for (int i = 0; i < NXT_ICON_COUNT; i++){
            const char* list = NXT_ICONS[i].words;
            const size_t len = strlen(words[w]);
            for (const char* q = list; *q;){
                const char* e = strchr(q, ' ');
                const size_t kl = e ? (size_t)(e - q) : strlen(q);
                if (kl == len && !strncmp(q, words[w], len)) return (uint8_t)i;
                if (!e) break;
                q = e + 1;
            }
        }
    }
    return (uint8_t)NXT_ICON_DEFAULT;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Step(JsonDocument& c){
    for (int i = 0; i < STEPS; i++){
        char key[8];
        snprintf(key, sizeof(key), "step%d", i);
        if (c[key].isNull()) continue;
        char text[64];
        mtb_Kit_To_Ascii(text, sizeof(text), c[key] | "");
        size_t len = strlen(text);
        while (len && text[len - 1] == ' ') text[--len] = '\0';
        char* p = text; while (*p == ' ') p++;
        portENTER_CRITICAL(&lock);
        if (!*p){                                                               // removed: the others move up
            if (i < save.n){ memmove(&save.step[i], &save.step[i + 1], sizeof(save.step[0]) * (STEPS - 1 - i)); memset(&save.step[STEPS - 1], 0, sizeof(save.step[0])); save.n--; }
        } else {
            Nxt_Step_t st = {};
            char* sp = strrchr(p, ' ');
            long m = 0;
            if (sp){ char* e; m = strtol(sp + 1, &e, 10); if (*e || m < 0 || m > 120){ m = 0; sp = nullptr; } }
            if (sp) *sp = '\0';
            if (!*p || strlen(p) >= NAME_LEN){ portEXIT_CRITICAL(&lock); mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(st.name, NAME_LEN, "%s", p);
            st.minutes = (uint8_t)m;
            st.icon = icon_For(st.name);
            const int at = i < save.n ? i : save.n;
            save.step[at] = st;
            if (at >= save.n) save.n++;
        }
        if (run.cur >= save.n){ run.cur = save.n ? save.n - 1 : 0; }
        run.version++;
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Auto(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["auto"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.autoNext = (uint8_t)v; phoneChanged = true; }
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }
static void cmd_Next(JsonDocument&){ if (!appOpen || !save.n){ mtb_Ble_App_Cmd_Reject(); return; } cmdNext = true; }
static void cmd_Back(JsonDocument&){ if (!appOpen || !save.n){ mtb_Ble_App_Cmd_Reject(); return; } cmdBack = true; }
static void cmd_Again(JsonDocument&){ if (!appOpen || !save.n){ mtb_Ble_App_Cmd_Reject(); return; } cmdAgain = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobHold = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; else if (b.type == BUTTON_PRESSED_LONG) knobHold = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The routine ------------------------------------------------------------------------------------------------------------------------
static void chime(bool done){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t NEXT[] = {{659, 220}, {0, 60}, {523, 420}};          // soft, falling: time to change
    static const Snd_Note_t DONE[] = {{523, 160}, {659, 160}, {784, 420}};
    snd_Play(done ? DONE : NEXT, 3, 0.2f, false);
}

static void go_To(int i, uint32_t ms){
    portENTER_CRITICAL(&lock);
    if (i >= save.n){ run.state = R_DONE; }
    else { run.state = R_RUNNING; run.cur = (uint8_t)(i < 0 ? 0 : i); run.since = ms; run.timeUp = false; }
    run.version++;
    portEXIT_CRITICAL(&lock);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(10, 12, 22), C_NEXT = MTB_KIT_RGB(20, 24, 40), C_SOON = MTB_KIT_RGB(255, 170, 40);

static void picture(int icon, int x, int y, int size, uint16_t bg){
    const char* svg = NXT_ICONS[icon].svg;
    mtb_Kit_Draw_Svg(svg, strlen(svg), Mtb_Kit_Box_t{(int16_t)x, (int16_t)y, (int16_t)size, (int16_t)size}, bg);
}

static void upper_Fit(char* out, size_t n, const char* name, const uint8_t* font, int width){
    char up[NAME_LEN + 2];
    snprintf(up, sizeof(up), "%s", name);
    mtb_Kit_Upper(up);
    mtb_Kit_Fit(out, n, font, up, width);
}

static void dots(const Nxt_Save_t& s, int cur, int y){
    const int gap = 9, w = s.n * gap;
    for (int i = 0; i < s.n; i++){
        const int x = W - 6 - w + i * gap + 3;
        if (i < cur) mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 2, KIT_GREEN);
        else if (i == cur) mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 3, KIT_TEXT);
        else mtb_Panel_Draw_Circle((int16_t)x, (int16_t)y, 2, KIT_DIM);
    }
}

static void draw(const Nxt_Save_t& s, const Nxt_Run_t& r, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48], fit[NAME_LEN + 4];
    if (!s.n){
        mtb_Kit_Text_Centre(W / 2, 44, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "NOW & NEXT", C_BG);
        mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_LABEL, "WRITE THE ROUTINE IN THE PHONE APP", C_BG);
        return;
    }
    if (r.state == R_DONE){
        picture(NXT_ICON_DEFAULT, W / 2 - 30, 10, 60, C_BG);
        mtb_Kit_Text_Centre(W / 2, 76, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "ALL DONE!", C_BG);
        mtb_Kit_Text_Centre(W / 2, 116, Terminal4x6, KIT_DIM, "HOLD THE KNOB TO START AGAIN", C_BG);
        return;
    }
    const Nxt_Step_t& st = s.step[r.cur];
    const bool ready = r.state == R_READY;
    // Now.
    picture(st.icon, 6, 6, 62, C_BG);
    mtb_Kit_Text(76, 6, Terminal6x8, ready ? KIT_LABEL : KIT_GREEN, ready ? "FIRST" : "NOW", C_BG);
    char up[NAME_LEN + 2];
    snprintf(up, sizeof(up), "%s", st.name);
    mtb_Kit_Upper(up);
    const uint8_t* f = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, up) <= W - 80 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
    mtb_Kit_Fit(fit, sizeof(fit), f, up, W - 80);
    mtb_Kit_Text(76, f == PT_Sans_Narrow_Bold28x34 ? 16 : 22, f, KIT_TEXT, fit, C_BG);
    if (ready) mtb_Kit_Text(76, 54, Terminal6x8, KIT_CYAN, "CLICK TO START", C_BG);
    else if (st.minutes){
        const int32_t total = st.minutes * 60000, left = total - (int32_t)(ms - r.since);
        if (left > 0){
            const int m = (left + 59999) / 60000;
            snprintf(t, sizeof(t), m == 1 ? "1 MINUTE LEFT" : "%d MINUTES LEFT", m);
            mtb_Kit_Text(76, 54, Terminal6x8, m <= 1 ? C_SOON : KIT_LABEL, t, C_BG);
            mtb_Kit_Bar(6, 72, W - 12, 6, (float)left / (float)total, m <= 1 ? C_SOON : KIT_GREEN, KIT_LINE);
        } else mtb_Kit_Text(76, 54, Terminal6x8, C_SOON, s.autoNext ? "TIME'S UP" : "TIME'S UP: CLICK FOR NEXT", C_BG);
    }
    // Next, or "time for" when this step's time is up.
    mtb_Panel_Fill_Rect(0, 84, W - 1, H - 1, C_NEXT);
    if (r.cur + 1 < s.n){
        const Nxt_Step_t& nx = s.step[r.cur + 1];
        const bool up = r.timeUp;
        const uint16_t bg = up && (ms / 700) % 2 ? MTB_KIT_RGB(60, 42, 10) : C_NEXT;
        if (bg != C_NEXT) mtb_Panel_Fill_Rect(0, 84, W - 1, H - 1, bg);
        picture(nx.icon, 6, 88, 36, bg);
        mtb_Kit_Text(48, 90, Terminal4x6, up ? C_SOON : KIT_LABEL, up ? "TIME FOR" : "NEXT", bg);
        upper_Fit(fit, sizeof(fit), nx.name, PT_Sans_Narrow_Bold18x22, W - 60 - s.n * 9);
        mtb_Kit_Text(48, 99, PT_Sans_Narrow_Bold18x22, up ? C_SOON : KIT_TEXT, fit, bg);
        dots(s, r.cur, 92);
    } else {
        mtb_Kit_Text(8, 98, PT_Sans_Narrow_Bold18x22, r.timeUp ? C_SOON : KIT_LABEL, r.timeUp ? "ALL DONE SOON" : "LAST ONE", C_NEXT);
        dots(s, r.cur, 92);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void nowNext_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Step, cmd_Auto, cmd_Sound, cmd_Next, cmd_Back, cmd_Again);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    memset(&run, 0, sizeof(run));
    turns = 0; knobClick = knobHold = false; cmdNext = cmdBack = cmdAgain = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, version = 0xFFFFFFFF;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        int t = turns;
        if (t && save.n){
            turns -= t;
            if (run.state == R_DONE){ go_To(save.n - 1, ms); t = t > 0 ? 0 : t + 1; }
            if (t) go_To(((int)run.cur + t < 0) ? 0 : ((int)run.cur + t >= save.n ? save.n - 1 : (int)run.cur + t), ms);
        } else if (t) turns -= t;
        const bool next = knobClick || cmdNext, again = knobHold || cmdAgain, back = cmdBack;
        knobClick = knobHold = false; cmdNext = cmdBack = cmdAgain = false;
        if (save.n){
            if (again){ go_To(0, ms); chime(false); }
            else if (next){
                if (run.state == R_READY) go_To(run.cur, ms);
                else if (run.state == R_RUNNING){ go_To(run.cur + 1, ms); chime(run.state == R_DONE); }
                else go_To(0, ms);
            } else if (back && run.state != R_READY) go_To(run.state == R_DONE ? save.n - 1 : run.cur - 1, ms);
        }
        // The step's time is up: a chime, "time for ..." (and on by itself, if chosen).
        if (run.state == R_RUNNING && run.cur < save.n && save.step[run.cur].minutes && !run.timeUp &&
            (int32_t)(ms - run.since) >= save.step[run.cur].minutes * 60000){
            run.timeUp = true; run.version++;
            if (save.autoNext){ go_To(run.cur + 1, ms); chime(run.state == R_DONE); }
            else chime(false);
        }
        if (run.version != version){ version = run.version; push = true; lastDraw = 0; }
        if (ms - lastDraw >= 500){
            lastDraw = ms;
            Nxt_Run_t r;
            portENTER_CRITICAL(&lock); r = run; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(save, r, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push || ms - pushedMs >= 30000) && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void nxt_Values(JsonObject values){
    Nxt_Save_t* s = (Nxt_Save_t*)heap_caps_malloc(sizeof(Nxt_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    Nxt_Run_t r;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; r = run; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(&r, 0, sizeof(r)); }
    char t[64], key[8];
    for (int i = 0; i < STEPS; i++){
        snprintf(key, sizeof(key), "step%d", i);
        if (i < s->n){
            if (s->step[i].minutes) snprintf(t, sizeof(t), "%s %u", s->step[i].name, (unsigned)s->step[i].minutes);
            else snprintf(t, sizeof(t), "%s", s->step[i].name);
        } else t[0] = '\0';
        values[key] = t;
    }
    values["auto"] = s->autoNext;
    values["sound"] = s->sound;
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject o = rows.add<JsonObject>();
    if (!s->n){ o["label"] = "Routine"; o["value"] = "Write the steps below"; o["state"] = "info"; }
    else if (!appOpen){ o["label"] = "Routine"; o["value"] = "Runs while Now & Next is open"; o["state"] = "off"; }
    else if (r.state == R_DONE){ o["label"] = "Routine"; o["value"] = "All done"; o["state"] = "ok"; }
    else {
        o["label"] = r.state == R_READY ? "First" : "Now";
        snprintf(t, sizeof(t), "%s (step %u of %u)%s", s->step[r.cur].name, (unsigned)(r.cur + 1), (unsigned)s->n, r.timeUp ? ", time is up" : "");
        o["value"] = t;
        o["state"] = r.timeUp ? "warn" : "ok";
        if (r.cur + 1 < s->n){ JsonObject n = rows.add<JsonObject>(); n["label"] = "Next"; n["value"] = s->step[r.cur + 1].name; n["state"] = "info"; }
    }
    heap_caps_free(s);
}
