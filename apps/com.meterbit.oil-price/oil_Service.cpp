#include "oil_Service.h"
#include "oil_Config.h"
#include "mtb_engine.h"
#include "mtb_github.h"
#include <FS.h>
#include <LittleFS.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mtb_kit_keys.h"

static const char TAG[] = "OIL_SERVICE";

struct Oil_Cache_Header_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t reserved;
    uint32_t size;
    uint32_t crc;
};

struct Oil_Feed_State_t {
    uint32_t due;           // Unix time of the next attempt.
    uint8_t fails;          // Failures in a row.
    Oil_Result_t last;
};

// Everything below is read and written under serviceLock, except where noted.
static SemaphoreHandle_t serviceLock = NULL;
static Oil_Provider_t* provider = nullptr;
static Oil_Data_t* published = nullptr;     // PSRAM. What the screen copies.
static uint32_t publishedVersion = 1;
static Oil_Settings_t current;
static Oil_Feed_State_t feeds[OIL_FEED_COUNT];
static bool scheduled = false;              // feeds[].due has been worked out from the cache.
static bool workerBusy = false;
static bool lowMemory = false;
static uint32_t lastManualRefresh = 0;
static uint32_t keyGeneration = 0;          // Counts key changes, so a download that used an old key can tell.
static TaskHandle_t worker_H = NULL;

static void oil_Lock(void){ xSemaphoreTake(serviceLock, portMAX_DELAY); }
static void oil_Unlock(void){ xSemaphoreGive(serviceLock); }

//##############################################################################################################
void oil_Settings_Default(Oil_Settings_t& settings){
    memset(&settings, 0, sizeof(settings));
    settings.magic = OIL_SETTINGS_MAGIC;
    settings.priceRefreshMin = OIL_PRICE_REFRESH_MIN_DEFAULT;
    settings.newsRefreshMin = OIL_NEWS_REFRESH_MIN_DEFAULT;
    settings.pageSeconds = OIL_PAGE_SEC_DEFAULT;
    settings.autoCycle = 1;
    snprintf(settings.selected, sizeof(settings.selected), "%s", OIL_DEFAULT_SERIES);
    settings.apiKey[0] = '\0';      // The user's own EIA key, sent from the phone.
}

static uint16_t oil_Clamp(uint16_t value, uint16_t low, uint16_t high){ return value < low ? low : value > high ? high : value; }

void oil_Settings_Clamp(Oil_Settings_t& settings){
    settings.priceRefreshMin = oil_Clamp(settings.priceRefreshMin, OIL_PRICE_REFRESH_MIN_LOWEST, OIL_PRICE_REFRESH_MIN_HIGHEST);
    settings.newsRefreshMin = oil_Clamp(settings.newsRefreshMin, OIL_NEWS_REFRESH_MIN_LOWEST, OIL_NEWS_REFRESH_MIN_HIGHEST);
    settings.pageSeconds = (uint8_t)oil_Clamp(settings.pageSeconds, OIL_PAGE_SEC_LOWEST, OIL_PAGE_SEC_HIGHEST);
    settings.selected[sizeof(settings.selected) - 1] = '\0';
    settings.apiKey[sizeof(settings.apiKey) - 1] = '\0';
    mtb_Kit_Clear_Retired_Key(settings.apiKey, sizeof(settings.apiKey));
}

static uint32_t oil_Interval(Oil_Feed_t feed){
    switch (feed){
        case OIL_FEED_PRICES: return current.priceRefreshMin * 60UL;
        case OIL_FEED_NEWS:   return current.newsRefreshMin * 60UL;
        default:              return OIL_SUPPLY_REFRESH_SEC;
    }
}

static uint32_t oil_Jitter(void){ return esp_random() % (OIL_JITTER_SEC + 1); }

// When the feed was last fetched, from the data itself, so a reboot does not bring every device back at once.
static uint32_t oil_Last_Fetched(Oil_Feed_t feed){
    if (feed == OIL_FEED_NEWS) return published->newsFetched;
    if (feed == OIL_FEED_SUPPLY) return published->supplyFetched;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < published->seriesCount; i++){
        if (published->series[i].kind == OIL_KIND_PRICE && published->series[i].fetched < oldest) oldest = published->series[i].fetched;
    }
    return oldest == UINT32_MAX ? 0 : oldest;
}

static void oil_Schedule_From_Data(uint32_t now){
    for (int f = 0; f < OIL_FEED_COUNT; f++){
        const uint32_t last = oil_Last_Fetched((Oil_Feed_t)f);
        // Never fetched, or the clock says the cache is from the future: fetch now.
        feeds[f].due = (last == 0 || last > now) ? now : last + oil_Interval((Oil_Feed_t)f);
        feeds[f].fails = 0;
        feeds[f].last = OIL_OK;
    }
    scheduled = true;
}

//##############################################################################################################
static void oil_Save_Cache(const Oil_Data_t& data){
    static const char tempPath[] = OIL_CACHE_FILE ".tmp";
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Oil_Cache_Header_t header = {OIL_DATA_MAGIC, OIL_DATA_LAYOUT, 0, sizeof(Oil_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Oil_Data_t))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header)
                       && file.write((const uint8_t*)&data, sizeof(Oil_Data_t)) == sizeof(Oil_Data_t);
    file.close();
    // Written to a new file and then renamed, so a power cut leaves the old cache whole.
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, OIL_CACHE_FILE)){
        LittleFS.remove(OIL_CACHE_FILE);
        if (!LittleFS.rename(tempPath, OIL_CACHE_FILE)) ESP_LOGW(TAG, "Could not save the cache");
    }
}

// Takes the history of each series the provider still offers from the cache. Returns false if there is no usable cache.
static bool oil_Load_Cache(Oil_Data_t& data){
    if (!LittleFS.exists(OIL_CACHE_FILE)) return false;
    File file = LittleFS.open(OIL_CACHE_FILE, "r");
    if (!file) return false;
    Oil_Cache_Header_t header;
    Oil_Data_t* saved = (Oil_Data_t*)heap_caps_malloc(sizeof(Oil_Data_t), MALLOC_CAP_SPIRAM);
    bool usable = saved != nullptr
               && file.size() == sizeof(header) + sizeof(Oil_Data_t)
               && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
               && header.magic == OIL_DATA_MAGIC && header.layout == OIL_DATA_LAYOUT && header.size == sizeof(Oil_Data_t)
               && file.read((uint8_t*)saved, sizeof(Oil_Data_t)) == sizeof(Oil_Data_t)
               && esp_rom_crc32_le(0, (const uint8_t*)saved, sizeof(Oil_Data_t)) == header.crc
               && strcmp(saved->provider, data.provider) == 0
               && saved->seriesCount <= OIL_MAX_SERIES && saved->headlineCount <= OIL_MAX_HEADLINES;
    file.close();
    if (usable){
        for (int i = 0; i < data.seriesCount; i++){
            const int from = oil_Find_Series(*saved, data.series[i].code);
            if (from < 0 || saved->series[from].count > OIL_HISTORY_POINTS) continue;
            Oil_Series_t& series = data.series[i];
            const Oil_Series_t& old = saved->series[from];
            if (series.sourceId[0] == '\0'){        // Found by the provider at run time: keep what it found.
                memcpy(series.sourceId, old.sourceId, sizeof(series.sourceId));
                series.unavailable = old.unavailable;
            } else if (strcmp(series.sourceId, old.sourceId) != 0) continue;    // The series behind the code changed.
            series.count = old.count;
            series.fetched = old.fetched;
            memcpy(series.points, old.points, sizeof(series.points));
        }
        data.headlineCount = saved->headlineCount;
        memcpy(data.headlines, saved->headlines, sizeof(data.headlines));
        data.newsFetched = saved->newsFetched;
        data.supplyFetched = saved->supplyFetched;
    } else ESP_LOGI(TAG, "No usable cache");
    heap_caps_free(saved);
    return usable;
}

//##############################################################################################################
// Runs one download round and ends. Only one exists at a time; the app task never waits for it.
static void oil_Worker_Task(void* parameter){
    const uint8_t dueMask = (uint8_t)(uintptr_t)parameter;
    Oil_Data_t* work = (Oil_Data_t*)heap_caps_malloc(sizeof(Oil_Data_t), MALLOC_CAP_SPIRAM);
    char key[OIL_KEY_LEN];

    for (int f = 0; f < OIL_FEED_COUNT && work != nullptr; f++){
        if (!(dueMask & (1 << f))) continue;
        oil_Lock();
        memcpy(work, published, sizeof(Oil_Data_t));
        memcpy(key, current.apiKey, sizeof(key));
        const uint32_t usedKeyGeneration = keyGeneration;
        oil_Unlock();

        const uint32_t now = (uint32_t)time(nullptr);
        const Oil_Fetch_Result_t outcome = provider->fetch((Oil_Feed_t)f, *work, key, now);

        oil_Lock();
        Oil_Feed_State_t& state = feeds[f];
        uint32_t nextInSec = 0;
        // The key changed while this ran: its failure says nothing about the new key, which
        // oil_Service_Set_Settings() has already scheduled.
        const bool staleKey = usedKeyGeneration != keyGeneration && provider->needs_Key((Oil_Feed_t)f);
        if (outcome.result != OIL_OK && staleKey){
            oil_Unlock();
            continue;
        }
        state.last = outcome.result;
        if (outcome.result == OIL_OK){
            memcpy(published, work, sizeof(Oil_Data_t));
            publishedVersion++;
            state.fails = 0;
            state.due = now + oil_Interval((Oil_Feed_t)f) + oil_Jitter();
        } else {
            if (state.fails < 16) state.fails++;
            uint32_t wait = OIL_BACKOFF_FIRST_SEC << (state.fails - 1 < 7 ? state.fails - 1 : 7);
            if (wait > OIL_BACKOFF_MAX_SEC) wait = OIL_BACKOFF_MAX_SEC;
            if (outcome.result == OIL_ERR_AUTH) wait = OIL_BACKOFF_MAX_SEC;     // Until the key changes.
            if (outcome.retryAfterSec > wait) wait = outcome.retryAfterSec < OIL_RETRY_AFTER_MAX_SEC ? outcome.retryAfterSec : OIL_RETRY_AFTER_MAX_SEC;
            state.due = now + wait + oil_Jitter() / 4;
            nextInSec = state.due - now;
        }
        oil_Unlock();

        if (outcome.result == OIL_OK) oil_Save_Cache(*work);    // work is still a copy of what was published.
        else ESP_LOGW(TAG, "Feed %d failed (%d, HTTP %d); next try in %u s", f, (int)outcome.result, outcome.httpCode, (unsigned)nextInSec);
    }

    heap_caps_free(work);
    oil_Lock();
    workerBusy = false;
    worker_H = NULL;
    oil_Unlock();
    vTaskDelete(NULL);
}

//##############################################################################################################
void oil_Service_Begin(Oil_Provider_t& dataProvider, const Oil_Settings_t& settings){
    if (serviceLock == NULL){
        serviceLock = xSemaphoreCreateMutex();
        published = (Oil_Data_t*)heap_caps_calloc(1, sizeof(Oil_Data_t), MALLOC_CAP_SPIRAM);
        if (serviceLock == NULL || published == nullptr){
            ESP_LOGE(TAG, "Out of memory");
            return;
        }
    }
    oil_Lock();
    const bool changedProvider = provider != &dataProvider && !workerBusy;
    oil_Unlock();
    if (changedProvider){
        Oil_Data_t* fresh = (Oil_Data_t*)heap_caps_calloc(1, sizeof(Oil_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh != nullptr){
            fresh->magic = OIL_DATA_MAGIC;
            fresh->layout = OIL_DATA_LAYOUT;
            fresh->size = sizeof(Oil_Data_t);
            dataProvider.describe(*fresh);
            oil_Load_Cache(*fresh);
            oil_Lock();
            memcpy(published, fresh, sizeof(Oil_Data_t));
            publishedVersion++;
            provider = &dataProvider;
            scheduled = false;
            oil_Unlock();
            heap_caps_free(fresh);
        }
    }
    oil_Service_Set_Settings(settings);
}

void oil_Service_Set_Settings(const Oil_Settings_t& settings){
    if (serviceLock == NULL) return;
    oil_Lock();
    const bool newKey = strcmp(current.apiKey, settings.apiKey) != 0;
    if (newKey) keyGeneration++;
    const bool newIntervals = current.priceRefreshMin != settings.priceRefreshMin || current.newsRefreshMin != settings.newsRefreshMin;
    current = settings;
    oil_Settings_Clamp(current);
    const uint32_t now = (uint32_t)time(nullptr);
    if (scheduled && provider != nullptr){
        for (int f = 0; f < OIL_FEED_COUNT; f++){
            if (newKey && provider->needs_Key((Oil_Feed_t)f)){ feeds[f].due = now; feeds[f].fails = 0; feeds[f].last = OIL_OK; }
            else if (newIntervals && feeds[f].fails == 0){
                const uint32_t last = oil_Last_Fetched((Oil_Feed_t)f);
                if (last != 0) feeds[f].due = last + oil_Interval((Oil_Feed_t)f);
            }
        }
    }
    oil_Unlock();
}

void oil_Service_Poll(bool online, uint32_t now){
    if (serviceLock == NULL || provider == nullptr || now == 0) return;
    oil_Lock();
    if (!scheduled) oil_Schedule_From_Data(now);
    uint8_t dueMask = 0;
    if (!workerBusy && online){
        for (int f = 0; f < OIL_FEED_COUNT; f++){
            if (provider->needs_Key((Oil_Feed_t)f) && current.apiKey[0] == '\0'){ feeds[f].last = OIL_ERR_NO_KEY; continue; }
            if ((int32_t)(now - feeds[f].due) >= 0) dueMask |= (uint8_t)(1 << f);
        }
    }
    if (dueMask != 0){
        lowMemory = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < OIL_WORKER_MIN_INTERNAL_HEAP;
        if (lowMemory){
            for (int f = 0; f < OIL_FEED_COUNT; f++) if (dueMask & (1 << f)) feeds[f].due = now + 60;
            dueMask = 0;
        } else workerBusy = true;
    }
    oil_Unlock();
    if (dueMask == 0) return;

    if (xTaskCreatePinnedToCore(oil_Worker_Task, "oilFetch", OIL_WORKER_STACK, (void*)(uintptr_t)dueMask, 1, &worker_H, tskNO_AFFINITY) != pdPASS){
        oil_Lock();
        workerBusy = false;
        worker_H = NULL;
        for (int f = 0; f < OIL_FEED_COUNT; f++) if (dueMask & (1 << f)) feeds[f].due = now + 60;
        oil_Unlock();
        ESP_LOGW(TAG, "Could not start the download task");
    }
}

bool oil_Service_Has_Series(const char* code){
    if (serviceLock == NULL || published == nullptr) return false;
    oil_Lock();
    const bool found = oil_Find_Series(*published, code) >= 0;
    oil_Unlock();
    return found;
}

bool oil_Service_Copy(Oil_Data_t& out, uint32_t& version){
    if (serviceLock == NULL || published == nullptr) return false;
    oil_Lock();
    const bool changed = version != publishedVersion;
    if (changed){
        memcpy(&out, published, sizeof(Oil_Data_t));
        version = publishedVersion;
    }
    oil_Unlock();
    return changed;
}

void oil_Service_Get_Status(Oil_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (serviceLock == NULL) return;
    oil_Lock();
    status.busy = workerBusy;
    status.lowMemory = lowMemory;
    for (int f = 0; f < OIL_FEED_COUNT; f++){
        switch (feeds[f].last){
            case OIL_OK:              break;
            case OIL_ERR_NO_KEY:      status.keyMissing = true; break;
            case OIL_ERR_AUTH:        status.keyRejected = true; break;
            case OIL_ERR_RATE_LIMIT:  status.rateLimited = true; break;
            default:                  status.failing = true; break;
        }
    }
    oil_Unlock();
}

bool oil_Service_Refresh_Now(uint32_t now){
    if (serviceLock == NULL || now == 0) return false;
    oil_Lock();
    bool accepted = lastManualRefresh == 0 || now - lastManualRefresh >= OIL_MANUAL_REFRESH_GAP_SEC;
    if (accepted){
        lastManualRefresh = now;
        for (int f = 0; f < OIL_FEED_COUNT; f++){
            // A source that asked us to wait, or rejected the key, is not asked again early.
            if (feeds[f].last == OIL_ERR_RATE_LIMIT || feeds[f].last == OIL_ERR_AUTH) continue;
            feeds[f].due = now;
        }
    }
    oil_Unlock();
    return accepted;
}
