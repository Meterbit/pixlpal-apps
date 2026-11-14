#include "cfx_Service.h"
#include "mtb_kit_http.h"
#include "mtb_github.h"
#include <FS.h>
#include <LittleFS.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char TAG[] = "CFX_SERVICE";

#define CFX_USER_AGENT          "Pixlpal-CurrencyExchange/2.0 (ESP32 LED display)"
#define CFX_OPEN_URL            "https://open.er-api.com/v6/latest/%s"
#define CFX_KEY_URL             "https://v6.exchangerate-api.com/v6/%s/%s"
#define CFX_MAX_BODY            (24 * 1024)
#define CFX_CACHE_FILE          "/currency/cache.bin"
#define CFX_WORKER_STACK        8192
#define CFX_QUOTA_REFRESH_SEC   (24UL * 3600)
#define CFX_OPEN_MIN_GAP_SEC    3600        // The keyless feed asks for no more than one request an hour.
#define CFX_KEY_MIN_GAP_SEC     300
#define CFX_AFTER_UPDATE_SEC    180         // Ask this long after the provider's update time.
#define CFX_RATE_WAIT_SEC       (21UL * 60) // The keyless feed's 429 lasts 20 minutes.
#define CFX_HISTORY_RETRY_SEC   (7UL * 86400)
#define CFX_LOW_QUOTA           20          // Below this, no history; just the rates.
#define CFX_MANUAL_GAP_SEC      120

enum Cfx_Feed_t : uint8_t { F_QUOTA = 0, F_LATEST, F_HISTORY, F_COUNT };

struct Cfx_Cache_Header_t { uint32_t magic; uint16_t layout; uint16_t reserved; uint32_t size; uint32_t crc; };

static SemaphoreHandle_t serviceLock = NULL;
static Cfx_Data_t* published = nullptr;     // PSRAM
static uint32_t publishedVersion = 1;
static Cfx_Settings_t current;
static uint32_t settingsGeneration = 0;
static bool started = false, workerBusy = false;
static uint32_t due[F_COUNT];
static uint8_t lastError[F_COUNT];
static uint8_t fails = 0;
static uint32_t historyDoneDay = 0, lastManual = 0;

static void cfx_Lock(void){ xSemaphoreTake(serviceLock, portMAX_DELAY); }
static void cfx_Unlock(void){ xSemaphoreGive(serviceLock); }

//##############################################################################################################
void cfx_Settings_Default(Cfx_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = CFX_SETTINGS_MAGIC;
    snprintf(s.base, sizeof(s.base), "USD");
    static const char* const WATCH[] = {"NGN", "EUR", "GBP", "JPY", "GHS", "ZAR", "CNY", "CAD"};
    for (const char* code : WATCH) snprintf(s.watch[s.watchCount++], 4, "%s", code);
    s.pageSeconds = 10;
    s.autoCycle = 1;
    s.amount = 100;
}

void cfx_Settings_Clamp(Cfx_Settings_t& s){
    s.base[3] = '\0';
    if (!cfx_Is_Code(s.base)) snprintf(s.base, sizeof(s.base), "USD");
    uint8_t kept = 0;
    const uint8_t count = s.watchCount < CFX_MAX_WATCH ? s.watchCount : CFX_MAX_WATCH;
    for (uint8_t i = 0; i < count; i++){
        char code[4];
        memcpy(code, s.watch[i], 4);
        code[3] = '\0';
        bool repeated = strcmp(code, s.base) == 0;
        for (uint8_t k = 0; k < kept; k++) if (strcmp(s.watch[k], code) == 0) repeated = true;
        if (cfx_Is_Code(code) && !repeated) memcpy(s.watch[kept++], code, 4);
    }
    for (uint8_t i = kept; i < CFX_MAX_WATCH; i++) memset(s.watch[i], 0, 4);
    s.watchCount = kept;
    if (s.featured >= kept) s.featured = 0;
    if (s.pageSeconds < 4) s.pageSeconds = 4;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
    s.inverse = s.inverse ? 1 : 0;
    if (!(s.amount > 0) || s.amount > 1e12) s.amount = 100;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
}

//##############################################################################################################
static void cfx_Save_Cache(const Cfx_Data_t& data){
    static const char tempPath[] = CFX_CACHE_FILE ".tmp";
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Cfx_Cache_Header_t header = {CFX_DATA_MAGIC, CFX_DATA_LAYOUT, 0, sizeof(Cfx_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Cfx_Data_t))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header)
                       && file.write((const uint8_t*)&data, sizeof(Cfx_Data_t)) == sizeof(Cfx_Data_t);
    file.close();
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, CFX_CACHE_FILE)){ LittleFS.remove(CFX_CACHE_FILE); LittleFS.rename(tempPath, CFX_CACHE_FILE); }
}

static bool cfx_Load_Cache(Cfx_Data_t& data){
    if (!LittleFS.exists(CFX_CACHE_FILE)) return false;
    File file = LittleFS.open(CFX_CACHE_FILE, "r");
    if (!file) return false;
    Cfx_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(Cfx_Data_t)
                     && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == CFX_DATA_MAGIC && header.layout == CFX_DATA_LAYOUT && header.size == sizeof(Cfx_Data_t)
                     && file.read((uint8_t*)&data, sizeof(Cfx_Data_t)) == sizeof(Cfx_Data_t)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Cfx_Data_t)) == header.crc
                     && data.rateCount <= CFX_MAX_RATES;
    file.close();
    return usable;
}

//##############################################################################################################
// One request: the parsed answer in @p doc. Returns the error, CFX_ERR_NONE, or CFX_ERR_OTHER for no answer.
static uint8_t cfx_Get(const char* url, JsonDocument& doc, int& httpCode, const char* label){
    Mtb_Kit_Http_t http;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, CFX_MAX_BODY, http, CFX_USER_AGENT);
    httpCode = http.code;
    if (body == nullptr){ ESP_LOGW(TAG, "%s: no answer (HTTP %d)", label, http.code); return CFX_ERR_OTHER; }
    const DeserializationError error = deserializeJson(doc, body, http.length);
    heap_caps_free(body);
    if (error){ ESP_LOGW(TAG, "%s: HTTP %d, unreadable: %s", label, http.code, error.c_str()); return CFX_ERR_OTHER; }
    const char* result = doc["result"];
    if (result && strcmp(result, "success") == 0) return CFX_ERR_NONE;
    const uint8_t kind = cfx_Error_Of(doc["error-type"]);
    ESP_LOGW(TAG, "%s: HTTP %d, %s", label, http.code, (const char*)(doc["error-type"] | "error"));
    return kind;
}

static uint32_t cfx_Min_Gap(const Cfx_Settings_t& s){ return s.apiKey[0] ? CFX_KEY_MIN_GAP_SEC : CFX_OPEN_MIN_GAP_SEC; }

// When to ask for rates next: after the provider's next update, never sooner than the gap.
static uint32_t cfx_Next_Latest(const Cfx_Data_t& data, const Cfx_Settings_t& s, uint32_t now){
    uint32_t next = data.nextUpdate ? data.nextUpdate + CFX_AFTER_UPDATE_SEC + esp_random() % 120 : now + 3600;
    if (next < now + cfx_Min_Gap(s)) next = now + cfx_Min_Gap(s);
    if (next > now + 26UL * 3600) next = now + 26UL * 3600;
    return next;
}

static void cfx_Worker_Task(void* parameter){
    const uint8_t mask = (uint8_t)(uintptr_t)parameter;
    Cfx_Data_t* work = (Cfx_Data_t*)heap_caps_malloc(sizeof(Cfx_Data_t), MALLOC_CAP_SPIRAM);
    Cfx_Settings_t* s = (Cfx_Settings_t*)heap_caps_malloc(sizeof(Cfx_Settings_t), MALLOC_CAP_SPIRAM);
    bool save = false, requested = false;
    if (work && s){
        cfx_Lock();
        memcpy(work, published, sizeof(Cfx_Data_t));
        *s = current;
        const uint32_t generation = settingsGeneration;
        cfx_Unlock();
        JsonDocument doc(mtb_Kit_Json_Allocator());
        char url[160];
        int code = 0;
        uint32_t now = (uint32_t)time(nullptr);

        if ((mask & (1u << F_QUOTA)) && s->apiKey[0]){
            snprintf(url, sizeof(url), CFX_KEY_URL, s->apiKey, "quota");
            const uint8_t error = cfx_Get(url, doc, code, "Quota");
            requested = true;
            Cfx_Quota_t quota = work->quota;
            cfx_Lock();
            lastError[F_QUOTA] = error;
            if (error == CFX_ERR_NONE && cfx_Parse_Quota(doc, quota) == CFX_ERR_NONE){
                quota.seen = now;
                published->quota = quota;
                published->plan = cfx_Plan_Of_Quota(quota.planQuota);
                work->quota = quota;
                work->plan = published->plan;
                // A new plan may allow history: ask again.
                if (published->historyLocked && published->plan >= CFX_PLAN_PRO){ published->historyLocked = 0; work->historyLocked = 0; }
                due[F_QUOTA] = now + CFX_QUOTA_REFRESH_SEC;
            } else if (error == CFX_ERR_QUOTA){
                published->quota.exhausted = 1;
                published->quota.remaining = 0;
                due[F_QUOTA] = now + CFX_QUOTA_REFRESH_SEC;
            } else due[F_QUOTA] = now + (error == CFX_ERR_KEY || error == CFX_ERR_INACTIVE ? 6UL * 3600 : 1800);
            publishedVersion++;
            cfx_Unlock();
        }

        if (mask & (1u << F_LATEST)){
            if (s->apiKey[0]) { char path[16]; snprintf(path, sizeof(path), "latest/%s", s->base); snprintf(url, sizeof(url), CFX_KEY_URL, s->apiKey, path); }
            else snprintf(url, sizeof(url), CFX_OPEN_URL, s->base);
            if (requested) vTaskDelay(pdMS_TO_TICKS(1000));
            const uint8_t error = cfx_Get(url, doc, code, "Rates");
            requested = true;
            now = (uint32_t)time(nullptr);
            uint8_t parsed = error;
            if (error == CFX_ERR_NONE) parsed = cfx_Parse_Latest(doc, *work);
            cfx_Lock();
            lastError[F_LATEST] = parsed;
            const bool sameSettings = generation == settingsGeneration;
            if (parsed == CFX_ERR_NONE && sameSettings && strcmp(work->base, s->base) == 0){
                work->fetched = now;
                // Today's sample of each watched code, by the provider's update day.
                const uint32_t day = (work->lastUpdate ? work->lastUpdate : now) / 86400;
                cfx_History_Keep(*work, s->watch, s->watchCount);
                for (uint8_t i = 0; i < s->watchCount; i++) cfx_History_Put(*work, s->watch[i], day, cfx_Rate(*work, s->watch[i]));
                // The plan and quota are the quota request's; history locks are the history requests'.
                const Cfx_Quota_t quota = published->quota;
                const uint8_t plan = published->plan, locked = published->historyLocked;
                const uint32_t retry = published->historyRetryAt;
                memcpy(published, work, sizeof(Cfx_Data_t));
                published->quota = quota;
                published->plan = s->apiKey[0] ? (plan == CFX_PLAN_OPEN ? (uint8_t)CFX_PLAN_UNKNOWN : plan) : (uint8_t)CFX_PLAN_OPEN;
                published->historyLocked = locked;
                published->historyRetryAt = retry;
                if (published->quota.seen && published->quota.remaining > 0) published->quota.remaining--;
                fails = 0;
                due[F_LATEST] = cfx_Next_Latest(*published, *s, now);
                save = true;
            } else if (parsed == CFX_ERR_NONE) due[F_LATEST] = now + 5;      // Settings changed meanwhile.
            else if (parsed == CFX_ERR_QUOTA){ published->quota.exhausted = 1; published->quota.remaining = 0; due[F_LATEST] = now + 6UL * 3600; }
            else if (code == 429){ due[F_LATEST] = now + CFX_RATE_WAIT_SEC; }
            else if (parsed == CFX_ERR_KEY || parsed == CFX_ERR_INACTIVE) due[F_LATEST] = now + 6UL * 3600;
            else {
                if (fails < 8) fails++;
                uint32_t wait = 120UL << (fails - 1);
                if (wait > 3UL * 3600) wait = 3UL * 3600;
                due[F_LATEST] = now + wait;
            }
            publishedVersion++;
            cfx_Unlock();
        }

        if ((mask & (1u << F_HISTORY)) && s->apiKey[0] && work->lastUpdate){
            // A month of daily rates for the sparkline and the 7- and 30-day changes: the days missing, newest first.
            const uint32_t newest = work->lastUpdate / 86400;
            static const uint8_t OFFSETS[] = {1, 7, 30, 2, 3, 4, 5, 6, 14, 21};
            uint8_t asked = 0;
            bool locked = false;
            const Cfx_History_t* ref = s->watchCount ? cfx_History_Of(*work, s->watch[0]) : nullptr;
            for (uint8_t k = 0; k < sizeof(OFFSETS) && asked < 8 && !locked; k++){
                const uint32_t day = newest - OFFSETS[k];
                bool have = false;
                for (int i = 0; ref && i < ref->count; i++) if (ref->day[i] == day) have = true;
                if (have) continue;
                const time_t t = (time_t)day * 86400;
                struct tm tm;
                gmtime_r(&t, &tm);
                char path[40];
                snprintf(path, sizeof(path), "history/%s/%d/%d/%d", s->base, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
                snprintf(url, sizeof(url), CFX_KEY_URL, s->apiKey, path);
                vTaskDelay(pdMS_TO_TICKS(1200));
                const uint8_t error = cfx_Get(url, doc, code, "History");
                asked++;
                float rates[CFX_MAX_WATCH];
                uint32_t got = 0;
                if (error == CFX_ERR_NONE && cfx_Parse_History(doc, s->watch, s->watchCount, rates, got) == CFX_ERR_NONE){
                    for (uint8_t i = 0; i < s->watchCount; i++) cfx_History_Put(*work, s->watch[i], got, rates[i]);
                    ref = cfx_History_Of(*work, s->watch[0]);
                } else if (error == CFX_ERR_PLAN) locked = true;
                else if (error != CFX_ERR_NO_DATA) break;
            }
            now = (uint32_t)time(nullptr);
            cfx_Lock();
            if (generation == settingsGeneration && strcmp(published->base, s->base) == 0){
                for (uint8_t i = 0; i < CFX_MAX_WATCH; i++) published->history[i] = work->history[i];
                if (published->quota.seen) published->quota.remaining = published->quota.remaining > asked ? published->quota.remaining - asked : 0;
                save = true;
            }
            if (locked){ published->historyLocked = 1; published->historyRetryAt = now + CFX_HISTORY_RETRY_SEC; }
            else published->historyLocked = 0;
            historyDoneDay = newest;
            lastError[F_HISTORY] = locked ? (uint8_t)CFX_ERR_PLAN : (uint8_t)CFX_ERR_NONE;
            publishedVersion++;
            cfx_Unlock();
        }
        if (save){
            cfx_Lock();
            memcpy(work, published, sizeof(Cfx_Data_t));
            cfx_Unlock();
            cfx_Save_Cache(*work);
        }
    }
    heap_caps_free(work);
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    cfx_Lock();
    workerBusy = false;
    cfx_Unlock();
    vTaskDelete(NULL);
}

//##############################################################################################################
void cfx_Service_Begin(const Cfx_Settings_t& settings){
    if (serviceLock == NULL){
        serviceLock = xSemaphoreCreateMutex();
        published = (Cfx_Data_t*)heap_caps_calloc(1, sizeof(Cfx_Data_t), MALLOC_CAP_SPIRAM);
        if (serviceLock == NULL || published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    if (!started){
        Cfx_Data_t* fresh = (Cfx_Data_t*)heap_caps_calloc(1, sizeof(Cfx_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!cfx_Load_Cache(*fresh)) memset(fresh, 0, sizeof(Cfx_Data_t));
        fresh->magic = CFX_DATA_MAGIC;
        fresh->layout = CFX_DATA_LAYOUT;
        fresh->size = sizeof(Cfx_Data_t);
        cfx_Lock();
        memcpy(published, fresh, sizeof(Cfx_Data_t));
        current = settings;
        cfx_Settings_Clamp(current);
        const uint32_t now = (uint32_t)time(nullptr);
        // From the cache: the rates again only when the provider has new ones.
        const bool cacheCurrent = published->rateCount && strcmp(published->base, current.base) == 0 && published->nextUpdate > now;
        due[F_LATEST] = cacheCurrent ? cfx_Next_Latest(*published, current, now) : 0;
        due[F_QUOTA] = 0;
        due[F_HISTORY] = 0;
        started = true;
        publishedVersion++;
        cfx_Unlock();
        heap_caps_free(fresh);
    }
    cfx_Service_Set_Settings(settings);
}

void cfx_Service_Set_Settings(const Cfx_Settings_t& settings){
    if (!started) return;
    Cfx_Settings_t next = settings;
    cfx_Settings_Clamp(next);
    cfx_Lock();
    const bool newKey = strcmp(next.apiKey, current.apiKey) != 0;
    const bool newBase = strcmp(next.base, current.base) != 0;
    const bool newWatch = next.watchCount != current.watchCount || memcmp(next.watch, current.watch, sizeof(next.watch)) != 0;
    current = next;
    if (newKey || newBase || newWatch) settingsGeneration++;
    if (newKey){
        memset(&published->quota, 0, sizeof(published->quota));
        published->plan = next.apiKey[0] ? CFX_PLAN_UNKNOWN : CFX_PLAN_OPEN;
        published->historyLocked = 0;
        due[F_QUOTA] = 0;
        due[F_LATEST] = 0;
        for (uint8_t f = 0; f < F_COUNT; f++) lastError[f] = 0;
    }
    if (newBase){
        published->rateCount = 0;
        memset(published->history, 0, sizeof(published->history));
        due[F_LATEST] = 0;
    }
    if (newBase || newWatch) historyDoneDay = 0;
    publishedVersion++;
    cfx_Unlock();
}

void cfx_Service_Poll(bool online, uint32_t now){
    if (!started || now == 0) return;
    uint8_t mask = 0;
    cfx_Lock();
    if (!workerBusy && online){
        const bool keyed = current.apiKey[0] != '\0';
        const bool keyBad = lastError[F_QUOTA] == CFX_ERR_KEY || lastError[F_LATEST] == CFX_ERR_KEY;
        const Cfx_Quota_t& q = published->quota;
        const bool low = keyed && q.seen && q.remaining < CFX_LOW_QUOTA;
        if (keyed && (int32_t)(now - due[F_QUOTA]) >= 0) mask |= 1u << F_QUOTA;
        if ((int32_t)(now - due[F_LATEST]) >= 0 && !(keyBad && due[F_LATEST] > now)) mask |= 1u << F_LATEST;
        // History: once per data day, with quota to spare.
        const uint32_t dataDay = published->lastUpdate / 86400;
        const bool historyAllowed = !published->historyLocked || (int32_t)(now - published->historyRetryAt) >= 0;
        // The docs say history is for paid plans, the pricing page lists it on Free too: a refusal locks it for a week.
        if (keyed && !low && !keyBad && published->rateCount && dataDay && historyDoneDay != dataDay && historyAllowed
            && (int32_t)(now - due[F_HISTORY]) >= 0){
            mask |= 1u << F_HISTORY;
            due[F_HISTORY] = now + 3600;
        }
        if (mask) workerBusy = true;
    }
    cfx_Unlock();
    if (mask == 0) return;
    if (xTaskCreatePinnedToCore(cfx_Worker_Task, "cfxFetch", CFX_WORKER_STACK, (void*)(uintptr_t)mask, 1, NULL, tskNO_AFFINITY) != pdPASS){
        cfx_Lock();
        workerBusy = false;
        for (uint8_t f = 0; f < F_COUNT; f++) if (mask & (1u << f)) due[f] = now + 60;
        cfx_Unlock();
    }
}

bool cfx_Service_Copy(Cfx_Data_t& out, uint32_t& version){
    if (!started) return false;
    cfx_Lock();
    const bool changed = version != publishedVersion;
    if (changed){ memcpy(&out, published, sizeof(Cfx_Data_t)); version = publishedVersion; }
    cfx_Unlock();
    return changed;
}

void cfx_Service_Get_Status(Cfx_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (!started) return;
    cfx_Lock();
    status.started = true;
    status.busy = workerBusy;
    status.keyless = current.apiKey[0] == '\0';
    status.keyRejected = lastError[F_LATEST] == CFX_ERR_KEY || lastError[F_QUOTA] == CFX_ERR_KEY;
    status.inactive = lastError[F_LATEST] == CFX_ERR_INACTIVE || lastError[F_QUOTA] == CFX_ERR_INACTIVE;
    status.quotaUsed = published->quota.exhausted != 0 || lastError[F_LATEST] == CFX_ERR_QUOTA;
    status.failing = lastError[F_LATEST] == CFX_ERR_OTHER;
    status.nextFetch = due[F_LATEST];
    cfx_Unlock();
}

void cfx_Service_Get_Plan(uint8_t& plan, Cfx_Quota_t& quota, uint32_t& lastUpdate, uint32_t& nextUpdate, uint8_t& historyLocked){
    plan = CFX_PLAN_UNKNOWN;
    memset(&quota, 0, sizeof(quota));
    lastUpdate = nextUpdate = 0;
    historyLocked = 0;
    if (!started) return;
    cfx_Lock();
    plan = published->plan;
    quota = published->quota;
    lastUpdate = published->lastUpdate;
    nextUpdate = published->nextUpdate;
    historyLocked = published->historyLocked;
    cfx_Unlock();
}

bool cfx_Service_Refresh_Now(uint32_t now){
    if (!started || now == 0) return false;
    cfx_Lock();
    const bool accepted = (lastManual == 0 || now - lastManual >= CFX_MANUAL_GAP_SEC) && !published->quota.exhausted;
    if (accepted){ lastManual = now; due[F_LATEST] = now; if (current.apiKey[0]) due[F_QUOTA] = now; }
    cfx_Unlock();
    return accepted;
}
