/**
 * ISS Tracker: the International Space Station from its orbit, worked out on Pixlpal (iss_Orbit.cpp).
 *   - The map: the world with day and night, the station and its last 45 minutes, its next 90, and the owner's place,
 *     with its height, speed and the next pass.
 *   - The passes: the next ones over the place for two days, when, how high, from where to where, which can be seen.
 *   - The next pass in the sky: its path across the owner's sky, looking up (north at the top), live while it passes.
 * The orbit (two-line elements) from wheretheiss.at twice a day, kept, so the station keeps moving offline. Knob: turn
 * or click for the page. A worker task fetches, searches places and works out the passes. NVS "issSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "iss_Orbit.h"
#include "iss_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define PAGES       3
#define NVS_KEY     "issSave"
#define SAVE_MAGIC  0x49535301u
#define UA          "Pixlpal-ISSTracker/1.0 (pixlpal.com)"
#define TLE_URL     "https://api.wheretheiss.at/v1/satellites/25544/tles?format=json"
#define TLE_EVERY   (12 * 3600)
#define PASSES_MAX  12
#define TRACK_BACK  45                  // minutes of track behind the station
#define TRACK_AHEAD 90                  // and ahead
#define TRACK_N     (TRACK_BACK + TRACK_AHEAD + 1)
#define SKY_N       48

EXT_RAM_BSS_ATTR TaskHandle_t issTracker_Task_H = NULL;
static void issTracker_App_Task(void* dApplication);
static void iss_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* issTracker_App;
Mtb_Applications* issTracker_App_GetInstance(){
    if (!issTracker_App){
        issTracker_App = new Mtb_Applications_FullScreen(issTracker_App_Task, &issTracker_Task_H, "ISS Tracker", {0,0}, 8192);
        issTracker_App->mtb_App_Set_Mobile_UI(ISS_UI_MANIFEST, ISS_UI_API, nullptr, iss_Values);
    }
    return issTracker_App;
}
MTB_REGISTER_APP(issTracker_App, 0, 0)      // the catalogue gives the app its number

struct Iss_Save_t {
    uint32_t magic;
    char place[64];
    double lat, lon;
    uint8_t page, cycle, visibleOnly, chime;
    char line1[72], line2[72];          // the last orbit fetched
    uint32_t tleAt;                     // when (unix)
};

struct Iss_Shared_t {
    Iss_Tle_t tle;
    Iss_Pass_t pass[PASSES_MAX];
    int passes;
    bool passesReady;
    uint32_t passesAt;                  // when they were worked out (unix)
    double passLat, passLon;            // for this place
    char why[44];
    uint32_t version;
    volatile bool stop, running, fetchNow, searchNow;
};

static EXT_RAM_BSS_ATTR Iss_Save_t save;
static EXT_RAM_BSS_ATTR Iss_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false;
static char searchQuery[64];
static volatile bool searchWanted = false;

static bool time_Valid(time_t t){ return t > 1700000000; }

static void read_Save(Iss_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC;
        snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
        s.lat = 6.45407; s.lon = 3.39467;
        s.chime = 1;
    }
    s.place[sizeof(s.place) - 1] = s.line1[sizeof(s.line1) - 1] = s.line2[sizeof(s.line2) - 1] = '\0';
    if (!(s.lat >= -90 && s.lat <= 90) || !(s.lon >= -180 && s.lon <= 180)){ s.lat = 6.45407; s.lon = 3.39467; }
    if (s.page >= PAGES) s.page = 0;
    if (s.cycle > 120) s.cycle = 0;
    s.visibleOnly = s.visibleOnly ? 1 : 0;
    s.chime = s.chime ? 1 : 0;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static void cmd_Search(JsonDocument& c){
    const char* q = c["query"] | "";
    const size_t n = strlen(q);
    if (n < 2 || n > 60 || searchWanted){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(searchQuery, sizeof(searchQuery), "%s", q); portEXIT_CRITICAL(&lock);
    searchWanted = true;
}
static void cmd_Place(JsonDocument& c){
    JsonVariant place = c["place"];
    const char* name = place["name"];
    const char* where = place["value"];
    double lat = NAN, lon = NAN;
    char* end = nullptr;
    if (where){ lat = strtod(where, &end); if (end && *end == ',') lon = strtod(end + 1, &end); }
    if (!name || !name[0] || !end || *end != '\0' || !(lat >= -90 && lat <= 90) || !(lon >= -180 && lon <= 180)){ mtb_Ble_App_Cmd_Reject(); return; }
    char ascii[64];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), name);
    portENTER_CRITICAL(&lock);
    snprintf(save.place, sizeof(save.place), "%s", ascii);
    save.lat = lat; save.lon = lon;
    portEXIT_CRITICAL(&lock);
    sh.searchNow = true;
    phoneChanged = saveWanted = true;
}
static void cmd_Page(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["page"], 0, PAGES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.page = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Cycle(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["cycle"], 0, 120, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.cycle = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Visible(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["visibleOnly"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.visibleOnly = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Chime(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["chime"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.chime = (uint8_t)v;
    phoneChanged = saveWanted = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) turns += 1; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static void do_Search(void){
    char q[64], encoded[160], url[256];
    portENTER_CRITICAL(&lock); memcpy(q, searchQuery, sizeof(q)); portEXIT_CRITICAL(&lock);
    mtb_Kit_Url_Encode(encoded, sizeof(encoded), q);
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=8&language=en&format=json", encoded);
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 32 * 1024, r, UA);
    JsonDocument doc(mtb_Kit_Json_Allocator());
    JsonDocument out;
    JsonArray results = out.to<JsonArray>();
    const char* error = nullptr;
    if (!body || r.code != 200 || deserializeJson(doc, body, r.length)) error = "Pixlpal could not search now";
    else for (JsonVariantConst p : doc["results"].as<JsonArrayConst>()){
        const char* name = p["name"] | "";
        if (!name[0] || !p["latitude"].is<double>() || !p["longitude"].is<double>()) continue;
        const char* country = p["country"] | (p["country_code"] | "");
        char label[96], where[40];
        snprintf(label, sizeof(label), "%s%s%s", name, country[0] ? ", " : "", country);
        snprintf(where, sizeof(where), "%.5f,%.5f", p["latitude"].as<double>(), p["longitude"].as<double>());
        JsonObject item = results.add<JsonObject>();
        item["name"] = label;
        item["value"] = where;
        item["detail"] = p["admin1"] | "";
    }
    heap_caps_free(body);
    if (issTracker_App) issTracker_App->mtb_App_Send_Search_Results("places", q, results, error);
}

static int fetch_Tle(char* l1, char* l2, size_t n){
    Mtb_Kit_Http_t r = {};
    const char* headers[] = {"Accept", "application/json", nullptr};
    char* body = mtb_Kit_Http_Get(TLE_URL, headers, nullptr, 0, 4096, r, UA);
    int code = r.code;
    if (code == 200 && body){
        JsonDocument doc;
        if (deserializeJson(doc, body, r.length)) code = -2;
        else {
            snprintf(l1, n, "%s", doc["line1"] | "");
            snprintf(l2, n, "%s", doc["line2"] | "");
            Iss_Tle_t t;
            if (!iss_Tle_Parse(l1, l2, t)) code = -2;
        }
    }
    heap_caps_free(body);
    return code;
}

static void worker(void*){
    static Iss_Pass_t found[PASSES_MAX];
    static Iss_Search_t search;
    uint32_t tryAt = 0;
    while (!sh.stop){
        if (searchWanted){ do_Search(); searchWanted = false; }
        const time_t now = time(nullptr);
        Iss_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        // The orbit: twice a day, or now when there is none; after a failure, again in 2 minutes.
        const bool stale = !s.line1[0] || (time_Valid(now) && (uint32_t)now - s.tleAt >= TLE_EVERY);
        if ((stale || sh.fetchNow) && (!tryAt || millis() - tryAt >= 120000u)){
            sh.fetchNow = false;
            tryAt = millis() ? millis() : 1;
            char l1[72], l2[72];
            const int code = fetch_Tle(l1, l2, sizeof(l1));
            portENTER_CRITICAL(&lock);
            if (code == 200){
                snprintf(save.line1, sizeof(save.line1), "%s", l1);
                snprintf(save.line2, sizeof(save.line2), "%s", l2);
                save.tleAt = (uint32_t)now;
                iss_Tle_Parse(l1, l2, sh.tle);
                sh.why[0] = '\0';
                sh.searchNow = true;
                tryAt = 0;
                saveWanted = true;
            } else if (!sh.tle.ok){
                if (code <= 0) snprintf(sh.why, sizeof(sh.why), "NO ANSWER FROM WHERETHEISS.AT");
                else snprintf(sh.why, sizeof(sh.why), "WHERETHEISS.AT: HTTP %d", code);
            }
            sh.version++;
            portEXIT_CRITICAL(&lock);
        }
        // The passes: for a new orbit or place, every half hour, and when the first one is over.
        bool redo = sh.searchNow;
        if (!redo && sh.passesReady && time_Valid(now)){
            if ((uint32_t)now - sh.passesAt >= 1800u) redo = true;
            else if (sh.passes && sh.pass[0].set < (int64_t)now) redo = true;
        }
        if (!sh.passesReady && sh.tle.ok) redo = true;
        if (redo && sh.tle.ok && time_Valid(now)){
            sh.searchNow = false;
            const Iss_Tle_t tle = sh.tle;
            iss_Search_Begin(search, tle, s.lat, s.lon, (int64_t)now, 48, 10.0, found, PASSES_MAX);
            while (!sh.stop && !sh.searchNow && iss_Search_Step(search, 20)) vTaskDelay(pdMS_TO_TICKS(2));
            if (!sh.stop && !sh.searchNow){
                portENTER_CRITICAL(&lock);
                memcpy(sh.pass, found, sizeof(found));
                sh.passes = search.count;
                sh.passesReady = true;
                sh.passesAt = (uint32_t)now;
                sh.passLat = s.lat; sh.passLon = s.lon;
                sh.version++;
                portEXIT_CRITICAL(&lock);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// The map ------------------------------------------------------------------------------------------------------------------------------
// The continents as rough outlines (longitude, latitude), as in Sky Tonight: a degree or two out at worst.
static const int16_t NAM[] = {-168,66, -162,70, -140,70, -128,70, -110,73, -95,72, -85,70, -80,63, -94,58, -90,57, -82,55, -79,52,
    -77,60, -65,60, -62,58, -55,52, -60,47, -66,44, -70,42, -76,38, -76,35, -81,31, -80,26, -82,25, -83,29, -90,30, -97,28, -97,22,
    -92,18, -88,21, -87,16, -83,10, -79,9, -80,7, -86,11, -92,14, -105,20, -110,24, -112,30, -115,32, -118,34, -124,40, -124,48,
    -130,55, -140,60, -150,60, -158,57, -165,60};
static const int16_t SAM[] = {-80,9, -75,11, -62,11, -52,5, -50,0, -45,-2, -35,-5, -35,-9, -39,-15, -41,-22, -48,-26, -53,-34, -58,-38,
    -62,-41, -65,-45, -68,-50, -70,-55, -74,-50, -73,-40, -71,-30, -70,-18, -76,-14, -81,-6, -80,0, -78,2, -77,8};
static const int16_t AFR[] = {-17,21, -16,15, -17,13, -12,8, -8,4, -2,5, 5,6, 9,4, 10,-1, 13,-6, 12,-12, 14,-22, 17,-29, 20,-35, 26,-34,
    32,-29, 35,-24, 40,-16, 40,-10, 39,-4, 43,2, 51,11, 43,12, 39,16, 35,24, 33,31, 25,32, 20,31, 11,33, 10,37, 3,37, -6,36, -10,31,
    -13,27};
static const int16_t EUA[] = {-10,36, -9,43, -2,44, -5,48, 0,50, 5,53, 8,55, 5,58, 5,62, 14,68, 20,70, 28,71, 40,67, 44,68, 60,70,
    70,73, 80,73, 100,78, 112,76, 130,72, 140,73, 160,70, 170,70, 180,68, 180,65, 178,62, 163,60, 160,55, 156,51, 150,59, 142,59,
    140,53, 135,44, 130,42, 127,39, 126,35, 121,31, 122,25, 117,23, 110,21, 106,18, 109,12, 105,9, 103,1, 100,6, 98,15, 94,17,
    92,22, 87,22, 80,15, 77,8, 73,17, 72,21, 66,25, 61,25, 57,26, 55,25, 56,22, 52,17, 44,12, 43,15, 39,21, 35,28, 34,31, 36,36,
    30,36, 26,38, 23,36, 22,40, 19,42, 13,45, 16,41, 18,40, 12,44, 9,44, 3,43, -1,38, -6,36};
static const int16_t AUS[] = {113,-22, 114,-26, 115,-34, 118,-35, 124,-34, 131,-31, 135,-34, 138,-35, 141,-38, 146,-39, 150,-37,
    153,-31, 153,-25, 146,-19, 145,-15, 142,-11, 141,-17, 136,-12, 130,-12, 126,-14, 122,-18};
static const int16_t GRL[] = {-73,78, -60,82, -30,83, -20,80, -20,72, -25,70, -40,65, -43,60, -50,62, -55,67, -58,75};
static const int16_t GBR[] = {-5,50, 1,51, 1,53, -2,56, -3,58, -6,58, -5,55, -3,54, -5,52};
static const int16_t JPN[] = {130,31, 132,34, 136,35, 140,36, 141,40, 142,45, 140,42, 137,37, 132,35, 130,33};
static const int16_t BOR[] = {109,1, 117,7, 119,5, 118,1, 116,-4, 110,-3};
static const int16_t SUM[] = {95,5, 98,4, 106,-6, 102,-4};
static const int16_t MDG[] = {44,-25, 47,-25, 50,-15, 49,-12, 44,-17};
static const int16_t NZL[] = {172,-34, 178,-38, 174,-41, 167,-46, 170,-46, 174,-39};
static const int16_t NGN[] = {131,-1, 141,-3, 150,-6, 147,-8, 141,-9, 138,-8, 132,-4};
static const int16_t ANT[] = {-180,-70, 180,-70, 180,-90, -180,-90};
struct Iss_Land_t { const int16_t* p; int n; };
static const Iss_Land_t LANDS[] = {{NAM, sizeof(NAM) / 4}, {SAM, sizeof(SAM) / 4}, {AFR, sizeof(AFR) / 4}, {EUA, sizeof(EUA) / 4},
    {AUS, sizeof(AUS) / 4}, {GRL, sizeof(GRL) / 4}, {GBR, sizeof(GBR) / 4}, {JPN, sizeof(JPN) / 4}, {BOR, sizeof(BOR) / 4},
    {SUM, sizeof(SUM) / 4}, {MDG, sizeof(MDG) / 4}, {NZL, sizeof(NZL) / 4}, {NGN, sizeof(NGN) / 4}, {ANT, sizeof(ANT) / 4}};

#define MAP_Y       10                  // under the top line
#define MAP_H       118
#define ROW_BYTES   (W / 8)
static uint8_t* landMask = nullptr;     // MAP_H rows of W bits
static uint8_t* layer[3] = {nullptr};   // sea at night, land by day, land at night
static uint32_t layersAt = 0;           // when the night was drawn (unix / 60)

static int map_X(double lon){ int x = (int)lround((lon + 180.0) * (W - 1) / 360.0); return x < 0 ? 0 : x > W - 1 ? W - 1 : x; }
static int map_Y(double lat){ return MAP_Y + (int)lround((90.0 - lat) * (MAP_H - 1) / 180.0); }

static void make_Land(void){
    if (landMask) return;
    landMask = (uint8_t*)heap_caps_calloc(MAP_H, ROW_BYTES, MALLOC_CAP_SPIRAM);
    if (!landMask) return;
    for (int row = 0; row < MAP_H; row++){
        const double lat = 90.0 - (row + 0.5) * 180.0 / MAP_H;
        for (const Iss_Land_t& l : LANDS){
            double xs[64];
            int nx = 0;
            for (int i = 0; i < l.n && nx < 64; i++){
                const int j = (i + 1) % l.n;
                const double lo0 = l.p[2 * i], la0 = l.p[2 * i + 1], lo1 = l.p[2 * j], la1 = l.p[2 * j + 1];
                if ((la0 <= lat) == (la1 <= lat)) continue;
                xs[nx++] = lo0 + (lat - la0) * (lo1 - lo0) / (la1 - la0);
            }
            for (int a = 1; a < nx; a++){ const double v = xs[a]; int b = a - 1; while (b >= 0 && xs[b] > v){ xs[b + 1] = xs[b]; b--; } xs[b + 1] = v; }
            for (int k = 0; k + 1 < nx; k += 2){
                const int c0 = map_X(xs[k]), c1 = map_X(xs[k + 1]);
                for (int c = c0; c <= c1; c++) landMask[row * ROW_BYTES + c / 8] |= (uint8_t)(1 << (c % 8));
            }
        }
    }
}

/** The night side for the time: for each column, the rows where the sun is down, into the three layers. */
static void make_Night(time_t now){
    for (int k = 0; k < 3; k++){
        if (!layer[k]) layer[k] = (uint8_t*)heap_caps_calloc(MAP_H, ROW_BYTES, MALLOC_CAP_SPIRAM);
        if (!layer[k]) return;
    }
    double sLat, sLon;
    iss_Sub_Solar((double)now, sLat, sLon);
    const double RAD = M_PI / 180.0;
    // The sun's height: sin(lat) sin(sunLat) + cos(lat) cos(sunLat) cos(lon - sunLon). The last part a column's only.
    static float colCos[W];
    for (int c = 0; c < W; c++) colCos[c] = (float)cos((-180.0 + (c + 0.5) * 360.0 / W - sLon) * RAD);
    const float ss = (float)sin(sLat * RAD), cs = (float)cos(sLat * RAD);
    for (int row = 0; row < MAP_H; row++){
        const double lat = (90.0 - (row + 0.5) * 180.0 / MAP_H) * RAD;
        const float a = (float)sin(lat) * ss, b = (float)cos(lat) * cs;
        uint8_t* sea = layer[0] + row * ROW_BYTES;
        uint8_t* day = layer[1] + row * ROW_BYTES;
        uint8_t* night = layer[2] + row * ROW_BYTES;
        memset(sea, 0, ROW_BYTES); memset(day, 0, ROW_BYTES); memset(night, 0, ROW_BYTES);
        for (int c = 0; c < W; c++){
            const float alt = a + b * colCos[c];
            const bool land = landMask && (landMask[row * ROW_BYTES + c / 8] >> (c % 8)) & 1;
            const uint8_t bit = (uint8_t)(1 << (c % 8));
            if (alt < -0.0145){ if (land) night[c / 8] |= bit; else sea[c / 8] |= bit; }     // the sun below -0.83 degrees
            else if (land) day[c / 8] |= bit;
        }
    }
    layersAt = (uint32_t)(now / 60);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 8, 20), C_SEA = MTB_KIT_RGB(10, 34, 70), C_SEA_NIGHT = MTB_KIT_RGB(4, 12, 30);
static const uint16_t C_LAND = MTB_KIT_RGB(34, 92, 52), C_LAND_NIGHT = MTB_KIT_RGB(14, 36, 24), C_BAR = MTB_KIT_RGB(8, 12, 28);

static void rect(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }

static void lat_Lon_Text(char* t, size_t n, double lat, double lon){
    snprintf(t, n, "%.1f%c %.1f%c", fabs(lat), lat >= 0 ? 'N' : 'S', fabs(lon), lon >= 0 ? 'E' : 'W');
}

static void clock_Of(char* t, size_t n, int64_t when){
    const time_t tt = (time_t)when;
    struct tm l;
    localtime_r(&tt, &l);
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", l.tm_hour, l.tm_min);
    else snprintf(t, n, "%d:%02d%s", (l.tm_hour + 11) % 12 + 1, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
}

static void day_Of(char* t, size_t n, int64_t when, time_t now){
    static const char* const WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const time_t tt = (time_t)when;
    struct tm l, ln;
    localtime_r(&tt, &l);
    localtime_r(&now, &ln);
    if (l.tm_yday == ln.tm_yday && l.tm_year == ln.tm_year) snprintf(t, n, "TODAY");
    else {
        const time_t tomorrow = now + 86400;
        struct tm lt;
        localtime_r(&tomorrow, &lt);
        if (l.tm_yday == lt.tm_yday && l.tm_year == lt.tm_year) snprintf(t, n, "TOMORROW");
        else snprintf(t, n, "%s %d", WD[l.tm_wday], l.tm_mday);
    }
}

static void in_Text(char* t, size_t n, int64_t secs){
    if (secs < 60) snprintf(t, n, "IN %dS", (int)(secs < 0 ? 0 : secs));
    else if (secs < 3600) snprintf(t, n, "IN %dM %02dS", (int)(secs / 60), (int)(secs % 60));
    else snprintf(t, n, "IN %dH %02dM", (int)(secs / 3600), (int)(secs / 60 % 60));
}

/** The first pass to show from now (one under way counts), honouring "only visible"; -1 if none. */
static int next_Pass(const Iss_Shared_t& d, const Iss_Save_t& s, time_t now){
    for (int i = 0; i < d.passes; i++){
        if (d.pass[i].set < (int64_t)now) continue;
        if (s.visibleOnly && !d.pass[i].visible) continue;
        return i;
    }
    return -1;
}

static void waiting(const Iss_Shared_t& d, const char* what){
    mtb_Panel_Fill_Screen(C_BG);
    mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : what, C_BG);
}

struct Iss_Track_t { int16_t x[TRACK_N], y[TRACK_N]; uint32_t at; bool ok; };
static EXT_RAM_BSS_ATTR Iss_Track_t track;

static void page_Map(const Iss_Shared_t& d, const Iss_Save_t& s, time_t now){
    make_Land();
    if (!layer[0] || layersAt != (uint32_t)(now / 60) / 2 * 2) make_Night(now - now % 120);
    mtb_Panel_Fill_Screen(C_BG);
    rect(0, MAP_Y, W - 1, MAP_Y + MAP_H - 1, C_SEA);
    if (layer[0]){
        mtb_Panel_Draw_Mono_Fg(0, MAP_Y, W, MAP_H, layer[0], ROW_BYTES, 0, C_SEA_NIGHT);
        mtb_Panel_Draw_Mono_Fg(0, MAP_Y, W, MAP_H, layer[1], ROW_BYTES, 0, C_LAND);
        mtb_Panel_Draw_Mono_Fg(0, MAP_Y, W, MAP_H, layer[2], ROW_BYTES, 0, C_LAND_NIGHT);
    }
    // The owner's place.
    const int ox = map_X(s.lon), oy = map_Y(s.lat);
    mtb_Panel_Draw_HLine((int16_t)oy, (int16_t)(ox - 3), (int16_t)(ox + 3), KIT_CYAN);
    mtb_Panel_Draw_VLine((int16_t)ox, (int16_t)(oy - 3), (int16_t)(oy + 3), KIT_CYAN);
    if (!d.tle.ok || !time_Valid(now)){
        rect(48, 56, 208, 72, C_BG);
        mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL,
                            !time_Valid(now) ? "WAITING FOR THE TIME..." : d.why[0] ? d.why : "FETCHING THE ORBIT...", C_BG);
        return;
    }
    // The track: a minute apart, behind dim, ahead bright and dotted; none across the date line.
    if (!track.ok || track.at != (uint32_t)(now / 60)){
        for (int i = 0; i < TRACK_N; i++){
            Iss_State_t p;
            iss_State_At(d.tle, (double)(now - now % 60 + (i - TRACK_BACK) * 60), p);
            track.x[i] = (int16_t)map_X(p.lon); track.y[i] = (int16_t)map_Y(p.lat);
        }
        track.at = (uint32_t)(now / 60);
        track.ok = true;
    }
    for (int i = 1; i < TRACK_N; i++){
        if (abs(track.x[i] - track.x[i - 1]) > W / 2) continue;
        const bool ahead = i > TRACK_BACK;
        if (ahead && (i / 2) % 2) continue;
        mtb_Panel_Draw_Line(track.x[i - 1], track.y[i - 1], track.x[i], track.y[i],
                            ahead ? MTB_KIT_RGB(255, 220, 120) : mtb_Kit_Dim(KIT_TEXT, (uint8_t)(50 + 150 * i / TRACK_BACK)));
    }
    Iss_State_t st;
    iss_State_At(d.tle, (double)now, st);
    const int sx = map_X(st.lon), sy = map_Y(st.lat);
    const uint16_t wing = st.sunlit ? KIT_GOLD : MTB_KIT_RGB(110, 110, 150);
    rect(sx - 1, sy - 1, sx + 1, sy + 1, KIT_TEXT);
    rect(sx - 6, sy - 2, sx - 3, sy + 2, wing);
    rect(sx + 3, sy - 2, sx + 6, sy + 2, wing);
    // The top line: where it is, how high and fast.
    char t[96], ll[32];
    rect(0, 0, W - 1, MAP_Y - 1, C_BAR);
    lat_Lon_Text(ll, sizeof(ll), st.lat, st.lon);
    snprintf(t, sizeof(t), "ISS  %s", ll);
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_TEXT, t, C_BAR);
    snprintf(t, sizeof(t), "%d KM UP  %d,%03d KM/H", (int)lround(st.alt), (int)st.speed / 1000, (int)st.speed % 1000);
    mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, st.sunlit ? KIT_GOLD : KIT_LABEL, t, C_BAR);
    // The bottom: the next pass, or the one under way.
    const int k = next_Pass(d, s, now);
    if (d.passesReady){
        if (k >= 0 && d.pass[k].rise <= (int64_t)now){
            double el, az, range;
            iss_Look(st, s.lat, s.lon, el, az, range);
            snprintf(t, sizeof(t), "OVERHEAD NOW: %d DEG %s, %d KM AWAY%s", (int)lround(el > 0 ? el : 0), iss_Compass(az), (int)range,
                     d.pass[k].visible ? "  LOOK UP!" : "");
        } else if (k >= 0){
            char c[12], in[20];
            clock_Of(c, sizeof(c), d.pass[k].rise);
            in_Text(in, sizeof(in), d.pass[k].rise - (int64_t)now);
            snprintf(t, sizeof(t), "NEXT PASS %s %s  %d DEG%s", c, in, (int)lround(d.pass[k].maxEl), d.pass[k].visible ? "  VISIBLE" : "");
        } else snprintf(t, sizeof(t), s.visibleOnly ? "NO PASS YOU CAN SEE IN 2 DAYS" : "NO PASS IN 2 DAYS");
        const int tw = mtb_Kit_Text_Width(Terminal4x6, t);
        rect(0, H - 8, tw + 4, H - 1, C_BAR);
        mtb_Kit_Text(2, H - 7, Terminal4x6, k >= 0 && d.pass[k].visible ? KIT_GOLD : KIT_TEXT, t, C_BAR);
    }
}

static void page_Passes(const Iss_Shared_t& d, const Iss_Save_t& s, time_t now){
    mtb_Panel_Fill_Screen(C_BG);
    char t[96], f[64];
    snprintf(t, sizeof(t), "PASSES OVER %s", s.place);
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, W - 8);
    mtb_Kit_Text(4, 2, Terminal6x8, KIT_TEXT, f, C_BG);
    mtb_Panel_Draw_HLine(12, 0, W - 1, KIT_LINE);
    if (!d.tle.ok || !time_Valid(now) || !d.passesReady){
        mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "WORKING OUT THE PASSES...", C_BG);
        return;
    }
    mtb_Kit_Text(4, 16, Terminal4x6, KIT_DIM, "DAY", C_BG);
    mtb_Kit_Text(62, 16, Terminal4x6, KIT_DIM, "RISES", C_BG);
    mtb_Kit_Text(104, 16, Terminal4x6, KIT_DIM, "MIN", C_BG);
    mtb_Kit_Text(126, 16, Terminal4x6, KIT_DIM, "HIGH", C_BG);
    mtb_Kit_Text(156, 16, Terminal4x6, KIT_DIM, "FROM > TO", C_BG);
    int row = 0;
    for (int i = 0; i < d.passes && row < 8; i++){
        const Iss_Pass_t& p = d.pass[i];
        if (p.set < (int64_t)now || (s.visibleOnly && !p.visible)) continue;
        const int y = 25 + row * 12;
        const bool now_ = p.rise <= (int64_t)now;
        const uint16_t c = now_ ? KIT_GREEN : p.visible ? KIT_GOLD : KIT_LABEL;
        if (now_) rect(0, y - 2, W - 1, y + 9, MTB_KIT_RGB(10, 40, 20));
        const uint16_t bg = now_ ? MTB_KIT_RGB(10, 40, 20) : C_BG;
        day_Of(t, sizeof(t), p.rise, now);
        mtb_Kit_Text(4, y, Terminal6x8, c, now_ ? "NOW" : t, bg);
        clock_Of(t, sizeof(t), p.rise);
        mtb_Kit_Text(62, y, Terminal6x8, KIT_TEXT, t, bg);
        snprintf(t, sizeof(t), "%d", (int)((p.set - p.rise + 30) / 60));
        mtb_Kit_Text(106, y, Terminal6x8, KIT_TEXT, t, bg);
        snprintf(t, sizeof(t), "%d", (int)lround(p.maxEl));
        mtb_Kit_Text(128, y, Terminal6x8, p.maxEl >= 40 ? KIT_GREEN : KIT_TEXT, t, bg);
        snprintf(t, sizeof(t), "%s > %s", iss_Compass(p.riseAz), iss_Compass(p.setAz));
        mtb_Kit_Text(156, y, Terminal6x8, KIT_TEXT, t, bg);
        if (p.visible) mtb_Kit_Text_Right(W - 3, y + 1, Terminal4x6, KIT_GOLD, "SEE IT", bg);
        row++;
    }
    if (!row) mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_LABEL, s.visibleOnly ? "NO PASS YOU CAN SEE IN 2 DAYS" : "NO PASS IN THE NEXT 2 DAYS", C_BG);
}

// The next pass across the sky: a circle looking up, north at the top, east on the left (as on a star chart).
static void sky_Point(double el, double az, int cx, int cy, int r, int& x, int& y){
    const double k = (90.0 - (el < 0 ? 0 : el)) / 90.0 * r, a = az * M_PI / 180.0;
    x = cx - (int)lround(k * sin(a));
    y = cy - (int)lround(k * cos(a));
}

static void page_Sky(const Iss_Shared_t& d, const Iss_Save_t& s, time_t now){
    mtb_Panel_Fill_Screen(C_BG);
    const int cx = 62, cy = 64, r = 56;
    mtb_Panel_Draw_Circle(cx, cy, r, MTB_KIT_RGB(70, 70, 90));
    mtb_Panel_Draw_Circle(cx, cy, r * 2 / 3, KIT_LINE);
    mtb_Panel_Draw_Circle(cx, cy, r / 3, KIT_LINE);
    mtb_Kit_Text_Centre(cx, cy - r - 7, Terminal4x6, KIT_LABEL, "N", C_BG);
    mtb_Kit_Text_Centre(cx, cy + r + 2, Terminal4x6, KIT_LABEL, "S", C_BG);
    mtb_Kit_Text(cx - r - 6, cy - 2, Terminal4x6, KIT_LABEL, "E", C_BG);
    mtb_Kit_Text(cx + r + 3, cy - 2, Terminal4x6, KIT_LABEL, "W", C_BG);
    const int k = (d.tle.ok && d.passesReady && time_Valid(now)) ? next_Pass(d, s, now) : -1;
    const int tx = 132;
    if (k < 0){
        mtb_Kit_Text(tx, 54, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, !d.passesReady ? "WORKING IT OUT..." : "NO PASS SOON", C_BG);
        return;
    }
    const Iss_Pass_t& p = d.pass[k];
    // The path, a point every few seconds, brighter towards its end; the station on it while it passes.
    int px = 0, py = 0;
    for (int i = 0; i <= SKY_N; i++){
        const double t = (double)p.rise + (double)(p.set - p.rise) * i / SKY_N;
        Iss_State_t st;
        iss_State_At(d.tle, t, st);
        double el, az, range;
        iss_Look(st, s.lat, s.lon, el, az, range);
        int x, y;
        sky_Point(el, az, cx, cy, r, x, y);
        if (i) mtb_Panel_Draw_Line((int16_t)px, (int16_t)py, (int16_t)x, (int16_t)y, st.sunlit ? KIT_GOLD : MTB_KIT_RGB(90, 90, 130));
        px = x; py = y;
    }
    int x, y;
    double el, az, range;
    Iss_State_t st;
    iss_State_At(d.tle, (double)p.rise, st); iss_Look(st, s.lat, s.lon, el, az, range); sky_Point(el, az, cx, cy, r, x, y);
    mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 2, KIT_GREEN);
    iss_State_At(d.tle, (double)p.set, st); iss_Look(st, s.lat, s.lon, el, az, range); sky_Point(el, az, cx, cy, r, x, y);
    mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 2, KIT_RED);
    const bool under = p.rise <= (int64_t)now;
    if (under){
        iss_State_At(d.tle, (double)now, st); iss_Look(st, s.lat, s.lon, el, az, range); sky_Point(el, az, cx, cy, r, x, y);
        mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 3, KIT_TEXT);
    }
    // The facts.
    char t[64], c[12];
    day_Of(t, sizeof(t), p.rise, now);
    mtb_Kit_Text(tx, 4, Terminal6x8, KIT_LABEL, under ? "PASSING NOW" : "NEXT PASS", C_BG);
    if (!under){ clock_Of(c, sizeof(c), p.rise); char line[40]; snprintf(line, sizeof(line), "%s %s", t, c); mtb_Kit_Text(tx, 14, Terminal8x12, KIT_TEXT, line, C_BG); }
    else { snprintf(t, sizeof(t), "%d DEG %s", (int)lround(el > 0 ? el : 0), iss_Compass(az)); mtb_Kit_Text(tx, 14, Terminal8x12, KIT_GREEN, t, C_BG); }
    if (!under){ in_Text(t, sizeof(t), p.rise - (int64_t)now); mtb_Kit_Text(tx, 30, Terminal6x8, KIT_CYAN, t, C_BG); }
    clock_Of(c, sizeof(c), p.rise);
    snprintf(t, sizeof(t), "RISES %s %s", c, iss_Compass(p.riseAz));
    mtb_Kit_Text(tx, 46, Terminal4x6, KIT_GREEN, t, C_BG);
    clock_Of(c, sizeof(c), p.top);
    snprintf(t, sizeof(t), "HIGHEST %s %d DEG %s", c, (int)lround(p.maxEl), iss_Compass(p.topAz));
    mtb_Kit_Text(tx, 56, Terminal4x6, KIT_TEXT, t, C_BG);
    clock_Of(c, sizeof(c), p.set);
    snprintf(t, sizeof(t), "SETS %s %s", c, iss_Compass(p.setAz));
    mtb_Kit_Text(tx, 66, Terminal4x6, KIT_RED, t, C_BG);
    snprintf(t, sizeof(t), "%d MINUTES IN YOUR SKY", (int)((p.set - p.rise + 30) / 60));
    mtb_Kit_Text(tx, 80, Terminal4x6, KIT_LABEL, t, C_BG);
    if (p.visible){
        mtb_Kit_Text(tx, 96, Terminal6x8, KIT_GOLD, "YOU CAN SEE IT", C_BG);
        mtb_Kit_Text(tx, 106, Terminal4x6, KIT_DIM, "A BRIGHT STAR, MOVING", C_BG);
    } else mtb_Kit_Text(tx, 96, Terminal4x6, KIT_DIM, p.maxEl > 0 ? "TOO BRIGHT OUT TO SEE" : "", C_BG);
    mtb_Kit_Text(tx, H - 8, Terminal4x6, KIT_DIM, "LOOKING UP: EAST ON THE LEFT", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void issTracker_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Search, cmd_Place, cmd_Page, cmd_Cycle, cmd_Visible, cmd_Chime);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = saveWanted = searchWanted = false;
    memset(&sh, 0, sizeof(sh));
    memset(&track, 0, sizeof(track));
    if (save.line1[0]) iss_Tle_Parse(save.line1, save.line2, sh.tle);
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "issNet", 16384, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Iss_Shared_t* d = (Iss_Shared_t*)heap_caps_calloc(1, sizeof(Iss_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0, pageMs = millis();
    int64_t chimedFor = 0;
    bool redraw = true, push = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const time_t now = time(nullptr);
        if (phoneChanged){ phoneChanged = false; redraw = push = true; pageMs = ms; }
        const int t = turns;
        if (t){ turns -= t; save.page = (uint8_t)(((int)save.page + t % PAGES + PAGES) % PAGES); redraw = true; pageMs = ms; push = true; }
        if (save.cycle && ms - pageMs >= save.cycle * 1000u){ save.page = (uint8_t)((save.page + 1) % PAGES); pageMs = ms; redraw = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        // A chime five minutes before a pass that can be seen.
        if (save.chime && d->passesReady && time_Valid(now)){
            for (int i = 0; i < d->passes; i++){
                const Iss_Pass_t& p = d->pass[i];
                if (!p.visible || p.rise == chimedFor) continue;
                const int64_t lead = p.rise - (int64_t)now;
                if (lead <= 300 && lead > 240){
                    static const Snd_Note_t CHIME[] = {{784, 120}, {988, 120}, {1175, 240}};
                    snd_Play(CHIME, 3, 0.25f, false);
                    chimedFor = p.rise;
                }
            }
        }
        if (redraw || ms - lastDraw >= 1000u){
            redraw = false;
            lastDraw = ms;
            Iss_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            if (s.page == 0) page_Map(*d, s, now);
            else if (s.page == 1) page_Passes(*d, s, now);
            else page_Sky(*d, s, now);
            mtb_Panel_End_Update();
        }
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push && ms - pushedMs >= 3000) || ms - pushedMs >= 60000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    for (int k = 0; k < 3; k++){ heap_caps_free(layer[k]); layer[k] = nullptr; }
    heap_caps_free(landMask); landMask = nullptr;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void iss_Values(JsonObject values){
    Iss_Save_t s;
    Iss_Shared_t* d = (Iss_Shared_t*)heap_caps_calloc(1, sizeof(Iss_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(d, &sh, sizeof(sh)); portEXIT_CRITICAL(&lock); }
    else { read_Save(s); if (s.line1[0]) iss_Tle_Parse(s.line1, s.line2, d->tle); }
    const time_t now = time(nullptr);
    char t[96], ll[32];
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Place";
    o["value"] = s.place;
    o["state"] = "info";
    o = rows.add<JsonObject>();
    o["label"] = "The station";
    if (!d->tle.ok || !time_Valid(now)){ o["value"] = d->why[0] ? d->why : "Fetching its orbit"; o["state"] = d->why[0] ? "warn" : "off"; }
    else {
        Iss_State_t st;
        iss_State_At(d->tle, (double)now, st);
        lat_Lon_Text(ll, sizeof(ll), st.lat, st.lon);
        snprintf(t, sizeof(t), "%s, %d km up, %s", ll, (int)lround(st.alt), st.sunlit ? "in sunlight" : "in the earth's shadow");
        o["value"] = t; o["state"] = "ok";
        if ((int64_t)now - d->tle.epoch > 14 * 86400){
            o = rows.add<JsonObject>();
            o["label"] = "Orbit";
            o["value"] = "Over two weeks old: Pixlpal could not fetch a new one";
            o["state"] = "warn";
        }
    }
    JsonArray passes = values["passes"].to<JsonArray>();
    if (appOpen && d->passesReady && time_Valid(now)){
        int n = 0;
        for (int i = 0; i < d->passes && n < 5; i++){
            const Iss_Pass_t& p = d->pass[i];
            if (p.set < (int64_t)now || (s.visibleOnly && !p.visible)) continue;
            char day[16], c[12];
            day_Of(day, sizeof(day), p.rise, now);
            clock_Of(c, sizeof(c), p.rise);
            JsonObject r = passes.add<JsonObject>();
            snprintf(t, sizeof(t), "%s %s", day, c);
            r["label"] = t;
            snprintf(t, sizeof(t), "%d degrees high, %s to %s, %d min%s", (int)lround(p.maxEl), iss_Compass(p.riseAz), iss_Compass(p.setAz),
                     (int)((p.set - p.rise + 30) / 60), p.visible ? ", you can see it" : "");
            r["value"] = t;
            r["state"] = p.rise <= (int64_t)now ? "ok" : p.visible ? "warn" : "info";
            n++;
        }
    }
    values["page"] = s.page;
    values["cycle"] = s.cycle;
    values["visibleOnly"] = s.visibleOnly;
    values["chime"] = s.chime;
    heap_caps_free(d);
}
