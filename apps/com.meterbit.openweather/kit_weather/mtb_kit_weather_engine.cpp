#include "mtb_kit_weather.h"
#include "mtb_kit_http.h"
#include "mtb_github.h"
#include <Arduino.h>
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

static const char TAG[] = "MTB_KIT_WX";

#define KIT_WX_MAGIC        0x4B575831UL    // "KWX1"
#define KIT_WX_LAYOUT       1
#define KIT_WX_STACK        10240
#define KIT_WX_MANUAL_GAP   300
#define KIT_WX_SAVE_GAP     900
#define KIT_WX_IDLE_CHECK   600             // A feed not wanted now is asked about again after this.

struct Kit_Wx_Cache_Header_t { uint32_t magic; uint16_t layout; uint16_t reserved; uint32_t size; uint32_t crc; };

static void wx_Lock(Mtb_Kit_Wx_t& e){ xSemaphoreTake((SemaphoreHandle_t)e.lock, portMAX_DELAY); }
static void wx_Unlock(Mtb_Kit_Wx_t& e){ xSemaphoreGive((SemaphoreHandle_t)e.lock); }

static void wx_Save_Cache(const Mtb_Kit_Wx_Provider_t& p, const Mtb_Kit_Wx_Data_t& data){
    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", p.cacheFile);
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Kit_Wx_Cache_Header_t header = {KIT_WX_MAGIC, KIT_WX_LAYOUT, 0, sizeof(data), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header) && file.write((const uint8_t*)&data, sizeof(data)) == sizeof(data);
    file.close();
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, p.cacheFile)){ LittleFS.remove(p.cacheFile); LittleFS.rename(tempPath, p.cacheFile); }
}

static bool wx_Load_Cache(const Mtb_Kit_Wx_Provider_t& p, Mtb_Kit_Wx_Data_t& data){
    if (!LittleFS.exists(p.cacheFile)) return false;
    File file = LittleFS.open(p.cacheFile, "r");
    if (!file) return false;
    Kit_Wx_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(data) && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == KIT_WX_MAGIC && header.layout == KIT_WX_LAYOUT && header.size == sizeof(data)
                     && file.read((uint8_t*)&data, sizeof(data)) == sizeof(data)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data)) == header.crc
                     && data.hourCount <= KIT_WX_HOURS && data.dayCount <= KIT_WX_DAYS && data.alertCount <= KIT_WX_ALERTS;
    file.close();
    return usable;
}

static void wx_Roll(Mtb_Kit_Wx_t& e, uint32_t now){
    const uint32_t day = now / 86400, month = mtb_Kit_Wx_Month_Of(now);
    if (e.published->usedDay != day){ e.published->usedDay = day; e.published->usedToday = 0; e.version++; }
    if (e.published->usedMonth != month){ e.published->usedMonth = month; e.published->usedCalls = 0; e.version++; }
}

static void wx_Count_Call(Mtb_Kit_Wx_t& e, uint32_t now){
    wx_Roll(e, now);
    e.published->usedToday++;
    e.published->usedCalls++;
}

static bool wx_Same_Place(const Mtb_Kit_Wx_Data_t& d, const Mtb_Kit_Wx_Config_t& c){
    return fabs(d.latitude - c.latitude) < 1e-4 && fabs(d.longitude - c.longitude) < 1e-4 && d.imperial == c.imperial;
}

static bool wx_Has_Key(const Mtb_Kit_Wx_t& e){ return e.provider->keyOptional || e.config.apiKey[0]; }

//##############################################################################################################
static void wx_Run_Search(Mtb_Kit_Wx_t& e, const Mtb_Kit_Wx_Config_t& config, const char* query){
    const Mtb_Kit_Wx_Provider_t& p = *e.provider;
    Mtb_Kit_Wx_Place_t places[KIT_WX_PLACES];
    int count = -1;
    char error[120] = "Could not reach the weather service";
    char url[384];
    if (p.searchRequest && p.searchRequest(config, query, url, sizeof(url))){
        Mtb_Kit_Http_t http;
        char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 64 * 1024, http, p.userAgent);
        memset(url, 0, sizeof(url));
        JsonDocument doc(mtb_Kit_Json_Allocator());
        const bool parsed = body && !deserializeJson(doc, body, http.length);
        heap_caps_free(body);
        if (http.code > 0){
            count = p.parsePlaces(http.code, parsed ? doc.as<JsonVariantConst>() : JsonVariantConst(), places, KIT_WX_PLACES, error, sizeof(error));
            wx_Lock(e);
            wx_Count_Call(e, (uint32_t)time(nullptr));
            e.version++;
            wx_Unlock(e);
        }
    }
    const Mtb_Kit_Wx_Search_Fn fn = e.searchFn;
    if (fn) fn(query, places, count < 0 ? 0 : count, count < 0 ? error : nullptr);
}

struct Kit_Wx_Job_t { Mtb_Kit_Wx_t* engine; uint8_t mask; bool search; };

static void wx_Worker(void* parameter){
    const Kit_Wx_Job_t job = *(Kit_Wx_Job_t*)parameter;
    heap_caps_free(parameter);
    Mtb_Kit_Wx_t& e = *job.engine;
    const Mtb_Kit_Wx_Provider_t& p = *e.provider;
    Mtb_Kit_Wx_Data_t* work = (Mtb_Kit_Wx_Data_t*)heap_caps_malloc(sizeof(Mtb_Kit_Wx_Data_t), MALLOC_CAP_SPIRAM);
    Mtb_Kit_Wx_Config_t* config = (Mtb_Kit_Wx_Config_t*)heap_caps_malloc(sizeof(Mtb_Kit_Wx_Config_t), MALLOC_CAP_SPIRAM);
    bool save = false;
    if (work && config){
        wx_Lock(e);
        *config = e.config;
        char query[sizeof(e.searchQuery)];
        memcpy(query, e.searchQuery, sizeof(query));
        e.searchQuery[0] = '\0';
        wx_Unlock(e);
        if (job.search && query[0]) wx_Run_Search(e, *config, query);

        bool first = true;
        for (uint8_t f = 0; f < p.feedCount; f++){
            if (!(job.mask & (1u << f))) continue;
            wx_Lock(e);
            *config = e.config;
            memcpy(work, e.published, sizeof(*work));
            const uint32_t generation = e.generation;
            wx_Unlock(e);
            char url[512];
            const char* headers[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
            uint32_t now = (uint32_t)time(nullptr);
            if (!p.request(*config, f, *work, url, sizeof(url), headers)){
                wx_Lock(e);
                e.due[f] = now + KIT_WX_IDLE_CHECK;
                wx_Unlock(e);
                continue;
            }
            if (!first) vTaskDelay(pdMS_TO_TICKS(400));
            first = false;
            Mtb_Kit_Http_t http;
            char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, p.maxBody, http, p.userAgent);
            memset(url, 0, sizeof(url));            // It may hold the key.
            char text[96] = "";
            uint8_t result = KIT_WX_NET;
            if (http.code > 0){
                JsonDocument doc(mtb_Kit_Json_Allocator());
                const bool parsed = body && !deserializeJson(doc, body, http.length);
                result = p.parse(f, http.code, parsed ? doc.as<JsonVariantConst>() : JsonVariantConst(), *config, *work, text, sizeof(text));
            }
            heap_caps_free(body);
            now = (uint32_t)time(nullptr);
            ESP_LOGI(TAG, "%s feed %u: HTTP %d, result %u", p.label, (unsigned)f, http.code, (unsigned)result);

            wx_Lock(e);
            const bool same = generation == e.generation;
            if (http.code > 0) wx_Count_Call(e, now);
            e.lastFetch = now;
            if (same){
                // The plan learned is kept whatever the answer; the weather only from a good one.
                const uint32_t usedDay = e.published->usedDay, usedToday = e.published->usedToday;
                const uint32_t usedMonth = e.published->usedMonth, usedCalls = e.published->usedCalls;
                if (result == KIT_WX_OK){
                    work->fetched[f] = now;
                    work->latitude = config->latitude;
                    work->longitude = config->longitude;
                    work->imperial = config->imperial;
                    work->lastError = KIT_WX_OK;
                    work->errorText[0] = '\0';
                    memcpy(e.published, work, sizeof(*work));
                    e.fails = 0;
                    save = true;
                } else {
                    e.published->plan = work->plan;
                    if (result != KIT_WX_PLAN){
                        e.published->lastError = result;
                        snprintf(e.published->errorText, sizeof(e.published->errorText), "%s", text);
                    }
                }
                e.published->usedDay = usedDay; e.published->usedToday = usedToday;
                e.published->usedMonth = usedMonth; e.published->usedCalls = usedCalls;
                e.published->feedResult[f] = result;
            }
            uint32_t next;
            switch (result){
                case KIT_WX_OK: {
                    const uint32_t every = p.every(f, e.config, *e.published, now);
                    next = every ? every : KIT_WX_IDLE_CHECK;
                    break;
                }
                case KIT_WX_PLAN: next = 24UL * 3600; break;
                case KIT_WX_KEY: next = 1800; break;                // New keys can take a while to work.
                case KIT_WX_QUOTA: next = (now / 86400 + 1) * 86400 + 90 - now; break;
                case KIT_WX_RATE: next = 65; break;
                case KIT_WX_BAD: next = 12UL * 3600; break;
                default: {
                    if (e.fails < 8) e.fails++;
                    next = 120UL << (e.fails - 1);
                    if (next > 3UL * 3600) next = 3UL * 3600;
                    break;
                }
            }
            e.due[f] = now + next;
            if (result == KIT_WX_KEY || result == KIT_WX_QUOTA) for (uint8_t g = 0; g < p.feedCount; g++) if ((int32_t)(e.due[g] - (now + next)) < 0) e.due[g] = now + next;
            e.version++;
            wx_Unlock(e);
            if (result == KIT_WX_KEY || result == KIT_WX_QUOTA) break;
        }
        const uint32_t now = (uint32_t)time(nullptr);
        if (save && (e.lastSave == 0 || now - e.lastSave >= KIT_WX_SAVE_GAP)){
            wx_Lock(e);
            memcpy(work, e.published, sizeof(*work));
            wx_Unlock(e);
            wx_Save_Cache(p, *work);
            e.lastSave = now;
        }
    }
    heap_caps_free(work);
    if (config){ memset(config, 0, sizeof(*config)); heap_caps_free(config); }
    wx_Lock(e);
    e.busy = false;
    wx_Unlock(e);
    vTaskDelete(NULL);
}

//##############################################################################################################
void mtb_Kit_Wx_Begin(Mtb_Kit_Wx_t& e, const Mtb_Kit_Wx_Provider_t& provider, const Mtb_Kit_Wx_Config_t& config){
    if (e.lock == nullptr){
        e.lock = xSemaphoreCreateMutex();
        e.published = (Mtb_Kit_Wx_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_Wx_Data_t), MALLOC_CAP_SPIRAM);
        if (e.lock == nullptr || e.published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    e.provider = &provider;
    if (!e.started){
        Mtb_Kit_Wx_Data_t* fresh = (Mtb_Kit_Wx_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_Wx_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!wx_Load_Cache(provider, *fresh)){ memset(fresh, 0, sizeof(*fresh)); mtb_Kit_Wx_Clear(*fresh); }
        fresh->magic = KIT_WX_MAGIC;
        fresh->layout = KIT_WX_LAYOUT;
        fresh->size = sizeof(Mtb_Kit_Wx_Data_t);
        wx_Lock(e);
        memcpy(e.published, fresh, sizeof(*fresh));
        e.config = config;
        const uint32_t now = (uint32_t)time(nullptr);
        if (!wx_Same_Place(*e.published, config)) mtb_Kit_Wx_Clear(*e.published);
        // A plan learned with another key says nothing about this one.
        const uint32_t keyHash = mtb_Kit_Wx_Key_Hash(config.apiKey);
        if (e.published->keyHash != keyHash){ memset(&e.published->plan, 0, sizeof(e.published->plan)); e.published->keyHash = keyHash; }
        // From the cache: each feed when it falls due.
        for (uint8_t f = 0; f < KIT_WX_FEEDS; f++){
            const uint32_t fetched = e.published->fetched[f];
            const uint32_t every = f < provider.feedCount ? provider.every(f, config, *e.published, now) : 0;
            e.due[f] = (fetched && every && now >= fetched && now - fetched < every) ? fetched + every : now + f;
        }
        e.started = true;
        e.version++;
        wx_Unlock(e);
        heap_caps_free(fresh);
    }
    mtb_Kit_Wx_Set_Config(e, config);
}

void mtb_Kit_Wx_Set_Config(Mtb_Kit_Wx_t& e, const Mtb_Kit_Wx_Config_t& config){
    if (!e.started) return;
    wx_Lock(e);
    const uint32_t now = (uint32_t)time(nullptr);
    const bool newPlace = fabs(config.latitude - e.config.latitude) > 1e-6 || fabs(config.longitude - e.config.longitude) > 1e-6;
    const bool newUnits = config.imperial != e.config.imperial;
    const bool newKey = strcmp(config.apiKey, e.config.apiKey) != 0;
    const bool newOptions = config.options != e.config.options;
    e.config = config;
    if (newPlace || newUnits || newKey || newOptions){
        e.generation++;
        for (uint8_t f = 0; f < KIT_WX_FEEDS; f++) e.due[f] = now + f;
        e.published->lastError = KIT_WX_OK;
        e.published->errorText[0] = '\0';
        memset(e.published->feedResult, 0, sizeof(e.published->feedResult));
        e.fails = 0;
    }
    if (newKey){ memset(&e.published->plan, 0, sizeof(e.published->plan)); e.published->keyHash = mtb_Kit_Wx_Key_Hash(config.apiKey); }
    if (newPlace || newUnits) mtb_Kit_Wx_Clear(*e.published);
    e.version++;
    wx_Unlock(e);
}

void mtb_Kit_Wx_Poll(Mtb_Kit_Wx_t& e, bool online, uint32_t now){
    if (!e.started || now == 0) return;
    Kit_Wx_Job_t job = {&e, 0, false};
    wx_Lock(e);
    wx_Roll(e, now);
    if (!e.busy && online){
        job.search = e.searchQuery[0] != '\0';
        if (wx_Has_Key(e)){
            for (uint8_t f = 0; f < e.provider->feedCount; f++){
                if ((int32_t)(now - e.due[f]) >= 0){ job.mask |= 1u << f; e.due[f] = now + 120; }
            }
        }
        if (job.mask || job.search) e.busy = true;
    }
    wx_Unlock(e);
    if (!(job.mask || job.search)) return;
    Kit_Wx_Job_t* parameter = (Kit_Wx_Job_t*)heap_caps_malloc(sizeof(job), MALLOC_CAP_SPIRAM);
    if (parameter) *parameter = job;
    if (parameter == nullptr || xTaskCreatePinnedToCore(wx_Worker, "kitWx", KIT_WX_STACK, parameter, 1, NULL, tskNO_AFFINITY) != pdPASS){
        heap_caps_free(parameter);
        wx_Lock(e);
        e.busy = false;
        wx_Unlock(e);
    }
}

bool mtb_Kit_Wx_Copy(Mtb_Kit_Wx_t& e, Mtb_Kit_Wx_Data_t& out, uint32_t& version){
    if (!e.started) return false;
    wx_Lock(e);
    const bool changed = version != e.version;
    if (changed){ memcpy(&out, e.published, sizeof(out)); version = e.version; }
    wx_Unlock(e);
    return changed;
}

void mtb_Kit_Wx_Get_Status(Mtb_Kit_Wx_t& e, Mtb_Kit_Wx_Status_t& status, char* errorText, size_t errorSize){
    memset(&status, 0, sizeof(status));
    if (errorSize) errorText[0] = '\0';
    if (!e.started) return;
    wx_Lock(e);
    const uint8_t error = e.published->lastError;
    status.started = true;
    status.busy = e.busy;
    status.keyMissing = !wx_Has_Key(e);
    status.keyRejected = error == KIT_WX_KEY;
    status.quotaUsed = error == KIT_WX_QUOTA;
    status.rateLimited = error == KIT_WX_RATE;
    status.failing = error == KIT_WX_NET || error == KIT_WX_PARSE || error == KIT_WX_SERVER;
    status.lastFetch = e.lastFetch;
    status.usedToday = e.published->usedToday;
    status.usedCalls = e.published->usedCalls;
    status.plan = e.published->plan;
    if (errorSize) snprintf(errorText, errorSize, "%s", e.published->errorText);
    wx_Unlock(e);
}

bool mtb_Kit_Wx_Refresh(Mtb_Kit_Wx_t& e, uint32_t now){
    if (!e.started || now == 0) return false;
    wx_Lock(e);
    const bool accepted = (e.lastManual == 0 || now - e.lastManual >= KIT_WX_MANUAL_GAP) && wx_Has_Key(e);
    if (accepted){
        e.lastManual = now;
        // Feeds not in the plan stay as they are.
        for (uint8_t f = 0; f < e.provider->feedCount; f++) if (e.published->feedResult[f] != KIT_WX_PLAN) e.due[f] = now;
    }
    wx_Unlock(e);
    return accepted;
}

bool mtb_Kit_Wx_Search(Mtb_Kit_Wx_t& e, const char* query, Mtb_Kit_Wx_Search_Fn fn){
    if (!e.started || query == nullptr) return false;
    const size_t n = strlen(query);
    if (n < 2 || n >= sizeof(e.searchQuery)) return false;
    wx_Lock(e);
    const bool accepted = e.searchQuery[0] == '\0';
    if (accepted){ memcpy(e.searchQuery, query, n + 1); e.searchFn = fn; }
    wx_Unlock(e);
    return accepted;
}
