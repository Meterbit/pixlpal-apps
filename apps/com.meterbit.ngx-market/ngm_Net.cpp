// NGX Market's worker: NGN Market's REST API, paced so the plan's monthly calls last.
//   Daily (and at the start): market status, forex, dividends (Starter and above).
//   Prices: snapshot, every company (one call of 200), indices, and the chosen stock's chart (Hobby and above), every
//   ngm_Refresh_Seconds() while NGX trades (20 minutes at the least: NGN Market refreshes no faster), once more after
//   the close, and not at all at weekends or on holidays.
#include "ngm_App.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char TAG[] = "NGM_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal NGX Market)"
#define BASE "https://api.ngnmarket.com/v1"

ArduinoJson::Allocator* ngm_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    Ngm_Settings_t* settings;           // under lock
    Ngm_Data_t* data;                   // under lock (its board.stocks: stocks)
    Ngx_Stock_t* stocks;
    uint32_t version;
    bool changed;                       // settings changed: the chart may need fetching
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

// One GET, its meta noted. Returns the body (freed by the caller) and the state.
static char* get(const char* path, const char* key, uint8_t& state, char* why, size_t ws, Ngm_Meta_t& meta, size_t maxBytes = 512 * 1024){
    char url[200], auth[100];
    snprintf(url, sizeof(url), BASE "%s", path);
    snprintf(auth, sizeof(auth), "Bearer %s", key);
    const char* headers[] = {"Authorization", auth, "Accept", "application/json", nullptr};
    const char* collect[] = {"Retry-After"};
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, headers, collect, 1, maxBytes, r, UA);
    memset(auth, 0, sizeof(auth));
    state = ngm_Check(body, r.length, r.code, meta, why, ws);
    if (state != NGM_OK) ESP_LOGW(TAG, "%s: %s", path, why);
    return body;
}

static void put_Meta(const Ngm_Meta_t& meta){
    lock();
    if (meta.known) N.data->meta = meta;
    N.version++;
    unlock();
}

// The next time prices are worth asking for, in Lagos time: the paced interval while NGX trades; else 16:20 after a
// session (its closing prices), else the next weekday's 09:25 (its first update).
static uint32_t next_Due(uint32_t now, uint32_t lastRefresh, uint32_t interval){
    int wd;
    uint16_t m;
    ngm_Lagos(now, wd, m);
    if (ngx_Trading_Time(wd, m)) return now + interval;
    const uint32_t midnight = now - m * 60u;
    const bool weekday = wd >= 1 && wd <= 5;
    if (weekday && m >= 16 * 60 && lastRefresh < midnight + (16 * 60 + 20) * 60u) return midnight + (16 * 60 + 20) * 60u;
    if (weekday && m < 9 * 60) return midnight + (9 * 60 + 25) * 60u;
    // The next weekday morning.
    uint32_t day = midnight + 86400u;
    for (int i = 0; i < 7; i++){
        int w;
        uint16_t mm;
        ngm_Lagos(day + 12 * 3600, w, mm);
        if (w >= 1 && w <= 5) break;
        day += 86400u;
    }
    return day + (9 * 60 + 25) * 60u;
}

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static Ngm_Settings_t* s;
static Ngx_Stock_t* fresh;
static Ngx_Table_t* table;
static float* chart;
static uint32_t nextPrices;
static uint32_t lastPrices;
static uint32_t dailyFor;
static uint32_t waitUntil;
static char chartFor[16];
static uint8_t chartPeriod;
static Ngm_Meta_t meta;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Ngm_Settings_t*)heap_caps_malloc(sizeof(Ngm_Settings_t), MALLOC_CAP_SPIRAM);
    fresh = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
    table = (Ngx_Table_t*)heap_caps_calloc(1, sizeof(Ngx_Table_t), MALLOC_CAP_SPIRAM);
    chart = (float*)heap_caps_calloc(NGX_CHART_MAX, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s || !fresh || !table || !chart) N.stop = true;
    nextPrices = 0;
    lastPrices = 0;
    dailyFor = 0;
    waitUntil = 0;
    memset(chartFor, 0, sizeof(chartFor));
    chartPeriod = 255;
    meta = Ngm_Meta_t();
    meta.plan = NGM_UNKNOWN;
}

static void worker_End(void){
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    heap_caps_free(fresh);
    heap_caps_free(table);
    heap_caps_free(chart);
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
        const bool refresh = N.refresh, changed = N.changed;
        N.refresh = false;
        N.changed = false;
        unlock();
        if (!s->apiKey[0]){
            lock();
            N.data->state = NGM_NO_KEY;
            snprintf(N.data->text, sizeof(N.data->text), "Add your NGN Market API key in the phone app");
            N.version++;
            unlock();
            nextPrices = dailyFor = 0;
            continue;
        }
        if (changed){ nextPrices = 0; waitUntil = 0; }            // a new key or watchlist: ask now
        if (refresh){ nextPrices = 0; waitUntil = 0; }
        if (waitUntil && now < waitUntil) continue;
        waitUntil = 0;
        char why[96] = "";
        uint8_t st = NGM_OK;

        // Daily: status, forex, dividends.
        int wd;
        uint16_t minute;
        ngm_Lagos(now, wd, minute);
        const uint32_t lagosDay = (now + 3600) / 86400;
        if (dailyFor != lagosDay){
            char* body = get("/market/status", s->apiKey, st, why, sizeof(why), meta, 16 * 1024);
            Ngm_Status_t ms;
            if (st == NGM_OK && ngm_Parse_Status(body, strlen(body ? body : ""), ms)){ lock(); N.data->status = ms; N.version++; unlock(); }
            heap_caps_free(body);
            if (st == NGM_KEY || st == NGM_QUOTA || st == NGM_RATE){
                lock(); N.data->state = st; snprintf(N.data->text, sizeof(N.data->text), "%s", why); N.version++; unlock();
                put_Meta(meta);
                waitUntil = now + (st == NGM_RATE ? 60 : st == NGM_QUOTA ? (meta.resetAt > now ? meta.resetAt - now : 3600) : 900);
                continue;
            }
            body = get("/forex/current", s->apiKey, st, why, sizeof(why), meta, 32 * 1024);
            if (st == NGM_OK && ngm_Parse_Forex(body, strlen(body ? body : ""), *table)){ table->updated = now; lock(); N.data->forex = *table; N.version++; unlock(); }
            heap_caps_free(body);
            bool dividends = false;
            if (meta.plan == NGM_UNKNOWN || meta.plan >= NGM_STARTER){
                body = get("/dividends/upcoming?limit=24", s->apiKey, st, why, sizeof(why), meta, 64 * 1024);
                if (st == NGM_OK && ngm_Parse_Dividends(body, strlen(body ? body : ""), *table)){
                    table->updated = now;
                    dividends = true;
                    lock(); N.data->dividends = *table; unlock();
                }
                heap_caps_free(body);
            }
            lock();
            N.data->dividendsAllowed = dividends;
            N.version++;
            unlock();
            put_Meta(meta);
            dailyFor = lagosDay;
        }

        // Prices.
        const bool chartWanted = s->focus[0] && (meta.plan == NGM_UNKNOWN || meta.plan >= NGM_HOBBY) &&
                                 (strcmp(chartFor, s->focus) || chartPeriod != s->chartPeriod);
        if (nextPrices && now < nextPrices && !chartWanted) continue;
        if (!nextPrices || now >= nextPrices){
            Ngx_Market_t market = {};
            char* body = get("/market/snapshot", s->apiKey, st, why, sizeof(why), meta, 32 * 1024);
            const bool snapOk = st == NGM_OK && ngm_Parse_Snapshot(body, strlen(body ? body : ""), market);
            heap_caps_free(body);
            if (st == NGM_KEY || st == NGM_QUOTA || st == NGM_RATE){
                lock(); N.data->state = st; snprintf(N.data->text, sizeof(N.data->text), "%s", why); N.version++; unlock();
                put_Meta(meta);
                waitUntil = now + (st == NGM_RATE ? 60 : st == NGM_QUOTA ? (meta.resetAt > now ? meta.resetAt - now : 3600) : 900);
                continue;
            }
            // Every company, 200 a page (about 150 are listed: one call).
            Ngx_Board_t b = {fresh, 0, {}, 0};
            int pages = 1;
            for (int page = 1; page <= pages && page <= 3; page++){
                char path[64];
                snprintf(path, sizeof(path), "/companies?limit=200&page=%d", page);
                body = get(path, s->apiKey, st, why, sizeof(why), meta, 1024 * 1024);
                const int p = st == NGM_OK ? ngm_Parse_Companies(body, strlen(body ? body : ""), b) : 0;
                heap_caps_free(body);
                if (!p) break;
                pages = p;
            }
            const bool companiesOk = b.count > 0;
            body = get("/indices", s->apiKey, st, why, sizeof(why), meta, 32 * 1024);
            const bool indicesOk = st == NGM_OK && ngm_Parse_Indices(body, strlen(body ? body : ""), *table);
            heap_caps_free(body);
            if (!snapOk && companiesOk){ market = {}; ngx_Breadth(b, market); }
            lock();
            if (companiesOk){
                memcpy(N.stocks, fresh, sizeof(Ngx_Stock_t) * (size_t)b.count);
                N.data->board.count = b.count;
                N.data->board.updated = now;
            }
            if (snapOk || companiesOk) N.data->board.market = market;
            if (indicesOk){ table->updated = now; N.data->indices = *table; }
            N.data->state = companiesOk ? NGM_OK : st;
            snprintf(N.data->text, sizeof(N.data->text), "%s", companiesOk ? "" : why);
            N.version++;
            unlock();
            lastPrices = now;
            const uint32_t per = 3 + (meta.plan >= NGM_HOBBY && meta.plan != NGM_UNKNOWN ? 1 : 0);
            const uint32_t interval = meta.known ? ngm_Refresh_Seconds(meta.callsRemaining, per, now, meta.resetAt) : 20 * 60;
            nextPrices = companiesOk ? next_Due(now, lastPrices, interval) : now + 300;
            chartFor[0] = '\0';                                           // the chart follows the new prices
        }
        // The chosen stock's chart (Hobby and above): when it or its period changes, and after each price refresh.
        const bool needChart = s->focus[0] && (meta.plan == NGM_UNKNOWN || meta.plan >= NGM_HOBBY) &&
                               (strcmp(chartFor, s->focus) || chartPeriod != s->chartPeriod);
        if (needChart){
            char path[96];
            snprintf(path, sizeof(path), "/companies/%s/chart?period=%s&format=chart", s->focus, ngm_Period(s->chartPeriod));
            char* body = get(path, s->apiKey, st, why, sizeof(why), meta, 256 * 1024);
            const int n = st == NGM_OK ? ngm_Parse_Chart(body, strlen(body ? body : ""), chart, NGX_CHART_MAX) : 0;
            heap_caps_free(body);
            lock();
            N.data->chartAllowed = st != NGM_PLAN;
            if (n >= 2){ memcpy(N.data->chart, chart, sizeof(float) * (size_t)n); N.data->chartCount = n; snprintf(N.data->chartSymbol, sizeof(N.data->chartSymbol), "%s", s->focus); }
            else N.data->chartCount = 0;
            N.version++;
            unlock();
            snprintf(chartFor, sizeof(chartFor), "%s", s->focus);
            chartPeriod = s->chartPeriod;
        }
        lock();
        N.data->nextRefresh = nextPrices;
        unlock();
        put_Meta(meta);
    } while (0);
}

bool ngm_Net_Start(const Ngm_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Ngm_Settings_t*)heap_caps_calloc(1, sizeof(Ngm_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Ngm_Data_t*)heap_caps_calloc(1, sizeof(Ngm_Data_t), MALLOC_CAP_SPIRAM);
        N.stocks = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data || !N.stocks) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Ngm_Data_t));
    N.data->board.stocks = N.stocks;
    N.data->meta.plan = NGM_UNKNOWN;
    N.data->state = NGM_WAIT;
    N.version++;
    unlock();
    N.stop = false;
    N.refresh = false;
    N.changed = false;
    N.running = true;
    worker_Begin();
    if (N.running) pxl_compat_add_background(worker_Step);
    return true;
}

void ngm_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    lock();
    memset(N.settings->apiKey, 0, sizeof(N.settings->apiKey));
    unlock();
}

void ngm_Net_Set_Settings(const Ngm_Settings_t& settings){
    if (!made) return;
    lock();
    const bool keyChanged = strcmp(N.settings->apiKey, settings.apiKey) != 0;
    *N.settings = settings;
    if (keyChanged) N.changed = true;
    unlock();
}

void ngm_Net_Refresh(void){ N.refresh = true; }

bool ngm_Net_Copy(Ngm_Data_t& d, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){
        Ngx_Stock_t* mine = d.board.stocks;
        d = *N.data;
        d.board.stocks = mine;
        if (mine && N.data->board.count) memcpy(mine, N.stocks, sizeof(Ngx_Stock_t) * (size_t)N.data->board.count);
        version = N.version;
    }
    unlock();
    return changed;
}
