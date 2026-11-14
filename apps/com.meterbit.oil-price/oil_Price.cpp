/**
 * Oil price app, ID {4,4}: crude oil, fuel and natural gas benchmarks, crude output and energy headlines.
 *
 * This file holds the app task: input, page rotation, the ticker and the settings. The data comes from
 * oil_Service.cpp, which downloads it in a task of its own through a provider (oil_Provider.h), and the
 * screen is drawn by oil_Ui.cpp.
 */
#include "mtb_engine.h"
#include "mtb_text_scroll.h"
#include <time.h>
#include <ctype.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "oil_Config.h"
#include "oil_Service.h"
#include "oil_Ui.h"
#include "oil_Mobile_Ui.h"

static const char TAG[] = "OIL_PRICE";

#define OIL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define OIL_DIRTY_HERO          0x01
#define OIL_DIRTY_PAGE          0x02
#define OIL_DIRTY_STRIP         0x04
#define OIL_DIRTY_TICKER        0x08
#define OIL_DIRTY_ALL           (OIL_DIRTY_HERO | OIL_DIRTY_PAGE | OIL_DIRTY_STRIP | OIL_DIRTY_TICKER)
#define OIL_CLOCK_VALID_AFTER   1700000000UL
#define OIL_TICKER_LEN          360

EXT_RAM_BSS_ATTR TaskHandle_t oilPrice_Task_H = NULL;
void oilPrice_App_Task(void*);
static void oil_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar* oilPrice_App;
Mtb_Applications* oilPrice_App_GetInstance(){
    if (!oilPrice_App){
        oilPrice_App = new Mtb_Applications_StatusBar(oilPrice_App_Task, &oilPrice_Task_H, "Oil price", {4,4}, 8192);
        oilPrice_App->mtb_App_Set_Mobile_UI(OIL_UI_MANIFEST, OIL_UI_API, OIL_UI_DATA, oil_Mobile_Ui_Values);
    }
    return oilPrice_App;
}
MTB_REGISTER_APP(oilPrice_App, 4, 4)

EXT_RAM_BSS_ATTR static Oil_Settings_t oilSettings;
static Oil_Data_t* viewData = nullptr;      // PSRAM, kept for the power-on session: the copy the screen draws.

// Requests from the button and BLE tasks. The app task acts on them, so only it touches the settings and the view.
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t seriesStepsRequested = 0;
static volatile uint8_t pageStepsRequested = 0;
static volatile bool autoCycleToggled = false;
static volatile bool refreshRequested = false;
static volatile bool settingsReceived = false;
static volatile bool seriesRequested = false;
static char requestedCode[OIL_CODE_LEN];
static Oil_Settings_t receivedSettings;     // Written by BLE handlers under requestLock.

static void oil_Button(button_event_t button);
static void oil_Ble_Show_Series(JsonDocument& command);
static void oil_Ble_Set_Refresh(JsonDocument& command);
static void oil_Ble_Set_Pages(JsonDocument& command);
static void oil_Ble_Set_Key(JsonDocument& command);
static void oil_Ble_Refresh_Now(JsonDocument& command);

//##############################################################################################################
static uint32_t oil_Now(void){
    const time_t now = time(nullptr);
    return (now > (time_t)OIL_CLOCK_VALID_AFTER) ? (uint32_t)now : 0;
}

// The settings as saved, with defaults for anything never set. Returns false if NVS held none.
static bool oil_Read_Saved_Settings(Oil_Settings_t& settings){
    Oil_Settings_t defaults;
    oil_Settings_Default(defaults);
    settings = defaults;
    mtb_Read_Nvs_Struct(OIL_NVS_KEY, &settings, sizeof(Oil_Settings_t));
    const bool saved = settings.magic == OIL_SETTINGS_MAGIC;
    if (!saved) settings = defaults;
    // A key built into the firmware fills in for one never set from the phone.
    if (settings.apiKey[0] == '\0') memcpy(settings.apiKey, defaults.apiKey, sizeof(settings.apiKey));
    oil_Settings_Clamp(settings);
    return saved;
}

static void oil_Load_Settings(void){
    if (!oil_Read_Saved_Settings(oilSettings)) mtb_Write_Nvs_Struct(OIL_NVS_KEY, &oilSettings, sizeof(Oil_Settings_t));
}

static void oil_Save_Settings(void){
    mtb_Write_Nvs_Struct(OIL_NVS_KEY, &oilSettings, sizeof(Oil_Settings_t));
}

// The next or previous daily benchmark after @p from; -1 if there are none.
static int oil_Step_Series(const Oil_Data_t& data, int from, int steps){
    int found = from;
    for (int s = 0; s < steps; s++){
        for (int k = 1; k <= data.seriesCount; k++){
            const int candidate = (found + k + data.seriesCount) % data.seriesCount;
            if (data.series[candidate].kind == OIL_KIND_PRICE){ found = candidate; break; }
        }
    }
    return (found >= 0 && found < data.seriesCount) ? found : -1;
}

static int oil_Selected_Index(const Oil_Data_t& data){
    const int index = oil_Find_Series(data, oilSettings.selected);
    return index >= 0 ? index : oil_Step_Series(data, -1, 1);
}

// The newest retrieval of a daily price, or 0.
static uint32_t oil_Prices_Fetched(const Oil_Data_t& data){
    uint32_t newest = 0;
    for (int i = 0; i < data.seriesCount; i++){
        if (data.series[i].kind == OIL_KIND_PRICE && data.series[i].fetched > newest) newest = data.series[i].fetched;
    }
    return newest;
}

// What to say instead of a page that has nothing to show, and in the ticker while there is a problem.
static const char* oil_Status_Text(const Oil_Status_t& status, bool online, uint32_t now, bool haveData){
    if (status.keyRejected) return "EIA rejected the API key. Check it at eia.gov/opendata and send it again from the Pixlpal app.";
    if (status.keyMissing) return "Add a free EIA API key: register at eia.gov/opendata, then send it from the Pixlpal app.";
    if (!online) return haveData ? "Offline. Showing the last prices received." : "No internet connection. Prices appear once connected.";
    if (now == 0) return "Waiting for the clock to be set...";
    if (status.rateLimited) return "EIA asked for fewer requests. Showing the last prices received.";
    if (status.lowMemory) return "Low memory: the download will be retried shortly.";
    if (status.failing) return haveData ? "Cannot reach EIA. Showing the last prices received." : "Cannot reach EIA. Retrying...";
    if (!haveData) return "Downloading prices from the U.S. EIA...";
    return nullptr;
}

//##############################################################################################################
enum Oil_Ticker_Item_t : uint8_t { OIL_TICK_SUMMARY = 0, OIL_TICK_HEADLINE, OIL_TICK_SOURCE, OIL_TICK_HEADLINE_2, OIL_TICK_COUNT };

static const uint16_t T_SUMMARY = OIL_RGB(255, 176, 0);
static const uint16_t T_HEADLINE = OIL_RGB(235, 235, 235);
static const uint16_t T_SOURCE = OIL_RGB(130, 130, 130);
static const uint16_t T_STATUS = OIL_RGB(255, 120, 40);

// Builds the next ticker message: a status message, if any, at the start of every round. Returns false if there
// is nothing to say.
static bool oil_Next_Ticker_Text(const Oil_Data_t& data, const char* statusText, uint8_t& item, bool& statusDue, uint8_t& headline, char* dest, size_t size, uint16_t& color){
    if (statusText != nullptr && statusDue){
        statusDue = false;
        snprintf(dest, size, "%s", statusText);
        color = T_STATUS;
        return true;
    }
    for (int attempt = 0; attempt < OIL_TICK_COUNT; attempt++){
        const uint8_t current = item;
        item = (item + 1) % OIL_TICK_COUNT;
        if (item == OIL_TICK_SUMMARY) statusDue = true;
        if (current == OIL_TICK_SUMMARY){
            size_t used = 0;
            int32_t newest = 0;
            for (int i = 0; i < data.seriesCount && used + 40 < size; i++){
                const Oil_Series_t& series = data.series[i];
                const Oil_Point_t* last = oil_Series_Last(series);
                if (series.kind != OIL_KIND_PRICE || last == nullptr) continue;
                char value[16], percentText[16] = "";
                float change, percent;
                oil_Ui_Format_Value(value, sizeof(value), last->value, series.unit);
                if (oil_Series_Change(series, 0, change, percent)) oil_Ui_Format_Percent(percentText, sizeof(percentText), percent);
                used += snprintf(dest + used, size - used, "%s%s %s %s", used ? "   " : "", series.code, value, percentText);
                if (last->day > newest) newest = last->day;
            }
            if (used == 0) continue;
            char date[16];
            oil_Format_Short_Date(date, sizeof(date), newest, false);
            snprintf(dest + used, size - used, "   (%s spot, %s)", data.sourceName, date);
            color = T_SUMMARY;
            return true;
        }
        if (current == OIL_TICK_HEADLINE || current == OIL_TICK_HEADLINE_2){
            if (data.headlineCount == 0) continue;
            const Oil_Headline_t& h = data.headlines[headline++ % data.headlineCount];
            char date[16] = "";
            if (h.day != 0) oil_Format_Short_Date(date, sizeof(date), h.day, false);
            snprintf(dest, size, "%s%s%s: %s", data.newsSource, date[0] ? ", " : "", date, h.title);
            color = T_HEADLINE;
            return true;
        }
        // Attribution, with the date of the data and when it was retrieved.
        const uint32_t fetched = oil_Prices_Fetched(data);
        if (fetched == 0) continue;
        const time_t fetchedTime = (time_t)fetched;
        struct tm local;
        localtime_r(&fetchedTime, &local);
        char retrieved[32], month[16];
        strftime(retrieved, sizeof(retrieved), "%d %b %H:%M", &local);
        strftime(month, sizeof(month), "%b %Y", &local);
        snprintf(dest, size, "Source: %s (%s), eia.gov. Retrieved %s. Daily data published by EIA, not live quotes. Not investment advice.",
                 data.sourceFull, month, retrieved);
        color = T_SOURCE;
        return true;
    }
    return false;
}

//##############################################################################################################
void oilPrice_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(oil_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(oil_Ble_Show_Series, oil_Ble_Set_Refresh, oil_Ble_Set_Pages, oil_Ble_Set_Key, oil_Ble_Refresh_Now);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    oil_Load_Settings();
    if (viewData == nullptr) viewData = (Oil_Data_t*)heap_caps_calloc(1, sizeof(Oil_Data_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr){
        ESP_LOGE(TAG, "Out of memory");
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    oil_Service_Begin(oil_Provider_Eia(), oilSettings);

    seriesStepsRequested = 0;
    pageStepsRequested = 0;
    autoCycleToggled = false;
    refreshRequested = false;
    settingsReceived = false;
    seriesRequested = false;

    Mtb_ScrollText_t ticker(0, OIL_TICKER_Y, PANEL_RES_X, Terminal6x8, T_SUMMARY, 20, 1);
    char tickerText[OIL_TICKER_LEN];
    uint16_t tickerColor = T_SUMMARY;
    uint8_t tickerItem = OIL_TICK_SUMMARY, tickerHeadline = 0;
    bool tickerStatusDue = true;
    uint32_t nextTickerCheckMs = 0;

    uint32_t dataVersion = 0;
    Oil_Status_t status = {}, shownStatus = {};
    bool shownOnline = false, shownStale = false;
    const char* statusText = nullptr;
    uint8_t page = OIL_PAGE_TREND, newsFirst = 0;
    uint32_t pageChangeMs = millis();
    uint8_t dirty = OIL_DIRTY_ALL;
    bool wholeScreen = true;
    int selected = -1;

    for (;;){
        const uint32_t beforeCheckpoint = millis();
        if (THIS_APP_IS_ACTIVE != pdTRUE) break;
        // The checkpoint only takes this long if the app was paused, and then the screen has been used by something else.
        if (millis() - beforeCheckpoint > 300) wholeScreen = true;

        const uint32_t now = oil_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;

        // Requests from other tasks.
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            oilSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            oil_Settings_Clamp(oilSettings);
            oil_Save_Settings();
            oil_Service_Set_Settings(oilSettings);
            pageChangeMs = millis();
            dirty |= OIL_DIRTY_ALL;
        }
        if (autoCycleToggled){
            autoCycleToggled = false;
            oilSettings.autoCycle = !oilSettings.autoCycle;
            oil_Save_Settings();
            pageChangeMs = millis();
        }
        if (refreshRequested){
            refreshRequested = false;
            ESP_LOGI(TAG, "Refresh %s", oil_Service_Refresh_Now(now) ? "requested" : "refused: asked too recently");
        }

        oil_Service_Poll(online, now);
        if (oil_Service_Copy(*viewData, dataVersion)){
            dirty |= OIL_DIRTY_ALL;
            selected = -1;
        }
        if (selected < 0 || selected >= viewData->seriesCount) selected = oil_Selected_Index(*viewData);

        const uint8_t seriesSteps = seriesStepsRequested;
        if (seriesRequested || seriesSteps > 0){
            int next = selected;
            if (seriesRequested){
                portENTER_CRITICAL(&requestLock);
                char code[OIL_CODE_LEN];
                memcpy(code, requestedCode, sizeof(code));
                seriesRequested = false;
                portEXIT_CRITICAL(&requestLock);
                const int found = oil_Find_Series(*viewData, code);
                if (found >= 0) next = found;
            } else {
                seriesStepsRequested = 0;
                next = oil_Step_Series(*viewData, selected, seriesSteps);
            }
            if (next >= 0 && next != selected){
                selected = next;
                snprintf(oilSettings.selected, sizeof(oilSettings.selected), "%s", viewData->series[selected].code);
                oil_Save_Settings();    // Only a choice made by the user is stored.
                dirty |= OIL_DIRTY_HERO | OIL_DIRTY_PAGE | OIL_DIRTY_STRIP;
            }
        }

        // Pages.
        const uint8_t pageSteps = pageStepsRequested;
        const bool pageDue = oilSettings.autoCycle && millis() - pageChangeMs >= oilSettings.pageSeconds * 1000UL;
        if (pageSteps > 0 || pageDue){
            pageStepsRequested = 0;
            pageChangeMs = millis();
            page = (page + (pageSteps > 0 ? pageSteps : 1)) % OIL_PAGE_COUNT;
            if (page == OIL_PAGE_NEWS && viewData->headlineCount > 2) newsFirst = (newsFirst + 2) % viewData->headlineCount;
            dirty |= OIL_DIRTY_PAGE;
        }

        // Download status: what the screen says about it.
        oil_Service_Get_Status(status);
        const uint32_t fetched = oil_Prices_Fetched(*viewData);
        const bool stale = !online || (now != 0 && fetched != 0 && now - fetched > 2UL * 60 * oilSettings.priceRefreshMin + 3600);
        if (memcmp(&status, &shownStatus, sizeof(status)) != 0 || online != shownOnline || stale != shownStale){
            const char* text = oil_Status_Text(status, online, now, fetched != 0);
            if (text != statusText) dirty |= OIL_DIRTY_TICKER | OIL_DIRTY_PAGE;
            if (stale != shownStale) dirty |= OIL_DIRTY_HERO;
            statusText = text;
            shownStatus = status;
            shownOnline = online;
            shownStale = stale;
        }

        if (wholeScreen) dirty |= OIL_DIRTY_HERO | OIL_DIRTY_PAGE | OIL_DIRTY_STRIP;
        if (dirty & (OIL_DIRTY_HERO | OIL_DIRTY_PAGE | OIL_DIRTY_STRIP)){
            const Oil_View_t view = {viewData, selected, page, newsFirst, stale, statusText};
            mtb_Panel_Begin_Update();
            if (wholeScreen) oil_Ui_Clear();
            if (dirty & OIL_DIRTY_HERO) oil_Ui_Draw_Hero(view);
            if (dirty & OIL_DIRTY_PAGE) oil_Ui_Draw_Page(view);
            if (dirty & OIL_DIRTY_STRIP) oil_Ui_Draw_Strip(view);
            mtb_Panel_End_Update();
        }
        const bool restartTicker = (dirty & OIL_DIRTY_TICKER) != 0;
        wholeScreen = false;
        dirty = 0;

        if (restartTicker){
            ticker.mtb_Scroll_Active(STOP_SCROLL);
            tickerItem = OIL_TICK_SUMMARY;
            tickerStatusDue = true;
            nextTickerCheckMs = 0;
        }
        if ((int32_t)(millis() - nextTickerCheckMs) >= 0){
            nextTickerCheckMs = millis() + 500;
            if (!ticker.mtb_Scroll_Active() && oil_Next_Ticker_Text(*viewData, statusText, tickerItem, tickerStatusDue, tickerHeadline, tickerText, sizeof(tickerText), tickerColor)){
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
// Click: next benchmark. Double click: next page. Triple click: refresh now. Long press: page rotation on/off.
static void oil_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG:
            autoCycleToggled = true;
            break;
        case BUTTON_CLICKED:
            if (button.count == 1) seriesStepsRequested = seriesStepsRequested + 1;
            else if (button.count == 2) pageStepsRequested = pageStepsRequested + 1;
            else if (button.count == 3) refreshRequested = true;
            break;
        default:
            break;
    }
}

// The BLE handlers start from the settings in use (or ones already waiting), change what the command holds, and
// hand the result to the app task.
static void oil_Ble_Begin_Change(Oil_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    next = settingsReceived ? receivedSettings : oilSettings;
    portEXIT_CRITICAL(&requestLock);
}

static void oil_Ble_End_Change(const Oil_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
}

// What the phone's UI shows when it opens: the saved settings, or ones received and not yet applied. The API key
// itself never leaves the device; the phone is told only whether one is set. Runs on the BLE parser task.
static void oil_Mobile_Ui_Values(JsonObject values){
    Oil_Settings_t settings;
    oil_Read_Saved_Settings(settings);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) settings = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    values["code"] = settings.selected;
    values["priceMinutes"] = settings.priceRefreshMin;
    values["newsMinutes"] = settings.newsRefreshMin;
    values["cycle"] = settings.autoCycle ? 1 : 0;
    values["seconds"] = settings.pageSeconds;
    values["api_key_set"] = settings.apiKey[0] != '\0' ? 1 : 0;
}

// A number the phone may send as 30 or as 30.0 (sliders). Returns false if the field is missing or not a number.
static bool oil_Ble_Number(JsonDocument& command, const char* key, long& value){
    JsonVariant field = command[key];
    if (field.is<bool>()) value = field.as<bool>() ? 1 : 0;
    else if (field.is<long>()) value = field.as<long>();
    else if (field.is<double>()) value = lround(field.as<double>());
    else return false;
    return true;
}

// Command 0: {"code": "WTI"}
static void oil_Ble_Show_Series(JsonDocument& command){
    const char* code = command["code"];
    if (code == nullptr || strlen(code) >= OIL_CODE_LEN || !oil_Service_Has_Series(code)){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    portENTER_CRITICAL(&requestLock);
    snprintf(requestedCode, sizeof(requestedCode), "%s", code);
    seriesRequested = true;
    portEXIT_CRITICAL(&requestLock);
}

// Command 1: {"priceMinutes": 180} and/or {"newsMinutes": 360}, within the limits in oil_Config.h.
static void oil_Ble_Set_Refresh(JsonDocument& command){
    long price, news;
    const bool hasPrice = oil_Ble_Number(command, "priceMinutes", price);
    const bool hasNews = oil_Ble_Number(command, "newsMinutes", news);
    if ((!hasPrice && !hasNews)
        || (hasPrice && (price < OIL_PRICE_REFRESH_MIN_LOWEST || price > OIL_PRICE_REFRESH_MIN_HIGHEST))
        || (hasNews && (news < OIL_NEWS_REFRESH_MIN_LOWEST || news > OIL_NEWS_REFRESH_MIN_HIGHEST))){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Oil_Settings_t next;
    oil_Ble_Begin_Change(next);
    if (hasPrice) next.priceRefreshMin = (uint16_t)price;
    if (hasNews) next.newsRefreshMin = (uint16_t)news;
    oil_Ble_End_Change(next);
}

// Command 2: {"cycle": 1} and/or {"seconds": 10}
static void oil_Ble_Set_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = oil_Ble_Number(command, "cycle", cycle);
    const bool hasSeconds = oil_Ble_Number(command, "seconds", seconds);
    if ((!hasCycle && !hasSeconds) || (hasSeconds && (seconds < OIL_PAGE_SEC_LOWEST || seconds > OIL_PAGE_SEC_HIGHEST))){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Oil_Settings_t next;
    oil_Ble_Begin_Change(next);
    if (hasCycle) next.autoCycle = cycle != 0;
    if (hasSeconds) next.pageSeconds = (uint8_t)seconds;
    oil_Ble_End_Change(next);
}

// Command 3: {"api_key": "..."}. Keys are letters and digits; anything else is refused, since the key goes in a URL.
static void oil_Ble_Set_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length > 0 && length < OIL_KEY_LEN;
    for (size_t i = 0; valid && i < length; i++) if (!isalnum((unsigned char)key[i])) valid = false;
    if (!valid){
        mtb_Ble_App_Cmd_Reject();
        return;
    }
    Oil_Settings_t next;
    oil_Ble_Begin_Change(next);
    memcpy(next.apiKey, key, length + 1);
    oil_Ble_End_Change(next);
}

// Command 4: {} - refresh now, at most once every OIL_MANUAL_REFRESH_GAP_SEC. Refused when asked too soon.
static void oil_Ble_Refresh_Now(JsonDocument&){
    if (!oil_Service_Refresh_Now(oil_Now())) mtb_Ble_App_Cmd_Reject();
}
