/**
 * My Training: the owner's week from Strava. The distance since Monday (of the chosen sport, or everything) in large
 * figures against a weekly goal, the time moving, the climbing and the number of activities; a bar for each day of
 * the week; and the latest activity, with its distance, time and pace (or speed for a ride).
 *
 * Strava signs in from the phone (accountLink, provider "strava"): the account server holds the client secret, makes
 * the code exchange and refreshes the tokens (kit_oauth, CONFIG_PXP_OAUTH_BROKER_URL in include/sdkconfig.h).
 * GET /api/v3/athlete (the name, once) and /api/v3/athlete/activities?per_page=30 (newest first) every hour: Strava
 * limits the requests of all Pixlpals together. An activity list is read one activity at a time, so a list cut short
 * at 64 KB still counts the newest ones. NVS "mtSave" (settings) and "mt_tok" (the refresh token).
 * Knob: click, check now; turn, brightness.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_kit_oauth.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "mt_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "MY_TRAINING";
#define W           256
#define H           128
#define NVS_KEY     "mtSave"
#define TOKEN_KEY   "mt_tok"
#define SAVE_MAGIC  0x4D545201u
#define UA          "Pixlpal-MyTraining/1.0"
#define EVERY_MS    (60u * 60u * 1000u)

static const Mtb_Kit_OAuth_Provider_t STRAVA = {
    "https://www.strava.com/oauth/token", "", "strava", nullptr, TOKEN_KEY, UA, nullptr,
};

EXT_RAM_BSS_ATTR TaskHandle_t myTraining_Task_H = NULL;
static void myTraining_App_Task(void* dApplication);
static void mt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* myTraining_App;
Mtb_Applications* myTraining_App_GetInstance(){
    if (!myTraining_App){
        myTraining_App = new Mtb_Applications_FullScreen(myTraining_App_Task, &myTraining_Task_H, "My Training", {0,0}, 8192);
        myTraining_App->mtb_App_Set_Mobile_UI(MT_UI_MANIFEST, MT_UI_API, nullptr, mt_Values);
    }
    return myTraining_App;
}
MTB_REGISTER_APP(myTraining_App, 0, 0)       // the catalogue gives the app its number

struct Mt_Save_t { uint32_t magic; uint8_t sport, miles; uint16_t goal; char account[40]; };

struct Mt_Latest_t { char name[40], type[16]; float metres; uint32_t seconds, at; };
struct Mt_Shared_t {
    bool known;
    float metres, climb;                // this week, of the chosen sport
    uint32_t seconds;
    uint8_t count;
    float day[7];                       // metres, Monday first
    Mt_Latest_t latest;
    char athlete[40];
    char why[48];
    uint8_t state;                      // Mtb_Kit_OAuth_State_t
    uint32_t at, version;
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Mt_Save_t save;
static EXT_RAM_BSS_ATTR Mt_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Mt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.goal = 30; }
    if (s.sport > 4) s.sport = 0;
    if (s.goal > 1000) s.goal = 30;
    s.account[sizeof(s.account) - 1] = '\0';
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Link(JsonDocument& c){
    const char* token = c["refreshToken"] | (const char*)nullptr;
    if (!token || !mtb_Kit_OAuth_Store(TOKEN_KEY, token)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.account[0] = '\0'; sh.known = false; sh.athlete[0] = '\0'; sh.why[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Unlink(JsonDocument&){
    mtb_Kit_OAuth_Forget(TOKEN_KEY);
    portENTER_CRITICAL(&lock); save.account[0] = '\0'; sh.known = false; sh.athlete[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Sport(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sport"], 0, 4, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sport = (uint8_t)v; sh.checkNow = true; phoneChanged = true; }
static void cmd_Goal(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["goal"], 0, 1000, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.goal = (uint16_t)v; phoneChanged = true; }
static void cmd_Miles(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["miles"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.miles = (uint8_t)v; phoneChanged = true; }
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }

// The worker ------------------------------------------------------------------------------------------------------------------------
/** Whether a Strava sport_type counts for the chosen sport (0 everything). */
static bool counts(uint8_t sport, const char* type){
    if (sport == 0) return true;
    if (sport == 1) return strstr(type, "Run") != nullptr;                                   // Run, TrailRun, VirtualRun
    if (sport == 2) return strstr(type, "Ride") != nullptr;                                  // Ride, GravelRide, MountainBikeRide, EBikeRide, VirtualRide
    if (sport == 3) return !strcmp(type, "Walk") || !strcmp(type, "Hike");
    return !strcmp(type, "Swim");
}

/** Local midnight of this week's Monday, as Unix time, and the local day (0 Monday) of a time. */
static uint32_t week_Start(int32_t& offset){
    const time_t now = time(nullptr);
    struct tm l;
    localtime_r(&now, &l);
    const int64_t localNow = (int64_t)mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday) * 86400 + l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
    offset = (int32_t)(localNow - (int64_t)now);
    const int monday = (l.tm_wday + 6) % 7;
    const int64_t localMidnight = localNow - (l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec) - (int64_t)monday * 86400;
    return (uint32_t)(localMidnight - offset);
}

static int get(Mtb_Kit_OAuth_t& oauth, const char* url, char** body, Mtb_Kit_Http_t& r){
    char* bearer = (char*)heap_caps_malloc(MTB_KIT_OAUTH_ACCESS_MAX + 8, MALLOC_CAP_SPIRAM);
    *body = nullptr;
    if (!bearer) return -3;
    if (!mtb_Kit_OAuth_Bearer(oauth, bearer, MTB_KIT_OAUTH_ACCESS_MAX + 8)){ heap_caps_free(bearer); return -4; }
    const char* headers[] = {"Authorization", bearer, nullptr};
    r = {};
    *body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 64000, r, UA);
    memset(bearer, 0, MTB_KIT_OAUTH_ACCESS_MAX + 8);
    heap_caps_free(bearer);
    if (r.code == 401) mtb_Kit_OAuth_Reject(oauth);
    return r.code;
}

/** The activities, newest first, one at a time (a list cut short still counts the ones before the cut). */
static void scan(const char* body, size_t n, uint8_t sport, uint32_t weekStart, int32_t offset, Mt_Shared_t& r){
    r.metres = r.climb = 0; r.seconds = 0; r.count = 0;
    memset(r.day, 0, sizeof(r.day));
    memset(&r.latest, 0, sizeof(r.latest));
    const char* p = strchr(body, '[');
    if (!p) return;
    p++;
    const char* end = body + n;
    while (p < end){
        while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r')) p++;
        if (p >= end || *p != '{') break;
        int depth = 0; bool str = false; const char* q = p;
        for (; q < end; q++){
            if (str){ if (*q == '\\') q++; else if (*q == '"') str = false; continue; }
            if (*q == '"') str = true; else if (*q == '{') depth++; else if (*q == '}' && --depth == 0) break;
        }
        if (q >= end) break;
        JsonDocument doc, filter;
        filter["name"] = true; filter["sport_type"] = true; filter["distance"] = true; filter["moving_time"] = true;
        filter["total_elevation_gain"] = true; filter["start_date"] = true;
        if (!deserializeJson(doc, p, (size_t)(q - p + 1), DeserializationOption::Filter(filter))){
            const char* type = doc["sport_type"] | "";
            const uint32_t at = mtb_Kit_Parse_Iso_Time(doc["start_date"] | "");
            if (counts(sport, type)){
                if (!r.latest.at){
                    mtb_Kit_To_Ascii(r.latest.name, sizeof(r.latest.name), doc["name"] | "");
                    snprintf(r.latest.type, sizeof(r.latest.type), "%s", type);
                    r.latest.metres = doc["distance"] | 0.0f; r.latest.seconds = doc["moving_time"] | 0u; r.latest.at = at;
                }
                if (at >= weekStart){
                    const float m = doc["distance"] | 0.0f;
                    r.metres += m; r.climb += doc["total_elevation_gain"] | 0.0f; r.seconds += doc["moving_time"] | 0u; r.count++;
                    const int d = (int)(((int64_t)at - weekStart) / 86400);
                    if (d >= 0 && d < 7) r.day[d] += m;
                }
            }
            if (at && at < weekStart && r.latest.at) break;          // older than the week, and the latest is known
        }
        p = q + 1;
    }
    (void)offset;
}

static void worker(void*){
    Mtb_Kit_OAuth_t oauth;
    memset(&oauth, 0, sizeof(oauth));
    const bool ready = mtb_Kit_OAuth_Begin(oauth, STRAVA);
    uint32_t next = 0;
    static Mt_Shared_t r;
    while (!sh.stop && ready){
        if (!sh.checkNow && (int32_t)(millis() - next) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        next = millis() + EVERY_MS;
        if (!mtb_Kit_OAuth_Is_Stored(TOKEN_KEY)){ portENTER_CRITICAL(&lock); sh.state = MTB_OAUTH_UNLINKED; sh.version++; portEXIT_CRITICAL(&lock); continue; }
        mtb_Kit_OAuth_End(oauth); mtb_Kit_OAuth_Begin(oauth, STRAVA);       // a token linked since
        Mt_Save_t s;
        portENTER_CRITICAL(&lock); s = save; r = sh; portEXIT_CRITICAL(&lock);
        r.why[0] = '\0';
        char* body = nullptr;
        Mtb_Kit_Http_t h;
        int code = 0;
        if (!r.athlete[0]){
            code = get(oauth, "https://www.strava.com/api/v3/athlete", &body, h);
            if (code == 200 && body){
                JsonDocument doc, filter;
                filter["firstname"] = true; filter["lastname"] = true;
                if (!deserializeJson(doc, body, h.length, DeserializationOption::Filter(filter))){
                    char n[48];
                    snprintf(n, sizeof(n), "%s %s", doc["firstname"] | "", doc["lastname"] | "");
                    mtb_Kit_To_Ascii(r.athlete, sizeof(r.athlete), n);
                }
            }
            heap_caps_free(body); body = nullptr;
        }
        if (code == 0 || code == 200){
            int32_t offset;
            const uint32_t weekStart = week_Start(offset);
            code = get(oauth, "https://www.strava.com/api/v3/athlete/activities?per_page=30", &body, h);
            if (code == 200 && body){ scan(body, h.length, s.sport, weekStart, offset, r); r.known = true; r.at = (uint32_t)time(nullptr); }
            ESP_LOGI(TAG, "Activities: HTTP %d, %u bytes%s, %u this week", code, (unsigned)h.length, h.truncated ? " (cut short)" : "", (unsigned)r.count);
            heap_caps_free(body);
        }
        r.state = oauth.state;
        if (code == -4) snprintf(r.why, sizeof(r.why), "%s", oauth.state == MTB_OAUTH_RELINK ? "LINK STRAVA AGAIN ON THE PHONE" : "STRAVA SIGN-IN: TRYING AGAIN LATER");
        else if (code == 429){ snprintf(r.why, sizeof(r.why), "STRAVA IS BUSY: TRYING AGAIN LATER"); next = millis() + 20u * 60000u; }
        else if (code == 401) snprintf(r.why, sizeof(r.why), "STRAVA REFUSED: LINK IT AGAIN");
        else if (code <= 0 && code != -4) snprintf(r.why, sizeof(r.why), "NO ANSWER FROM STRAVA");
        else if (code != 200) snprintf(r.why, sizeof(r.why), "STRAVA: HTTP %d", code);
        if (r.why[0] && code != 429) next = millis() + 10u * 60000u;
        portENTER_CRITICAL(&lock);
        if (s.sport == save.sport){
            sh.known = r.known; sh.metres = r.metres; sh.climb = r.climb; sh.seconds = r.seconds; sh.count = r.count;
            memcpy(sh.day, r.day, sizeof(r.day)); sh.latest = r.latest; snprintf(sh.athlete, sizeof(sh.athlete), "%s", r.athlete);
            snprintf(sh.why, sizeof(sh.why), "%s", r.why); sh.state = r.state; sh.at = r.at;
            if (r.athlete[0] && strcmp(save.account, r.athlete)){ snprintf(save.account, sizeof(save.account), "%s", r.athlete); phoneChanged = true; }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    mtb_Kit_OAuth_End(oauth);
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(8, 6, 4), C_STRAVA = MTB_KIT_RGB(252, 76, 2), C_TRACK = MTB_KIT_RGB(50, 30, 20);
static const char* const SPORT[] = {"ALL ACTIVITIES", "RUNS", "RIDES", "WALKS AND HIKES", "SWIMS"};

static void distance_Text(char* t, size_t n, float metres, bool miles, bool unit){
    const float v = miles ? metres / 1609.344f : metres / 1000.0f;
    snprintf(t, n, v < 100 ? "%.1f%s" : "%.0f%s", v, unit ? (miles ? " MI" : " KM") : "");
}

static void time_Text(char* t, size_t n, uint32_t s){
    if (s >= 3600) snprintf(t, n, "%luH %02luM", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    else snprintf(t, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void draw(const Mt_Save_t& s, const Mt_Shared_t& d){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], f[64];
    if (!mtb_Kit_OAuth_Is_Stored(TOKEN_KEY)){
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, "LINK YOUR STRAVA ACCOUNT", C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, "IN THE PHONE APP, UNDER MY TRAINING", C_BG);
        return;
    }
    // The header.
    mtb_Kit_Text(4, 2, Terminal6x8, C_STRAVA, "THIS WEEK", C_BG);
    snprintf(t, sizeof(t), "%s", d.athlete); mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, 150);
    mtb_Kit_Text_Right(W - 4, 3, Terminal4x6, KIT_LABEL, f, C_BG);
    if (!d.known){ mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "READING STRAVA...", C_BG); return; }
    // The distance and the goal.
    distance_Text(t, sizeof(t), d.metres, s.miles, false);
    const int16_t x = mtb_Kit_Text(4, 12, PT_Sans_Narrow_Bold28x34, KIT_TEXT, t, C_BG);
    mtb_Kit_Text(x + 4, 32, Terminal8x12, KIT_LABEL, s.miles ? "MI" : "KM", C_BG);
    mtb_Kit_Text(4, 47, Terminal4x6, KIT_DIM, SPORT[s.sport], C_BG);
    if (s.goal){
        const float goalM = s.goal * (s.miles ? 1609.344f : 1000.0f);
        const float frac = d.metres / goalM;
        mtb_Kit_Bar(4, 56, 120, 4, frac, frac >= 1 ? KIT_GREEN : C_STRAVA, C_TRACK);
        snprintf(t, sizeof(t), frac >= 1 ? "GOAL MET! %u %s" : "OF %u %s", (unsigned)s.goal, s.miles ? "MI" : "KM");
        mtb_Kit_Text(4, 63, Terminal4x6, frac >= 1 ? KIT_GREEN : KIT_LABEL, t, C_BG);
    }
    // Time, climbing, activities.
    time_Text(t, sizeof(t), d.seconds);
    mtb_Kit_Text(140, 14, Terminal4x6, KIT_DIM, "TIME", C_BG); mtb_Kit_Text(140, 21, Terminal8x12, KIT_TEXT, t, C_BG);
    snprintf(t, sizeof(t), s.miles ? "%.0f FT" : "%.0f M", s.miles ? d.climb * 3.28084f : d.climb);
    mtb_Kit_Text(140, 36, Terminal4x6, KIT_DIM, "CLIMB", C_BG); mtb_Kit_Text(140, 43, Terminal6x8, KIT_TEXT, t, C_BG);
    snprintf(t, sizeof(t), "%u", (unsigned)d.count);
    mtb_Kit_Text(204, 36, Terminal4x6, KIT_DIM, "ACTIVITIES", C_BG); mtb_Kit_Text(204, 43, Terminal6x8, KIT_TEXT, t, C_BG);
    // A bar a day.
    static const char* const DAY = "MTWTFSS";
    float most = 1;
    for (int k = 0; k < 7; k++) if (d.day[k] > most) most = d.day[k];
    int32_t offset;
    const uint32_t weekStart = week_Start(offset);
    const int today = (int)(((int64_t)time(nullptr) - weekStart) / 86400);
    for (int k = 0; k < 7; k++){
        const int bx = 140 + k * 16, by = 56, bh = 20;
        const int hh = (int)(bh * d.day[k] / most);
        mtb_Panel_Fill_Rect(bx, by, bx + 11, by + bh - 1, C_TRACK);
        if (hh) mtb_Panel_Fill_Rect(bx, by + bh - hh, bx + 11, by + bh - 1, C_STRAVA);
        char c[2] = {DAY[k], 0};
        mtb_Kit_Text_Centre(bx + 6, by + bh + 2, Terminal4x6, k == today ? KIT_TEXT : KIT_DIM, c, C_BG);
    }
    // The latest.
    mtb_Panel_Fill_Rect(0, 92, W - 1, 92, C_TRACK);
    if (!d.latest.at){ mtb_Kit_Text(4, 100, Terminal6x8, KIT_DIM, "NO ACTIVITIES YET", C_BG); return; }
    snprintf(t, sizeof(t), "%s", d.latest.name); mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, W - 60);
    mtb_Kit_Text(4, 97, Terminal6x8, KIT_TEXT, f, C_BG);
    const uint32_t now = (uint32_t)time(nullptr);
    char ago[20] = "";
    if (now > d.latest.at){ mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - d.latest.at)); mtb_Kit_Upper(ago); }
    mtb_Kit_Text_Right(W - 4, 98, Terminal4x6, KIT_DIM, ago, C_BG);
    char dist[16], tm[16], pace[24] = "";
    distance_Text(dist, sizeof(dist), d.latest.metres, s.miles, true);
    time_Text(tm, sizeof(tm), d.latest.seconds);
    const float unitM = s.miles ? 1609.344f : 1000.0f;
    if (d.latest.metres > 100 && d.latest.seconds){
        if (strstr(d.latest.type, "Ride")) snprintf(pace, sizeof(pace), "%.1f %s", d.latest.metres / unitM / (d.latest.seconds / 3600.0f), s.miles ? "MPH" : "KM/H");
        else if (!strcmp(d.latest.type, "Swim")) { const float per100 = d.latest.seconds / (d.latest.metres / 100.0f); snprintf(pace, sizeof(pace), "%d:%02d /100M", (int)per100 / 60, (int)per100 % 60); }
        else { const float per = d.latest.seconds / (d.latest.metres / unitM); snprintf(pace, sizeof(pace), "%d:%02d /%s", (int)per / 60, (int)per % 60, s.miles ? "MI" : "KM"); }
    }
    snprintf(t, sizeof(t), "%s   %s   %s", dist, tm, pace);
    mtb_Kit_Text(4, 110, Terminal8x12, C_STRAVA, t, C_BG);
    if (d.why[0]){ mtb_Kit_Fit(f, sizeof(f), Terminal4x6, d.why, 120); mtb_Kit_Text_Right(W - 4, 120, Terminal4x6, KIT_ORANGE, f, C_BG); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void myTraining_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Link, cmd_Unlink, cmd_Sport, cmd_Goal, cmd_Miles, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "mtNet", 16384, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Mt_Shared_t* d = (Mt_Shared_t*)heap_caps_calloc(1, sizeof(Mt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        if (redraw || ms - lastDraw >= 60000u){
            redraw = false;
            lastDraw = ms;
            Mt_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void mt_Values(JsonObject values){
    Mt_Save_t s;
    Mt_Shared_t* d = (Mt_Shared_t*)heap_caps_calloc(1, sizeof(Mt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); } else read_Save(s);
    const bool linked = mtb_Kit_OAuth_Is_Stored(TOKEN_KEY);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[96], a[16], b[16];
    if (linked){
        JsonObject o = rows.add<JsonObject>();
        if (!appOpen){ o["label"] = "Strava"; o["value"] = "Read while My Training is open"; o["state"] = "off"; }
        else if (!d->known){ o["label"] = "Strava"; o["value"] = d->why[0] ? d->why : "Reading"; o["state"] = d->why[0] ? "warn" : "info"; }
        else {
            distance_Text(a, sizeof(a), d->metres, s.miles, true);
            time_Text(b, sizeof(b), d->seconds);
            o["label"] = "This week";
            snprintf(t, sizeof(t), "%s in %s, %u activities", a, b, (unsigned)d->count);
            for (char* k = t; *k; k++) if (k[0] == ' ' && (k[1] == 'K' || k[1] == 'M') && (k[2] == 'M' || k[2] == 'I')){ k[1] = (char)(k[1] + 32); k[2] = (char)(k[2] + 32); }
            o["value"] = t;
            o["state"] = s.goal && d->metres >= s.goal * (s.miles ? 1609.344f : 1000.0f) ? "ok" : "info";
            if (d->why[0]){ o = rows.add<JsonObject>(); o["label"] = "Last check"; o["value"] = d->why; o["state"] = "warn"; }
        }
    }
    values["linked"] = linked ? 1 : 0;
    values["account"] = linked ? s.account : "";
    values["sport"] = s.sport; values["goal"] = s.goal; values["miles"] = s.miles;
    heap_caps_free(d);
}
