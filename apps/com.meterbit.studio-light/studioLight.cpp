/**
 * Studio Light app, ID {6,0}: the whole panel as a light. Solid colour, two colours split or in a gradient, a cycle
 * of five colours (cut or faded), white light at a colour temperature (1800-10000 K, for photos and video calls),
 * and effects: breathing, candle, rainbow, aurora, and a sunrise wake light that brightens over minutes.
 *
 * The knob: turn, the intensity (the colour temperature in white mode); click, the next colour, warmth or effect;
 * double click, the next mode; long press, light on or off. A short label shows what changed.
 * The picture: stl_Model.cpp.
 */
#include "mtb_engine.h"
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "stl_Model.h"
#include "stl_Mobile_Ui.h"

#define STL_FRAME_MS        40
#define STL_LABEL_MS        1500

EXT_RAM_BSS_ATTR TaskHandle_t studioLight_Task_H = NULL;
void studioLight_App_Task(void *);
static void stl_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen *studioLight_App;
Mtb_Applications* studioLight_App_GetInstance() {
    if (!studioLight_App){
        studioLight_App = new Mtb_Applications_FullScreen(studioLight_App_Task, &studioLight_Task_H, "Studio Light", {6,0}, 4096);
        studioLight_App->mtb_App_Set_Mobile_UI(STL_UI_MANIFEST, STL_UI_API, nullptr, stl_Mobile_Ui_Values);
    }
    return studioLight_App;
}
MTB_REGISTER_APP(studioLight_App, 6, 0)

EXT_RAM_BSS_ATTR static Stl_Settings_t stlSettings;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Stl_Settings_t receivedSettings;
static volatile uint8_t clicksRequested = 0;
static volatile int8_t turnsRequested = 0;
static volatile bool longPressRequested = false;

static void stl_Button(button_event_t button);
static void stl_Knob(rotary_encoder_rotation_t direction);
static void stl_Ble_Color(JsonDocument& command);
static void stl_Ble_Brightness(JsonDocument& command);
static void stl_Ble_Mode(JsonDocument& command);
static void stl_Ble_Interval(JsonDocument& command);
static void stl_Ble_Kelvin(JsonDocument& command);
static void stl_Ble_Effect(JsonDocument& command);
static void stl_Ble_Intensity(JsonDocument& command);
static void stl_Ble_Power(JsonDocument& command);
static void stl_Ble_Fade(JsonDocument& command);
static void stl_Ble_Sunrise(JsonDocument& command);

static const uint16_t KELVIN_STEPS[] = {2700, 3200, 4000, 5000, 5600, 6500};
static const uint32_t COLOR_PRESETS[] = {0xFFFFFF, 0xFF2020, 0xFF8000, 0xFFD000, 0x20FF40, 0x00C0FF, 0x2040FF, 0xA020FF, 0xFF40A0};

//##############################################################################################################
static bool stl_Read_Saved_Settings(Stl_Settings_t& settings){
    stl_Settings_Default(settings);
    Stl_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(STL_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == STL_SETTINGS_MAGIC;
    if (ok) settings = saved;
    stl_Settings_Clamp(settings);
    return ok;
}

static void stl_Save_Settings(void){ mtb_Write_Nvs_Struct(STL_NVS_KEY, &stlSettings, sizeof(Stl_Settings_t)); }

//##############################################################################################################
void studioLight_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(stl_Button, stl_Knob);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(stl_Ble_Color, stl_Ble_Brightness, stl_Ble_Mode, stl_Ble_Interval, stl_Ble_Kelvin, stl_Ble_Effect,
                                       stl_Ble_Intensity, stl_Ble_Power, stl_Ble_Fade, stl_Ble_Sunrise);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!stl_Read_Saved_Settings(stlSettings)) stl_Save_Settings();
    uint8_t* rgb = (uint8_t*)heap_caps_malloc(PANEL_RES_X * PANEL_RES_Y * 3, MALLOC_CAP_SPIRAM);
    if (rgb == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    settingsReceived = false;
    clicksRequested = 0;
    turnsRequested = 0;
    longPressRequested = false;

    uint32_t modeStartMs = millis(), drawnMs = 0, labelUntilMs = 0, saveDueMs = 0;
    char label[32] = "";
    bool redraw = true;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const uint8_t oldMode = stlSettings.mode, oldEffect = stlSettings.effect, oldPower = stlSettings.power;
            stlSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            stl_Settings_Clamp(stlSettings);
            stl_Save_Settings();
            // A new mode, effect, or switching on: the effect (the sunrise above all) starts from its beginning.
            if (stlSettings.mode != oldMode || stlSettings.effect != oldEffect || stlSettings.power != oldPower) modeStartMs = millis();
            redraw = true;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        // The knob.
        const uint8_t clicks = clicksRequested;
        const int8_t turns = turnsRequested;
        if (clicks) clicksRequested = 0;
        if (turns) turnsRequested = 0;
        Stl_Settings_t& s = stlSettings;
        bool changed = false;
        if (longPressRequested){
            longPressRequested = false;
            s.power = !s.power;
            snprintf(label, sizeof(label), "%s", s.power ? "LIGHT ON" : "");
            modeStartMs = millis();
            changed = true;
        }
        if (turns && s.power){
            if (s.mode == STL_WHITE){
                int k = s.kelvin + turns * 100;
                s.kelvin = (uint16_t)(k < 1800 ? 1800 : k > 10000 ? 10000 : k);
                snprintf(label, sizeof(label), "%u K", (unsigned)s.kelvin);
            } else {
                int level = s.intensity + turns * 2;
                s.intensity = (uint8_t)(level < 1 ? 1 : level > 100 ? 100 : level);
                snprintf(label, sizeof(label), "%u%%", (unsigned)s.intensity);
            }
            changed = true;
        }
        if (clicks == 2 && s.power){
            s.mode = (uint8_t)((s.mode + 1) % STL_MODE_COUNT);
            snprintf(label, sizeof(label), "%s", stl_Mode_Name(s.mode));
            modeStartMs = millis();
            changed = true;
        }
        if (clicks == 1 && s.power){
            if (s.mode == STL_WHITE){
                int i = 0;
                while (i < 6 && KELVIN_STEPS[i] <= s.kelvin) i++;
                s.kelvin = KELVIN_STEPS[i % 6];
                snprintf(label, sizeof(label), "%u K", (unsigned)s.kelvin);
            } else if (s.mode == STL_EFFECT){
                s.effect = (uint8_t)((s.effect + 1) % STL_EFFECT_COUNT);
                snprintf(label, sizeof(label), "%s", stl_Effect_Name(s.effect));
                modeStartMs = millis();
            } else {
                // The first colour through the presets.
                int i = 0;
                while (i < 9 && COLOR_PRESETS[i] != s.colors[0]) i++;
                s.colors[0] = COLOR_PRESETS[(i + 1) % 9];
                snprintf(label, sizeof(label), "COLOUR %d", (i + 1) % 9 + 1);
            }
            changed = true;
        }
        if (changed){
            labelUntilMs = millis() + STL_LABEL_MS;
            saveDueMs = millis() + 2000;          // Knob turns are saved once they stop.
            redraw = true;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        if (saveDueMs && (int32_t)(millis() - saveDueMs) >= 0){ saveDueMs = 0; stl_Save_Settings(); }

        const bool labelShown = labelUntilMs && (int32_t)(labelUntilMs - millis()) > 0;
        if (labelUntilMs && !labelShown){ labelUntilMs = 0; redraw = true; }
        if (stl_Animated(s) && millis() - drawnMs >= STL_FRAME_MS) redraw = true;
        if (redraw){
            redraw = false;
            drawnMs = millis();
            stl_Render(s, millis() - modeStartMs, rgb, PANEL_RES_X, PANEL_RES_Y);
            mtb_Panel_Begin_Update();
            mtb_Panel_Draw_FrameRGB888(0, 0, PANEL_RES_X, PANEL_RES_Y, rgb);
            if (labelShown && label[0]){
                const int16_t w = mtb_Kit_Text_Width(Terminal8x12, label) + 12;
                mtb_Panel_Fill_Rect(128 - w / 2, 108, 128 + w / 2, 124, BLACK);
                mtb_Kit_Text_Centre(128, 111, Terminal8x12, KIT_TEXT, label);
            }
            mtb_Panel_End_Update();
        }
        delay(10);
    }
    if (saveDueMs) stl_Save_Settings();
    heap_caps_free(rgb);
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void stl_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 2) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void stl_Knob(rotary_encoder_rotation_t direction){
    const int8_t turns = turnsRequested;
    if (direction == ROT_CLOCKWISE && turns < 30) turnsRequested = turns + 1;
    else if (direction == ROT_COUNTERCLOCKWISE && turns > -30) turnsRequested = turns - 1;
}

static Stl_Settings_t* stl_Begin_Change(void){
    Stl_Settings_t* next = (Stl_Settings_t*)heap_caps_malloc(sizeof(Stl_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : stlSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void stl_End_Change(Stl_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

// What the phone shows. Runs on the BLE parser task.
static void stl_Mobile_Ui_Values(JsonObject values){
    Stl_Settings_t s;
    portENTER_CRITICAL(&requestLock);
    s = settingsReceived ? receivedSettings : stlSettings;
    portEXIT_CRITICAL(&requestLock);
    if (s.magic != STL_SETTINGS_MAGIC) stl_Read_Saved_Settings(s);
    values["power"] = s.power;
    values["lightMode"] = s.mode;
    values["kelvin"] = s.kelvin;
    values["effect"] = s.effect;
    values["intensity"] = s.intensity;
    values["dInterval"] = s.cycleSeconds;
    values["fade"] = s.fade;
    values["sunrise"] = s.sunriseMinutes;
    values["value"] = (int)((panelBrightness * 100 + 127) / 255);
    char text[64];
    static const char* const MODES[STL_MODE_COUNT] = {"Solid colour", "Two colours, split", "Colour cycle", "Gradient", "White light", "Effect"};
    if (!s.power) snprintf(text, sizeof(text), "Off");
    else if (s.mode == STL_WHITE) snprintf(text, sizeof(text), "White light, %u K, at %u%%", (unsigned)s.kelvin, (unsigned)s.intensity);
    else if (s.mode == STL_EFFECT){
        static const char* const EFFECTS[STL_EFFECT_COUNT] = {"Breathing", "Candle", "Rainbow", "Aurora", "Sunrise"};
        snprintf(text, sizeof(text), "%s, at %u%%", EFFECTS[s.effect % STL_EFFECT_COUNT], (unsigned)s.intensity);
    } else snprintf(text, sizeof(text), "%s, at %u%%", MODES[s.mode % STL_MODE_COUNT], (unsigned)s.intensity);
    values["status"] = text;
    values["statusState"] = s.power ? "ok" : "off";
}

// Command 0: {"colorIndex": 0-4, "colorVal": "0xFFRRGGBB"}
static void stl_Ble_Color(JsonDocument& command){
    long index;
    const char* text = command["colorVal"];
    if (!mtb_Kit_Json_Range(command["colorIndex"], 0, STL_COLORS - 1, index) || text == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    uint16_t check;
    if (!mtb_Kit_Parse_Color(text, check)){ mtb_Ble_App_Cmd_Reject(); return; }
    const char* hex = text[0] == '#' ? text + 1 : text + 2;
    const unsigned long value = strtoul(hex, nullptr, 16);
    Stl_Settings_t* next = stl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->colors[index] = (uint32_t)(value & 0xFFFFFF);
    next->power = 1;
    stl_End_Change(next);
}

// Command 1: {"value": 1-100}: the panel's brightness (all apps), as the old page did.
static void stl_Ble_Brightness(JsonDocument& command){
    long percent;
    if (!mtb_Kit_Json_Range(command["value"], 1, 100, percent)){ mtb_Ble_App_Cmd_Reject(); return; }
    panelBrightness = (uint8_t)(percent * 255 / 100);
    mtb_Panel_Set_Brightness(panelBrightness);
    mtb_Write_Nvs_Struct("pan_brghnss", &panelBrightness, sizeof(uint8_t));
}

static void stl_Byte(JsonDocument& command, const char* key, long low, long high, uint8_t Stl_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], low, high, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Stl_Settings_t* next = stl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    if (field != &Stl_Settings_t::power) next->power = 1;
    stl_End_Change(next);
}

// Command 2: {"lightMode": 0 solid, 1 split, 2 cycle, 3 gradient, 4 white, 5 effect}
static void stl_Ble_Mode(JsonDocument& command){ stl_Byte(command, "lightMode", 0, STL_MODE_COUNT - 1, &Stl_Settings_t::mode); }

// Command 3: {"dInterval": 1-600} seconds per colour of the cycle (the old page also sent "lightMode": 2).
static void stl_Ble_Interval(JsonDocument& command){
    long seconds;
    if (!mtb_Kit_Json_Range(command["dInterval"], 1, 600, seconds)){ mtb_Ble_App_Cmd_Reject(); return; }
    Stl_Settings_t* next = stl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->cycleSeconds = (uint16_t)seconds;
    long mode;
    if (mtb_Kit_Json_Range(command["lightMode"], 0, STL_MODE_COUNT - 1, mode)) next->mode = (uint8_t)mode;
    stl_End_Change(next);
}

// Command 4: {"kelvin": 1800-10000}
static void stl_Ble_Kelvin(JsonDocument& command){
    long kelvin;
    if (!mtb_Kit_Json_Range(command["kelvin"], 1800, 10000, kelvin)){ mtb_Ble_App_Cmd_Reject(); return; }
    Stl_Settings_t* next = stl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->kelvin = (uint16_t)kelvin;
    next->mode = STL_WHITE;
    next->power = 1;
    stl_End_Change(next);
}

// Commands 5-9: {"effect": 0-4}, {"intensity": 1-100}, {"power": 0-1}, {"fade": 0-1}, {"sunrise": 1-60 minutes}
static void stl_Ble_Effect(JsonDocument& command){ stl_Byte(command, "effect", 0, STL_EFFECT_COUNT - 1, &Stl_Settings_t::effect); }
static void stl_Ble_Intensity(JsonDocument& command){ stl_Byte(command, "intensity", 1, 100, &Stl_Settings_t::intensity); }
static void stl_Ble_Power(JsonDocument& command){ stl_Byte(command, "power", 0, 1, &Stl_Settings_t::power); }
static void stl_Ble_Fade(JsonDocument& command){ stl_Byte(command, "fade", 0, 1, &Stl_Settings_t::fade); }
static void stl_Ble_Sunrise(JsonDocument& command){ stl_Byte(command, "sunrise", 1, 60, &Stl_Settings_t::sunriseMinutes); }
