/**
 * Google Weather app, ID {3,2}: the weather from Google's Weather API (Google Maps Platform) for a place found
 * from the phone: conditions now, the next hours, seven days, sun and moon, and public alerts.
 *
 * It needs the user's Google Cloud API key with the Weather API (and, for the place search, the Geocoding API)
 * enabled, and billing on the project. Google publishes no plan through the API, so the user sets the calls a month
 * the app may make, and it spreads them over the month.
 *
 * Data: gwx_Service.cpp. Screens: gwx_Ui.cpp. Model: gwx_Model.cpp.
 */
#include "mtb_engine.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "gwx_Service.h"
#include "gwx_Ui.h"
#include "gwx_Mobile_Ui.h"

#define GWX_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t googleWeather_Task_H = NULL;
void googleWeatherUpdate_App_Task(void *);
static void gwx_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *googleWeather_App;
Mtb_Applications* googleWeather_App_GetInstance() {
    if (!googleWeather_App){
        googleWeather_App = new Mtb_Applications_StatusBar(googleWeatherUpdate_App_Task, &googleWeather_Task_H, "Google Weather", {3,2}, 8192);
        googleWeather_App->mtb_App_Set_Mobile_UI(GWX_UI_MANIFEST, GWX_UI_API, nullptr, gwx_Mobile_Ui_Values);
    }
    return googleWeather_App;
}
MTB_REGISTER_APP(googleWeather_App, 3, 2)

EXT_RAM_BSS_ATTR static Gwx_Settings_t gwxSettings;
static Gwx_Data_t* viewData = nullptr;      // PSRAM

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Gwx_Settings_t receivedSettings;

static void gwx_Button(button_event_t button);
static void gwx_Ble_Search(JsonDocument& command);
static void gwx_Ble_Place(JsonDocument& command);
static void gwx_Ble_Units(JsonDocument& command);
static void gwx_Ble_Alerts(JsonDocument& command);
static void gwx_Ble_Budget(JsonDocument& command);
static void gwx_Ble_Key(JsonDocument& command);
static void gwx_Ble_Pages(JsonDocument& command);
static void gwx_Ble_Refresh(JsonDocument& command);
static void gwx_Search_Answer(const char* query, const Gwx_Place_t* places, int count, const char* error);

//##############################################################################################################
static uint32_t gwx_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)GWX_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool gwx_Read_Saved_Settings(Gwx_Settings_t& settings){
    gwx_Settings_Default(settings);
    Gwx_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(GWX_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == GWX_SETTINGS_MAGIC;
    if (ok) settings = saved;
    gwx_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void gwx_Save_Settings(void){ mtb_Write_Nvs_Struct(GWX_NVS_KEY, &gwxSettings, sizeof(Gwx_Settings_t)); }

static bool gwx_Message(const Gwx_Status_t& status, bool online, uint32_t now, const char*& title, const char*& detail){
    if (status.keyMissing){ title = "ADD YOUR KEY"; detail = "Create a Google Cloud API key with the Weather API enabled, and paste it into Google Weather in the Pixlpal app."; return true; }
    if (viewData->current.valid) return false;
    if (status.denied){ title = "GOOGLE REFUSED"; detail = "Check the key, that the Weather API is enabled for it, and that billing is on for its project."; return true; }
    if (!online){ title = "OFFLINE"; detail = "The weather appears once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "The weather appears once Pixlpal knows the time."; return true; }
    if (status.quotaUsed){ title = "BUDGET SPENT"; detail = "This month's calls are used. Raise the budget in the Pixlpal app, or wait for next month."; return true; }
    title = "GETTING THE WEATHER";
    detail = "Asking Google for the conditions...";
    return true;
}

//##############################################################################################################
void googleWeatherUpdate_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(gwx_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(gwx_Ble_Search, gwx_Ble_Place, gwx_Ble_Units, gwx_Ble_Alerts, gwx_Ble_Budget, gwx_Ble_Key,
                                       gwx_Ble_Pages, gwx_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!gwx_Read_Saved_Settings(gwxSettings)) gwx_Save_Settings();
    if (viewData == nullptr) viewData = (Gwx_Data_t*)heap_caps_calloc(1, sizeof(Gwx_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    gwx_Service_Set_Search_Callback(gwx_Search_Answer);
    gwx_Service_Begin(gwxSettings);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, pageStartMs = millis(), pushMs = 0, alertHash = 0;
    uint8_t page = GWX_PAGE_NOW;
    Gwx_Status_t status = {};
    bool redraw = true;
    int lastMinute = -1;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = gwx_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            gwxSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            gwx_Settings_Clamp(gwxSettings);
            gwx_Save_Settings();
            gwx_Service_Set_Settings(gwxSettings);
            redraw = true;
        }
        gwx_Service_Poll(online, now);
        if (gwx_Service_Copy(*viewData, version)){
            redraw = true;
            // A new alert: show it at once.
            uint32_t hash = 0;
            for (uint8_t i = 0; i < viewData->alertCount; i++) for (const char* c = viewData->alerts[i].title; *c; c++) hash = hash * 31 + (uint8_t)*c;
            if (hash && hash != alertHash){ page = GWX_PAGE_ALERTS; pageStartMs = millis(); }
            alertHash = hash;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        gwx_Service_Get_Status(status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next page; double click, now; triple, refresh; long press, rotation.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; gwxSettings.autoCycle = !gwxSettings.autoCycle; gwx_Save_Settings(); }
            else if (clicks == 1){
                page = (page + 1) % GWX_PAGE_COUNT;
                if (page == GWX_PAGE_ALERTS && viewData->alertCount == 0) page++;
            } else if (clicks == 2) page = GWX_PAGE_NOW;
            else if (clicks == 3) gwx_Service_Refresh_Now(now);
            pageStartMs = millis();
            redraw = true;
        }
        // Rotation: now, hours, days, sky, and alerts when there are any; usage only by the knob.
        if (gwxSettings.autoCycle && millis() - pageStartMs >= gwxSettings.pageSeconds * 1000UL * (page == GWX_PAGE_NOW ? 2 : 1)){
            pageStartMs = millis();
            if (page >= GWX_PAGE_SKY) page = (page == GWX_PAGE_SKY && viewData->alertCount) ? GWX_PAGE_ALERTS : GWX_PAGE_NOW;
            else page++;
            redraw = true;
        }
        if (now && (int)(now / 60) != lastMinute){ lastMinute = (int)(now / 60); redraw = true; }

        if (redraw){
            redraw = false;
            Gwx_View_t view = {};
            view.data = viewData;
            view.settings = &gwxSettings;
            view.page = page;
            view.now = now;
            view.interval = status.interval;
            const char *title, *detail;
            if (gwx_Message(status, online, now, title, detail)){ view.message = title; view.detail = detail; }
            mtb_Panel_Begin_Update();
            gwx_Draw(view);
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void gwx_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Gwx_Settings_t* gwx_Begin_Change(void){
    Gwx_Settings_t* next = (Gwx_Settings_t*)heap_caps_malloc(sizeof(Gwx_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : gwxSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void gwx_End_Change(Gwx_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void gwx_Status_Values(JsonObject values, const Gwx_Settings_t& s){
    Gwx_Status_t status;
    char error[96];
    gwx_Service_Get_Status(status, error, sizeof(error));
    if (!status.started) return;
    if (status.denied){ values["keyText"] = error[0] ? error : "Refused by Google"; values["keyState"] = "error"; }
    else if (status.lastFetch && !status.keyMissing){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    values["used"] = status.usedCalls;
    values["usedLeft"] = s.monthlyBudget > status.usedCalls ? s.monthlyBudget - status.usedCalls : 0;
    values["usedState"] = status.interval == 0 ? "error" : status.usedCalls * 10 > s.monthlyBudget * 9 ? "warn" : "ok";
    char pace[48];
    if (status.interval) snprintf(pace, sizeof(pace), "Conditions every %u min", (unsigned)((status.interval + 30) / 60));
    else snprintf(pace, sizeof(pace), "Budget spent until next month");
    values["pace"] = pace;
    if (status.lastFetch) values["updated"] = status.lastFetch;
    // The conditions, from the screen's copy (only while the app runs).
    if (viewData && viewData->current.valid){
        char now[48];
        snprintf(now, sizeof(now), "%.0f\xC2\xB0%s, %s", viewData->current.temp, viewData->imperial ? "F" : "C", viewData->current.text);
        values["conditions"] = now;
        if (viewData->alertCount){ values["alert"] = viewData->alerts[0].title; values["alertState"] = viewData->alerts[0].severity >= GWX_SEV_SEVERE ? "error" : "warn"; }
        else { values["alert"] = "None in force"; values["alertState"] = "ok"; }
    }
}

// What the phone shows when it opens. The key never leaves the device. Runs on the BLE parser task.
static void gwx_Mobile_Ui_Values(JsonObject values){
    Gwx_Settings_t* s = (Gwx_Settings_t*)heap_caps_malloc(sizeof(Gwx_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    gwx_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonObject place = values["place"].to<JsonObject>();
    place["name"] = s->place;
    char where[32];
    snprintf(where, sizeof(where), "%.5f,%.5f", s->latitude, s->longitude);
    place["value"] = where;
    values["units"] = s->imperial;
    values["alerts"] = s->alerts;
    values["budget"] = s->monthlyBudget;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    gwx_Status_Values(values, *s);
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}

// Command 0: {"query": "Lagos"}: a place search (Geocoding, one call), answered as command 248.
static void gwx_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) > 60 || !gwx_Service_Search(query)) mtb_Ble_App_Cmd_Reject();
}

static void gwx_Search_Answer(const char* query, const Gwx_Place_t* places, int count, const char* error){
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
    if (googleWeather_App) googleWeather_App->mtb_App_Send_Search_Results("places", query, results, error);
}

// Command 1: {"place": {"name": "Lagos, Nigeria", "value": "6.52440,3.37920"}}
static void gwx_Ble_Place(JsonDocument& command){
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
    Gwx_Settings_t* next = gwx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(next->place, sizeof(next->place), "%s", name);
    next->latitude = latitude;
    next->longitude = longitude;
    gwx_End_Change(next);
}

static void gwx_Switch(JsonDocument& command, const char* key, uint8_t Gwx_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], 0, 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Gwx_Settings_t* next = gwx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    gwx_End_Change(next);
}

// Commands 2 and 3: {"units": 0 metric, 1 imperial}, {"alerts": 1}
static void gwx_Ble_Units(JsonDocument& command){ gwx_Switch(command, "units", &Gwx_Settings_t::imperial); }
static void gwx_Ble_Alerts(JsonDocument& command){ gwx_Switch(command, "alerts", &Gwx_Settings_t::alerts); }

// Command 4: {"budget": 1000-200000}: calls a month.
static void gwx_Ble_Budget(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["budget"], 1000, 200000, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Gwx_Settings_t* next = gwx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->monthlyBudget = (uint32_t)value;
    gwx_End_Change(next);
}

// Command 5: {"api_key": "AIza..."}: Google API keys are 39 characters, starting "AIza".
static void gwx_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length >= 30 && length < GWX_KEY_LEN && strncmp(key, "AIza", 4) == 0;
    for (size_t i = 0; valid && i < length; i++) if (!(isalnum((unsigned char)key[i]) || key[i] == '-' || key[i] == '_')) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Gwx_Settings_t* next = gwx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, length);
    gwx_End_Change(next);
}

// Command 6: {"cycle"} and/or {"seconds": 4-60}
static void gwx_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 4, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Gwx_Settings_t* next = gwx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    gwx_End_Change(next);
}

// Command 7: {} - read the conditions and hours now.
static void gwx_Ble_Refresh(JsonDocument&){
    if (!gwx_Service_Refresh_Now(gwx_Now())) mtb_Ble_App_Cmd_Reject();
}
