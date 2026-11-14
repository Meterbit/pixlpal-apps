/**
 * Oil Price API app, ID {4,5}: oil, gas, fuel, coal and carbon prices from OilPriceAPI (oilpriceapi.com).
 *
 * This file holds the app task: input, page rotation, the ticker and the settings. The data comes from
 * opa_Service.cpp, which downloads it in a task of its own and paces the requests to the plan's quota, and the
 * screen is drawn by opa_Ui.cpp.
 */
#include "mtb_engine.h"
#include "mtb_text_scroll.h"
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "opa_Config.h"
#include "opa_Service.h"
#include "opa_Ui.h"
#include "opa_Mobile_Ui.h"

static const char TAG[] = "OIL_PRICE_API";

#define OPA_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define OPA_DIRTY_HERO          0x01
#define OPA_DIRTY_PAGE          0x02
#define OPA_DIRTY_STRIP         0x04
#define OPA_DIRTY_TICKER        0x08
#define OPA_DIRTY_ALL           (OPA_DIRTY_HERO | OPA_DIRTY_PAGE | OPA_DIRTY_STRIP | OPA_DIRTY_TICKER)
#define OPA_CLOCK_VALID_AFTER   1700000000UL
#define OPA_TICKER_LEN          420

EXT_RAM_BSS_ATTR TaskHandle_t oilPriceApi_Task_H = NULL;
void oilPriceApi_App_Task(void*);
static void opa_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar* oilPriceApi_App;
Mtb_Applications* oilPriceApi_App_GetInstance(){
    if (!oilPriceApi_App){
        oilPriceApi_App = new Mtb_Applications_StatusBar(oilPriceApi_App_Task, &oilPriceApi_Task_H, "Oil Price API", {4,5}, 8192);
        oilPriceApi_App->mtb_App_Set_Mobile_UI(OPA_UI_MANIFEST, OPA_UI_API, OPA_UI_DATA, opa_Mobile_Ui_Values);
    }
    return oilPriceApi_App;
}
MTB_REGISTER_APP(oilPriceApi_App, 4, 5)

EXT_RAM_BSS_ATTR static Opa_Settings_t opaSettings;
static Opa_Data_t* viewData = nullptr;      // PSRAM, kept for the power-on session: the copy the screen draws.

// Requests from the button and BLE tasks. The app task acts on them, so only it touches the settings and the view.
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t featuredStepsRequested = 0;
static volatile uint8_t pageStepsRequested = 0;
static volatile bool autoCycleToggled = false;
static volatile bool refreshRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Opa_Settings_t receivedSettings;   // Written by BLE handlers under requestLock.

static void opa_Button(button_event_t button);
static void opa_Ble_Feature(JsonDocument& command);
static void opa_Ble_Set_Watchlist(JsonDocument& command);
static void opa_Ble_Set_Range(JsonDocument& command);
static void opa_Ble_Set_Key(JsonDocument& command);
static void opa_Ble_Set_Pace(JsonDocument& command);
static void opa_Ble_Set_Pages(JsonDocument& command);
static void opa_Ble_Refresh_Now(JsonDocument& command);

//##############################################################################################################
static uint32_t opa_Now(void){
    const time_t now = time(nullptr);
    return (now > (time_t)OPA_CLOCK_VALID_AFTER) ? (uint32_t)now : 0;
}

// The settings as saved, with defaults for anything never set. Returns false if NVS held none.
static bool opa_Read_Saved_Settings(Opa_Settings_t& settings){
    Opa_Settings_t defaults;
    opa_Settings_Default(defaults);
    settings = defaults;
    mtb_Read_Nvs_Struct(OPA_NVS_KEY, &settings, sizeof(Opa_Settings_t));
    const bool saved = settings.magic == OPA_SETTINGS_MAGIC;
    if (!saved) settings = defaults;
    // A key built into the firmware fills in for one never set from the phone.
    if (settings.apiKey[0] == '\0') memcpy(settings.apiKey, defaults.apiKey, sizeof(settings.apiKey));
    opa_Settings_Clamp(settings);
    return saved;
}

static void opa_Save_Settings(void){
    mtb_Write_Nvs_Struct(OPA_NVS_KEY, &opaSettings, sizeof(Opa_Settings_t));
}

static int opa_Featured_Index(const Opa_Data_t& data){
    const int index = opa_Find_Quote(data, opaSettings.featured);
    return index >= 0 ? index : (data.quoteCount > 0 ? 0 : -1);
}

// Codes whose source is the U.S. EIA, which asks to be credited where its data is shown.
static bool opa_From_Eia(const char* code){
    static const char* const EIA_CODES[] = {"NATURAL_GAS_USD", "DIESEL_USD", "GASOLINE_USD", "DIESEL_RETAIL_USD"};
    for (const char* eia : EIA_CODES) if (strcmp(code, eia) == 0) return true;
    return false;
}

// What to say instead of a page that has nothing to show, and in the ticker while there is a problem.
static const char* opa_Status_Text(const Opa_Status_t& status, const Opa_Data_t& data, bool online, uint32_t now, char* buffer, size_t size){
    const bool haveData = data.quotesFetched != 0;
    if (status.keyRejected) return "OilPriceAPI rejected the API key. Check it at oilpriceapi.com and send it again from the Pixlpal app.";
    if (status.emailUnconfirmed) return "Confirm your e-mail address with OilPriceAPI: the key is paused until you do.";
    if (!online) return haveData ? "Offline. Showing the last prices received." : "No internet connection. Prices appear once connected.";
    if (now == 0) return "Waiting for the clock to be set...";
    if (status.quotaUsed){
        char when[24] = "it resets";
        if (data.account.reset > now){
            const time_t reset = (time_t)data.account.reset;
            struct tm local;
            localtime_r(&reset, &local);
            strftime(when, sizeof(when), data.account.reset - now < 86400 ? "%H:%M" : "%d %b %H:%M", &local);
        }
        snprintf(buffer, size, "The OilPriceAPI quota is used up. Prices resume at %s. A bigger plan: oilpriceapi.com/pricing", when);
        return buffer;
    }
    if (status.rateLimited) return "OilPriceAPI asked for fewer requests. Showing the last prices received.";
    if (status.lowMemory) return "Low memory: the download will be retried shortly.";
    if (status.failing) return haveData ? "Cannot reach OilPriceAPI. Showing the last prices received." : "Cannot reach OilPriceAPI. Retrying...";
    if (!haveData) return status.demo ? "Downloading OilPriceAPI demo prices..." : "Downloading prices from OilPriceAPI...";
    if (status.demo) return "Demo prices, every 90 minutes. Get a free OilPriceAPI key (no card) at oilpriceapi.com and send it from the Pixlpal app.";
    return nullptr;
}

//##############################################################################################################
enum Opa_Ticker_Item_t : uint8_t { OPA_TICK_SUMMARY = 0, OPA_TICK_PLAN, OPA_TICK_SOURCE, OPA_TICK_COUNT };

static const uint16_t T_SUMMARY = OPA_RGB(255, 176, 0);
static const uint16_t T_PLAN = OPA_RGB(180, 140, 255);
static const uint16_t T_SOURCE = OPA_RGB(130, 130, 130);
static const uint16_t T_STATUS = OPA_RGB(255, 120, 40);

// Builds the next ticker message: a status message, if any, at the start of every round. Returns false if there
// is nothing to say.
static bool opa_Next_Ticker_Text(const Opa_Data_t& data, const Opa_Status_t& status, const char* statusText, uint8_t& item,
                                 bool& statusDue, char* dest, size_t size, uint16_t& color){
    if (statusText != nullptr && statusDue){
        statusDue = false;
        snprintf(dest, size, "%s", statusText);
        color = T_STATUS;
        return true;
    }
    for (int attempt = 0; attempt < OPA_TICK_COUNT; attempt++){
        const uint8_t current = item;
        item = (item + 1) % OPA_TICK_COUNT;
        if (item == OPA_TICK_SUMMARY) statusDue = true;
        if (current == OPA_TICK_SUMMARY){
            size_t used = 0;
            for (int i = 0; i < data.quoteCount && used + 40 < size; i++){
                const Opa_Quote_t& quote = data.quotes[i];
                if (isnan(quote.price)) continue;
                char price[16], percentText[16] = "";
                float percent;
                opa_Format_Price(price, sizeof(price), quote.price);
                if (opa_Quote_Change(data, quote, OPA_W_24H, percent)) opa_Format_Percent(percentText, sizeof(percentText), percent);
                used += snprintf(dest + used, size - used, "%s%s %s %s", used ? "   " : "", opa_Label(quote.code), price, percentText);
            }
            if (used == 0) continue;
            color = T_SUMMARY;
            return true;
        }
        if (current == OPA_TICK_PLAN){
            const Opa_Account_t& account = data.account;
            if (status.demo || account.seen == 0 || account.limit == 0) continue;
            char plan[24];
            opa_Ui_Plan_Name(plan, sizeof(plan), account, false);
            const uint32_t used = account.limit > account.remaining ? account.limit - account.remaining : 0;
            const char* window = account.window == OPA_QUOTA_MONTH ? "this month" : account.window == OPA_QUOTA_TRIAL ? "in the trial" : "today";
            const uint32_t minutes = (status.quoteInterval + 30) / 60;
            const int written = snprintf(dest, size, "OilPriceAPI %s plan: %u of %u requests used %s. Prices every %u min.",
                                         plan, (unsigned)used, (unsigned)account.limit, window, (unsigned)minutes);
            if (!opa_Plan_Is_Paid(account) && written > 0 && (size_t)written < size){
                snprintf(dest + written, size - written, " Paid plans add history, futures and more: oilpriceapi.com/pricing");
            }
            color = T_PLAN;
            return true;
        }
        // Attribution, with when the prices were received.
        if (data.quotesFetched == 0) continue;
        const time_t fetchedTime = (time_t)data.quotesFetched;
        struct tm local;
        localtime_r(&fetchedTime, &local);
        char retrieved[24];
        strftime(retrieved, sizeof(retrieved), "%d %b %H:%M", &local);
        int used = snprintf(dest, size, "Data: OilPriceAPI.com%s, retrieved %s.", data.demo ? " (demo)" : "", retrieved);
        bool eia = false;
        for (int i = 0; i < data.quoteCount; i++) if (opa_From_Eia(data.quotes[i].code) && !isnan(data.quotes[i].price)) eia = true;
        if (eia && used > 0 && (size_t)used < size) used += snprintf(dest + used, size - used, " Gas, gasoline and diesel: Source: U.S. Energy Information Administration (EIA).");
        if (used > 0 && (size_t)used < size) snprintf(dest + used, size - used, " Not investment advice.");
        color = T_SOURCE;
        return true;
    }
    return false;
}

// The next page with something to show, @p steps pages on.
static uint8_t opa_Step_Page(const Opa_View_t& view, uint8_t page, uint8_t steps){
    Opa_View_t probe = view;
    for (uint8_t s = 0; s < steps; s++){
        for (int k = 1; k <= OPA_PAGE_COUNT; k++){
            const uint8_t candidate = (page + k) % OPA_PAGE_COUNT;
            if (opa_Ui_Page_Available(probe, candidate)){ page = candidate; break; }
        }
    }
    return page;
}

//##############################################################################################################
void oilPriceApi_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(opa_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(opa_Ble_Feature, opa_Ble_Set_Watchlist, opa_Ble_Set_Range, opa_Ble_Set_Key,
                                       opa_Ble_Set_Pace, opa_Ble_Set_Pages, opa_Ble_Refresh_Now);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!opa_Read_Saved_Settings(opaSettings)) opa_Save_Settings();
    if (viewData == nullptr) viewData = (Opa_Data_t*)heap_caps_calloc(1, sizeof(Opa_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){
        ESP_LOGE(TAG, "Out of memory");
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    opa_Service_Begin(opaSettings);

    featuredStepsRequested = 0;
    pageStepsRequested = 0;
    autoCycleToggled = false;
    refreshRequested = false;
    settingsReceived = false;

    Mtb_ScrollText_t ticker(0, OPA_TICKER_Y, PANEL_RES_X, Terminal6x8, T_SUMMARY, 20, 1);
    char tickerText[OPA_TICKER_LEN];
    char statusBuffer[160], shownStatusText[160] = "";
    uint16_t tickerColor = T_SUMMARY;
    uint8_t tickerItem = OPA_TICK_SUMMARY;
    bool tickerStatusDue = true;
    uint32_t nextTickerCheckMs = 0;

    uint32_t dataVersion = 0, valuesPushMs = 0;
    Opa_Status_t status = {};
    bool shownStale = false;
    const char* statusText = nullptr;
    uint8_t page = OPA_PAGE_CHART;
    uint32_t pageChangeMs = millis();
    uint8_t dirty = OPA_DIRTY_ALL;
    bool wholeScreen = true;
    int selected = -1;
    uint32_t lastMinute = 0;

    for (;;){
        const uint32_t beforeCheckpoint = millis();
        if (THIS_APP_IS_ACTIVE != pdTRUE) break;
        // The checkpoint only takes this long if the app was paused, and then the screen has been used by something else.
        if (millis() - beforeCheckpoint > 300) wholeScreen = true;

        const uint32_t now = opa_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;

        // Requests from other tasks.
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            opaSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            opa_Settings_Clamp(opaSettings);
            opa_Save_Settings();
            opa_Service_Set_Settings(opaSettings);
            pageChangeMs = millis();
            selected = -1;
            dirty |= OPA_DIRTY_ALL;
        }
        if (autoCycleToggled){
            autoCycleToggled = false;
            opaSettings.autoCycle = !opaSettings.autoCycle;
            opa_Save_Settings();
            pageChangeMs = millis();
        }
        if (refreshRequested){
            refreshRequested = false;
            ESP_LOGI(TAG, "Refresh %s", opa_Service_Refresh_Now(now) ? "requested" : "refused");
        }

        opa_Service_Poll(online, now);
        const bool newData = opa_Service_Copy(*viewData, dataVersion);
        if (newData){
            dirty |= OPA_DIRTY_ALL;
            selected = -1;
        }
        // The phone's status panel: on new data, and every 30 s (the engine skips identical values).
        if (newData || millis() - valuesPushMs >= 30000){
            valuesPushMs = millis();
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        if (selected < 0 || selected >= viewData->quoteCount) selected = opa_Featured_Index(*viewData);

        // A click features the next code on the watchlist.
        const uint8_t featuredSteps = featuredStepsRequested;
        if (featuredSteps > 0 && viewData->quoteCount > 0){
            featuredStepsRequested = 0;
            selected = (selected + featuredSteps) % viewData->quoteCount;
            snprintf(opaSettings.featured, sizeof(opaSettings.featured), "%s", viewData->quotes[selected].code);
            opa_Save_Settings();
            opa_Service_Set_Settings(opaSettings);      // The chart follows the featured code.
            dirty |= OPA_DIRTY_HERO | OPA_DIRTY_PAGE | OPA_DIRTY_STRIP;
        }

        // Download status: what the screen says about it.
        opa_Service_Get_Status(status);
        const uint32_t fetched = viewData->quotesFetched;
        const uint32_t expected = status.quoteInterval > 0 ? status.quoteInterval : OPA_DEMO_INTERVAL_SEC;
        const bool stale = !online || (now != 0 && fetched != 0 && now - fetched > 2 * expected + 600);
        const char* text = opa_Status_Text(status, *viewData, online, now, statusBuffer, sizeof(statusBuffer));
        if (strcmp(text ? text : "", shownStatusText) != 0){
            snprintf(shownStatusText, sizeof(shownStatusText), "%s", text ? text : "");
            dirty |= OPA_DIRTY_TICKER | OPA_DIRTY_PAGE;
        }
        statusText = text;
        if (stale != shownStale){ shownStale = stale; dirty |= OPA_DIRTY_HERO; }

        Opa_View_t view = {viewData, selected, page, opaSettings.range, opaSettings.share, stale,
                           viewData->quotesFetched == 0 ? statusText : nullptr, &status, now};

        // Pages: those with nothing to show are skipped.
        const uint8_t pageSteps = pageStepsRequested;
        const bool pageDue = opaSettings.autoCycle && millis() - pageChangeMs >= opaSettings.pageSeconds * 1000UL;
        if (pageSteps > 0 || pageDue || !opa_Ui_Page_Available(view, page)){
            pageStepsRequested = 0;
            pageChangeMs = millis();
            page = opa_Step_Page(view, page, pageSteps > 0 ? pageSteps : 1);
            view.page = page;
            dirty |= OPA_DIRTY_PAGE;
        }
        // Times on the hero and the plan page move on with the clock.
        if (now / 60 != lastMinute){
            lastMinute = now / 60;
            if (page == OPA_PAGE_PLAN) dirty |= OPA_DIRTY_PAGE;
        }

        if (wholeScreen) dirty |= OPA_DIRTY_HERO | OPA_DIRTY_PAGE | OPA_DIRTY_STRIP;
        if (dirty & (OPA_DIRTY_HERO | OPA_DIRTY_PAGE | OPA_DIRTY_STRIP)){
            mtb_Panel_Begin_Update();
            if (wholeScreen) opa_Ui_Clear();
            if (dirty & OPA_DIRTY_HERO) opa_Ui_Draw_Hero(view);
            if (dirty & OPA_DIRTY_PAGE) opa_Ui_Draw_Page(view);
            if (dirty & OPA_DIRTY_STRIP) opa_Ui_Draw_Strip(view);
            mtb_Panel_End_Update();
        }
        const bool restartTicker = (dirty & OPA_DIRTY_TICKER) != 0;
        wholeScreen = false;
        dirty = 0;

        if (restartTicker){
            ticker.mtb_Scroll_Active(STOP_SCROLL);
            tickerItem = OPA_TICK_SUMMARY;
            tickerStatusDue = true;
            nextTickerCheckMs = 0;
        }
        if ((int32_t)(millis() - nextTickerCheckMs) >= 0){
            nextTickerCheckMs = millis() + 500;
            if (!ticker.mtb_Scroll_Active() && opa_Next_Ticker_Text(*viewData, status, statusText, tickerItem, tickerStatusDue, tickerText, sizeof(tickerText), tickerColor)){
                ticker.mtb_Scroll_This_Text(tickerText, tickerColor);
            }
        }
        delay(50);
    }

    ticker.mtb_Scroll_Active(STOP_SCROLL);
    // A download in progress finishes on its own; its result is kept for the next launch.
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
// Click: next code. Double click: next page. Triple click: refresh now. Long press: page rotation on/off.
static void opa_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG:
            autoCycleToggled = true;
            break;
        case BUTTON_CLICKED:
            if (button.count == 1) featuredStepsRequested = featuredStepsRequested + 1;
            else if (button.count == 2) pageStepsRequested = pageStepsRequested + 1;
            else if (button.count == 3) refreshRequested = true;
            break;
        default:
            break;
    }
}

// The BLE handlers start from the settings in use (or ones already waiting), change what the command holds, and
// hand the result to the app task.
static void opa_Ble_Begin_Change(Opa_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    next = settingsReceived ? receivedSettings : opaSettings;
    portEXIT_CRITICAL(&requestLock);
}

static void opa_Ble_End_Change(const Opa_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
}

// What the phone's UI shows when it opens: the saved settings, or ones received and not yet applied. The API key
// itself never leaves the device; the phone is told only whether one is set. Runs on the BLE parser task.
// The status panel: plan, quota, pace, what the plan includes. Only once the service has run.
static void opa_Status_Values(JsonObject values){
    Opa_Status_t status;
    opa_Service_Get_Status(status);
    if (!status.started) return;
    const Opa_Account_t& a = status.account;
    if (status.demo){ values["plan"] = "Demo (no key)"; values["planState"] = "info"; }
    else if (a.seen && a.tier[0]){
        char plan[20];
        snprintf(plan, sizeof(plan), "%s", a.tier);
        plan[0] = (char)toupper((unsigned char)plan[0]);
        values["plan"] = plan;
        values["planState"] = strcasecmp(a.tier, "free") == 0 ? "info" : "ok";
    }
    values["paid"] = (!status.demo && a.seen && a.tier[0] && strcasecmp(a.tier, "free") != 0) ? 1 : 0;
    if (status.keyRejected){ values["keyText"] = "Not accepted by OilPriceAPI"; values["keyState"] = "error"; }
    else if (status.emailUnconfirmed){ values["keyText"] = "Confirm your email with OilPriceAPI"; values["keyState"] = "warn"; }
    else if (!status.demo && a.seen){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    if (a.seen && a.limit){
        values["left"] = a.exhausted ? 0 : a.remaining;
        values["limit"] = a.limit;
        values["window"] = a.window == OPA_QUOTA_MONTH ? "this month" : a.window == OPA_QUOTA_DAY ? "today" : a.window == OPA_QUOTA_TRIAL ? "in the trial" : "";
        values["quotaState"] = status.quotaUsed ? "error" : a.remaining * 10 < a.limit ? "warn" : "ok";
        if (a.reset) values["reset"] = a.reset;
    }
    static const char* const FEATURE[OPA_F_COUNT] = {"history", "futures curve", "rig counts", "OPEC output"};
    char open[64] = "", shut[64] = "";
    for (uint8_t f = 0; f < OPA_F_COUNT; f++){
        char* list = status.access[f] == OPA_ACCESS_OK ? open : status.access[f] == OPA_ACCESS_LOCKED ? shut : nullptr;
        if (list == nullptr) continue;
        const size_t used = strlen(list);
        snprintf(list + used, 64 - used, "%s%s", used ? ", " : "", FEATURE[f]);
    }
    if (open[0]){ open[0] = (char)toupper((unsigned char)open[0]); values["features"] = open; }
    if (shut[0]){ shut[0] = (char)toupper((unsigned char)shut[0]); values["lockedFeatures"] = shut; }
    char pace[40];
    if (status.quotaUsed){ values["pace"] = "Paused until the quota resets"; values["paceState"] = "error"; }
    else if (status.quoteInterval){
        if (status.quoteInterval < 120) snprintf(pace, sizeof(pace), "Every %u s", (unsigned)status.quoteInterval);
        else if (status.quoteInterval < 7200) snprintf(pace, sizeof(pace), "Every %u min", (unsigned)((status.quoteInterval + 30) / 60));
        else snprintf(pace, sizeof(pace), "Every %u h", (unsigned)((status.quoteInterval + 1800) / 3600));
        values["pace"] = pace;
        values["paceState"] = "ok";
    }
    if (status.quotesFetched) values["updated"] = status.quotesFetched;
}

static void opa_Mobile_Ui_Values(JsonObject values){
    Opa_Settings_t* settings = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (settings == nullptr) return;
    opa_Read_Saved_Settings(*settings);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *settings = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonArray watch = values["watch"].to<JsonArray>();
    for (uint8_t i = 0; i < settings->watchCount; i++) watch.add(settings->watch[i]);
    values["code"] = settings->featured;
    values["range"] = settings->range;
    values["share"] = settings->share;
    values["floor"] = settings->floorMin;
    values["cycle"] = settings->autoCycle ? 1 : 0;
    values["seconds"] = settings->pageSeconds;
    values["api_key_set"] = settings->apiKey[0] != '\0' ? 1 : 0;
    heap_caps_free(settings);
    opa_Status_Values(values);
}

// A number the phone may send as 30 or as 30.0 (sliders). Returns false if the field is missing or not a number.
static bool opa_Ble_Number(JsonDocument& command, const char* key, long& value){
    JsonVariant field = command[key];
    if (field.is<bool>()) value = field.as<bool>() ? 1 : 0;
    else if (field.is<long>()) value = field.as<long>();
    else if (field.is<double>()) value = lround(field.as<double>());
    else return false;
    return true;
}

// Command 0: {"code": "BRENT_CRUDE_USD"}. A code not on the watchlist is added to it, if there is room.
static void opa_Ble_Feature(JsonDocument& command){
    const Opa_Commodity_t* known = opa_Find_Commodity(command["code"].as<const char*>());
    if (known == nullptr){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    bool listed = false;
    for (uint8_t i = 0; i < next->watchCount; i++) if (strcmp(next->watch[i], known->code) == 0) listed = true;
    if (!listed && next->watchCount >= OPA_MAX_WATCH){
        heap_caps_free(next);
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    if (!listed) snprintf(next->watch[next->watchCount++], OPA_CODE_LEN, "%s", known->code);
    snprintf(next->featured, sizeof(next->featured), "%s", known->code);
    opa_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 1: {"watch": ["BRENT_CRUDE_USD", "WTI_USD"]}: 1 to 12 known codes. Older phone apps send the list as the
// text "[BRENT_CRUDE_USD, WTI_USD]", which is read too.
static void opa_Ble_Set_Watchlist(JsonDocument& command){
    char codes[OPA_MAX_WATCH][OPA_CODE_LEN];
    int count = 0;
    bool valid = true;
    auto add = [&](const char* code, size_t length){
        char name[OPA_CODE_LEN];
        if (length == 0) return;
        if (length >= sizeof(name) || count >= OPA_MAX_WATCH){ valid = false; return; }
        memcpy(name, code, length);
        name[length] = '\0';
        const Opa_Commodity_t* known = opa_Find_Commodity(name);
        if (known == nullptr){ valid = false; return; }
        for (int k = 0; k < count; k++) if (strcmp(codes[k], known->code) == 0) return;
        snprintf(codes[count++], OPA_CODE_LEN, "%s", known->code);
    };
    JsonVariant watch = command["watch"];
    if (watch.is<JsonArray>()){
        for (JsonVariant item : watch.as<JsonArray>()){
            const char* code = item.as<const char*>();
            if (code == nullptr){ valid = false; break; }
            add(code, strlen(code));
        }
    } else if (watch.is<const char*>()){
        const char* text = watch.as<const char*>();
        const char* start = nullptr;
        for (const char* c = text; ; c++){
            const bool separator = *c == '\0' || *c == ',' || *c == '[' || *c == ']' || *c == ' ' || *c == '"';
            if (!separator && start == nullptr) start = c;
            if (separator && start != nullptr){ add(start, (size_t)(c - start)); start = nullptr; }
            if (*c == '\0') break;
        }
    } else valid = false;
    if (!valid || count == 0){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    next->watchCount = (uint8_t)count;
    memcpy(next->watch, codes, sizeof(codes));
    opa_Ble_End_Change(*next);      // A featured code taken off the list gives way to the first one (opa_Settings_Clamp).
    heap_caps_free(next);
}

// Command 2: {"range": 2}: 0 24 hours, 1 7 days, 2 30 days, 3 1 year.
static void opa_Ble_Set_Range(JsonDocument& command){
    long range;
    if (!opa_Ble_Number(command, "range", range) || range < 0 || range >= OPA_RANGE_COUNT){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    next->range = (uint8_t)range;
    opa_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 3: {"api_key": "..."}. The key goes in a request header, so it must be printable with no spaces or quotes.
static void opa_Ble_Set_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length >= 8 && length < OPA_KEY_LEN;
    for (size_t i = 0; valid && i < length; i++){
        const char c = key[i];
        if (c <= ' ' || c > '~' || c == '"' || c == '\\') valid = false;
    }
    if (!valid){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    memcpy(next->apiKey, key, length + 1);
    opa_Ble_End_Change(*next);
    memset(next, 0, sizeof(Opa_Settings_t));       // No copy of the key left behind.
    heap_caps_free(next);
}

// Command 4: {"share": 90} and/or {"floor": 5}, within the limits in opa_Config.h.
static void opa_Ble_Set_Pace(JsonDocument& command){
    long share, floor;
    const bool hasShare = opa_Ble_Number(command, "share", share);
    const bool hasFloor = opa_Ble_Number(command, "floor", floor);
    if ((!hasShare && !hasFloor)
        || (hasShare && (share < OPA_SHARE_LOWEST || share > OPA_SHARE_HIGHEST))
        || (hasFloor && (floor < OPA_FLOOR_MIN_LOWEST || floor > OPA_FLOOR_MIN_HIGHEST))){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    if (hasShare) next->share = (uint8_t)share;
    if (hasFloor) next->floorMin = (uint8_t)floor;
    opa_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 5: {"cycle": 1} and/or {"seconds": 10}
static void opa_Ble_Set_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = opa_Ble_Number(command, "cycle", cycle);
    const bool hasSeconds = opa_Ble_Number(command, "seconds", seconds);
    if ((!hasCycle && !hasSeconds) || (hasSeconds && (seconds < OPA_PAGE_SEC_LOWEST || seconds > OPA_PAGE_SEC_HIGHEST))){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Opa_Settings_t* next = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    opa_Ble_Begin_Change(*next);
    if (hasCycle) next->autoCycle = cycle != 0;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    opa_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 6: {} - refresh now, at most once every OPA_MANUAL_REFRESH_GAP_SEC, and not with the quota used up.
static void opa_Ble_Refresh_Now(JsonDocument&){
    if (!opa_Service_Refresh_Now(opa_Now())) mtb_Ble_App_Cmd_Reject();
}
