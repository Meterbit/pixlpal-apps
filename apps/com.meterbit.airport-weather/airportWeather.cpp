/**
 * Airport Weather: an airport's METAR and TAF from aviationweather.gov (free, no key), decoded. Page one: the flight
 * category as a coloured badge (VFR green, MVFR blue, IFR red, LIFR magenta), the wind on a compass (the arrow points
 * where it blows from) with speed and gusts, visibility, weather, clouds, temperature and dew point, pressure, and the
 * report's age. Page two: the forecast's periods. Page three: the raw reports.
 *
 * Up to three airports (ICAO codes) from the phone, fetched together every 10 minutes by a worker task. Knob: turn
 * for the pages, click for the next airport. NVS "awxSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "awx_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define STATIONS    3
#define CLOUDS      4
#define PERIODS     6
#define NVS_KEY     "awxSave"
#define SAVE_MAGIC  0x41575801u
#define UA          "Pixlpal-AirportWeather/1.0"
#define PAGES       3

EXT_RAM_BSS_ATTR TaskHandle_t airportWeather_Task_H = NULL;
static void airportWeather_App_Task(void* dApplication);
static void awx_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* airportWeather_App;
Mtb_Applications* airportWeather_App_GetInstance(){
    if (!airportWeather_App){
        airportWeather_App = new Mtb_Applications_FullScreen(airportWeather_App_Task, &airportWeather_Task_H, "Airport Weather", {0,0}, 8192);
        airportWeather_App->mtb_App_Set_Mobile_UI(AWX_UI_MANIFEST, AWX_UI_API, nullptr, awx_Values);
    }
    return airportWeather_App;
}
MTB_REGISTER_APP(airportWeather_App, 0, 0)    // the catalogue gives the app its number

struct Awx_Save_t { uint32_t magic; char station[STATIONS][5]; uint8_t visKm, inHg; };

struct Awx_Cloud_t { char cover[4]; int32_t base; };            // base: feet, -1 none
struct Awx_Wind_t { int16_t dir; int16_t spd, gst; };           // dir -1 variable; gst 0 none
struct Awx_Period_t {
    uint32_t from, to;
    char change[8];                     // "", FM, BECMG, TEMPO, PROB
    int8_t prob;
    Awx_Wind_t wind;
    float vis; bool visPlus; bool hasVis;
    char wx[40];
    Awx_Cloud_t cloud[2];
};
struct Awx_Station_t {
    bool have;
    char name[48];
    uint32_t obsTime;
    char cat[6];                        // VFR, MVFR, IFR, LIFR
    Awx_Wind_t wind;
    float vis; bool visPlus;
    float temp, dewp; bool hasTemp;
    float altim;                        // hPa
    char wx[48];
    Awx_Cloud_t cloud[CLOUDS];
    char raw[200];
    Awx_Period_t period[PERIODS];
    uint8_t periods;
    char rawTaf[520];
};

enum : uint8_t { F_NONE = 0, F_OK, F_FAIL };

struct Awx_Shared_t {
    Awx_Station_t st[STATIONS];
    uint8_t fetch;
    int code;
    uint32_t fetchedAt, version;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Awx_Save_t save;
static EXT_RAM_BSS_ATTR Awx_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Awx_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; }
    for (int i = 0; i < STATIONS; i++) s.station[i][4] = '\0';
}

// Decoding ---------------------------------------------------------------------------------------------------------------------------
/** "-TSRA BR" -> "LIGHT THUNDERSTORM RAIN, MIST". */
static void decode_Wx(const char* in, char* out, size_t n){
    static const char* const CODES[][2] = {
        {"MI", "SHALLOW"}, {"BC", "PATCHES OF"}, {"PR", "PARTIAL"}, {"DR", "DRIFTING"}, {"BL", "BLOWING"}, {"SH", "SHOWERS OF"},
        {"TS", "THUNDERSTORM"}, {"FZ", "FREEZING"}, {"DZ", "DRIZZLE"}, {"RA", "RAIN"}, {"SN", "SNOW"}, {"SG", "SNOW GRAINS"},
        {"IC", "ICE CRYSTALS"}, {"PL", "ICE PELLETS"}, {"GR", "HAIL"}, {"GS", "SMALL HAIL"}, {"UP", "PRECIPITATION"}, {"BR", "MIST"},
        {"FG", "FOG"}, {"FU", "SMOKE"}, {"VA", "VOLCANIC ASH"}, {"DU", "DUST"}, {"SA", "SAND"}, {"HZ", "HAZE"}, {"PY", "SPRAY"},
        {"PO", "DUST WHIRLS"}, {"SQ", "SQUALLS"}, {"FC", "FUNNEL CLOUD"}, {"SS", "SANDSTORM"}, {"DS", "DUSTSTORM"}};
    out[0] = '\0';
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", in);
    for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")){
        char part[64] = "";
        const char* p = tok;
        if (*p == '-'){ strlcat(part, "LIGHT ", sizeof(part)); p++; }
        else if (*p == '+'){ strlcat(part, "HEAVY ", sizeof(part)); p++; }
        bool near = false;
        if (!strncmp(p, "VC", 2)){ near = true; p += 2; }
        for (; p[0] && p[1]; p += 2){
            for (auto& c : CODES) if (!strncmp(p, c[0], 2)){ strlcat(part, c[1], sizeof(part)); strlcat(part, " ", sizeof(part)); break; }
        }
        size_t k = strlen(part); while (k && part[k - 1] == ' ') part[--k] = '\0';
        if (k && !strncmp(part + k - 3, " OF", 3)) part[k - 3] = '\0';             // "SHOWERS OF" alone
        if (near) strlcat(part, " NEARBY", sizeof(part));
        if (!part[0]) continue;
        if (out[0]) strlcat(out, ", ", n);
        strlcat(out, part, n);
    }
}

static void read_Wind(JsonVariantConst o, Awx_Wind_t& w){
    w.dir = o["wdir"].is<const char*>() ? -1 : (int16_t)(o["wdir"] | 0);
    w.spd = (int16_t)(o["wspd"] | 0);
    w.gst = (int16_t)(o["wgst"] | 0);
}
static void read_Vis(JsonVariantConst v, float& vis, bool& plus, bool* has = nullptr){
    plus = false;
    if (v.is<const char*>()){ const char* s = v; vis = (float)atof(s); plus = strchr(s, '+') != nullptr; if (has) *has = true; }
    else if (v.is<float>()){ vis = v.as<float>(); if (has) *has = true; }
    else { vis = 0; if (has) *has = false; }
}
static void read_Clouds(JsonVariantConst arr, Awx_Cloud_t* out, int max){
    int n = 0;
    for (JsonVariantConst c : arr.as<JsonArrayConst>()){
        if (n == max) break;
        snprintf(out[n].cover, sizeof(out[n].cover), "%s", c["cover"] | "");
        out[n].base = c["base"].isNull() ? -1 : (int32_t)(c["base"] | 0);
        n++;
    }
    for (; n < max; n++){ out[n].cover[0] = '\0'; out[n].base = -1; }
}

/** The category from ceiling and visibility, when the report gives none. */
static const char* category_Of(const Awx_Station_t& s){
    int32_t ceiling = 100000;
    for (int i = 0; i < CLOUDS; i++) if ((!strcmp(s.cloud[i].cover, "BKN") || !strcmp(s.cloud[i].cover, "OVC") || !strcmp(s.cloud[i].cover, "OVX")) && s.cloud[i].base >= 0 && s.cloud[i].base < ceiling) ceiling = s.cloud[i].base;
    const float v = s.visPlus ? 99 : s.vis;
    if (ceiling < 500 || v < 1) return "LIFR";
    if (ceiling < 1000 || v < 3) return "IFR";
    if (ceiling <= 3000 || v <= 5) return "MVFR";
    return "VFR";
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Station(JsonDocument& c){
    for (int i = 0; i < STATIONS; i++){
        char key[10];
        snprintf(key, sizeof(key), "station%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        char code[5] = "";
        size_t n = strlen(v);
        while (n && v[n - 1] == ' ') n--;
        if (n){
            if (n != 4){ mtb_Ble_App_Cmd_Reject(); return; }
            for (int k = 0; k < 4; k++){ if (!isalnum((unsigned char)v[k])){ mtb_Ble_App_Cmd_Reject(); return; } code[k] = (char)toupper((unsigned char)v[k]); }
            code[4] = '\0';
        }
        portENTER_CRITICAL(&lock); snprintf(save.station[i], 5, "%s", code); memset(&sh.st[i], 0, sizeof(sh.st[i])); sh.version++; portEXIT_CRITICAL(&lock);
        sh.readNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Vis(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["vis"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.visKm = (uint8_t)v; phoneChanged = true; }
static void cmd_Press(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["press"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.inHg = (uint8_t)v; phoneChanged = true; }
static void cmd_Fetch(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static EXT_RAM_BSS_ATTR Awx_Station_t got[STATIONS];

static int index_Of(const Awx_Save_t& s, const char* icao){
    for (int i = 0; i < STATIONS; i++) if (s.station[i][0] && !strcasecmp(s.station[i], icao)) return i;
    return -1;
}

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        nextRead = millis() + 10 * 60000u;
        Awx_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        char ids[24] = "";
        for (int i = 0; i < STATIONS; i++) if (s.station[i][0]){ if (ids[0]) strlcat(ids, ",", sizeof(ids)); strlcat(ids, s.station[i], sizeof(ids)); }
        if (!ids[0]) continue;
        memset(got, 0, sizeof(got));
        char url[160];
        uint8_t fetch = F_OK;
        int code = 0;
        // METAR
        snprintf(url, sizeof(url), "https://aviationweather.gov/api/data/metar?ids=%s&format=json", ids);
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 32768, r, UA);
        code = r.code;
        if (r.code == 200 && body){
            JsonDocument doc;
            if (!deserializeJson(doc, body, r.length)) for (JsonObjectConst m : doc.as<JsonArrayConst>()){
                const int i = index_Of(s, m["icaoId"] | "");
                if (i < 0) continue;
                Awx_Station_t& t = got[i];
                t.have = true;
                char nm[64]; mtb_Kit_To_Ascii(nm, sizeof(nm), m["name"] | ""); snprintf(t.name, sizeof(t.name), "%s", nm);
                t.obsTime = m["obsTime"] | 0u;
                snprintf(t.cat, sizeof(t.cat), "%s", m["fltCat"] | "");
                read_Wind(m, t.wind);
                read_Vis(m["visib"], t.vis, t.visPlus);
                t.hasTemp = !m["temp"].isNull();
                t.temp = m["temp"] | 0.0f; t.dewp = m["dewp"] | 0.0f;
                t.altim = m["altim"] | 0.0f;
                decode_Wx(m["wxString"] | "", t.wx, sizeof(t.wx));
                read_Clouds(m["clouds"], t.cloud, CLOUDS);
                snprintf(t.raw, sizeof(t.raw), "%s", m["rawOb"] | "");
                if (!t.cat[0]) snprintf(t.cat, sizeof(t.cat), "%s", category_Of(t));
            }
        } else if (r.code != 204) fetch = F_FAIL;
        heap_caps_free(body);
        // TAF
        if (!sh.stop){
            snprintf(url, sizeof(url), "https://aviationweather.gov/api/data/taf?ids=%s&format=json", ids);
            Mtb_Kit_Http_t r2 = {};
            body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 49152, r2, UA);
            if (r2.code == 200 && body){
                JsonDocument doc;
                if (!deserializeJson(doc, body, r2.length)) for (JsonObjectConst f : doc.as<JsonArrayConst>()){
                    const int i = index_Of(s, f["icaoId"] | "");
                    if (i < 0) continue;
                    Awx_Station_t& t = got[i];
                    snprintf(t.rawTaf, sizeof(t.rawTaf), "%s", f["rawTAF"] | "");
                    const uint32_t now = (uint32_t)time(nullptr);
                    t.periods = 0;
                    for (JsonObjectConst p : f["fcsts"].as<JsonArrayConst>()){
                        if (t.periods == PERIODS) break;
                        Awx_Period_t& q = t.period[t.periods];
                        q.from = p["timeFrom"] | 0u; q.to = p["timeTo"] | 0u;
                        if (q.to && q.to < now) continue;                             // over
                        snprintf(q.change, sizeof(q.change), "%s", p["fcstChange"] | "");
                        q.prob = (int8_t)(p["probability"] | 0);
                        read_Wind(p, q.wind);
                        read_Vis(p["visib"], q.vis, q.visPlus, &q.hasVis);
                        decode_Wx(p["wxString"] | "", q.wx, sizeof(q.wx));
                        read_Clouds(p["clouds"], q.cloud, 2);
                        t.periods++;
                    }
                }
            }
            heap_caps_free(body);
        }
        portENTER_CRITICAL(&lock);
        bool same = true;
        for (int i = 0; i < STATIONS; i++) if (strcmp(s.station[i], save.station[i])) same = false;
        if (same){
            sh.fetch = fetch; sh.code = code;
            if (fetch == F_OK){ memcpy(sh.st, got, sizeof(got)); sh.fetchedAt = (uint32_t)time(nullptr); }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
        if (fetch != F_OK) nextRead = millis() + 2 * 60000u;
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 8, 16), C_HEAD = MTB_KIT_RGB(12, 20, 36), C_PANEL = MTB_KIT_RGB(12, 16, 28);

static uint16_t cat_Color(const char* c){
    if (!strcmp(c, "VFR")) return MTB_KIT_RGB(40, 200, 80);
    if (!strcmp(c, "MVFR")) return MTB_KIT_RGB(60, 120, 255);
    if (!strcmp(c, "IFR")) return MTB_KIT_RGB(235, 40, 40);
    if (!strcmp(c, "LIFR")) return MTB_KIT_RGB(220, 60, 220);
    return KIT_DIM;
}

static void vis_Text(char* t, size_t n, float vis, bool plus, bool km){
    if (km){
        const float k = vis * 1.609f;
        if (plus) snprintf(t, n, "%.0f+ KM", floorf(k));
        else if (k < 5) snprintf(t, n, "%.1f KM", k);
        else snprintf(t, n, "%.0f KM", k);
    } else {
        if (plus) snprintf(t, n, "%.0f+ SM", vis);
        else if (vis < 3 && vis != floorf(vis)) snprintf(t, n, "%.1f SM", vis);
        else snprintf(t, n, "%.0f SM", vis);
    }
}
static void wind_Text(char* t, size_t n, const Awx_Wind_t& w){
    if (w.spd == 0) snprintf(t, n, "CALM");
    else if (w.dir < 0) snprintf(t, n, w.gst ? "VRB %dG%d KT" : "VRB %d KT", w.spd, w.gst);
    else snprintf(t, n, w.gst ? "%03d/%dG%d KT" : "%03d/%d KT", w.dir, w.spd, w.gst);
}
static void clouds_Text(char* t, size_t n, const Awx_Cloud_t* c, int max){
    t[0] = '\0';
    for (int i = 0; i < max && c[i].cover[0]; i++){
        char one[20];
        if (c[i].base >= 0) snprintf(one, sizeof(one), "%s %ld", c[i].cover, (long)c[i].base); else snprintf(one, sizeof(one), "%s", c[i].cover);
        if (t[0]) strlcat(t, "  ", n);
        strlcat(t, one, n);
    }
    if (!t[0]) snprintf(t, n, "SKY CLEAR");
}

/** The wind on a compass: the arrow points where it blows from (as a windsock's tail). */
static void compass(int cx, int cy, int r, const Awx_Wind_t& w){
    mtb_Panel_Draw_Circle((int16_t)cx, (int16_t)cy, (int16_t)r, KIT_LINE);
    mtb_Panel_Draw_Circle((int16_t)cx, (int16_t)cy, (int16_t)(r - 1), KIT_LINE);
    static const char* const NESW[] = {"N", "E", "S", "W"};
    for (int k = 0; k < 4; k++){
        const float a = k * 90 * 3.14159265f / 180.0f;
        mtb_Kit_Text_Centre((int16_t)(cx + sinf(a) * (r - 6)), (int16_t)(cy - cosf(a) * (r - 6) - 3), Terminal4x6, KIT_DIM, NESW[k], C_BG);
    }
    if (w.spd == 0){ mtb_Kit_Text_Centre((int16_t)cx, (int16_t)(cy - 3), Terminal4x6, KIT_LABEL, "CALM", C_BG); return; }
    if (w.dir < 0){ mtb_Kit_Text_Centre((int16_t)cx, (int16_t)(cy - 3), Terminal4x6, KIT_GOLD, "VRB", C_BG); return; }
    const float a = w.dir * 3.14159265f / 180.0f;
    const float tx = cx + sinf(a) * (r - 3), ty = cy - cosf(a) * (r - 3);                // where it comes from
    const float hx = cx - sinf(a) * (r - 10), hy = cy + cosf(a) * (r - 10);              // where it goes
    mtb_Panel_Draw_Line((int16_t)tx, (int16_t)ty, (int16_t)hx, (int16_t)hy, KIT_CYAN);
    mtb_Panel_Draw_Line((int16_t)(tx + 1), (int16_t)ty, (int16_t)(hx + 1), (int16_t)hy, KIT_CYAN);
    const float px = cosf(a) * 4, py = sinf(a) * 4;
    mtb_Panel_Fill_Triangle((int16_t)(hx - sinf(a) * 6), (int16_t)(hy + cosf(a) * 6), (int16_t)(hx + px), (int16_t)(hy + py), (int16_t)(hx - px), (int16_t)(hy - py), KIT_CYAN);
}

static void header(const Awx_Save_t& s, const Awx_Station_t& t, int st, int page, int used){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    char nm[64], fit[64], right[24];
    snprintf(nm, sizeof(nm), "%s %s", s.station[st], t.name);
    mtb_Kit_Upper(nm);
    char* comma = strchr(nm, ','); if (comma) *comma = '\0';
    const uint32_t now = (uint32_t)time(nullptr);
    if (t.obsTime && now >= t.obsTime){ char ago[16]; mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - t.obsTime)); mtb_Kit_Upper(ago); snprintf(right, sizeof(right), "%s", ago); }
    else right[0] = '\0';
    const int rw = mtb_Kit_Text_Width(Terminal4x6, right);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, W / 2 + 34);        // clear of the page dots
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, fit, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 3, Terminal4x6, KIT_DIM, right, C_HEAD);
    for (int k = 0; k < PAGES; k++) mtb_Panel_Fill_Rect((int16_t)(W / 2 + 40 + k * 6), 4, (int16_t)(W / 2 + 43 + k * 6), 6, k == page ? KIT_TEXT : KIT_DIM);
    (void)used;
}

static void page_Now(const Awx_Save_t& s, const Awx_Station_t& t){
    char a[64], b[48];
    // The category badge, the compass.
    const uint16_t cc = cat_Color(t.cat);
    mtb_Panel_Fill_Round_Rect(4, 14, 66, 34, 4, cc);
    mtb_Kit_Text_Centre(35, 17, PT_Sans_Narrow_Bold18x22, BLACK, t.cat[0] ? t.cat : "?", cc);
    compass(35, 76, 32, t.wind);
    wind_Text(a, sizeof(a), t.wind);
    mtb_Kit_Text_Centre(35, 113, Terminal6x8, t.wind.gst >= 25 || t.wind.spd >= 20 ? KIT_GOLD : KIT_TEXT, a, C_BG);
    // The rest, in rows.
    const int x = 76;
    int y = 15;
    auto row = [&](const char* label, const char* value, uint16_t c){
        mtb_Kit_Text(x, (int16_t)(y + 1), Terminal4x6, KIT_LABEL, label, C_BG);
        char fit[64]; mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, value, W - x - 40);
        mtb_Kit_Text(x + 36, (int16_t)y, Terminal6x8, c, fit, C_BG);
        y += 12;
    };
    vis_Text(a, sizeof(a), t.vis, t.visPlus, s.visKm);
    row("VIS", a, KIT_TEXT);
    row("WEATHER", t.wx[0] ? t.wx : "NONE", t.wx[0] ? KIT_GOLD : KIT_DIM);
    clouds_Text(a, sizeof(a), t.cloud, CLOUDS);
    row("CLOUDS", a, KIT_TEXT);
    if (t.hasTemp){ snprintf(a, sizeof(a), "%.0f C  DEW %.0f C", t.temp, t.dewp); row("TEMP", a, KIT_TEXT); }
    if (t.altim > 0){
        if (s.inHg) snprintf(b, sizeof(b), "%.2f INHG", t.altim * 0.02953f); else snprintf(b, sizeof(b), "Q%.0f HPA", t.altim);
        row("PRESSURE", b, KIT_TEXT);
    }
    // The raw report, as a reminder along the bottom.
    char fit[220];
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t.raw, W - x - 4);
    mtb_Kit_Text(x, 116, Terminal4x6, KIT_DIM, fit, C_BG);
}

static void page_Taf(const Awx_Save_t& s, const Awx_Station_t& t){
    mtb_Kit_Text(4, 14, Terminal6x8, KIT_LABEL, "FORECAST (TAF)", C_BG);
    if (!t.periods){ mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_DIM, "NO FORECAST FOR THIS AIRPORT", C_BG); return; }
    char a[80], b[48], c[48], when[24];
    for (int i = 0; i < t.periods && i < 5; i++){
        const Awx_Period_t& p = t.period[i];
        const int y = 26 + i * 20;
        mtb_Panel_Fill_Rect(2, (int16_t)y, W - 3, (int16_t)(y + 18), C_PANEL);
        struct tm f, e; time_t x = p.from, z = p.to; gmtime_r(&x, &f); gmtime_r(&z, &e);
        snprintf(when, sizeof(when), "%02d%02dZ-%02d%02dZ", f.tm_hour, f.tm_min, e.tm_hour, e.tm_min);
        mtb_Kit_Text(5, (int16_t)(y + 2), Terminal4x6, KIT_CYAN, when, C_PANEL);
        if (p.change[0]){
            if (p.prob) snprintf(a, sizeof(a), "PROB%d %s", p.prob, strcmp(p.change, "PROB") ? p.change : "");
            else snprintf(a, sizeof(a), "%s", p.change);
            mtb_Kit_Text(5, (int16_t)(y + 10), Terminal4x6, KIT_GOLD, a, C_PANEL);
        }
        wind_Text(b, sizeof(b), p.wind);
        if (p.hasVis) vis_Text(c, sizeof(c), p.vis, p.visPlus, s.visKm); else c[0] = '\0';
        snprintf(a, sizeof(a), "%s  %s", (p.wind.spd || p.wind.dir) ? b : "", c);
        mtb_Kit_Text(66, (int16_t)(y + 2), Terminal6x8, KIT_TEXT, a, C_PANEL);
        clouds_Text(b, sizeof(b), p.cloud, 2);
        snprintf(a, sizeof(a), "%s%s%s", p.wx, p.wx[0] ? "  " : "", p.cloud[0].cover[0] ? b : "");
        char fit[80]; mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, a, W - 72);
        mtb_Kit_Text(66, (int16_t)(y + 11), Terminal4x6, p.wx[0] ? KIT_GOLD : KIT_LABEL, fit, C_PANEL);
    }
}

static void page_Raw(const Awx_Station_t& t){
    char lines[14][64];
    mtb_Kit_Text(4, 14, Terminal4x6, KIT_LABEL, "METAR", C_BG);
    int n = mtb_Kit_Wrap(t.raw, Terminal4x6, W - 8, &lines[0][0], sizeof(lines[0]), 3);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, (int16_t)(22 + i * 7), Terminal4x6, KIT_TEXT, lines[i], C_BG);
    int y = 24 + n * 7;
    mtb_Kit_Text(4, (int16_t)y, Terminal4x6, KIT_LABEL, "TAF", C_BG);
    n = mtb_Kit_Wrap(t.rawTaf[0] ? t.rawTaf : "NONE", Terminal4x6, W - 8, &lines[0][0], sizeof(lines[0]), (H - y - 10) / 7);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, (int16_t)(y + 8 + i * 7), Terminal4x6, KIT_TEXT, lines[i], C_BG);
}

static void draw(const Awx_Save_t& s, const Awx_Shared_t& d, int st, int page){
    mtb_Panel_Fill_Screen(C_BG);
    int used = 0;
    for (int i = 0; i < STATIONS; i++) if (s.station[i][0]) used++;
    if (!used){
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, "NO AIRPORTS YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, "ADD AN ICAO CODE IN THE PHONE APP: EGLL, KJFK, DNMM", C_BG);
        return;
    }
    const Awx_Station_t& t = d.st[st];
    if (!t.have){
        mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
        mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, s.station[st], C_HEAD);
        const char* msg = d.fetch == F_FAIL ? "AVIATIONWEATHER.GOV DIDN'T ANSWER" : d.fetchedAt ? "NO REPORT FOR THIS AIRPORT" : "FETCHING THE REPORT";
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, KIT_LABEL, msg, C_BG);
        return;
    }
    header(s, t, st, page, used);
    if (page == 0) page_Now(s, t);
    else if (page == 1) page_Taf(s, t);
    else page_Raw(t);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void airportWeather_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Station, cmd_Vis, cmd_Press, cmd_Fetch);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "awxNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Awx_Shared_t* d = (Awx_Shared_t*)heap_caps_calloc(1, sizeof(Awx_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, autoAt = millis();
    int st = 0, page = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){ turns -= t; page = ((page + t) % PAGES + PAGES) % PAGES; autoAt = ms; lastDraw = 0; }
        if (clickRequested){
            clickRequested = false;
            for (int k = 1; k <= STATIONS; k++){ const int n = (st + k) % STATIONS; if (save.station[n][0]){ st = n; break; } }
            page = 0; autoAt = ms; lastDraw = 0;
        }
        if (!save.station[st][0]) for (int n = 0; n < STATIONS; n++) if (save.station[n][0]){ st = n; break; }
        if (page && ms - autoAt >= 60000){ page = 0; lastDraw = 0; }                  // back to now after a minute
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; push = true; lastDraw = 0; }
        portEXIT_CRITICAL(&lock);
        if (ms - lastDraw >= 5000){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(save, *d, st, page);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void awx_Values(JsonObject values){
    Awx_Save_t s;
    Awx_Shared_t* d = (Awx_Shared_t*)heap_caps_malloc(sizeof(Awx_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(s); memset(d, 0, sizeof(*d)); }
    char key[10], t[128], a[32], b[32];
    for (int i = 0; i < STATIONS; i++){ snprintf(key, sizeof(key), "station%d", i); values[key] = s.station[i]; }
    values["vis"] = s.visKm;
    values["press"] = s.inHg;
    JsonArray rows = values["now"].to<JsonArray>();
    for (int i = 0; i < STATIONS; i++){
        if (!s.station[i][0]) continue;
        JsonObject r = rows.add<JsonObject>();
        r["label"] = s.station[i];
        const Awx_Station_t& st = d->st[i];
        if (!appOpen){ r["value"] = "Fetched while Airport Weather is open"; r["state"] = "off"; continue; }
        if (!st.have){ r["value"] = d->fetchedAt ? "No report for this airport" : "Fetching"; r["state"] = d->fetchedAt ? "warn" : "info"; continue; }
        wind_Text(a, sizeof(a), st.wind);
        vis_Text(b, sizeof(b), st.vis, st.visPlus, s.visKm);
        snprintf(t, sizeof(t), "%s: wind %s, visibility %s%s%s", st.cat, a, b, st.wx[0] ? ", " : "", st.wx);
        r["value"] = t;
        r["state"] = !strcmp(st.cat, "VFR") ? "ok" : !strcmp(st.cat, "MVFR") ? "info" : "warn";
        if (st.obsTime){ snprintf(key, sizeof(key), "obs%d", i); values[key] = st.obsTime; r["ago"] = key; }
    }
    heap_caps_free(d);
}
