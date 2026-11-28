/**
 * Life & Automata: cellular automata on the whole screen (life_Engine.cpp), offline. Cells of 1, 2 or 4 pixels; up
 * to 30 generations a second. Drawn as four one-colour layers (a cell that just died, one just born, an adult, an old
 * one), so new growth shows bright and still life darker. When a world settles (repeats, or dies out) it is seeded
 * again; it can also move to the next rule by itself. Knob: turn for the rule, click to seed again, double-click to
 * pause. NVS "lifSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "life_Engine.h"
#include "lif_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#define W           256
#define H           128
#define ROW_BYTES   (W / 8)
#define NVS_KEY     "lifSave"
#define SAVE_MAGIC  0x4C494601u
#define LABEL_MS    2500u
#define THEMES      4

EXT_RAM_BSS_ATTR TaskHandle_t lifeAutomata_Task_H = NULL;
static void lifeAutomata_App_Task(void* dApplication);
static void lif_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* lifeAutomata_App;
Mtb_Applications* lifeAutomata_App_GetInstance(){
    if (!lifeAutomata_App){
        lifeAutomata_App = new Mtb_Applications_FullScreen(lifeAutomata_App_Task, &lifeAutomata_Task_H, "Life & Automata", {0,0}, 8192);
        lifeAutomata_App->mtb_App_Set_Mobile_UI(LIF_UI_MANIFEST, LIF_UI_API, nullptr, lif_Values);
    }
    return lifeAutomata_App;
}
MTB_REGISTER_APP(lifeAutomata_App, 0, 0)        // the catalogue gives the app its number

struct Lif_Save_t { uint32_t magic; uint8_t rule, cells, speed, theme, autoMin, labels, pad[2]; };

static EXT_RAM_BSS_ATTR Lif_Save_t save;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false, reseedWanted = false, restartWanted = false;
static volatile int pausedWanted = -1;
static volatile uint32_t shownGeneration = 0, shownPopulation = 0;
static volatile bool shownPaused = false;

static void read_Save(Lif_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.cells = 1; s.speed = 12; }
    if (s.rule >= LIFE_RULE_COUNT) s.rule = 0;
    if (s.cells > 2) s.cells = 1;
    if (s.speed < 1 || s.speed > 30) s.speed = 12;
    if (s.theme >= THEMES) s.theme = 0;
    if (s.autoMin > 30) s.autoMin = 0;
    s.labels = s.labels ? 1 : 0;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static bool range_Cmd(JsonDocument& c, const char* key, long lo, long hi, uint8_t& out){
    long v;
    if (!mtb_Kit_Json_Range(c[key], lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return false; }
    out = (uint8_t)v;
    phoneChanged = saveWanted = true;
    return true;
}
static void cmd_Rule(JsonDocument& c){ if (range_Cmd(c, "rule", 0, LIFE_RULE_COUNT - 1, save.rule)) restartWanted = true; }
static void cmd_Cells(JsonDocument& c){ if (range_Cmd(c, "cells", 0, 2, save.cells)) restartWanted = true; }
static void cmd_Speed(JsonDocument& c){ range_Cmd(c, "speed", 1, 30, save.speed); }
static void cmd_Theme(JsonDocument& c){ range_Cmd(c, "theme", 0, THEMES - 1, save.theme); }
static void cmd_Auto(JsonDocument& c){ range_Cmd(c, "auto", 0, 30, save.autoMin); }
static void cmd_Labels(JsonDocument& c){ range_Cmd(c, "labels", 0, 1, save.labels); }
static void cmd_Seed(JsonDocument&){ reseedWanted = true; phoneChanged = true; }
static void cmd_Paused(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["paused"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } pausedWanted = (int)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0, clicks = 0, doubles = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED){ if (b.count >= 2) doubles++; else clicks++; } }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ----------------------------------------------------------------------------------------------------------------------------
// Each theme: the trail (just died), just born, adult, old (Brian's Brain: dying).
static const uint16_t THEME[THEMES][4] = {
    {MTB_KIT_RGB(26, 10, 40), MTB_KIT_RGB(120, 255, 255), MTB_KIT_RGB(255, 60, 200), MTB_KIT_RGB(110, 40, 200)},
    {MTB_KIT_RGB(40, 8, 0), MTB_KIT_RGB(255, 240, 120), MTB_KIT_RGB(255, 130, 0), MTB_KIT_RGB(200, 30, 0)},
    {MTB_KIT_RGB(0, 24, 6), MTB_KIT_RGB(220, 255, 220), MTB_KIT_RGB(40, 230, 70), MTB_KIT_RGB(0, 120, 30)},
    {MTB_KIT_RGB(6, 14, 36), MTB_KIT_RGB(255, 255, 255), MTB_KIT_RGB(120, 200, 255), MTB_KIT_RGB(40, 90, 200)},
};

static uint16_t widen2[256];            // a byte of cells, each 2 pixels wide
static uint32_t widen4[256];            // each 4 pixels wide

static void make_Tables(void){
    for (int b = 0; b < 256; b++){
        uint16_t a = 0; uint32_t c = 0;
        for (int i = 0; i < 8; i++) if ((b >> i) & 1){ a |= (uint16_t)(3u << (2 * i)); c |= 0xFu << (4 * i); }
        widen2[b] = a; widen4[b] = c;
    }
}

/** One layer's cells, as pixels: @p pick(y, w) gives the layer's word of cells. */
template <typename F>
static bool layer(const Life_t& L, int scale, uint8_t* out, F pick){
    bool any = false;
    for (int y = 0; y < L.h; y++){
        uint8_t* row = out + (size_t)y * scale * ROW_BYTES;
        for (int w = 0; w < L.words; w++){
            const uint64_t v = pick(y, w);
            if (v) any = true;
            for (int k = 0; k < 8; k++){
                const uint8_t b = (uint8_t)(v >> (8 * k));
                const int cell = w * 64 + k * 8;            // the byte's first cell
                if (scale == 1) row[cell / 8] = b;
                else if (scale == 2){ const uint16_t p = widen2[b]; row[cell / 4] = (uint8_t)p; row[cell / 4 + 1] = (uint8_t)(p >> 8); }
                else { const uint32_t p = widen4[b]; row[cell / 2] = (uint8_t)p; row[cell / 2 + 1] = (uint8_t)(p >> 8); row[cell / 2 + 2] = (uint8_t)(p >> 16); row[cell / 2 + 3] = (uint8_t)(p >> 24); }
            }
        }
        for (int r = 1; r < scale; r++) memcpy(row + r * ROW_BYTES, row, ROW_BYTES);
    }
    return any;
}

static void draw(const Life_t& L, int scale, uint8_t theme, uint8_t* buf){
    mtb_Panel_Fill_Screen(BLACK);
    const uint16_t* c = THEME[theme];
    const Life_Rule_t& r = LIFE_RULES[L.rule];
    if (r.kind == LIFE_ELEMENTARY){
        // A scroll: the newest lines at the bottom brightest.
        if (layer(L, scale, buf, [&](int y, int w){ return y >= L.h - L.h / 4 ? 0 : L.cur[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[2]);
        if (layer(L, scale, buf, [&](int y, int w){ return y >= L.h - L.h / 4 ? L.cur[y][w] : 0; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[1]);
        return;
    }
    if (r.kind == LIFE_BRIAN){
        if (layer(L, scale, buf, [&](int y, int w){ return L.prev[y][w] & ~L.cur[y][w] & ~L.older[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[0]);
        if (layer(L, scale, buf, [&](int y, int w){ return L.older[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[3]);
        if (layer(L, scale, buf, [&](int y, int w){ return L.cur[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[1]);
        return;
    }
    if (layer(L, scale, buf, [&](int y, int w){ return L.prev[y][w] & ~L.cur[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[0]);
    if (layer(L, scale, buf, [&](int y, int w){ return L.cur[y][w] & ~L.prev[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[1]);
    if (layer(L, scale, buf, [&](int y, int w){ return L.cur[y][w] & L.prev[y][w] & ~L.older[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[2]);
    if (layer(L, scale, buf, [&](int y, int w){ return L.older[y][w]; })) mtb_Panel_Draw_Mono_Fg(0, 0, W, H, buf, ROW_BYTES, 0, c[3]);
}

static void label(const Life_t& L, const char* extra){
    char t[64];
    const Life_Rule_t& r = LIFE_RULES[L.rule];
    snprintf(t, sizeof(t), "%s  %s%s%s", r.name, r.kind == LIFE_ELEMENTARY ? "" : r.code, extra[0] ? "  " : "", extra);
    const int w = mtb_Kit_Text_Width(Terminal4x6, t);
    mtb_Panel_Fill_Rect(0, H - 9, (int16_t)(w + 5), H - 1, MTB_KIT_RGB(0, 0, 0));
    mtb_Kit_Text(3, H - 7, Terminal4x6, KIT_TEXT, t, BLACK);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void lifeAutomata_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Rule, cmd_Cells, cmd_Speed, cmd_Theme, cmd_Auto, cmd_Labels, cmd_Seed, cmd_Paused);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = clicks = doubles = 0; phoneChanged = saveWanted = reseedWanted = restartWanted = false; pausedWanted = -1;
    Life_t* L = (Life_t*)heap_caps_calloc(1, sizeof(Life_t), MALLOC_CAP_SPIRAM);
    uint8_t* buf = (uint8_t*)heap_caps_calloc(1, ROW_BYTES * H, MALLOC_CAP_SPIRAM);
    if (!L || !buf){ heap_caps_free(L); heap_caps_free(buf); mtb_Delete_This_App(THIS_APP); return; }
    make_Tables();
    appOpen = true;
    auto start = [&](void){
        const int scale = 1 << save.cells;
        life_Init(*L, W / scale, H / scale, save.rule, pxl_random());
    };
    start();
    uint32_t lastStep = millis(), labelUntil = millis() + LABEL_MS, ruleSince = millis(), settledAt = 0, pushedMs = 0;
    bool paused = false, push = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = true; }
        // The knob and the phone.
        const int t = turns, c = clicks, dc = doubles;
        if (t){ turns -= t; save.rule = (uint8_t)(((save.rule + t) % LIFE_RULE_COUNT + LIFE_RULE_COUNT) % LIFE_RULE_COUNT); restartWanted = true; saveWanted = push = true; }
        if (c){ clicks -= c; reseedWanted = true; }
        if (dc){ doubles -= dc; paused = !paused; labelUntil = ms + LABEL_MS; push = true; }
        if (pausedWanted >= 0){ paused = pausedWanted != 0; pausedWanted = -1; labelUntil = ms + LABEL_MS; }
        if (save.autoMin && ms - ruleSince >= save.autoMin * 60000u){ save.rule = (uint8_t)((save.rule + 1) % LIFE_RULE_COUNT); restartWanted = true; saveWanted = push = true; }
        if (restartWanted){ restartWanted = reseedWanted = false; start(); labelUntil = ms + LABEL_MS; ruleSince = ms; settledAt = 0; push = true; }
        if (reseedWanted){ reseedWanted = false; life_Seed(*L, pxl_random()); labelUntil = ms + LABEL_MS; settledAt = 0; push = true; }
        // The generations, at the speed asked (at most two a frame, so a slow frame doesn't run away).
        if (!paused){
            const uint32_t every = 1000u / save.speed;
            int steps = 0;
            while (ms - lastStep >= every && steps < 2){ life_Step(*L); lastStep += every; steps++; }
            if (ms - lastStep >= every) lastStep = ms;
            // Settled: a moment to look at it, then a new seed.
            if (life_Settled(*L)){ if (!settledAt) settledAt = ms ? ms : 1; else if (ms - settledAt >= 2000u){ life_Seed(*L, pxl_random()); settledAt = 0; } }
            else settledAt = 0;
        } else lastStep = ms;
        draw(*L, 1 << save.cells, save.theme, buf);
        if (paused) label(*L, "PAUSED");
        else if (save.labels || (int32_t)(labelUntil - ms) > 0){
            char g[24];
            snprintf(g, sizeof(g), "GEN %lu", (unsigned long)L->generation);
            label(*L, g);
        }
        shownGeneration = L->generation; shownPopulation = L->population; shownPaused = paused;
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push && ms - pushedMs >= 2000) || ms - pushedMs >= 30000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    heap_caps_free(L);
    heap_caps_free(buf);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void lif_Values(JsonObject values){
    Lif_Save_t s;
    if (appOpen) s = save; else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Rule";
    o["value"] = LIFE_RULES[s.rule].name;
    o["state"] = "info";
    if (appOpen){
        char t[64];
        o = rows.add<JsonObject>();
        o["label"] = shownPaused ? "Paused" : "Running";
        snprintf(t, sizeof(t), "Generation %lu, %lu cells alive", (unsigned long)shownGeneration, (unsigned long)shownPopulation);
        o["value"] = t;
        o["state"] = shownPaused ? "warn" : "ok";
    }
    values["rule"] = s.rule;
    values["cells"] = s.cells;
    values["speed"] = s.speed;
    values["theme"] = s.theme;
    values["auto"] = s.autoMin;
    values["labels"] = s.labels;
    values["paused"] = appOpen && shownPaused ? 1 : 0;
}
