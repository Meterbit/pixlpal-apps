// Commute's network worker: Transitland departures, TomTom or Google drive times, and the phone's searches (stops,
// a stop's lines, addresses). Each provider's requests are paced by a monthly budget kept in NVS.
#include "cmt_App.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>

static const char TAG[] = "COMMUTE_NET";

#define UA                  "PIXLPAL-L2/1.0 (Pixlpal Commute)"
#define TRANSITLAND         "https://transit.land/api/v2/rest"
#define NET_STACK           12288
#define DEPARTURES_FLOOR_S  60
#define ROUTE_FLOOR_S       120
#define WINDOW_SEC          7200        // departures asked for: the next two hours
#define ANSWER_MAX          (192 * 1024)

extern Mtb_Applications_StatusBar* commute_App;

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    TaskHandle_t task;
    volatile bool running, stop, refresh, changed;
    Cmt_Settings_t* settings;           // under lock
    Cmt_Result_t* results;              // under lock
    Cmt_Status_t status;                // under lock
    uint32_t version;
    char searchId[12], searchQuery[64];
    volatile bool searchPending;
    uint32_t lastAt[CMT_BOARDS];        // worker: monotonic seconds of the last fetch, 0 never
    uint32_t savedTransit, savedRoute;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }
static uint32_t mono(void){ return millis() / 1000; }

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

static void save_Usage(bool force){
    lock();
    Cmt_Usage_t u = N.status.usage;
    unlock();
    if (force || u.transit - N.savedTransit >= 10 || u.route + u.geocode - N.savedRoute >= 10){
        mtb_Write_Nvs_Struct(CMT_USAGE_KEY, &u, sizeof(u));
        N.savedTransit = u.transit;
        N.savedRoute = u.route + u.geocode;
    }
}

static void count(uint32_t Cmt_Usage_t::* field){
    lock();
    N.status.usage.*field = N.status.usage.*field + 1;
    N.version++;
    unlock();
}

static uint32_t yyyymm(uint32_t t){
    const time_t tt = (time_t)t;
    struct tm u;
    gmtime_r(&tt, &u);
    return (uint32_t)((u.tm_year + 1900) * 100 + u.tm_mon + 1);
}

// A provider's own words, or the status.
static void error_Text(char* dest, size_t size, const char* who, int code, const char* body, size_t length){
    char message[64] = "";
    if (body && length){
        JsonDocument doc;
        if (!deserializeJson(doc, body, length)){
            const char* m = doc["error"]["message"] | (doc["error_message"] | (doc["detailedError"]["message"] | (doc["message"] | (const char*)nullptr)));
            if (m == nullptr) m = doc["error"].as<const char*>();
            copy(message, sizeof(message), m);
        }
    }
    if (code <= 0) snprintf(dest, size, "No connection to %s", who);
    else if (code == 401 || code == 403) snprintf(dest, size, "%s refused the key%s%.40s", who, message[0] ? ": " : "", message);
    else if (code == 429) snprintf(dest, size, "%s: too many requests", who);
    else if (message[0]) snprintf(dest, size, "%s %d: %.50s", who, code, message);
    else snprintf(dest, size, "%s: HTTP %d", who, code);
}

static void result_Error(int i, const char* text){
    lock();
    N.results[i].state = CMT_FAILED;
    copy(N.results[i].error, sizeof(N.results[i].error), text);
    N.version++;
    unlock();
}

// Departures ---------------------------------------------------------------------------------------------------
static char* transit_Get(const char* url, const char* key, Mtb_Kit_Http_t& r){
    const char* headers[] = {"apikey", key, "Accept", "application/json", nullptr};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, ANSWER_MAX, r, UA);
    count(&Cmt_Usage_t::transit);
    if (r.code == 429){            // the plan's month is spent: stop until the next one
        lock();
        N.status.transitSpent = 1;
        N.version++;
        unlock();
    }
    return body;
}

static void fetch_Departures(int i, const Cmt_Board_t& b, const char* key){
    char enc[160], url[320];
    mtb_Kit_Url_Encode(enc, sizeof(enc), b.stopKey);
    snprintf(url, sizeof(url), TRANSITLAND "/stops/%s/departures?next=%d&limit=40&include_alerts=false", enc, WINDOW_SEC);
    Mtb_Kit_Http_t r;
    char* body = transit_Get(url, key, r);
    if (r.code != 200 || body == nullptr){
        char e[72];
        error_Text(e, sizeof(e), "Transitland", r.code, body, body ? r.length : 0);
        if (r.code == 404) copy(e, sizeof(e), "Transitland does not know this stop: pick it again");
        heap_caps_free(body);
        result_Error(i, e);
        return;
    }
    Cmt_Departure_t* d = (Cmt_Departure_t*)heap_caps_calloc(CMT_DEPARTURES, sizeof(Cmt_Departure_t), MALLOC_CAP_SPIRAM);
    const int n = d ? cmt_Parse_Departures(body, r.length, b.lines, (uint32_t)time(nullptr), d, CMT_DEPARTURES) : -1;
    heap_caps_free(body);
    if (n < 0){ heap_caps_free(d); result_Error(i, "Transitland's answer was not readable"); return; }
    lock();
    Cmt_Result_t& res = N.results[i];
    res.state = CMT_OK;
    res.fetchedAt = (uint32_t)time(nullptr);
    res.error[0] = '\0';
    res.count = n;
    memcpy(res.departures, d, sizeof(Cmt_Departure_t) * n);
    N.version++;
    unlock();
    heap_caps_free(d);
}

// Drives -----------------------------------------------------------------------------------------------------------
static void fetch_Drive(int i, const Cmt_Board_t& b, const Cmt_Settings_t& s){
    const Cmt_Place_t& a = s.places[b.from];
    const Cmt_Place_t& z = s.places[b.to];
    if (!a.set || !z.set){ result_Error(i, "Set both places' addresses in the phone app"); return; }
    Mtb_Kit_Http_t r;
    char* body = nullptr;
    Cmt_Drive_Result_t res;
    bool ok = false;
    char e[72] = "";
    if (s.drive == CMT_GOOGLE){
        if (!s.googleKey[0]){ result_Error(i, "Add your Google Maps key in the phone app"); return; }
        char json[420];
        const int n = snprintf(json, sizeof(json),
            "{\"origin\":{\"location\":{\"latLng\":{\"latitude\":%.6f,\"longitude\":%.6f}}},"
            "\"destination\":{\"location\":{\"latLng\":{\"latitude\":%.6f,\"longitude\":%.6f}}},"
            "\"travelMode\":\"DRIVE\",\"routingPreference\":\"TRAFFIC_AWARE\","
            "\"routeModifiers\":{\"avoidTolls\":%s,\"avoidHighways\":%s},\"units\":\"METRIC\"}",
            a.latE6 / 1e6, a.lonE6 / 1e6, z.latE6 / 1e6, z.lonE6 / 1e6, s.avoidTolls ? "true" : "false", s.avoidHighways ? "true" : "false");
        const char* headers[] = {"Content-Type", "application/json", "X-Goog-Api-Key", s.googleKey,
                                 "X-Goog-FieldMask", "routes.duration,routes.staticDuration,routes.distanceMeters,routes.description", nullptr};
        body = mtb_Kit_Http_Send("POST", "https://routes.googleapis.com/directions/v2:computeRoutes", headers, json, (size_t)n,
                                 nullptr, 0, 64 * 1024, r, UA);
        count(&Cmt_Usage_t::route);
        ok = r.code == 200 && body && cmt_Parse_Google_Route(body, r.length, res);
        if (!ok) error_Text(e, sizeof(e), "Google", r.code, body, body ? r.length : 0);
        if (r.code == 200 && !ok) copy(e, sizeof(e), "Google found no driving route");
    } else {
        if (!s.tomtomKey[0]){ result_Error(i, "Add your TomTom key in the phone app"); return; }
        char url[420];
        snprintf(url, sizeof(url), "https://api.tomtom.com/routing/1/calculateRoute/%.6f,%.6f:%.6f,%.6f/json?traffic=true&computeTravelTimeFor=all"
                                   "&sectionType=importantRoadStretch&routeRepresentation=summaryOnly%s%s",
                 a.latE6 / 1e6, a.lonE6 / 1e6, z.latE6 / 1e6, z.lonE6 / 1e6,
                 s.avoidTolls ? "&avoid=tollRoads" : "", s.avoidHighways ? "&avoid=motorways" : "");
        // TomTom takes its key in the URL only; the kit never logs URLs.
        char full[520];
        snprintf(full, sizeof(full), "%s&key=%s", url, s.tomtomKey);
        body = mtb_Kit_Http_Get(full, nullptr, nullptr, 0, 64 * 1024, r, UA);
        memset(full, 0, sizeof(full));
        count(&Cmt_Usage_t::route);
        ok = r.code == 200 && body && cmt_Parse_TomTom_Route(body, r.length, res);
        if (!ok) error_Text(e, sizeof(e), "TomTom", r.code, body, body ? r.length : 0);
        if (r.code == 200 && !ok) copy(e, sizeof(e), "TomTom found no driving route");
        if (r.code == 403 && strstr(e, "refused")) copy(e, sizeof(e), "TomTom refused the key, or its free limit is reached");
    }
    heap_caps_free(body);
    if (!ok){ result_Error(i, e); return; }
    lock();
    Cmt_Result_t& x = N.results[i];
    x.state = CMT_OK;
    x.fetchedAt = (uint32_t)time(nullptr);
    x.error[0] = '\0';
    x.drive = res;
    if (x.historyCount == CMT_HISTORY){ memmove(x.history, x.history + 1, sizeof(x.history[0]) * (CMT_HISTORY - 1)); x.historyCount--; }
    x.history[x.historyCount++] = (uint16_t)((res.seconds + 59) / 60);
    N.version++;
    unlock();
}

// The phone's searches -----------------------------------------------------------------------------------------
static void send(const char* id, const char* q, JsonArray results, const char* error){
    if (commute_App) commute_App->mtb_App_Send_Search_Results(id, q, results, error);
}

static void search_Stops(const Cmt_Settings_t& s, const char* q){
    JsonDocument out;
    JsonArray results = out.to<JsonArray>();
    if (!s.transitKey[0]){ send("stop", q, results, "Add your Transitland key first"); return; }
    char enc[160], url[360];
    mtb_Kit_Url_Encode(enc, sizeof(enc), q);
    // Near the first place set (stops within 10 km), else anywhere.
    const Cmt_Place_t* near = nullptr;
    for (const Cmt_Place_t& p : s.places) if (p.set){ near = &p; break; }
    if (near) snprintf(url, sizeof(url), TRANSITLAND "/stops?search=%s&limit=15&lat=%.6f&lon=%.6f&radius=10000", enc, near->latE6 / 1e6, near->lonE6 / 1e6);
    else snprintf(url, sizeof(url), TRANSITLAND "/stops?search=%s&limit=15", enc);
    Mtb_Kit_Http_t r;
    char* body = transit_Get(url, s.transitKey, r);
    Cmt_Stop_t* stops = (Cmt_Stop_t*)heap_caps_calloc(15, sizeof(Cmt_Stop_t), MALLOC_CAP_SPIRAM);
    const int n = (r.code == 200 && body && stops) ? cmt_Parse_Stops(body, r.length, stops, 15) : -1;
    char e[72] = "";
    if (n < 0) error_Text(e, sizeof(e), "Transitland", r.code, body, body ? r.length : 0);
    heap_caps_free(body);
    for (int i = 0; i < n; i++){
        JsonObject o = results.add<JsonObject>();
        o["name"] = stops[i].name;
        o["value"] = stops[i].key;
        o["detail"] = stops[i].detail;
    }
    heap_caps_free(stops);
    send("stop", q, results, n < 0 ? e : (n == 0 && near ? "None within 10 km of your first place" : nullptr));
}

static bool contains_Text(const char* text, const char* part){
    const size_t n = strlen(part);
    for (const char* p = text; *p; p++) if (strncasecmp(p, part, n) == 0) return true;
    return false;
}

static void search_Lines(const Cmt_Settings_t& s, const char* q){
    JsonDocument out;
    JsonArray results = out.to<JsonArray>();
    const Cmt_Board_t& b = s.boards[s.editing];
    if (!b.stopKey[0]){ send("lines", q, results, "Pick the stop first"); return; }
    char enc[160], url[320];
    mtb_Kit_Url_Encode(enc, sizeof(enc), b.stopKey);
    snprintf(url, sizeof(url), TRANSITLAND "/stops/%s/departures?next=14400&limit=80&include_alerts=false", enc);
    Mtb_Kit_Http_t r;
    char* body = transit_Get(url, s.transitKey, r);
    char (*lines)[64] = (char (*)[64])heap_caps_calloc(24, 64, MALLOC_CAP_SPIRAM);
    const int n = (r.code == 200 && body && lines) ? cmt_Parse_Lines(body, r.length, lines, 24) : -1;
    char e[72] = "";
    if (n < 0) error_Text(e, sizeof(e), "Transitland", r.code, body, body ? r.length : 0);
    heap_caps_free(body);
    for (int i = 0; i < n; i++){
        char line[16];
        copy(line, sizeof(line), lines[i]);
        char* sp = strchr(line, ' ');
        if (sp) *sp = '\0';
        if (q[0] && !contains_Text(lines[i], q)) continue;
        JsonObject o = results.add<JsonObject>();
        o["name"] = lines[i];
        o["value"] = line;
    }
    heap_caps_free(lines);
    send("lines", q, results, n < 0 ? e : (n == 0 ? "No departures in the next 4 hours to list" : nullptr));
}

static void search_Address(const Cmt_Settings_t& s, const char* q){
    JsonDocument out;
    JsonArray results = out.to<JsonArray>();
    char enc[200];
    mtb_Kit_Url_Encode(enc, sizeof(enc), q);
    Mtb_Kit_Http_t r;
    char* body = nullptr;
    Cmt_Address_t* a = (Cmt_Address_t*)heap_caps_calloc(6, sizeof(Cmt_Address_t), MALLOC_CAP_SPIRAM);
    int n = -1;
    char e[72] = "";
    if (s.drive == CMT_GOOGLE && s.googleKey[0]){
        char url[480];
        snprintf(url, sizeof(url), "https://maps.googleapis.com/maps/api/geocode/json?address=%s&key=%s", enc, s.googleKey);
        body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 64 * 1024, r, UA);
        memset(url, 0, sizeof(url));
        char status[24] = "";
        if (r.code == 200 && body && a) n = cmt_Parse_Google_Geocode(body, r.length, a, 6, status, sizeof(status));
        if (n < 0) snprintf(e, sizeof(e), "Google Geocoding: %s", status[0] ? status : "no answer");
    } else if (s.tomtomKey[0]){
        char url[480];
        snprintf(url, sizeof(url), "https://api.tomtom.com/search/2/geocode/%s.json?limit=6&key=%s", enc, s.tomtomKey);
        body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 64 * 1024, r, UA);
        memset(url, 0, sizeof(url));
        if (r.code == 200 && body && a) n = cmt_Parse_TomTom_Geocode(body, r.length, a, 6);
        if (n < 0) error_Text(e, sizeof(e), "TomTom", r.code, body, body ? r.length : 0);
    } else {
        heap_caps_free(a);
        send("address", q, results, "Add a TomTom (or Google) key first: it finds addresses");
        return;
    }
    count(&Cmt_Usage_t::geocode);
    heap_caps_free(body);
    for (int i = 0; i < n; i++){
        JsonObject o = results.add<JsonObject>();
        char value[32];
        snprintf(value, sizeof(value), "%.6f,%.6f", a[i].latE6 / 1e6, a[i].lonE6 / 1e6);
        o["name"] = a[i].text;
        o["value"] = value;
    }
    heap_caps_free(a);
    send("address", q, results, n < 0 ? e : nullptr);
}

// The worker -------------------------------------------------------------------------------------------------------
// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static Cmt_Settings_t* s;
static uint32_t savedAt;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Cmt_Settings_t*)heap_caps_malloc(sizeof(Cmt_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr){ N.running = false; return; }
    lock();
    *s = *N.settings;
    unlock();
    memset(N.lastAt, 0, sizeof(N.lastAt));
    savedAt = mono();
}

static void worker_End(void){
    save_Usage(true);
    heap_caps_free(s);
    N.task = NULL;
    N.running = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!N.running) return;
    if (N.stop){ worker_End(); return; }
    if (millis() - wk_StepAt < 500) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {        if (N.changed){
            lock();
            const Cmt_Settings_t old = *s;
            *s = *N.settings;
            N.changed = false;
            for (int i = 0; i < CMT_BOARDS; i++){
                // A board that changed what it shows starts afresh.
                const Cmt_Board_t& a = old.boards[i];
                const Cmt_Board_t& b = s->boards[i];
                if (a.used != b.used || a.kind != b.kind || strcmp(a.stopKey, b.stopKey) || strcmp(a.lines, b.lines) || a.from != b.from || a.to != b.to
                    || memcmp(&old.places, &s->places, sizeof(old.places)) || old.drive != s->drive){
                    memset(&N.results[i], 0, sizeof(Cmt_Result_t));
                    N.lastAt[i] = 0;
                }
            }
            // A new key or plan: try again this month.
            if (strcmp(old.transitKey, s->transitKey) || old.plan != s->plan || old.paygCap != s->paygCap) N.status.transitSpent = 0;
            if (strcmp(old.tomtomKey, s->tomtomKey) || strcmp(old.googleKey, s->googleKey) || old.drive != s->drive || old.googleCap != s->googleCap) N.status.routeSpent = 0;
            N.version++;
            unlock();
        }
        const bool online = Mtb_Applications::internetConnectStatus;
        const uint32_t now = (uint32_t)time(nullptr);
        if (!online || now < 1700000000u) continue;
        if (N.searchPending){
            char id[12], q[64];
            lock();
            copy(id, sizeof(id), N.searchId);
            copy(q, sizeof(q), N.searchQuery);
            N.searchPending = false;
            unlock();
            if (strcmp(id, "stop") == 0) search_Stops(*s, q);
            else if (strcmp(id, "lines") == 0) search_Lines(*s, q);
            else if (strcmp(id, "address") == 0) search_Address(*s, q);
        }
        // This month's budgets.
        lock();
        cmt_Usage_Roll(N.status.usage, yyyymm(now));
        if (N.status.usage.transit == 0) N.status.transitSpent = 0;
        if (N.status.usage.route == 0) N.status.routeSpent = 0;
        const Cmt_Usage_t u = N.status.usage;
        unlock();
        const time_t tt = (time_t)now;
        struct tm lt;
        localtime_r(&tt, &lt);
        const int weekday = (lt.tm_wday + 6) % 7, minute = lt.tm_hour * 60 + lt.tm_min;
        uint32_t transitActive = 0, routeActive = 0;
        for (const Cmt_Board_t& b : s->boards){
            const uint32_t sec = cmt_Active_Seconds_Left(b, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, weekday, minute);
            if (b.kind == CMT_DEPARTURES_BOARD) transitActive += sec; else routeActive += sec;
        }
        const uint32_t transitCap = cmt_Transit_Monthly(*s), routeCap = cmt_Route_Monthly(*s);
        const uint32_t transitLeft = u.transit < transitCap ? transitCap - u.transit : 0;
        const uint32_t routeLeft = u.route < routeCap ? routeCap - u.route : 0;
        const uint32_t transitEvery = cmt_Interval(transitLeft, transitActive, 1, DEPARTURES_FLOOR_S);
        const uint32_t routeEvery = cmt_Interval(routeLeft, routeActive, 1, ROUTE_FLOOR_S);
        lock();
        N.status.nextTransit = transitEvery;
        N.status.nextRoute = routeEvery;
        if (transitLeft == 0) N.status.transitSpent = 1;
        if (routeLeft == 0 && s->drive == CMT_GOOGLE) N.status.routeSpent = 1;
        const bool transitSpent = N.status.transitSpent, routeSpent = N.status.routeSpent;
        unlock();

        const bool all = N.refresh;
        N.refresh = false;
        for (int i = 0; i < CMT_BOARDS && !N.stop; i++){
            const Cmt_Board_t& b = s->boards[i];
            if (!b.used) continue;
            const bool active = cmt_Active(b, weekday, minute);
            // Inactive boards are read once (so they show something), then rest until active or asked.
            const bool never = N.lastAt[i] == 0;
            const uint32_t every = b.kind == CMT_DEPARTURES_BOARD ? transitEvery : routeEvery;
            const bool due = all || never || (active && every && mono() - N.lastAt[i] >= every);
            if (!due) continue;
            if (b.kind == CMT_DEPARTURES_BOARD){
                if (!b.stopKey[0]){ result_Error(i, "Pick a stop in the phone app"); N.lastAt[i] = mono(); continue; }
                if (!s->transitKey[0]){ result_Error(i, "Add your Transitland key in the phone app"); N.lastAt[i] = mono(); continue; }
                if (transitSpent && !all){ result_Error(i, "This month's Transitland requests are used up"); N.lastAt[i] = mono(); continue; }
                fetch_Departures(i, b, s->transitKey);
            } else {
                if (routeSpent && !all){ result_Error(i, "This month's free routing requests are used up"); N.lastAt[i] = mono(); continue; }
                fetch_Drive(i, b, *s);
            }
            N.lastAt[i] = mono();
        }
        if (mono() - savedAt >= 600){ save_Usage(false); savedAt = mono(); }
    } while (0);
}

bool cmt_Net_Start(const Cmt_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Cmt_Settings_t*)heap_caps_calloc(1, sizeof(Cmt_Settings_t), MALLOC_CAP_SPIRAM);
        N.results = (Cmt_Result_t*)heap_caps_calloc(CMT_BOARDS, sizeof(Cmt_Result_t), MALLOC_CAP_SPIRAM);
        if (!N.lock || !N.settings || !N.results) return false;
        made = true;
    }
    for (int i = 0; i < 400 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (N.running) return false;
    Cmt_Usage_t u = {};
    mtb_Read_Nvs_Struct(CMT_USAGE_KEY, &u, sizeof(u));
    lock();
    *N.settings = settings;
    memset(N.results, 0, sizeof(Cmt_Result_t) * CMT_BOARDS);
    memset(&N.status, 0, sizeof(N.status));
    N.status.usage = u;
    N.version++;
    unlock();
    N.savedTransit = u.transit;
    N.savedRoute = u.route + u.geocode;
    N.stop = false;
    N.refresh = false;
    N.changed = false;
    N.searchPending = false;
    N.running = true;
    worker_Begin();
    if (N.running) pxl_compat_add_background(worker_Step);
    return true;
}

void cmt_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 60 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
}

void cmt_Net_Set_Settings(const Cmt_Settings_t& s){
    if (!made) return;
    lock();
    *N.settings = s;
    unlock();
    N.changed = true;
}

void cmt_Net_Refresh(void){ N.refresh = true; }

bool cmt_Net_Search(const char* id, const char* q){
    if (!made || !N.running) return false;
    lock();
    copy(N.searchId, sizeof(N.searchId), id);
    copy(N.searchQuery, sizeof(N.searchQuery), q);
    N.searchPending = true;
    unlock();
    return true;
}

bool cmt_Net_Copy(Cmt_Result_t* results, Cmt_Status_t& status, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ memcpy(results, N.results, sizeof(Cmt_Result_t) * CMT_BOARDS); status = N.status; version = N.version; }
    unlock();
    return changed;
}

bool cmt_Net_Running(void){ return N.running; }
