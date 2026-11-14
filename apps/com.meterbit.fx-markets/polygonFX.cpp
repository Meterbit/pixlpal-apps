/**
 * FX Markets app, ID {4,3}: forex pairs from Massive (formerly Polygon.io): a board of up to eight pairs with the
 * day's change and a month's sparkline, a candle chart of one pair, and the world's trading sessions with the
 * weekend close.
 *
 * Works on Massive's free Currencies Basic plan (end-of-day prices, 5 calls a minute, paced by the app). Once a day
 * it checks whether the key has snapshots; if it does (Currencies Starter and up), prices go live every 15 seconds
 * and the chart can show today's 15-minute bars. Data: pfx_Service.cpp; screens: pfx_Ui.cpp; model: pfx_Model.cpp.
 */
#include "mtb_engine.h"
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "pfx_Model.h"
#include "pfx_Service.h"
#include "pfx_Ui.h"
#include "pfx_Mobile_Ui.h"

#define PFX_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t polygonFX_Task_H = NULL;
void polygonFX_App_Task(void *);
static void pfx_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *polygonFX_App;
Mtb_Applications* polygonFX_App_GetInstance() {
    if (!polygonFX_App){
        polygonFX_App = new Mtb_Applications_StatusBar(polygonFX_App_Task, &polygonFX_Task_H, "FX Markets", {4,3}, 8192);
        polygonFX_App->mtb_App_Set_Mobile_UI(PFX_UI_MANIFEST, PFX_UI_API, nullptr, pfx_Mobile_Ui_Values);
    }
    return polygonFX_App;
}
MTB_REGISTER_APP(polygonFX_App, 4, 3)

EXT_RAM_BSS_ATTR static Pfx_Settings_t pfxSettings;
static Pfx_Data_t* viewData = nullptr;      // PSRAM
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Pfx_Settings_t receivedSettings;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;

static void pfx_Button(button_event_t button);
static void pfx_Ble_Search(JsonDocument& command);
static void pfx_Ble_Pairs(JsonDocument& command);
static void pfx_Ble_Intraday(JsonDocument& command);
static void pfx_Ble_Pages(JsonDocument& command);
static void pfx_Ble_Key(JsonDocument& command);
static void pfx_Ble_Refresh(JsonDocument& command);

//##############################################################################################################
static uint32_t pfx_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)PFX_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool pfx_Read_Saved_Settings(Pfx_Settings_t& settings){
    pfx_Settings_Default(settings);
    Pfx_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(PFX_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == PFX_SETTINGS_MAGIC;
    if (ok) settings = saved;
    pfx_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void pfx_Save_Settings(void){ mtb_Write_Nvs_Struct(PFX_NVS_KEY, &pfxSettings, sizeof(Pfx_Settings_t)); }

static int32_t pfx_Local_Offset(uint32_t now){
    const time_t t = now;
    struct tm local, utc;
    localtime_r(&t, &local);
    gmtime_r(&t, &utc);
    const int days = local.tm_yday - utc.tm_yday;
    const int dayFix = days > 1 ? -1 : days < -1 ? 1 : days;      // Across New Year.
    return ((dayFix * 24 + local.tm_hour - utc.tm_hour) * 60 + local.tm_min - utc.tm_min) * 60;
}

static bool pfx_Message(const Pfx_Status_t& status, bool online, uint32_t now, const char*& title, const char*& detail){
    if (status.keyMissing){ title = "ADD YOUR KEY"; detail = "Get a free key at massive.com (Currencies Basic) and paste it into FX Markets in the Pixlpal app."; return true; }
    if (status.keyRejected){ title = "KEY NOT ACCEPTED"; detail = "Massive did not accept the key. Check it on massive.com/dashboard/keys."; return true; }
    bool any = false;
    for (uint8_t i = 0; i < pfxSettings.pairCount; i++) if (viewData->quotes[i].valid) any = true;
    if (any) return false;
    if (!online){ title = "OFFLINE"; detail = "Prices appear once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "Prices appear once Pixlpal knows the time."; return true; }
    title = "GETTING PRICES";
    detail = "On the free plan, one pair every 13 seconds...";
    return true;
}

//##############################################################################################################
void polygonFX_App_Task(void *dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(pfx_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(pfx_Ble_Search, pfx_Ble_Pairs, pfx_Ble_Intraday, pfx_Ble_Pages, pfx_Ble_Key, pfx_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!pfx_Read_Saved_Settings(pfxSettings)) pfx_Save_Settings();
    if (viewData == nullptr) viewData = (Pfx_Data_t*)heap_caps_calloc(1, sizeof(Pfx_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    pfx_Service_Begin(pfxSettings);
    settingsReceived = false;
    clicksRequested = 0;
    longPressRequested = false;

    uint32_t version = 0, pageStartMs = millis(), pushMs = 0;
    uint8_t page = PFX_PAGE_BOARD;
    Pfx_Status_t status = {};
    bool redraw = true;
    int lastMinute = -1;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = pfx_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            pfxSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            pfx_Settings_Clamp(pfxSettings);
            pfx_Save_Settings();
            pfx_Service_Set_Settings(pfxSettings);
            redraw = true;
        }
        pfx_Service_Poll(online, now);
        if (pfx_Service_Copy(*viewData, version)){ redraw = true; THIS_APP->mtb_App_Push_Mobile_Values(); }
        pfx_Service_Get_Status(status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next page; double click, the next pair on the chart; triple, refresh; long press, rotation.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; pfxSettings.autoCycle = !pfxSettings.autoCycle; pfx_Save_Settings(); }
            else if (clicks == 1) page = (page + 1) % PFX_PAGE_COUNT;
            else if (clicks == 2){
                pfxSettings.focus = (uint8_t)((pfxSettings.focus + 1) % pfxSettings.pairCount);
                pfx_Save_Settings();
                pfx_Service_Set_Settings(pfxSettings);
                page = PFX_PAGE_CHART;
            } else if (clicks == 3) pfx_Service_Refresh(now);
            pageStartMs = millis();
            redraw = true;
        }
        // Rotation: board, chart, sessions; the chart steps through the pairs; the plan only by the knob.
        if (pfxSettings.autoCycle && millis() - pageStartMs >= pfxSettings.pageSeconds * 1000UL * (page == PFX_PAGE_BOARD ? 2 : 1)){
            pageStartMs = millis();
            if (page == PFX_PAGE_CHART && pfxSettings.pairCount > 1 && pfxSettings.focus + 1 < pfxSettings.pairCount){
                pfxSettings.focus++;
                pfx_Service_Set_Settings(pfxSettings);
            } else {
                if (page == PFX_PAGE_CHART && pfxSettings.focus != 0){ pfxSettings.focus = 0; pfx_Service_Set_Settings(pfxSettings); }
                page = page >= PFX_PAGE_SESSIONS ? PFX_PAGE_BOARD : page + 1;
            }
            redraw = true;
        }
        if (now && (int)(now / 60) != lastMinute){ lastMinute = (int)(now / 60); redraw = true; }

        if (redraw){
            redraw = false;
            Pfx_View_t view = {};
            view.data = viewData;
            view.settings = &pfxSettings;
            view.page = page;
            view.now = now;
            view.localOffset = now ? pfx_Local_Offset(now) : 0;
            const char *title, *detail;
            if (page != PFX_PAGE_SESSIONS && pfx_Message(status, online, now, title, detail)){ view.message = title; view.detail = detail; }
            mtb_Panel_Begin_Update();
            pfx_Draw(view);
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void pfx_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Pfx_Settings_t* pfx_Begin_Change(void){
    Pfx_Settings_t* next = (Pfx_Settings_t*)heap_caps_malloc(sizeof(Pfx_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : pfxSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void pfx_End_Change(Pfx_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

// What the phone shows. The key never leaves the device. Runs on the BLE parser task.
static void pfx_Mobile_Ui_Values(JsonObject values){
    Pfx_Settings_t* s = (Pfx_Settings_t*)heap_caps_malloc(sizeof(Pfx_Settings_t), MALLOC_CAP_SPIRAM);
    Pfx_Data_t* d = (Pfx_Data_t*)heap_caps_malloc(sizeof(Pfx_Data_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr || d == nullptr){ heap_caps_free(s); heap_caps_free(d); return; }
    pfx_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonArray pairs = values["pairs"].to<JsonArray>();
    for (uint8_t i = 0; i < s->pairCount; i++){
        char text[8];
        pfx_Pair_Text(s->pairs[i], text, sizeof(text));
        JsonObject p = pairs.add<JsonObject>();
        p["name"] = text;
        p["value"] = text;
    }
    values["intraday"] = s->intraday;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    Pfx_Status_t status;
    pfx_Service_Get_Status(status);
    uint32_t version = 0;
    if (status.started && pfx_Service_Copy(*d, version)){
        values["plan"] = !d->planKnown ? "Checking" : d->realtime ? "Starter or higher: live prices" : "Currencies Basic (free): end of day";
        values["planState"] = !d->planKnown ? "off" : d->realtime ? "ok" : "info";
        values["paid"] = d->realtime;
        if (status.keyRejected){ values["keyText"] = d->errorText[0] ? d->errorText : "Not accepted"; values["keyState"] = "error"; }
        else if (d->planKnown){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
        values["callsToday"] = d->callsToday;
        char board[200] = "";
        for (uint8_t i = 0; i < s->pairCount && i < 4; i++){
            const Pfx_Quote_t& q = d->quotes[i];
            if (!q.valid) continue;
            char pair[8];
            pfx_Pair_Text(s->pairs[i], pair, sizeof(pair));
            const size_t used = strlen(board);
            snprintf(board + used, sizeof(board) - used, "%s%s %.*f", used ? ", " : "", pair, pfx_Decimals(q.last), q.last);
        }
        values["board"] = board[0] ? board : "Waiting for prices";
    }
    const uint32_t now = pfx_Now();
    if (now){
        char open[64] = "";
        for (uint8_t k = 0; k < PFX_SESSIONS; k++) if (pfx_Session_Open(k, now)){ const size_t used = strlen(open); snprintf(open + used, sizeof(open) - used, "%s%s", used ? ", " : "", pfx_Session_Name(k)); }
        if (!pfx_Market_Open(now)){ const uint32_t wait = pfx_Until_Open(now); snprintf(open, sizeof(open), "Closed for the weekend: opens in %u h", (unsigned)((wait + 1800) / 3600)); }
        else if (!open[0]) snprintf(open, sizeof(open), "Open, between the main sessions");
        values["market"] = open;
        values["marketState"] = pfx_Market_Open(now) ? "ok" : "off";
    }
    if (status.lastFetch) values["updated"] = status.lastFetch;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    heap_caps_free(d);
}

// Command 0: {"query": "naira"}: pairs from the currency list, answered as command 248.
static void pfx_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) > 40){ mtb_Ble_App_Cmd_Reject(); return; }
    Pfx_Result_Item_t results[12];
    const int count = pfx_Search(query, results, 12);
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < count; i++){
        JsonObject item = list.add<JsonObject>();
        item["name"] = results[i].pair;
        item["value"] = results[i].pair;
        item["detail"] = results[i].detail;
    }
    if (polygonFX_App) polygonFX_App->mtb_App_Send_Search_Results("pairs", query, list, nullptr);
}

// Command 1: {"pairs": [{"name": "EUR/USD", "value": "EUR/USD"}, ...]}: 1 to 8, the first on the chart.
static void pfx_Ble_Pairs(JsonDocument& command){
    JsonArray list = command["pairs"];
    if (list.isNull() || list.size() == 0 || list.size() > PFX_PAIRS){ mtb_Ble_App_Cmd_Reject(); return; }
    Pfx_Pair_Code_t pairs[PFX_PAIRS];
    uint8_t count = 0;
    for (JsonVariant item : list){
        const char* text = item["value"] | (item["name"] | "");
        Pfx_Pair_Code_t p;
        if (!pfx_Parse_Pair(text, p)){ mtb_Ble_App_Cmd_Reject(); return; }
        bool repeat = false;
        for (uint8_t k = 0; k < count; k++) if (memcmp(&pairs[k], &p, sizeof(p)) == 0) repeat = true;
        if (!repeat) pairs[count++] = p;
    }
    Pfx_Settings_t* next = pfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->pairs, 0, sizeof(next->pairs));
    memcpy(next->pairs, pairs, sizeof(pairs[0]) * count);
    next->pairCount = count;
    next->focus = 0;
    pfx_End_Change(next);
}

// Command 2: {"intraday": 0-1}: today's 15-minute chart when the plan has it.
static void pfx_Ble_Intraday(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["intraday"], 0, 1, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Pfx_Settings_t* next = pfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->intraday = (uint8_t)value;
    pfx_End_Change(next);
}

// Command 3: {"cycle"} and/or {"seconds": 4-60}
static void pfx_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 4, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Pfx_Settings_t* next = pfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    pfx_End_Change(next);
}

// Command 4: {"api_key": "..."}: a Massive (or older Polygon.io) key.
static void pfx_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    if (!pfx_Key_Valid(key)){ mtb_Ble_App_Cmd_Reject(); return; }
    Pfx_Settings_t* next = pfx_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, strlen(key));
    pfx_End_Change(next);
}

// Command 5: {} - read every pair now.
static void pfx_Ble_Refresh(JsonDocument&){
    if (!pfx_Service_Refresh(pfx_Now())) mtb_Ble_App_Cmd_Reject();
}
