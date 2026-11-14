/**
 * Typing Hero, ID {19,5} (KIDS, ages 7-12): touch typing on a USB keyboard plugged into Pixlpal (any USB keyboard,
 * through a hub too). 13 lessons, each three lines with the next key and its finger shown; falling letters to zap;
 * a one-minute word race. A lesson typed with enough accuracy opens the next. Play counts towards the daily limit.
 *
 * Keys: the arrows and Enter choose, Esc goes back. The knob works too: turn to choose, press to choose, hold to go
 * back (from the menu: who is playing).
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_usb_hid.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "typ_App.h"
#include "typ_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t typingHero_Task_H = NULL;
static void typingHero_App_Task(void* dApplication);
static void typ_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *typingHero_App;
Mtb_Applications* typingHero_App_GetInstance() {
    if (!typingHero_App){
        typingHero_App = new Mtb_Applications_StatusBar(typingHero_App_Task, &typingHero_Task_H, "Typing Hero", {19,5}, 8192);
        typingHero_App->mtb_App_Set_Mobile_UI(TYP_UI_MANIFEST, TYP_UI_API, nullptr, typ_Values);
    }
    return typingHero_App;
}
MTB_REGISTER_APP(typingHero_App, 19, 5)

#define LINES 3

EXT_RAM_BSS_ATTR static Typ_Settings_t settings;
EXT_RAM_BSS_ATTR static Typ_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false, resetRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static void read_Settings(Typ_Settings_t& s){
    typ_Settings_Default(s);
    Typ_Settings_t* saved = (Typ_Settings_t*)heap_caps_calloc(1, sizeof(Typ_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(TYP_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == TYP_SETTINGS_MAGIC) s = *saved;
    heap_caps_free(saved);
    typ_Settings_Clamp(s);
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
// 0 {"keyboardMap"} | {"unlockAll"} | {"passAccuracy"}   1 {} every child starts again
static void cmd_Options(JsonDocument& c){
    long v;
    Typ_Settings_t* n = (Typ_Settings_t*)heap_caps_malloc(sizeof(Typ_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (appOpen){ portENTER_CRITICAL(&requestLock); *n = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*n);
    if (!c["keyboardMap"].isNull() && mtb_Kit_Json_Range(c["keyboardMap"], 0, 1, v)) n->keyboardMap = (uint8_t)v;
    else if (!c["unlockAll"].isNull() && mtb_Kit_Json_Range(c["unlockAll"], 0, 1, v)) n->unlockAll = (uint8_t)v;
    else if (!c["passAccuracy"].isNull() && mtb_Kit_Json_Range(c["passAccuracy"], 50, 100, v)) n->passAccuracy = (uint8_t)v;
    else { heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); return; }
    typ_Settings_Clamp(*n);
    if (appOpen){ portENTER_CRITICAL(&requestLock); received = *n; settingsReceived = true; portEXIT_CRITICAL(&requestLock); }
    else mtb_Write_Nvs_Struct(TYP_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}
static void cmd_Reset(JsonDocument&){
    if (appOpen){ resetRequested = true; return; }
    Typ_Settings_t* n = (Typ_Settings_t*)heap_caps_malloc(sizeof(Typ_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    read_Settings(*n);
    const uint8_t map = n->keyboardMap, all = n->unlockAll, pass = n->passAccuracy;
    typ_Settings_Default(*n);
    n->keyboardMap = map; n->unlockAll = all; n->passAccuracy = pass;
    mtb_Write_Nvs_Struct(TYP_NVS_KEY, n, sizeof(*n));
    heap_caps_free(n);
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = 1;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static void typingHero_App_Task(void* dApplication){
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
    Typ_Fall_t* fall = (Typ_Fall_t*)heap_caps_calloc(1, sizeof(Typ_Fall_t), MALLOC_CAP_SPIRAM);
    if (!k || !fall){ heap_caps_free(k); heap_caps_free(fall); mtb_Delete_This_App(THIS_APP); return; }
    kk_Begin(*k);
    mtb_Usb_Input_Open(false);              // the keyboard types here (media keys stay system-wide)
    appOpen = true;
    uint32_t rng = esp_random() | 1;
    char line[TYP_LINE + 8] = "", typed[24] = "", nextWords[64] = "";
    const char* raceWord = "";
    const char* queue[4];
    Typ_View_t v = {};
    v.s = &settings;
    v.k = k;
    v.line = line;
    v.typed = typed;
    v.nextWords = nextWords;
    v.fall = fall;
    v.pick = k->child;
    v.screen = k->count > 1 ? TYP_SCR_PICK : TYP_SCR_MENU;
    uint32_t startMs = 0, doneMs = 0, frameMs = millis(), missMs = 0, lastSecond = 0, raceEnd = 0;
    uint32_t keys = 0, misses = 0, rightChars = 0;
    uint8_t want = TYP_SCR_MENU;            // where to go once a keyboard is plugged in
    bool redraw = true, dirty = false;

    auto child = [&]() -> uint8_t { return (uint8_t)(k->real[k->child] % 3); };
    auto open_Count = [&]() -> uint8_t { return settings.unlockAll ? TYP_LESSONS : (uint8_t)(settings.lesson[child()] + 1); };
    auto refill = [&](){
        nextWords[0] = '\0';
        for (int i = 1; i < 4; i++){ strncat(nextWords, queue[i], sizeof(nextWords) - strlen(nextWords) - 1); if (i < 3) strncat(nextWords, "  ", sizeof(nextWords) - strlen(nextWords) - 1); }
        raceWord = queue[0];
        v.word = raceWord;
    };
    auto start = [&](uint8_t screen){
        if (!mtb_Usb_Keyboard_Connected()){ want = screen; v.screen = TYP_SCR_NO_KEYBOARD; redraw = true; return; }
        const uint32_t now = (uint32_t)time(nullptr);
        v.stop = now > 1700000000u ? kk_Stop(*k, kk_Local_Minute(now)) : (uint8_t)KK_GO;
        if (v.stop != KK_GO){ v.screen = TYP_SCR_STOP; redraw = true; return; }
        keys = misses = rightChars = 0;
        startMs = 0;
        v.wpm = 0;
        v.accuracy = 100;
        v.missed = false;
        if (screen == TYP_SCR_LESSON){ v.lineNo = 0; v.lines = LINES; v.at = 0; typ_Line(v.lesson, rng, line, sizeof(line)); }
        else if (screen == TYP_SCR_FALL) typ_Fall_Start(*fall);
        else if (screen == TYP_SCR_RACE){
            for (int i = 0; i < 4; i++) queue[i] = typ_Race_Word(rng);
            refill();
            typed[0] = '\0';
            v.words = 0;
            raceEnd = 0;
            v.secondsLeft = 60;
        }
        v.screen = screen;
        redraw = true;
    };
    auto finish = [&](const char* title, uint8_t stars, const char* note){
        v.title = title;
        v.stars = stars;
        v.note = note;
        v.screen = TYP_SCR_DONE;
        doneMs = millis();
        kk_Add_Stars(*k, stars);
        kk_Sound(*k, stars == 3 ? KK_FANFARE : stars ? KK_YAY : KK_SOFT);
        dirty = redraw = true;
    };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings.keyboardMap = received.keyboardMap;
            settings.unlockAll = received.unlockAll;
            settings.passAccuracy = received.passAccuracy;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            dirty = redraw = true;
        }
        if (resetRequested){
            resetRequested = false;
            const uint8_t map = settings.keyboardMap, all = settings.unlockAll, pass = settings.passAccuracy;
            typ_Settings_Default(settings);
            settings.keyboardMap = map; settings.unlockAll = all; settings.passAccuracy = pass;
            v.screen = TYP_SCR_MENU;
            dirty = redraw = true;
        }
        const bool playing = v.screen == TYP_SCR_LESSON || v.screen == TYP_SCR_FALL || v.screen == TYP_SCR_RACE;
        if (now != lastSecond){
            lastSecond = now;
            if (now > 1700000000u){
                kk_Tick(*k, playing);
                if (playing && (v.stop = kk_Stop(*k, kk_Local_Minute(now))) != KK_GO){ v.screen = TYP_SCR_STOP; redraw = true; }
            }
        }

        // Input: the knob, then the keyboard.
        int t = turns;
        if (t) turns = 0;
        bool click = clicksRequested != 0;
        clicksRequested = 0;
        bool back = longPressRequested;
        longPressRequested = false;
        char typedKey = 0;
        bool backspace = false;
        Mtb_Usb_Input_t in;
        while (mtb_Usb_Input_Read(in, 0)){
            if (in.type != MTB_USB_KEY_DOWN) continue;
            switch (in.key){
                case 0x4F: case 0x51: if (!playing) t++; break;             // right, down
                case 0x50: case 0x52: if (!playing) t--; break;             // left, up
                case 0x28: case 0x58: if (!playing || v.screen == TYP_SCR_FALL) click = true; break;
                case 0x29: back = true; break;                              // Esc
                case 0x2A: backspace = true; break;
                default: if (in.ascii >= ' ' && in.ascii <= '~') typedKey = in.ascii; break;
            }
            if (typedKey && playing) break;                                 // one key a pass while typing
        }
        const int dir = t > 0 ? 1 : t < 0 ? -1 : 0;

        switch (v.screen){
            case TYP_SCR_PICK:
                if (dir){ v.pick = (uint8_t)((v.pick + k->count + dir) % k->count); redraw = true; }
                if (click){ kk_Select(*k, v.pick); v.screen = TYP_SCR_MENU; redraw = true; }
                break;
            case TYP_SCR_NO_KEYBOARD:
                if (mtb_Usb_Keyboard_Connected()) start(want);
                if (back || click){ v.screen = TYP_SCR_MENU; redraw = true; }
                break;
            case TYP_SCR_MENU:
                if (dir){ v.menu = (uint8_t)((v.menu + TYP_MENU + dir) % TYP_MENU); redraw = true; kk_Sound(*k, KK_CLICK); }
                if (click){
                    if (v.menu == TYP_M_LESSONS){ v.open = open_Count(); v.lesson = v.open - 1; v.screen = TYP_SCR_LESSONS; redraw = true; }
                    else start(v.menu == TYP_M_FALL ? TYP_SCR_FALL : TYP_SCR_RACE);
                }
                if (back && k->count > 1){ v.pick = k->child; v.screen = TYP_SCR_PICK; redraw = true; }
                break;
            case TYP_SCR_LESSONS:
                if (dir){ v.lesson = (v.lesson + TYP_LESSONS + dir) % TYP_LESSONS; redraw = true; }
                if (click){ if (v.lesson < v.open) start(TYP_SCR_LESSON); else kk_Sound(*k, KK_OOPS); }
                if (back){ v.screen = TYP_SCR_MENU; redraw = true; }
                break;
            case TYP_SCR_LESSON:
                if (back){ v.screen = TYP_SCR_LESSONS; redraw = true; break; }
                if (v.missed && ms - missMs > 250){ v.missed = false; redraw = true; }
                if (typedKey){
                    if (!startMs) startMs = ms;
                    keys++;
                    if (typedKey == line[v.at]){
                        v.at++;
                        rightChars++;
                        if (!line[v.at]){
                            // The line is done: the next, or the lesson's end.
                            if (++v.lineNo >= LINES){
                                const uint8_t c = child();
                                settings.accuracy[c] = (uint8_t)v.accuracy;
                                settings.lessonsDone[c]++;
                                const bool pass = v.accuracy >= settings.passAccuracy;
                                const char* note = nullptr;
                                if (pass && v.lesson == settings.lesson[c] && settings.lesson[c] + 1 < TYP_LESSONS){ settings.lesson[c]++; note = "A NEW LESSON IS OPEN!"; }
                                else if (!pass) note = "TRY AGAIN, A LITTLE SLOWER: AIM FOR FEWER MISTAKES";
                                if (v.wpm > settings.bestWpm[c]) settings.bestWpm[c] = (uint8_t)(v.wpm > 255 ? 255 : v.wpm);
                                finish(pass ? "WELL DONE!" : "GOOD TRY!", (uint8_t)(1 + (v.accuracy >= 90) + (v.accuracy >= 97)), note);
                            } else { v.at = 0; typ_Line(v.lesson, rng, line, sizeof(line)); }
                        }
                    } else { misses++; v.missed = true; missMs = ms; kk_Sound(*k, KK_CLICK); }
                    v.accuracy = keys ? (int)((keys - misses) * 100 / keys) : 100;
                    v.wpm = typ_Wpm(rightChars, startMs ? ms - startMs : 0);
                    redraw = true;
                }
                break;
            case TYP_SCR_DONE:
                v.ms = ms - doneMs;
                if (v.stars == 3 && v.ms < 2600) redraw = true;
                if (click || back){ v.screen = v.title && strcmp(v.title, "RACE OVER!") ? TYP_SCR_LESSONS : TYP_SCR_MENU; v.open = open_Count(); redraw = true; }
                break;
            case TYP_SCR_FALL:
                if (back){ v.screen = TYP_SCR_MENU; redraw = true; break; }
                if (fall->over){
                    if (click){ if (fall->score > settings.bestFall[child()]){ settings.bestFall[child()] = fall->score; dirty = true; } kk_Add_Stars(*k, (uint16_t)(fall->score / 10)); start(TYP_SCR_FALL); }
                    break;
                }
                if (typedKey){ if (typ_Fall_Key(*fall, typedKey)) kk_Sound(*k, KK_CLICK); }
                if (ms - frameMs >= 40){
                    const float dt = (ms - frameMs) / 1000.0f;
                    frameMs = ms;
                    const int letters = settings.unlockAll ? TYP_LESSONS - 4 : settings.lesson[child()];
                    typ_Fall_Step(*fall, dt, typ_Lesson(letters > 7 ? 7 : letters).keys, PANEL_RES_X, PANEL_RES_Y - MTB_KIT_TOP - 30, rng);
                    if (fall->over) kk_Sound(*k, KK_OOPS);
                    redraw = true;
                }
                break;
            case TYP_SCR_RACE: {
                if (back){ v.screen = TYP_SCR_MENU; redraw = true; break; }
                if (raceEnd) v.secondsLeft = ms >= raceEnd ? 0 : (raceEnd - ms + 999) / 1000;
                if (typedKey || backspace){
                    if (!raceEnd){ raceEnd = ms + 60000; startMs = ms; }
                    size_t n = strlen(typed);
                    if (backspace){ if (n) typed[n - 1] = '\0'; }
                    else if (typedKey == ' '){
                        if (n){
                            keys += (uint32_t)strlen(raceWord) + 1;
                            if (!strcmp(typed, raceWord)){ rightChars += (uint32_t)strlen(raceWord) + 1; v.words++; kk_Sound(*k, KK_CLICK); }
                            else misses += (uint32_t)strlen(raceWord) + 1;
                            queue[0] = queue[1]; queue[1] = queue[2]; queue[2] = queue[3]; queue[3] = typ_Race_Word(rng);
                            refill();
                            typed[0] = '\0';
                        }
                    } else if (n + 1 < sizeof(typed)){ typed[n] = typedKey; typed[n + 1] = '\0'; }
                    v.wpm = typ_Wpm(rightChars, ms - startMs);
                    v.accuracy = keys ? (int)((keys - misses) * 100 / keys) : 100;
                    redraw = true;
                }
                if (raceEnd && ms >= raceEnd){
                    v.wpm = typ_Wpm(rightChars, 60000);
                    if (v.wpm > settings.bestWpm[child()]){ settings.bestWpm[child()] = (uint8_t)(v.wpm > 255 ? 255 : v.wpm); }
                    finish("RACE OVER!", (uint8_t)((v.words >= 5) + (v.words >= 10) + (v.words >= 18)), v.wpm >= settings.bestWpm[child()] ? "YOUR BEST SPEED!" : nullptr);
                }
                if (raceEnd && now != lastSecond) redraw = true;
                static uint32_t tickMs = 0;
                if (raceEnd && ms - tickMs > 500){ tickMs = ms; redraw = true; }
                break;
            }
            case TYP_SCR_STOP:
                if (click || back){ v.screen = TYP_SCR_MENU; redraw = true; }
                break;
        }

        if (redraw){ redraw = false; typ_Draw(v); }
        if (dirty){ dirty = false; mtb_Write_Nvs_Struct(TYP_NVS_KEY, &settings, sizeof(settings)); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(15);
    }
    appOpen = false;
    mtb_Usb_Input_Close();
    kk_End(*k);
    mtb_Write_Nvs_Struct(TYP_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(k);
    heap_caps_free(fall);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void typ_Values(JsonObject values){
    Typ_Settings_t* s = (Typ_Settings_t*)heap_caps_malloc(sizeof(Typ_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    values["keyboardMap"] = s->keyboardMap;
    values["unlockAll"] = s->unlockAll;
    values["passAccuracy"] = s->passAccuracy;
    Kk_Session_t k;
    kk_Begin(k);
    JsonArray rows = values["status"].to<JsonArray>();
    char t[128];
    for (int c = 0; c < k.count; c++){
        const uint8_t r = k.real[c] % 3;
        if (s->lessonsDone[r] || s->bestWpm[r] || s->bestFall[r])
            snprintf(t, sizeof(t), "Lesson %u (%s). Best %u words a minute, last accuracy %u%%, falling letters %u",
                     s->lesson[r] + 1, typ_Lesson(s->lesson[r]).name, s->bestWpm[r], s->accuracy[r], s->bestFall[r]);
        else snprintf(t, sizeof(t), "Not started yet");
        JsonObject o = rows.add<JsonObject>();
        o["label"] = k.names[c];
        o["value"] = t;
        o["state"] = "info";
    }
    values["keyboard"] = mtb_Usb_Keyboard_Connected() ? 1 : 0;
    heap_caps_free(s);
}
