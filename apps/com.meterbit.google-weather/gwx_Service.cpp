#include "gwx_Service.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "mtb_github.h"
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char TAG[] = "GWX_SERVICE";

#define GWX_USER_AGENT      "Pixlpal-GoogleWeather/1.0 (ESP32 LED display)"
#define GWX_WEATHER_HOST    "https://weather.googleapis.com/v1"
#define GWX_GEOCODE_URL     "https://maps.googleapis.com/maps/api/geocode/json?address=%s&key=%s"
#define GWX_MAX_BODY        (128 * 1024)
#define GWX_CACHE_FILE      "/weather/google.bin"
#define GWX_WORKER_STACK    10240
#define GWX_MANUAL_GAP      300

enum Gwx_Feed_t : uint8_t { F_CURRENT = 0, F_HOURS, F_DAYS, F_ALERTS, F_COUNT };
#define GWX_SEARCH_BIT  (1u << F_COUNT)

struct Gwx_Cache_Header_t { uint32_t magic; uint16_t layout; uint16_t reserved; uint32_t size; uint32_t crc; };

static SemaphoreHandle_t serviceLock = NULL;
static Gwx_Data_t* published = nullptr;     // PSRAM
static uint32_t publishedVersion = 1;
static Gwx_Settings_t current;
static uint32_t generation = 0;
static bool started = false, workerBusy = false;
static uint32_t due[F_COUNT];
static uint8_t fails = 0;
static uint32_t lastManual = 0, interval = 0, lastFetch = 0, lastSave = 0;
static char searchQuery[64];
static Gwx_Search_Fn searchCallback = nullptr;

static void gwx_Lock(void){ xSemaphoreTake(serviceLock, portMAX_DELAY); }
static void gwx_Unlock(void){ xSemaphoreGive(serviceLock); }

//##############################################################################################################
static void gwx_Save_Cache(const Gwx_Data_t& data){
    static const char tempPath[] = GWX_CACHE_FILE ".tmp";
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Gwx_Cache_Header_t header = {GWX_DATA_MAGIC, GWX_DATA_LAYOUT, 0, sizeof(Gwx_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header) && file.write((const uint8_t*)&data, sizeof(data)) == sizeof(data);
    file.close();
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, GWX_CACHE_FILE)){ LittleFS.remove(GWX_CACHE_FILE); LittleFS.rename(tempPath, GWX_CACHE_FILE); }
}

static bool gwx_Load_Cache(Gwx_Data_t& data){
    if (!LittleFS.exists(GWX_CACHE_FILE)) return false;
    File file = LittleFS.open(GWX_CACHE_FILE, "r");
    if (!file) return false;
    Gwx_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(data) && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == GWX_DATA_MAGIC && header.layout == GWX_DATA_LAYOUT && header.size == sizeof(data)
                     && file.read((uint8_t*)&data, sizeof(data)) == sizeof(data)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data)) == header.crc
                     && data.hourCount <= GWX_HOURS && data.dayCount <= GWX_DAYS && data.alertCount <= GWX_ALERTS;
    file.close();
    return usable;
}

// A new month: the count starts again.
static void gwx_Roll_Month(uint32_t now){
    const uint32_t month = gwx_Month_Of(now);
    if (published->usedMonth != month){ published->usedMonth = month; published->usedCalls = 0; publishedVersion++; }
}

static void gwx_Plan_Interval(uint32_t now){
    uint32_t left = 0;
    gwx_Month_Of(now, &left);
    interval = gwx_Interval(current.monthlyBudget, published->usedCalls, left, current.alerts);
}

//##############################################################################################################
static char* gwx_Get(const char* url, int& code, Mtb_Kit_Http_t& http){
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, GWX_MAX_BODY, http, GWX_USER_AGENT);
    code = http.code;
    return body;
}

static void gwx_Run_Search(const char* query, const char* key){
    char encoded[160], url[320];
    mtb_Kit_Url_Encode(encoded, sizeof(encoded), query);
    snprintf(url, sizeof(url), GWX_GEOCODE_URL, encoded, key);
    Mtb_Kit_Http_t http;
    int code = 0;
    char* body = gwx_Get(url, code, http);
    memset(url, 0, sizeof(url));
    Gwx_Place_t places[GWX_PLACES];
    int count = -1;
    char error[120] = "Could not reach Google";
    if (body){
        JsonDocument doc(mtb_Kit_Json_Allocator());
        if (!deserializeJson(doc, body, http.length)) count = gwx_Parse_Places(doc, places, GWX_PLACES, error, sizeof(error));
        heap_caps_free(body);
    }
    if (code > 0){ gwx_Lock(); published->usedCalls++; publishedVersion++; gwx_Unlock(); }
    if (count < 0 && strstr(error, "REQUEST_DENIED")) snprintf(error, sizeof(error), "Google refused: enable the Geocoding API for this key");
    const Gwx_Search_Fn callback = searchCallback;
    if (callback) callback(query, places, count < 0 ? 0 : count, count < 0 ? error : nullptr);
}

static void gwx_Worker_Task(void* parameter){
    const uint8_t mask = (uint8_t)(uintptr_t)parameter;
    Gwx_Data_t* work = (Gwx_Data_t*)heap_caps_malloc(sizeof(Gwx_Data_t), MALLOC_CAP_SPIRAM);
    Gwx_Settings_t* s = (Gwx_Settings_t*)heap_caps_malloc(sizeof(Gwx_Settings_t), MALLOC_CAP_SPIRAM);
    bool save = false;
    if (work && s){
        gwx_Lock();
        *s = current;
        char query[sizeof(searchQuery)];
        memcpy(query, searchQuery, sizeof(query));
        searchQuery[0] = '\0';
        const uint32_t gen = generation;
        gwx_Unlock();
        if ((mask & GWX_SEARCH_BIT) && query[0]) gwx_Run_Search(query, s->apiKey);

        static const char* const PATHS[F_COUNT] = {"currentConditions:lookup", "forecast/hours:lookup", "forecast/days:lookup", "publicAlerts:lookup"};
        static const char* const NAMES[F_COUNT] = {"Current", "Hours", "Days", "Alerts"};
        for (uint8_t f = 0; f < F_COUNT; f++){
            if (!(mask & (1u << f))) continue;
            char url[384];
            size_t used = snprintf(url, sizeof(url), GWX_WEATHER_HOST "/%s?key=%s&location.latitude=%.5f&location.longitude=%.5f&languageCode=en",
                                   PATHS[f], s->apiKey, s->latitude, s->longitude);
            if (f != F_ALERTS) used += snprintf(url + used, sizeof(url) - used, "&unitsSystem=%s", s->imperial ? "IMPERIAL" : "METRIC");
            if (f == F_HOURS) snprintf(url + used, sizeof(url) - used, "&hours=%d&pageSize=%d", GWX_HOURS, GWX_HOURS);
            if (f == F_DAYS) snprintf(url + used, sizeof(url) - used, "&days=%d&pageSize=%d", GWX_DAYS, GWX_DAYS);
            Mtb_Kit_Http_t http;
            int code = 0;
            char* body = gwx_Get(url, code, http);
            memset(url, 0, sizeof(url));
            uint8_t result = GWX_ERR_NET;
            char text[96] = "";
            gwx_Lock();
            memcpy(work, published, sizeof(Gwx_Data_t));
            gwx_Unlock();
            if (body){
                JsonDocument doc(mtb_Kit_Json_Allocator());
                const bool parsed = !deserializeJson(doc, body, http.length);
                heap_caps_free(body);
                result = parsed ? gwx_Error_Of(doc, code, text, sizeof(text)) : (uint8_t)GWX_ERR_PARSE;
                if (result == GWX_OK){
                    switch (f){
                        case F_CURRENT: result = gwx_Parse_Current(doc, work->current); break;
                        case F_HOURS: result = gwx_Parse_Hours(doc, *work); break;
                        case F_DAYS: result = gwx_Parse_Days(doc, *work); break;
                        default: result = gwx_Parse_Alerts(doc, *work); break;
                    }
                }
            } else if (code > 0) result = code >= 500 ? GWX_ERR_SERVER : GWX_ERR_PARSE;
            const uint32_t now = (uint32_t)time(nullptr);
            ESP_LOGI(TAG, "%s: HTTP %d, result %u", NAMES[f], code, (unsigned)result);
            gwx_Lock();
            gwx_Roll_Month(now);
            if (code > 0) published->usedCalls++;
            lastFetch = now;
            const bool same = gen == generation;
            if (result == GWX_OK && same){
                switch (f){
                    case F_CURRENT: published->current = work->current; break;
                    case F_HOURS:
                        memcpy(published->hours, work->hours, sizeof(published->hours));
                        published->hourCount = work->hourCount;
                        published->utcOffset = work->utcOffset;
                        break;
                    case F_DAYS: memcpy(published->days, work->days, sizeof(published->days)); published->dayCount = work->dayCount; break;
                    default: memcpy(published->alerts, work->alerts, sizeof(published->alerts)); published->alertCount = work->alertCount; break;
                }
                published->fetched[f] = now;
                published->latitude = s->latitude;
                published->longitude = s->longitude;
                published->imperial = s->imperial;
                published->lastError = GWX_OK;
                published->errorText[0] = '\0';
                fails = 0;
                save = true;
            } else if (result != GWX_OK){
                // Alerts the region does not have are not an error worth showing.
                if (!(f == F_ALERTS && result == GWX_ERR_ARGUMENT)){
                    published->lastError = result;
                    snprintf(published->errorText, sizeof(published->errorText), "%s", text);
                }
            }
            gwx_Plan_Interval(now);
            const uint32_t step = interval ? interval : 6UL * 3600;
            uint32_t next;
            switch (result){
                case GWX_OK:
                    next = f == F_CURRENT ? step : f == F_HOURS ? (step * 4 > 3600 ? step * 4 : 3600)
                         : f == F_DAYS ? (step * 12 > 3 * 3600 ? step * 12 : 3 * 3600) : step * 2;
                    break;
                case GWX_ERR_DENIED: next = 6UL * 3600; break;
                case GWX_ERR_QUOTA: next = 3600; break;
                case GWX_ERR_ARGUMENT: next = f == F_ALERTS ? 24UL * 3600 : 12UL * 3600; break;
                default: {
                    if (fails < 8) fails++;
                    next = 120UL << (fails - 1);
                    if (next > 3UL * 3600) next = 3UL * 3600;
                    break;
                }
            }
            due[f] = now + next;
            publishedVersion++;
            gwx_Unlock();
            if (result == GWX_ERR_DENIED || result == GWX_ERR_QUOTA) break;
            vTaskDelay(pdMS_TO_TICKS(400));
        }
        const uint32_t now = (uint32_t)time(nullptr);
        if (save && (lastSave == 0 || now - lastSave >= 900)){
            gwx_Lock();
            memcpy(work, published, sizeof(Gwx_Data_t));
            gwx_Unlock();
            gwx_Save_Cache(*work);
            lastSave = now;
        }
    }
    heap_caps_free(work);
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    gwx_Lock();
    workerBusy = false;
    gwx_Unlock();
    vTaskDelete(NULL);
}

//##############################################################################################################
void gwx_Service_Begin(const Gwx_Settings_t& settings){
    if (serviceLock == NULL){
        serviceLock = xSemaphoreCreateMutex();
        published = (Gwx_Data_t*)heap_caps_calloc(1, sizeof(Gwx_Data_t), MALLOC_CAP_SPIRAM);
        if (serviceLock == NULL || published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    if (!started){
        Gwx_Data_t* fresh = (Gwx_Data_t*)heap_caps_calloc(1, sizeof(Gwx_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!gwx_Load_Cache(*fresh)) memset(fresh, 0, sizeof(*fresh));
        fresh->magic = GWX_DATA_MAGIC;
        fresh->layout = GWX_DATA_LAYOUT;
        fresh->size = sizeof(Gwx_Data_t);
        gwx_Lock();
        memcpy(published, fresh, sizeof(*fresh));
        current = settings;
        gwx_Settings_Clamp(current);
        const uint32_t now = (uint32_t)time(nullptr);
        const bool samePlace = fabs(published->latitude - current.latitude) < 1e-4 && fabs(published->longitude - current.longitude) < 1e-4
                            && published->imperial == current.imperial;
        if (!samePlace){
            published->current.valid = 0;
            published->hourCount = published->dayCount = published->alertCount = 0;
            memset(published->fetched, 0, sizeof(published->fetched));
        }
        gwx_Roll_Month(now);
        gwx_Plan_Interval(now);
        // From the cache: each read when it falls due.
        const uint32_t steps[F_COUNT] = {interval, interval * 4 > 3600 ? interval * 4 : 3600, interval * 12 > 10800 ? interval * 12 : 10800, interval * 2};
        for (uint8_t f = 0; f < F_COUNT; f++){
            const uint32_t fetched = published->fetched[f];
            due[f] = (fetched && interval && now - fetched < steps[f]) ? fetched + steps[f] : now + f;
        }
        started = true;
        publishedVersion++;
        gwx_Unlock();
        heap_caps_free(fresh);
    }
    gwx_Service_Set_Settings(settings);
}

void gwx_Service_Set_Settings(const Gwx_Settings_t& settings){
    if (!started) return;
    Gwx_Settings_t next = settings;
    gwx_Settings_Clamp(next);
    gwx_Lock();
    const uint32_t now = (uint32_t)time(nullptr);
    const bool newPlace = fabs(next.latitude - current.latitude) > 1e-6 || fabs(next.longitude - current.longitude) > 1e-6;
    const bool newUnits = next.imperial != current.imperial;
    const bool newKey = strcmp(next.apiKey, current.apiKey) != 0;
    const bool alertsOn = next.alerts && !current.alerts;
    current = next;
    if (newPlace || newUnits || newKey){
        generation++;
        for (uint8_t f = 0; f < F_COUNT; f++) due[f] = now + f;
        published->lastError = GWX_OK;
        published->errorText[0] = '\0';
        fails = 0;
    }
    if (newPlace){
        published->current.valid = 0;
        published->hourCount = published->dayCount = published->alertCount = 0;
        memset(published->fetched, 0, sizeof(published->fetched));
    }
    if (alertsOn) due[F_ALERTS] = now;
    if (!current.alerts) published->alertCount = 0;
    gwx_Plan_Interval(now);
    publishedVersion++;
    gwx_Unlock();
}

void gwx_Service_Poll(bool online, uint32_t now){
    if (!started || now == 0) return;
    uint8_t mask = 0;
    gwx_Lock();
    gwx_Roll_Month(now);
    gwx_Plan_Interval(now);
    if (!workerBusy && online && current.apiKey[0]){
        if (searchQuery[0]) mask |= GWX_SEARCH_BIT;
        if (interval){
            for (uint8_t f = 0; f < F_COUNT; f++){
                if (f == F_ALERTS && !current.alerts) continue;
                if ((int32_t)(now - due[f]) >= 0){ mask |= 1u << f; due[f] = now + 120; }
            }
        }
        if (mask) workerBusy = true;
    }
    gwx_Unlock();
    if (mask == 0) return;
    if (xTaskCreatePinnedToCore(gwx_Worker_Task, "gwxFetch", GWX_WORKER_STACK, (void*)(uintptr_t)mask, 1, NULL, tskNO_AFFINITY) != pdPASS){
        gwx_Lock();
        workerBusy = false;
        gwx_Unlock();
    }
}

bool gwx_Service_Copy(Gwx_Data_t& out, uint32_t& version){
    if (!started) return false;
    gwx_Lock();
    const bool changed = version != publishedVersion;
    if (changed){ memcpy(&out, published, sizeof(out)); version = publishedVersion; }
    gwx_Unlock();
    return changed;
}

void gwx_Service_Get_Status(Gwx_Status_t& status, char* errorText, size_t errorSize){
    memset(&status, 0, sizeof(status));
    if (errorSize) errorText[0] = '\0';
    if (!started) return;
    gwx_Lock();
    status.started = true;
    status.busy = workerBusy;
    status.keyMissing = current.apiKey[0] == '\0';
    status.denied = published->lastError == GWX_ERR_DENIED;
    status.quotaUsed = published->lastError == GWX_ERR_QUOTA || interval == 0;
    status.failing = published->lastError == GWX_ERR_NET || published->lastError == GWX_ERR_SERVER || published->lastError == GWX_ERR_PARSE;
    status.interval = interval;
    status.usedCalls = published->usedCalls;
    status.lastFetch = lastFetch;
    if (errorSize) snprintf(errorText, errorSize, "%s", published->errorText);
    gwx_Unlock();
}

bool gwx_Service_Refresh_Now(uint32_t now){
    if (!started || now == 0) return false;
    gwx_Lock();
    const bool accepted = (lastManual == 0 || now - lastManual >= GWX_MANUAL_GAP) && current.apiKey[0] && interval > 0;
    if (accepted){ lastManual = now; due[F_CURRENT] = now; due[F_HOURS] = now; }
    gwx_Unlock();
    return accepted;
}

void gwx_Service_Set_Search_Callback(Gwx_Search_Fn fn){ searchCallback = fn; }

bool gwx_Service_Search(const char* query){
    if (!started || query == nullptr) return false;
    const size_t n = strlen(query);
    if (n < 2 || n >= sizeof(searchQuery)) return false;
    gwx_Lock();
    const bool accepted = searchQuery[0] == '\0' && current.apiKey[0];
    if (accepted) memcpy(searchQuery, query, n + 1);
    gwx_Unlock();
    return accepted;
}
