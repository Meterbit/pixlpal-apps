/**
 * Code Quest, ID {19,4} (KIDS, ages 6-12): children program a robot to the star with blocks (forward, turn, repeat,
 * repeat until the star, if a wall is ahead), then watch it run. 16 levels; each child's stars and last program are
 * kept. Play time counts towards the daily limit set in Kids Learning.
 *
 * The knob: turn to move the caret along the program (the green arrow at the end runs it); press to open the
 * blocks, turn to choose one, press to put it in (the arrow block deletes the one before the caret); hold to close
 * the blocks, or to go back to the levels (and from the levels to who is playing).
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "cdq_App.h"
#include "cdq_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t codeQuest_Task_H = NULL;
static void codeQuest_App_Task(void* dApplication);
static void cdq_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *codeQuest_App;
Mtb_Applications* codeQuest_App_GetInstance() {
    if (!codeQuest_App){
        codeQuest_App = new Mtb_Applications_StatusBar(codeQuest_App_Task, &codeQuest_Task_H, "Code Quest", {19,4}, 8192);
        codeQuest_App->mtb_App_Set_Mobile_UI(CDQ_UI_MANIFEST, CDQ_UI_API, nullptr, cdq_Values);
    }
    return codeQuest_App;
}
MTB_REGISTER_APP(codeQuest_App, 19, 4)

EXT_RAM_BSS_ATTR static Cdq_Settings_t settings;
EXT_RAM_BSS_ATTR static Cdq_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false, resetRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static void read_Settings(Cdq_Settings_t& s){
    cdq_Settings_Default(s);
    Cdq_Settings_t* saved = (Cdq_Settings_t*)heap_caps_calloc(1, sizeof(Cdq_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(CDQ_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == CDQ_SETTINGS_MAGIC) s = *saved;
    heap_caps_free(saved);
    cdq_Settings_Clamp(s);
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
// 0 {"unlockAll"} | {"speed"}
static void cmd_Options(JsonDocument& c){
    long v;
    Cdq_Settings_t* n = (Cdq_Settings_t*)heap_caps_malloc(sizeof(Cdq_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (appOpen){ portENTER_CRITICAL(&requestLock); *n = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*n);
    if (!c["unlockAll"].isNull() && mtb_Kit_Json_Range(c["unlockAll"], 0, 1, v)) n->unlockAll = (uint8_t)v;
    else if (!c["speed"].isNull() && mtb_Kit_Json_Range(c["speed"], 0, 2, v)) n->speed = (uint8_t)v;
    else { heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); return; }
    cdq_Settings_Clamp(*n);
    if (appOpen){ portENTER_CRITICAL(&requestLock); received = *n; settingsReceived = true; portEXIT_CRITICAL(&requestLock); }
    else mtb_Write_Nvs_Struct(CDQ_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}

// 1 {} every child starts again
static void cmd_Reset(JsonDocument&){
    if (appOpen){ resetRequested = true; return; }
    Cdq_Settings_t* n = (Cdq_Settings_t*)heap_caps_malloc(sizeof(Cdq_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    read_Settings(*n);
    memset(n->stars, 0, sizeof(n->stars));
    memset(n->saved, 0, sizeof(n->saved));
    memset(n->savedLevel, 0, sizeof(n->savedLevel));
    mtb_Write_Nvs_Struct(CDQ_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = 1;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static void codeQuest_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Options, cmd_Reset);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    settingsReceived = false;
    longPressRequested = false;
    resetRequested = false;
    clicksRequested = 0;
    turns = 0;
    Kk_Session_t* k = (Kk_Session_t*)heap_caps_calloc(1, sizeof(Kk_Session_t), MALLOC_CAP_SPIRAM);
    Cdq_Run_t* run = (Cdq_Run_t*)heap_caps_calloc(1, sizeof(Cdq_Run_t), MALLOC_CAP_SPIRAM);
    if (!k || !run){ heap_caps_free(k); heap_caps_free(run); mtb_Delete_This_App(THIS_APP); return; }
    kk_Begin(*k);
    appOpen = true;
    char program[CDQ_PROGRAM + 1] = "";
    char items[24];
    Cdq_View_t v = {};
    v.s = &settings;
    v.k = k;
    v.program = program;
    v.items = items;
    v.pick = k->child;
    v.screen = k->count > 1 ? CDQ_SCR_PICK : CDQ_SCR_LEVELS;
    uint32_t stepMs = 0, resultMs = 0, messageMs = 0, lastSecond = 0, savedMs = 0;
    bool redraw = true, dirty = false;
    static const uint16_t STEP[3] = {600, 350, 180};

    auto child = [&]() -> uint8_t { return (uint8_t)(k->real[k->child] % 3); };
    auto first_Open = [&](){
        // The child's level: the one they were on, else the first without a star.
        int l = settings.savedLevel[child()];
        if (!cdq_Unlocked(settings, child(), l)) l = 0;
        v.level = l;
    };
    auto open_Level = [&](int l){
        v.level = l;
        if (settings.savedLevel[child()] == l) snprintf(program, sizeof(program), "%s", settings.saved[child()]);
        else program[0] = '\0';
        v.caret = (int)strlen(program);
        v.palette = false;
        v.run = nullptr;
        v.message = nullptr;
        v.screen = CDQ_SCR_EDIT;
    };
    auto keep_Program = [&](){
        settings.savedLevel[child()] = (uint8_t)v.level;
        snprintf(settings.saved[child()], sizeof(settings.saved[0]), "%s", program);
        dirty = true;
    };
    if (v.screen == CDQ_SCR_LEVELS) first_Open();

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings.unlockAll = received.unlockAll;
            settings.speed = received.speed;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            dirty = redraw = true;
        }
        if (resetRequested){
            resetRequested = false;
            memset(settings.stars, 0, sizeof(settings.stars));
            memset(settings.saved, 0, sizeof(settings.saved));
            memset(settings.savedLevel, 0, sizeof(settings.savedLevel));
            program[0] = '\0';
            v.screen = CDQ_SCR_LEVELS;
            v.level = 0;
            dirty = redraw = true;
        }
        if (now != lastSecond){
            lastSecond = now;
            const bool playing = v.screen == CDQ_SCR_EDIT || v.screen == CDQ_SCR_RUN || v.screen == CDQ_SCR_RESULT;
            if (now > 1700000000u){
                kk_Tick(*k, playing);
                if (playing && (v.stop = kk_Stop(*k, kk_Local_Minute(now))) != KK_GO){ keep_Program(); v.screen = CDQ_SCR_STOP; redraw = true; kk_Sound(*k, KK_SOFT); }
            }
        }
        if (v.message && ms - messageMs > 2500){ v.message = nullptr; redraw = true; }

        int t = turns;
        if (t) turns = 0;
        const bool click = clicksRequested != 0;
        clicksRequested = 0;
        const bool hold = longPressRequested;
        longPressRequested = false;
        const int dir = t > 0 ? 1 : t < 0 ? -1 : 0;

        switch (v.screen){
            case CDQ_SCR_PICK:
                if (dir){ v.pick = (uint8_t)((v.pick + k->count + dir) % k->count); redraw = true; }
                if (click){ kk_Select(*k, v.pick); first_Open(); v.screen = CDQ_SCR_LEVELS; redraw = true; }
                break;
            case CDQ_SCR_LEVELS:
                if (dir){ v.level = (v.level + CDQ_LEVELS + dir) % CDQ_LEVELS; redraw = true; kk_Sound(*k, KK_CLICK); }
                if (click){
                    if (cdq_Unlocked(settings, child(), v.level)){
                        v.stop = now > 1700000000u ? kk_Stop(*k, kk_Local_Minute(now)) : (uint8_t)KK_GO;
                        if (v.stop != KK_GO) v.screen = CDQ_SCR_STOP; else open_Level(v.level);
                    } else kk_Sound(*k, KK_OOPS);
                    redraw = true;
                }
                if (hold && k->count > 1){ v.pick = k->child; v.screen = CDQ_SCR_PICK; redraw = true; }
                break;
            case CDQ_SCR_EDIT: {
                const Cdq_Level_t& l = cdq_Level(v.level);
                const int n = (int)strlen(program);
                if (v.palette){
                    if (dir){ v.item = (v.item + v.itemCount + dir) % v.itemCount; redraw = true; kk_Sound(*k, KK_CLICK); }
                    if (hold){ v.palette = false; redraw = true; break; }
                    if (click){
                        const char b = items[v.item];
                        if (b == CDQ_PAL_CLOSE) v.palette = false;
                        else if (b == CDQ_PAL_DELETE){
                            if (v.caret > 0){ memmove(program + v.caret - 1, program + v.caret, (size_t)(n - v.caret + 1)); v.caret--; }
                        } else if (n >= l.maxBlocks || n >= CDQ_PROGRAM){ v.message = "NO MORE BLOCKS: USE A REPEAT?"; messageMs = ms; kk_Sound(*k, KK_OOPS); }
                        else {
                            memmove(program + v.caret + 1, program + v.caret, (size_t)(n - v.caret + 1));
                            program[v.caret++] = b;
                        }
                        keep_Program();
                        redraw = true;
                    }
                    break;
                }
                if (dir){ v.caret = (v.caret + (n + 2) + dir) % (n + 2); redraw = true; }
                if (hold){ keep_Program(); v.screen = CDQ_SCR_LEVELS; redraw = true; break; }
                if (click){
                    if (v.caret == n + 1){
                        // Run it.
                        cdq_Run(l, program, *run);
                        v.run = run;
                        v.step = 0;
                        stepMs = ms;
                        v.screen = run->result == CDQ_BAD_PROGRAM ? CDQ_SCR_RESULT : CDQ_SCR_RUN;
                        if (v.screen == CDQ_SCR_RESULT){ resultMs = ms; kk_Sound(*k, KK_OOPS); }
                    } else {
                        v.itemCount = cdq_Palette(l, items, 20);
                        if (v.caret > 0) items[v.itemCount++] = CDQ_PAL_DELETE;
                        items[v.itemCount++] = CDQ_PAL_CLOSE;
                        v.item = 0;
                        v.palette = true;
                    }
                    redraw = true;
                }
                break;
            }
            case CDQ_SCR_RUN:
                if (click){ v.step = run->steps - 1; stepMs = 0; }
                if (ms - stepMs >= STEP[settings.speed]){
                    stepMs = ms;
                    if (v.step + 1 < run->steps){
                        v.step++;
                        if (run->step[v.step].gem) kk_Sound(*k, KK_CLICK);
                    } else {
                        // The end: stars kept (the best), new stars to the child.
                        if (run->result == CDQ_GOAL){
                            uint8_t& best = settings.stars[child()][v.level];
                            if (run->stars > best){ kk_Add_Stars(*k, (uint16_t)(run->stars - best)); best = run->stars; }
                            keep_Program();
                            kk_Sound(*k, run->stars == 3 ? KK_FANFARE : KK_YAY);
                        } else kk_Sound(*k, KK_OOPS);
                        v.screen = CDQ_SCR_RESULT;
                        resultMs = ms;
                    }
                    redraw = true;
                }
                break;
            case CDQ_SCR_RESULT:
                v.ms = ms - resultMs;
                if (run->result == CDQ_GOAL && v.ms < 3000) redraw = true;
                if (click || hold){
                    if (run->result == CDQ_GOAL && v.level + 1 < CDQ_LEVELS && !hold){ open_Level(v.level + 1); keep_Program(); }
                    else if (run->result == CDQ_GOAL){ v.screen = CDQ_SCR_LEVELS; }
                    else { v.screen = CDQ_SCR_EDIT; v.run = nullptr; }
                    redraw = true;
                }
                break;
            case CDQ_SCR_STOP:
                if (click || hold){ v.screen = CDQ_SCR_LEVELS; redraw = true; }
                break;
        }

        if (redraw){ redraw = false; cdq_Draw(v); }
        if (dirty && ms - savedMs > 5000){ dirty = false; savedMs = ms; mtb_Write_Nvs_Struct(CDQ_NVS_KEY, &settings, sizeof(settings)); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(25);
    }
    appOpen = false;
    kk_End(*k);
    mtb_Write_Nvs_Struct(CDQ_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(k);
    heap_caps_free(run);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void cdq_Values(JsonObject values){
    Cdq_Settings_t* s = (Cdq_Settings_t*)heap_caps_malloc(sizeof(Cdq_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    values["unlockAll"] = s->unlockAll;
    values["speed"] = s->speed;
    Kk_Session_t k;
    kk_Begin(k);
    JsonArray rows = values["status"].to<JsonArray>();
    char t[96];
    for (int c = 0; c < k.count; c++){
        const uint8_t real = k.real[c] % 3;
        int done = 0, stars = 0, highest = 0;
        for (int i = 0; i < CDQ_LEVELS; i++){ if (s->stars[real][i]){ done++; stars += s->stars[real][i]; highest = i + 1; } }
        if (done) snprintf(t, sizeof(t), "%d of %d levels, %d of %d stars. Up to \"%s\"", done, CDQ_LEVELS, stars, CDQ_LEVELS * 3, cdq_Level(highest - 1).name);
        else snprintf(t, sizeof(t), "Not started yet");
        JsonObject o = rows.add<JsonObject>();
        o["label"] = k.names[c];
        o["value"] = t;
        o["state"] = done == CDQ_LEVELS ? "ok" : "info";
    }
    heap_caps_free(s);
}
