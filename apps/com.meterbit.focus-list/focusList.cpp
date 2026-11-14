/**
 * Focus List, ID {21,0} (PRODUCTIVITY): today's to-do list, written on the phone, with a focus timer (focus, short
 * break, and a long break after every four focuses) that counts the minutes spent on each task.
 *
 * Knob: turn to choose a task, click to start or pause the timer on it, double click to tick it off, hold to skip to
 * the next part. A USB keyboard works too: arrows, Space (start/pause), Enter (tick), Tab (skip).
 * NVS "fclSave": the tasks, the minutes today, the timer's lengths.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_usb_hid.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "fcl_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define RGB             MTB_KIT_RGB
#define W               256
#define H               128
#define TASKS           8
#define TASK_LEN        48
#define NVS_KEY         "fclSave"
#define SAVE_MAGIC      0x46434C01u
#define LIST_W          160             // the list on the left; the timer on the right

EXT_RAM_BSS_ATTR TaskHandle_t focusList_Task_H = NULL;
static void focusList_App_Task(void* dApplication);
static void fcl_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* focusList_App;
Mtb_Applications* focusList_App_GetInstance(){
    if (!focusList_App){
        focusList_App = new Mtb_Applications_FullScreen(focusList_App_Task, &focusList_Task_H, "Focus List", {21,0}, 8192);
        focusList_App->mtb_App_Set_Mobile_UI(FCL_UI_MANIFEST, FCL_UI_API, nullptr, fcl_Values);
    }
    return focusList_App;
}
MTB_REGISTER_APP(focusList_App, 21, 0)

struct Fcl_Task_t { char text[TASK_LEN]; uint8_t done; uint8_t pad[3]; uint32_t seconds; };
struct Fcl_Save_t {
    uint32_t magic;
    Fcl_Task_t t[TASKS];
    uint8_t focusMin, breakMin, longMin, sound;
    uint32_t day;                       // local year * 1000 + day of the year, of the counts below
    uint16_t focuses;                   // focuses finished today
    uint8_t pick;                       // the task the timer counts for
    uint8_t pad;
};

enum Phase_t : uint8_t { P_READY = 0, P_FOCUS, P_BREAK, P_LONG };

static EXT_RAM_BSS_ATTR Fcl_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
enum : uint32_t { R_TOGGLE = 1, R_SKIP = 2, R_STOP = 4, R_CLEAN = 8 };
static volatile uint32_t requests = 0;
// The timer, for the phone.
static volatile uint8_t phase = P_READY;
static volatile bool running = false;
static volatile uint32_t leftMs = 0;

static void read_Save(Fcl_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.focusMin = 25; s.breakMin = 5; s.longMin = 15; s.sound = 1; }
    if (s.focusMin < 10 || s.focusMin > 90) s.focusMin = 25;
    if (s.breakMin < 1 || s.breakMin > 30) s.breakMin = 5;
    if (s.longMin < 5 || s.longMin > 45) s.longMin = 15;
    if (s.pick >= TASKS) s.pick = 0;
    for (int i = 0; i < TASKS; i++) s.t[i].text[TASK_LEN - 1] = '\0';
}

static uint32_t local_Day(void){
    const time_t now = time(nullptr);
    if (now < 1700000000) return 0;
    struct tm t;
    localtime_r(&now, &t);
    return (uint32_t)(t.tm_year + 1900) * 1000u + (uint32_t)t.tm_yday;
}

/** A new day: the minutes and the focuses start again (the tasks stay). */
static bool new_Day(Fcl_Save_t& s){
    const uint32_t d = local_Day();
    if (!d || d == s.day) return false;
    s.day = d;
    s.focuses = 0;
    for (int i = 0; i < TASKS; i++) s.t[i].seconds = 0;
    return true;
}

// Phone commands (the BLE task) -------------------------------------------------------------------------------------------
static void cmd_Task(JsonDocument& c){
    for (int i = 0; i < TASKS; i++){
        char key[8];
        snprintf(key, sizeof(key), "task%d", i);
        if (c[key].isNull()) continue;
        const char* text = c[key] | "";
        char ascii[TASK_LEN];
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
        // Trim the ends.
        char* p = ascii;
        while (*p == ' ') p++;
        size_t n = strlen(p);
        while (n && p[n - 1] == ' ') p[--n] = '\0';
        portENTER_CRITICAL(&lock);
        Fcl_Task_t& t = save.t[i];
        if (strcmp(t.text, p)){ t.done = 0; if (!*p) t.seconds = 0; }
        snprintf(t.text, sizeof(t.text), "%s", p);
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Lengths(JsonDocument& c){
    long v;
    bool ok = false;
    portENTER_CRITICAL(&lock);
    if (!c["focusMin"].isNull() && mtb_Kit_Json_Range(c["focusMin"], 10, 90, v)){ save.focusMin = (uint8_t)v; ok = true; }
    else if (!c["breakMin"].isNull() && mtb_Kit_Json_Range(c["breakMin"], 1, 30, v)){ save.breakMin = (uint8_t)v; ok = true; }
    else if (!c["longMin"].isNull() && mtb_Kit_Json_Range(c["longMin"], 5, 45, v)){ save.longMin = (uint8_t)v; ok = true; }
    portEXIT_CRITICAL(&lock);
    if (!ok){ mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.sound = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void request(uint32_t r){
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); requests |= r; portEXIT_CRITICAL(&lock);
}
static void cmd_Toggle(JsonDocument&){ request(R_TOGGLE); }
static void cmd_Skip(JsonDocument&){ request(R_SKIP); }
static void cmd_Stop(JsonDocument&){ request(R_STOP); }
static void cmd_Clean(JsonDocument&){
    portENTER_CRITICAL(&lock); requests |= R_CLEAN; portEXIT_CRITICAL(&lock);
    if (!appOpen){
        // Not open: do it on the saved list.
        Fcl_Save_t* s = (Fcl_Save_t*)heap_caps_malloc(sizeof(Fcl_Save_t), MALLOC_CAP_SPIRAM);
        if (!s) return;
        read_Save(*s);
        int k = 0;
        for (int i = 0; i < TASKS; i++) if (s->t[i].text[0] && !s->t[i].done) s->t[k++] = s->t[i];
        for (; k < TASKS; k++) memset(&s->t[k], 0, sizeof(s->t[k]));
        mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s));
        heap_caps_free(s);
        portENTER_CRITICAL(&lock); requests &= ~R_CLEAN; portEXIT_CRITICAL(&lock);
    }
}
static void cmd_Pick(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["pick"], 0, TASKS - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    const bool used = save.t[v].text[0] != '\0';
    if (used) save.pick = (uint8_t)v;
    portEXIT_CRITICAL(&lock);
    if (!used){ mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}

// Knob ----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile uint8_t clicks = 0;
static volatile bool holdRequested = false;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) holdRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicks = (uint8_t)(b.count >= 2 ? 2 : 1);
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The timer ------------------------------------------------------------------------------------------------------------------------
struct Fcl_t {
    uint8_t phase;
    bool running;
    uint32_t left, total;               // ms
    uint32_t lastMs;
    uint32_t secondAcc;                 // ms towards the task's next second
    uint8_t sel;                        // the chosen task (a slot)
    char banner[40]; uint32_t bannerUntil;
};

static void chime(int kind){
    static const Snd_Note_t FOCUS_DONE[] = {{784, 140}, {659, 140}, {523, 300}};
    static const Snd_Note_t BREAK_DONE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 260}};
    static const Snd_Note_t TICK[] = {{1319, 40}, {1760, 70}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (kind == 0) snd_Play(FOCUS_DONE, 3, 0.3f, false);
    else if (kind == 1) snd_Play(BREAK_DONE, 4, 0.3f, false);
    else snd_Play(TICK, 2, 0.2f, false);
}

static void set_Phase(Fcl_t& f, uint8_t p, bool run){
    f.phase = p;
    f.running = run;
    const uint32_t m = p == P_FOCUS ? save.focusMin : p == P_BREAK ? save.breakMin : p == P_LONG ? save.longMin : save.focusMin;
    f.total = f.left = m * 60000u;
}

/** The part that ends goes on to the next: a focus to its break (started), a break to a focus (waiting for a click). */
static void next_Part(Fcl_t& f, bool finished, uint32_t ms){
    if (f.phase == P_READY) return;                     // nothing to skip yet
    if (f.phase == P_FOCUS){
        if (finished){ portENTER_CRITICAL(&lock); save.focuses++; portEXIT_CRITICAL(&lock); }
        const bool longOne = finished && save.focuses % 4 == 0;
        set_Phase(f, longOne ? P_LONG : P_BREAK, true);
        snprintf(f.banner, sizeof(f.banner), longOne ? "LONG BREAK: WELL DONE!" : "TIME FOR A BREAK");
        if (finished) chime(0);
    } else {
        set_Phase(f, P_FOCUS, false);
        snprintf(f.banner, sizeof(f.banner), "CLICK TO FOCUS AGAIN");
        if (finished) chime(1);
    }
    f.bannerUntil = ms + 5000;
}

static int used_Count(void){ int n = 0; for (int i = 0; i < TASKS; i++) if (save.t[i].text[0]) n++; return n; }
static int next_Used(int from, int dir){
    for (int k = 1; k <= TASKS; k++){
        const int i = (from + dir * k + TASKS * 2) % TASKS;
        if (save.t[i].text[0]) return i;
    }
    return from;
}

static void start_Pause(Fcl_t& f, uint32_t ms){
    if (f.phase == P_READY){
        if (!save.t[f.sel].text[0] && used_Count()) f.sel = (uint8_t)next_Used(f.sel, 1);
        portENTER_CRITICAL(&lock); save.pick = f.sel; portEXIT_CRITICAL(&lock);
        set_Phase(f, P_FOCUS, true);
        return;
    }
    if (f.phase == P_FOCUS && !f.running){ portENTER_CRITICAL(&lock); save.pick = f.sel; portEXIT_CRITICAL(&lock); }
    f.running = !f.running;
    (void)ms;
}

static void tick_Done(Fcl_t& f){
    if (!save.t[f.sel].text[0]) return;
    portENTER_CRITICAL(&lock); save.t[f.sel].done ^= 1; portEXIT_CRITICAL(&lock);
    if (save.t[f.sel].done) chime(2);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------
static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void frame(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Draw_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void line(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Draw_Line((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }

static const uint16_t C_BG = RGB(6, 8, 14), C_SEL = RGB(28, 36, 64), C_LINE = RGB(36, 40, 56);

static uint16_t phase_Color(uint8_t p){ return p == P_FOCUS ? RGB(255, 90, 70) : p == P_BREAK ? RGB(60, 210, 120) : p == P_LONG ? RGB(80, 170, 255) : KIT_LABEL; }

static void fmt_Minutes(char* t, size_t n, uint32_t seconds){
    const uint32_t m = seconds / 60;
    if (m >= 60) snprintf(t, n, "%luh%02lu", (unsigned long)(m / 60), (unsigned long)(m % 60));
    else snprintf(t, n, "%lum", (unsigned long)m);
}

static void draw(const Fcl_t& f, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    // The list.
    int done = 0, used = 0;
    for (int i = 0; i < TASKS; i++) if (save.t[i].text[0]){ used++; if (save.t[i].done) done++; }
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, "TODAY", C_BG);
    char t[64];
    if (used){ snprintf(t, sizeof(t), "%d OF %d DONE", done, used); mtb_Kit_Text_Right(LIST_W - 3, 3, Terminal4x6, done == used ? KIT_GREEN : KIT_LABEL, t, C_BG); }
    line(0, 11, LIST_W - 1, 11, C_LINE);
    if (!used){
        mtb_Kit_Text_Centre(LIST_W / 2, 52, Terminal6x8, KIT_LABEL, "NO TASKS YET", C_BG);
        mtb_Kit_Text_Centre(LIST_W / 2, 66, Terminal4x6, KIT_DIM, "WRITE THEM IN THE PHONE APP", C_BG);
    }
    int y = 14;
    for (int i = 0; i < TASKS; i++){
        const Fcl_Task_t& k = save.t[i];
        if (!k.text[0]) continue;
        const bool on = f.sel == i;
        const uint16_t bg = on ? C_SEL : C_BG;
        if (on) rect(0, y - 1, LIST_W - 2, y + 12, C_SEL);
        // The box.
        frame(3, y + 2, 11, y + 10, k.done ? KIT_GREEN : KIT_LABEL);
        if (k.done){ rect(4, y + 3, 10, y + 9, KIT_GREEN); line(5, y + 6, 6, y + 8, C_BG); line(6, y + 8, 9, y + 4, C_BG); }
        // The minutes, and a mark on the task the timer counts for.
        char mins[12] = "";
        if (k.seconds >= 60) fmt_Minutes(mins, sizeof(mins), k.seconds);
        const int mw = mins[0] ? mtb_Kit_Text_Width(Terminal4x6, mins) : 0;
        if (mins[0]) mtb_Kit_Text_Right(LIST_W - 4, y + 4, Terminal4x6, KIT_LABEL, mins, bg);
        const bool counting = f.phase == P_FOCUS && save.pick == i;
        if (counting){
            const uint16_t c = f.running && (ms / 500) % 2 ? phase_Color(P_FOCUS) : mtb_Kit_Dim(phase_Color(P_FOCUS), 120);
            mtb_Panel_Fill_Circle((int16_t)(LIST_W - 8 - mw), (int16_t)(y + 6), 2, c);
        }
        char fit[TASK_LEN];
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, k.text, LIST_W - 18 - mw - (counting ? 10 : 2));
        mtb_Kit_Text(15, y + 3, Terminal6x8, k.done ? KIT_DIM : (on ? WHITE : KIT_TEXT), fit, bg);
        if (k.done) line(15, y + 6, 15 + mtb_Kit_Text_Width(Terminal6x8, fit), y + 6, KIT_DIM);
        y += 14;
    }
    line(LIST_W, 0, LIST_W, H - 1, C_LINE);

    // The timer.
    const int cx = LIST_W + (W - LIST_W) / 2;
    static const char* const NAME[] = {"READY", "FOCUS", "BREAK", "LONG BREAK"};
    const uint16_t pc = phase_Color(f.phase);
    mtb_Kit_Text_Centre(cx, 3, Terminal6x8, pc, f.phase == P_FOCUS && !f.running && f.left < f.total ? "PAUSED" : NAME[f.phase], C_BG);
    const uint32_t s = (f.left + 999) / 1000;
    snprintf(t, sizeof(t), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    const bool blinkOff = !f.running && f.phase != P_READY && (ms / 600) % 2;
    mtb_Kit_Text_Centre(cx, 16, Terminal10x17, blinkOff ? KIT_DIM : WHITE, t, C_BG);
    const float frac = f.total ? 1.0f - (float)f.left / (float)f.total : 0;
    mtb_Kit_Bar(LIST_W + 8, 38, W - LIST_W - 16, 3, frac, pc, C_LINE);
    // Four dots: the focuses towards the long break.
    const int inSet = save.focuses % 4;
    for (int i = 0; i < 4; i++){
        const int x = cx - 15 + i * 10;
        if (i < inSet || (inSet == 0 && save.focuses > 0 && f.phase == P_LONG)) mtb_Panel_Fill_Circle((int16_t)x, 50, 3, phase_Color(P_FOCUS));
        else mtb_Panel_Draw_Circle((int16_t)x, 50, 3, KIT_DIM);
    }
    uint32_t total = 0;
    for (int i = 0; i < TASKS; i++) total += save.t[i].seconds;
    char mins[12];
    fmt_Minutes(mins, sizeof(mins), total);
    snprintf(t, sizeof(t), "%u FOCUS%s TODAY", save.focuses, save.focuses == 1 ? "" : "ES");
    mtb_Kit_Text_Centre(cx, 60, Terminal4x6, KIT_LABEL, t, C_BG);
    snprintf(t, sizeof(t), "%s ON TASKS", mins);
    mtb_Kit_Text_Centre(cx, 68, Terminal4x6, KIT_LABEL, t, C_BG);
    // The clock.
    const time_t now = time(nullptr);
    if (now > 1700000000){
        struct tm lt;
        localtime_r(&now, &lt);
        const bool h24 = mtb_Clock_24h();
        int hh = lt.tm_hour;
        if (!h24){ hh %= 12; if (!hh) hh = 12; }
        snprintf(t, sizeof(t), h24 ? "%02d:%02d" : "%d:%02d", hh, lt.tm_min);
        mtb_Kit_Text_Centre(cx, 84, Terminal8x12, KIT_TEXT, t, C_BG);
        if (!h24) mtb_Kit_Text_Centre(cx, 98, Terminal4x6, KIT_DIM, lt.tm_hour < 12 ? "AM" : "PM", C_BG);
    }
    const char* hint = f.phase == P_READY ? "CLICK: FOCUS" : f.running ? "CLICK: PAUSE" : "CLICK: GO ON";
    mtb_Kit_Text_Centre(cx, 110, Terminal4x6, KIT_DIM, hint, C_BG);
    mtb_Kit_Text_Centre(cx, 118, Terminal4x6, KIT_DIM, "2 CLICKS: TICK", C_BG);

    if (ms < f.bannerUntil){
        rect(20, 46, W - 21, 80, RGB(16, 20, 36));
        frame(20, 46, W - 21, 80, pc);
        mtb_Kit_Text_Centre(W / 2, 57, Terminal8x12, pc, f.banner, RGB(16, 20, 36));
    }
}

// The app ---------------------------------------------------------------------------------------------------------------------------
static void focusList_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Task, cmd_Lengths, cmd_Sound, cmd_Toggle, cmd_Skip, cmd_Stop, cmd_Clean, cmd_Pick);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    bool dirty = new_Day(save);
    turns = 0; clicks = 0; holdRequested = false; requests = 0; phoneChanged = false;

    Fcl_t* f = (Fcl_t*)heap_caps_calloc(1, sizeof(Fcl_t), MALLOC_CAP_SPIRAM);
    if (!f){ mtb_Delete_This_App(THIS_APP); return; }
    set_Phase(*f, P_READY, false);
    f->sel = save.t[save.pick].text[0] ? save.pick : (uint8_t)next_Used(save.pick, 1);
    f->lastMs = millis();
    mtb_Usb_Input_Open(false);
    appOpen = true;

    uint32_t lastDraw = 0, savedMs = 0, pushedMs = 0, lastSecond = 0;
    bool push = true, redraw = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t dt = ms - f->lastMs;
        f->lastMs = ms;

        if (phoneChanged){
            phoneChanged = false;
            dirty = push = redraw = true;
            if (!save.t[f->sel].text[0]) f->sel = (uint8_t)next_Used(f->sel, 1);
            if (f->phase == P_FOCUS && !f->running && f->left == f->total) set_Phase(*f, P_FOCUS, false);   // a new length
            if (f->phase == P_FOCUS && save.t[save.pick].text[0]) f->sel = save.pick;
        }
        uint32_t r;
        portENTER_CRITICAL(&lock); r = requests; requests = 0; portEXIT_CRITICAL(&lock);
        if (r & R_TOGGLE) start_Pause(*f, ms);
        if (r & R_SKIP) next_Part(*f, false, ms);
        if (r & R_STOP) set_Phase(*f, P_READY, false);
        if (r & R_CLEAN){
            portENTER_CRITICAL(&lock);
            int k = 0;
            for (int i = 0; i < TASKS; i++) if (save.t[i].text[0] && !save.t[i].done) save.t[k++] = save.t[i];
            for (; k < TASKS; k++) memset(&save.t[k], 0, sizeof(save.t[k]));
            save.pick = 0;
            portEXIT_CRITICAL(&lock);
            f->sel = 0;
        }
        if (r){ dirty = push = redraw = true; }

        // The knob and the keyboard.
        const int t = turns;
        if (t){ turns -= t; if (used_Count()) f->sel = (uint8_t)next_Used(f->sel, t > 0 ? 1 : -1); redraw = true; }
        const uint8_t c = clicks;
        if (c){ clicks = 0; if (c >= 2) tick_Done(*f); else start_Pause(*f, ms); dirty = push = redraw = true; }
        if (holdRequested){ holdRequested = false; next_Part(*f, false, ms); push = redraw = true; }
        Mtb_Usb_Input_t in;
        int events = 0;
        while (events++ < 16 && mtb_Usb_Input_Read(in, 0)){
            if (in.type == MTB_USB_KEY_DOWN){
                if ((in.key == 0x51 || in.key == 0x52) && used_Count()) f->sel = (uint8_t)next_Used(f->sel, in.key == 0x51 ? 1 : -1);
                else if (in.key == 0x2C) start_Pause(*f, ms);
                else if (in.key == 0x28) tick_Done(*f);
                else if (in.key == 0x2B) next_Part(*f, false, ms);
                else continue;
                dirty = push = redraw = true;
            } else if (in.type == MTB_USB_MOUSE_WHEEL && used_Count()){ f->sel = (uint8_t)next_Used(f->sel, in.wheel > 0 ? -1 : 1); redraw = true; }
            else if (in.type == MTB_USB_MOUSE_BUTTONS && (in.buttons & 1)){ start_Pause(*f, ms); dirty = push = redraw = true; }
        }

        // Time passes.
        if (f->running){
            if (dt >= f->left){ next_Part(*f, true, ms); dirty = push = true; }
            else f->left -= dt;
            if (f->phase == P_FOCUS && f->running){
                f->secondAcc += dt;
                while (f->secondAcc >= 1000){
                    f->secondAcc -= 1000;
                    portENTER_CRITICAL(&lock);
                    if (save.t[save.pick].text[0]) save.t[save.pick].seconds++;
                    portEXIT_CRITICAL(&lock);
                }
            }
        }
        if (ms - lastSecond >= 1000){
            lastSecond = ms;
            redraw = true;
            portENTER_CRITICAL(&lock);
            const bool day = new_Day(save);
            portEXIT_CRITICAL(&lock);
            if (day) dirty = push = true;
            if (f->running && f->phase == P_FOCUS && (save.t[save.pick].seconds % 60) == 0) dirty = true;
        }
        phase = f->phase; running = f->running; leftMs = f->left;

        if (dirty && ms - savedMs >= 5000){ dirty = false; savedMs = ms; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        if ((redraw || ms - lastDraw >= 250) && ms - lastDraw >= 40){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(*f, ms);
            mtb_Panel_End_Update();
        }
        delay(10);
    }
    appOpen = false;
    phase = P_READY; running = false;
    mtb_Usb_Input_Close();
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    heap_caps_free(f);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ----------------------------------------------------------------------------------------------
static void fcl_Values(JsonObject values){
    Fcl_Save_t* s = (Fcl_Save_t*)heap_caps_malloc(sizeof(Fcl_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); new_Day(*s); }
    char t[96];
    JsonArray now = values["now"].to<JsonArray>();
    JsonObject o = now.add<JsonObject>();
    o["label"] = "Timer";
    const uint32_t sec = (leftMs + 999) / 1000;
    if (!appOpen) { o["value"] = "Focus List is not open on Pixlpal"; o["state"] = "off"; }
    else if (phase == P_READY){ o["value"] = "Ready: click the knob to start a focus"; o["state"] = "info"; }
    else {
        static const char* const NAME[] = {"Ready", "Focus", "Break", "Long break"};
        if (phase == P_FOCUS && s->t[s->pick].text[0])
            snprintf(t, sizeof(t), "%s on \"%s\": %lu:%02lu left%s", NAME[phase], s->t[s->pick].text, (unsigned long)(sec / 60), (unsigned long)(sec % 60), running ? "" : " (paused)");
        else snprintf(t, sizeof(t), "%s: %lu:%02lu left%s", NAME[phase], (unsigned long)(sec / 60), (unsigned long)(sec % 60), running ? "" : " (paused)");
        o["value"] = t;
        o["state"] = phase == P_FOCUS ? (running ? "warn" : "info") : "ok";
    }
    JsonObject o2 = now.add<JsonObject>();
    o2["label"] = "Today";
    uint32_t total = 0;
    for (int i = 0; i < TASKS; i++) total += s->t[i].seconds;
    snprintf(t, sizeof(t), "%u focus%s, %lu min on tasks", s->focuses, s->focuses == 1 ? "" : "es", (unsigned long)(total / 60));
    o2["value"] = t;
    o2["state"] = "info";

    JsonArray rows = values["tasks"].to<JsonArray>();
    for (int i = 0; i < TASKS; i++){
        char key[8];
        snprintf(key, sizeof(key), "task%d", i);
        values[key] = s->t[i].text;
        if (!s->t[i].text[0]) continue;
        JsonObject r = rows.add<JsonObject>();
        char label[TASK_LEN + 8];
        snprintf(label, sizeof(label), "%d. %s", i + 1, s->t[i].text);
        r["label"] = label;
        snprintf(t, sizeof(t), "%s%lu min today", s->t[i].done ? "Done, " : "", (unsigned long)(s->t[i].seconds / 60));
        r["value"] = t;
        r["state"] = s->t[i].done ? "ok" : (appOpen && phase == P_FOCUS && s->pick == i ? "warn" : "info");
    }
    values["focusMin"] = s->focusMin;
    values["breakMin"] = s->breakMin;
    values["longMin"] = s->longMin;
    values["sound"] = s->sound;
    values["pick"] = s->pick;
    heap_caps_free(s);
}
