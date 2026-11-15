/**
 * World Flags app, ID {6,1}: the flags of 270 countries and territories (the phone app's former list) with their
 * capitals and continents. One flag; a slideshow of every flag, one continent's or your favourites, each once before
 * any repeats; or a quiz that shows a flag and, after a few seconds, its name.
 *
 * The flags are SVGs from the woble/flags set on GitHub, drawn to fit (mtb_kit_image.h). The next flag is fetched
 * while the current one shows, and the last sixteen are kept in memory.
 *
 * The knob: click, the next flag (in the quiz: the answer, then the next); double click, details on or off; long
 * press, the next mode (one flag, slideshow, quiz); turn, brightness.
 */
#include "mtb_engine.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_http.h"
#include "mtb_kit_image.h"
#include "wfl_Model.h"
#include "wfl_Ui.h"
#include "wfl_Mobile_Ui.h"

#define WFL_FLAG_URL        "https://raw.githubusercontent.com/woble/flags/refs/heads/master/SVG/3x2/%s.svg"
#define WFL_FLAG_MAX        (96 * 1024)
#define WFL_CACHE           16
#define WFL_ANSWER_MS       6000
#define WFL_BACKGROUND      MTB_KIT_RGB(0, 0, 16)

EXT_RAM_BSS_ATTR TaskHandle_t worldFlags_Task_H = NULL;
void worldFlags_App_Task(void *);
static void wfl_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen *worldFlags_App;
Mtb_Applications* worldFlags_App_GetInstance() {
    if (!worldFlags_App){
        worldFlags_App = new Mtb_Applications_FullScreen(worldFlags_App_Task, &worldFlags_Task_H, "World Flags", {6,1}, 6144);
        worldFlags_App->mtb_App_Set_Mobile_UI(WFL_UI_MANIFEST, WFL_UI_API, nullptr, wfl_Mobile_Ui_Values);
    }
    return worldFlags_App;
}
MTB_REGISTER_APP(worldFlags_App, 6, 1)

EXT_RAM_BSS_ATTR static Wfl_Settings_t wflSettings;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false;
EXT_RAM_BSS_ATTR static Wfl_Settings_t receivedSettings;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile int shownIndex = -1;            // For the phone.

// Flags kept in memory, by code; only the app task touches flags[]. The fetch task hands over in flagArrival.
struct Wfl_Flag_t { char code[3]; char* svg; size_t length; bool failed; uint32_t usedMs; };
static Wfl_Flag_t flags[WFL_CACHE];
static Wfl_Flag_t flagArrival;
static volatile bool flagBusy = false, flagArrived = false;

static void wfl_Button(button_event_t button);
static void wfl_Ble_Show(JsonDocument& command);
static void wfl_Ble_Favourites(JsonDocument& command);
static void wfl_Ble_Cycle_Legacy(JsonDocument& command);
static void wfl_Ble_Info(JsonDocument& command);
static void wfl_Ble_Interval(JsonDocument& command);
static void wfl_Ble_Search(JsonDocument& command);
static void wfl_Ble_Search_Show(JsonDocument& command);
static void wfl_Ble_Mode(JsonDocument& command);
static void wfl_Ble_Source(JsonDocument& command);
static void wfl_Ble_Continent(JsonDocument& command);
static void wfl_Ble_Quiz_Seconds(JsonDocument& command);

//##############################################################################################################
static bool wfl_Read_Saved_Settings(Wfl_Settings_t& settings){
    wfl_Settings_Default(settings);
    Wfl_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(WFL_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == WFL_SETTINGS_MAGIC;
    if (ok) settings = saved;
    wfl_Settings_Clamp(settings);
    return ok;
}

static void wfl_Save_Settings(void){ mtb_Write_Nvs_Struct(WFL_NVS_KEY, &wflSettings, sizeof(Wfl_Settings_t)); }

static Wfl_Flag_t* wfl_Cached(const char* code){
    for (auto& f : flags) if (f.code[0] && strncmp(f.code, code, 2) == 0) return &f;
    return nullptr;
}

static void wfl_Flag_Task(void* parameter){
    Wfl_Flag_t result = {};
    memcpy(result.code, parameter, sizeof(result.code));
    heap_caps_free(parameter);
    char url[128];
    snprintf(url, sizeof(url), WFL_FLAG_URL, result.code);
    Mtb_Kit_Http_t http;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, WFL_FLAG_MAX, http, "Pixlpal-WorldFlags/2.0 (ESP32 LED display)");
    if (body && http.code == 200 && !http.truncated){ result.svg = body; result.length = http.length; }
    else { heap_caps_free(body); result.failed = true; }
    flagArrival = result;
    flagArrived = true;
    vTaskDelete(NULL);
}

// Into the cache, over the least recently used.
static void wfl_Install_Flag(void){
    Wfl_Flag_t* slot = wfl_Cached(flagArrival.code);
    for (auto& f : flags) if (slot == nullptr && f.code[0] == '\0') slot = &f;
    if (slot == nullptr){ slot = &flags[0]; for (auto& f : flags) if (f.usedMs < slot->usedMs) slot = &f; }
    heap_caps_free(slot->svg);
    *slot = flagArrival;
    slot->usedMs = millis();
    memset(&flagArrival, 0, sizeof(flagArrival));
    flagBusy = false;
}

static bool wfl_Request_Flag(const char* code){
    const Wfl_Flag_t* f = wfl_Cached(code);
    if (f) return true;
    if (flagBusy || Mtb_Applications::internetConnectStatus != true) return false;
    char* parameter = (char*)heap_caps_malloc(3, MALLOC_CAP_SPIRAM);
    if (parameter == nullptr) return false;
    memcpy(parameter, code, 3);
    flagBusy = true;
    if (xTaskCreatePinnedToCore(wfl_Flag_Task, "wflFlag", 8192, parameter, 1, NULL, tskNO_AFFINITY) != pdPASS){ heap_caps_free(parameter); flagBusy = false; }
    return false;
}

//##############################################################################################################
void worldFlags_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(wfl_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(wfl_Ble_Show, wfl_Ble_Favourites, wfl_Ble_Cycle_Legacy, wfl_Ble_Info, wfl_Ble_Interval, wfl_Ble_Search,
                                       wfl_Ble_Search_Show, wfl_Ble_Mode, wfl_Ble_Source, wfl_Ble_Continent, wfl_Ble_Quiz_Seconds);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!wfl_Read_Saved_Settings(wflSettings)) wfl_Save_Settings();
    uint16_t* playlist = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * WFL_MAX_COUNTRIES, MALLOC_CAP_SPIRAM);
    Wfl_Bag_t* bag = (Wfl_Bag_t*)heap_caps_calloc(1, sizeof(Wfl_Bag_t), MALLOC_CAP_SPIRAM);
    if (playlist == nullptr || bag == nullptr){ heap_caps_free(playlist); heap_caps_free(bag); mtb_Delete_This_App(THIS_APP); return; }
    settingsReceived = false;
    clicksRequested = 0;
    longPressRequested = false;

    int playlistCount = 0, position = 0, upcomingPosition = 0, current = -1, upcoming = -1;
    bool rebuild = true, advance = true, revealed = false, redraw = true;
    uint32_t shownSinceMs = millis(), revealedMs = 0, drawnTick = 0;
    Wfl_Country_t country;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const Wfl_Settings_t old = wflSettings;
            wflSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            wfl_Settings_Clamp(wflSettings);
            wfl_Save_Settings();
            if (wflSettings.source != old.source || wflSettings.continent != old.continent || wflSettings.favouriteCount != old.favouriteCount
                || memcmp(wflSettings.favourites, old.favourites, sizeof(old.favourites)) != 0) rebuild = true;
            if (wflSettings.mode != old.mode || strcmp(wflSettings.shown, old.shown) != 0) advance = true;
            redraw = true;
        }
        // The knob.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        if (longPressRequested){
            longPressRequested = false;
            wflSettings.mode = (uint8_t)((wflSettings.mode + 1) % WFL_MODE_COUNT);
            wfl_Save_Settings();
            advance = true;
        }
        if (clicks == 2){ wflSettings.showInfo = !wflSettings.showInfo; wfl_Save_Settings(); redraw = true; }
        if (clicks == 1){
            if (wflSettings.mode == WFL_QUIZ && !revealed){ revealed = true; revealedMs = millis(); redraw = true; }
            else if (wflSettings.mode == WFL_ONE){
                // A flag at random, kept as the one shown.
                const int index = (int)(esp_random() % WFL_COUNTRY_COUNT);
                Wfl_Country_t c;
                if (wfl_Country(index, c)){ memcpy(wflSettings.shown, c.code, 3); wfl_Save_Settings(); }
                advance = true;
            } else advance = true;
        }
        if (rebuild){
            rebuild = false;
            playlistCount = wfl_Playlist(wflSettings, playlist, WFL_MAX_COUNTRIES);
            wfl_Bag_Fill(*bag, (uint16_t)playlistCount, esp_random());
            upcoming = -1;
            advance = true;
        }
        // Time to move on.
        const uint32_t onScreen = millis() - shownSinceMs;
        if (wflSettings.mode == WFL_SLIDESHOW && onScreen >= wflSettings.seconds * 1000UL) advance = true;
        if (wflSettings.mode == WFL_QUIZ && !revealed && onScreen >= wflSettings.quizSeconds * 1000UL){ revealed = true; revealedMs = millis(); redraw = true; }
        if (wflSettings.mode == WFL_QUIZ && revealed && millis() - revealedMs >= WFL_ANSWER_MS) advance = true;
        if (advance){
            advance = false;
            if (wflSettings.mode == WFL_ONE) current = wfl_Find_Code(wflSettings.shown);
            else if (playlistCount){
                if (upcoming >= 0){ current = upcoming; position = upcomingPosition; }
                else { const uint16_t p = wfl_Bag_Next(*bag); position = bag->position; current = playlist[p]; }
                upcoming = -1;
            }
            if (current < 0) current = 0;
            wfl_Country(current, country);
            shownIndex = current;
            shownSinceMs = millis();
            revealed = false;
            redraw = true;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        // The flag on screen first, then the next one.
        const bool haveFlag = wfl_Request_Flag(country.code);
        if (haveFlag && wflSettings.mode != WFL_ONE && upcoming < 0 && playlistCount > 1){
            const uint16_t p = wfl_Bag_Next(*bag);
            upcomingPosition = bag->position;
            upcoming = playlist[p];
        }
        if (haveFlag && upcoming >= 0){ Wfl_Country_t next; if (wfl_Country(upcoming, next)) wfl_Request_Flag(next.code); }
        if (flagArrived){ flagArrived = false; const bool forShown = strncmp(flagArrival.code, country.code, 2) == 0; wfl_Install_Flag(); if (forShown) redraw = true; }
        // The countdown and the progress bar move: redrawn four times a second.
        const uint32_t tick = millis() / 250;
        const bool moved = wflSettings.mode != WFL_ONE && tick != drawnTick;
        if (moved) drawnTick = tick;

        if (redraw || moved){
            Wfl_Flag_t* flag = wfl_Cached(country.code);
            Wfl_View_t v = {};
            v.settings = &wflSettings;
            v.country = &country;
            v.position = position > 0 ? (position - 1) % (playlistCount ? playlistCount : 1) + 1 : 1;
            v.total = playlistCount;
            v.revealed = revealed;
            const float span = wflSettings.mode == WFL_QUIZ ? (revealed ? WFL_ANSWER_MS : wflSettings.quizSeconds * 1000.0f) : wflSettings.seconds * 1000.0f;
            const uint32_t since = wflSettings.mode == WFL_QUIZ && revealed ? millis() - revealedMs : millis() - shownSinceMs;
            v.progress = span > 0 ? since / span : 0;
            if (v.progress > 1) v.progress = 1;
            v.flagState = flag == nullptr ? 0 : flag->svg ? 1 : 2;
            mtb_Panel_Begin_Update();
            if (redraw){
                const Mtb_Kit_Box_t box = wfl_Draw(v);
                if (flag && flag->svg){ flag->usedMs = millis(); if (!mtb_Kit_Draw_Svg(flag->svg, flag->length, box, WFL_BACKGROUND)) { v.flagState = 2; wfl_Draw_Flag_Placeholder(v, box); } }
                else wfl_Draw_Flag_Placeholder(v, box);
            } else wfl_Draw_Progress(v);
            mtb_Panel_End_Update();
            redraw = false;
        }
        delay(30);
    }
    heap_caps_free(playlist);
    heap_caps_free(bag);
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void wfl_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 2) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static Wfl_Settings_t* wfl_Begin_Change(void){
    Wfl_Settings_t* next = (Wfl_Settings_t*)heap_caps_malloc(sizeof(Wfl_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? receivedSettings : wflSettings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void wfl_End_Change(Wfl_Settings_t* next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

// What the phone shows. Runs on the BLE parser task.
static void wfl_Mobile_Ui_Values(JsonObject values){
    Wfl_Settings_t* s = (Wfl_Settings_t*)heap_caps_malloc(sizeof(Wfl_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    wfl_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    Wfl_Country_t c;
    JsonArray favourites = values["countries"].to<JsonArray>();
    for (uint8_t i = 0; i < s->favouriteCount; i++){
        const int index = wfl_Find_Code(s->favourites[i]);
        if (index < 0 || !wfl_Country(index, c)) continue;
        JsonObject item = favourites.add<JsonObject>();
        item["name"] = c.name;
        item["value"] = c.code;
    }
    if (wfl_Country(wfl_Find_Code(s->shown), c)){
        JsonObject shown = values["country"].to<JsonObject>();
        shown["name"] = c.name;
        shown["value"] = c.code;
    }
    const int index = shownIndex;
    if (index >= 0 && wfl_Country(index, c)){
        char text[160];
        snprintf(text, sizeof(text), "%s%s%s%s%s", c.name, c.capital[0] ? ", capital " : "", c.capital, c.continent[0] ? ", " : "", c.continent);
        values["status"] = text;
    }
    values["mode"] = s->mode;
    values["source"] = s->source;
    values["continent"] = s->continent;
    values["dInterval"] = s->seconds;
    values["showData"] = s->showInfo;
    values["quizSeconds"] = s->quizSeconds;
    values["favouriteCount"] = s->favouriteCount;
    heap_caps_free(s);
}

// Command 0: show a flag: {"country": {"name", "value": "ng"}}, or the old page's {"countryName": "Nigeria"}.
static void wfl_Ble_Show(JsonDocument& command){
    int index = wfl_Find_Code(command["country"]["value"] | "");
    if (index < 0) index = wfl_Find_Name(command["countryName"] | "");
    Wfl_Country_t c;
    if (index < 0 || !wfl_Country(index, c)){ mtb_Ble_App_Cmd_Reject(); return; }
    Wfl_Settings_t* next = wfl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memcpy(next->shown, c.code, 3);
    next->mode = WFL_ONE;
    wfl_End_Change(next);
}

// Command 1: favourites: {"countries": [{"name", "value": "ng"}, ...]}, or the old page's {"flagIDs": "[ng, gh]"}.
static void wfl_Ble_Favourites(JsonDocument& command){
    char codes[WFL_FAVOURITES][3];
    uint8_t count = 0;
    bool valid = true;
    auto add = [&](const char* code){
        char c[3] = {0, 0, 0};
        if (code == nullptr || strlen(code) != 2){ valid = false; return; }
        c[0] = (char)tolower((unsigned char)code[0]); c[1] = (char)tolower((unsigned char)code[1]);
        if (wfl_Find_Code(c) < 0){ valid = false; return; }
        for (uint8_t k = 0; k < count; k++) if (strcmp(codes[k], c) == 0) return;
        if (count >= WFL_FAVOURITES){ valid = false; return; }
        memcpy(codes[count++], c, 3);
    };
    if (command["countries"].is<JsonArray>()) for (JsonVariant item : command["countries"].as<JsonArray>()) add(item["value"] | "");
    else if (command["flagIDs"].is<const char*>()){
        char text[WFL_FAVOURITES * 6];
        snprintf(text, sizeof(text), "%s", command["flagIDs"].as<const char*>());
        for (char* ch = text; *ch; ch++) if (*ch == '[' || *ch == ']' || *ch == '"') *ch = ' ';
        for (char* token = strtok(text, ", "); token && valid; token = strtok(nullptr, ", ")) add(token);
    } else valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Wfl_Settings_t* next = wfl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(next->favourites, 0, sizeof(next->favourites));
    memcpy(next->favourites, codes, sizeof(codes[0]) * count);
    next->favouriteCount = count;
    next->source = count ? WFL_FAVOURITE : WFL_ALL;
    wfl_End_Change(next);
}

// Command 2 (the old page): {"cycleFlags": 0-1, "showData": 0-1, "dInterval": seconds}
static void wfl_Ble_Cycle_Legacy(JsonDocument& command){
    long cycle = -1, info = -1, seconds = -1;
    const bool hasCycle = mtb_Kit_Json_Range(command["cycleFlags"], 0, 1, cycle);
    const bool hasInfo = mtb_Kit_Json_Range(command["showData"], 0, 1, info);
    const bool hasSeconds = mtb_Kit_Json_Range(command["dInterval"], 3, 600, seconds);
    if (!hasCycle && !hasInfo && !hasSeconds){ mtb_Ble_App_Cmd_Reject(); return; }
    Wfl_Settings_t* next = wfl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) next->mode = cycle ? WFL_SLIDESHOW : WFL_ONE;
    if (hasInfo) next->showInfo = (uint8_t)info;
    if (hasSeconds) next->seconds = (uint16_t)seconds;
    wfl_End_Change(next);
}

// Command 3: {"showData": 0-1}: name, capital and continent beside the flag.
static void wfl_Ble_Info(JsonDocument& command){ wfl_Ble_Cycle_Legacy(command); }

// Command 4: {"dInterval": 3-600} seconds per flag (the old page also sent "cycleFlags": 1).
static void wfl_Ble_Interval(JsonDocument& command){ wfl_Ble_Cycle_Legacy(command); }

// Commands 5 and 6: {"query": "gha"}: the country list, answered as 248 for the favourites ("countries") or the
// show-now box ("show").
static void wfl_Search_Answer(JsonDocument& command, const char* searchId){
    const char* query = command["query"];
    if (query == nullptr || strlen(query) < 1 || strlen(query) > 40){ mtb_Ble_App_Cmd_Reject(); return; }
    Wfl_Country_t* results = (Wfl_Country_t*)heap_caps_malloc(sizeof(Wfl_Country_t) * 12, MALLOC_CAP_SPIRAM);
    if (results == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    const int count = wfl_Search(query, results, 12);
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < count; i++){
        char detail[64];
        snprintf(detail, sizeof(detail), "%s%s%s", results[i].capital, results[i].capital[0] && results[i].continent[0] ? ", " : "", results[i].continent);
        JsonObject item = list.add<JsonObject>();
        item["name"] = results[i].name;
        item["value"] = results[i].code;
        item["detail"] = detail;
    }
    heap_caps_free(results);
    if (worldFlags_App) worldFlags_App->mtb_App_Send_Search_Results(searchId, query, list, nullptr);
}

static void wfl_Ble_Search(JsonDocument& command){ wfl_Search_Answer(command, "countries"); }
static void wfl_Ble_Search_Show(JsonDocument& command){ wfl_Search_Answer(command, "show"); }

static void wfl_Byte(JsonDocument& command, const char* key, long low, long high, uint8_t Wfl_Settings_t::* field){
    long value;
    if (!mtb_Kit_Json_Range(command[key], low, high, value)){ mtb_Ble_App_Cmd_Reject(); return; }
    Wfl_Settings_t* next = wfl_Begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)value;
    if (field == &Wfl_Settings_t::source && value == WFL_FAVOURITE && next->favouriteCount == 0){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    wfl_End_Change(next);
}

// Commands 7-10: {"mode": 0 one flag, 1 slideshow, 2 quiz}, {"source": 0 all, 1 a continent, 2 favourites},
// {"continent": 0-6}, {"quizSeconds": 3-30}
static void wfl_Ble_Mode(JsonDocument& command){ wfl_Byte(command, "mode", 0, WFL_MODE_COUNT - 1, &Wfl_Settings_t::mode); }
static void wfl_Ble_Source(JsonDocument& command){ wfl_Byte(command, "source", 0, WFL_SOURCE_COUNT - 1, &Wfl_Settings_t::source); }
static void wfl_Ble_Continent(JsonDocument& command){ wfl_Byte(command, "continent", 0, WFL_CONTINENTS - 1, &Wfl_Settings_t::continent); }
static void wfl_Ble_Quiz_Seconds(JsonDocument& command){ wfl_Byte(command, "quizSeconds", 3, 30, &Wfl_Settings_t::quizSeconds); }
