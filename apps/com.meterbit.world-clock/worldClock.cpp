/**
 * World Clock app, ID {0,2}: up to six cities from the phone app's former list of 172 (searched on the device),
 * shown three ways: one city large with its flag, a board of all of them, and a meeting planner whose cursor the
 * knob moves through the hours, every city's working hours, mornings, evenings and nights side by side.
 *
 * Local time comes from each city's POSIX rule (wck_Model.cpp), so it runs without the internet once the clock is
 * set; only the flags are fetched (from the woble/flags set on GitHub) and kept in memory.
 *
 * The knob: click, the next view; double click, the next city (in the focus view); turn, move the planner's cursor
 * (in the planner) or the brightness; long press, views change by themselves or stay.
 */
#include "mtb_engine.h"
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_http.h"
#include "mtb_kit_image.h"
#include "wck_Model.h"
#include "wck_Ui.h"
#include "wck_Mobile_Ui.h"

#define WCK_CLOCK_VALID_AFTER   1700000000L
#define WCK_FLAG_URL            "https://raw.githubusercontent.com/woble/flags/refs/heads/master/SVG/3x2/%s.svg"
#define WCK_FLAG_MAX            (48 * 1024)
#define WCK_PLANNER_IDLE_MS     60000       // The planner's cursor goes back to now after a minute untouched.

EXT_RAM_BSS_ATTR TaskHandle_t worldClock_Task_H = NULL;
void worldClock_App_Task(void *);
static void wck_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *worldClock_App;
Mtb_Applications* worldClock_App_GetInstance() {
    if (!worldClock_App){
        worldClock_App = new Mtb_Applications_StatusBar(worldClock_App_Task, &worldClock_Task_H, "World Clock", {0,2}, 6144, CLOCK_STATUS_BAR);
        worldClock_App->mtb_App_Set_Mobile_UI(WCK_UI_MANIFEST, WCK_UI_API, nullptr, wck_Mobile_Ui_Values);
    }
    return worldClock_App;
}
MTB_REGISTER_APP(worldClock_App, 0, 2)

EXT_RAM_BSS_ATTR static Wck_Settings_t wckSettings;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Wck_Settings_t receivedSettings;
static volatile uint8_t clicksRequested = 0;
static volatile int8_t turnsRequested = 0;
static volatile bool longPressRequested = false;

// Flags, kept for the session: the SVG text by country code. Only the app task touches flags[]; the fetch task
// hands its result over in flagArrival.
struct Wck_Flag_t { char code[3]; char* svg; size_t length; bool failed; };
static Wck_Flag_t flags[WCK_CITIES];
static Wck_Flag_t flagArrival;
static volatile bool flagBusy = false;
static volatile bool flagArrived = false;

static void wck_Button(button_event_t button);
static void wck_Knob(rotary_encoder_rotation_t direction);
static void wck_Ble_City_Legacy(JsonDocument& command);
static void wck_Ble_Color(JsonDocument& command);
static void wck_Ble_View(JsonDocument& command);
static void wck_Ble_Sync(JsonDocument& command);
static void wck_Ble_Cities(JsonDocument& command);
static void wck_Ble_Search(JsonDocument& command);
static void wck_Ble_Hour24(JsonDocument& command);
static void wck_Ble_Cycle(JsonDocument& command);
static void wck_Ble_Office(JsonDocument& command);

//##############################################################################################################
static bool wck_Read_Saved_Settings(Wck_Settings_t& settings){
    wck_Settings_Default(settings);
    Wck_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(WCK_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == WCK_SETTINGS_MAGIC;
    if (ok) settings = saved;
    char report[200];
    if (wck_Settings_Clamp(settings, report, sizeof(report)) > 0){
        // A damaged save: say what was wrong (to find its cause) and keep the repaired copy.
        ESP_LOGW("WORLD_CLOCK", "Saved settings had control characters: %s", report);
        mtb_Write_Nvs_Struct(WCK_NVS_KEY, &settings, sizeof(settings));
    }
    return ok;
}

static void wck_Save_Settings(void){ mtb_Write_Nvs_Struct(WCK_NVS_KEY, &wckSettings, sizeof(Wck_Settings_t)); }

static int32_t wck_Device_Offset(time_t now){
    struct tm local, utc;
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    int days = local.tm_yday - utc.tm_yday;
    if (days > 1) days = -1;
    if (days < -1) days = 1;
    return ((days * 24 + local.tm_hour - utc.tm_hour) * 60 + local.tm_min - utc.tm_min) * 60;
}

static Wck_Flag_t* wck_Flag(const char* code){
    for (auto& f : flags) if (f.code[0] && strcmp(f.code, code) == 0) return &f;
    return nullptr;
}

static void wck_Flag_Task(void* parameter){
    Wck_Flag_t result = {};
    memcpy(result.code, parameter, sizeof(result.code));
    heap_caps_free(parameter);
    char url[128];
    snprintf(url, sizeof(url), WCK_FLAG_URL, result.code);
    Mtb_Kit_Http_t http;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, WCK_FLAG_MAX, http, "Pixlpal-WorldClock/2.0 (ESP32 LED display)");
    if (body && http.code == 200 && !http.truncated){ result.svg = body; result.length = http.length; }
    else { heap_caps_free(body); result.failed = true; }
    flagArrival = result;
    flagArrived = true;
    vTaskDelete(NULL);
}

// The fetched flag into the cache (on the app task).
static void wck_Install_Flag(void){
    Wck_Flag_t* slot = wck_Flag(flagArrival.code);
    for (int i = 0; slot == nullptr && i < WCK_CITIES; i++) if (flags[i].code[0] == '\0') slot = &flags[i];
    if (slot == nullptr){ slot = &flags[0]; }
    heap_caps_free(slot->svg);
    *slot = flagArrival;
    memset(&flagArrival, 0, sizeof(flagArrival));
    flagBusy = false;
}

static void wck_Request_Flag(const char* code){
    if (code[0] == '\0' || flagBusy || Mtb_Applications::internetConnectStatus != true) return;
    const Wck_Flag_t* f = wck_Flag(code);
    if (f && (f->svg || f->failed)) return;
    char* parameter = (char*)heap_caps_malloc(3, MALLOC_CAP_SPIRAM);
    if (parameter == nullptr) return;
    memcpy(parameter, code, 3);
    flagBusy = true;
    if (xTaskCreatePinnedToCore(wck_Flag_Task, "wckFlag", 8192, parameter, 1, NULL, tskNO_AFFINITY) != pdPASS){ heap_caps_free(parameter); flagBusy = false; }
}

//##############################################################################################################
void worldClock_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(wck_Button, wck_Knob);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(wck_Ble_City_Legacy, wck_Ble_Color, wck_Ble_View, wck_Ble_Sync, wck_Ble_Cities, wck_Ble_Search,
                                       wck_Ble_Hour24, wck_Ble_Cycle, wck_Ble_Office);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!wck_Read_Saved_Settings(wckSettings)) wck_Save_Settings();
    settingsReceived = false;
    clicksRequested = 0;
    turnsRequested = 0;
    longPressRequested = false;

    uint8_t view = wckSettings.view, focus = 0;
    int32_t plannerShift = 0;
    uint32_t viewSinceMs = millis(), plannerTouchedMs = 0;
    int64_t lastSecond = -1;
    bool redraw = true;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            wckSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            wck_Settings_Clamp(wckSettings);
            wck_Save_Settings();
            view = wckSettings.view;
            focus %= wckSettings.count;
            viewSinceMs = millis();
            redraw = true;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        // The knob.
        const uint8_t clicks = clicksRequested;
        const int8_t turns = turnsRequested;
        if (clicks) clicksRequested = 0;
        if (turns) turnsRequested = 0;
        if (longPressRequested){ longPressRequested = false; wckSettings.cycle = !wckSettings.cycle; wck_Save_Settings(); redraw = true; }
        if (clicks == 1){ view = (view + 1) % WCK_VIEW_COUNT; plannerShift = 0; viewSinceMs = millis(); redraw = true; }
        if (clicks == 2){ view = WCK_VIEW_FOCUS; focus = (focus + 1) % wckSettings.count; viewSinceMs = millis(); redraw = true; }
        if (turns){
            if (view == WCK_VIEW_PLANNER){
                plannerShift += turns * 60;
                if (plannerShift > 7 * 24 * 60) plannerShift = 7 * 24 * 60;
                if (plannerShift < -7 * 24 * 60) plannerShift = -7 * 24 * 60;
                plannerTouchedMs = millis();
                viewSinceMs = millis();
                redraw = true;
            } else for (int i = 0; i < (turns > 0 ? turns : -turns); i++) mtb_Brightness_Control(turns > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE);
        }
        if (plannerShift && millis() - plannerTouchedMs >= WCK_PLANNER_IDLE_MS){ plannerShift = 0; redraw = true; }
        // Cycling: the focus view goes through every city before the next view.
        if (wckSettings.cycle && plannerShift == 0 && millis() - viewSinceMs >= wckSettings.seconds * 1000UL){
            viewSinceMs = millis();
            if (view == WCK_VIEW_FOCUS && focus + 1 < wckSettings.count) focus++;
            else { focus = 0; view = (view + 1) % WCK_VIEW_COUNT; }
            redraw = true;
        }
        const time_t now = time(nullptr);
        const bool valid = now > WCK_CLOCK_VALID_AFTER;
        if (now != lastSecond){ lastSecond = now; redraw = true; }
        if (view == WCK_VIEW_FOCUS) wck_Request_Flag(wckSettings.cities[focus % wckSettings.count].code);
        if (flagArrived){ flagArrived = false; wck_Install_Flag(); redraw = true; }

        if (redraw){
            redraw = false;
            Wck_View_t v = {};
            v.settings = &wckSettings;
            v.view = view;
            v.focus = focus;
            v.utc = now;
            v.localOffset = valid ? wck_Device_Offset(now) : 0;
            v.plannerShift = plannerShift;
            v.clockValid = valid;
            Mtb_Kit_Box_t flagBox;
            mtb_Panel_Begin_Update();
            wck_Draw(v, &flagBox);
            if (flagBox.w > 0){
                const Wck_Flag_t* f = wck_Flag(wckSettings.cities[focus % wckSettings.count].code);
                if (f && f->svg) mtb_Kit_Draw_Svg(f->svg, f->length, flagBox, BLACK);
                else mtb_Kit_Text_Centre(flagBox.x + flagBox.w / 2, flagBox.y + flagBox.h / 2 - 3, Terminal4x6, KIT_DIM, f && f->failed ? "NO FLAG" : "FLAG...");
            }
            mtb_Panel_End_Update();
        }
        delay(40);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void wck_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 2) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void wck_Knob(rotary_encoder_rotation_t direction){
    const int8_t turns = turnsRequested;
    if (direction == ROT_CLOCKWISE && turns < 24) turnsRequested = turns + 1;
    else if (direction == ROT_COUNTERCLOCKWISE && turns > -24) turnsRequested = turns - 1;
}

static Wck_Settings_t* wck_Begin_Change(void){
    Wck_Settings_t* next = (Wck_Settings_t*)heap_caps_malloc(sizeof(Wck_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : wckSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void wck_End_Change(Wck_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static void wck_Hex(char* dest, size_t size, uint16_t c){
    snprintf(dest, size, "0xFF%02X%02X%02X", ((c >> 11) & 0x1F) * 255 / 31, ((c >> 5) & 0x3F) * 255 / 63, (c & 0x1F) * 255 / 31);
}

// What the phone shows: the cities and their times now. Runs on the BLE parser task.
static void wck_Mobile_Ui_Values(JsonObject values){
    Wck_Settings_t* s = (Wck_Settings_t*)heap_caps_malloc(sizeof(Wck_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    wck_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonArray cities = values["cities"].to<JsonArray>();
    const time_t now = time(nullptr);
    const bool valid = now > WCK_CLOCK_VALID_AFTER;
    const int32_t here = valid ? wck_Device_Offset(now) : 0;
    JsonArray rows = values["rows"].to<JsonArray>();
    for (uint8_t i = 0; i < s->count; i++){
        const Wck_City_t& c = s->cities[i];
        char value[120], name[64];
        wck_Value_Of(c, value, sizeof(value));
        snprintf(name, sizeof(name), "%s, %s", c.name, c.country);
        JsonObject item = cities.add<JsonObject>();
        item["name"] = name;
        item["value"] = value;
        char hex[16];
        wck_Hex(hex, sizeof(hex), c.color);
        values["color" + String(i)] = hex;
        if (!valid) continue;
        // The status panel's rows: each city's time now.
        const int32_t offset = wck_Offset_Of(c.tz, now);
        Wck_Local_t t;
        wck_Local(now, offset, t);
        char time[12], meridiem[4], difference[12], line[64];
        wck_Clock_Text(t, s->hour24, time, sizeof(time), meridiem, sizeof(meridiem));
        wck_Difference_Text(offset - here, difference, sizeof(difference));
        const int day = wck_Day_Difference(now, offset, here);
        snprintf(line, sizeof(line), "%s%s%s, %s%s", time, meridiem[0] ? " " : "", meridiem, strcmp(difference, "SAME") ? difference : "same as here",
                 day > 0 ? ", tomorrow" : day < 0 ? ", yesterday" : "");
        const uint8_t period = wck_Period(t.hour, t.minute, s->officeFrom, s->officeTo);
        JsonObject row = rows.add<JsonObject>();
        row["label"] = c.name;
        row["value"] = line;
        row["state"] = period == WCK_OFFICE ? "ok" : period == WCK_NIGHT ? "off" : "info";
    }
    values["ClockMode"] = s->view;
    values["hour24"] = s->hour24;
    values["cycle"] = s->cycle;
    values["seconds"] = s->seconds;
    char from[8], to[8];
    mtb_Kit_Clock_Text(from, sizeof(from), s->officeFrom);
    mtb_Kit_Clock_Text(to, sizeof(to), s->officeTo);
    values["from"] = from;
    values["to"] = to;
    heap_caps_free(s);
}

// Command 0 (the old page): {"dCityIndex": 0-5, "dCityName": "Tokyo, Japan", "dTimeZone": "JST-9"}.
static void wck_Ble_City_Legacy(JsonDocument& command){
    long index;
    const char* text = command["dCityName"];
    const char* tz = command["dTimeZone"];
    if (!mtb_Kit_Json_Range(command["dCityIndex"], 0, WCK_CITIES - 1, index) || text == nullptr || tz == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    char name[64];
    snprintf(name, sizeof(name), "%s", text);
    for (char* c = name; *c; c++) if (*c == '[' || *c == ']') *c = ' ';
    char* comma = strchr(name, ',');
    const char* country = comma ? comma + 1 : "";
    if (comma) *comma = '\0';
    while (*country == ' ') country++;
    char* city = name;
    while (*city == ' ') city++;
    for (size_t n = strlen(city); n && city[n - 1] == ' '; n--) city[n - 1] = '\0';
    Wck_City_t c;
    if (!wck_Find(city, country, c)){
        memset(&c, 0, sizeof(c));
        snprintf(c.name, sizeof(c.name), "%.23s", city);
        snprintf(c.country, sizeof(c.country), "%.31s", country);
        snprintf(c.tz, sizeof(c.tz), "%s", tz);
        Wck_Tz_t check;
        if (!wck_Tz_Parse(c.tz, check)){ mtb_Ble_App_Cmd_Reject(); return; }
    }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    c.color = next->cities[index].color;
    if (index >= next->count) index = next->count < WCK_CITIES ? next->count : WCK_CITIES - 1;
    next->cities[index] = c;
    if (index >= next->count) next->count = (uint8_t)(index + 1);
    wck_End_Change(next);
}

// Command 1: {"dCityIndex": 0-5, "value": "0xFFRRGGBB"}: a city's colour on the board.
static void wck_Ble_Color(JsonDocument& command){
    long index;
    uint16_t color;
    if (!mtb_Kit_Json_Range(command["dCityIndex"], 0, WCK_CITIES - 1, index) || !mtb_Kit_Parse_Color(command["value"], color)){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->cities[index].color = color;
    wck_End_Change(next);
}

// Command 2: {"ClockMode": 0 one city, 1 board, 2 planner}
static void wck_Ble_View(JsonDocument& command){
    long view;
    if (!mtb_Kit_Json_Range(command["ClockMode"], 0, WCK_VIEW_COUNT - 1, view)){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->view = (uint8_t)view;
    wck_End_Change(next);
}

// Command 3: {} - set the clock from the internet now.
static void wck_Ble_Sync(JsonDocument&){ mtb_Launch_This_Service(mtb_Sntp_Time_Sv); }

// Command 4: {"cities": [{"name", "value": "Tokyo|Japan|jp|JST-9"}, ...]}: 1-6, in order.
static void wck_Ble_Cities(JsonDocument& command){
    JsonArray list = command["cities"];
    if (list.isNull() || list.size() == 0 || list.size() > WCK_CITIES){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_City_t chosen[WCK_CITIES];
    uint8_t count = 0;
    for (JsonVariant item : list){
        if (!wck_Parse_Value(item["value"] | "", chosen[count])){ mtb_Ble_App_Cmd_Reject(); return; }
        count++;
    }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    // A city kept keeps its colour.
    for (uint8_t i = 0; i < count; i++){
        chosen[i].color = 0;
        for (uint8_t k = 0; k < next->count; k++) if (strcmp(next->cities[k].name, chosen[i].name) == 0 && strcmp(next->cities[k].tz, chosen[i].tz) == 0) chosen[i].color = next->cities[k].color;
    }
    memset(next->cities, 0, sizeof(next->cities));
    memcpy(next->cities, chosen, sizeof(chosen[0]) * count);
    next->count = count;
    wck_End_Change(next);
}

// Command 5: {"query": "lag"}: the city list, answered as 248.
static void wck_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) > 40){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_City_t* results = (Wck_City_t*)heap_caps_malloc(sizeof(Wck_City_t) * 12, MALLOC_CAP_SPIRAM);
    if (results == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    const int count = wck_Search(query, results, 12);
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    const time_t now = time(nullptr);
    for (int i = 0; i < count; i++){
        char name[64], value[120], detail[48];
        snprintf(name, sizeof(name), "%s, %s", results[i].name, results[i].country);
        wck_Value_Of(results[i], value, sizeof(value));
        if (now > WCK_CLOCK_VALID_AFTER){
            Wck_Local_t t;
            wck_Local(now, wck_Offset_Of(results[i].tz, now), t);
            snprintf(detail, sizeof(detail), "Now %02d:%02d there", t.hour, t.minute);
        } else snprintf(detail, sizeof(detail), "%s", results[i].tz);
        JsonObject item = list.add<JsonObject>();
        item["name"] = name;
        item["value"] = value;
        item["detail"] = detail;
    }
    heap_caps_free(results);
    if (worldClock_App) worldClock_App->mtb_App_Send_Search_Results("cities", query, list, nullptr);
}

// Command 6: {"hour24": 0-1}
static void wck_Ble_Hour24(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["hour24"], 0, 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->hour24 = (uint8_t)value;
    wck_End_Change(next);
}

// Command 7: {"cycle"} and/or {"seconds": 5-120}
static void wck_Ble_Cycle(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 5, 120, seconds);
    if (!hasCycle && !hasSeconds){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->cycle = (uint8_t)cycle;
    if (hasSeconds) next->seconds = (uint8_t)seconds;
    wck_End_Change(next);
}

// Command 8: {"from": "09:00", "to": "17:00"}: working hours, for the planner and the panel's colours.
static void wck_Ble_Office(JsonDocument& command){
    const int from = mtb_Kit_Parse_Clock(command["from"]), to = mtb_Kit_Parse_Clock(command["to"]);
    if (from < 0 || to < 0 || from == to){ mtb_Ble_App_Cmd_Reject(); return; }
    Wck_Settings_t* next = wck_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->officeFrom = (uint16_t)from;
    next->officeTo = (uint16_t)to;
    wck_End_Change(next);
}
