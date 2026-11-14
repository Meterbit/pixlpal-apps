#include "opa_Service.h"
#include "opa_Config.h"
#include "opa_Api.h"
#include "opa_Parse.h"
#include "mtb_engine.h"
#include "mtb_github.h"
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mtb_kit_keys.h"

static const char TAG[] = "OPA_SERVICE";

enum Opa_Feed_t : uint8_t { OPA_FEED_QUOTES = 0, OPA_FEED_HISTORY, OPA_FEED_FUTURES, OPA_FEED_RIGS, OPA_FEED_OPEC, OPA_FEED_COUNT };

struct Opa_Cache_Header_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t reserved;
    uint32_t size;
    uint32_t crc;
};

struct Opa_Feed_State_t {
    uint32_t due;           // Unix time of the next attempt.
    uint32_t lastTry;
    uint8_t fails;          // Failures in a row.
    Opa_Result_t last;
};

// Everything below is read and written under serviceLock.
static SemaphoreHandle_t serviceLock = NULL;
static Opa_Data_t* published = nullptr;     // PSRAM. What the screen copies.
static uint32_t publishedVersion = 1;
static Opa_Settings_t current;
static Opa_Feed_State_t feeds[OPA_FEED_COUNT];
static bool started = false;
static bool scheduled = false;
static bool workerBusy = false;
static bool lowMemory = false;
static uint32_t lastManualRefresh = 0;
static uint32_t lastCacheSave = 0;
static uint32_t keyGeneration = 0;          // Counts key changes, so a download that used an old key can tell.
static uint32_t quoteInterval = 0;
static bool invalidCode[OPA_MAX_WATCH];     // The API said it does not know this code: left out of requests.
static char quotesEtag[80];
static uint32_t watchGeneration = 0;        // Counts watchlist changes, so an ETag is only kept for the list it was for.

static void opa_Lock(void){ xSemaphoreTake(serviceLock, portMAX_DELAY); }
static void opa_Unlock(void){ xSemaphoreGive(serviceLock); }

//##############################################################################################################
static const char* const DEFAULT_WATCH[] = {
    "BRENT_CRUDE_USD", "WTI_USD", "DUBAI_CRUDE_USD", "OPEC_BASKET_USD", "URALS_CRUDE_USD", "WCS_CRUDE_USD",
    "NATURAL_GAS_USD", "GASOLINE_RBOB_USD", "DIESEL_USD", "HEATING_OIL_USD",
};

void opa_Settings_Default(Opa_Settings_t& settings){
    memset(&settings, 0, sizeof(settings));
    settings.magic = OPA_SETTINGS_MAGIC;
    settings.range = OPA_RANGE_MONTH;
    settings.share = OPA_SHARE_DEFAULT;
    settings.floorMin = OPA_FLOOR_MIN_DEFAULT;
    settings.pageSeconds = OPA_PAGE_SEC_DEFAULT;
    settings.autoCycle = 1;
    for (const char* code : DEFAULT_WATCH) snprintf(settings.watch[settings.watchCount++], OPA_CODE_LEN, "%s", code);
    snprintf(settings.featured, sizeof(settings.featured), "%s", DEFAULT_WATCH[0]);
    settings.apiKey[0] = '\0';      // The user's own OilPriceAPI key, sent from the phone.
}

static uint8_t opa_Clamp(uint8_t value, uint8_t low, uint8_t high){ return value < low ? low : value > high ? high : value; }

void opa_Settings_Clamp(Opa_Settings_t& settings){
    if (settings.range >= OPA_RANGE_COUNT) settings.range = OPA_RANGE_MONTH;
    settings.share = opa_Clamp(settings.share, OPA_SHARE_LOWEST, OPA_SHARE_HIGHEST);
    settings.floorMin = opa_Clamp(settings.floorMin, OPA_FLOOR_MIN_LOWEST, OPA_FLOOR_MIN_HIGHEST);
    settings.pageSeconds = opa_Clamp(settings.pageSeconds, OPA_PAGE_SEC_LOWEST, OPA_PAGE_SEC_HIGHEST);
    settings.apiKey[sizeof(settings.apiKey) - 1] = '\0';
    mtb_Kit_Clear_Retired_Key(settings.apiKey, sizeof(settings.apiKey));
    settings.featured[sizeof(settings.featured) - 1] = '\0';
    // Known codes only, each once, in the catalogue's spelling.
    uint8_t kept = 0;
    const uint8_t count = settings.watchCount < OPA_MAX_WATCH ? settings.watchCount : OPA_MAX_WATCH;
    for (uint8_t i = 0; i < count; i++){
        settings.watch[i][OPA_CODE_LEN - 1] = '\0';
        const Opa_Commodity_t* known = opa_Find_Commodity(settings.watch[i]);
        bool repeated = false;
        for (uint8_t k = 0; known && k < kept; k++) if (strcmp(settings.watch[k], known->code) == 0) repeated = true;
        if (known == nullptr || repeated) continue;
        snprintf(settings.watch[kept++], OPA_CODE_LEN, "%s", known->code);
    }
    settings.watchCount = kept;
    if (kept == 0){
        settings.watchCount = 1;
        snprintf(settings.watch[0], OPA_CODE_LEN, "%s", DEFAULT_WATCH[0]);
    }
    // The featured code is always on the watchlist.
    bool featuredListed = false;
    for (uint8_t i = 0; i < settings.watchCount; i++) if (strcasecmp(settings.watch[i], settings.featured) == 0) featuredListed = true;
    if (!featuredListed) snprintf(settings.featured, sizeof(settings.featured), "%s", settings.watch[0]);
}

//##############################################################################################################
static bool opa_Have_Key(void){ return current.apiKey[0] != '\0'; }

static uint32_t opa_Jitter(void){ return esp_random() % (OPA_JITTER_SEC + 1); }

static uint32_t opa_History_Refresh(uint8_t range){
    switch (range){
        case OPA_RANGE_DAY:   return 2UL * 3600;
        case OPA_RANGE_WEEK:  return 6UL * 3600;
        case OPA_RANGE_MONTH: return 12UL * 3600;
        default:              return 24UL * 3600;
    }
}

static Opa_Feature_t opa_Feature_Of(uint8_t feed){
    switch (feed){
        case OPA_FEED_HISTORY: return OPA_F_HISTORY;
        case OPA_FEED_FUTURES: return OPA_F_FUTURES;
        case OPA_FEED_RIGS:    return OPA_F_RIGS;
        default:               return OPA_F_OPEC;
    }
}

// A paid feature is worth asking for: the plan has not refused it, or it refused long ago, or the plan has changed.
static bool opa_Feature_Open(uint8_t feed, uint32_t now){
    const Opa_Access_t& access = published->access[opa_Feature_Of(feed)];
    if (access.state != OPA_ACCESS_LOCKED) return true;
    return (int32_t)(now - access.retryAt) >= 0 || (published->account.seen != 0 && published->account.limit != access.lockedLimit);
}

// Whether an extra feed is in use at all: needs a key; history is also tried on the Free plan, the rest on paid plans.
static bool opa_Feed_Wanted(uint8_t feed, uint32_t now){
    if (feed == OPA_FEED_QUOTES) return true;
    if (!opa_Have_Key() || !opa_Feature_Open(feed, now)) return false;
    return feed == OPA_FEED_HISTORY || opa_Plan_Is_Paid(published->account);
}

static uint32_t opa_Feed_Refresh(uint8_t feed){
    switch (feed){
        case OPA_FEED_HISTORY: return opa_History_Refresh(current.range);
        case OPA_FEED_FUTURES: return OPA_FUTURES_REFRESH_SEC;
        case OPA_FEED_RIGS:    return OPA_RIGS_REFRESH_SEC;
        default:               return OPA_OPEC_REFRESH_SEC;
    }
}

static uint32_t opa_Extras_Per_Day(uint32_t now){
    uint32_t perDay = 0;
    for (uint8_t f = OPA_FEED_HISTORY; f < OPA_FEED_COUNT; f++) if (opa_Feed_Wanted(f, now)) perDay += 86400 / opa_Feed_Refresh(f);
    return perDay;
}

static uint32_t opa_Next_Quote_Interval(uint32_t now){
    if (!opa_Have_Key()) return OPA_DEMO_INTERVAL_SEC;
    bool exhausted;
    return opa_Plan_Quote_Interval(published->account, now, current.share, current.floorMin, opa_Extras_Per_Day(now), exhausted);
}

static uint32_t opa_Last_Fetched(uint8_t feed){
    switch (feed){
        case OPA_FEED_QUOTES:  return published->quotesFetched;
        case OPA_FEED_HISTORY: return published->chart.fetched;
        case OPA_FEED_FUTURES: return published->curve.fetched;
        case OPA_FEED_RIGS:    return published->rigs.fetched;
        default:               return published->opec.fetched;
    }
}

// The chart in hand is of another code or range than the one asked for.
static bool opa_Chart_Outdated(void){
    return strcasecmp(published->chart.code, current.featured) != 0 || published->chart.range != current.range;
}

// When each feed is next due, from the data itself, so a reboot does not bring every device back at once.
static void opa_Schedule_From_Data(uint32_t now){
    quoteInterval = opa_Next_Quote_Interval(now);
    for (uint8_t f = 0; f < OPA_FEED_COUNT; f++){
        const uint32_t last = opa_Last_Fetched(f);
        const uint32_t interval = f == OPA_FEED_QUOTES ? quoteInterval : opa_Feed_Refresh(f);
        feeds[f].due = (last == 0 || last > now || (f == OPA_FEED_HISTORY && opa_Chart_Outdated())) ? now : last + interval;
        feeds[f].fails = 0;
        feeds[f].last = OPA_OK;
    }
    scheduled = true;
}

// The quote list follows the watchlist; quotes of codes still listed keep their data.
static void opa_Apply_Watchlist(void){
    Opa_Quote_t* old = (Opa_Quote_t*)heap_caps_malloc(sizeof(published->quotes), MALLOC_CAP_SPIRAM);
    if (old == nullptr) return;
    memcpy(old, published->quotes, sizeof(published->quotes));
    const uint8_t oldCount = published->quoteCount;
    memset(published->quotes, 0, sizeof(published->quotes));
    for (uint8_t i = 0; i < current.watchCount; i++){
        Opa_Quote_t& quote = published->quotes[i];
        int from = -1;
        for (uint8_t k = 0; k < oldCount; k++) if (strcasecmp(old[k].code, current.watch[i]) == 0) from = k;
        if (from >= 0) quote = old[from];
        else {
            snprintf(quote.code, sizeof(quote.code), "%s", current.watch[i]);
            quote.price = NAN;
            for (float& change : quote.change) change = NAN;
        }
        invalidCode[i] = false;
    }
    published->quoteCount = current.watchCount;
    heap_caps_free(old);
}

//##############################################################################################################
static void opa_Save_Cache(const Opa_Data_t& data){
    static const char tempPath[] = OPA_CACHE_FILE ".tmp";
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Opa_Cache_Header_t header = {OPA_DATA_MAGIC, OPA_DATA_LAYOUT, 0, sizeof(Opa_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Opa_Data_t))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header)
                       && file.write((const uint8_t*)&data, sizeof(Opa_Data_t)) == sizeof(Opa_Data_t);
    file.close();
    // Written to a new file and then renamed, so a power cut leaves the old cache whole.
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, OPA_CACHE_FILE)){
        LittleFS.remove(OPA_CACHE_FILE);
        if (!LittleFS.rename(tempPath, OPA_CACHE_FILE)) ESP_LOGW(TAG, "Could not save the cache");
    }
}

static bool opa_Load_Cache(Opa_Data_t& data){
    if (!LittleFS.exists(OPA_CACHE_FILE)) return false;
    File file = LittleFS.open(OPA_CACHE_FILE, "r");
    if (!file) return false;
    Opa_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(Opa_Data_t)
                     && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == OPA_DATA_MAGIC && header.layout == OPA_DATA_LAYOUT && header.size == sizeof(Opa_Data_t)
                     && file.read((uint8_t*)&data, sizeof(Opa_Data_t)) == sizeof(Opa_Data_t)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Opa_Data_t)) == header.crc
                     && data.quoteCount <= OPA_MAX_WATCH && data.chart.count <= OPA_CHART_POINTS && data.curve.count <= OPA_CURVE_MAX;
    file.close();
    if (usable){
        for (int i = 0; i < data.quoteCount; i++) if (data.quotes[i].localCount > OPA_LOCAL_POINTS) data.quotes[i].localCount = 0;
    } else ESP_LOGI(TAG, "No usable cache");
    return usable;
}

//##############################################################################################################
// What one feed asks for. Returns false if there is nothing to ask (e.g. every watchlist code is unknown).
static bool opa_Feed_Path(uint8_t feed, const Opa_Settings_t& settings, const bool* skip, char* path, size_t size){
    switch (feed){
        case OPA_FEED_QUOTES: {
            const bool demo = settings.apiKey[0] == '\0';
            size_t used = snprintf(path, size, demo ? "/v1/demo/prices?codes=" : "/v1/prices/latest?by_code=");
            const size_t start = used;
            for (uint8_t i = 0; i < settings.watchCount && used + OPA_CODE_LEN + 2 < size; i++){
                if (skip[i]) continue;
                used += snprintf(path + used, size - used, "%s%s", used > start ? "," : "", settings.watch[i]);
            }
            return used > start;
        }
        case OPA_FEED_HISTORY: {
            static const char* const ENDPOINT[OPA_RANGE_COUNT] = {
                "past_day?interval=1h&per_page=48", "past_week?interval=1h&per_page=168",
                "past_month?interval=1d&per_page=31", "past_year?interval=1w&per_page=53",
            };
            snprintf(path, size, "/v1/prices/%s&by_code=%s", ENDPOINT[settings.range], settings.featured);
            return true;
        }
        case OPA_FEED_FUTURES: snprintf(path, size, "/v1/futures/ice-brent"); return true;
        case OPA_FEED_RIGS:    snprintf(path, size, "/v1/ei/rig_counts/latest"); return true;
        default:               snprintf(path, size, "/v1/ei/opec_productions/latest"); return true;
    }
}

static const char* const FEED_NAME[OPA_FEED_COUNT] = {"Quotes", "History", "Futures", "Rig counts", "OPEC output"};

// Runs one download round and ends. Only one exists at a time; the app task never waits for it.
static void opa_Worker_Task(void* parameter){
    const uint8_t dueMask = (uint8_t)(uintptr_t)parameter;
    Opa_Data_t* work = (Opa_Data_t*)heap_caps_malloc(sizeof(Opa_Data_t), MALLOC_CAP_SPIRAM);
    Opa_Settings_t* settings = (Opa_Settings_t*)heap_caps_malloc(sizeof(Opa_Settings_t), MALLOC_CAP_SPIRAM);
    bool saveCache = false;

    for (uint8_t f = 0; f < OPA_FEED_COUNT && work != nullptr && settings != nullptr; f++){
        if (!(dueMask & (1 << f))) continue;
        char path[OPA_MAX_WATCH * (OPA_CODE_LEN + 1) + 64];
        char etag[sizeof(quotesEtag)] = "";
        bool skip[OPA_MAX_WATCH];
        opa_Lock();
        memcpy(work, published, sizeof(Opa_Data_t));
        *settings = current;
        memcpy(skip, invalidCode, sizeof(skip));
        if (f == OPA_FEED_QUOTES) memcpy(etag, quotesEtag, sizeof(etag));
        const uint32_t usedKeyGeneration = keyGeneration;
        const uint32_t usedWatchGeneration = watchGeneration;
        const bool hasPath = opa_Feed_Path(f, *settings, skip, path, sizeof(path));
        opa_Unlock();
        if (!hasPath) continue;

        const bool demo = settings->apiKey[0] == '\0';
        const uint32_t now = (uint32_t)time(nullptr);
        JsonDocument filter(opa_Json_Allocator());
        JsonDocument doc(opa_Json_Allocator());
        switch (f){
            case OPA_FEED_QUOTES:  opa_Quotes_Filter(filter); break;
            case OPA_FEED_HISTORY: opa_History_Filter(filter); break;
            case OPA_FEED_FUTURES: opa_Curve_Filter(filter); break;
            case OPA_FEED_RIGS:    opa_Rigs_Filter(filter); break;
            default:               opa_Opec_Filter(filter); break;
        }
        Opa_Response_t response = opa_Api_Get(path, demo ? nullptr : settings->apiKey, f == OPA_FEED_QUOTES ? etag : nullptr,
                                              sizeof(etag), filter, doc, FEED_NAME[f], now);
        // Read the answer into the work copy.
        if (response.result == OPA_OK){
            JsonVariantConst root = doc.as<JsonVariantConst>();
            bool parsed;
            switch (f){
                case OPA_FEED_QUOTES:  parsed = opa_Parse_Quotes(root, *work, now, demo); break;
                case OPA_FEED_HISTORY:
                    snprintf(work->chart.code, sizeof(work->chart.code), "%s", settings->featured);
                    work->chart.range = settings->range;
                    parsed = opa_Parse_History(root, work->chart);
                    break;
                case OPA_FEED_FUTURES: parsed = opa_Parse_Curve(root, work->curve); break;
                case OPA_FEED_RIGS:    parsed = opa_Parse_Rigs(root, work->rigs); break;
                default:               parsed = opa_Parse_Opec(root, work->opec); break;
            }
            if (!parsed) response.result = OPA_ERR_PARSE;
        }
        doc.clear();

        opa_Lock();
        Opa_Feed_State_t& state = feeds[f];
        state.lastTry = now;
        // The key changed while this ran: the answer says nothing about the new key, which
        // opa_Service_Set_Settings() has already scheduled.
        if (usedKeyGeneration != keyGeneration){
            opa_Unlock();
            continue;
        }
        if (response.quotaSeen){
            published->account = response.quota;
            if (response.quota.window == OPA_QUOTA_DEMO) published->account.tier[0] = '\0';
        } else if (response.result == OPA_OK && published->account.exhausted){
            published->account.exhausted = 0;
        }
        state.last = response.result;
        uint32_t wait = 0;
        const bool feature = f != OPA_FEED_QUOTES;
        switch (response.result){
            case OPA_OK: {
                state.fails = 0;
                if (feature) published->access[opa_Feature_Of(f)].state = OPA_ACCESS_OK;
                // Publish only this feed's part: the watchlist or chart may have changed while it ran.
                if (f == OPA_FEED_QUOTES){
                    for (int i = 0; i < published->quoteCount; i++){
                        const int from = opa_Find_Quote(*work, published->quotes[i].code);
                        if (from >= 0) published->quotes[i] = work->quotes[from];
                    }
                    published->quotesFetched = work->quotesFetched;
                    published->demo = work->demo;
                    if (usedWatchGeneration == watchGeneration) memcpy(quotesEtag, etag, sizeof(quotesEtag));
                }
                else if (f == OPA_FEED_HISTORY){ work->chart.fetched = now; published->chart = work->chart; }
                else if (f == OPA_FEED_FUTURES){ work->curve.fetched = now; published->curve = work->curve; }
                else if (f == OPA_FEED_RIGS){ work->rigs.fetched = now; published->rigs = work->rigs; }
                else { work->opec.fetched = now; published->opec = work->opec; }
                publishedVersion++;
                wait = f == OPA_FEED_QUOTES ? 0 : opa_Feed_Refresh(f);
                if (f != OPA_FEED_QUOTES || lastCacheSave == 0 || now - lastCacheSave >= 1800) saveCache = true;
                break;
            }
            case OPA_NOT_MODIFIED:
                state.fails = 0;
                published->quotesFetched = now;
                publishedVersion++;
                break;
            case OPA_ERR_PLAN:
                if (feature){       // Not on this plan: asked again in a week, or when the plan or key changes.
                    Opa_Access_t& access = published->access[opa_Feature_Of(f)];
                    access.state = OPA_ACCESS_LOCKED;
                    access.retryAt = now + OPA_LOCKED_RETRY_SEC;
                    access.lockedLimit = published->account.limit;
                    publishedVersion++;
                    wait = OPA_LOCKED_RETRY_SEC;
                    break;
                }
                // Quotes refused by the plan are retried like a failure.
                [[fallthrough]];
            case OPA_ERR_NETWORK:
            case OPA_ERR_PARSE:
            case OPA_ERR_SERVER:
            case OPA_ERR_NO_KEY: {
                if (state.fails < 16) state.fails++;
                wait = OPA_BACKOFF_FIRST_SEC << (state.fails - 1 < 7 ? state.fails - 1 : 7);
                if (wait > OPA_BACKOFF_MAX_SEC) wait = OPA_BACKOFF_MAX_SEC;
                break;
            }
            case OPA_ERR_AUTH:   wait = OPA_BACKOFF_MAX_SEC; break;      // Until the key changes.
            case OPA_ERR_EMAIL:  wait = 3600; break;
            case OPA_ERR_RATE:   wait = response.retryAfterSec > 0 ? response.retryAfterSec : 60; break;
            case OPA_ERR_QUOTA:
                published->account.exhausted = 1;
                published->account.remaining = 0;
                wait = response.retryAfterSec > 0 ? response.retryAfterSec : 3600;
                publishedVersion++;
                break;
            case OPA_ERR_BAD_CODE:
                // Codes the API no longer knows are left out from now on; the rest are asked for again shortly.
                for (int i = 0; i < published->quoteCount; i++){
                    if (strstr(response.invalid, published->quotes[i].code) != nullptr){
                        invalidCode[i] = true;
                        published->quotes[i].state = OPA_Q_MISSING;
                    }
                }
                publishedVersion++;
                wait = 60;
                break;
        }
        if (wait > OPA_WAIT_MAX_SEC) wait = OPA_WAIT_MAX_SEC;
        if (f == OPA_FEED_QUOTES){
            quoteInterval = opa_Next_Quote_Interval(now);
            if (wait == 0) wait = quoteInterval;
        }
        state.due = now + wait + opa_Jitter();
        const uint32_t nextInSec = state.due - now;
        opa_Unlock();
        if (response.result != OPA_OK && response.result != OPA_NOT_MODIFIED){
            ESP_LOGW(TAG, "%s: result %d (HTTP %d); next try in %u s", FEED_NAME[f], (int)response.result, response.httpCode, (unsigned)nextInSec);
        }
    }

    if (saveCache && work != nullptr){
        opa_Lock();
        memcpy(work, published, sizeof(Opa_Data_t));
        lastCacheSave = (uint32_t)time(nullptr);
        opa_Unlock();
        opa_Save_Cache(*work);
    }
    heap_caps_free(work);
    heap_caps_free(settings);
    opa_Lock();
    workerBusy = false;
    opa_Unlock();
    vTaskDelete(NULL);
}

//##############################################################################################################
void opa_Service_Begin(const Opa_Settings_t& settings){
    if (serviceLock == NULL){
        serviceLock = xSemaphoreCreateMutex();
        published = (Opa_Data_t*)heap_caps_calloc(1, sizeof(Opa_Data_t), MALLOC_CAP_SPIRAM);
        if (serviceLock == NULL || published == nullptr){
            ESP_LOGE(TAG, "Out of memory");
            return;
        }
    }
    if (!started){
        Opa_Data_t* fresh = (Opa_Data_t*)heap_caps_calloc(1, sizeof(Opa_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!opa_Load_Cache(*fresh)){
            memset(fresh, 0, sizeof(Opa_Data_t));
            for (Opa_Quote_t& quote : fresh->quotes){
                quote.price = NAN;
                for (float& change : quote.change) change = NAN;
            }
        }
        fresh->magic = OPA_DATA_MAGIC;
        fresh->layout = OPA_DATA_LAYOUT;
        fresh->size = sizeof(Opa_Data_t);
        opa_Lock();
        memcpy(published, fresh, sizeof(Opa_Data_t));
        current = settings;
        opa_Settings_Clamp(current);
        opa_Apply_Watchlist();
        publishedVersion++;
        started = true;
        scheduled = false;
        opa_Unlock();
        heap_caps_free(fresh);
    }
    opa_Service_Set_Settings(settings);
}

void opa_Service_Set_Settings(const Opa_Settings_t& settings){
    if (!started) return;
    Opa_Settings_t next = settings;
    opa_Settings_Clamp(next);
    opa_Lock();
    const uint32_t now = (uint32_t)time(nullptr);
    const bool newKey = strcmp(current.apiKey, next.apiKey) != 0;
    bool newWatch = current.watchCount != next.watchCount;
    for (uint8_t i = 0; !newWatch && i < next.watchCount; i++) newWatch = strcmp(current.watch[i], next.watch[i]) != 0;
    const bool newPace = current.share != next.share || current.floorMin != next.floorMin;
    current = next;
    if (newKey){
        keyGeneration++;
        // Another key may be on another plan: what the old one could do no longer holds.
        memset(&published->account, 0, sizeof(published->account));
        memset(published->access, 0, sizeof(published->access));
        quotesEtag[0] = '\0';
        publishedVersion++;
    }
    if (newWatch){
        opa_Apply_Watchlist();
        quotesEtag[0] = '\0';
        watchGeneration++;
        publishedVersion++;
    }
    if (scheduled){
        // A new key or watchlist is fetched at once (at most once a minute); a new chart after OPA_HISTORY_MIN_GAP_SEC.
        if (newKey){
            for (uint8_t f = 0; f < OPA_FEED_COUNT; f++){ feeds[f].due = now; feeds[f].fails = 0; feeds[f].last = OPA_OK; }
        } else if (newWatch){
            const uint32_t soonest = feeds[OPA_FEED_QUOTES].lastTry + 60;
            feeds[OPA_FEED_QUOTES].due = (int32_t)(soonest - now) > 0 ? soonest : now;
        }
        if (!newKey && opa_Chart_Outdated()){
            const uint32_t soonest = feeds[OPA_FEED_HISTORY].lastTry + OPA_HISTORY_MIN_GAP_SEC;
            const bool locked = published->access[OPA_F_HISTORY].state == OPA_ACCESS_LOCKED;
            if (!locked) feeds[OPA_FEED_HISTORY].due = (int32_t)(soonest - now) > 0 ? soonest : now;
        }
        if (newPace && !newKey && feeds[OPA_FEED_QUOTES].fails == 0){
            quoteInterval = opa_Next_Quote_Interval(now);
            if (published->quotesFetched != 0) feeds[OPA_FEED_QUOTES].due = published->quotesFetched + quoteInterval;
        }
    }
    opa_Unlock();
}

void opa_Service_Poll(bool online, uint32_t now){
    if (!started || now == 0) return;
    opa_Lock();
    if (!scheduled) opa_Schedule_From_Data(now);
    uint8_t dueMask = 0;
    if (!workerBusy && online){
        const bool quotaLeft = opa_Plan_Available(published->account, now, current.share) > 0;
        for (uint8_t f = 0; f < OPA_FEED_COUNT; f++){
            if (!opa_Feed_Wanted(f, now)) continue;
            if (f == OPA_FEED_QUOTES && !opa_Have_Key() && !OPA_DEMO_ENABLED) continue;
            if ((int32_t)(now - feeds[f].due) < 0) continue;
            // Extras only while the quota has room for them; quotes come first.
            if (f != OPA_FEED_QUOTES && !quotaLeft){ feeds[f].due = now + 3600; continue; }
            dueMask |= (uint8_t)(1 << f);
        }
    }
    if (dueMask != 0){
        lowMemory = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < OPA_WORKER_MIN_INTERNAL_HEAP;
        if (lowMemory){
            for (uint8_t f = 0; f < OPA_FEED_COUNT; f++) if (dueMask & (1 << f)) feeds[f].due = now + 60;
            dueMask = 0;
        } else workerBusy = true;
    }
    opa_Unlock();
    if (dueMask == 0) return;

    if (xTaskCreatePinnedToCore(opa_Worker_Task, "opaFetch", OPA_WORKER_STACK, (void*)(uintptr_t)dueMask, 1, NULL, tskNO_AFFINITY) != pdPASS){
        opa_Lock();
        workerBusy = false;
        for (uint8_t f = 0; f < OPA_FEED_COUNT; f++) if (dueMask & (1 << f)) feeds[f].due = now + 60;
        opa_Unlock();
        ESP_LOGW(TAG, "Could not start the download task");
    }
}

bool opa_Service_Copy(Opa_Data_t& out, uint32_t& version){
    if (!started) return false;
    opa_Lock();
    const bool changed = version != publishedVersion;
    if (changed){
        memcpy(&out, published, sizeof(Opa_Data_t));
        version = publishedVersion;
    }
    opa_Unlock();
    return changed;
}

void opa_Service_Get_Status(Opa_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (!started) return;
    opa_Lock();
    status.busy = workerBusy;
    status.lowMemory = lowMemory;
    status.demo = !opa_Have_Key();
    status.quoteInterval = quoteInterval;
    status.nextQuote = feeds[OPA_FEED_QUOTES].due;
    const Opa_Result_t last = feeds[OPA_FEED_QUOTES].last;
    status.keyRejected = last == OPA_ERR_AUTH;
    status.emailUnconfirmed = last == OPA_ERR_EMAIL;
    status.rateLimited = last == OPA_ERR_RATE;
    status.failing = last == OPA_ERR_NETWORK || last == OPA_ERR_PARSE || last == OPA_ERR_SERVER || last == OPA_ERR_PLAN;
    const uint32_t now = (uint32_t)time(nullptr);
    status.quotaUsed = published->account.exhausted && (published->account.reset == 0 || published->account.reset > now);
    status.started = true;
    status.quotesFetched = published->quotesFetched;
    status.account = published->account;
    for (uint8_t f = 0; f < OPA_F_COUNT; f++) status.access[f] = published->access[f].state;
    opa_Unlock();
}

bool opa_Service_Refresh_Now(uint32_t now){
    if (!started || now == 0) return false;
    opa_Lock();
    const Opa_Result_t last = feeds[OPA_FEED_QUOTES].last;
    bool accepted = (lastManualRefresh == 0 || now - lastManualRefresh >= OPA_MANUAL_REFRESH_GAP_SEC)
                 && last != OPA_ERR_AUTH && last != OPA_ERR_RATE
                 && !(published->account.exhausted && published->account.reset > now);
    // The reserve is there for this, so only an empty quota refuses.
    if (accepted && opa_Have_Key() && published->account.seen != 0 && published->account.remaining == 0) accepted = false;
    if (accepted){
        lastManualRefresh = now;
        feeds[OPA_FEED_QUOTES].due = now;
    }
    opa_Unlock();
    return accepted;
}
