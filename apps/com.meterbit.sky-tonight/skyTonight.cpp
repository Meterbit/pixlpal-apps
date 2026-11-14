/**
 * Sky Tonight, ID {3,5} (WEATHER & SKY): three pages for a place found from the phone.
 *   - Sun and moon: the sun's height through the day with sunrise, sunset and day length; the moon's phase drawn as
 *     it looks (mirrored south of the equator), its rise and set, and the next full moon. Worked out on Pixlpal.
 *   - The International Space Station now: on a latitude/longitude grid with its last ten minutes, how far it is from
 *     the place, its speed and height, in sunlight or in the earth's shadow (wheretheiss.at).
 *   - Aurora: the planetary K index (NOAA SWPC), its last three hours, and the chance at the place.
 * Knob: turn for the page. A worker task makes the requests and the place searches. NVS "skySave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "sky_Astro.h"
#include "sky_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define TAG             "SKY"
#define W               256
#define H               128
#define PAGES           3
#define TRAIL           60
#define KP_POINTS       36
#define NVS_KEY         "skySave"
#define SAVE_MAGIC      0x534B5901u
#define UA              "Pixlpal-SkyTonight/1.0"
#define ISS_URL         "https://api.wheretheiss.at/v1/satellites/25544"
#define KP_URL          "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json"

EXT_RAM_BSS_ATTR TaskHandle_t skyTonight_Task_H = NULL;
static void skyTonight_App_Task(void* dApplication);
static void sky_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* skyTonight_App;
Mtb_Applications* skyTonight_App_GetInstance(){
    if (!skyTonight_App){
        skyTonight_App = new Mtb_Applications_FullScreen(skyTonight_App_Task, &skyTonight_Task_H, "Sky Tonight", {3,5}, 8192);
        skyTonight_App->mtb_App_Set_Mobile_UI(SKY_UI_MANIFEST, SKY_UI_API, nullptr, sky_Values);
    }
    return skyTonight_App;
}
MTB_REGISTER_APP(skyTonight_App, 3, 5)

struct Sky_Save_t {
    uint32_t magic;
    char place[64];
    double lat, lon;
    uint8_t page, cycle;                // cycle: seconds a page (0 stays)
    uint8_t pad[2];
};

struct Sky_Net_t {
    // The space station.
    bool issOk;
    float issLat, issLon, issAlt, issSpeed;
    bool issSunlit;
    uint32_t issAt;
    float trailLat[TRAIL], trailLon[TRAIL];
    uint8_t trailN;
    // The K index.
    bool kpOk;
    float kp;
    float kpHist[KP_POINTS];
    uint8_t kpN;
    uint32_t kpAt;
    uint32_t version;
};

static EXT_RAM_BSS_ATTR Sky_Save_t save;
static EXT_RAM_BSS_ATTR Sky_Net_t net;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, workerStop = false, workerRunning = false;
static volatile uint8_t pageNow = 0;
static char searchQuery[64];
static volatile bool searchWanted = false;

static void read_Save(Sky_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC;
        snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
        s.lat = 6.45407; s.lon = 3.39467;
        s.cycle = 20;
    }
    s.place[sizeof(s.place) - 1] = '\0';
    if (!(s.lat >= -90 && s.lat <= 90) || !(s.lon >= -180 && s.lon <= 180)){ s.lat = 6.45407; s.lon = 3.39467; }
    if (s.page >= PAGES) s.page = 0;
    if (s.cycle > 120) s.cycle = 20;
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
    phoneChanged = true;
}
static void cmd_Page(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["page"], 0, PAGES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.page = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Cycle(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["cycle"], 0, 120, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.cycle = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
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
    if (!body || r.code != 200 || deserializeJson(doc, body)) error = "Pixlpal could not search now";
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
    if (skyTonight_App) skyTonight_App->mtb_App_Send_Search_Results("places", q, results, error);
}

static void get_Iss(void){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(ISS_URL, nullptr, nullptr, 0, 4096, r, UA);
    JsonDocument doc(mtb_Kit_Json_Allocator());
    const bool ok = body && r.code == 200 && !deserializeJson(doc, body) && doc["latitude"].is<double>();
    heap_caps_free(body);
    portENTER_CRITICAL(&lock);
    net.issOk = ok;
    if (ok){
        net.issLat = doc["latitude"].as<float>();
        net.issLon = doc["longitude"].as<float>();
        net.issAlt = doc["altitude"] | 0.0f;
        net.issSpeed = doc["velocity"] | 0.0f;
        net.issSunlit = !strcmp(doc["visibility"] | "", "daylight");
        net.issAt = (uint32_t)time(nullptr);
        if (net.trailN == TRAIL){ memmove(net.trailLat, net.trailLat + 1, sizeof(float) * (TRAIL - 1)); memmove(net.trailLon, net.trailLon + 1, sizeof(float) * (TRAIL - 1)); net.trailN--; }
        net.trailLat[net.trailN] = net.issLat; net.trailLon[net.trailN] = net.issLon; net.trailN++;
    }
    net.version++;
    portEXIT_CRITICAL(&lock);
}

static void get_Kp(void){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(KP_URL, nullptr, nullptr, 0, 400 * 1024, r, UA);
    JsonDocument filter;
    filter[0]["estimated_kp"] = true;
    filter[0]["kp_index"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    bool ok = body && r.code == 200 && !deserializeJson(doc, body, DeserializationOption::Filter(filter));
    heap_caps_free(body);
    float hist[KP_POINTS];
    int n = 0;
    float kp = 0;
    if (ok){
        JsonArrayConst a = doc.as<JsonArrayConst>();
        const int total = (int)a.size();
        ok = total > 0;
        // One point every 5 minutes over the last three hours (the list is a minute apart, oldest first).
        for (int i = total - 1 - (KP_POINTS - 1) * 5; i < total && n < KP_POINTS; i += 5){
            if (i < 0) continue;
            JsonVariantConst e = a[i];
            hist[n++] = e["estimated_kp"].is<float>() ? e["estimated_kp"].as<float>() : (float)(e["kp_index"] | 0);
        }
        if (ok){
            JsonVariantConst last = a[total - 1];
            kp = last["estimated_kp"].is<float>() ? last["estimated_kp"].as<float>() : (float)(last["kp_index"] | 0);
        }
    }
    portENTER_CRITICAL(&lock);
    if (ok){
        net.kpOk = true;
        net.kp = kp;
        memcpy(net.kpHist, hist, sizeof(float) * n);
        net.kpN = (uint8_t)n;
        net.kpAt = (uint32_t)time(nullptr);
    } else if (!net.kpAt) net.kpOk = false;
    net.version++;
    portEXIT_CRITICAL(&lock);
}

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 200 ms. Its state lives in these statics between steps.
static uint32_t nextIss;
static uint32_t nextKp;
static int kpFails;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    nextIss = 0;
    nextKp = 0;
    kpFails = 0;
}

static void worker_End(void){
    workerRunning = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!workerRunning) return;
    if (workerStop){ worker_End(); return; }
    if (millis() - wk_StepAt < 200) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {
        if (searchWanted){ do_Search(); searchWanted = false; continue; }
        const uint32_t ms = millis();
        if ((int32_t)(ms - nextIss) >= 0){ get_Iss(); nextIss = millis() + (pageNow == 1 ? 10000u : 60000u); continue; }
        if ((int32_t)(ms - nextKp) >= 0){
            get_Kp();
            kpFails = net.kpOk && net.kpAt ? 0 : kpFails + 1;
            nextKp = millis() + (kpFails ? 120000u : 600000u);
            continue;
        }
        // On the station's page, ask sooner.
        if (pageNow == 1 && nextIss - ms > 10000u) nextIss = ms + 10000u;
} while (0);
}

// What the day looks like (worked out again for a new day or place) ----------------------------------------------------------
struct Sky_Day_t {
    int64_t midnight;                   // local midnight, UTC seconds
    double lat, lon;
    Sky_Rise_Set_t sun, sunYesterday, moon;
    int8_t curve[136];                  // the sun's altitude (degrees, rounded) through the day, at each column
    int64_t nextFull, nextNew;
};

static int64_t local_Midnight(time_t now){
    struct tm t;
    localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    return (int64_t)mktime(&t);
}

static void make_Day(Sky_Day_t& d, time_t now, double lat, double lon){
    d.midnight = local_Midnight(now);
    d.lat = lat; d.lon = lon;
    d.sun = sky_Sun_Rise_Set(d.midnight, lat, lon);
    d.sunYesterday = sky_Sun_Rise_Set(d.midnight - 86400, lat, lon);
    d.moon = sky_Moon_Rise_Set(d.midnight, lat, lon);
    for (int x = 0; x < 136; x++){
        const double a = sky_Sun_Altitude(d.midnight + (int64_t)x * 86400 / 135, lat, lon);
        d.curve[x] = (int8_t)lround(a);
    }
    d.nextFull = sky_Next_Phase((int64_t)now, 0.5);
    d.nextNew = sky_Next_Phase((int64_t)now, 0.0);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
#define RGB MTB_KIT_RGB
static const uint16_t C_BG = RGB(2, 4, 14), C_GRID = RGB(18, 24, 44), C_SUN = RGB(255, 200, 40), C_MOON = RGB(232, 228, 205);

static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void dot(int x, int y, uint16_t c){ if (x >= 0 && y >= 0 && x < W && y < H) mtb_Panel_Draw_PixelRGB565((int16_t)x, (int16_t)y, c); }
static void hline(int y, int x0, int x1, uint16_t c){ mtb_Panel_Draw_HLine((int16_t)y, (int16_t)x0, (int16_t)x1, c); }
static void vline(int x, int y0, int y1, uint16_t c){ mtb_Panel_Draw_VLine((int16_t)x, (int16_t)y0, (int16_t)y1, c); }

static void clock_Text(char* t, size_t n, int64_t at){
    if (!at){ snprintf(t, n, "--"); return; }
    const time_t x = (time_t)at;
    struct tm lt;
    localtime_r(&x, &lt);
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else { int h = lt.tm_hour % 12; if (!h) h = 12; snprintf(t, n, "%d:%02d%s", h, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM"); }
}

static void header(const char* place, time_t now){
    rect(0, 0, W - 1, 9, RGB(10, 14, 30));
    char p[64], fit[64];
    snprintf(p, sizeof(p), "%s", place);
    mtb_Kit_Upper(p);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, p, 150);
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_TEXT, fit, RGB(10, 14, 30));
    if (now > 1700000000){
        static const char* const DAY[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        static const char* const MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        struct tm lt;
        localtime_r(&now, &lt);
        char c[12], t[32];
        clock_Text(c, sizeof(c), (int64_t)now);
        snprintf(t, sizeof(t), "%s %d %s %s", DAY[lt.tm_wday], lt.tm_mday, MON[lt.tm_mon], c);
        mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, KIT_LABEL, t, RGB(10, 14, 30));
    }
}

/** The moon as it looks: lit from the right while it grows (north of the equator), from the left as it shrinks. */
static void moon_Disc(int cx, int cy, int r, double phase, bool south){
    // Three darker "seas", where they are on the real moon (as fractions of the radius).
    static const float SEA[3][3] = {{-0.30f, -0.35f, 0.30f}, {0.10f, -0.05f, 0.28f}, {-0.15f, 0.40f, 0.22f}};
    for (int y = -r + 1; y <= r - 1; y++){
        const double w = sqrt((double)r * r - (double)y * y) - 0.3;
        const double c = cos(2 * M_PI * phase);
        double litFrom, litTo;
        if (phase <= 0.5){ litFrom = w * c; litTo = w; }       // waxing: from the terminator to the right edge
        else { litFrom = -w; litTo = -w * c; }                  // waning: from the left edge to the terminator
        for (int x = (int)-floor(w); x <= (int)floor(w); x++){
            const bool lit = x >= litFrom && x <= litTo;
            uint16_t col = lit ? C_MOON : RGB(26, 28, 40);
            for (int k = 0; k < 3; k++){
                const float dx = x - SEA[k][0] * r, dy = y - SEA[k][1] * r, rr = SEA[k][2] * r;
                if (dx * dx + dy * dy < rr * rr){ col = lit ? RGB(196, 192, 170) : RGB(22, 24, 34); break; }
            }
            dot(cx + (south ? -x : x), cy + y, col);
        }
    }
}

static void page_Sun_Moon(const Sky_Day_t& d, time_t now){
    // The sun's height through the day: a curve above and below the horizon, with the sun where it is now.
    const int gx = 2, gy0 = 13, horizon = 50, scale = 37;     // 90 degrees is 37 px
    hline(horizon, gx, gx + 135, RGB(70, 60, 40));
    for (int hr = 6; hr < 24; hr += 6) vline(gx + hr * 135 / 24, gy0, horizon + 12, C_GRID);
    for (int x = 0; x < 136; x++){
        const int a = d.curve[x];
        const int y = horizon - a * scale / 90;
        if (y < gy0 || y > horizon + 12) continue;
        dot(gx + x, y, a >= 0 ? C_SUN : RGB(60, 50, 90));
        if (a >= 0) vline(gx + x, y + 1, horizon - 1, RGB(40, 30, 8));
    }
    const double alt = sky_Sun_Altitude((int64_t)now, d.lat, d.lon);
    const int nx = gx + (int)((now - d.midnight) * 135 / 86400);
    int ny = horizon - (int)lround(alt) * scale / 90;
    if (ny > horizon + 10) ny = horizon + 10;
    vline(nx, gy0, horizon + 12, RGB(60, 70, 110));
    mtb_Panel_Fill_Circle((int16_t)nx, (int16_t)ny, 3, alt >= -0.833 ? C_SUN : RGB(110, 90, 50));
    char a[16], b[16], t[48];
    clock_Text(a, sizeof(a), d.sun.rise);
    clock_Text(b, sizeof(b), d.sun.set);
    if (!d.sun.rise && !d.sun.set) snprintf(t, sizeof(t), d.sun.maxAlt > 0 ? "THE SUN STAYS UP" : "THE SUN STAYS DOWN");
    else snprintf(t, sizeof(t), "RISE %s  SET %s", a, b);
    mtb_Kit_Text(2, 66, Terminal6x8, KIT_TEXT, t, C_BG);
    if (d.sun.rise && d.sun.set){
        const int64_t len = d.sun.set > d.sun.rise ? d.sun.set - d.sun.rise : d.sun.set + 86400 - d.sun.rise;
        int64_t before = 0;
        if (d.sunYesterday.rise && d.sunYesterday.set) before = d.sunYesterday.set > d.sunYesterday.rise ? d.sunYesterday.set - d.sunYesterday.rise : d.sunYesterday.set + 86400 - d.sunYesterday.rise;
        const int change = before ? (int)((len - before) / 60) : 0;
        snprintf(t, sizeof(t), "DAY %dH %02dM", (int)(len / 3600), (int)(len % 3600 / 60));
        mtb_Kit_Text(2, 78, Terminal6x8, KIT_LABEL, t, C_BG);
        if (before){
            char ch[24];
            if (change == 0) snprintf(ch, sizeof(ch), "SAME AS YESTERDAY");
            else snprintf(ch, sizeof(ch), "%c%d MIN ON YESTERDAY", change > 0 ? '+' : '-', abs(change));
            mtb_Kit_Text(2, 88, Terminal4x6, change >= 0 ? KIT_GREEN : KIT_ORANGE, ch, C_BG);
        }
    }
    snprintf(t, sizeof(t), alt >= 0 ? "THE SUN IS %d DEG UP" : "THE SUN IS %d DEG DOWN", (int)fabs(alt));
    mtb_Kit_Text(2, 100, Terminal4x6, alt >= 0 ? C_SUN : KIT_LABEL, t, C_BG);
    if (alt < 0 && alt > -18){
        const char* tw = alt > -6 ? "CIVIL TWILIGHT" : alt > -12 ? "NAUTICAL TWILIGHT" : "ASTRONOMICAL TWILIGHT";
        mtb_Kit_Text(2, 108, Terminal4x6, KIT_DIM, tw, C_BG);
    } else if (alt <= -18) mtb_Kit_Text(2, 108, Terminal4x6, KIT_CYAN, "FULLY DARK: GOOD FOR STARS", C_BG);

    // The moon.
    vline(140, 12, H - 4, C_GRID);
    double frac, phase;
    sky_Moon_Phase((int64_t)now, frac, phase);
    const int cx = 198;
    moon_Disc(cx, 36, 22, phase, d.lat < 0);
    mtb_Kit_Text_Centre(cx, 62, Terminal4x6, KIT_TEXT, sky_Phase_Name(phase), C_BG);
    snprintf(t, sizeof(t), "%d%% LIT", (int)lround(frac * 100));
    mtb_Kit_Text_Centre(cx, 70, Terminal4x6, KIT_LABEL, t, C_BG);
    clock_Text(a, sizeof(a), d.moon.rise);
    clock_Text(b, sizeof(b), d.moon.set);
    snprintf(t, sizeof(t), "RISES %s", a);
    mtb_Kit_Text_Centre(cx, 82, Terminal4x6, KIT_LABEL, t, C_BG);
    snprintf(t, sizeof(t), "SETS %s", b);
    mtb_Kit_Text_Centre(cx, 90, Terminal4x6, KIT_LABEL, t, C_BG);
    if (d.nextFull){
        const int days = (int)((d.nextFull - (int64_t)now) / 86400);
        if (phase > 0.47 && phase < 0.53) snprintf(t, sizeof(t), "FULL MOON NOW");
        else if (days == 0) snprintf(t, sizeof(t), "FULL MOON TONIGHT");
        else snprintf(t, sizeof(t), "FULL IN %d DAY%s", days, days == 1 ? "" : "S");
        mtb_Kit_Text_Centre(cx, 104, Terminal4x6, C_MOON, t, C_BG);
    }
    if (d.nextNew){
        const int days = (int)((d.nextNew - (int64_t)now) / 86400);
        snprintf(t, sizeof(t), days ? "NEW IN %d DAY%s" : "NEW MOON TODAY", days, days == 1 ? "" : "S");
        mtb_Kit_Text_Centre(cx, 112, Terminal4x6, KIT_DIM, t, C_BG);
    }
}

// A world map for the station's page: the continents as rough outlines (longitude, latitude), about a degree or two
// out at worst, which is under a pixel and a half here. Filled once into a bitmap.
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
struct Sky_Land_t { const int16_t* p; int n; };
static const Sky_Land_t LANDS[] = {{NAM, sizeof(NAM) / 4}, {SAM, sizeof(SAM) / 4}, {AFR, sizeof(AFR) / 4}, {EUA, sizeof(EUA) / 4},
    {AUS, sizeof(AUS) / 4}, {GRL, sizeof(GRL) / 4}, {GBR, sizeof(GBR) / 4}, {JPN, sizeof(JPN) / 4}, {BOR, sizeof(BOR) / 4},
    {SUM, sizeof(SUM) / 4}, {MDG, sizeof(MDG) / 4}, {NZL, sizeof(NZL) / 4}, {NGN, sizeof(NGN) / 4}, {ANT, sizeof(ANT) / 4}};

#define MAP_X   10
#define MAP_Y   11
#define MAP_W   236
#define MAP_H   116
static uint8_t* landMask = nullptr;     // MAP_H rows of MAP_W bits

/** Each row of the map: where it crosses each outline, filled between pairs (even-odd). */
static void make_Land(void){
    if (landMask) return;
    landMask = (uint8_t*)heap_caps_calloc(MAP_H, (MAP_W + 7) / 8, MALLOC_CAP_SPIRAM);
    if (!landMask) return;
    for (int row = 0; row < MAP_H; row++){
        const double lat = 90.0 - (row + 0.5) * 180.0 / MAP_H;
        for (const Sky_Land_t& l : LANDS){
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
                int c0 = (int)lround((xs[k] + 180.0) * (MAP_W - 1) / 360.0), c1 = (int)lround((xs[k + 1] + 180.0) * (MAP_W - 1) / 360.0);
                if (c0 < 0) c0 = 0;
                if (c1 > MAP_W - 1) c1 = MAP_W - 1;
                for (int c = c0; c <= c1; c++) landMask[row * ((MAP_W + 7) / 8) + c / 8] |= (uint8_t)(1 << (c % 8));
            }
        }
    }
}

static void page_Iss(const Sky_Net_t& n, double lat, double lon, time_t now){
    // A latitude/longitude grid: the place, the station and its last ten minutes.
    const int mx = MAP_X, my = MAP_Y, mw = MAP_W, mh = MAP_H;
    make_Land();
    if (landMask) for (int row = 0; row < mh; row++) for (int c = 0; c < mw; c++)
        if (landMask[row * ((MAP_W + 7) / 8) + c / 8] & (1 << (c % 8))) dot(mx + c, my + row, RGB(20, 52, 34));
    auto px = [&](double lo){ return mx + (int)lround((lo + 180.0) * (mw - 1) / 360.0); };
    auto py = [&](double la){ return my + (int)lround((90.0 - la) * (mh - 1) / 180.0); };
    for (int lo = -180; lo <= 180; lo += 30) vline(px(lo), my, my + mh - 1, C_GRID);
    for (int la = -90; la <= 90; la += 30) hline(py(la), mx, mx + mw - 1, la == 0 ? RGB(40, 50, 90) : C_GRID);
    // The place.
    const int ox = px(lon), oy = py(lat);
    hline(oy, ox - 3, ox + 3, KIT_CYAN); vline(ox, oy - 3, oy + 3, KIT_CYAN);
    if (!n.issOk && !n.issAt){
        rect(60, 52, 196, 74, C_BG);
        mtb_Kit_Text_Centre(W / 2, 59, Terminal6x8, KIT_LABEL, "FINDING THE STATION...", C_BG);
        return;
    }
    // The trail (ten minutes), fading; no line across the date line.
    for (int i = 1; i < n.trailN; i++){
        const int x0 = px(n.trailLon[i - 1]), y0 = py(n.trailLat[i - 1]), x1 = px(n.trailLon[i]), y1 = py(n.trailLat[i]);
        if (abs(x1 - x0) > mw / 2) continue;
        mtb_Panel_Draw_Line((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, mtb_Kit_Dim(RGB(255, 255, 255), (uint8_t)(60 + 180 * i / n.trailN)));
    }
    const int sx = px(n.issLon), sy = py(n.issLat);
    rect(sx - 1, sy - 1, sx + 1, sy + 1, WHITE);
    rect(sx - 5, sy - 1, sx - 3, sy + 1, n.issSunlit ? KIT_GOLD : RGB(90, 90, 120));
    rect(sx + 3, sy - 1, sx + 5, sy + 1, n.issSunlit ? KIT_GOLD : RGB(90, 90, 120));
    // The facts, along the top.
    char t[96], d[24];
    const double km = sky_Distance_Km(lat, lon, n.issLat, n.issLon);
    if (km < 1000) snprintf(d, sizeof(d), "%d KM", (int)km);
    else snprintf(d, sizeof(d), "%d,%03d KM", (int)km / 1000, (int)km % 1000);
    rect(0, 0, W - 1, 9, RGB(10, 14, 30));
    snprintf(t, sizeof(t), "SPACE STATION  %s AWAY", d);
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_TEXT, t, RGB(10, 14, 30));
    snprintf(t, sizeof(t), "%d,%03d KM/H  %d KM UP", (int)n.issSpeed / 1000, (int)n.issSpeed % 1000, (int)n.issAlt);
    rect(mx + 1, H - 9, mx + 1 + mtb_Kit_Text_Width(Terminal4x6, t) + 3, H - 2, C_BG);
    mtb_Kit_Text(mx + 3, H - 8, Terminal4x6, KIT_LABEL, t, C_BG);
    const char* light = n.issSunlit ? "IN SUNLIGHT" : "IN THE EARTH'S SHADOW";
    const int lw = mtb_Kit_Text_Width(Terminal4x6, light);
    rect(mx + mw - lw - 5, H - 9, mx + mw - 2, H - 2, C_BG);
    mtb_Kit_Text_Right(mx + mw - 3, H - 8, Terminal4x6, n.issSunlit ? KIT_GOLD : KIT_LABEL, light, C_BG);
    if (now > n.issAt + 60){
        char ago[20];
        mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - n.issAt));
        mtb_Kit_Upper(ago);
        mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, KIT_ORANGE, ago, RGB(10, 14, 30));
    }
}

static void page_Aurora(const Sky_Net_t& n, const char* place, double lat, double lon, time_t now){
    header(place, now);
    if (!n.kpAt){
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, KIT_LABEL, n.kpOk ? "ASKING NOAA..." : "NOAA DID NOT ANSWER YET", C_BG);
        return;
    }
    char t[64];
    snprintf(t, sizeof(t), "KP %.1f", n.kp);
    mtb_Kit_Text(4, 14, Terminal10x17, WHITE, t, C_BG);
    const int k = (int)floorf(n.kp);
    const char* level = k >= 9 ? "EXTREME STORM G5" : k == 8 ? "SEVERE STORM G4" : k == 7 ? "STRONG STORM G3" : k == 6 ? "MODERATE STORM G2"
                      : k == 5 ? "MINOR STORM G1" : k == 4 ? "ACTIVE" : k == 3 ? "UNSETTLED" : "QUIET";
    const uint16_t lc = k >= 5 ? KIT_RED : k == 4 ? KIT_ORANGE : k == 3 ? KIT_GOLD : KIT_GREEN;
    mtb_Kit_Text_Right(W - 4, 20, Terminal6x8, lc, level, C_BG);
    // Nine steps.
    for (int i = 0; i < 9; i++){
        const int x = 4 + i * 28;
        const uint16_t c = i >= 5 ? KIT_RED : i == 4 ? KIT_ORANGE : i == 3 ? KIT_GOLD : KIT_GREEN;
        const float fill = n.kp - i;
        rect(x, 36, x + 25, 41, C_GRID);
        if (fill > 0) rect(x, 36, x + (int)(25 * (fill > 1 ? 1 : fill)), 41, c);
    }
    // The chance here.
    const int chance = sky_Aurora_Chance(n.kp, lat);
    const char* way = lat >= 0 ? "NORTH" : "SOUTH";
    const double sun = sky_Sun_Altitude((int64_t)now, lat, lon);
    if (chance == 2) snprintf(t, sizeof(t), "LIKELY HERE: LOOK %s%s", way, sun > -12 ? " WHEN DARK" : "");
    else if (chance == 1) snprintf(t, sizeof(t), "POSSIBLE, LOW IN THE %s", way);
    else snprintf(t, sizeof(t), "NOT LIKELY AT THIS PLACE");
    mtb_Kit_Text(4, 48, Terminal6x8, chance == 2 ? KIT_GREEN : chance == 1 ? KIT_GOLD : KIT_LABEL, t, C_BG);
    // The last three hours.
    if (n.kpN >= 2){
        // On a fixed scale of 0 to 9, so a quiet day looks quiet.
        rect(4, 62, W - 5, 114, RGB(8, 12, 26));
        for (int g = 3; g <= 9; g += 3){ const int y = 113 - g * 50 / 9; hline(y, 5, W - 6, C_GRID); }
        int px = 0, py = 0;
        for (int i = 0; i < n.kpN; i++){
            const float v = n.kpHist[i] < 0 ? 0 : n.kpHist[i] > 9 ? 9 : n.kpHist[i];
            const int x = 6 + i * (W - 13) / (n.kpN - 1), y = 113 - (int)lroundf(v * 50 / 9);
            vline(x, y + 1, 113, mtb_Kit_Dim(lc, 60));
            if (i) mtb_Panel_Draw_Line((int16_t)px, (int16_t)py, (int16_t)x, (int16_t)y, lc);
            px = x; py = y;
        }
        mtb_Kit_Text(4, 118, Terminal4x6, KIT_DIM, "THE LAST 3 HOURS", C_BG);
    }
    char ago[20];
    mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - n.kpAt));
    mtb_Kit_Upper(ago);
    snprintf(t, sizeof(t), "NOAA, %s", ago);
    mtb_Kit_Text_Right(W - 4, 118, Terminal4x6, KIT_DIM, t, C_BG);
}

// The app ----------------------------------------------------------------------------------------------------------------------------
static void skyTonight_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Search, cmd_Place, cmd_Page, cmd_Cycle);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = false; searchWanted = false;
    memset(&net, 0, sizeof(net));
    pageNow = save.page;
    workerStop = false;
    workerRunning = true;
    worker_Begin();
    pxl_compat_add_background(worker_Step);

    Sky_Day_t* day = (Sky_Day_t*)heap_caps_calloc(1, sizeof(Sky_Day_t), MALLOC_CAP_SPIRAM);
    Sky_Net_t* n = (Sky_Net_t*)heap_caps_calloc(1, sizeof(Sky_Net_t), MALLOC_CAP_SPIRAM);
    if (!day || !n){ workerStop = true; heap_caps_free(day); heap_caps_free(n); mtb_Delete_This_App(THIS_APP); return; }
    appOpen = true;
    uint8_t page = save.page;
    uint32_t pageAt = millis(), lastDraw = 0, version = 0xFFFFFFFF, pushedMs = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const time_t now = time(nullptr);
        if (phoneChanged){ phoneChanged = false; page = save.page; pageAt = ms; day->midnight = 0; redraw = push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; page = (uint8_t)((page + (t > 0 ? 1 : PAGES - 1)) % PAGES); pageAt = ms; redraw = true; }
        if (save.cycle && ms - pageAt >= save.cycle * 1000u){ page = (uint8_t)((page + 1) % PAGES); pageAt = ms; redraw = true; }
        pageNow = page;

        // A new day (or place): the sun's curve, rise and set, the moon.
        if (now > 1700000000 && (local_Midnight(now) != day->midnight || day->lat != save.lat || day->lon != save.lon)){
            make_Day(*day, now, save.lat, save.lon);
            redraw = push = true;
        }
        portENTER_CRITICAL(&lock);
        if (net.version != version){ *n = net; version = net.version; if (page != 0) redraw = true; push = true; }
        portEXIT_CRITICAL(&lock);

        if ((redraw || ms - lastDraw >= 30000) && ms - lastDraw >= 100){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            mtb_Panel_Fill_Screen(C_BG);
            if (now < 1700000000) mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, KIT_LABEL, "WAITING FOR THE TIME...", C_BG);
            else if (page == 0){ header(save.place, now); page_Sun_Moon(*day, now); }
            else if (page == 1) page_Iss(*n, save.lat, save.lon, now);
            else page_Aurora(*n, save.place, save.lat, save.lon, now);
            // Page dots.
            for (int i = 0; i < PAGES; i++) mtb_Panel_Fill_Circle((int16_t)(W / 2 - 6 + i * 6), H - 2, 1, i == page ? KIT_TEXT : KIT_DIM);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 5000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    workerStop = true;
    for (int i = 0; i < 400 && workerRunning; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(day);
    heap_caps_free(n);
    heap_caps_free(landMask);
    landMask = nullptr;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------------
static void sky_Values(JsonObject values){
    Sky_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonObject place = values["place"].to<JsonObject>();
    place["name"] = s.place;
    char where[40];
    snprintf(where, sizeof(where), "%.5f,%.5f", s.lat, s.lon);
    place["value"] = where;
    values["page"] = s.page;
    values["cycle"] = s.cycle;

    const time_t now = time(nullptr);
    if (now < 1700000000) return;
    JsonArray rows = values["sky"].to<JsonArray>();
    char t[96], a[16], b[16];
    const int64_t mid = local_Midnight(now);
    const Sky_Rise_Set_t sun = sky_Sun_Rise_Set(mid, s.lat, s.lon);
    JsonObject r = rows.add<JsonObject>();
    r["label"] = "Sun";
    clock_Text(a, sizeof(a), sun.rise);
    clock_Text(b, sizeof(b), sun.set);
    if (sun.rise || sun.set) snprintf(t, sizeof(t), "Rises %s, sets %s", a, b);
    else snprintf(t, sizeof(t), sun.maxAlt > 0 ? "Up all day" : "Down all day");
    r["value"] = t;
    r["state"] = "info";
    double frac, phase;
    sky_Moon_Phase((int64_t)now, frac, phase);
    const Sky_Rise_Set_t moon = sky_Moon_Rise_Set(mid, s.lat, s.lon);
    JsonObject m = rows.add<JsonObject>();
    m["label"] = "Moon";
    char name[24];
    snprintf(name, sizeof(name), "%s", sky_Phase_Name(phase));
    for (char* p = name + 1; *p; p++) if (*p >= 'A' && *p <= 'Z' && p[-1] != ' ') *p = (char)(*p - 'A' + 'a');
    for (char* p = name + 1; *p; p++) if (p[-1] == ' ' && *p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    clock_Text(a, sizeof(a), moon.rise);
    clock_Text(b, sizeof(b), moon.set);
    snprintf(t, sizeof(t), "%s, %d%% lit; rises %s, sets %s", name, (int)lround(frac * 100), a, b);
    m["value"] = t;
    m["state"] = "info";
    if (appOpen){
        Sky_Net_t n;
        portENTER_CRITICAL(&lock); n = net; portEXIT_CRITICAL(&lock);
        if (n.issAt){
            JsonObject i = rows.add<JsonObject>();
            i["label"] = "Space station";
            snprintf(t, sizeof(t), "%d km away, %s", (int)sky_Distance_Km(s.lat, s.lon, n.issLat, n.issLon), n.issSunlit ? "in sunlight" : "in the earth's shadow");
            i["value"] = t;
            i["state"] = "info";
        }
        if (n.kpAt){
            JsonObject k = rows.add<JsonObject>();
            k["label"] = "Aurora";
            const int c = sky_Aurora_Chance(n.kp, s.lat);
            snprintf(t, sizeof(t), "Kp %.1f: %s", n.kp, c == 2 ? "likely at this place" : c == 1 ? "possible, low in the sky" : "not likely at this place");
            k["value"] = t;
            k["state"] = c == 2 ? "ok" : c == 1 ? "warn" : "off";
        }
    }
}
