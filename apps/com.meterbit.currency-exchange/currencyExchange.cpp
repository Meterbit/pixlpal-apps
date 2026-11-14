/**
 * Currency Exchange app, ID {4,2}: exchange rates from ExchangeRate-API for up to 12 currencies against a base,
 * with the featured pair in big digits, its changes and a month's sparkline, a board, a converter, and the plan.
 *
 * Works without a key (the provider's daily keyless feed, with its attribution on screen). A free key gives
 * 1,500 requests a month; Pro and Business add hourly or 5-minute rates and history. The plan is read from the
 * key's quota, so nothing changes on Pixlpal after an upgrade.
 *
 * Data: cfx_Service.cpp. Screens: cfx_Ui.cpp. Model: cfx_Model.cpp.
 */
#include "mtb_engine.h"
#include <ctype.h>
#include <cmath>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "cfx_Service.h"
#include "cfx_Ui.h"
#include "cfx_Mobile_Ui.h"

#define CFX_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t currencyExchange_Task_H = NULL;
void currencyExchange_App_Task(void *);
static void cfx_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *currencyExchange_App;
Mtb_Applications* currencyExchange_App_GetInstance() {
    if (!currencyExchange_App){
        currencyExchange_App = new Mtb_Applications_StatusBar(currencyExchange_App_Task, &currencyExchange_Task_H, "Currency Exchange", {4,2}, 8192);
        currencyExchange_App->mtb_App_Set_Mobile_UI(CFX_UI_MANIFEST, CFX_UI_API, CFX_UI_DATA, cfx_Mobile_Ui_Values);
    }
    return currencyExchange_App;
}
MTB_REGISTER_APP(currencyExchange_App, 4, 2)

EXT_RAM_BSS_ATTR static Cfx_Settings_t cfxSettings;
static Cfx_Data_t* viewData = nullptr;      // PSRAM: the copy the screens draw.

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Cfx_Settings_t receivedSettings;

static void cfx_Button(button_event_t button);
static void cfx_Ble_Base(JsonDocument& command);
static void cfx_Ble_Watch(JsonDocument& command);
static void cfx_Ble_Featured(JsonDocument& command);
static void cfx_Ble_Key(JsonDocument& command);
static void cfx_Ble_Amount(JsonDocument& command);
static void cfx_Ble_Pages(JsonDocument& command);
static void cfx_Ble_Inverse(JsonDocument& command);
static void cfx_Ble_Refresh(JsonDocument& command);

//##############################################################################################################
static uint32_t cfx_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)CFX_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool cfx_Read_Saved_Settings(Cfx_Settings_t& settings){
    cfx_Settings_Default(settings);
    Cfx_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(CFX_NVS_KEY, &saved, sizeof(Cfx_Settings_t));
    const bool ok = saved.magic == CFX_SETTINGS_MAGIC;
    if (ok) settings = saved;
    cfx_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void cfx_Save_Settings(void){ mtb_Write_Nvs_Struct(CFX_NVS_KEY, &cfxSettings, sizeof(Cfx_Settings_t)); }

// What the screen says instead of the pages, or false to show them.
static bool cfx_Message(const Cfx_Status_t& status, bool online, uint32_t now, const char*& title, const char*& detail){
    if (viewData->rateCount > 0 && strcmp(viewData->base, cfxSettings.base) == 0) return false;
    if (status.keyRejected){ title = "KEY NOT ACCEPTED"; detail = "ExchangeRate-API did not accept the key. Check it on your dashboard and send it again from the Pixlpal app."; return true; }
    if (status.inactive){ title = "CONFIRM YOUR EMAIL"; detail = "ExchangeRate-API needs the account's email confirmed before the key works."; return true; }
    if (status.quotaUsed){ title = "REQUESTS USED UP"; detail = "This month's requests are used. Rates come back when the quota renews."; return true; }
    if (!online){ title = "OFFLINE"; detail = "Rates appear once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "Rates appear once Pixlpal knows the time."; return true; }
    title = "GETTING RATES";
    detail = cfxSettings.apiKey[0] ? "Asking ExchangeRate-API with your key..." : "From ExchangeRate-API's free daily feed. Add a free key in the Pixlpal app for more.";
    return true;
}

//##############################################################################################################
void currencyExchange_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(cfx_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cfx_Ble_Base, cfx_Ble_Watch, cfx_Ble_Featured, cfx_Ble_Key, cfx_Ble_Amount, cfx_Ble_Pages,
                                       cfx_Ble_Inverse, cfx_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!cfx_Read_Saved_Settings(cfxSettings)) cfx_Save_Settings();
    if (viewData == nullptr) viewData = (Cfx_Data_t*)heap_caps_calloc(1, sizeof(Cfx_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    cfx_Service_Begin(cfxSettings);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, pageStartMs = millis(), pushMs = 0;
    uint8_t page = CFX_PAGE_PAIR, boardPage = 0, featured = cfxSettings.featured;
    Cfx_Status_t status = {};
    bool redraw = true;
    int lastMinute = -1;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = cfx_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const uint8_t oldFeatured = cfxSettings.featured;
            cfxSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            cfx_Settings_Clamp(cfxSettings);
            cfx_Save_Settings();
            cfx_Service_Set_Settings(cfxSettings);
            if (cfxSettings.featured != oldFeatured){ featured = cfxSettings.featured; page = CFX_PAGE_PAIR; pageStartMs = millis(); }
            if (featured >= cfxSettings.watchCount) featured = 0;
            redraw = true;
        }
        cfx_Service_Poll(online, now);
        const bool newData = cfx_Service_Copy(*viewData, version);
        if (newData) redraw = true;
        cfx_Service_Get_Status(status);
        if (newData || millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next page; double click, next pair; triple click, refresh; long press, rotation.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; cfxSettings.autoCycle = !cfxSettings.autoCycle; cfx_Save_Settings(); }
            else if (clicks == 1){
                if (page == CFX_PAGE_BOARD && boardPage + 1 < cfx_Board_Pages(cfxSettings.watchCount)) boardPage++;
                else { page = (page + 1) % CFX_PAGE_COUNT; boardPage = 0; }
            } else if (clicks == 2){ page = CFX_PAGE_PAIR; featured = cfxSettings.watchCount ? (featured + 1) % cfxSettings.watchCount : 0; }
            else if (clicks == 3) cfx_Service_Refresh_Now(now);
            pageStartMs = millis();
            redraw = true;
        }
        // Rotation: the pair, the board's pages, the converter, then the next pair. The plan page only by the knob.
        if (cfxSettings.autoCycle && millis() - pageStartMs >= cfxSettings.pageSeconds * 1000UL){
            pageStartMs = millis();
            if (page == CFX_PAGE_PAIR){ page = CFX_PAGE_BOARD; boardPage = 0; }
            else if (page == CFX_PAGE_BOARD && boardPage + 1 < cfx_Board_Pages(cfxSettings.watchCount)) boardPage++;
            else if (page == CFX_PAGE_BOARD) page = CFX_PAGE_CONVERT;
            else { page = CFX_PAGE_PAIR; featured = cfxSettings.watchCount ? (featured + 1) % cfxSettings.watchCount : 0; }
            redraw = true;
        }
        if (now && (int)(now / 60) != lastMinute){ lastMinute = (int)(now / 60); redraw = true; }     // The footer's "next in".

        if (redraw){
            redraw = false;
            Cfx_View_t view = {};
            view.data = viewData;
            view.settings = &cfxSettings;
            view.status = &status;
            view.page = page;
            view.featured = featured;
            view.boardPage = boardPage;
            view.now = now;
            const char *title, *detail;
            if (cfx_Message(status, online, now, title, detail)){ view.message = title; view.detail = detail; }
            mtb_Panel_Begin_Update();
            cfx_Draw(view);
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void cfx_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Cfx_Settings_t* cfx_Begin_Change(void){
    Cfx_Settings_t* next = (Cfx_Settings_t*)heap_caps_malloc(sizeof(Cfx_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : cfxSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void cfx_End_Change(Cfx_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void cfx_Status_Values(JsonObject values){
    Cfx_Status_t status;
    cfx_Service_Get_Status(status);
    if (!status.started) return;
    uint8_t plan, locked;
    Cfx_Quota_t q;
    uint32_t last, next;
    cfx_Service_Get_Plan(plan, q, last, next, locked);
    if (status.keyless){ values["plan"] = "No key (free daily feed)"; values["planState"] = "info"; }
    else { values["plan"] = cfx_Plan_Name(plan); values["planState"] = plan >= CFX_PLAN_PRO ? "ok" : plan == CFX_PLAN_FREE ? "info" : "off"; }
    values["paid"] = plan >= CFX_PLAN_PRO ? 1 : 0;
    if (status.keyRejected){ values["keyText"] = "Not accepted by ExchangeRate-API"; values["keyState"] = "error"; }
    else if (status.inactive){ values["keyText"] = "Confirm your email with ExchangeRate-API"; values["keyState"] = "warn"; }
    else if (!status.keyless && q.seen){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    if (!status.keyless && q.seen && q.planQuota){
        values["left"] = q.remaining;
        values["limit"] = q.planQuota;
        values["quotaState"] = q.exhausted ? "error" : q.remaining * 10 < q.planQuota ? "warn" : "ok";
        char renews[32];
        snprintf(renews, sizeof(renews), "On day %u of each month", (unsigned)q.refreshDay);
        values["renews"] = renews;
    }
    if (next > last && last){
        const uint32_t every = next - last;
        values["cadence"] = every >= 20UL * 3600 ? "Daily" : every >= 50UL * 60 ? "Hourly" : "Every few minutes";
    }
    if (last) values["updated"] = last;
    if (next) values["next"] = next;
    if (!status.keyless) values["history"] = locked ? "Not in this plan" : plan >= CFX_PLAN_PRO ? "Included" : "Checking";
}

// What the phone shows when it opens. The key never leaves the device. Runs on the BLE parser task.
static void cfx_Mobile_Ui_Values(JsonObject values){
    Cfx_Settings_t* s = (Cfx_Settings_t*)heap_caps_malloc(sizeof(Cfx_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    cfx_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    values["base"] = s->base;
    JsonArray watch = values["watch"].to<JsonArray>();
    for (uint8_t i = 0; i < s->watchCount; i++) watch.add(s->watch[i]);
    if (s->watchCount) values["featured"] = s->watch[s->featured];
    char amount[24];
    snprintf(amount, sizeof(amount), "%.10g", s->amount);
    values["amount"] = amount;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["inverse"] = s->inverse;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    cfx_Status_Values(values);
}

static bool cfx_Code_Of(JsonVariantConst field, char* code){
    const char* text = field;
    if (text == nullptr || strlen(text) != 3) return false;
    for (int i = 0; i < 3; i++) code[i] = (char)toupper((unsigned char)text[i]);
    code[3] = '\0';
    return cfx_Is_Code(code);
}

// Command 0: {"base": "USD"}
static void cfx_Ble_Base(JsonDocument& command){
    char code[4];
    if (!cfx_Code_Of(command["base"], code)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memcpy(next->base, code, 4);
    cfx_End_Change(next);       // A watched code equal to the base drops out in the clamp.
}

// Command 1: {"watch": ["NGN", "EUR"]}: 1 to 12 currencies. Older phone apps send "[NGN, EUR]".
static void cfx_Ble_Watch(JsonDocument& command){
    char codes[CFX_MAX_WATCH][4];
    int count = 0;
    bool valid = true;
    auto add = [&](const char* text, size_t length){
        if (length != 3 || count >= CFX_MAX_WATCH){ valid = false; return; }
        char code[4] = {(char)toupper((unsigned char)text[0]), (char)toupper((unsigned char)text[1]), (char)toupper((unsigned char)text[2]), 0};
        if (!cfx_Is_Code(code)){ valid = false; return; }
        for (int k = 0; k < count; k++) if (strcmp(codes[k], code) == 0) return;
        memcpy(codes[count++], code, 4);
    };
    JsonVariant list = command["watch"];
    if (list.is<JsonArray>()){
        for (JsonVariant item : list.as<JsonArray>()){ const char* t = item; if (t) add(t, strlen(t)); else valid = false; }
    } else if (list.is<const char*>()){
        const char* t = list.as<const char*>();
        for (const char* c = t; *c; ){
            if (isalpha((unsigned char)*c)){ const char* start = c; while (isalpha((unsigned char)*c)) c++; add(start, (size_t)(c - start)); }
            else if (strchr("[], \"", *c)) c++;
            else { valid = false; break; }
        }
    } else valid = false;
    if (!valid || count == 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    char featured[4];
    memcpy(featured, next->watch[next->featured], 4);
    memset(next->watch, 0, sizeof(next->watch));
    for (int i = 0; i < count; i++) memcpy(next->watch[i], codes[i], 4);
    next->watchCount = (uint8_t)count;
    next->featured = 0;
    for (int i = 0; i < count; i++) if (strcmp(codes[i], featured) == 0) next->featured = (uint8_t)i;
    cfx_End_Change(next);
}

// Command 2: {"featured": "NGN"}: the big pair. A code not watched is added, if there is room.
static void cfx_Ble_Featured(JsonDocument& command){
    char code[4];
    if (!cfx_Code_Of(command["featured"], code)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (strcmp(code, next->base) == 0){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    int at = -1;
    for (uint8_t i = 0; i < next->watchCount; i++) if (strcmp(next->watch[i], code) == 0) at = i;
    if (at < 0){
        if (next->watchCount >= CFX_MAX_WATCH){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
        memcpy(next->watch[next->watchCount], code, 4);
        at = next->watchCount++;
    }
    next->featured = (uint8_t)at;
    cfx_End_Change(next);
}

// Command 3: {"api_key": "..."}: 24 hexadecimal characters for ExchangeRate-API; letters and digits, 16 to 40,
// accepted. "" goes back to the keyless feed.
static void cfx_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = key != nullptr && (length == 0 || (length >= 16 && length <= 40));
    for (size_t i = 0; valid && i < length; i++) if (!isalnum((unsigned char)key[i])) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, length);
    cfx_End_Change(next);
}

// Command 4: {"amount": "250000"} (or a number): the converter's amount, more than 0, up to a trillion.
static void cfx_Ble_Amount(JsonDocument& command){
    double amount = 0;
    JsonVariant field = command["amount"];
    if (field.is<double>() || field.is<long>()) amount = field.as<double>();
    else if (field.is<const char*>()){
        // "250,000" and "1 000" are fine; letters are not.
        char clean[32];
        size_t used = 0;
        for (const char* c = field.as<const char*>(); *c && used + 1 < sizeof(clean); c++){
            if (*c == ',' || *c == ' ' || *c == '_') continue;
            clean[used++] = *c;
        }
        clean[used] = '\0';
        char* end = nullptr;
        amount = strtod(clean, &end);
        if (end == clean || *end != '\0') amount = 0;
    }
    if (!(amount > 0) || amount > 1e12 || !std::isfinite(amount)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->amount = amount;
    cfx_End_Change(next);
}

// Command 5: {"cycle": 1} and/or {"seconds": 4-60}
static void cfx_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 4, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    cfx_End_Change(next);
}

// Command 6: {"inverse": 1}: show 1 NGN = 0.00074 USD instead of 1 USD = 1,348 NGN.
static void cfx_Ble_Inverse(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["inverse"], 0, 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cfx_Settings_t* next = cfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->inverse = (uint8_t)value;
    cfx_End_Change(next);
}

// Command 7: {} - fetch the rates now.
static void cfx_Ble_Refresh(JsonDocument&){
    if (!cfx_Service_Refresh_Now(cfx_Now())) mtb_Ble_App_Cmd_Reject();
}
