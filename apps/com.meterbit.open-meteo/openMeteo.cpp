/**
 * Open-Meteo app, ID {3,1}: the weather for a place found from the phone, from Open-Meteo: conditions now, 48 hours,
 * 16 days, sun and moon, and the air quality (with pollen in Europe).
 *
 * Works with no key on Open-Meteo's free API (non-commercial use, 10,000 calls a day; the app makes about 200). With
 * a key from a paid plan it uses Open-Meteo's customer servers, which allow commercial use, and can read the
 * conditions every 5 minutes. The shared weather engine (mtb_kit_weather.h) fetches and paces; omt_Model.cpp speaks
 * Open-Meteo.
 */
#include "mtb_engine.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_weather.h"
#include "omt_Model.h"
#include "omt_Ui.h"
#include "omt_Mobile_Ui.h"

#define OMT_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t openMeteo_Task_H = NULL;
void openMeteoUpdate_App_Task(void *);
static void omt_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *openMeteo_App;
Mtb_Applications* openMeteo_App_GetInstance() {
    if (!openMeteo_App){
        openMeteo_App = new Mtb_Applications_StatusBar(openMeteoUpdate_App_Task, &openMeteo_Task_H, "Open-Meteo", {3,1}, 8192);
        openMeteo_App->mtb_App_Set_Mobile_UI(OMT_UI_MANIFEST, OMT_UI_API, nullptr, omt_Mobile_Ui_Values);
    }
    return openMeteo_App;
}
MTB_REGISTER_APP(openMeteo_App, 3, 1)

EXT_RAM_BSS_ATTR static Omt_Settings_t omtSettings;
EXT_RAM_BSS_ATTR static Mtb_Kit_Wx_t omtEngine;
static Mtb_Kit_Wx_Data_t* viewData = nullptr;      // PSRAM

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Omt_Settings_t receivedSettings;

static void omt_Button(button_event_t button);
static void omt_Ble_Search(JsonDocument& command);
static void omt_Ble_Place(JsonDocument& command);
static void omt_Ble_Units(JsonDocument& command);
static void omt_Ble_Air(JsonDocument& command);
static void omt_Ble_Aqi(JsonDocument& command);
static void omt_Ble_Refresh_Minutes(JsonDocument& command);
static void omt_Ble_Key(JsonDocument& command);
static void omt_Ble_Pages(JsonDocument& command);
static void omt_Ble_Refresh(JsonDocument& command);
static void omt_Ble_Free(JsonDocument& command);
static void omt_Search_Answer(const char* query, const Mtb_Kit_Wx_Place_t* places, int count, const char* error);

//##############################################################################################################
static uint32_t omt_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)OMT_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool omt_Read_Saved_Settings(Omt_Settings_t& settings){
    omt_Settings_Default(settings);
    Omt_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(OMT_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == OMT_SETTINGS_MAGIC;
    if (ok) settings = saved;
    omt_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void omt_Save_Settings(void){ mtb_Write_Nvs_Struct(OMT_NVS_KEY, &omtSettings, sizeof(Omt_Settings_t)); }

static bool omt_Message(const Mtb_Kit_Wx_Status_t& status, bool online, uint32_t now, const char*& title, const char*& detail){
    if (status.keyRejected){ title = "KEY NOT ACCEPTED"; detail = "Open-Meteo did not accept your API key. Check it, or switch back to the free API in the Pixlpal app."; return true; }
    if (viewData->current.valid) return false;
    if (!online){ title = "OFFLINE"; detail = "The weather appears once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "The weather appears once Pixlpal knows the time."; return true; }
    if (status.quotaUsed){ title = "DAILY LIMIT REACHED"; detail = "Open-Meteo's free calls for today are used on this network. The weather comes back tomorrow."; return true; }
    title = "GETTING THE WEATHER";
    detail = "Asking Open-Meteo for the conditions...";
    return true;
}

static const char* omt_Credit(uint8_t page){
    return page == KIT_WX_PAGE_AIR ? "CAMS, VIA OPEN-METEO.COM" : "WEATHER DATA BY OPEN-METEO.COM";
}

//##############################################################################################################
void openMeteoUpdate_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(omt_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(omt_Ble_Search, omt_Ble_Place, omt_Ble_Units, omt_Ble_Air, omt_Ble_Aqi, omt_Ble_Refresh_Minutes,
                                       omt_Ble_Key, omt_Ble_Pages, omt_Ble_Refresh, omt_Ble_Free);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!omt_Read_Saved_Settings(omtSettings)) omt_Save_Settings();
    if (viewData == nullptr) viewData = (Mtb_Kit_Wx_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_Wx_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    Mtb_Kit_Wx_Config_t config;
    omt_Kit_Config(omtSettings, config);
    mtb_Kit_Wx_Begin(omtEngine, omt_Provider(), config);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, pageStartMs = millis(), pushMs = 0;
    uint8_t page = KIT_WX_PAGE_NOW;
    Mtb_Kit_Wx_Status_t status = {};
    bool redraw = true;
    int lastMinute = -1;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = omt_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            omtSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            omt_Settings_Clamp(omtSettings);
            omt_Save_Settings();
            omt_Kit_Config(omtSettings, config);
            mtb_Kit_Wx_Set_Config(omtEngine, config);
            redraw = true;
        }
        mtb_Kit_Wx_Poll(omtEngine, online, now);
        if (mtb_Kit_Wx_Copy(omtEngine, *viewData, version)){ redraw = true; THIS_APP->mtb_App_Push_Mobile_Values(); }
        mtb_Kit_Wx_Get_Status(omtEngine, status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next page; double click, now; triple, refresh; long press, rotation on or off.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; omtSettings.autoCycle = !omtSettings.autoCycle; omt_Save_Settings(); }
            else if (clicks == 1) page = mtb_Kit_Wx_Next_Page(page, *viewData, true);
            else if (clicks == 2) page = KIT_WX_PAGE_NOW;
            else if (clicks == 3) mtb_Kit_Wx_Refresh(omtEngine, now);
            pageStartMs = millis();
            redraw = true;
        }
        // Rotation: the weather pages; the plan page only by the knob.
        if (omtSettings.autoCycle && millis() - pageStartMs >= omtSettings.pageSeconds * 1000UL * (page == KIT_WX_PAGE_NOW ? 2 : 1)){
            pageStartMs = millis();
            page = mtb_Kit_Wx_Next_Page(page, *viewData, false);
            redraw = true;
        }
        if (now && (int)(now / 60) != lastMinute){ lastMinute = (int)(now / 60); redraw = true; }

        if (redraw){
            redraw = false;
            mtb_Panel_Begin_Update();
            const char *title, *detail;
            if (page == KIT_WX_PAGE_APP) omt_Draw_Plan(omtSettings, status);
            else {
                Mtb_Kit_Wx_View_t view = {};
                view.data = viewData;
                view.place = omtSettings.place;
                view.page = page;
                view.now = now;
                view.credit = omt_Credit(page);
                view.appName = "OPEN-METEO";
                if (omt_Message(status, online, now, title, detail)){ view.message = title; view.detail = detail; }
                mtb_Kit_Wx_Draw(view);
            }
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void omt_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Omt_Settings_t* omt_Begin_Change(void){
    Omt_Settings_t* next = (Omt_Settings_t*)heap_caps_malloc(sizeof(Omt_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : omtSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void omt_End_Change(Omt_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void omt_Status_Values(JsonObject values, const Omt_Settings_t& s){
    Mtb_Kit_Wx_Status_t status;
    char error[96];
    mtb_Kit_Wx_Get_Status(omtEngine, status, error, sizeof(error));
    if (!status.started) return;
    const bool paid = s.apiKey[0] != '\0';
    values["plan"] = status.plan.known ? status.plan.name : paid ? "Checking your key" : "Free, non-commercial";
    values["planState"] = status.keyRejected ? "error" : paid ? "ok" : "info";
    values["paid"] = paid ? 1 : 0;
    if (status.keyRejected) values["keyText"] = error[0] ? error : "Not accepted";
    else if (paid && status.plan.known) values["keyText"] = "Accepted";
    values["keyState"] = status.keyRejected ? "error" : "ok";
    values["usedToday"] = status.usedToday;
    char pace[64];
    snprintf(pace, sizeof(pace), "Every %u min: about %.0f calls a day", (unsigned)s.refreshMinutes, omt_Calls_Per_Day(s.refreshMinutes, s.air));
    values["pace"] = pace;
    if (status.lastFetch) values["updated"] = status.lastFetch;
    if (viewData && viewData->current.valid){
        char now[48];
        snprintf(now, sizeof(now), "%.0f\xC2\xB0%s, %s", viewData->current.temp, viewData->imperial ? "F" : "C", viewData->current.text);
        values["conditions"] = now;
        if (viewData->air.valid){
            char air[48];
            snprintf(air, sizeof(air), "%d, %s", viewData->air.index, mtb_Kit_Aqi_Name(viewData->air.scale, viewData->air.index));
            values["airText"] = air;
            const int band = mtb_Kit_Aqi_Band(viewData->air.scale, viewData->air.index);
            values["airState"] = band <= 0 ? "ok" : band <= 2 ? "warn" : "error";
        }
    }
}

// What the phone shows when it opens. The key never leaves the device. Runs on the BLE parser task.
static void omt_Mobile_Ui_Values(JsonObject values){
    Omt_Settings_t* s = (Omt_Settings_t*)heap_caps_malloc(sizeof(Omt_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    omt_Read_Saved_Settings(*s);
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
    values["aqi"] = s->aqiScale;
    values["refresh"] = s->refreshMinutes;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    omt_Status_Values(values, *s);
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}

// Command 0: {"query": "Lagos"}: a place search (free geocoding), answered as command 248.
static void omt_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) > 60 || !mtb_Kit_Wx_Search(omtEngine, query, omt_Search_Answer)) mtb_Ble_App_Cmd_Reject();
}

static void omt_Search_Answer(const char* query, const Mtb_Kit_Wx_Place_t* places, int count, const char* error){
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
    if (openMeteo_App) openMeteo_App->mtb_App_Send_Search_Results("places", query, results, error);
}

// Command 1: {"place": {"name": "Lagos, Nigeria", "value": "6.45407,3.39467"}}
static void omt_Ble_Place(JsonDocument& command){
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
    Omt_Settings_t* next = omt_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(next->place, sizeof(next->place), "%s", name);
    next->latitude = latitude;
    next->longitude = longitude;
    omt_End_Change(next);
}

static void omt_Byte(JsonDocument& command, const char* key, long low, long high, uint8_t Omt_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], low, high, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Omt_Settings_t* next = omt_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    omt_End_Change(next);
}

// Commands 2-5: {"units": 0 metric, 1 imperial}, {"air": 0-1}, {"aqi": 0 US, 1 European}, {"refresh": 5-60 minutes}
static void omt_Ble_Units(JsonDocument& command){ omt_Byte(command, "units", 0, 1, &Omt_Settings_t::imperial); }
static void omt_Ble_Air(JsonDocument& command){ omt_Byte(command, "air", 0, 1, &Omt_Settings_t::air); }
static void omt_Ble_Aqi(JsonDocument& command){ omt_Byte(command, "aqi", KIT_AQI_US, KIT_AQI_EU, &Omt_Settings_t::aqiScale); }
static void omt_Ble_Refresh_Minutes(JsonDocument& command){ omt_Byte(command, "refresh", 5, 60, &Omt_Settings_t::refreshMinutes); }

// Command 6: {"api_key": "..."}: a key from a paid Open-Meteo plan.
static void omt_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    if (!omt_Key_Valid(key)){ mtb_Ble_App_Cmd_Reject(); return; }
    Omt_Settings_t* next = omt_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, strlen(key));
    omt_End_Change(next);
}

// Command 7: {"cycle"} and/or {"seconds": 4-60}
static void omt_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 4, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Omt_Settings_t* next = omt_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    omt_End_Change(next);
}

// Command 8: {} - read everything now.
static void omt_Ble_Refresh(JsonDocument&){
    if (!mtb_Kit_Wx_Refresh(omtEngine, omt_Now())) mtb_Ble_App_Cmd_Reject();
}

// Command 9: {} - forget the key: back to the free API.
static void omt_Ble_Free(JsonDocument&){
    Omt_Settings_t* next = omt_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    omt_End_Change(next);
}
