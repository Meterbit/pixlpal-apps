/**
 * News API app, ID {1,2}: US top stories by category and stories on your own topics in any language, from
 * NewsAPI.org, one at a time as large as they fit, or as a list. New stories are marked and shown first.
 *
 * NewsAPI's free Developer plan (100 requests a day, stories 24 hours late) is for development only; its terms
 * ask for a paid plan anywhere else. The plan is probed once a day, so a Business key's real-time stories and
 * larger quota are used as soon as the key has them. The shared news engine (mtb_kit_news.h) paces the requests;
 * nap_Provider.cpp speaks NewsAPI.
 */
#include "mtb_engine.h"
#include <ctype.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_news.h"
#include "nap_Provider.h"
#include "nap_Mobile_Ui.h"

#define NAP_CLOCK_VALID_AFTER   1700000000UL

EXT_RAM_BSS_ATTR TaskHandle_t newsAPI_Task_H = NULL;
void newsAPI_App_Task(void *);
static void nap_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *newsAPI_App;
Mtb_Applications* newsAPI_App_GetInstance() {
    if (!newsAPI_App){
        newsAPI_App = new Mtb_Applications_StatusBar(newsAPI_App_Task, &newsAPI_Task_H, "News API", {1,2}, 8192);
        newsAPI_App->mtb_App_Set_Mobile_UI(NAP_UI_MANIFEST, NAP_UI_API, NAP_UI_DATA, nap_Mobile_Ui_Values);
    }
    return newsAPI_App;
}
MTB_REGISTER_APP(newsAPI_App, 1, 2)

EXT_RAM_BSS_ATTR static Nap_Settings_t napSettings;
EXT_RAM_BSS_ATTR static Mtb_Kit_News_t napEngine;
static Mtb_Kit_News_Data_t* viewData = nullptr;         // PSRAM

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Nap_Settings_t receivedSettings;

static void nap_Button(button_event_t button);
static void nap_Ble_Categories(JsonDocument& command);
static void nap_Ble_Topics(JsonDocument& command);
static void nap_Ble_Language(JsonDocument& command);
static void nap_Ble_Key(JsonDocument& command);
static void nap_Ble_Floor(JsonDocument& command);
static void nap_Ble_Pages(JsonDocument& command);
static void nap_Ble_View(JsonDocument& command);
static void nap_Ble_Refresh(JsonDocument& command);

//##############################################################################################################
static uint32_t nap_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)NAP_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool nap_Read_Saved_Settings(Nap_Settings_t& settings){
    nap_Settings_Default(settings);
    Nap_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(NAP_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == NAP_SETTINGS_MAGIC;
    if (ok) settings = saved;
    nap_Settings_Clamp(settings);
    memset(&saved, 0, sizeof(saved));
    return ok;
}

static void nap_Save_Settings(void){ mtb_Write_Nvs_Struct(NAP_NVS_KEY, &napSettings, sizeof(Nap_Settings_t)); }

static bool nap_Message(const Mtb_Kit_News_Status_t& status, bool online, uint32_t now, int stories, const char*& title, const char*& detail){
    if (status.keyMissing){ title = "ADD YOUR KEY"; detail = "Get a key at newsapi.org and paste it into News API in the Pixlpal app."; return true; }
    if (status.keyRejected && stories == 0){ title = "KEY NOT ACCEPTED"; detail = "NewsAPI did not accept the key. Check it on newsapi.org/account."; return true; }
    if (stories > 0) return false;
    if (!online){ title = "OFFLINE"; detail = "Headlines appear once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "SETTING THE CLOCK"; detail = "Headlines appear once Pixlpal knows the time."; return true; }
    if (status.quotaUsed){ title = "REQUESTS USED UP"; detail = "Today's NewsAPI requests are used. Headlines come back after midnight UTC."; return true; }
    title = "GETTING THE NEWS";
    detail = "Asking NewsAPI for your feeds...";
    return true;
}

//##############################################################################################################
void newsAPI_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(nap_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(nap_Ble_Categories, nap_Ble_Topics, nap_Ble_Language, nap_Ble_Key,
                                       nap_Ble_Floor, nap_Ble_Pages, nap_Ble_View, nap_Ble_Refresh);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!nap_Read_Saved_Settings(napSettings)) nap_Save_Settings();
    if (viewData == nullptr) viewData = (Mtb_Kit_News_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
    uint8_t* order = (uint8_t*)heap_caps_calloc(KIT_NEWS_STORIES, 1, MALLOC_CAP_SPIRAM);
    Mtb_Kit_News_Config_t* config = (Mtb_Kit_News_Config_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Config_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr || order == nullptr || config == nullptr){ heap_caps_free(order); heap_caps_free(config); mtb_Delete_This_App(THIS_APP); return; }
    nap_Config(napSettings, *config);
    mtb_Kit_News_Begin(napEngine, nap_Provider(), *config);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;

    uint32_t version = 0, storyStartMs = millis(), pushMs = 0, drawnMs = 0;
    uint32_t currentHash = 0, announcedHash = 0;     // The new story last jumped to: not again.
    int current = 0, orderCount = 0;
    bool details = false, paused = false, redraw = true;
    Mtb_Kit_News_Status_t status = {};

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = nap_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            napSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            nap_Settings_Clamp(napSettings);
            nap_Save_Settings();
            nap_Config(napSettings, *config);
            mtb_Kit_News_Set_Config(napEngine, *config);
            redraw = true;
        }
        mtb_Kit_News_Poll(napEngine, online, now);
        if (mtb_Kit_News_Copy(napEngine, *viewData, version)){
            orderCount = mtb_Kit_News_Order(*viewData, order, KIT_NEWS_STORIES);
            // Stay on the story shown, or go to the first new one.
            int found = -1, fresh = -1;
            for (int i = 0; i < orderCount; i++){
                if (viewData->stories[order[i]].hash == currentHash) found = i;
                if (fresh < 0 && viewData->stories[order[i]].fresh) fresh = i;
            }
            const bool announce = fresh >= 0 && !paused && viewData->stories[order[fresh]].hash != announcedHash;
            current = announce ? fresh : found >= 0 ? found : 0;
            if (announce){ announcedHash = viewData->stories[order[fresh]].hash; storyStartMs = millis(); details = false; }
            redraw = true;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        mtb_Kit_News_Get_Status(napEngine, status);
        if (millis() - pushMs >= 30000){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }

        // The knob: click, next story; double click, the summary; triple, refresh; long press, pause.
        const uint8_t clicks = clicksRequested;
        if (clicks || longPressRequested){
            clicksRequested = 0;
            if (longPressRequested){ longPressRequested = false; paused = !paused; }
            else if (clicks == 1 && orderCount){ current = (current + 1) % orderCount; details = false; }
            else if (clicks == 2) details = !details;
            else if (clicks == 3) mtb_Kit_News_Refresh(napEngine, now);
            storyStartMs = millis();
            redraw = true;
        }
        const uint32_t storyMs = napSettings.pageSeconds * 1000UL * (details ? 2 : 1);
        if (!paused && napSettings.autoCycle && orderCount > 1 && millis() - storyStartMs >= storyMs){
            current = (current + 1) % orderCount;
            details = false;
            storyStartMs = millis();
            redraw = true;
        }
        if (orderCount) currentHash = viewData->stories[order[current % orderCount]].hash;
        // The card's progress bar moves; the list does not need it.
        if (!redraw && napSettings.view == NAP_VIEW_CARD && !paused && millis() - drawnMs >= 500) redraw = true;

        if (redraw){
            redraw = false;
            drawnMs = millis();
            const char *title, *detail;
            mtb_Panel_Begin_Update();
            if (nap_Message(status, online, now, orderCount, title, detail)) mtb_Kit_News_Draw_Message(title, detail, "NEWSAPI.ORG");
            else {
                Mtb_Kit_News_View_t view = {};
                view.data = viewData;
                view.config = config;
                view.order = order;
                view.orderCount = orderCount;
                view.current = current;
                view.now = now;
                view.progress = napSettings.autoCycle ? (float)(millis() - storyStartMs) / storyMs : 0;
                view.details = details;
                view.paused = paused;
                view.credit = "NEWSAPI.ORG";
                if (napSettings.view == NAP_VIEW_LIST) mtb_Kit_News_Draw_List(view);
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
static void nap_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Nap_Settings_t* nap_Begin_Change(void){
    Nap_Settings_t* next = (Nap_Settings_t*)heap_caps_malloc(sizeof(Nap_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : napSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void nap_End_Change(Nap_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void nap_Status_Values(JsonObject values){
    Mtb_Kit_News_Status_t status;
    mtb_Kit_News_Get_Status(napEngine, status);
    if (!status.started) return;
    Mtb_Kit_News_Plan_t plan;
    uint16_t used;
    mtb_Kit_News_Get_Plan(napEngine, plan, used);
    values["plan"] = plan.known ? plan.name : "Checking";
    values["planState"] = !plan.known ? "off" : plan.paid ? "ok" : "info";
    values["paid"] = plan.paid;
    values["delay"] = !plan.known ? "" : plan.delayHours ? "24 hours late (Developer plan)" : "Real time";
    if (status.keyRejected){ values["keyText"] = "Not accepted by NewsAPI"; values["keyState"] = "error"; }
    else if (plan.known){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    const uint32_t limit = plan.dailyLimit ? plan.dailyLimit : 100;
    values["left"] = used < limit ? limit - used : 0;
    values["limit"] = limit;
    values["quotaState"] = status.quotaUsed ? "error" : (limit - (used < limit ? used : limit)) * 10 < limit ? "warn" : "ok";
    char pace[48];
    if (status.quotaUsed) snprintf(pace, sizeof(pace), "Paused until midnight UTC");
    else if (status.feedInterval >= 3600) snprintf(pace, sizeof(pace), "Each feed every %u h %02u min", (unsigned)(status.feedInterval / 3600), (unsigned)(status.feedInterval % 3600 / 60));
    else snprintf(pace, sizeof(pace), "Each feed every %u min", (unsigned)((status.feedInterval + 30) / 60));
    values["pace"] = pace;
    values["paceState"] = status.quotaUsed ? "error" : "ok";
    if (status.lastFetch) values["updated"] = status.lastFetch;
}

// What the phone shows when it opens. The key never leaves the device. Runs on the BLE parser task.
static void nap_Mobile_Ui_Values(JsonObject values){
    Nap_Settings_t* s = (Nap_Settings_t*)heap_caps_malloc(sizeof(Nap_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    nap_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    size_t total;
    const Nap_Category_t* categories = nap_Categories(total);
    JsonArray list = values["categories"].to<JsonArray>();
    for (uint8_t i = 0; i < s->categoryCount; i++) list.add(categories[s->categories[i]].id);
    char topics[NAP_MAX_TOPICS * (NAP_TOPIC_LEN + 2)] = "";
    for (uint8_t i = 0; i < s->topicCount; i++){
        const size_t used = strlen(topics);
        snprintf(topics + used, sizeof(topics) - used, "%s%s", i ? ", " : "", s->topics[i]);
    }
    values["topics"] = topics;
    values["language"] = s->language[0] ? s->language : "any";
    values["floor"] = s->floorMinutes;
    values["cycle"] = s->autoCycle;
    values["seconds"] = s->pageSeconds;
    values["view"] = s->view;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    nap_Status_Values(values);
}

// Command 0: {"categories": ["general", "technology"]}: up to 4. Older phone apps send "[general, technology]".
static void nap_Ble_Categories(JsonDocument& command){
    uint8_t picked[NAP_MAX_CATEGORIES];
    uint8_t count = 0;
    bool valid = true;
    auto add = [&](const char* id, size_t length){
        char name[16];
        if (length == 0 || length >= sizeof(name)){ valid = false; return; }
        memcpy(name, id, length);
        name[length] = '\0';
        const int index = nap_Category_Index(name);
        if (index < 0 || count >= NAP_MAX_CATEGORIES){ valid = false; return; }
        for (uint8_t k = 0; k < count; k++) if (picked[k] == index) return;
        picked[count++] = (uint8_t)index;
    };
    JsonVariant list = command["categories"];
    if (list.is<JsonArray>()){ for (JsonVariant item : list.as<JsonArray>()){ const char* t = item; if (t) add(t, strlen(t)); else valid = false; } }
    else if (list.is<const char*>()){
        for (const char* c = list.as<const char*>(); *c; ){
            if (isalpha((unsigned char)*c)){ const char* start = c; while (isalpha((unsigned char)*c)) c++; add(start, (size_t)(c - start)); }
            else if (strchr("[], \"", *c)) c++;
            else { valid = false; break; }
        }
    } else valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (count == 0 && next->topicCount == 0){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }     // Nothing to show.
    memcpy(next->categories, picked, count);
    next->categoryCount = count;
    nap_End_Change(next);
}

// Command 1: {"topics": "AI, Arsenal, Lagos"}: up to 3, comma-separated; "" clears.
static void nap_Ble_Topics(JsonDocument& command){
    const char* text = command["topics"];
    if (text == nullptr || strlen(text) > 100){ mtb_Ble_App_Cmd_Reject(); return; }
    char topics[NAP_MAX_TOPICS][NAP_TOPIC_LEN];
    uint8_t count = 0;
    for (const char* start = text; *start; ){
        const char* end = start;
        while (*end && *end != ',') end++;
        const char* a = start; const char* b = end;
        while (a < b && *a == ' ') a++;
        while (b > a && b[-1] == ' ') b--;
        if (b > a){
            if (count >= NAP_MAX_TOPICS || (size_t)(b - a) >= NAP_TOPIC_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
            memcpy(topics[count], a, (size_t)(b - a));
            topics[count][b - a] = '\0';
            if (!nap_Topic_Valid(topics[count])){ mtb_Ble_App_Cmd_Reject(); return; }
            count++;
        }
        start = *end ? end + 1 : end;
    }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (count == 0 && next->categoryCount == 0){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->topics, 0, sizeof(next->topics));
    memcpy(next->topics, topics, sizeof(topics[0]) * count);
    next->topicCount = count;
    nap_End_Change(next);
}

// "en", "ng" or "any".
static bool nap_Two_Letters(JsonVariantConst field, char* code){
    const char* text = field;
    if (text == nullptr) return false;
    if (strcasecmp(text, "any") == 0 || text[0] == '\0'){ code[0] = '\0'; return true; }
    if (strlen(text) != 2 || !isalpha((unsigned char)text[0]) || !isalpha((unsigned char)text[1])) return false;
    code[0] = (char)tolower((unsigned char)text[0]);
    code[1] = (char)tolower((unsigned char)text[1]);
    code[2] = '\0';
    return true;
}

// Command 2: {"language": "en"} ("any" for all): the language of the topics' stories.
static void nap_Ble_Language(JsonDocument& command){
    char code[4];
    if (!nap_Two_Letters(command["language"], code)){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memcpy(next->language, code, sizeof(code));
    nap_End_Change(next);
}

// Command 3: {"api_key": "..."}: NewsAPI keys are 32 hexadecimal characters.
static void nap_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length == 32;
    for (size_t i = 0; valid && i < length; i++) if (!isxdigit((unsigned char)key[i])) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->apiKey, 0, sizeof(next->apiKey));
    memcpy(next->apiKey, key, length);
    nap_End_Change(next);
}

// Command 4: {"floor": 5-180}: no feed more often than this many minutes.
static void nap_Ble_Floor(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["floor"], 5, 180, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->floorMinutes = (uint8_t)value;
    nap_End_Change(next);
}

// Command 5: {"cycle"} and/or {"seconds": 5-60}
static void nap_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycle"], 0, 1, cycle);
    const bool hasSeconds = mtb_Kit_Json_Range(command["seconds"], 5, 60, seconds);
    if ((!hasCycle && !hasSeconds) || (!command["seconds"].isNull() && !hasSeconds) || (!command["cycle"].isNull() && !hasCycle)){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->autoCycle = (uint8_t)cycle;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    nap_End_Change(next);
}

// Command 6: {"view": 0 one story, 1 list}
static void nap_Ble_View(JsonDocument& command){
    long value;
    if (!mtb_Kit_Json_Range(command["view"], 0, NAP_VIEW_LIST, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Nap_Settings_t* next = nap_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->view = (uint8_t)value;
    nap_End_Change(next);
}

// Command 7: {} - read every feed now.
static void nap_Ble_Refresh(JsonDocument&){
    if (!mtb_Kit_News_Refresh(napEngine, nap_Now())) mtb_Ble_App_Cmd_Reject();
}
