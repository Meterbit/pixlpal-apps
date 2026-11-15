/**
 * Name Picker: who answers next. The class's names run past on a reel that slows down and stops on one, which then
 * fills the screen. Nobody is picked twice until everyone has had a turn (unless the teacher allows repeats). The other
 * mode splits the class into random groups of a chosen size (the last ones made even).
 *
 * Knob: click to pick (or make new groups), double click to switch modes, turn to page the groups, hold for a new
 * round. NVS "npkSave": the class, the choices and who has been picked this round. Works without the internet.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "npk_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define W           256
#define H           128
#define PEOPLE      40
#define NAME_LEN    20
#define NVS_KEY     "npkSave"
#define SAVE_MAGIC  0x4E504B01u
#define SPIN_MS     2600

EXT_RAM_BSS_ATTR TaskHandle_t namePicker_Task_H = NULL;
static void namePicker_App_Task(void* dApplication);
static void npk_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* namePicker_App;
Mtb_Applications* namePicker_App_GetInstance(){
    if (!namePicker_App){
        namePicker_App = new Mtb_Applications_FullScreen(namePicker_App_Task, &namePicker_Task_H, "Name Picker", {0,0}, 8192);
        namePicker_App->mtb_App_Set_Mobile_UI(NPK_UI_MANIFEST, NPK_UI_API, nullptr, npk_Values);
    }
    return namePicker_App;
}
MTB_REGISTER_APP(namePicker_App, 0, 0)        // the catalogue gives the app its number

struct Npk_Save_t {
    uint32_t magic;
    char name[PEOPLE][NAME_LEN];
    uint8_t count, size, repeat, sound, mode;
    uint64_t picked;                    // a bit a name: picked this round
    int8_t last;                        // the last name picked, -1 none
};

struct Npk_Groups_t {
    uint8_t order[PEOPLE];              // names in group order
    uint8_t start[PEOPLE + 1];          // group g: order[start[g]] .. order[start[g + 1] - 1]
    uint8_t n;                          // groups
    uint32_t version;
};

static EXT_RAM_BSS_ATTR Npk_Save_t save;
static EXT_RAM_BSS_ATTR Npk_Groups_t groups;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile bool cmdPick = false, cmdGroups = false, cmdRound = false;

static void read_Save(Npk_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.size = 4; s.sound = 1; s.last = -1; }
    if (s.count > PEOPLE) s.count = 0;
    if (s.size < 2 || s.size > 8) s.size = 4;
    if (s.last >= s.count) s.last = -1;
    for (int i = 0; i < PEOPLE; i++) s.name[i][NAME_LEN - 1] = '\0';
}

static int left_In_Round(const Npk_Save_t& s){
    int n = 0;
    for (int i = 0; i < s.count; i++) if (!(s.picked >> i & 1)) n++;
    return n;
}

/** Random groups of about the size chosen: as many groups as needed, the people shared out evenly among them. */
static void make_Groups(const Npk_Save_t& s, Npk_Groups_t& g){
    const int n = s.count;
    for (int i = 0; i < n; i++) g.order[i] = (uint8_t)i;
    for (int i = n - 1; i > 0; i--){ const int j = (int)(esp_random() % (uint32_t)(i + 1)); const uint8_t t = g.order[i]; g.order[i] = g.order[j]; g.order[j] = t; }
    g.n = (uint8_t)(n ? (n + s.size - 1) / s.size : 0);
    int at = 0;
    for (int k = 0; k < g.n; k++){
        g.start[k] = (uint8_t)at;
        at += n / g.n + (k < n % g.n ? 1 : 0);
    }
    g.start[g.n] = (uint8_t)n;
    g.version++;
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
            n++;
        }
        p = e;
    }
    if (n < 2){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    memset(save.name, 0, sizeof(save.name));
    memcpy(save.name, names, sizeof(names[0]) * n);
    save.count = (uint8_t)n;
    save.picked = 0;
    save.last = -1;
    make_Groups(save, groups);
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Size(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["size"], 2, 8, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.size = (uint8_t)v; make_Groups(save, groups); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Repeat(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["repeat"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.repeat = (uint8_t)v; phoneChanged = true; }
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }
static void cmd_Mode(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["mode"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.mode = (uint8_t)v; phoneChanged = true; }
static void cmd_Pick(JsonDocument&){ if (!appOpen || save.count < 2){ mtb_Ble_App_Cmd_Reject(); return; } cmdPick = true; }
static void cmd_Groups(JsonDocument&){ if (!appOpen || save.count < 2){ mtb_Ble_App_Cmd_Reject(); return; } cmdGroups = true; }
static void cmd_Round(JsonDocument&){ save.picked = 0; save.last = -1; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobDouble = false, knobHold = false;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED){ if (b.count >= 2) knobDouble = true; else knobClick = true; }
    else if (b.type == BUTTON_PRESSED_LONG) knobHold = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The spin ---------------------------------------------------------------------------------------------------------------------------
struct Npk_Spin_t {
    bool on;
    uint32_t since;
    uint8_t reel[PEOPLE * 2];           // the names the reel shows, in turn
    int reelN;
    int steps;                          // how many names go past before the chosen one
    int chosen;                         // the name it lands on
    int shownStep;                      // for the ticks
};
static Npk_Spin_t spin;

static void start_Spin(uint32_t ms){
    int cand[PEOPLE], n = 0;
    for (int i = 0; i < save.count; i++) if (save.repeat || !(save.picked >> i & 1)) cand[n++] = i;
    if (!n){ save.picked = 0; for (int i = 0; i < save.count; i++) cand[n++] = i; }   // everyone picked: a new round
    if (n > 1 && save.last >= 0) for (int i = 0; i < n; i++) if (cand[i] == save.last){ cand[i] = cand[--n]; break; }   // not twice running
    spin.chosen = cand[esp_random() % (uint32_t)n];
    // The reel: every name in a shuffled order, the chosen one last.
    spin.reelN = 0;
    int all[PEOPLE];
    for (int i = 0; i < save.count; i++) all[i] = i;
    for (int i = save.count - 1; i > 0; i--){ const int j = (int)(esp_random() % (uint32_t)(i + 1)); const int t = all[i]; all[i] = all[j]; all[j] = t; }
    for (int i = 0; i < save.count; i++) if (all[i] != spin.chosen) spin.reel[spin.reelN++] = (uint8_t)all[i];
    spin.reel[spin.reelN++] = (uint8_t)spin.chosen;
    spin.steps = spin.reelN - 1 + spin.reelN * (save.count < 8 ? 3 : save.count < 20 ? 2 : 1);
    spin.on = true;
    spin.since = ms;
    spin.shownStep = -1;
}

/** The reel's position (in names) at @p t ms: fast, then slowing to a stop on the chosen name. */
static float spin_Position(uint32_t t){
    float f = (float)t / SPIN_MS;
    if (f > 1) f = 1;
    const float eased = 1.0f - powf(1.0f - f, 3.0f);
    return eased * spin.steps;
}

static void beep(int kind){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t TICK[] = {{1800, 8}};
    static const Snd_Note_t WIN[] = {{523, 90}, {659, 90}, {784, 90}, {1047, 260}};
    static const Snd_Note_t GROUP[] = {{659, 80}, {784, 80}, {988, 160}};
    if (kind == 0) snd_Play(TICK, 1, 0.12f, true);
    else if (kind == 1) snd_Play(WIN, 4, 0.3f, false);
    else snd_Play(GROUP, 3, 0.25f, false);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(8, 6, 16), C_HEAD = MTB_KIT_RGB(20, 16, 36), C_REEL = MTB_KIT_RGB(18, 14, 34);
static const uint16_t PALETTE[] = {MTB_KIT_RGB(255, 196, 0), MTB_KIT_RGB(80, 200, 255), MTB_KIT_RGB(40, 210, 90), MTB_KIT_RGB(255, 120, 60),
                                   MTB_KIT_RGB(220, 110, 255), MTB_KIT_RGB(255, 90, 140), MTB_KIT_RGB(120, 230, 210), MTB_KIT_RGB(200, 200, 90)};

static const uint8_t* fit_Font(const char* text, int width){
    if (mtb_Kit_Text_Width(PT_Sans_Narrow38x34, text) <= width) return PT_Sans_Narrow38x34;
    if (mtb_Kit_Text_Width(PT_Sans_Narrow_Bold18x22, text) <= width) return PT_Sans_Narrow_Bold18x22;
    return Terminal8x12;
}

static void header(const char* left, const char* right){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, left, C_HEAD);
    if (right) mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_LABEL, right, C_HEAD);
}

static void draw_Pick(const Npk_Save_t& s, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48], up[NAME_LEN + 2];
    if (s.count < 2){
        header("NAME PICKER", nullptr);
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, "WHO'S NEXT?", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "WRITE THE CLASS'S NAMES IN THE PHONE APP", C_BG);
        return;
    }
    const int left = left_In_Round(s);
    if (s.repeat) snprintf(t, sizeof(t), "%u NAMES", (unsigned)s.count);
    else snprintf(t, sizeof(t), "%d OF %u LEFT THIS ROUND", left, (unsigned)s.count);
    header("WHO'S NEXT?", t);
    if (spin.on){
        // The reel: three names, the middle one in the window.
        const float pos = spin_Position(ms - spin.since);
        const int base = (int)floorf(pos);
        const float frac = pos - base;
        mtb_Panel_Fill_Rect(0, 14, W - 1, 113, C_REEL);
        for (int k = -2; k <= 2; k++){
            const int step = base + k;
            if (step < 0) continue;
            const int idx = spin.reel[step % spin.reelN];
            const int y = 64 - 17 + (int)((k - frac) * 36.0f);
            if (y < 6 || y > 110) continue;
            snprintf(up, sizeof(up), "%s", s.name[idx]);
            mtb_Kit_Upper(up);
            const bool mid = k == 0 && frac < 0.5f;
            const uint8_t* f = fit_Font(up, W - 16);
            mtb_Kit_Text_Centre(W / 2, (int16_t)(y + (f == PT_Sans_Narrow38x34 ? 0 : f == PT_Sans_Narrow_Bold18x22 ? 6 : 11)), f,
                                mid ? KIT_TEXT : mtb_Kit_Dim(KIT_TEXT, 90), up, C_REEL);
        }
        mtb_Panel_Fill_Rect(0, 14, W - 1, 22, C_BG);                          // the window's edges
        mtb_Panel_Fill_Rect(0, 106, W - 1, 113, C_BG);
        mtb_Panel_Draw_HLine(44, 0, W - 1, KIT_GOLD);
        mtb_Panel_Draw_HLine(84, 0, W - 1, KIT_GOLD);
        return;
    }
    if (s.last >= 0){
        // The chosen name, large, with sparkles for a moment.
        snprintf(up, sizeof(up), "%s", s.name[s.last]);
        mtb_Kit_Upper(up);
        const uint8_t* f = fit_Font(up, W - 12);
        const uint16_t c = PALETTE[s.last % 8];
        mtb_Kit_Text_Centre(W / 2, (int16_t)(f == PT_Sans_Narrow38x34 ? 44 : f == PT_Sans_Narrow_Bold18x22 ? 50 : 56), f, c, up, C_BG);
        const uint32_t since = ms - spin.since;
        if (since < SPIN_MS + 2500){
            for (int k = 0; k < 24; k++){
                const uint32_t seed = (uint32_t)k * 2654435761u + spin.since;
                const float a = (float)(seed % 628) / 100.0f, r = 34.0f + (float)((since - SPIN_MS) % 2500) / 2500.0f * (50 + seed % 40);     // from just outside the name
                const int x = W / 2 + (int)(cosf(a) * r * 1.6f), y = 62 + (int)(sinf(a) * r * 0.7f);
                if (x >= 0 && x < W - 1 && y > 12 && y < 114) mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + 1), (int16_t)(y + 1), PALETTE[k % 8]);
            }
        }
    } else mtb_Kit_Text_Centre(W / 2, 50, Terminal10x17, KIT_TEXT, "READY", C_BG);
    mtb_Panel_Fill_Rect(0, 116, W - 1, 127, C_HEAD);
    mtb_Kit_Text_Centre(W / 2, 119, Terminal4x6, KIT_LABEL, "CLICK TO PICK   DOUBLE CLICK: GROUPS", C_HEAD);
}

static void draw_Groups(const Npk_Save_t& s, const Npk_Groups_t& g, int page){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48];
    if (s.count < 2 || !g.n){
        header("GROUPS", nullptr);
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, KIT_LABEL, "WRITE THE NAMES IN THE PHONE APP", C_BG);
        return;
    }
    // Columns of groups: four a page, or six when the groups are small.
    int biggest = 0;
    for (int k = 0; k < g.n; k++) if (g.start[k + 1] - g.start[k] > biggest) biggest = g.start[k + 1] - g.start[k];
    const int perPage = biggest <= 4 && g.n > 4 ? 8 : 4;
    const int cols = perPage == 8 ? 4 : 4, rowsOfGroups = perPage == 8 ? 2 : 1;
    const int pages = (g.n + perPage - 1) / perPage;
    const int pg = ((page % pages) + pages) % pages;
    snprintf(t, sizeof(t), "%u GROUPS OF %u%s", (unsigned)g.n, (unsigned)s.size, pages > 1 ? "" : "");
    char right[16];
    if (pages > 1) snprintf(right, sizeof(right), "%d/%d", pg + 1, pages); else right[0] = '\0';
    header(t, right);
    const int cw = W / cols, ch = (H - 12) / rowsOfGroups;
    for (int i = 0; i < perPage; i++){
        const int k = pg * perPage + i;
        if (k >= g.n) break;
        const int x = (i % cols) * cw, y = 12 + (i / cols) * ch;
        const uint16_t c = PALETTE[k % 8];
        mtb_Panel_Fill_Rect((int16_t)(x + 1), (int16_t)y, (int16_t)(x + cw - 2), (int16_t)(y + 9), mtb_Kit_Dim(c, 90));
        snprintf(t, sizeof(t), "GROUP %d", k + 1);
        mtb_Kit_Text((int16_t)(x + 3), (int16_t)(y + 1), Terminal6x8, BLACK, t, mtb_Kit_Dim(c, 90));
        for (int m = g.start[k]; m < g.start[k + 1]; m++){
            char up[NAME_LEN + 2], fit[NAME_LEN + 2];
            snprintf(up, sizeof(up), "%s", s.name[g.order[m]]);
            mtb_Kit_Upper(up);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, up, cw - 6);
            mtb_Kit_Text((int16_t)(x + 3), (int16_t)(y + 12 + (m - g.start[k]) * 10), Terminal6x8, KIT_TEXT, fit, C_BG);
        }
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void namePicker_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Names, cmd_Size, cmd_Repeat, cmd_Sound, cmd_Mode, cmd_Pick, cmd_Groups, cmd_Round);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    memset(&groups, 0, sizeof(groups));
    make_Groups(save, groups);
    memset(&spin, 0, sizeof(spin));
    turns = 0; knobClick = knobDouble = knobHold = false; cmdPick = cmdGroups = cmdRound = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0;
    int page = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){ turns -= t; if (save.mode == 1){ page += t; lastDraw = 0; } }
        if (knobDouble){ knobDouble = false; save.mode = !save.mode; push = saveNow = true; lastDraw = 0; }
        if (knobHold){ knobHold = false; save.picked = 0; save.last = -1; push = saveNow = true; lastDraw = 0; beep(2); }
        const bool pick = cmdPick || (knobClick && save.mode == 0);
        const bool regroup = cmdGroups || (knobClick && save.mode == 1);
        knobClick = false; cmdPick = cmdGroups = false;
        if (pick && !spin.on && save.count >= 2){ save.mode = 0; start_Spin(ms); push = true; }
        if (regroup && save.count >= 2){
            portENTER_CRITICAL(&lock); make_Groups(save, groups); portEXIT_CRITICAL(&lock);
            save.mode = 1; page = 0; push = saveNow = true; lastDraw = 0; beep(2);
        }
        if (spin.on){
            const int step = (int)spin_Position(ms - spin.since);
            if (step != spin.shownStep){ spin.shownStep = step; if (ms - spin.since < SPIN_MS - 150) beep(0); }
            if (ms - spin.since >= SPIN_MS){
                spin.on = false;
                save.last = (int8_t)spin.chosen;
                if (!save.repeat){ save.picked |= 1ULL << spin.chosen; if (left_In_Round(save) == 0) save.picked = 1ULL << spin.chosen; }
                beep(1);
                push = saveNow = true;
            }
        }
        if (ms - lastDraw >= (spin.on || ms - spin.since < SPIN_MS + 2600 ? 30u : 500u)){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (save.mode == 1 && !spin.on) draw_Groups(save, groups, page); else draw_Pick(save, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(15);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void npk_Values(JsonObject values){
    Npk_Save_t* s = (Npk_Save_t*)heap_caps_malloc(sizeof(Npk_Save_t), MALLOC_CAP_SPIRAM);
    Npk_Groups_t* g = (Npk_Groups_t*)heap_caps_malloc(sizeof(Npk_Groups_t), MALLOC_CAP_SPIRAM);
    if (!s || !g){ heap_caps_free(s); heap_caps_free(g); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *g = groups; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(g, 0, sizeof(*g)); }
    char names[PEOPLE * (NAME_LEN + 2)] = "";
    for (int i = 0; i < s->count; i++){ if (i) strlcat(names, ", ", sizeof(names)); strlcat(names, s->name[i], sizeof(names)); }
    values["names"] = names;
    values["size"] = s->size;
    values["repeat"] = s->repeat;
    values["sound"] = s->sound;
    values["mode"] = s->mode;
    JsonArray now = values["now"].to<JsonArray>();
    JsonObject r = now.add<JsonObject>();
    char t[96];
    r["label"] = "Picked";
    if (s->last >= 0){ r["value"] = s->name[s->last]; r["state"] = "ok"; }
    else { r["value"] = s->count >= 2 ? "Nobody yet" : "Write the class's names below"; r["state"] = "info"; }
    if (s->count >= 2 && !s->repeat){
        JsonObject q = now.add<JsonObject>();
        q["label"] = "This round";
        snprintf(t, sizeof(t), "%d of %u still to be picked", left_In_Round(*s), (unsigned)s->count);
        q["value"] = t;
        q["state"] = "info";
    }
    JsonArray gr = values["groups"].to<JsonArray>();
    for (int k = 0; k < g->n; k++){
        JsonObject o = gr.add<JsonObject>();
        snprintf(t, sizeof(t), "Group %d", k + 1);
        o["label"] = t;
        char list[PEOPLE * (NAME_LEN + 2)] = "";
        for (int m = g->start[k]; m < g->start[k + 1]; m++){ if (m > g->start[k]) strlcat(list, ", ", sizeof(list)); strlcat(list, s->name[g->order[m]], sizeof(list)); }
        o["value"] = list;
        o["state"] = "info";
    }
    heap_caps_free(s);
    heap_caps_free(g);
}
