/**
 * OpenWeather app, ID {3,0}: the weather for a place found from the phone, from OpenWeather: conditions now, the
 * next hours, the days ahead, sun and moon, air quality, and government weather alerts.
 *
 * Needs a free OpenWeather key. The app learns what the key has from OpenWeather's answers and uses the best of it:
 * the free plan's 5 day / 3 hour forecast; the 16-day forecast (Startup and up); the 4-day hourly forecast (Developer
 * and up); One Call 3.0 (48 hours and alerts) when the key is subscribed to "One Call by Call", read at most 96 times
 * a day so it stays inside the 1,000 free calls. The shared weather engine (mtb_kit_weather.h) fetches and paces;
 * owm_Model.cpp speaks OpenWeather.
 */
#include "mtb_engine.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_weather.h"
#include "owm_Model.h"
#include "owm_Ui.h"
#include "owm_Mobile_Ui.h"

#define OWM_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t openWeather_Task_H = NULL;
void openWeather_App_Task(void *);
static void owm_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *openWeather_App;
Mtb_Applications* openWeather_App_GetInstance() {
    if (!openWeather_App){
        openWeather_App = new Mtb_Applications_StatusBar(openWeather_App_Task, &openWeather_Task_H, "OpenWeather", {3,0}, 8192);
        openWeather_App->mtb_App_Set_Mobile_UI(OWM_UI_MANIFEST, OWM_UI_API, nullptr, owm_Mobile_Ui_Values);
    }
    return openWeather_App;
}
MTB_REGISTER_APP(openWeather_App, 3, 0)

EXT_RAM_BSS_ATTR static Owm_Settings_t owmSettings;
EXT_RAM_BSS_ATTR static Mtb_Kit_Wx_t owmEngine;
static Mtb_Kit_Wx_Data_t* viewData = nullptr;      // PSRAM

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Owm_Settings_t receivedSettings;

static void owm_Button(button_event_t button);
static void owm_Ble_Search(JsonDocument& command);
static void owm_Ble_Place(JsonDocument& command);
static void owm_Ble_Units(JsonDocument& command);
static void owm_Ble_Air(JsonDocument& command);
static void owm_Ble_Alerts(JsonDocument& command);
static void owm_Ble_One_Call(JsonDocument& command);
static void owm_Ble_Key(JsonDocument& command);
static void owm_Ble_Pages(JsonDocument& command);
static void owm_Ble_Refresh(JsonDocument& command);
static void owm_Search_Answer(const char* query, const Mtb_Kit_Wx_Place_t* places, int count, const char* error);

//##############################################################################################################
static uint32_t owm_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)OWM_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool owm_Read_Saved_Settings(Owm_Settings_t& settings){
    owm_Settings_Default(settings);
    Owm_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(OWM_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == OWM_SETTINGS_MAGIC;
    if (ok) settings = saved;
    owm_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void owm_Save_Settings(void){ mtb_Write_Nvs_Struct(OWM_NVS_KEY, &owmSettings, sizeof(Owm_Settings_t)); }

static bool owm_Message(const Mtb_Kit_Wx_Status_t& status, bool online, uint32_t now, const char*& title, const char*& detail){
    if (status.keyMissing){ title = "ADD YOUR KEY"; detail = "Get a free key at openweathermap.org and paste it into OpenWeather in the Pixlpal app."; return true; }
    if (status.keyRejected && !viewData->current.valid){ title = "KEY NOT ACCEPTED YET"; detail = "OpenWeather did not accept the key. New keys can take up to two hours to start working; Pixlpal keeps trying."; return true; }
    if (viewData->current.valid) return false;
    if (!online){ title = "OFFLINE"; detail = "The weather appears once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "The weather appears once Pixlpal knows the time."; return true; }
    if (status.quotaUsed){ title = "CALLS USED UP"; detail = "OpenWeather says this key's calls are used up for now. The weather comes back when they reset."; return true; }
    title = "GETTING THE WEATHER";
    detail = "Asking OpenWeather for the conditions...";
    return true;
}

static const char* owm_Credit(uint8_t){ return "WEATHER DATA (C) OPENWEATHER"; }

//##############################################################################################################
void openWeather_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(owm_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(owm_Ble_Search, owm_Ble_Place, owm_Ble_Units, owm_Ble_Air, owm_Ble_Alerts, owm_Ble_One_Call,
                                       owm_Ble_Key, owm_Ble_Pages, owm_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!owm_Read_Saved_Settings(owmSettings)) owm_Save_Settings();
    if (viewData == nullptr) viewData = (Mtb_Kit_Wx_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_Wx_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    Mtb_Kit_Wx_Config_t config;
    owm_Kit_Config(owmSettings, config);
    mtb_Kit_Wx_Begin(owmEngine, owm_Provider(), config);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, pageStartMs = millis(), pushMs = 0;
    uint8_t page = KIT_WX_PAGE_NOW;
    Mtb_Kit_Wx_Status_t status = {};
    bool redraw = true;
    int lastMinute = -1;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = owm_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            owmSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            owm_Settings_Clamp(owmSettings);
            owm_Save_Settings();
            owm_Kit_Config(owmSettings, config);
            mtb_Kit_Wx_Set_Config(owmEngine, config);
            redraw = true;
        }
        mtb_Kit_Wx_Poll(owmEngine, online, now);
        if (mtb_Kit_Wx_Copy(owmEngine, *viewData, version)){ redraw = true; THIS_APP->mtb_App_Push_Mobile_Values(); }
        mtb_Kit_Wx_Get_Status(owmEngine, status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next page; double click, now; triple, refresh; long press, rotation on or off.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; owmSettings.autoCycle = !owmSettings.autoCycle; owm_Save_Settings(); }
            else if (clicks == 1) page = mtb_Kit_Wx_Next_Page(page, *viewData, true);
            else if (clicks == 2) page = KIT_WX_PAGE_NOW;
            else if (clicks == 3) mtb_Kit_Wx_Refresh(owmEngine, now);
            pageStartMs = millis();
            redraw = true;
        }
        // Rotation: the weather pages; the plan page only by the knob.
        if (owmSettings.autoCycle && millis() - pageStartMs >= owmSettings.pageSeconds * 1000UL * (page == KIT_WX_PAGE_NOW ? 2 : 1)){
            pageStartMs = millis();
            page = mtb_Kit_Wx_Next_Page(page, *viewData, false);
            redraw = true;
        }
        if (now && (int)(now / 60) != lastMinute){ lastMinute = (int)(now / 60); redraw = true; }

        if (redraw){
            redraw = false;
            mtb_Panel_Begin_Update();
            const char *title, *detail;
            if (page == KIT_WX_PAGE_APP) owm_Draw_Plan(owmSettings, status);
            else {
                Mtb_Kit_Wx_View_t view = {};
                view.data = viewData;
                view.place = owmSettings.place;
                view.page = page;
                view.now = now;
                view.credit = owm_Credit(page);
                view.appName = "OPENWEATHER";
                if (owm_Message(status, online, now, title, detail)){ view.message = title; view.detail = detail; }
                mtb_Kit_Wx_Draw(view);
            }
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void owm_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Owm_Settings_t* owm_Begin_Change(void){
    Owm_Settings_t* next = (Owm_Settings_t*)heap_caps_malloc(sizeof(Owm_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : owmSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void owm_End_Change(Owm_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void owm_Status_Values(JsonObject values, const Owm_Settings_t& s){
    Mtb_Kit_Wx_Status_t status;
    char error[96];
    mtb_Kit_Wx_Get_Status(owmEngine, status, error, sizeof(error));
    if (!status.started) return;
    const Mtb_Kit_Wx_Plan_t& plan = status.plan;
    values["plan"] = (plan.flags & OWM_HAS_KEY) ? plan.name : "Checking your key";
    values["planState"] = status.keyRejected ? "error" : plan.paid ? "ok" : (plan.flags & OWM_HAS_KEY) ? "info" : "off";
    values["paid"] = plan.paid;
    values["hasOneCall"] = (plan.flags & OWM_HAS_ONECALL) ? 1 : 0;
    values["oneCallText"] = (plan.flags & OWM_HAS_ONECALL) ? (s.oneCall ? "In use: 48 hours and alerts" : "Subscribed, turned off below")
                          : (plan.tested & OWM_HAS_ONECALL) ? "Not subscribed" : "Not checked yet";
    if (status.keyRejected) values["keyText"] = error[0] ? error : "Not accepted";
    else if (plan.flags & OWM_HAS_KEY) values["keyText"] = "Accepted";
    values["keyState"] = status.keyRejected ? "error" : "ok";
    values["usedToday"] = status.usedToday;
    if (status.lastFetch) values["updated"] = status.lastFetch;
    if (viewData && viewData->current.valid){
        char now[48];
        snprintf(now, sizeof(now), "%.0f\xC2\xB0%s, %s", viewData->current.temp, viewData->imperial ? "F" : "C", viewData->current.text);
        values["conditions"] = now;
        if (viewData->air.valid){
            char air[48];
            snprintf(air, sizeof(air), "%d of 5, %s", viewData->air.index, mtb_Kit_Aqi_Name(viewData->air.scale, viewData->air.index));
            values["airText"] = air;
            const int band = mtb_Kit_Aqi_Band(viewData->air.scale, viewData->air.index);
            values["airState"] = band <= 0 ? "ok" : band <= 2 ? "warn" : "error";
        }
        if (viewData->alertCount){ values["alert"] = viewData->alerts[0].title; values["alertState"] = viewData->alerts[0].severity >= KIT_WX_SEV_SEVERE ? "error" : "warn"; }
        else if (s.oneCall && s.alerts && (viewData->plan.flags & OWM_HAS_ONECALL)){ values["alert"] = "None in force"; values["alertState"] = "ok"; }
    }
}

// What the phone shows when it opens. The key never leaves the device. Runs on the BLE parser task.
static void owm_Mobile_Ui_Values(JsonObject values){
    Owm_Settings_t* s = (Owm_Settings_t*)heap_caps_malloc(sizeof(Owm_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    owm_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonObject place = values["place"].to<JsonObject>();
    place["name"] = s->place;
    char where[32];
    snprintf(where, sizeof(where), "%.5f,%.5f", s->latitude, s->longitude);
    place["value"] = where;
    values["units"] = s->imperial;
    values["air"] = s->air;
    values["alerts"] = s->alerts;
    values["oneCall"] = s->oneCall;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    owm_Status_Values(values, *s);
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}

// Command 0: {"query": "Lagos"}: a place search (OpenWeather geocoding, one call), answered as command 248.
static void owm_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) > 60 || !mtb_Kit_Wx_Search(owmEngine, query, owm_Search_Answer)) mtb_Ble_App_Cmd_Reject();
}

static void owm_Search_Answer(const char* query, const Mtb_Kit_Wx_Place_t* places, int count, const char* error){
    JsonDocument doc;
    JsonArray results = doc.to<JsonArray>();
    for (int i = 0; i < count; i++){
        JsonObject item = results.add<JsonObject>();
        item["name"] = places[i].name;
        char where[32];
        snprintf(where, sizeof(where), "%.5f,%.5f", places[i].latitude, places[i].longitude);
        item["value"] = where;
        item["detail"] = places[i].detail;
    }
    if (openWeather_App) openWeather_App->mtb_App_Send_Search_Results("places", query, results, error);
}

// Command 1: {"place": {"name": "Lagos, Nigeria", "value": "6.45407,3.39467"}}
static void owm_Ble_Place(JsonDocument& command){
    JsonVariant place = command["place"];
    const char* name = place["name"];
    const char* where = place["value"];
    double latitude = NAN, longitude = NAN;
    char* end = nullptr;
    if (where){ latitude = strtod(where, &end); if (end && *end == ',') longitude = strtod(end + 1, &end); }
    if (name == nullptr || name[0] == '\0' || end == nullptr || *end != '\0' || !(latitude >= -90 && latitude <= 90) || !(longitude >= -180 && longitude <= 180)){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Owm_Settings_t* next = owm_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(next->place, sizeof(next->place), "%s", name);
    next->latitude = latitude;
    next->longitude = longitude;
    owm_End_Change(next);
}

static void owm_Byte(JsonDocument& command, const char* key, long low, long high, uint8_t Owm_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], low, high, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Owm_Settings_t* next = owm_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    owm_End_Change(next);
}

// Commands 2-5: {"units": 0 metric, 1 imperial}, {"air": 0-1}, {"alerts": 0-1}, {"oneCall": 0-1}
static void owm_Ble_Units(JsonDocument& command){ owm_Byte(command, "units", 0, 1, &Owm_Settings_t::imperial); }
static void owm_Ble_Air(JsonDocument& command){ owm_Byte(command, "air", 0, 1, &Owm_Settings_t::air); }
static void owm_Ble_Alerts(JsonDocument& command){ owm_Byte(command, "alerts", 0, 1, &Owm_Settings_t::alerts); }
static void owm_Ble_One_Call(JsonDocument& command){ owm_Byte(command, "oneCall", 0, 1, &Owm_Settings_t::oneCall); }

// Command 6: {"api_key": "..."}: 32 hexadecimal characters.
static void owm_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    if (!owm_Key_Valid(key)){ mtb_Ble_App_Cmd_Reject(); return; }
    Owm_Settings_t* next = owm_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, strlen(key));
    owm_End_Change(next);
}

// Command 7: {"cycle"} and/or {"seconds": 4-60}
static void owm_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 4, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Owm_Settings_t* next = owm_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    owm_End_Change(next);
}

// Command 8: {} - read everything now.
static void owm_Ble_Refresh(JsonDocument&){
    if (!mtb_Kit_Wx_Refresh(owmEngine, owm_Now())) mtb_Ble_App_Cmd_Reject();
}
