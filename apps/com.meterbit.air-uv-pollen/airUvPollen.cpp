/**
 * Air, UV & Pollen: today's outdoor health conditions for one place, in three columns.
 *   - Air: the air quality index now (US AQI 0-500 or European AQI), in its scale's words and colours, and PM2.5.
 *   - UV: the UV index now, today's highest and its hour, and the hours it is 3 or more (WHO: sun protection then).
 *   - Pollen: tree (the highest of alder, birch, olive), grass and weed (the higher of mugwort, ragweed), each as low,
 *     moderate, high or very high on the US National Allergy Bureau's scales. Open-Meteo has pollen for Europe only;
 *     elsewhere the column says so.
 *
 * One request every 30 minutes to Open-Meteo's free air quality API (CAMS forecasts), no key: current values and today's
 * hourly UV, in the place's time zone. The place comes from Open-Meteo's free geocoding (a search on the phone).
 * NVS "aupSave". Knob: click, check now; turn, brightness.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "aup_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "aupSave"
#define SAVE_MAGIC  0x41555001u
#define UA          "Pixlpal-AirUvPollen/1.0"
#define EVERY_MS    (30u * 60u * 1000u)
#define PLACES      8

EXT_RAM_BSS_ATTR TaskHandle_t airUvPollen_Task_H = NULL;
static void airUvPollen_App_Task(void* dApplication);
static void aup_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* airUvPollen_App;
Mtb_Applications* airUvPollen_App_GetInstance(){
    if (!airUvPollen_App){
        airUvPollen_App = new Mtb_Applications_FullScreen(airUvPollen_App_Task, &airUvPollen_Task_H, "Air, UV & Pollen", {0,0}, 8192);
        airUvPollen_App->mtb_App_Set_Mobile_UI(AUP_UI_MANIFEST, AUP_UI_API, nullptr, aup_Values);
    }
    return airUvPollen_App;
}
MTB_REGISTER_APP(airUvPollen_App, 0, 0)       // the catalogue gives the app its number

struct Aup_Save_t {
    uint32_t magic;
    char place[56];
    float lat, lon;
    uint8_t scale;                      // 0 US AQI, 1 European AQI
};

enum : uint8_t { P_TREE = 0, P_GRASS, P_WEED, P_KINDS };

struct Aup_Shared_t {
    bool known, pollenHere;
    int usAqi, euAqi;
    float pm25, uvNow, uvMax;
    int uvMaxHour, sunFrom, sunTo;      // local hours; sunFrom -1: no sunscreen hours today
    float pollen[P_KINDS];              // grains per cubic metre
    char why[44];
    uint32_t at, version;
    // A place search, done by the worker.
    char query[64];
    volatile bool searchNow, checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Aup_Save_t save;
static EXT_RAM_BSS_ATTR Aup_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Aup_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC || !(s.lat >= -90 && s.lat <= 90) || !(s.lon >= -180 && s.lon <= 180)){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC;
        snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
        s.lat = 6.45407f; s.lon = 3.39467f;
    }
    s.place[sizeof(s.place) - 1] = '\0';
    if (s.scale > 1) s.scale = 0;
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Search(JsonDocument& c){
    const char* q = c["query"] | (const char*)nullptr;
    if (!appOpen || !q || strlen(q) < 2 || strlen(q) >= sizeof(sh.query)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(sh.query, sizeof(sh.query), "%s", q); portEXIT_CRITICAL(&lock);
    sh.searchNow = true;
}
static void cmd_Place(JsonDocument& c){
    const char* name = c["place"]["name"] | (const char*)nullptr;
    const char* where = c["place"]["value"] | (const char*)nullptr;
    char* end = nullptr;
    double lat = NAN, lon = NAN;
    if (where){ lat = strtod(where, &end); if (end && *end == ',') lon = strtod(end + 1, &end); }
    if (!name || !name[0] || !end || *end || !(lat >= -90 && lat <= 90) || !(lon >= -180 && lon <= 180)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.place, sizeof(save.place), "%s", name);
    save.lat = (float)lat; save.lon = (float)lon;
    sh.known = false; sh.why[0] = '\0'; sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Scale(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["scale"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.scale = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, JsonDocument& doc, JsonDocument* filter){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 24000, r, UA);
    if (r.code == 200 && body){
        const DeserializationError e = filter ? deserializeJson(doc, body, r.length, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body, r.length);
        if (r.truncated || e) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static void search(const char* query){
    char q[96], url[220];
    mtb_Kit_Url_Encode(q, sizeof(q), query);
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json", q, PLACES);
    JsonDocument doc, filter, out;
    JsonObject f = filter["results"][0].to<JsonObject>();
    f["name"] = true; f["country"] = true; f["admin1"] = true; f["latitude"] = true; f["longitude"] = true;
    const int code = get_Json(url, doc, &filter);
    JsonArray list = out.to<JsonArray>();
    for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()){
        const char* name = r["name"] | "";
        if (!name[0] || !r["latitude"].is<double>()) continue;
        JsonObject item = list.add<JsonObject>();
        char t[96], where[40];
        const char* country = r["country"] | "";
        snprintf(t, sizeof(t), "%s%s%s", name, country[0] ? ", " : "", country);
        item["name"] = t;
        snprintf(where, sizeof(where), "%.5f,%.5f", r["latitude"].as<double>(), r["longitude"].as<double>());
        item["value"] = where;
        item["detail"] = r["admin1"] | "";
    }
    if (airUvPollen_App) airUvPollen_App->mtb_App_Send_Search_Results("places", query, list, code == 200 ? nullptr : "Open-Meteo could not search");
}

static float worst(JsonVariantConst a, JsonVariantConst b, JsonVariantConst c, bool& any){
    float m = -1;
    for (JsonVariantConst v : {a, b, c}) if (v.is<float>()){ any = true; if (v.as<float>() > m) m = v.as<float>(); }
    return m;
}

static void check(const Aup_Save_t& s, Aup_Shared_t& r){
    char url[400];
    snprintf(url, sizeof(url), "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi,european_aqi,pm2_5,uv_index,alder_pollen,birch_pollen,olive_pollen,grass_pollen,mugwort_pollen,ragweed_pollen"
             "&hourly=uv_index&forecast_days=1&timezone=auto&timeformat=unixtime", s.lat, s.lon);
    JsonDocument doc;
    const int code = get_Json(url, doc, nullptr);
    if (code != 200 || doc["error"].as<bool>()){
        if (code == PXL_HTTP_NOT_ALLOWED) snprintf(r.why, sizeof(r.why), "NOT ALLOWED");
        else if (code <= 0 && code != -2) snprintf(r.why, sizeof(r.why), "NO ANSWER FROM OPEN-METEO");
        else snprintf(r.why, sizeof(r.why), "OPEN-METEO: %s", code == -2 ? "AN ODD ANSWER" : "HTTP ERROR");
        return;
    }
    JsonVariantConst c = doc["current"];
    r.usAqi = c["us_aqi"].is<int>() ? c["us_aqi"].as<int>() : -1;
    r.euAqi = c["european_aqi"].is<int>() ? c["european_aqi"].as<int>() : -1;
    r.pm25 = c["pm2_5"].is<float>() ? c["pm2_5"].as<float>() : -1;
    r.uvNow = c["uv_index"].is<float>() ? c["uv_index"].as<float>() : -1;
    bool any = false;
    r.pollen[P_TREE] = worst(c["alder_pollen"], c["birch_pollen"], c["olive_pollen"], any);
    r.pollen[P_GRASS] = worst(c["grass_pollen"], JsonVariantConst(), JsonVariantConst(), any);
    r.pollen[P_WEED] = worst(c["mugwort_pollen"], c["ragweed_pollen"], JsonVariantConst(), any);
    r.pollenHere = any;
    // Today's UV, hour by hour from local midnight.
    r.uvMax = -1; r.uvMaxHour = -1; r.sunFrom = r.sunTo = -1;
    int h = 0;
    for (JsonVariantConst v : doc["hourly"]["uv_index"].as<JsonArrayConst>()){
        if (h >= 24) break;
        const float u = v | -1.0f;
        if (u > r.uvMax){ r.uvMax = u; r.uvMaxHour = h; }
        if (u >= 3.0f){ if (r.sunFrom < 0) r.sunFrom = h; r.sunTo = h + 1; }
        h++;
    }
    r.why[0] = '\0';
    r.known = true;
    r.at = (uint32_t)time(nullptr);
}

static void worker(void*){
    uint32_t next = 0;
    while (!sh.stop){
        if (sh.searchNow){
            sh.searchNow = false;
            char q[64];
            portENTER_CRITICAL(&lock); snprintf(q, sizeof(q), "%s", sh.query); portEXIT_CRITICAL(&lock);
            search(q);
            continue;
        }
        if (!sh.checkNow && (int32_t)(millis() - next) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Aup_Save_t s;
        Aup_Shared_t r;
        portENTER_CRITICAL(&lock); s = save; r = sh; portEXIT_CRITICAL(&lock);
        check(s, r);
        next = millis() + (r.why[0] ? 5u * 60000u : EVERY_MS);
        portENTER_CRITICAL(&lock);
        if (s.lat == save.lat && s.lon == save.lon){             // not moved meanwhile
            sh.known = r.known; sh.pollenHere = r.pollenHere; sh.usAqi = r.usAqi; sh.euAqi = r.euAqi; sh.pm25 = r.pm25;
            sh.uvNow = r.uvNow; sh.uvMax = r.uvMax; sh.uvMaxHour = r.uvMaxHour; sh.sunFrom = r.sunFrom; sh.sunTo = r.sunTo;
            memcpy(sh.pollen, r.pollen, sizeof(r.pollen));
            snprintf(sh.why, sizeof(sh.why), "%s", r.why);
            sh.at = r.at;
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// The scales -----------------------------------------------------------------------------------------------------------------------
struct Aup_Level_t { const char* word; uint16_t color; int rank; };

static Aup_Level_t aqi_Level(int v, bool eu){
    static const uint16_t G = MTB_KIT_RGB(0, 200, 80), Y = MTB_KIT_RGB(240, 220, 0), O = MTB_KIT_RGB(255, 126, 0), R = MTB_KIT_RGB(240, 0, 0),
                          P = MTB_KIT_RGB(160, 60, 200), M = MTB_KIT_RGB(150, 0, 40);
    if (v < 0) return {"-", KIT_DIM, 0};
    if (!eu){                                               // US EPA
        if (v <= 50) return {"GOOD", G, 1};
        if (v <= 100) return {"MODERATE", Y, 2};
        if (v <= 150) return {"UNHEALTHY FOR SENSITIVE GROUPS", O, 3};
        if (v <= 200) return {"UNHEALTHY", R, 4};
        if (v <= 300) return {"VERY UNHEALTHY", P, 5};
        return {"HAZARDOUS", M, 6};
    }
    if (v < 20) return {"GOOD", MTB_KIT_RGB(80, 240, 230), 1};    // European Environment Agency
    if (v < 40) return {"FAIR", MTB_KIT_RGB(80, 204, 170), 2};
    if (v < 60) return {"MODERATE", Y, 3};
    if (v < 80) return {"POOR", R, 4};
    if (v < 100) return {"VERY POOR", M, 5};
    return {"EXTREMELY POOR", P, 6};
}

static Aup_Level_t uv_Level(float u){
    if (u < 0) return {"-", KIT_DIM, 0};
    if (u < 3) return {"LOW", MTB_KIT_RGB(60, 180, 60), 1};       // WHO
    if (u < 6) return {"MODERATE", MTB_KIT_RGB(240, 220, 0), 2};
    if (u < 8) return {"HIGH", MTB_KIT_RGB(255, 126, 0), 3};
    if (u < 11) return {"VERY HIGH", MTB_KIT_RGB(230, 0, 0), 4};
    return {"EXTREME", MTB_KIT_RGB(170, 70, 220), 5};
}

// US National Allergy Bureau scales (grains per cubic metre): trees, grass and weeds each have their own.
static Aup_Level_t pollen_Level(int kind, float g){
    static const float LIMIT[P_KINDS][3] = {{15, 90, 1500}, {5, 20, 200}, {10, 50, 500}};
    if (g < 0) return {"-", KIT_DIM, 0};
    if (g < 1) return {"NONE", MTB_KIT_RGB(90, 90, 90), 0};
    if (g < LIMIT[kind][0]) return {"LOW", MTB_KIT_RGB(60, 180, 60), 1};
    if (g < LIMIT[kind][1]) return {"MODERATE", MTB_KIT_RGB(240, 220, 0), 2};
    if (g < LIMIT[kind][2]) return {"HIGH", MTB_KIT_RGB(255, 126, 0), 3};
    return {"VERY HIGH", MTB_KIT_RGB(230, 0, 0), 4};
}

static void hour_Text(char* t, size_t n, int h){
    h %= 24;
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:00", h);
    else snprintf(t, n, "%d %s", h % 12 ? h % 12 : 12, h < 12 ? "AM" : "PM");
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 8, 14), C_HEAD = MTB_KIT_RGB(14, 20, 30), C_COL = MTB_KIT_RGB(10, 15, 24);

static void draw(const Aup_Save_t& s, const Aup_Shared_t& d){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], fit[64];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.place); mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 70);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_LABEL, fit, C_HEAD);
    if (d.at){
        const uint32_t now = (uint32_t)time(nullptr);
        if (now >= d.at){ mtb_Kit_Ago(t, sizeof(t), (int32_t)(now - d.at)); mtb_Kit_Upper(t); mtb_Kit_Text_Right(W - 3, 2, Terminal4x6, KIT_DIM, t, C_HEAD); }
    }
    if (!d.known){
        mtb_Kit_Text_Centre(W / 2, 54, Terminal8x12, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "CHECKING...", C_BG);
        return;
    }
    const int cw = 84, top = 12, bottom = H - 1;
    for (int k = 0; k < 3; k++) mtb_Panel_Fill_Rect(k * (cw + 2), top, k * (cw + 2) + cw - 1, bottom, C_COL);
    // Air.
    const int aqi = s.scale ? d.euAqi : d.usAqi;
    const Aup_Level_t a = aqi_Level(aqi, s.scale == 1);
    mtb_Kit_Text(4, top + 3, Terminal4x6, KIT_LABEL, s.scale ? "AIR (EU AQI)" : "AIR (US AQI)", C_COL);
    snprintf(t, sizeof(t), "%d", aqi);
    mtb_Kit_Text_Centre(cw / 2, top + 13, PT_Sans_Narrow_Bold28x34, a.color, aqi >= 0 ? t : "-", C_COL);
    char lines[3][40];
    int n = mtb_Kit_Wrap(a.word, Terminal4x6, cw - 6, &lines[0][0], sizeof(lines[0]), 3);
    for (int k = 0; k < n; k++) mtb_Kit_Text_Centre(cw / 2, top + 52 + k * 8, Terminal4x6, a.color, lines[k], C_COL);
    if (d.pm25 >= 0){ snprintf(t, sizeof(t), "PM2.5 %.0f UG/M3", d.pm25); mtb_Kit_Text_Centre(cw / 2, bottom - 9, Terminal4x6, KIT_DIM, t, C_COL); }
    // UV.
    const int ux = cw + 2;
    const Aup_Level_t u = uv_Level(d.uvNow);
    mtb_Kit_Text(ux + 4, top + 3, Terminal4x6, KIT_LABEL, "UV INDEX NOW", C_COL);
    snprintf(t, sizeof(t), "%.0f", d.uvNow < 0 ? 0 : d.uvNow);
    mtb_Kit_Text_Centre(ux + cw / 2, top + 13, PT_Sans_Narrow_Bold28x34, u.color, d.uvNow >= 0 ? t : "-", C_COL);
    mtb_Kit_Text_Centre(ux + cw / 2, top + 52, Terminal4x6, u.color, u.word, C_COL);
    if (d.uvMaxHour >= 0){
        char hr[12]; hour_Text(hr, sizeof(hr), d.uvMaxHour);
        snprintf(t, sizeof(t), "MAX %.0f AT %s", d.uvMax, hr);
        mtb_Kit_Text_Centre(ux + cw / 2, top + 64, Terminal4x6, KIT_LABEL, t, C_COL);
    }
    if (d.sunFrom >= 0){
        char a1[12], a2[12]; hour_Text(a1, sizeof(a1), d.sunFrom); hour_Text(a2, sizeof(a2), d.sunTo);
        mtb_Kit_Text_Centre(ux + cw / 2, bottom - 20, Terminal4x6, KIT_GOLD, "SUNSCREEN", C_COL);
        snprintf(t, sizeof(t), "%s-%s", a1, a2);
        mtb_Kit_Text_Centre(ux + cw / 2, bottom - 11, Terminal4x6, KIT_GOLD, t, C_COL);
    } else mtb_Kit_Text_Centre(ux + cw / 2, bottom - 11, Terminal4x6, KIT_DIM, "LOW ALL DAY", C_COL);
    // Pollen.
    const int px = 2 * (cw + 2), pw = W - px;
    mtb_Kit_Text(px + 4, top + 3, Terminal4x6, KIT_LABEL, "POLLEN", C_COL);
    if (!d.pollenHere){
        mtb_Kit_Text_Centre(px + pw / 2, top + 40, Terminal6x8, KIT_DIM, "NOT HERE", C_COL);
        mtb_Kit_Text_Centre(px + pw / 2, top + 54, Terminal4x6, KIT_DIM, "EUROPE ONLY", C_COL);
        return;
    }
    static const char* const KIND[P_KINDS] = {"TREE", "GRASS", "WEED"};
    for (int k = 0; k < P_KINDS; k++){
        const Aup_Level_t p = pollen_Level(k, d.pollen[k]);
        const int y = top + 16 + k * 34;
        mtb_Kit_Text(px + 5, y, Terminal6x8, KIT_TEXT, KIND[k], C_COL);
        mtb_Panel_Fill_Rect(px + 5, y + 11, px + 5 + (pw - 12) * (p.rank < 0 ? 0 : p.rank) / 4, y + 14, p.color);
        mtb_Kit_Text(px + 5, y + 18, Terminal4x6, p.color, p.word, C_COL);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void airUvPollen_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Search, cmd_Place, cmd_Scale, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "aupNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Aup_Shared_t* d = (Aup_Shared_t*)heap_caps_calloc(1, sizeof(Aup_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0;
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
            Aup_Save_t s;
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
static void aup_Values(JsonObject values){
    Aup_Save_t s;
    Aup_Shared_t* d = (Aup_Shared_t*)heap_caps_calloc(1, sizeof(Aup_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[96];
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Place"; o["value"] = s.place; o["state"] = "info";
    if (!appOpen){ o = rows.add<JsonObject>(); o["label"] = "Conditions"; o["value"] = "Checked while Air, UV & Pollen is open"; o["state"] = "off"; }
    else if (!d->known){ o = rows.add<JsonObject>(); o["label"] = "Conditions"; o["value"] = d->why[0] ? d->why : "Checking"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        const int aqi = s.scale ? d->euAqi : d->usAqi;
        const Aup_Level_t a = aqi_Level(aqi, s.scale == 1);
        o = rows.add<JsonObject>();
        o["label"] = "Air quality";
        snprintf(t, sizeof(t), "%d, %s", aqi, a.word);
        o["value"] = t; o["state"] = a.rank <= 1 ? "ok" : a.rank <= 2 ? "info" : a.rank <= 3 ? "warn" : "error";
        const Aup_Level_t u = uv_Level(d->uvNow);
        o = rows.add<JsonObject>();
        o["label"] = "UV index";
        if (d->sunFrom >= 0){ char a1[12], a2[12]; hour_Text(a1, sizeof(a1), d->sunFrom); hour_Text(a2, sizeof(a2), d->sunTo); snprintf(t, sizeof(t), "%.0f now (%s), sunscreen %s to %s", d->uvNow, u.word, a1, a2); }
        else snprintf(t, sizeof(t), "%.0f now (%s), low all day", d->uvNow < 0 ? 0 : d->uvNow, u.word);
        o["value"] = t; o["state"] = u.rank <= 1 ? "ok" : u.rank <= 2 ? "info" : "warn";
        o = rows.add<JsonObject>();
        o["label"] = "Pollen";
        if (!d->pollenHere) snprintf(t, sizeof(t), "Not available here (Europe only)");
        else snprintf(t, sizeof(t), "Tree %s, grass %s, weed %s", pollen_Level(P_TREE, d->pollen[P_TREE]).word, pollen_Level(P_GRASS, d->pollen[P_GRASS]).word, pollen_Level(P_WEED, d->pollen[P_WEED]).word);
        o["value"] = t; o["state"] = d->pollenHere ? "info" : "off";
    }
    values["scale"] = s.scale;
    JsonObject place = values["place"].to<JsonObject>();
    char where[40];
    snprintf(where, sizeof(where), "%.5f,%.5f", s.lat, s.lon);
    place["name"] = s.place; place["value"] = where;
    heap_caps_free(d);
}
