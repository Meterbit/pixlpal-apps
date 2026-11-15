/**
 * RSS Reader app, ID {1,1}: headlines from up to seven RSS or Atom feeds, picked on the phone from a catalogue of
 * checked feeds (BBC, Al Jazeera, NPR, the Guardian, tech and Nigerian news...) or added by address. One story at a
 * time as large as it fits, then its summary, or a list. New stories are marked and shown first; old stories and
 * stories about muted words can be kept off the screen.
 *
 * No key and no plan: feeds are free to read. The shared news engine (mtb_kit_news.h) reads each feed on the chosen
 * schedule and keeps the stories in flash; rss_Model.cpp reads the feeds, rss_Provider.cpp hands them to the engine.
 */
#include "mtb_engine.h"
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_news.h"
#include "rss_Model.h"
#include "rss_Provider.h"
#include "rss_Mobile_Ui.h"

#define RSS_CLOCK_VALID_AFTER   1700000000UL
#define RSS_KNOB_HOLD_MS        30000       // Turning the knob holds the story this long.

EXT_RAM_BSS_ATTR TaskHandle_t rssNewsApp_Task_H = NULL;
void rssNewsApp_Task(void *dApplication);
static void rss_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *rssNewsApp;
Mtb_Applications* rssNewsApp_GetInstance() {
    if (!rssNewsApp){
        rssNewsApp = new Mtb_Applications_StatusBar(rssNewsApp_Task, &rssNewsApp_Task_H, "RSS Reader", {1,1}, 8192);
        rssNewsApp->mtb_App_Set_Mobile_UI(RSS_UI_MANIFEST, RSS_UI_API, nullptr, rss_Mobile_Ui_Values);
    }
    return rssNewsApp;
}
MTB_REGISTER_APP(rssNewsApp, 1, 1)

EXT_RAM_BSS_ATTR static Rss_Settings_t rssSettings;
EXT_RAM_BSS_ATTR static Mtb_Kit_News_t rssEngine;
static Mtb_Kit_News_Data_t* viewData = nullptr;         // PSRAM

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile int8_t turnsRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Rss_Settings_t receivedSettings;

static void rss_Button(button_event_t button);
static void rss_Knob(rotary_encoder_rotation_t direction);
static void rss_Ble_Search(JsonDocument& command);
static void rss_Ble_Feeds(JsonDocument& command);
static void rss_Ble_View(JsonDocument& command);
static void rss_Ble_Pages(JsonDocument& command);
static void rss_Ble_Summaries(JsonDocument& command);
static void rss_Ble_Max_Age(JsonDocument& command);
static void rss_Ble_Muted(JsonDocument& command);
static void rss_Ble_Refresh_Minutes(JsonDocument& command);
static void rss_Ble_Refresh(JsonDocument& command);

//##############################################################################################################
static uint32_t rss_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)RSS_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool rss_Read_Saved_Settings(Rss_Settings_t& settings){
    rss_Settings_Default(settings);
    Rss_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(RSS_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == RSS_SETTINGS_MAGIC;
    if (ok) settings = saved;
    rss_Settings_Clamp(settings);
    return ok;
}

static void rss_Save_Settings(void){ mtb_Write_Nvs_Struct(RSS_NVS_KEY, &rssSettings, sizeof(Rss_Settings_t)); }

static void rss_Apply(Mtb_Kit_News_Config_t& config, bool begin){
    rss_Provider_Set_Feeds(rssSettings);
    rss_Kit_Config(rssSettings, config);
    if (begin) mtb_Kit_News_Begin(rssEngine, rss_Provider(), config);
    else mtb_Kit_News_Set_Config(rssEngine, config);
}

// The stories to show: the engine's order, less those too old or muted.
static int rss_Order(const Mtb_Kit_News_Data_t& data, uint8_t* order, uint32_t now){
    const int all = mtb_Kit_News_Order(data, order, KIT_NEWS_STORIES);
    int kept = 0;
    for (int i = 0; i < all; i++) if (rss_Visible(data.stories[order[i]], rssSettings, now)) order[kept++] = order[i];
    return kept;
}

static bool rss_Message(const Mtb_Kit_News_Status_t& status, bool online, uint32_t now, int stories, int hidden, const char*& title, const char*& detail){
    if (rssSettings.feedCount == 0){ title = "PICK SOME FEEDS"; detail = "Choose feeds for the RSS Reader in the Pixlpal app."; return true; }
    if (stories > 0) return false;
    if (hidden > 0){ title = "ALL FILTERED OUT"; detail = "Every story is older than your limit or has a muted word. Change the filters in the Pixlpal app."; return true; }
    if (!online){ title = "OFFLINE"; detail = "Headlines appear once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "Headlines appear once Pixlpal knows the time."; return true; }
    if (status.failing && !status.busy){ title = "FEEDS NOT ANSWERING"; detail = "None of your feeds could be read. Pixlpal will try again; check the addresses in the Pixlpal app."; return true; }
    title = "READING FEEDS";
    detail = "Fetching your feeds...";
    return true;
}

//##############################################################################################################
void rssNewsApp_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(rss_Button, rss_Knob);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(rss_Ble_Search, rss_Ble_Feeds, rss_Ble_View, rss_Ble_Pages, rss_Ble_Summaries, rss_Ble_Max_Age,
                                       rss_Ble_Muted, rss_Ble_Refresh_Minutes, rss_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!rss_Read_Saved_Settings(rssSettings)) rss_Save_Settings();
    if (viewData == nullptr) viewData = (Mtb_Kit_News_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
    uint8_t* order = (uint8_t*)heap_caps_calloc(KIT_NEWS_STORIES, 1, MALLOC_CAP_SPIRAM);
    Mtb_Kit_News_Config_t* config = (Mtb_Kit_News_Config_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Config_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr || order == nullptr || config == nullptr){ heap_caps_free(order); heap_caps_free(config); mtb_Delete_This_App(THIS_APP); return; }
    rss_Apply(*config, true);
    clicksRequested = 0;
    turnsRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, storyStartMs = millis(), pushMs = 0, drawnMs = 0, heldMs = 0, filterMinute = 0;
    uint32_t currentHash = 0, announcedHash = 0;
    int current = 0, orderCount = 0, hidden = 0;
    bool details = false, paused = false, redraw = true, reorder = false;
    Mtb_Kit_News_Status_t status = {};

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = rss_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            rssSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            rss_Settings_Clamp(rssSettings);
            rss_Save_Settings();
            rss_Apply(*config, false);
            reorder = true;
        }
        mtb_Kit_News_Poll(rssEngine, online, now);
        const bool fresh = mtb_Kit_News_Copy(rssEngine, *viewData, version);
        // The age filter changes with time: look again each minute.
        if (fresh || reorder || (rssSettings.maxAgeHours && now / 60 != filterMinute)){
            filterMinute = now / 60;
            orderCount = rss_Order(*viewData, order, now);
            hidden = viewData->storyCount - orderCount;
            int found = -1, first = -1;
            for (int i = 0; i < orderCount; i++){
                if (viewData->stories[order[i]].hash == currentHash) found = i;
                if (first < 0 && viewData->stories[order[i]].fresh) first = i;
            }
            const bool announce = fresh && first >= 0 && !paused && viewData->stories[order[first]].hash != announcedHash;
            current = announce ? first : found >= 0 ? found : 0;
            if (announce){ announcedHash = viewData->stories[order[first]].hash; storyStartMs = millis(); details = false; }
            if (fresh || reorder) THIS_APP->mtb_App_Push_Mobile_Values();
            reorder = false;
            redraw = true;
        }
        mtb_Kit_News_Get_Status(rssEngine, status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: turn, through the stories; click, next; double click, card or list; triple, refresh; long press, pause.
        const int8_t turns = turnsRequested;
        if (turns){
            turnsRequested = 0;
            if (orderCount) current = ((current + turns) % orderCount + orderCount) % orderCount;
            details = false;
            heldMs = millis();
            storyStartMs = millis();
            redraw = true;
        }
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; paused = !paused; }
            else if (clicks == 1 && orderCount){ current = (current + 1) % orderCount; details = false; }
            else if (clicks == 2){ rssSettings.view = rssSettings.view == RSS_VIEW_CARD ? RSS_VIEW_LIST : RSS_VIEW_CARD; rss_Save_Settings(); }
            else if (clicks == 3) mtb_Kit_News_Refresh(rssEngine, now);
            storyStartMs = millis();
            redraw = true;
        }
        const bool held = heldMs && millis() - heldMs < RSS_KNOB_HOLD_MS;
        if (!held) heldMs = 0;
        const uint32_t storyMs = rssSettings.storySeconds * 1000UL;
        const bool moving = !paused && !held && rssSettings.autoCycle && orderCount > 0;
        // The card shows the headline, then (with summaries on) the summary for the second half of its time.
        if (moving && rssSettings.view == RSS_VIEW_CARD && rssSettings.summaries && !details && millis() - storyStartMs >= storyMs / 2
            && viewData->stories[order[current % orderCount]].summary[0]){
            details = true;
            redraw = true;
        }
        if (moving && orderCount > 1 && millis() - storyStartMs >= storyMs){
            current = (current + 1) % orderCount;
            details = false;
            storyStartMs = millis();
            redraw = true;
        }
        if (orderCount) currentHash = viewData->stories[order[current % orderCount]].hash;
        if (!redraw && rssSettings.view == RSS_VIEW_CARD && moving && millis() - drawnMs >= 500) redraw = true;

        if (redraw){
            redraw = false;
            drawnMs = millis();
            const char *title, *detail;
            mtb_Panel_Begin_Update();
            if (rss_Message(status, online, now, orderCount, hidden, title, detail)) mtb_Kit_News_Draw_Message(title, detail, "RSS");
            else {
                Mtb_Kit_News_View_t view = {};
                view.data = viewData;
                view.config = config;
                view.order = order;
                view.orderCount = orderCount;
                view.current = current;
                view.now = now;
                view.progress = moving ? (float)(millis() - storyStartMs) / storyMs : 0;
                view.details = details;
                view.paused = paused;
                view.credit = "RSS";
                if (rssSettings.view == RSS_VIEW_LIST) mtb_Kit_News_Draw_List(view);
                else mtb_Kit_News_Draw_Card(view);
            }
            mtb_Panel_End_Update();
        }
        delay(50);
    }
    heap_caps_free(order);
    heap_caps_free(config);
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void rss_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void rss_Knob(rotary_encoder_rotation_t direction){
    const int8_t turns = turnsRequested;
    if (direction == ROT_CLOCKWISE && turns < 20) turnsRequested = turns + 1;
    else if (direction == ROT_COUNTERCLOCKWISE && turns > -20) turnsRequested = turns - 1;
}

static Rss_Settings_t* rss_Begin_Change(void){
    Rss_Settings_t* next = (Rss_Settings_t*)heap_caps_malloc(sizeof(Rss_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : rssSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void rss_End_Change(Rss_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static void rss_Status_Values(JsonObject values, const Rss_Settings_t& s){
    Mtb_Kit_News_Status_t status;
    mtb_Kit_News_Get_Status(rssEngine, status);
    if (!status.started || viewData == nullptr) return;
    // Each feed's last result; the engine's copy is the screen's (only while the app runs).
    Mtb_Kit_News_Data_t* data = (Mtb_Kit_News_Data_t*)heap_caps_malloc(sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
    if (data == nullptr) return;
    uint32_t version = 0;
    mtb_Kit_News_Copy(rssEngine, *data, version);
    int read = 0, failing = 0;
    char problems[96] = "";
    for (uint8_t f = 0; f < s.feedCount && f < KIT_NEWS_FEEDS; f++){
        if (data->feedFetched[f]) read++;
        const uint8_t result = data->feedResult[f];
        if (result != KIT_NEWS_OK){
            failing++;
            const size_t used = strlen(problems);
            snprintf(problems + used, sizeof(problems) - used, "%s%s%s", used ? ", " : "", s.feeds[f].name,
                     result == KIT_NEWS_PARSE ? " (not a feed)" : result == KIT_NEWS_BAD_REQUEST ? " (gone or refused)" : " (not answering)");
        }
    }
    char text[48];
    snprintf(text, sizeof(text), "%d of %u read", read, (unsigned)s.feedCount);
    values["feedsText"] = text;
    values["feedsState"] = failing == 0 ? "ok" : failing < s.feedCount ? "warn" : "error";
    values["problems"] = failing ? problems : "None";
    int shown = 0, freshCount = 0;
    const uint32_t now = rss_Now();
    for (int i = 0; i < data->storyCount; i++){
        if (!rss_Visible(data->stories[i], s, now)) continue;
        shown++;
        if (data->stories[i].fresh) freshCount++;
    }
    values["stories"] = shown;
    values["hidden"] = data->storyCount - shown;
    values["fresh"] = freshCount;
    if (status.lastFetch) values["updated"] = status.lastFetch;
    heap_caps_free(data);
}

// What the phone shows when it opens. Runs on the BLE parser task.
static void rss_Mobile_Ui_Values(JsonObject values){
    Rss_Settings_t* s = (Rss_Settings_t*)heap_caps_malloc(sizeof(Rss_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    rss_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonArray feeds = values["feeds"].to<JsonArray>();
    for (uint8_t i = 0; i < s->feedCount; i++){
        JsonObject f = feeds.add<JsonObject>();
        f["name"] = s->feeds[i].name;
        f["value"] = s->feeds[i].url;
    }
    values["view"] = s->view;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->storySeconds;
    values["summaries"] = s->summaries;
    values["maxAge"] = s->maxAgeHours;
    values["muted"] = s->muted;
    values["refresh"] = s->refreshMinutes;
    rss_Status_Values(values, *s);
    heap_caps_free(s);
}

// Command 0: {"query": "tech"} or a feed's address: answered as command 248 from the catalogue.
static void rss_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 2 || strlen(query) >= RSS_URL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Result_t* results = (Rss_Result_t*)heap_caps_malloc(sizeof(Rss_Result_t) * RSS_RESULTS, MALLOC_CAP_SPIRAM);
    if (results == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    const int count = rss_Search(query, results, RSS_RESULTS);
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < count; i++){
        JsonObject item = list.add<JsonObject>();
        item["name"] = results[i].name;
        item["value"] = results[i].url;
        item["detail"] = results[i].detail;
    }
    heap_caps_free(results);
    const bool address = strncmp(query, "http", 4) == 0;
    if (rssNewsApp) rssNewsApp->mtb_App_Send_Search_Results("feeds", query, list, count == 0 && address ? "That address is not one Pixlpal can read" : nullptr);
}

// Command 1: {"feeds": [{"name": "BBC News", "value": "https://..."}, ...]}: up to 7, in the order shown.
static void rss_Ble_Feeds(JsonDocument& command){
    JsonArray list = command["feeds"];
    if (list.isNull() || list.size() == 0 || list.size() > RSS_FEEDS){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Settings_t* next = rss_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Feed_t feeds[RSS_FEEDS];
    uint8_t count = 0;
    bool valid = true;
    for (JsonVariant item : list){
        const char* url = item["value"];
        const char* name = item["name"];
        if (!rss_Url_Valid(url)){ valid = false; break; }
        bool repeat = false;
        for (uint8_t k = 0; k < count; k++) if (strcmp(feeds[k].url, url) == 0) repeat = true;
        if (repeat) continue;
        memset(&feeds[count], 0, sizeof(feeds[count]));
        snprintf(feeds[count].url, RSS_URL_LEN, "%s", url);
        if (name && name[0]) mtb_Kit_To_Ascii(feeds[count].name, RSS_NAME_LEN, name);
        if (feeds[count].name[0] == '\0') rss_Name_From_Url(feeds[count].name, RSS_NAME_LEN, url);
        count++;
    }
    if (!valid || count == 0){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->feeds, 0, sizeof(next->feeds));
    memcpy(next->feeds, feeds, sizeof(feeds[0]) * count);
    next->feedCount = count;
    rss_End_Change(next);
}

static void rss_Byte_Setting(JsonDocument& command, const char* key, long low, long high, uint8_t Rss_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], low, high, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Settings_t* next = rss_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    rss_End_Change(next);
}

// Command 2: {"view": 0 one story, 1 list}
static void rss_Ble_View(JsonDocument& command){ rss_Byte_Setting(command, "view", 0, RSS_VIEW_LIST, &Rss_Settings_t::view); }

// Command 3: {"cycle"} and/or {"seconds": 5-60}
static void rss_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 5, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Settings_t* next = rss_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->storySeconds = (uint8_t)seconds;
    rss_End_Change(next);
}

// Command 4: {"summaries": 0-1}
static void rss_Ble_Summaries(JsonDocument& command){ rss_Byte_Setting(command, "summaries", 0, 1, &Rss_Settings_t::summaries); }

// Command 5: {"maxAge": 0-168}: hours; 0 for any age.
static void rss_Ble_Max_Age(JsonDocument& command){ rss_Byte_Setting(command, "maxAge", 0, 168, &Rss_Settings_t::maxAgeHours); }

// Command 6: {"muted": "crypto, election"}: "" clears.
static void rss_Ble_Muted(JsonDocument& command){
    const char* text = command["muted"];
    if (text == nullptr || strlen(text) >= RSS_MUTED_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    Rss_Settings_t* next = rss_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->muted, 0, sizeof(next->muted));
    mtb_Kit_To_Ascii(next->muted, sizeof(next->muted), text);
    rss_End_Change(next);
}

// Command 7: {"refresh": 10-180}: minutes between reads of each feed.
static void rss_Ble_Refresh_Minutes(JsonDocument& command){ rss_Byte_Setting(command, "refresh", 10, 180, &Rss_Settings_t::refreshMinutes); }

// Command 8: {} - read every feed now.
static void rss_Ble_Refresh(JsonDocument&){
    if (!mtb_Kit_News_Refresh(rssEngine, rss_Now())) mtb_Ble_App_Cmd_Reject();
}
