/**
 * Solar Inverter: a solar system's power flow, live. The panels, the battery, the grid and the house around a hub,
 * with dots running along the lines the way the power flows (faster for more), each with its power; the battery's
 * charge; today's energy and how much of the house the sun and battery cover.
 *   - SolarEdge's monitoring API (the owner's key): currentPowerFlow every 6 minutes, overview (today) every 30, to
 *     stay inside its 300 requests a day.
 *   - Fronius Solar API v1 on the owner's network (no key): GetPowerFlowRealtimeData every 10 seconds.
 *   - Any JSON address (Home Assistant, Node-RED, a hub): values found by JSON Pointer, every 10 seconds.
 * Fronius and JSON addresses are sites the owner allows ("ownerNet"). Knob: the brightness. NVS "solSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "sol_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "solSave"
#define SAVE_MAGIC  0x534F4C01u
#define UA          "Pixlpal-SolarInverter/1.0"
#define PARSE_ERROR (-100)              // not PXL_HTTP_NOT_ALLOWED (-2)

EXT_RAM_BSS_ATTR TaskHandle_t solarInverter_Task_H = NULL;
static void solarInverter_App_Task(void* dApplication);
static void sol_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* solarInverter_App;
Mtb_Applications* solarInverter_App_GetInstance(){
    if (!solarInverter_App){
        solarInverter_App = new Mtb_Applications_FullScreen(solarInverter_App_Task, &solarInverter_Task_H, "Solar Inverter", {0,0}, 8192);
        solarInverter_App->mtb_App_Set_Mobile_UI(SOL_UI_MANIFEST, SOL_UI_API, nullptr, sol_Values);
    }
    return solarInverter_App;
}
MTB_REGISTER_APP(solarInverter_App, 0, 0)       // the catalogue gives the app its number

struct Sol_Save_t {
    uint32_t magic;
    uint8_t source, unitKw, gridSells, capacity;
    char siteId[16];
    char key[48];
    char address[128];
    char header[128];
    char paths[160];
};

// Power in watts. Grid: + buying, - selling. Battery: + discharging, - charging. NAN: not known.
struct Sol_Flow_t { float pv, load, grid, battery, soc; float todayKwh; };

struct Sol_Shared_t {
    Sol_Flow_t f;
    bool known;
    uint32_t at;                        // unix
    char why[48];
    uint32_t version;
    volatile bool stop, running, again;
};

static EXT_RAM_BSS_ATTR Sol_Save_t save;
static EXT_RAM_BSS_ATTR Sol_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false;

static void read_Save(Sol_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.capacity = 5; }
    if (s.source > 2) s.source = 0;
    s.unitKw = s.unitKw ? 1 : 0;
    s.gridSells = s.gridSells ? 1 : 0;
    if (s.capacity < 1 || s.capacity > 100) s.capacity = 5;
    s.siteId[sizeof(s.siteId) - 1] = s.key[sizeof(s.key) - 1] = s.address[sizeof(s.address) - 1] = '\0';
    s.header[sizeof(s.header) - 1] = s.paths[sizeof(s.paths) - 1] = '\0';
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static void changed(void){ sh.again = true; phoneChanged = saveWanted = true; }
static bool range_Cmd(JsonDocument& c, const char* key, long lo, long hi, uint8_t& out){
    long v;
    if (!mtb_Kit_Json_Range(c[key], lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return false; }
    portENTER_CRITICAL(&lock); out = (uint8_t)v; portEXIT_CRITICAL(&lock);
    return true;
}
static void cmd_Source(JsonDocument& c){ if (range_Cmd(c, "source", 0, 2, save.source)){ sh.known = false; changed(); } }
static void cmd_Site(JsonDocument& c){
    const char* v = c["siteId"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.siteId)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!isdigit((unsigned char)*k)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.siteId, sizeof(save.siteId), "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.key)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!isalnum((unsigned char)*k)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, sizeof(save.key), "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Address(JsonDocument& c){
    const char* v = c["address"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    const bool scheme = !strncasecmp(v, "http://", 7) || !strncasecmp(v, "https://", 8);
    if ((n && !scheme) || n >= sizeof(save.address)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (size_t i = 0; i < n; i++) if (v[i] <= ' ' || v[i] == '"' || v[i] == '\\'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.address, sizeof(save.address), "%.*s", (int)n, v);
    if (save.source == 1){ size_t k = strlen(save.address); while (k && save.address[k - 1] == '/') save.address[--k] = '\0'; }
    portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Header(JsonDocument& c){
    const char* v = c["header"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.header) || (v[0] && !strchr(v, ':'))){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (*k < ' ' || *k == '\x7f'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.header, sizeof(save.header), "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Paths(JsonDocument& c){
    const char* v = c["paths"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.paths)){ mtb_Ble_App_Cmd_Reject(); return; }
    // Each path empty or starting with "/".
    bool start = true;
    for (const char* k = v; *k; k++){
        if (start && *k != ',' && *k != '/' && *k != ' '){ mtb_Ble_App_Cmd_Reject(); return; }
        start = *k == ',';
    }
    portENTER_CRITICAL(&lock); snprintf(save.paths, sizeof(save.paths), "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Unit(JsonDocument& c){ if (range_Cmd(c, "unit", 0, 1, save.unitKw)) changed(); }
static void cmd_Grid_Sign(JsonDocument& c){ if (range_Cmd(c, "gridSign", 0, 1, save.gridSells)) changed(); }
static void cmd_Capacity(JsonDocument& c){ if (range_Cmd(c, "capacity", 1, 100, save.capacity)){ phoneChanged = saveWanted = true; } }

// The worker -------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, const char* headerLine, JsonDocument& doc, JsonDocument* filter){
    const char* headers[5] = {"Accept", "application/json", nullptr, nullptr, nullptr};
    char name[64] = "";
    const char* value = nullptr;
    if (headerLine && headerLine[0]){
        const char* colon = strchr(headerLine, ':');
        if (colon && colon - headerLine < (int)sizeof(name)){
            snprintf(name, sizeof(name), "%.*s", (int)(colon - headerLine), headerLine);
            value = colon + 1;
            while (*value == ' ') value++;
            headers[2] = name; headers[3] = value;
        }
    }
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 48000, r, UA);
    int code = r.code;
    if (code == 200 && body){
        const DeserializationError e = filter ? deserializeJson(doc, body, r.length, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body, r.length);
        if (e || r.truncated) code = PARSE_ERROR;
    }
    heap_caps_free(body);
    return code;
}

static float num(JsonVariantConst v){
    if (v.is<float>() || v.is<int>() || v.is<long>()) return v.as<float>();
    if (v.is<const char*>()){ char* e; const float f = strtof(v.as<const char*>(), &e); if (e != v.as<const char*>()) return f; }
    return NAN;
}

static int fetch_SolarEdge(const Sol_Save_t& s, Sol_Flow_t& f, bool withToday){
    if (!s.siteId[0] || !s.key[0]) return -50;
    char url[200];
    snprintf(url, sizeof(url), "https://monitoringapi.solaredge.com/site/%s/currentPowerFlow?api_key=%s", s.siteId, s.key);
    JsonDocument doc;
    int code = get_Json(url, nullptr, doc, nullptr);
    if (code != 200) return code;
    JsonVariantConst p = doc["siteCurrentPowerFlow"];
    const float k = !strcmp(p["unit"] | "kW", "W") ? 1.0f : 1000.0f;
    f.pv = num(p["PV"]["currentPower"]) * k;
    f.load = num(p["LOAD"]["currentPower"]) * k;
    float grid = num(p["GRID"]["currentPower"]) * k, battery = num(p["STORAGE"]["currentPower"]) * k;
    f.soc = num(p["STORAGE"]["chargeLevel"]);
    // The directions are in the connections: {"from": "GRID", "to": "Load"}.
    bool gridIn = true, battOut = false;
    for (JsonVariantConst c : p["connections"].as<JsonArrayConst>()){
        const char* from = c["from"] | ""; const char* to = c["to"] | "";
        if (!strcasecmp(to, "grid")) gridIn = false;
        if (!strcasecmp(from, "grid")) gridIn = true;
        if (!strcasecmp(from, "storage")) battOut = true;
        if (!strcasecmp(to, "storage")) battOut = false;
    }
    f.grid = isnan(grid) ? NAN : gridIn ? grid : -grid;
    f.battery = isnan(battery) ? NAN : battOut ? battery : -battery;
    if (!p["STORAGE"].is<JsonObjectConst>()){ f.battery = NAN; f.soc = NAN; }
    if (withToday){
        snprintf(url, sizeof(url), "https://monitoringapi.solaredge.com/site/%s/overview?api_key=%s", s.siteId, s.key);
        JsonDocument o, filter;
        filter["overview"]["lastDayData"]["energy"] = true;
        if (get_Json(url, nullptr, o, &filter) == 200){ const float e = num(o["overview"]["lastDayData"]["energy"]); if (!isnan(e)) f.todayKwh = e / 1000.0f; }
    }
    return 200;
}

static int fetch_Fronius(const Sol_Save_t& s, Sol_Flow_t& f){
    if (!s.address[0]) return -51;
    char url[200];
    snprintf(url, sizeof(url), "%s/solar_api/v1/GetPowerFlowRealtimeData.fcgi", s.address);
    JsonDocument doc, filter;
    filter["Body"]["Data"]["Site"] = true;
    filter["Body"]["Data"]["Inverters"] = true;
    const int code = get_Json(url, nullptr, doc, &filter);
    if (code != 200) return code;
    JsonVariantConst site = doc["Body"]["Data"]["Site"];
    if (!site.is<JsonObjectConst>()) return PARSE_ERROR;
    f.pv = site["P_PV"].isNull() ? 0.0f : num(site["P_PV"]);         // null at night
    const float load = num(site["P_Load"]);
    f.load = isnan(load) ? NAN : fabsf(load);                         // negative: used
    f.grid = num(site["P_Grid"]);                                     // positive: bought
    f.battery = site["P_Akku"].isNull() ? NAN : num(site["P_Akku"]);  // positive: discharging
    const float e = num(site["E_Day"]);
    if (!isnan(e)) f.todayKwh = e / 1000.0f;
    for (JsonPairConst inv : doc["Body"]["Data"]["Inverters"].as<JsonObjectConst>()){
        const float soc = num(inv.value()["SOC"]);
        if (!isnan(soc)){ f.soc = soc; break; }
    }
    return 200;
}

/** The value at a JSON Pointer ("/data/0/pv"; "~1" is "/", "~0" is "~"), or NAN. */
static float at_Pointer(JsonVariantConst root, const char* p, size_t n){
    while (n && *p == ' '){ p++; n--; }
    while (n && p[n - 1] == ' ') n--;
    if (!n || *p != '/') return NAN;
    JsonVariantConst v = root;
    size_t i = 1;
    while (i <= n){
        char key[48];
        size_t k = 0;
        while (i < n && p[i] != '/'){
            char c = p[i];
            if (c == '~' && i + 1 < n){ c = p[i + 1] == '1' ? '/' : '~'; i++; }
            if (k + 1 < sizeof(key)) key[k++] = c;
            i++;
        }
        key[k] = '\0';
        if (v.is<JsonArrayConst>()){
            char* end;
            const long idx = strtol(key, &end, 10);
            if (*end || idx < 0) return NAN;
            v = v[(size_t)idx];
        } else v = v[(const char*)key];
        if (v.isNull()) return NAN;
        i++;
    }
    return num(v);
}

static int fetch_Custom(const Sol_Save_t& s, Sol_Flow_t& f){
    if (!s.address[0]) return -51;
    if (!s.paths[0]) return -52;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    const int code = get_Json(s.address, s.header, doc, nullptr);
    if (code != 200) return code;
    float v[5] = {NAN, NAN, NAN, NAN, NAN};
    const char* p = s.paths;
    for (int k = 0; k < 5 && *p; k++){
        const char* comma = strchr(p, ',');
        const size_t n = comma ? (size_t)(comma - p) : strlen(p);
        v[k] = at_Pointer(doc.as<JsonVariantConst>(), p, n);
        if (!comma) break;
        p = comma + 1;
    }
    const float k = s.unitKw ? 1000.0f : 1.0f;
    f.pv = v[0] * k; f.load = isnan(v[1]) ? NAN : fabsf(v[1] * k); f.soc = v[2];
    f.grid = v[3] * k * (s.gridSells ? -1 : 1); f.battery = v[4] * k;
    if (isnan(f.pv) && isnan(f.load) && isnan(f.grid) && isnan(f.soc)) return -53;
    return 200;
}

static void worker(void*){
    uint32_t nextAt = 0, todayAt = 0;
    float todayKwh = NAN;
    while (!sh.stop){
        if (sh.again){ sh.again = false; nextAt = 0; todayAt = 0; todayKwh = NAN; }
        if (nextAt && (int32_t)(millis() - nextAt) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        Sol_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        Sol_Flow_t f = {NAN, NAN, NAN, NAN, NAN, NAN};
        int code;
        if (s.source == 0){
            const bool withToday = !todayAt || millis() - todayAt >= 1800000u;
            code = fetch_SolarEdge(s, f, withToday);
            if (code == 200 && withToday){ todayAt = millis() ? millis() : 1; if (!isnan(f.todayKwh)) todayKwh = f.todayKwh; }
            if (isnan(f.todayKwh)) f.todayKwh = todayKwh;
        } else if (s.source == 1) code = fetch_Fronius(s, f);
        else code = fetch_Custom(s, f);
        // Work out what is missing from the rest: the house uses what comes in.
        if (code == 200 && isnan(f.load) && !isnan(f.pv)){
            f.load = f.pv + (isnan(f.grid) ? 0 : f.grid) + (isnan(f.battery) ? 0 : f.battery);
            if (f.load < 0) f.load = 0;
        }
        portENTER_CRITICAL(&lock);
        if (code == 200){ sh.f = f; sh.known = true; sh.at = (uint32_t)time(nullptr); sh.why[0] = '\0'; }
        else if (code == -50) snprintf(sh.why, sizeof(sh.why), "ADD THE SITE ID AND KEY IN THE PHONE APP");
        else if (code == -51) snprintf(sh.why, sizeof(sh.why), "ADD THE ADDRESS IN THE PHONE APP");
        else if (code == -52) snprintf(sh.why, sizeof(sh.why), "ADD WHERE THE VALUES ARE IN THE PHONE APP");
        else if (code == -53) snprintf(sh.why, sizeof(sh.why), "NONE OF THE PATHS FOUND A NUMBER");
        else if (code == PXL_HTTP_NOT_ALLOWED) snprintf(sh.why, sizeof(sh.why), "ALLOW THE ADDRESS IN THE PHONE APP");
        else if (code == PARSE_ERROR) snprintf(sh.why, sizeof(sh.why), "THE ANSWER WAS NOT JSON");
        else if (code == 403 || code == 401) snprintf(sh.why, sizeof(sh.why), s.source == 0 ? "SOLAREDGE REFUSED THE KEY OR SITE" : "THE ADDRESS REFUSED PIXLPAL");
        else if (code == 429) snprintf(sh.why, sizeof(sh.why), "TOO MANY REQUESTS TODAY: WAITING");
        else if (code <= 0) snprintf(sh.why, sizeof(sh.why), "NO ANSWER");
        else snprintf(sh.why, sizeof(sh.why), "HTTP %d", code);
        sh.version++;
        portEXIT_CRITICAL(&lock);
        const uint32_t every = s.source == 0 ? (code == 429 ? 3600000u : 360000u) : (code == 200 ? 10000u : 30000u);
        nextAt = millis() + (code <= -50 && code >= -53 ? 2000u : every);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 12), C_LINE = MTB_KIT_RGB(40, 44, 56);
static const uint16_t C_SUN = MTB_KIT_RGB(255, 200, 40), C_HOME = MTB_KIT_RGB(120, 200, 255), C_BATT = MTB_KIT_RGB(80, 220, 120);
static const uint16_t C_BUY = MTB_KIT_RGB(255, 90, 70), C_SELL = MTB_KIT_RGB(80, 220, 120);

#define HUB_X 128
#define HUB_Y 64

static void power_Text(char* t, size_t n, float w){
    if (isnan(w)){ snprintf(t, n, "--"); return; }
    const float a = fabsf(w);
    if (a >= 10000) snprintf(t, n, "%.1f KW", a / 1000);
    else if (a >= 1000) snprintf(t, n, "%.2f KW", a / 1000);
    else snprintf(t, n, "%d W", (int)lroundf(a));
}

/** A line from a node to the hub, with dots running along it: towards the hub if @p in, faster for more power. */
static void flow(int x0, int y0, float watts, bool in, uint16_t color, uint32_t ms){
    mtb_Panel_Draw_Line((int16_t)x0, (int16_t)y0, HUB_X, HUB_Y, C_LINE);
    if (isnan(watts) || fabsf(watts) < 10) return;
    const float speed = 0.15f + fminf(fabsf(watts), 6000.0f) / 6000.0f * 0.85f;      // lengths a second
    const float phase = fmodf(ms / 1000.0f * speed, 1.0f);
    for (int k = 0; k < 3; k++){
        float t = fmodf(phase + k / 3.0f, 1.0f);
        if (!in) t = 1 - t;                         // away from the node: t runs hub to node
        const int x = (int)lroundf(x0 + (HUB_X - x0) * t), y = (int)lroundf(y0 + (HUB_Y - y0) * t);
        mtb_Panel_Fill_Rect((int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(x + 1), (int16_t)(y + 1), color);
    }
}

static void icon_Sun(int x, int y, uint16_t c){
    mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, 6, c);
    static const int8_t R[8][2] = {{0, -10}, {7, -7}, {10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}};
    for (const auto& r : R) mtb_Panel_Draw_Line((int16_t)(x + r[0] * 8 / 10), (int16_t)(y + r[1] * 8 / 10), (int16_t)(x + r[0]), (int16_t)(y + r[1]), c);
}
static void icon_Home(int x, int y, uint16_t c){
    mtb_Panel_Fill_Triangle((int16_t)(x - 10), (int16_t)(y - 1), (int16_t)x, (int16_t)(y - 10), (int16_t)(x + 10), (int16_t)(y - 1), c);
    mtb_Panel_Fill_Rect((int16_t)(x - 7), (int16_t)(y - 1), (int16_t)(x + 7), (int16_t)(y + 9), c);
    mtb_Panel_Fill_Rect((int16_t)(x - 2), (int16_t)(y + 3), (int16_t)(x + 2), (int16_t)(y + 9), C_BG);
}
static void icon_Grid(int x, int y, uint16_t c){     // a pylon
    mtb_Panel_Draw_Line((int16_t)(x - 7), (int16_t)(y + 10), (int16_t)x, (int16_t)(y - 10), c);
    mtb_Panel_Draw_Line((int16_t)(x + 7), (int16_t)(y + 10), (int16_t)x, (int16_t)(y - 10), c);
    mtb_Panel_Draw_HLine((int16_t)(y - 5), (int16_t)(x - 9), (int16_t)(x + 9), c);
    mtb_Panel_Draw_HLine((int16_t)(y + 1), (int16_t)(x - 7), (int16_t)(x + 7), c);
    mtb_Panel_Draw_Line((int16_t)(x - 4), (int16_t)(y + 1), (int16_t)(x + 5), (int16_t)(y + 8), c);
    mtb_Panel_Draw_Line((int16_t)(x + 4), (int16_t)(y + 1), (int16_t)(x - 5), (int16_t)(y + 8), c);
}
static void icon_Battery(int x, int y, float soc, uint16_t c){
    mtb_Panel_Draw_Rect((int16_t)(x - 11), (int16_t)(y - 6), (int16_t)(x + 9), (int16_t)(y + 6), c);
    mtb_Panel_Fill_Rect((int16_t)(x + 10), (int16_t)(y - 2), (int16_t)(x + 11), (int16_t)(y + 2), c);
    if (!isnan(soc)){
        const int w = (int)lroundf(17 * fminf(fmaxf(soc, 0), 100) / 100.0f);
        const uint16_t fill = soc < 20 ? KIT_RED : soc < 50 ? KIT_GOLD : c;
        if (w > 0) mtb_Panel_Fill_Rect((int16_t)(x - 9), (int16_t)(y - 4), (int16_t)(x - 9 + w - 1), (int16_t)(y + 4), fill);
    }
}

static void draw(const Sol_Save_t& s, const Sol_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    if (!d.known){
        mtb_Kit_Text_Centre(W / 2, 54, Terminal6x8, KIT_LABEL, "SOLAR INVERTER", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "ASKING THE SYSTEM...", C_BG);
        return;
    }
    const Sol_Flow_t& f = d.f;
    char t[32];
    const bool hasBatt = !isnan(f.battery) || !isnan(f.soc);
    // Nodes: the sun top left, the grid top right, the battery bottom left, the house bottom right.
    const int sx = 26, sy = 30, gx = 230, gy = 30, bx = 26, by = 100, hx = 230, hy = 100;
    flow(sx + 12, sy + 6, f.pv, true, C_SUN, ms);
    flow(gx - 12, gy + 6, f.grid, f.grid > 0, f.grid > 0 ? C_BUY : C_SELL, ms);
    if (hasBatt) flow(bx + 14, by - 6, f.battery, f.battery > 0, C_BATT, ms);
    flow(hx - 12, hy - 6, f.load, false, C_HOME, ms);
    mtb_Panel_Fill_Circle(HUB_X, HUB_Y, 4, MTB_KIT_RGB(90, 94, 110));
    // The sun, with its share of the panels' size.
    const bool sunny = !isnan(f.pv) && f.pv >= 10;
    icon_Sun(sx, sy, sunny ? C_SUN : KIT_DIM);
    power_Text(t, sizeof(t), f.pv);
    mtb_Kit_Text(sx + 14, sy - 12, Terminal8x12, sunny ? C_SUN : KIT_LABEL, t, C_BG);
    if (!isnan(f.pv)){
        const float share = f.pv / (s.capacity * 1000.0f);
        mtb_Kit_Bar(sx + 14, sy + 2, 54, 3, share > 1 ? 1 : share < 0 ? 0 : share, C_SUN);
        snprintf(t, sizeof(t), "%d%% OF %d KW", (int)lroundf(fminf(share, 1.5f) * 100), s.capacity);
        mtb_Kit_Text(sx + 14, sy + 7, Terminal4x6, KIT_DIM, t, C_BG);
    }
    // The grid: buying or selling.
    const bool buying = !isnan(f.grid) && f.grid > 10, selling = !isnan(f.grid) && f.grid < -10;
    icon_Grid(gx, gy, buying ? C_BUY : selling ? C_SELL : KIT_DIM);
    power_Text(t, sizeof(t), f.grid);
    mtb_Kit_Text_Right(gx - 14, gy - 12, Terminal8x12, buying ? C_BUY : selling ? C_SELL : KIT_LABEL, t, C_BG);
    mtb_Kit_Text_Right(gx - 14, gy + 2, Terminal4x6, KIT_DIM, isnan(f.grid) ? "GRID" : buying ? "BUYING" : selling ? "SELLING" : "GRID IDLE", C_BG);
    // The battery.
    if (hasBatt){
        icon_Battery(bx, by, f.soc, C_BATT);
        if (!isnan(f.soc)){ snprintf(t, sizeof(t), "%d%%", (int)lroundf(f.soc)); mtb_Kit_Text(bx + 16, by - 2, Terminal8x12, f.soc < 20 ? KIT_RED : KIT_TEXT, t, C_BG); }
        power_Text(t, sizeof(t), f.battery);
        const bool charging = !isnan(f.battery) && f.battery < -10, discharging = !isnan(f.battery) && f.battery > 10;
        char line[40];
        snprintf(line, sizeof(line), "%s %s", charging ? "CHARGING" : discharging ? "USING" : "IDLE", charging || discharging ? t : "");
        mtb_Kit_Text(bx + 16, by + 12, Terminal4x6, charging ? C_BATT : discharging ? KIT_GOLD : KIT_DIM, line, C_BG);
        mtb_Kit_Text(bx - 11, by - 16, Terminal4x6, KIT_DIM, "BATTERY", C_BG);
    }
    // The house.
    icon_Home(hx, hy, C_HOME);
    power_Text(t, sizeof(t), f.load);
    mtb_Kit_Text_Right(hx - 14, hy - 2, Terminal8x12, KIT_TEXT, t, C_BG);
    mtb_Kit_Text_Right(hx - 14, hy + 12, Terminal4x6, KIT_DIM, "THE HOUSE USES", C_BG);
    // The middle top: today, and how much of the house the sun and battery cover.
    if (!isnan(f.todayKwh)){
        snprintf(t, sizeof(t), "TODAY %.1f KWH", f.todayKwh);
        mtb_Kit_Text_Centre(HUB_X, 2, Terminal6x8, C_SUN, t, C_BG);
    }
    if (!isnan(f.load) && f.load > 20){
        const float bought = !isnan(f.grid) && f.grid > 0 ? f.grid : 0;
        int self = (int)lroundf((1 - bought / f.load) * 100);
        if (self < 0) self = 0;
        if (self > 100) self = 100;
        snprintf(t, sizeof(t), "%d%% OWN POWER", self);
        mtb_Kit_Text_Centre(HUB_X, 76, Terminal4x6, self >= 80 ? KIT_GREEN : self >= 40 ? KIT_GOLD : KIT_LABEL, t, C_BG);
    }
    const time_t now = time(nullptr);
    const int32_t age = (int32_t)(now - (time_t)d.at);
    if (d.why[0]) mtb_Kit_Text_Centre(HUB_X, H - 7, Terminal4x6, KIT_ORANGE, d.why, C_BG);
    else if (age > (s.source == 0 ? 900 : 120)){ char ago[24], line[40]; mtb_Kit_Ago(ago, sizeof(ago), age); mtb_Kit_Upper(ago); snprintf(line, sizeof(line), "LAST HEARD %s", ago); mtb_Kit_Text_Centre(HUB_X, H - 7, Terminal4x6, KIT_ORANGE, line, C_BG); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void solarInverter_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(buttonDoNothing, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Source, cmd_Site, cmd_Key, cmd_Address, cmd_Header, cmd_Paths, cmd_Unit, cmd_Grid_Sign, cmd_Capacity);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    phoneChanged = saveWanted = false;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "solNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Sol_Shared_t* d = (Sol_Shared_t*)heap_caps_calloc(1, sizeof(Sol_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0;
    bool push = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; push = true; }
        portEXIT_CRITICAL(&lock);
        Sol_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        draw(s, *d, ms);                // every frame: the dots move
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push && ms - pushedMs >= 3000) || ms - pushedMs >= 30000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void sol_Values(JsonObject values){
    Sol_Save_t s;
    Sol_Shared_t* d = (Sol_Shared_t*)heap_caps_calloc(1, sizeof(Sol_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(d, &sh, sizeof(sh)); portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[64];
    auto row = [&](const char* label, const char* value, const char* state){ JsonObject o = rows.add<JsonObject>(); o["label"] = label; o["value"] = value; o["state"] = state; };
    if (!appOpen) row("Power", "While Solar Inverter is open", "off");
    else if (!d->known) row("Power", d->why[0] ? d->why : "Asking the system", d->why[0] ? "warn" : "info");
    else {
        const Sol_Flow_t& f = d->f;
        power_Text(t, sizeof(t), f.pv); row("Solar", t, !isnan(f.pv) && f.pv >= 10 ? "ok" : "off");
        power_Text(t, sizeof(t), f.load); row("The house", t, "info");
        if (!isnan(f.grid)){ char p[24]; power_Text(p, sizeof(p), f.grid); snprintf(t, sizeof(t), "%s %s", f.grid > 10 ? "Buying" : f.grid < -10 ? "Selling" : "Idle,", p); row("Grid", t, f.grid > 10 ? "warn" : "ok"); }
        if (!isnan(f.soc)){ char p[24]; power_Text(p, sizeof(p), f.battery); snprintf(t, sizeof(t), "%d%%, %s %s", (int)lroundf(f.soc), !isnan(f.battery) && f.battery < -10 ? "charging" : !isnan(f.battery) && f.battery > 10 ? "using" : "idle", !isnan(f.battery) && fabsf(f.battery) > 10 ? p : ""); row("Battery", t, f.soc < 20 ? "warn" : "ok"); }
        if (!isnan(f.todayKwh)){ snprintf(t, sizeof(t), "%.1f kWh", f.todayKwh); row("Today", t, "info"); }
        if (d->why[0]) row("Last check", d->why, "warn");
    }
    values["source"] = s.source;
    values["siteId"] = s.siteId;
    values["apiKey_set"] = s.key[0] ? 1 : 0;
    values["address"] = s.address;
    values["header_set"] = s.header[0] ? 1 : 0;
    values["paths"] = s.paths;
    values["unit"] = s.unitKw;
    values["gridSign"] = s.gridSells;
    values["capacity"] = s.capacity;
    heap_caps_free(d);
}
