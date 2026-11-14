/**
 * Treasure Hunt, ID {19,3} (KIDS, ages 5-12): clues round the house or garden. A grown-up sets the clues on the phone
 * (or picks a ready-made hunt) and hides a card with each clue's 3-digit code where the clue leads; the child enters
 * the code with the knob to open the next clue, and the last opens the treasure. Without codes, the grown-up moves the
 * hunt on from the phone. Children are away from the screen most of the time, so it doesn't count towards the limit.
 *
 * The knob: press to start, and on a clue to enter its code; turn to change a digit, press for the next; hold on a
 * clue for its hint (on the start: who is playing).
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "trh_App.h"
#include "trh_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t treasureHunt_Task_H = NULL;
static void treasureHunt_App_Task(void* dApplication);
static void trh_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *treasureHunt_App;
Mtb_Applications* treasureHunt_App_GetInstance() {
    if (!treasureHunt_App){
        treasureHunt_App = new Mtb_Applications_StatusBar(treasureHunt_App_Task, &treasureHunt_Task_H, "Treasure Hunt", {19,3}, 8192);
        treasureHunt_App->mtb_App_Set_Mobile_UI(TRH_UI_MANIFEST, TRH_UI_API, nullptr, trh_Values);
    }
    return treasureHunt_App;
}
MTB_REGISTER_APP(treasureHunt_App, 19, 3)

EXT_RAM_BSS_ATTR static Trh_Settings_t settings;
EXT_RAM_BSS_ATTR static Trh_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0, goRequested = 0;     // 1 start, 2 next clue, 3 hint
static volatile int turns = 0;

// The hunt under way, kept while Pixlpal runs (the app may close and open again in the middle).
static volatile int8_t huntClue = -1;           // -1 not started, count: found the treasure
static uint32_t huntStart = 0, huntEnd = 0;

static void read_Settings(Trh_Settings_t& s){
    trh_Settings_Default(s);
    Trh_Settings_t* saved = (Trh_Settings_t*)heap_caps_calloc(1, sizeof(Trh_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(TRH_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == TRH_SETTINGS_MAGIC) s = *saved;
    heap_caps_free(saved);
    trh_Settings_Clamp(s);
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Trh_Settings_t* begin_Change(void){
    Trh_Settings_t* n = (Trh_Settings_t*)heap_caps_malloc(sizeof(Trh_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n) return nullptr;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *n = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*n);
    return n;
}
static void drop_Change(Trh_Settings_t* n){ heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); }
static void end_Change(Trh_Settings_t* n, bool newHunt){
    trh_Settings_Clamp(*n);
    if (newHunt) huntClue = -1;                 // the clues changed: the hunt starts again
    if (appOpen){ portENTER_CRITICAL(&requestLock); received = *n; settingsReceived = true; portEXIT_CRITICAL(&requestLock); }
    else mtb_Write_Nvs_Struct(TRH_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}

// 0 {"clue": "text | hint"}   1 {} remove the last   2 {} clear   3 {"preset": 0-2}   6 {} new codes
static void cmd_Add(JsonDocument& c){
    Trh_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!trh_Add_Clue(*n, c["clue"] | "")){ drop_Change(n); return; }
    end_Change(n, true);
}
static void cmd_Remove(JsonDocument&){
    Trh_Settings_t* n = begin_Change();
    if (!n || !n->count){ if (n) drop_Change(n); else mtb_Ble_App_Cmd_Reject(); return; }
    n->count--;
    end_Change(n, true);
}
static void cmd_Clear(JsonDocument&){
    Trh_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->count = 0;
    end_Change(n, true);
}
static void cmd_Preset(JsonDocument& c){
    long v;
    Trh_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->seed ^= esp_random();
    if (!mtb_Kit_Json_Range(c["preset"], 0, TRH_PRESETS - 1, v) || !trh_Preset(*n, (uint8_t)v)){ drop_Change(n); return; }
    end_Change(n, true);
}
// 4 {"useCodes"} | {"showTimer"} | {"prize"}
static void cmd_Options(JsonDocument& c){
    long v;
    Trh_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["useCodes"].isNull() && mtb_Kit_Json_Range(c["useCodes"], 0, 1, v)) n->useCodes = (uint8_t)v;
    else if (!c["showTimer"].isNull() && mtb_Kit_Json_Range(c["showTimer"], 0, 1, v)) n->showTimer = (uint8_t)v;
    else if (!c["prize"].isNull()){
        const char* p = c["prize"] | "";
        if (strlen(p) >= sizeof(n->prize)){ drop_Change(n); return; }
        snprintf(n->prize, sizeof(n->prize), "%s", p);
        for (char* q = n->prize; *q; q++) if ((unsigned char)*q < ' ') *q = ' ';
    } else { drop_Change(n); return; }
    end_Change(n, false);
}
// 5 {"go": 1 start | 2 next clue | 3 hint}: the grown-up's buttons (the app must be open).
static void cmd_Go(JsonDocument& c){
    long v;
    if (!appOpen || !mtb_Kit_Json_Range(c["go"], 1, 3, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    goRequested = (uint8_t)v;
}
static void cmd_Codes(JsonDocument&){
    Trh_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->seed ^= esp_random();
    trh_New_Codes(*n);
    end_Change(n, true);
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = 1;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static void treasureHunt_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Add, cmd_Remove, cmd_Clear, cmd_Preset, cmd_Options, cmd_Go, cmd_Codes);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    settingsReceived = false;
    longPressRequested = false;
    clicksRequested = 0;
    goRequested = 0;
    turns = 0;
    Kk_Session_t* k = (Kk_Session_t*)heap_caps_calloc(1, sizeof(Kk_Session_t), MALLOC_CAP_SPIRAM);
    if (!k){ mtb_Delete_This_App(THIS_APP); return; }
    kk_Begin(*k);
    appOpen = true;
    Trh_View_t v = {};
    v.s = &settings;
    v.k = k;
    v.pick = k->child;
    uint32_t screenAt = millis(), wrongAt = 0, frameMs = 0, lastSecond = 0;
    bool redraw = true, dirty = false;

    auto show = [&](){
        if (!settings.count) v.screen = TRH_SCR_EMPTY;
        else if (huntClue < 0) v.screen = TRH_SCR_READY;
        else if (huntClue >= settings.count) v.screen = TRH_SCR_TREASURE;
        else { v.screen = TRH_SCR_CLUE; v.clue = (uint8_t)huntClue; }
        screenAt = millis();
        v.hint = false;
        redraw = true;
    };
    auto found = [&](){
        kk_Add_Stars(*k, 1);
        huntClue = (int8_t)(huntClue + 1);
        if (huntClue >= settings.count){
            huntEnd = (uint32_t)time(nullptr);
            const uint32_t took = huntEnd - huntStart;
            v.best = !settings.bestSeconds || took < settings.bestSeconds;
            if (v.best){ settings.bestSeconds = (uint16_t)(took > 65535 ? 65535 : took); dirty = true; }
            kk_Add_Stars(*k, 3);
            kk_Sound(*k, KK_FANFARE);
        } else kk_Sound(*k, KK_YAY);
        show();
    };
    if (huntClue > (int)settings.count) huntClue = -1;
    v.screen = k->count > 1 && huntClue < 0 ? TRH_SCR_PICK : 0;
    if (v.screen != TRH_SCR_PICK) show();

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const uint16_t best = settings.bestSeconds;
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            if (!settings.bestSeconds || (best && best < settings.bestSeconds)) settings.bestSeconds = best;
            dirty = true;
            if (v.screen != TRH_SCR_PICK) show();
        }
        kk_Tick(*k, false);
        const uint8_t go = goRequested;
        goRequested = 0;
        if (go == 1){ huntClue = 0; huntStart = now; v.digit = 0; memset(v.digits, 0, 3); kk_Sound(*k, KK_LEVEL_UP); show(); }
        else if (go == 2 && huntClue >= 0 && huntClue < settings.count) found();
        else if (go == 3 && v.screen == TRH_SCR_CLUE){ v.hint = true; screenAt = ms; redraw = true; }

        int t = turns;
        if (t) turns = 0;
        const bool click = clicksRequested != 0;
        clicksRequested = 0;
        const bool hold = longPressRequested;
        longPressRequested = false;
        const int dir = t > 0 ? 1 : t < 0 ? -1 : 0;

        switch (v.screen){
            case TRH_SCR_PICK:
                if (dir){ v.pick = (uint8_t)((v.pick + k->count + dir) % k->count); redraw = true; }
                if (click){ kk_Select(*k, v.pick); show(); }
                break;
            case TRH_SCR_READY:
                if (click){ huntClue = 0; huntStart = now; kk_Sound(*k, KK_LEVEL_UP); show(); }
                if (hold && k->count > 1){ v.pick = k->child; v.screen = TRH_SCR_PICK; redraw = true; }
                if (ms - frameMs > 500){ frameMs = ms; redraw = true; }
                break;
            case TRH_SCR_CLUE:
                if (hold){ v.hint = !v.hint; redraw = true; }
                if (click && settings.useCodes){ v.screen = TRH_SCR_CODE; v.digit = 0; memset(v.digits, 0, 3); v.wrong = 0; redraw = true; }
                if (v.hint && ms - screenAt > 15000){ v.hint = false; redraw = true; }
                break;
            case TRH_SCR_CODE:
                if (v.wrong){
                    if (ms - wrongAt > 1200){ v.wrong = 0; v.digit = 0; memset(v.digits, 0, 3); }
                    redraw = true;
                    break;
                }
                if (hold){ show(); break; }
                if (dir){ v.digits[v.digit] = (uint8_t)((v.digits[v.digit] + 10 + dir) % 10); kk_Sound(*k, KK_CLICK); redraw = true; }
                if (click){
                    if (v.digit < 2){ v.digit++; redraw = true; }
                    else {
                        const uint16_t entered = (uint16_t)(v.digits[0] * 100 + v.digits[1] * 10 + v.digits[2]);
                        if (huntClue >= 0 && huntClue < settings.count && entered == settings.clues[huntClue].code) found();
                        else { v.wrong = 1; wrongAt = ms; kk_Sound(*k, KK_OOPS); redraw = true; }
                    }
                }
                break;
            case TRH_SCR_TREASURE:
                if (click || hold){ huntClue = -1; show(); }
                if (ms - screenAt < 4500 && ms - frameMs > 60){ frameMs = ms; redraw = true; }
                break;
            default:
                break;
        }
        if (now != lastSecond){ lastSecond = now; if (v.screen == TRH_SCR_CLUE && settings.showTimer) redraw = true; }
        v.seconds = huntClue >= settings.count ? huntEnd - huntStart : (huntClue >= 0 ? now - huntStart : 0);
        v.ms = ms - screenAt;
        if (v.screen == TRH_SCR_CLUE && ms - frameMs > 400){ frameMs = ms; redraw = true; }
        if (redraw){ redraw = false; trh_Draw(v); }
        if (dirty){ dirty = false; mtb_Write_Nvs_Struct(TRH_NVS_KEY, &settings, sizeof(settings)); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    kk_End(*k);
    mtb_Write_Nvs_Struct(TRH_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(k);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void trh_Values(JsonObject values){
    Trh_Settings_t* s = begin_Change();
    if (!s) return;
    values["useCodes"] = s->useCodes;
    values["showTimer"] = s->showTimer;
    values["prize"] = s->prize;
    char label[40], t[64];
    JsonArray rows = values["clueRows"].to<JsonArray>();
    for (int i = 0; i < s->count; i++){
        JsonObject o = rows.add<JsonObject>();
        if (s->useCodes) snprintf(label, sizeof(label), "%d. Card code %03u", i + 1, s->clues[i].code);
        else snprintf(label, sizeof(label), "Clue %d", i + 1);
        o["label"] = label;
        o["value"] = s->clues[i].text;
        o["state"] = huntClue > i ? "ok" : huntClue == i ? "warn" : "info";
    }
    JsonArray st = values["status"].to<JsonArray>();
    JsonObject o = st.add<JsonObject>();
    o["label"] = "The hunt";
    const int c = huntClue;
    if (!appOpen) o["value"] = "Open Treasure Hunt on Pixlpal to play";
    else if (c < 0) o["value"] = "Ready to start";
    else if (c >= s->count){ trh_Time_Text(label, sizeof(label), huntEnd - huntStart); snprintf(t, sizeof(t), "Treasure found in %s!", label); o["value"] = t; }
    else { trh_Time_Text(label, sizeof(label), (uint32_t)time(nullptr) - huntStart); snprintf(t, sizeof(t), "On clue %d of %u (%s)", c + 1, s->count, label); o["value"] = t; }
    o["state"] = c >= 0 && c < s->count ? "warn" : "ok";
    values["huntOn"] = appOpen && c >= 0 && c < s->count ? 1 : 0;
    if (s->bestSeconds){ JsonObject b = st.add<JsonObject>(); trh_Time_Text(label, sizeof(label), s->bestSeconds); b["label"] = "Best time"; b["value"] = label; b["state"] = "info"; }
    heap_caps_free(s);
}
