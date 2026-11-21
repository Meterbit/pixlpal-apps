/**
 * Big Clock app, ID {0,3}: the time across the whole panel, in four faces (giant segments, bold type, a dial, words)
 * and seven colour themes, with a night mode for the bedside. Runs without the internet once the clock is set.
 * Show: the face with its date, the time alone, or the date alone (large, for two Pixlpals side by side).
 *
 * The faces are drawn by bgc_Ui.cpp; the settings, palettes and words are in bgc_Model.cpp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mtb_ntp.h"
#include "mtb_nvs.h"
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "bgc_Model.h"
#include "bgc_Ui.h"
#include "bgc_Mobile_Ui.h"

#define BGC_CLOCK_VALID_AFTER   1700000000L

EXT_RAM_BSS_ATTR TaskHandle_t bigClockCalendar_Task_H = NULL;
void bigClock_App_Task(void *dApplication);
static void bgc_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen *bigClockCalendar_App;
Mtb_Applications* bigClockCalendar_App_GetInstance() {
    if (!bigClockCalendar_App){
        bigClockCalendar_App = new Mtb_Applications_FullScreen(bigClock_App_Task, &bigClockCalendar_Task_H, "big Clock", {0,3}, 6144);
        bigClockCalendar_App->mtb_App_Set_Mobile_UI(BGC_UI_MANIFEST, BGC_UI_API, nullptr, bgc_Mobile_Ui_Values);
    }
    return bigClockCalendar_App;
}
MTB_REGISTER_APP(bigClockCalendar_App, 0, 3)

EXT_RAM_BSS_ATTR static Bgc_Settings_t bgcSettings;

// Requests from the button and BLE tasks; the app task applies them.
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
static Bgc_Settings_t receivedSettings;

static void bgc_Button(button_event_t button);
static void bgc_Ble_Face(JsonDocument& command);
static void bgc_Ble_Theme(JsonDocument& command);
static void bgc_Ble_Color(JsonDocument& command);
static void bgc_Ble_Hour24(JsonDocument& command);
static void bgc_Ble_Seconds(JsonDocument& command);
static void bgc_Ble_Date(JsonDocument& command);
static void bgc_Ble_Auto_Face(JsonDocument& command);
static void bgc_Ble_Night(JsonDocument& command);
static void bgc_Ble_Night_Times(JsonDocument& command);
static void bgc_Ble_Night_Style(JsonDocument& command);
static void bgc_Ble_Sync(JsonDocument& command);
static void bgc_Ble_Show(JsonDocument& command);

//##############################################################################################################
static bool bgc_Read_Saved_Settings(Bgc_Settings_t& settings){
    Bgc_Settings_t defaults;
    bgc_Settings_Default(defaults);
    settings = defaults;
    mtb_Read_Nvs_Struct(BGC_NVS_KEY, &settings, sizeof(Bgc_Settings_t));
    const bool saved = settings.magic == BGC_SETTINGS_MAGIC;
    if (!saved) settings = defaults;
    bgc_Settings_Clamp(settings);
    return saved;
}

static void bgc_Save_Settings(void){ mtb_Write_Nvs_Struct(BGC_NVS_KEY, &bgcSettings, sizeof(Bgc_Settings_t)); }

static bool bgc_Night_Now(const Bgc_Settings_t& settings, const struct tm& local){
    return settings.night && bgc_In_Window(settings.nightFrom, settings.nightTo, (uint16_t)(local.tm_hour * 60 + local.tm_min));
}

//##############################################################################################################
void bigClock_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(bgc_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(bgc_Ble_Face, bgc_Ble_Theme, bgc_Ble_Color, bgc_Ble_Hour24, bgc_Ble_Seconds, bgc_Ble_Date,
                                       bgc_Ble_Auto_Face, bgc_Ble_Night, bgc_Ble_Night_Times, bgc_Ble_Night_Style, bgc_Ble_Sync,
                                       bgc_Ble_Show);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!bgc_Read_Saved_Settings(bgcSettings)) bgc_Save_Settings();
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint8_t face = bgcSettings.face;
    uint32_t faceSinceMs = millis(), lastPushMs = 0;
    int lastSecond = -1;
    bool redraw = true, wasNight = false, wasValid = false;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const uint8_t oldFace = bgcSettings.face;
            bgcSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            bgc_Settings_Clamp(bgcSettings);
            bgc_Save_Settings();
            if (bgcSettings.face != oldFace){ face = bgcSettings.face; faceSinceMs = millis(); }
            redraw = true;
        }

        // The knob: click, next face; double click, next theme; triple click, what to show (clock and date, clock,
        // calendar); long press, faces rotate or stay.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){
                longPressRequested = false;
                bgcSettings.autoFaceMinutes = bgcSettings.autoFaceMinutes ? 0 : 5;
            } else if (clicks == 1){
                face = (face + 1) % BGC_FACE_COUNT;
                bgcSettings.face = face;
                faceSinceMs = millis();
            } else if (clicks == 2) bgcSettings.theme = (bgcSettings.theme + 1) % BGC_THEME_COUNT;
            else if (clicks == 3) bgcSettings.show = (bgcSettings.show + 1) % BGC_SHOW_COUNT;
            bgc_Save_Settings();
            redraw = true;
        }
        if (bgcSettings.autoFaceMinutes && millis() - faceSinceMs >= bgcSettings.autoFaceMinutes * 60000UL){
            face = (face + 1) % BGC_FACE_COUNT;
            faceSinceMs = millis();
            redraw = true;
        }

        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        const bool valid = now > BGC_CLOCK_VALID_AFTER;
        const bool night = valid && bgc_Night_Now(bgcSettings, local);
        if (night != wasNight || valid != wasValid){ wasNight = night; wasValid = valid; redraw = true; }

        if (redraw || local.tm_sec != lastSecond){
            lastSecond = local.tm_sec;
            redraw = false;
            Bgc_View_t view = {};
            view.settings = &bgcSettings;
            view.palette = bgc_Palette(bgcSettings, night, (uint32_t)now);
            view.clockValid = valid;
            view.year = local.tm_year + 1900;
            view.month = local.tm_mon + 1;
            view.day = local.tm_mday;
            view.weekday = local.tm_wday;
            view.hour = local.tm_hour;
            view.minute = local.tm_min;
            view.second = local.tm_sec;
            view.face = face;
            mtb_Panel_Begin_Update();
            bgc_Draw(view);
            mtb_Panel_End_Update();
        }
        // The phone's status panel: when the clock or night mode changes, and every minute.
        if (millis() - lastPushMs >= 60000 || lastPushMs == 0){
            lastPushMs = millis();
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void bgc_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void bgc_Begin_Change(Bgc_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    next = settingsReceived ? receivedSettings : bgcSettings;
    portEXIT_CRITICAL(&requestLock);
}

static void bgc_End_Change(const Bgc_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
}

// What the phone shows when it opens: the settings, and the clock's status. Runs on the BLE parser task.
static void bgc_Mobile_Ui_Values(JsonObject values){
    Bgc_Settings_t s;
    bgc_Read_Saved_Settings(s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    values["face"] = s.face;
    values["theme"] = s.theme;
    values["hour24"] = s.hour24;
    values["seconds"] = s.showSeconds;
    values["date"] = s.showDate;
    values["autoFace"] = s.autoFaceMinutes;
    values["night"] = s.night;
    values["nightStyle"] = s.nightStyle;
    values["show"] = s.show;
    char clock[8];
    mtb_Kit_Clock_Text(clock, sizeof(clock), s.nightFrom);
    values["nightFrom"] = clock;
    mtb_Kit_Clock_Text(clock, sizeof(clock), s.nightTo);
    values["nightTo"] = clock;

    const time_t now = time(nullptr);
    if (now > BGC_CLOCK_VALID_AFTER){
        values["clockText"] = "Set from the network";
        values["clockState"] = "ok";
        struct tm local;
        localtime_r(&now, &local);
        char text[48];
        if (s.night){
            const bool on = bgc_Night_Now(s, local);
            char other[8];
            mtb_Kit_Clock_Text(other, sizeof(other), on ? s.nightTo : s.nightFrom);
            snprintf(text, sizeof(text), on ? "On now, until %s" : "Starts at %s", other);
            values["nightText"] = text;
            values["nightState"] = on ? "info" : "ok";
        } else { values["nightText"] = "Off"; values["nightState"] = "off"; }
    } else {
        values["clockText"] = "Waiting for the network time";
        values["clockState"] = "warn";
    }
    if (ntp_TimeZone[0]) values["zone"] = ntp_TimeZone;
}

// Command 0: {"face": 0-3}
static void bgc_Ble_Face(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["face"], 0, BGC_FACE_COUNT - 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.face = (uint8_t)value;
    bgc_End_Change(next);
}

// Command 1: {"theme": 0-6}
static void bgc_Ble_Theme(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["theme"], 0, BGC_THEME_COUNT - 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.theme = (uint8_t)value;
    bgc_End_Change(next);
}

// Command 2: {"color": "0xFFRRGGBB"}: the custom theme's colour, which it also selects.
static void bgc_Ble_Color(JsonDocument& command){
    uint16_t color;
    if (!mtb_Kit_Parse_Color(command["color"], color)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (color == BLACK) color = MTB_KIT_RGB(40, 40, 40);     // Black digits would vanish.
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.customColor = color;
    next.theme = BGC_THEME_CUSTOM;
    bgc_End_Change(next);
}

static void bgc_Switch(JsonDocument& command, const char* key, uint8_t Bgc_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], 0, 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.*field = (uint8_t)value;
    bgc_End_Change(next);
}

// Commands 3-5, 7: switches.
static void bgc_Ble_Hour24(JsonDocument& command){ bgc_Switch(command, "hour24", &Bgc_Settings_t::hour24); }
static void bgc_Ble_Seconds(JsonDocument& command){ bgc_Switch(command, "seconds", &Bgc_Settings_t::showSeconds); }
static void bgc_Ble_Date(JsonDocument& command){ bgc_Switch(command, "date", &Bgc_Settings_t::showDate); }
static void bgc_Ble_Night(JsonDocument& command){ bgc_Switch(command, "night", &Bgc_Settings_t::night); }

// Command 6: {"autoFace": 0-60}: minutes per face; 0 keeps one face.
static void bgc_Ble_Auto_Face(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["autoFace"], 0, 60, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.autoFaceMinutes = (uint8_t)value;
    bgc_End_Change(next);
}

// Command 8: {"nightFrom": "22:00", "nightTo": "07:00"}
static void bgc_Ble_Night_Times(JsonDocument& command){
    const int from = mtb_Kit_Parse_Clock(command["nightFrom"]), to = mtb_Kit_Parse_Clock(command["nightTo"]);
    if (from < 0 || to < 0 || from == to){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.nightFrom = (uint16_t)from;
    next.nightTo = (uint16_t)to;
    bgc_End_Change(next);
}

// Command 9: {"nightStyle": 0 red, 1 dimmed theme}
static void bgc_Ble_Night_Style(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["nightStyle"], 0, BGC_NIGHT_DIM, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.nightStyle = (uint8_t)value;
    bgc_End_Change(next);
}

// Command 11: {"show": 0 the clock and date, 1 the clock alone, 2 the calendar alone}
static void bgc_Ble_Show(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["show"], 0, BGC_SHOW_COUNT - 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bgc_Settings_t next;
    bgc_Begin_Change(next);
    next.show = (uint8_t)value;
    bgc_End_Change(next);
}

// Command 10: {} - set the clock from the network now.
static void bgc_Ble_Sync(JsonDocument&){
    if (Mtb_Applications::internetConnectStatus != true){ mtb_Ble_App_Cmd_Reject(); return; }
    mtb_Launch_This_Service(mtb_Sntp_Time_Sv);
}
