// Tides & Water's worker. What it reads, and how often:
//   Open-Meteo Forecast (wind, rain, storms, sun): every 30 minutes.       Free.
//   Open-Meteo Marine (waves, sea temperature, currents, model tide): every hour.
//   Open-Meteo Flood (the river, 7 days back and 30 ahead): every 6 hours; its last 10 years once per place.
//   Tides: NOAA every 6 hours and its gauge every 10 minutes; WorldTides once a day (2 credits); Stormglass every
//   12 hours (2 of the free plan's 10 requests a day). Auto takes NOAA within 40 km, then WorldTides, then
//   Stormglass (when their keys are set), then Open-Meteo's model.
// About 80 Open-Meteo calls a day, far under its free 10,000.
#include "tdw_App.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "TDW_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Tides and Water)"

ArduinoJson::Allocator* tdw_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    bool changed, searchAsked, placesReady, stationReady;
    uint32_t creditsToTake;
    Tdw_Settings_t* settings;
    Tdw_Data_t* data;
    Tdw_Places_t* places;
    Tdw_Station_t station;
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;
static uint32_t omDay = 0, omCalls = 0;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

static char* get(const char* url, const char* const* headers, size_t maxBytes, Mtb_Kit_Http_t& r){
    return mtb_Kit_Http_Get(url, headers, nullptr, 0, maxBytes, r, UA);
}

static char* open_Meteo(const char* url, size_t maxBytes, Mtb_Kit_Http_t& r){
    const uint32_t day = (uint32_t)time(nullptr) / 86400;
    if (day != omDay){ omDay = day; omCalls = 0; }
    omCalls++;
    return get(url, nullptr, maxBytes, r);
}

// The NOAA station list (2 MB), read as it streams: nothing of it is kept but the nearest station.
static bool noaa_Nearest(double lat, double lon, Tdw_Station_t& out){
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);
    http.setReuse(false);
    http.setUserAgent(UA);
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    if (!http.begin(client, "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions")) return false;
    const int code = http.GET();
    if (code != 200){ http.end(); ESP_LOGW(TAG, "NOAA stations: %d", code); return false; }
    Tdw_Noaa_Scan_t* scan = (Tdw_Noaa_Scan_t*)heap_caps_malloc(sizeof(Tdw_Noaa_Scan_t), MALLOC_CAP_SPIRAM);
    char* buf = (char*)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    if (!scan || !buf){ heap_caps_free(scan); heap_caps_free(buf); http.end(); return false; }
    tdw_Noaa_Scan_Begin(*scan, lat, lon);
    Client& stream = http.getStream();
    uint32_t last = millis();
    size_t total = 0;
    while (!N.stop && total < 8u * 1024 * 1024){
        const int waiting = stream.available();
        if (waiting > 0){
            const int n = stream.read((uint8_t*)buf, waiting < 2048 ? waiting : 2048);
            if (n > 0){ tdw_Noaa_Scan_Feed(*scan, buf, (size_t)n); total += (size_t)n; last = millis(); continue; }
        } else if (!stream.connected()) break;
        if (millis() - last > 15000) break;
        delay(2);
    }
    http.end();
    const bool ok = scan->stations > 1000;
    if (ok){
        out.checked = 1;
        out.km = scan->bestKm;
        snprintf(out.id, sizeof(out.id), "%s", scan->bestKm <= TDW_NOAA_KM ? scan->bestId : "");
        snprintf(out.name, sizeof(out.name), "%s", scan->bestName);
        ESP_LOGI(TAG, "NOAA: %u stations, nearest %s %s at %.1f km", (unsigned)scan->stations, scan->bestId, scan->bestName, (double)scan->bestKm);
    }
    heap_caps_free(scan);
    heap_caps_free(buf);
    return ok;
}

struct Due_t { uint32_t weather, marine, flood, tide, level; };

static void publish_Tide(const Tdw_Tide_t& t){
    lock();
    N.data->tide = t;
    N.version++;
    unlock();
}

static void tide_Note(uint8_t state, const char* why, uint8_t source){
    lock();
    if (N.data->tide.state != TDW_OK || N.data->tide.source == source){
        N.data->tide.state = state;
        snprintf(N.data->tide.why, sizeof(N.data->tide.why), "%s", why);
    }
    N.version++;
    unlock();
}

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static Tdw_Settings_t* s;
static Tdw_Tide_t* tide;
static Tdw_Tide_t* model;
static Tdw_River_t* river;
static float* scratch;
static char* url;
static Due_t due;
static bool climateDone;
static bool modelOk;
static uint32_t stormglassDay;
static uint32_t paidAt;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Tdw_Settings_t*)heap_caps_malloc(sizeof(Tdw_Settings_t), MALLOC_CAP_SPIRAM);
    tide = (Tdw_Tide_t*)heap_caps_malloc(sizeof(Tdw_Tide_t), MALLOC_CAP_SPIRAM);
    model = (Tdw_Tide_t*)heap_caps_malloc(sizeof(Tdw_Tide_t), MALLOC_CAP_SPIRAM);
    river = (Tdw_River_t*)heap_caps_malloc(sizeof(Tdw_River_t), MALLOC_CAP_SPIRAM);
    scratch = (float*)heap_caps_malloc(4000 * sizeof(float), MALLOC_CAP_SPIRAM);
    url = (char*)heap_caps_malloc(640, MALLOC_CAP_SPIRAM);
    if (!s || !tide || !model || !river || !scratch || !url) N.stop = true;
    due = Due_t();
    climateDone = false;
    modelOk = false;
    stormglassDay = 0;
    paidAt = 0;
}

static void worker_End(void){
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    if (url){ memset(url, 0, 640); heap_caps_free(url); }
    heap_caps_free(tide);
    heap_caps_free(model);
    heap_caps_free(river);
    heap_caps_free(scratch);
    N.running = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!N.running) return;
    if (N.stop){ worker_End(); return; }
    if (millis() - wk_StepAt < 500) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u || !Mtb_Applications::internetConnectStatus) continue;
        lock();
        *s = *N.settings;
        const bool refresh = N.refresh, changed = N.changed, search = N.searchAsked;
        Tdw_Places_t* places = N.places;
        N.refresh = false;
        N.changed = false;
        unlock();
        char why[96] = "";
        Mtb_Kit_Http_t r;

        // A place search for the phone.
        if (search && places){
            lock();
            char query[64];
            snprintf(query, sizeof(query), "%s", places->query);
            N.searchAsked = false;
            unlock();
            int count = -1;
            if (tdw_Url_Places(url, 640, query)){
                char* body = open_Meteo(url, 32 * 1024, r);
                Tdw_Places_t* p = (Tdw_Places_t*)heap_caps_calloc(1, sizeof(Tdw_Places_t), MALLOC_CAP_SPIRAM);
                if (p){
                    count = tdw_Parse_Places(body, r.length, r.code, p->names, p->details, p->lats, p->lons, TDW_PLACES_MAX);
                    lock();
                    memcpy(places->names, p->names, sizeof(p->names));
                    memcpy(places->details, p->details, sizeof(p->details));
                    memcpy(places->lats, p->lats, sizeof(p->lats));
                    memcpy(places->lons, p->lons, sizeof(p->lons));
                    unlock();
                    heap_caps_free(p);
                }
                heap_caps_free(body);
            }
            lock();
            places->count = count;
            N.placesReady = true;
            unlock();
        }

        if (changed){
            due = {};
            climateDone = false;
            modelOk = false;
            paidAt = 0;
            lock();
            memset(&N.data->tide, 0, sizeof(N.data->tide));
            memset(&N.data->sea, 0, sizeof(N.data->sea));
            memset(&N.data->river, 0, sizeof(N.data->river));
            N.data->wx.known = false;
            N.version++;
            unlock();
        }
        if (refresh){
            due.weather = due.marine = due.flood = due.level = 0;
            if (!paidAt || now - paidAt > 3 * 3600) due.tide = 0;
        }
        const double rLat = s->riverPlace[0] ? s->riverLat : s->lat, rLon = s->riverPlace[0] ? s->riverLon : s->lon;

        // The NOAA station for this place, once.
        if ((s->source == TDW_SRC_AUTO || s->source == TDW_SRC_NOAA) && !s->noaaChecked && tdw_Noaa_Region(s->lat, s->lon)){
            Tdw_Station_t st = {};
            lock();
            snprintf(N.data->noaaText, sizeof(N.data->noaaText), "Looking for the nearest NOAA station");
            N.version++;
            unlock();
            if (noaa_Nearest(s->lat, s->lon, st)){
                lock();
                N.station = st;
                N.stationReady = true;
                N.settings->noaaChecked = 1;
                snprintf(N.settings->noaaId, sizeof(N.settings->noaaId), "%s", st.id);
                snprintf(N.settings->noaaName, sizeof(N.settings->noaaName), "%s", st.name);
                N.settings->noaaKm = st.km;
                *s = *N.settings;
                unlock();
                due.tide = 0;
            } else { wk_WakeAt = millis() + 30000; continue; }
        }
        lock();
        if (!tdw_Noaa_Region(s->lat, s->lon)) snprintf(N.data->noaaText, sizeof(N.data->noaaText), "NOAA: US waters only");
        else if (s->noaaId[0]) snprintf(N.data->noaaText, sizeof(N.data->noaaText), "%s, %.0f km", s->noaaName, (double)s->noaaKm);
        else if (s->noaaChecked) snprintf(N.data->noaaText, sizeof(N.data->noaaText), "No NOAA station within %.0f km", (double)TDW_NOAA_KM);
        unlock();

        // Wind, rain, storms, sun.
        if (now >= due.weather){
            Tdw_Weather_t wx;
            char* body = tdw_Url_Weather(url, 640, s->lat, s->lon) ? open_Meteo(url, 64 * 1024, r) : nullptr;
            const bool ok = tdw_Parse_Weather(body, r.length, r.code, now, s->leadHours, wx, why, sizeof(why));
            heap_caps_free(body);
            if (ok){ lock(); N.data->wx = wx; N.data->omCalls = omCalls; N.version++; unlock(); }
            else ESP_LOGW(TAG, "weather: %s", why);
            due.weather = now + (ok ? 1800 : 600);
        }
        // The sea, and Open-Meteo's tide.
        if (now >= due.marine){
            Tdw_Sea_t sea;
            int32_t off = 0;
            char tz[8] = "";
            char* body = tdw_Url_Marine(url, 640, s->lat, s->lon) ? open_Meteo(url, 64 * 1024, r) : nullptr;
            memset(model, 0, sizeof(*model));
            const bool ok = tdw_Parse_Marine(body, r.length, r.code, now, s->lat, s->lon, sea, model, off, tz, sizeof(tz), why, sizeof(why));
            heap_caps_free(body);
            if (ok){
                modelOk = model->state == TDW_OK || model->state == TDW_NONE;
                lock();
                N.data->sea = sea;
                N.data->utcOffset = off;
                snprintf(N.data->tzName, sizeof(N.data->tzName), "%s", tz);
                N.data->omCalls = omCalls;
                // Open-Meteo's tide is shown when it is the source, or Auto found nothing better.
                const bool useModel = s->source == TDW_SRC_OPENMETEO
                                   || (s->source == TDW_SRC_AUTO && (N.data->tide.source == TDW_SRC_OPENMETEO || N.data->tide.state != TDW_OK));
                if (useModel) N.data->tide = *model;
                N.version++;
                unlock();
            } else ESP_LOGW(TAG, "marine: %s", why);
            due.marine = now + (ok ? 3600 : 600);
        }
        // The river.
        if (now >= due.flood){
            *river = N.data->river;
            char* body = tdw_Url_Flood(url, 640, rLat, rLon) ? open_Meteo(url, 32 * 1024, r) : nullptr;
            const bool ok = tdw_Parse_Flood(body, r.length, r.code, now, rLat, rLon, *river, why, sizeof(why));
            heap_caps_free(body);
            if (ok && river->known && !climateDone){
                body = tdw_Url_Flood_Climate(url, 640, rLat, rLon, now) ? open_Meteo(url, 160 * 1024, r) : nullptr;
                climateDone = tdw_Parse_Flood_Climate(body, r.length, r.code, *river, scratch, 4000);
                heap_caps_free(body);
                if (!climateDone) river->climate = false;
            }
            if (ok){ lock(); N.data->river = *river; N.data->omCalls = omCalls; N.version++; unlock(); }
            due.flood = now + (ok ? 6 * 3600 : 900);
        }

        // Tides from a station or a paid provider.
        if (now >= due.tide){
            uint8_t order[3];
            int count = 0;
            const bool noaa = s->noaaId[0] != 0;
            if (s->source == TDW_SRC_AUTO){
                if (noaa) order[count++] = TDW_SRC_NOAA;
                if (s->worldTidesKey[0]) order[count++] = TDW_SRC_WORLDTIDES;
                if (s->stormglassKey[0]) order[count++] = TDW_SRC_STORMGLASS;
            } else if (s->source != TDW_SRC_OPENMETEO) order[count++] = s->source;
            uint32_t next = now + 6 * 3600;
            bool got = false;
            for (int i = 0; i < count && !got && !N.stop; i++){
                memset(tide, 0, sizeof(*tide));
                uint8_t st = TDW_ERROR;
                if (order[i] == TDW_SRC_NOAA){
                    if (!noaa){ tide_Note(TDW_NONE, tdw_Noaa_Region(s->lat, s->lon) ? "No NOAA station near this place" : "NOAA covers US waters only", TDW_SRC_NOAA); continue; }
                    char* body = tdw_Url_Noaa_Hilo(url, 640, s->noaaId, s->datum, now) ? get(url, nullptr, 32 * 1024, r) : nullptr;
                    st = tdw_Parse_Noaa_Hilo(body, r.length, r.code, *tide);
                    heap_caps_free(body);
                    if (st == TDW_OK){
                        snprintf(tide->station, sizeof(tide->station), "%s", s->noaaName);
                        tide->stationKm = s->noaaKm;
                        snprintf(tide->datum, sizeof(tide->datum), "%s", s->datum ? "MSL" : "MLLW");
                        next = now + 6 * 3600;
                        due.level = 0;
                    }
                } else if (order[i] == TDW_SRC_WORLDTIDES){
                    if (!s->worldTidesKey[0]){ tide_Note(TDW_KEY, "Add your WorldTides key in the phone app", TDW_SRC_WORLDTIDES); continue; }
                    uint32_t credits = 0;
                    char* body = tdw_Url_WorldTides(url, 640, s->lat, s->lon, s->datum, now, s->worldTidesKey) ? get(url, nullptr, 64 * 1024, r) : nullptr;
                    memset(url, 0, 640);
                    st = tdw_Parse_WorldTides(body, r.length, r.code, *tide, credits);
                    heap_caps_free(body);
                    if (credits){ lock(); N.creditsToTake += credits; unlock(); }
                    if (st == TDW_OK){ next = now + 24 * 3600; paidAt = now; }
                } else if (order[i] == TDW_SRC_STORMGLASS){
                    if (!s->stormglassKey[0]){ tide_Note(TDW_KEY, "Add your Stormglass key in the phone app", TDW_SRC_STORMGLASS); continue; }
                    if (stormglassDay == now / 86400 && N.data->sgQuota && N.data->sgUsed >= N.data->sgQuota){
                        tide_Note(TDW_QUOTA, "Stormglass: today's requests are used up", TDW_SRC_STORMGLASS);
                        continue;
                    }
                    const char* headers[] = {"Authorization", s->stormglassKey, nullptr};
                    uint32_t quota = N.data->sgQuota, used = N.data->sgUsed;
                    char* body = tdw_Url_Stormglass(url, 640, s->lat, s->lon, s->datum, now) ? get(url, headers, 32 * 1024, r) : nullptr;
                    st = tdw_Parse_Stormglass(body, r.length, r.code, *tide, quota, used);
                    heap_caps_free(body);
                    stormglassDay = now / 86400;
                    if (st == TDW_QUOTA) used = quota ? quota : used;
                    lock();
                    N.data->sgQuota = quota;
                    N.data->sgUsed = used;
                    unlock();
                    if (st == TDW_OK){ next = now + 12 * 3600; paidAt = now; }
                }
                if (st == TDW_OK){
                    tide->state = TDW_OK;
                    tide->updated = now;
                    publish_Tide(*tide);
                    got = true;
                } else {
                    ESP_LOGW(TAG, "%s: %s", tdw_Source_Name(order[i]), tide->why);
                    tide_Note(st, tide->why, order[i]);
                    if (st == TDW_QUOTA) next = (now / 86400 + 1) * 86400 + 300;
                    else if (st == TDW_KEY) next = now + 3600;
                    else next = now + 900;
                }
            }
            // Auto with nothing better (or Open-Meteo chosen): the model's tide from the marine reading.
            if (!got && modelOk && (s->source == TDW_SRC_AUTO || s->source == TDW_SRC_OPENMETEO)){
                lock();
                const bool keepNote = N.data->tide.state == TDW_KEY || N.data->tide.state == TDW_QUOTA;
                char note[72];
                snprintf(note, sizeof(note), "%s", N.data->tide.why);
                N.data->tide = *model;
                if (keepNote && model->state == TDW_OK) snprintf(N.data->tide.why, sizeof(N.data->tide.why), "%s", note);
                N.version++;
                unlock();
                if (count == 0) next = now + 3600;
            }
            due.tide = next;
        }
        // The NOAA gauge, where there is one.
        lock();
        const bool noaaShown = N.data->tide.source == TDW_SRC_NOAA && N.data->tide.state == TDW_OK;
        unlock();
        if (noaaShown && now >= due.level){
            float h;
            uint32_t at;
            char* body = tdw_Url_Noaa_Level(url, 640, s->noaaId, s->datum) ? get(url, nullptr, 8 * 1024, r) : nullptr;
            const bool ok = tdw_Parse_Noaa_Level(body, r.length, h, at);
            heap_caps_free(body);
            lock();
            N.data->tide.observed = ok;
            if (ok){ N.data->tide.obsH = h; N.data->tide.obsT = at; }
            N.version++;
            unlock();
            due.level = now + (ok ? 600 : 3 * 3600);     // a station without a gauge: try now and then
        }
    } while (0);
}

bool tdw_Net_Start(const Tdw_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Tdw_Settings_t*)heap_caps_calloc(1, sizeof(Tdw_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Tdw_Data_t*)heap_caps_calloc(1, sizeof(Tdw_Data_t), MALLOC_CAP_SPIRAM);
        N.places = (Tdw_Places_t*)heap_caps_calloc(1, sizeof(Tdw_Places_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data || !N.places) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Tdw_Data_t));
    N.data->wtCredits = settings.wtCredits;
    N.version++;
    N.placesReady = N.stationReady = N.searchAsked = false;
    N.creditsToTake = 0;
    unlock();
    N.stop = false;
    N.refresh = false;
    N.changed = false;
    N.running = true;
    worker_Begin();
    if (N.running) pxl_compat_add_background(worker_Step);
    return true;
}

void tdw_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    lock();
    memset(N.settings->worldTidesKey, 0, sizeof(N.settings->worldTidesKey));
    memset(N.settings->stormglassKey, 0, sizeof(N.settings->stormglassKey));
    unlock();
}

void tdw_Net_Set_Settings(const Tdw_Settings_t& settings){
    if (!made) return;
    lock();
    Tdw_Settings_t& o = *N.settings;
    const bool placeChanged = o.lat != settings.lat || o.lon != settings.lon || o.riverLat != settings.riverLat
                           || o.riverLon != settings.riverLon || strcmp(o.riverPlace, settings.riverPlace) != 0;
    const bool tideChanged = o.source != settings.source || o.datum != settings.datum || strcmp(o.worldTidesKey, settings.worldTidesKey) != 0
                          || strcmp(o.stormglassKey, settings.stormglassKey) != 0 || o.leadHours != settings.leadHours;
    o = settings;
    if (placeChanged || tideChanged) N.changed = true;
    unlock();
}

void tdw_Net_Refresh(void){ N.refresh = true; }

bool tdw_Net_Copy(Tdw_Data_t& d, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ d = *N.data; version = N.version; }
    unlock();
    return changed;
}

bool tdw_Net_Search(const char* searchId, const char* query){
    if (!made || !N.running) return false;
    lock();
    const bool busy = N.searchAsked;
    if (!busy){
        snprintf(N.places->searchId, sizeof(N.places->searchId), "%s", searchId);
        snprintf(N.places->query, sizeof(N.places->query), "%s", query);
        N.places->count = 0;
        N.searchAsked = true;
        N.placesReady = false;
    }
    unlock();
    return !busy;
}

bool tdw_Net_Take_Places(Tdw_Places_t& p){
    if (!made) return false;
    lock();
    const bool ready = N.placesReady;
    if (ready){ p = *N.places; N.placesReady = false; }
    unlock();
    return ready;
}

bool tdw_Net_Take_Station(Tdw_Station_t& st){
    if (!made) return false;
    lock();
    const bool ready = N.stationReady;
    if (ready){ st = N.station; N.stationReady = false; }
    unlock();
    return ready;
}

bool tdw_Net_Take_Credits(uint32_t& credits){
    if (!made) return false;
    lock();
    credits = N.creditsToTake;
    N.creditsToTake = 0;
    unlock();
    return credits != 0;
}
