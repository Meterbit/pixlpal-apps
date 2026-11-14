#include "pfx_Service.h"
#include "mtb_kit_http.h"
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char TAG[] = "PFX_SERVICE";

#define PFX_HOST            "https://api.massive.com"
#define PFX_USER_AGENT      "Pixlpal-FX/2.0 (ESP32 LED display)"
#define PFX_MAX_BODY        (96 * 1024)
#define PFX_BASIC_GAP       13          // Seconds between calls on Basic: under 5 a minute.
#define PFX_DAILY_EVERY     3600        // Daily bars: end-of-day data changes once a day; hourly is plenty.
#define PFX_SNAPSHOT_OPEN   15
#define PFX_SNAPSHOT_CLOSED 300
#define PFX_INTRADAY_EVERY  120
#define PFX_PLAN_EVERY      86400
#define PFX_MANUAL_GAP      120

enum Pfx_Job_t : uint8_t { JOB_NONE = 0, JOB_SNAPSHOT, JOB_DAILY, JOB_INTRADAY };

static SemaphoreHandle_t lock = NULL;
static Pfx_Data_t* published = nullptr;     // PSRAM
static Pfx_Settings_t current;
static uint32_t version = 1, generation = 0;
static bool started = false, busy = false;
static uint32_t nextAllowed = 0, snapshotDue = 0, intradayDue = 0, lastFetch = 0, lastManual = 0;
static uint32_t dailyDue[PFX_PAIRS];
static uint8_t fails = 0;

static void pfx_Lock(void){ xSemaphoreTake(lock, portMAX_DELAY); }
static void pfx_Unlock(void){ xSemaphoreGive(lock); }

struct Pfx_Job_Args_t { uint8_t job; uint8_t pair; };

static size_t pfx_Url(char* url, size_t size, const Pfx_Settings_t& s, uint8_t job, uint8_t pair, uint32_t now){
    char from[16], to[16];
    if (job == JOB_SNAPSHOT){
        size_t n = (size_t)snprintf(url, size, PFX_HOST "/v2/snapshot/locale/global/markets/forex/tickers?tickers=");
        for (uint8_t i = 0; i < s.pairCount && n < size; i++) n += (size_t)snprintf(url + n, size - n, "%sC:%s%s", i ? "," : "", s.pairs[i].from, s.pairs[i].to);
        return n + (size_t)snprintf(url + n, n < size ? size - n : 0, "&apiKey=%s", s.apiKey);
    }
    const Pfx_Pair_Code_t& p = s.pairs[pair];
    if (job == JOB_DAILY){
        pfx_Date(from, sizeof(from), now - 45UL * 86400);
        pfx_Date(to, sizeof(to), now);
        return (size_t)snprintf(url, size, PFX_HOST "/v2/aggs/ticker/C:%s%s/range/1/day/%s/%s?adjusted=true&sort=asc&limit=60&apiKey=%s", p.from, p.to, from, to, s.apiKey);
    }
    pfx_Date(from, sizeof(from), now - 86400);
    pfx_Date(to, sizeof(to), now);
    return (size_t)snprintf(url, size, PFX_HOST "/v2/aggs/ticker/C:%s%s/range/15/minute/%s/%s?adjusted=true&sort=asc&limit=300&apiKey=%s", p.from, p.to, from, to, s.apiKey);
}

static void pfx_Worker(void* parameter){
    Pfx_Job_Args_t args;
    memcpy(&args, &parameter, sizeof(args));
    Pfx_Settings_t* s = (Pfx_Settings_t*)heap_caps_malloc(sizeof(Pfx_Settings_t), MALLOC_CAP_SPIRAM);
    Pfx_Data_t* work = (Pfx_Data_t*)heap_caps_malloc(sizeof(Pfx_Data_t), MALLOC_CAP_SPIRAM);
    if (s && work){
        pfx_Lock();
        *s = current;
        memcpy(work, published, sizeof(*work));
        const uint32_t gen = generation;
        pfx_Unlock();
        uint32_t now = (uint32_t)time(nullptr);
        char url[640];
        pfx_Url(url, sizeof(url), *s, args.job, args.pair, now);
        Mtb_Kit_Http_t http;
        char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, PFX_MAX_BODY, http, PFX_USER_AGENT);
        memset(url, 0, sizeof(url));
        uint8_t result = PFX_NET;
        char text[96] = "";
        if (http.code > 0){
            JsonDocument doc(mtb_Kit_Json_Allocator());
            const bool parsed = body && !deserializeJson(doc, body, http.length);
            JsonVariantConst root = parsed ? doc.as<JsonVariantConst>() : JsonVariantConst();
            result = pfx_Error_Of(http.code, root, text, sizeof(text));
            if (result == PFX_OK){
                if (args.job == JOB_SNAPSHOT) result = pfx_Parse_Snapshot(root, *s, *work, now);
                else if (args.job == JOB_DAILY) result = pfx_Parse_Daily(root, work->quotes[args.pair]);
                else { result = pfx_Parse_Intraday(root, *work); work->intradayPair = args.pair; }
            }
        }
        heap_caps_free(body);
        now = (uint32_t)time(nullptr);
        ESP_LOGI(TAG, "Job %u pair %u: HTTP %d, result %u", (unsigned)args.job, (unsigned)args.pair, http.code, (unsigned)result);

        pfx_Lock();
        if (published->callsDay != now / 86400){ published->callsDay = now / 86400; published->callsToday = 0; }
        if (http.code > 0) published->callsToday++;
        lastFetch = now;
        const bool same = gen == generation;
        // The snapshot is also the plan check: only paid plans have it.
        if (args.job == JOB_SNAPSHOT && (result == PFX_OK || result == PFX_PLAN)){
            published->planKnown = 1;
            published->realtime = result == PFX_OK;
            published->planChecked = now;
            if (!published->realtime) for (auto& q : published->quotes) q.realtime = 0;
        }
        if (result == PFX_OK && same){
            const uint32_t calls = published->callsToday, day = published->callsDay;
            const uint8_t known = published->planKnown, realtime = published->realtime;
            const uint32_t checked = published->planChecked;
            memcpy(published, work, sizeof(*work));
            published->callsToday = calls; published->callsDay = day;
            published->planKnown = known; published->realtime = realtime; published->planChecked = checked;
            published->lastError = PFX_OK;
            published->errorText[0] = '\0';
            fails = 0;
        } else if (result != PFX_OK && !(args.job == JOB_SNAPSHOT && result == PFX_PLAN)){
            published->lastError = result;
            snprintf(published->errorText, sizeof(published->errorText), "%s", text);
            if (result == PFX_BAD && args.job == JOB_DAILY && same) published->quotes[args.pair].error = 1;
        }
        const bool realtime = published->realtime;
        const bool open = pfx_Market_Open(now);
        uint32_t wait = realtime ? 1 : PFX_BASIC_GAP;
        switch (result){
            case PFX_OK: break;
            case PFX_PLAN: break;
            case PFX_RATE: wait = 65; break;
            case PFX_KEY: wait = 1800; break;
            case PFX_BAD: break;
            default: {
                if (fails < 8) fails++;
                wait = 30UL << (fails - 1);
                if (wait > 3600) wait = 3600;
                break;
            }
        }
        nextAllowed = now + wait;
        if (args.job == JOB_SNAPSHOT) snapshotDue = now + (result == PFX_OK ? (open ? PFX_SNAPSHOT_OPEN : PFX_SNAPSHOT_CLOSED) : PFX_PLAN_EVERY);
        else if (args.job == JOB_DAILY) dailyDue[args.pair] = now + (result == PFX_BAD ? 12UL * 3600 : result == PFX_OK ? (realtime ? 3 * PFX_DAILY_EVERY : PFX_DAILY_EVERY) : wait + 60);
        else intradayDue = now + (open ? PFX_INTRADAY_EVERY : 10 * PFX_INTRADAY_EVERY);
        version++;
        pfx_Unlock();
    }
    heap_caps_free(work);
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    pfx_Lock();
    busy = false;
    pfx_Unlock();
    vTaskDelete(NULL);
}

void pfx_Service_Begin(const Pfx_Settings_t& settings){
    if (lock == NULL){
        lock = xSemaphoreCreateMutex();
        published = (Pfx_Data_t*)heap_caps_calloc(1, sizeof(Pfx_Data_t), MALLOC_CAP_SPIRAM);
        if (lock == NULL || published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    if (!started){
        pfx_Lock();
        current = settings;
        // Basic's first: the pairs' bars, then the plan check.
        const uint32_t now = (uint32_t)time(nullptr);
        for (uint8_t i = 0; i < PFX_PAIRS; i++) dailyDue[i] = now;
        snapshotDue = now;
        intradayDue = now;
        started = true;
        version++;
        pfx_Unlock();
    }
    pfx_Service_Set_Settings(settings);
}

void pfx_Service_Set_Settings(const Pfx_Settings_t& settings){
    if (!started) return;
    pfx_Lock();
    const bool newPairs = settings.pairCount != current.pairCount || memcmp(settings.pairs, current.pairs, sizeof(settings.pairs)) != 0;
    const bool newKey = strcmp(settings.apiKey, current.apiKey) != 0;
    const bool newFocus = settings.focus != current.focus;
    current = settings;
    const uint32_t now = (uint32_t)time(nullptr);
    if (newPairs || newKey){
        generation++;
        memset(published->quotes, 0, sizeof(published->quotes));
        published->intradayCount = 0;
        for (uint8_t i = 0; i < PFX_PAIRS; i++) dailyDue[i] = now;
        snapshotDue = now;
        intradayDue = now;
        published->lastError = PFX_OK;
        published->errorText[0] = '\0';
        nextAllowed = now;
        fails = 0;
    }
    if (newKey){ published->planKnown = 0; published->realtime = 0; }
    if (newFocus){ published->intradayCount = 0; intradayDue = now; }
    version++;
    pfx_Unlock();
}

void pfx_Service_Poll(bool online, uint32_t now){
    if (!started || now == 0) return;
    Pfx_Job_Args_t args = {JOB_NONE, 0};
    pfx_Lock();
    if (!busy && online && current.apiKey[0] && (int32_t)(now - nextAllowed) >= 0){
        int dueDaily = -1;
        for (uint8_t i = 0; i < current.pairCount && dueDaily < 0; i++) if ((int32_t)(now - dailyDue[i]) >= 0) dueDaily = i;
        const bool snapshotNow = (int32_t)(now - snapshotDue) >= 0;
        const bool intradayNow = current.intraday && (int32_t)(now - intradayDue) >= 0;
        uint8_t job = JOB_NONE;
        if (!published->planKnown){
            // First the board (pairs not yet shown), then the plan check.
            if (dueDaily >= 0 && !published->quotes[dueDaily].valid) job = JOB_DAILY;
            else if (snapshotNow) job = JOB_SNAPSHOT;
            else if (dueDaily >= 0) job = JOB_DAILY;
        } else if (published->realtime){
            job = snapshotNow ? JOB_SNAPSHOT : dueDaily >= 0 ? JOB_DAILY : intradayNow ? JOB_INTRADAY : JOB_NONE;
        } else {
            // Basic: the bars; the snapshot only as the daily plan check.
            job = dueDaily >= 0 ? JOB_DAILY : snapshotNow ? JOB_SNAPSHOT : JOB_NONE;
        }
        args.job = job;
        args.pair = job == JOB_INTRADAY ? current.focus : job == JOB_DAILY ? (uint8_t)dueDaily : 0;
        if (args.job != JOB_NONE) busy = true;
    }
    pfx_Unlock();
    if (args.job == JOB_NONE) return;
    void* parameter = nullptr;
    memcpy(&parameter, &args, sizeof(args));
    if (xTaskCreatePinnedToCore(pfx_Worker, "pfxFetch", 9216, parameter, 1, NULL, tskNO_AFFINITY) != pdPASS){
        pfx_Lock();
        busy = false;
        pfx_Unlock();
    }
}

bool pfx_Service_Copy(Pfx_Data_t& out, uint32_t& seen){
    if (!started) return false;
    pfx_Lock();
    const bool changed = seen != version;
    if (changed){ memcpy(&out, published, sizeof(out)); seen = version; }
    pfx_Unlock();
    return changed;
}

void pfx_Service_Get_Status(Pfx_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (!started) return;
    pfx_Lock();
    status.started = true;
    status.busy = busy;
    status.keyMissing = current.apiKey[0] == '\0';
    status.keyRejected = published->lastError == PFX_KEY;
    status.rateLimited = published->lastError == PFX_RATE;
    status.failing = published->lastError == PFX_NET || published->lastError == PFX_SERVER || published->lastError == PFX_PARSE;
    status.lastFetch = lastFetch;
    pfx_Unlock();
}

bool pfx_Service_Refresh(uint32_t now){
    if (!started || now == 0) return false;
    pfx_Lock();
    const bool accepted = (lastManual == 0 || now - lastManual >= PFX_MANUAL_GAP) && current.apiKey[0];
    if (accepted){
        lastManual = now;
        for (uint8_t i = 0; i < PFX_PAIRS; i++) dailyDue[i] = now;
        snapshotDue = now;
        intradayDue = now;
    }
    pfx_Unlock();
    return accepted;
}
