/**
 * Tell the Time, ID {19,1} (KIDS, ages 5-8): a big clock with the time in words and the family's day, and rounds of
 * eight questions: set the clock with the knob, read the clock, and what time it will be later. Levels move with
 * how the child does. Stars and play time are the child's in Kids Learning (one daily limit for every kids app).
 *
 * The knob: turn to choose or to move the hands; press to choose or check; hold to go back (from the menu: who is
 * playing).
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ttm_App.h"
#include "ttm_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t tellTime_Task_H = NULL;
static void tellTime_App_Task(void* dApplication);
static void ttm_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *tellTime_App;
Mtb_Applications* tellTime_App_GetInstance() {
    if (!tellTime_App){
        tellTime_App = new Mtb_Applications_StatusBar(tellTime_App_Task, &tellTime_Task_H, "Tell the Time", {19,1}, 8192);
        tellTime_App->mtb_App_Set_Mobile_UI(TTM_UI_MANIFEST, TTM_UI_API, nullptr, ttm_Values);
    }
    return tellTime_App;
}
MTB_REGISTER_APP(tellTime_App, 19, 1)

EXT_RAM_BSS_ATTR static Ttm_Settings_t settings;
EXT_RAM_BSS_ATTR static Ttm_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;
static volatile int8_t playing = -1;            // the child playing, for the phone

static void read_Settings(Ttm_Settings_t& s){
    ttm_Settings_Default(s);
    Ttm_Settings_t* saved = (Ttm_Settings_t*)heap_caps_calloc(1, sizeof(Ttm_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(TTM_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == TTM_SETTINGS_MAGIC) s = *saved;
    heap_caps_free(saved);
    ttm_Settings_Clamp(s);
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Ttm_Settings_t* begin_Change(void){
    Ttm_Settings_t* n = (Ttm_Settings_t*)heap_caps_malloc(sizeof(Ttm_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n) return nullptr;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *n = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*n);
    return n;
}
static void drop_Change(Ttm_Settings_t* n){ heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); }
static void end_Change(Ttm_Settings_t* n){
    ttm_Settings_Clamp(*n);
    if (appOpen){ portENTER_CRITICAL(&requestLock); received = *n; settingsReceived = true; portEXIT_CRITICAL(&requestLock); }
    else mtb_Write_Nvs_Struct(TTM_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}

// 0 {"startLevel": 0-5, or 255 by age}
static void cmd_Level(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["startLevel"], 0, 255, v) || (v >= TTM_LEVELS && v != 255)){ mtb_Ble_App_Cmd_Reject(); return; }
    Ttm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->startLevel = (uint8_t)v;
    memset(n->level, 0xFF, sizeof(n->level));       // everyone starts again from it
    end_Change(n);
}

// 1 {"kSet"} | {"kRead"} | {"kPass"} | {"hour24"} | {"words"}
static void cmd_Switch(JsonDocument& c){
    long v;
    Ttm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    const char* keys[TTM_KINDS] = {"kSet", "kRead", "kPass"};
    for (int k = 0; k < TTM_KINDS; k++){
        if (c[keys[k]].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[keys[k]], 0, 1, v)){ drop_Change(n); return; }
        if (v) n->kinds |= (uint8_t)(1u << k); else n->kinds &= (uint8_t)~(1u << k);
        end_Change(n);
        return;
    }
    if (!c["hour24"].isNull() && mtb_Kit_Json_Range(c["hour24"], 0, 1, v)) n->hour24 = (uint8_t)v;
    else if (!c["words"].isNull() && mtb_Kit_Json_Range(c["words"], 0, 1, v)) n->words = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// 2 {"event": "07:30 School bus"}   3 {} remove the last   4 {} clear the day
static void cmd_Event(JsonDocument& c){
    Ttm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!ttm_Add_Event(*n, c["event"] | "")){ drop_Change(n); return; }
    end_Change(n);
}
static void cmd_Remove(JsonDocument&){
    Ttm_Settings_t* n = begin_Change();
    if (!n || !n->eventCount){ if (n) drop_Change(n); else mtb_Ble_App_Cmd_Reject(); return; }
    n->eventCount--;
    end_Change(n);
}
static void cmd_Clear(JsonDocument&){
    Ttm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->eventCount = 0;
    end_Change(n);
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = 1;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static void tellTime_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Level, cmd_Switch, cmd_Event, cmd_Remove, cmd_Clear);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    settingsReceived = false;
    longPressRequested = false;
    clicksRequested = 0;
    turns = 0;
    Kk_Session_t* k = (Kk_Session_t*)heap_caps_calloc(1, sizeof(Kk_Session_t), MALLOC_CAP_SPIRAM);
    Ttm_Question_t* q = (Ttm_Question_t*)heap_caps_calloc(1, sizeof(Ttm_Question_t), MALLOC_CAP_SPIRAM);
    if (!k || !q){ heap_caps_free(k); heap_caps_free(q); mtb_Delete_This_App(THIS_APP); return; }
    kk_Begin(*k);
    appOpen = true;
    playing = (int8_t)k->child;
    uint32_t rng = esp_random() | 1;
    Ttm_View_t v = {};
    v.s = &settings;
    v.k = k;
    v.q = q;
    v.screen = k->count > 1 ? TTM_SCR_PICK : TTM_SCR_MENU;
    v.pick = k->child;
    uint8_t tries = 0;
    uint32_t feedbackMs = 0, lastSecond = 0, confettiStart = 0;
    bool redraw = true, dirty = false;

    auto level_Of = [&]() -> uint8_t {
        uint8_t& l = settings.level[k->real[k->child] % 3];
        if (l == 0xFF) l = settings.startLevel != 0xFF ? settings.startLevel : ttm_Level_For_Age(kk_Age(*k));
        return l;
    };
    auto next_Question = [&](){
        v.number++;
        ttm_Question(level_Of(), settings.kinds, rng, *q);
        v.set = q->shown;
        v.choice = 0;
        v.feedback = 0;
        tries = 0;
        if (q->kind == TTM_SET) kk_Say(*k, q->prompt);
    };
    auto start_Round = [&](){
        v.number = 0;
        v.right = 0;
        v.levelUp = false;
        v.screen = TTM_SCR_QUESTION;
        next_Question();
    };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const Ttm_Settings_t keep = settings;
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            // Levels and counts played here meanwhile are kept unless the phone reset them.
            for (int i = 0; i < 3; i++){
                settings.rounds[i] = keep.rounds[i]; settings.right[i] = keep.right[i]; settings.asked[i] = keep.asked[i];
                if (settings.level[i] != 0xFF) settings.level[i] = keep.level[i];
            }
            dirty = redraw = true;
        }
        v.level = level_Of();
        if (now != lastSecond){
            lastSecond = now;
            v.minute = kk_Local_Minute(now);
            const time_t tt = (time_t)now;
            struct tm lt;
            localtime_r(&tt, &lt);
            v.second = (uint8_t)lt.tm_sec;
            const bool playingNow = v.screen == TTM_SCR_QUESTION || v.screen == TTM_SCR_RESULT;
            if (now > 1700000000u) kk_Tick(*k, playingNow);
            if (playingNow && now > 1700000000u){
                v.stop = kk_Stop(*k, v.minute);
                if (v.stop != KK_GO){ v.screen = TTM_SCR_STOP; kk_Sound(*k, KK_SOFT); }
            }
            if (v.screen == TTM_SCR_CLOCK || v.screen == TTM_SCR_MENU) redraw = true;
        }

        int t = turns;
        if (t) turns = 0;
        const bool click = clicksRequested != 0;
        clicksRequested = 0;
        const bool hold = longPressRequested;
        longPressRequested = false;

        switch (v.screen){
            case TTM_SCR_PICK:
                if (t){ v.pick = (uint8_t)((v.pick + k->count + (t > 0 ? 1 : -1)) % k->count); redraw = true; kk_Sound(*k, KK_CLICK); }
                if (click){ kk_Select(*k, v.pick); playing = (int8_t)k->child; v.screen = TTM_SCR_MENU; redraw = true; }
                break;
            case TTM_SCR_MENU:
                if (t){ v.menu ^= 1; redraw = true; kk_Sound(*k, KK_CLICK); }
                if (click){
                    if (v.menu == 0) v.screen = TTM_SCR_CLOCK;
                    else {
                        v.stop = kk_Stop(*k, v.minute);
                        if (v.stop != KK_GO && now > 1700000000u) v.screen = TTM_SCR_STOP; else start_Round();
                    }
                    redraw = true;
                }
                if (hold && k->count > 1){ v.screen = TTM_SCR_PICK; v.pick = k->child; redraw = true; }
                break;
            case TTM_SCR_CLOCK:
            case TTM_SCR_STOP:
                if (click || hold){ v.screen = TTM_SCR_MENU; redraw = true; }
                break;
            case TTM_SCR_QUESTION:
                if (hold){ v.screen = TTM_SCR_MENU; redraw = true; break; }
                if (v.feedback){
                    // Right: the next one shortly. Wrong the second time: shown the answer, then on.
                    if (ms - feedbackMs > (v.feedback == 1 ? 1300u : 2800u) || click){
                        if (v.feedback == 2 && tries < 2){ v.feedback = 0; redraw = true; break; }
                        if (v.number >= TTM_ROUND){
                            // The round's end: stars, and the level moves.
                            const uint8_t real = k->real[k->child] % 3;
                            settings.rounds[real]++;
                            const int stars = v.right >= 7 ? 3 : v.right >= 5 ? 2 : v.right >= 3 ? 1 : 0;
                            kk_Add_Stars(*k, (uint16_t)(v.right + stars));
                            const uint8_t top = (settings.kinds & (1u << TTM_PASS)) ? TTM_LATER : TTM_MINUTE;
                            uint8_t& l = settings.level[real];
                            if (v.right >= 7 && l < top){ l++; v.levelUp = true; }
                            else if (v.right <= 3 && l > 0) l--;
                            dirty = true;
                            v.screen = TTM_SCR_RESULT;
                            confettiStart = v.right >= 6 ? ms : 0;
                            kk_Sound(*k, v.levelUp ? KK_LEVEL_UP : v.right >= 6 ? KK_FANFARE : KK_SOFT);
                        } else next_Question();
                        redraw = true;
                    }
                    break;
                }
                if (t){
                    if (q->kind == TTM_SET){
                        const int step = ttm_Step(v.level) * (t > 1 || t < -1 ? (v.level == TTM_MINUTE ? 5 : 1) * (t > 0 ? 1 : -1) : t);
                        v.set = (uint16_t)(((int)v.set + step + 720 * 4) % 720);
                    } else v.choice = (uint8_t)((v.choice + 3 + (t > 0 ? 1 : -1)) % 3);
                    redraw = true;
                }
                if (click){
                    const bool right = q->kind == TTM_SET ? ttm_Set_Right(*q, v.set) : q->choices[v.choice] == q->target;
                    tries++;
                    const uint8_t real = k->real[k->child] % 3;
                    if (tries == 1) settings.asked[real]++;
                    if (right){
                        if (tries == 1){ v.right++; settings.right[real]++; }
                        v.feedback = 1;
                        kk_Sound(*k, KK_YAY);
                    } else {
                        v.feedback = 2;
                        kk_Sound(*k, KK_OOPS);
                        if (tries >= 2){ if (q->kind == TTM_SET) v.set = q->target; }    // the answer, shown
                    }
                    feedbackMs = ms;
                    dirty = redraw = true;
                }
                break;
            case TTM_SCR_RESULT:
                if (click){ start_Round(); redraw = true; }
                if (hold){ v.screen = TTM_SCR_MENU; redraw = true; }
                if (confettiStart && ms - confettiStart < 2500) redraw = true;
                break;
        }
        v.confettiMs = (v.screen == TTM_SCR_RESULT && confettiStart && ms - confettiStart < 2500) ? ms - confettiStart + 1 : 0;

        if (redraw){
            redraw = false;
            if (now < 1700000000u && v.screen == TTM_SCR_CLOCK){ v.screen = TTM_SCR_MENU; }
            ttm_Draw(v);
        }
        if (dirty){ dirty = false; mtb_Write_Nvs_Struct(TTM_NVS_KEY, &settings, sizeof(settings)); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    playing = -1;
    kk_End(*k);
    mtb_Write_Nvs_Struct(TTM_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(k);
    heap_caps_free(q);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void ttm_Values(JsonObject values){
    Ttm_Settings_t* s = begin_Change();
    if (!s) return;
    values["startLevel"] = s->startLevel;
    values["kSet"] = (s->kinds >> TTM_SET) & 1;
    values["kRead"] = (s->kinds >> TTM_READ) & 1;
    values["kPass"] = (s->kinds >> TTM_PASS) & 1;
    values["hour24"] = s->hour24;
    values["words"] = s->words;
    char t[96], a[16];
    JsonArray day = values["dayRows"].to<JsonArray>();
    for (int i = 0; i < s->eventCount; i++){
        JsonObject o = day.add<JsonObject>();
        ttm_Digital(a, sizeof(a), s->events[i].minute, true);
        o["label"] = a;
        o["value"] = s->events[i].label;
        o["state"] = "info";
    }
    // How each child is doing (the names are Kids Learning's).
    Kk_Session_t k;
    kk_Begin(k);
    JsonArray rows = values["status"].to<JsonArray>();
    for (int c = 0; c < k.count; c++){
        const uint8_t real = k.real[c] % 3;
        const uint8_t lv = s->level[real] != 0xFF ? s->level[real] : (s->startLevel != 0xFF ? s->startLevel : ttm_Level_For_Age(k.ages[c]));
        if (s->asked[real]) snprintf(t, sizeof(t), "Level %u (%s), %u rounds, %u%% right", lv + 1, ttm_Level_Name(lv), s->rounds[real],
                                     (unsigned)(s->right[real] * 100u / s->asked[real]));
        else snprintf(t, sizeof(t), "Starts at level %u (%s)", lv + 1, ttm_Level_Name(lv));
        JsonObject o = rows.add<JsonObject>();
        o["label"] = k.names[c];
        o["value"] = t;
        o["state"] = playing == c ? "ok" : "info";
    }
    heap_caps_free(s);
}
