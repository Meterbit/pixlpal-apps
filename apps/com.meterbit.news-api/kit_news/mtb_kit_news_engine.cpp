#include "mtb_kit_news.h"
#include "mtb_kit_http.h"
#include "mtb_github.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char TAG[] = "MTB_KIT_NEWS";

#define KIT_NEWS_MAGIC          0x4E575331UL    // "NWS1"
#define KIT_NEWS_LAYOUT         1
#define KIT_NEWS_WORKER_STACK   10240
#define KIT_NEWS_PER_RUN        3               // Feeds read in one go, a second apart.
#define KIT_NEWS_MANUAL_GAP     120
#define KIT_NEWS_SAVE_GAP       600

struct Kit_News_Cache_Header_t { uint32_t magic; uint16_t layout; uint16_t reserved; uint32_t size; uint32_t crc; };

static void kit_Lock(Mtb_Kit_News_t& e){ xSemaphoreTake((SemaphoreHandle_t)e.lock, portMAX_DELAY); }
static void kit_Unlock(Mtb_Kit_News_t& e){ xSemaphoreGive((SemaphoreHandle_t)e.lock); }

static void kit_Save_Cache(const Mtb_Kit_News_Provider_t& p, const Mtb_Kit_News_Data_t& data){
    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", p.cacheFile);
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Kit_News_Cache_Header_t header = {KIT_NEWS_MAGIC, KIT_NEWS_LAYOUT, 0, sizeof(Mtb_Kit_News_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header)
                       && file.write((const uint8_t*)&data, sizeof(data)) == sizeof(data);
    file.close();
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, p.cacheFile)){ LittleFS.remove(p.cacheFile); LittleFS.rename(tempPath, p.cacheFile); }
}

static bool kit_Load_Cache(const Mtb_Kit_News_Provider_t& p, Mtb_Kit_News_Data_t& data){
    if (!LittleFS.exists(p.cacheFile)) return false;
    File file = LittleFS.open(p.cacheFile, "r");
    if (!file) return false;
    Kit_News_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(data)
                     && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == KIT_NEWS_MAGIC && header.layout == KIT_NEWS_LAYOUT && header.size == sizeof(data)
                     && file.read((uint8_t*)&data, sizeof(data)) == sizeof(data)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(data)) == header.crc
                     && data.storyCount <= KIT_NEWS_STORIES;
    file.close();
    return usable;
}

static uint32_t kit_Daily_Limit(const Mtb_Kit_News_t& e){
    return e.published->plan.known && e.published->plan.dailyLimit ? e.published->plan.dailyLimit : e.provider->defaultDailyLimit;
}

// A new UTC day: the count starts again.
static void kit_Roll_Day(Mtb_Kit_News_t& e, uint32_t now){
    const uint32_t day = now / 86400;
    if (e.published->usedDay != day){
        e.published->usedDay = day;
        e.published->usedToday = 0;
        e.published->quotaUsed = 0;
        e.version++;
    }
}

struct Kit_Worker_Job_t {
    Mtb_Kit_News_t* engine;
    uint8_t feeds[KIT_NEWS_PER_RUN + 1];
    uint8_t count;
};

static void kit_News_Worker(void* parameter){
    Kit_Worker_Job_t job = *(Kit_Worker_Job_t*)parameter;
    heap_caps_free(parameter);
    Mtb_Kit_News_t& e = *job.engine;
    const Mtb_Kit_News_Provider_t& p = *e.provider;
    Mtb_Kit_News_Config_t* config = (Mtb_Kit_News_Config_t*)heap_caps_malloc(sizeof(Mtb_Kit_News_Config_t), MALLOC_CAP_SPIRAM);
    Mtb_Kit_Story_t* stories = (Mtb_Kit_Story_t*)heap_caps_malloc(sizeof(Mtb_Kit_Story_t) * KIT_NEWS_PER_FEED, MALLOC_CAP_SPIRAM);
    bool anyOk = false;
    for (uint8_t j = 0; j < job.count && config && stories; j++){
        const uint8_t feed = job.feeds[j];
        kit_Lock(e);
        *config = e.config;
        Mtb_Kit_News_Plan_t plan = e.published->plan;
        const uint32_t generation = e.generation;
        kit_Unlock(e);
        uint32_t now = (uint32_t)time(nullptr);
        char url[512];
        const char* headers[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        if (!p.request(*config, feed, plan, now, url, sizeof(url), headers)) continue;
        if (j > 0) vTaskDelay(pdMS_TO_TICKS(1100));
        Mtb_Kit_Http_t http;
        char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, p.maxBody, http, p.userAgent);
        memset(url, 0, sizeof(url));            // It may hold the key.
        now = (uint32_t)time(nullptr);
        uint8_t result = KIT_NEWS_NET, count = 0;
        if (p.parseText){
            result = p.parseText(feed, http.code, body, body ? http.length : 0, http.truncated, *config, now, stories, KIT_NEWS_PER_FEED, count, plan);
            heap_caps_free(body);
        } else if (body){
            JsonDocument doc(mtb_Kit_Json_Allocator());
            const DeserializationError error = deserializeJson(doc, body, http.length);
            heap_caps_free(body);
            result = p.parse(feed, http.code, error ? JsonVariantConst() : doc.as<JsonVariantConst>(), *config, now, stories, KIT_NEWS_PER_FEED, count, plan);
        } else if (http.code > 0) result = p.parse(feed, http.code, JsonVariantConst(), *config, now, stories, KIT_NEWS_PER_FEED, count, plan);
        ESP_LOGI(TAG, "%s %s %u: HTTP %d, result %u, %u stories", p.label, feed == KIT_NEWS_PROBE ? "probe" : "feed", (unsigned)feed, http.code, (unsigned)result, (unsigned)count);

        kit_Lock(e);
        kit_Roll_Day(e, now);
        if (http.code > 0 && e.published->usedToday < 65535) e.published->usedToday++;
        e.published->plan = plan;
        e.lastResult = result;
        e.lastFetch = now;
        const bool same = generation == e.generation;
        const uint32_t interval = mtb_Kit_News_Interval(kit_Daily_Limit(e), e.published->usedToday, e.config.feedCount, now, e.config.floorMinutes * 60UL, p.reserve);
        e.interval = interval;
        const uint32_t midnight = (now / 86400 + 1) * 86400 + 90;
        uint32_t next = now;
        switch (result){
            case KIT_NEWS_OK:
                e.fails = 0;
                if (feed == KIT_NEWS_PROBE){ e.probeDue = now + p.probeEverySec; break; }
                if (same && feed < KIT_NEWS_FEEDS){
                    mtb_Kit_News_Merge(*e.published, feed, stories, count);
                    e.published->feedFetched[feed] = now;
                    anyOk = true;
                }
                next = interval ? now + interval : midnight;
                break;
            case KIT_NEWS_QUOTA:
                e.published->quotaUsed = 1;
                for (uint8_t f = 0; f < KIT_NEWS_FEEDS; f++) e.due[f] = midnight;
                e.probeDue = midnight;
                next = midnight;
                break;
            case KIT_NEWS_RATE: next = now + 65; break;
            case KIT_NEWS_KEY: next = now + 6UL * 3600; break;
            case KIT_NEWS_PLAN: case KIT_NEWS_BAD_REQUEST: next = now + 12UL * 3600; break;
            default: {
                if (e.fails < 8) e.fails++;
                uint32_t wait = 120UL << (e.fails - 1);
                if (wait > 3UL * 3600) wait = 3UL * 3600;
                next = now + wait;
                break;
            }
        }
        if (feed == KIT_NEWS_PROBE){ if (result != KIT_NEWS_OK) e.probeDue = next; }
        else if (feed < KIT_NEWS_FEEDS){
            e.due[feed] = next;
            e.published->feedResult[feed] = result;
        }
        e.version++;
        kit_Unlock(e);
        if (result == KIT_NEWS_QUOTA || result == KIT_NEWS_KEY) break;
    }
    if (anyOk){
        const uint32_t now = (uint32_t)time(nullptr);
        if (e.lastSave == 0 || now - e.lastSave >= KIT_NEWS_SAVE_GAP){
            Mtb_Kit_News_Data_t* copy = (Mtb_Kit_News_Data_t*)heap_caps_malloc(sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
            if (copy){
                kit_Lock(e);
                memcpy(copy, e.published, sizeof(*copy));
                kit_Unlock(e);
                kit_Save_Cache(p, *copy);
                heap_caps_free(copy);
                e.lastSave = now;
            }
        }
    }
    if (config){ memset(config, 0, sizeof(*config)); heap_caps_free(config); }
    heap_caps_free(stories);
    kit_Lock(e);
    e.busy = false;
    kit_Unlock(e);
    vTaskDelete(NULL);
}

//##############################################################################################################
void mtb_Kit_News_Begin(Mtb_Kit_News_t& e, const Mtb_Kit_News_Provider_t& provider, const Mtb_Kit_News_Config_t& config){
    if (e.lock == nullptr){
        e.lock = xSemaphoreCreateMutex();
        e.published = (Mtb_Kit_News_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
        if (e.lock == nullptr || e.published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    e.provider = &provider;
    if (!e.started){
        Mtb_Kit_News_Data_t* fresh = (Mtb_Kit_News_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_News_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!kit_Load_Cache(provider, *fresh)) memset(fresh, 0, sizeof(*fresh));
        fresh->magic = KIT_NEWS_MAGIC;
        fresh->layout = KIT_NEWS_LAYOUT;
        fresh->size = sizeof(Mtb_Kit_News_Data_t);
        kit_Lock(e);
        memcpy(e.published, fresh, sizeof(*fresh));
        e.config = config;
        const uint32_t now = (uint32_t)time(nullptr);
        if (e.published->configHash != mtb_Kit_News_Config_Hash(config)){
            e.published->storyCount = 0;
            memset(e.published->feedFetched, 0, sizeof(e.published->feedFetched));
            e.published->configHash = mtb_Kit_News_Config_Hash(config);
        }
        // From the cache: each feed when its turn comes, staggered.
        const uint32_t interval = mtb_Kit_News_Interval(kit_Daily_Limit(e), e.published->usedDay == now / 86400 ? e.published->usedToday : 0,
                                                        config.feedCount, now, config.floorMinutes * 60UL, provider.reserve);
        for (uint8_t f = 0; f < KIT_NEWS_FEEDS; f++){
            const uint32_t fetched = e.published->feedFetched[f];
            e.due[f] = (fetched && interval && now - fetched < interval) ? fetched + interval : now + f * 2;
        }
        e.probeDue = 0;
        e.started = true;
        e.version++;
        kit_Unlock(e);
        heap_caps_free(fresh);
    }
    mtb_Kit_News_Set_Config(e, config);
}

void mtb_Kit_News_Set_Config(Mtb_Kit_News_t& e, const Mtb_Kit_News_Config_t& config){
    if (!e.started) return;
    kit_Lock(e);
    const bool newKey = strcmp(config.apiKey, e.config.apiKey) != 0;
    const uint32_t hash = mtb_Kit_News_Config_Hash(config);
    const bool newFeeds = hash != e.published->configHash;
    const uint32_t now = (uint32_t)time(nullptr);
    e.config = config;
    if (newKey || newFeeds){
        e.generation++;
        for (uint8_t f = 0; f < KIT_NEWS_FEEDS; f++) e.due[f] = now + f * 2;
    }
    if (newKey){
        memset(&e.published->plan, 0, sizeof(e.published->plan));
        e.published->quotaUsed = 0;
        e.probeDue = 0;
        e.lastResult = KIT_NEWS_OK;
        e.fails = 0;
    }
    if (newFeeds){
        e.published->storyCount = 0;
        memset(e.published->feedFetched, 0, sizeof(e.published->feedFetched));
        memset(e.published->feedResult, 0, sizeof(e.published->feedResult));
        e.published->configHash = hash;
    }
    e.version++;
    kit_Unlock(e);
}

void mtb_Kit_News_Poll(Mtb_Kit_News_t& e, bool online, uint32_t now){
    if (!e.started || now == 0) return;
    Kit_Worker_Job_t job = {};
    job.engine = &e;
    kit_Lock(e);
    kit_Roll_Day(e, now);
    e.interval = mtb_Kit_News_Interval(kit_Daily_Limit(e), e.published->usedToday, e.config.feedCount, now, e.config.floorMinutes * 60UL, e.provider->reserve);
    const bool canAsk = online && !e.busy && (e.provider->keyless || e.config.apiKey[0]) && !e.published->quotaUsed && e.interval > 0;
    if (canAsk){
        if (e.provider->probeEverySec && (int32_t)(now - e.probeDue) >= 0) job.feeds[job.count++] = KIT_NEWS_PROBE;
        for (uint8_t f = 0; f < e.config.feedCount && job.count < KIT_NEWS_PER_RUN; f++){
            if ((int32_t)(now - e.due[f]) >= 0){ job.feeds[job.count++] = f; e.due[f] = now + 120; }
        }
        if (job.count){
            if (job.feeds[0] == KIT_NEWS_PROBE) e.probeDue = now + 600;
            e.busy = true;
        }
    }
    kit_Unlock(e);
    if (job.count == 0) return;
    Kit_Worker_Job_t* parameter = (Kit_Worker_Job_t*)heap_caps_malloc(sizeof(job), MALLOC_CAP_SPIRAM);
    if (parameter) *parameter = job;
    if (parameter == nullptr || xTaskCreatePinnedToCore(kit_News_Worker, "kitNews", KIT_NEWS_WORKER_STACK, parameter, 1, NULL, tskNO_AFFINITY) != pdPASS){
        heap_caps_free(parameter);
        kit_Lock(e);
        e.busy = false;
        kit_Unlock(e);
    }
}

bool mtb_Kit_News_Copy(Mtb_Kit_News_t& e, Mtb_Kit_News_Data_t& out, uint32_t& version){
    if (!e.started) return false;
    kit_Lock(e);
    const bool changed = version != e.version;
    if (changed){ memcpy(&out, e.published, sizeof(out)); version = e.version; }
    kit_Unlock(e);
    return changed;
}

void mtb_Kit_News_Get_Status(Mtb_Kit_News_t& e, Mtb_Kit_News_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (!e.started) return;
    kit_Lock(e);
    status.started = true;
    status.busy = e.busy;
    status.keyMissing = !e.provider->keyless && e.config.apiKey[0] == '\0';
    status.keyRejected = e.lastResult == KIT_NEWS_KEY;
    status.quotaUsed = e.published->quotaUsed || (e.interval == 0 && e.config.feedCount > 0);
    status.rateLimited = e.lastResult == KIT_NEWS_RATE;
    status.failing = e.lastResult == KIT_NEWS_NET || e.lastResult == KIT_NEWS_PARSE || e.lastResult == KIT_NEWS_SERVER;
    status.feedInterval = e.interval;
    status.lastFetch = e.lastFetch;
    kit_Unlock(e);
}

void mtb_Kit_News_Get_Plan(Mtb_Kit_News_t& e, Mtb_Kit_News_Plan_t& plan, uint16_t& usedToday){
    memset(&plan, 0, sizeof(plan));
    usedToday = 0;
    if (!e.started) return;
    kit_Lock(e);
    plan = e.published->plan;
    usedToday = e.published->usedToday;
    if (!plan.known) plan.dailyLimit = e.provider->defaultDailyLimit;
    kit_Unlock(e);
}

bool mtb_Kit_News_Refresh(Mtb_Kit_News_t& e, uint32_t now){
    if (!e.started || now == 0) return false;
    kit_Lock(e);
    const bool accepted = (e.lastManual == 0 || now - e.lastManual >= KIT_NEWS_MANUAL_GAP) && !e.published->quotaUsed && (e.provider->keyless || e.config.apiKey[0])
                       && e.published->usedToday < kit_Daily_Limit(e);
    if (accepted){
        e.lastManual = now;
        for (uint8_t f = 0; f < KIT_NEWS_FEEDS; f++) e.due[f] = now;
    }
    kit_Unlock(e);
    return accepted;
}
