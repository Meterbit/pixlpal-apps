// Kobo Terminal's worker. The calls are counted by the (UTC) day against the plan's allowance:
//   Once a day: the plan (a Starter call and a Professional one, tried), market status, CBN auctions, bonds, and the
//   chosen stock's fundamentals (Starter and above).
//   Prices: every stock, the market, the indices (3 calls), every kbt_Refresh_Seconds() while NGX trades (20 minutes
//   at the least: Kobo refreshes no faster, 30 minutes delayed), once after the close, never at weekends.
#include "kbt_App.h"
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

static const char TAG[] = "KBT_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Kobo Terminal)"
#define BASE "https://koboterminal.com"

ArduinoJson::Allocator* kbt_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    bool changed;
    Kbt_Settings_t* settings;
    Kbt_Data_t* data;
    Ngx_Stock_t* stocks;
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;
static uint32_t callsDay = 0, calls = 0;           // the worker's

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

static void lagos(uint32_t unix, int& wd, uint16_t& m){
    const uint32_t t = unix + 3600;
    wd = (int)((t / 86400 + 4) % 7);
    m = (uint16_t)(t % 86400 / 60);
}

static char* get(const char* path, const char* key, uint8_t& state, char* why, size_t ws, size_t maxBytes = 512 * 1024){
    const uint32_t day = (uint32_t)time(nullptr) / 86400;
    if (day != callsDay){ callsDay = day; calls = 0; }
    calls++;
    char url[200];
    snprintf(url, sizeof(url), BASE "%s", path);
    const char* headers[] = {"X-API-Key", key, "Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, maxBytes, r, UA);
    state = kbt_Check(body, r.length, r.code, why, ws);
    if (state != KBT_OK) ESP_LOGW(TAG, "%s: %s", path, why);
    return body;
}

static uint32_t next_Due(uint32_t now, uint32_t last, uint32_t interval){
    int wd;
    uint16_t m;
    lagos(now, wd, m);
    if (ngx_Trading_Time(wd, m)) return now + interval;
    const uint32_t midnight = now - m * 60u;
    const bool weekday = wd >= 1 && wd <= 5;
    // Prices are 30 minutes late: the close shows from 16:30.
    if (weekday && m >= 16 * 60 && last < midnight + (16 * 60 + 35) * 60u) return midnight + (16 * 60 + 35) * 60u;
    if (weekday && m < 9 * 60) return midnight + (9 * 60 + 35) * 60u;
    uint32_t day = midnight + 86400u;
    for (int i = 0; i < 7; i++){
        int w;
        uint16_t mm;
        lagos(day + 12 * 3600, w, mm);
        if (w >= 1 && w <= 5) break;
        day += 86400u;
    }
    return day + (9 * 60 + 35) * 60u;
}

static void set_State(uint8_t st, const char* why){
    lock();
    N.data->state = st;
    snprintf(N.data->text, sizeof(N.data->text), "%s", why ? why : "");
    N.data->callsToday = calls;
    N.version++;
    unlock();
}

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static Kbt_Settings_t* s;
static Ngx_Stock_t* fresh;
static Ngx_Table_t* table;
static uint32_t nextPrices;
static uint32_t lastPrices;
static uint32_t dailyFor;
static uint32_t waitUntil;
static uint8_t plan;
static char factsFor[16];
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Kbt_Settings_t*)heap_caps_malloc(sizeof(Kbt_Settings_t), MALLOC_CAP_SPIRAM);
    fresh = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
    table = (Ngx_Table_t*)heap_caps_calloc(1, sizeof(Ngx_Table_t), MALLOC_CAP_SPIRAM);
    if (!s || !fresh || !table) N.stop = true;
    nextPrices = 0;
    lastPrices = 0;
    dailyFor = 0;
    waitUntil = 0;
    plan = KBT_UNKNOWN;
    memset(factsFor, 0, sizeof(factsFor));
}

static void worker_End(void){
    if (s){ memset(s, 0, sizeof(*s)); heap_caps_free(s); }
    heap_caps_free(fresh);
    heap_caps_free(table);
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
        if (!s->apiKey[0]){ set_State(KBT_NO_KEY, "Add your Kobo Terminal API key in the phone app"); nextPrices = dailyFor = 0; continue; }
        if (changed){ nextPrices = dailyFor = 0; waitUntil = 0; plan = KBT_UNKNOWN; }
        if (refresh){ nextPrices = 0; waitUntil = 0; }
        if (waitUntil && now < waitUntil) continue;
        waitUntil = 0;
        const uint32_t limit = kbt_Daily_Limit(plan == KBT_UNKNOWN ? KBT_PERSONAL : plan);
        if (now / 86400 == callsDay && calls >= limit * 95 / 100){
            set_State(KBT_DAY_USED, "Today's calls are nearly used: new prices tomorrow");
            waitUntil = (now / 86400 + 1) * 86400 + 60;
            continue;
        }
        char why[96] = "";
        uint8_t st = KBT_OK;
        const uint32_t utcDay = now / 86400;

        // Once a day: the plan, the market's status, auctions, bonds.
        if (dailyFor != utcDay){
            char* body = get("/api/ngxdata/fundamentals?symbols=DANGCEM", s->apiKey, st, why, sizeof(why), 32 * 1024);
            heap_caps_free(body);
            if (st == KBT_KEY){ set_State(st, why); waitUntil = now + 900; continue; }
            if (st == KBT_RATE){ waitUntil = now + 60; continue; }
            uint8_t found = st == KBT_OK ? KBT_STARTER : st == KBT_PLAN ? KBT_PERSONAL : plan;
            if (found == KBT_STARTER){
                body = get("/api/forex/rates?symbols=NGN-USD", s->apiKey, st, why, sizeof(why), 16 * 1024);
                heap_caps_free(body);
                if (st == KBT_OK) found = KBT_PROFESSIONAL;
            }
            plan = found;
            bool open = false;
            char msg[48] = "";
            body = get("/api/ngxdata/market-status", s->apiKey, st, why, sizeof(why), 8 * 1024);
            const bool statusOk = st == KBT_OK && kbt_Parse_Status(body, strlen(body ? body : ""), open, msg, sizeof(msg));
            heap_caps_free(body);
            body = get("/api/ngxdata/bonds/auctions?limit=24", s->apiKey, st, why, sizeof(why), 128 * 1024);
            if (st == KBT_OK && kbt_Parse_Auctions(body, strlen(body ? body : ""), *table)){ table->updated = now; lock(); N.data->tbills = *table; unlock(); }
            heap_caps_free(body);
            body = get("/api/ngxdata/bonds", s->apiKey, st, why, sizeof(why), 256 * 1024);
            if (st == KBT_OK && kbt_Parse_Bonds(body, strlen(body ? body : ""), *table)){ table->updated = now; lock(); N.data->bonds = *table; unlock(); }
            heap_caps_free(body);
            lock();
            N.data->plan = plan;
            N.data->dailyLimit = kbt_Daily_Limit(plan);
            if (statusOk){ N.data->statusKnown = true; N.data->open = open; snprintf(N.data->statusText, sizeof(N.data->statusText), "%s", msg); }
            N.data->callsToday = calls;
            N.version++;
            unlock();
            dailyFor = utcDay;
            factsFor[0] = '\0';
        }

        // Prices.
        if (!nextPrices || now >= nextPrices){
            Ngx_Board_t b = {fresh, 0, {}, 0};
            char* body = get("/api/ngxdata/stocks", s->apiKey, st, why, sizeof(why), 1024 * 1024);
            const bool stocksOk = st == KBT_OK && kbt_Parse_Stocks(body, strlen(body ? body : ""), b) > 0;
            heap_caps_free(body);
            if (st == KBT_KEY || st == KBT_RATE || st == KBT_DAY_USED){
                set_State(st, why);
                waitUntil = now + (st == KBT_RATE ? 60 : st == KBT_DAY_USED ? 3600 : 900);
                continue;
            }
            Ngx_Market_t market = {};
            body = get("/api/ngxdata/market", s->apiKey, st, why, sizeof(why), 64 * 1024);
            const bool marketOk = st == KBT_OK && kbt_Parse_Market(body, strlen(body ? body : ""), market);
            heap_caps_free(body);
            if (!marketOk && stocksOk) ngx_Breadth(b, market);
            body = get("/api/ngxdata/indices", s->apiKey, st, why, sizeof(why), 128 * 1024);
            const bool indicesOk = st == KBT_OK && kbt_Parse_Indices(body, strlen(body ? body : ""), *table);
            heap_caps_free(body);
            lock();
            if (stocksOk){ memcpy(N.stocks, fresh, sizeof(Ngx_Stock_t) * (size_t)b.count); N.data->board.count = b.count; N.data->board.updated = now; }
            if (marketOk || stocksOk) N.data->board.market = market;
            if (indicesOk){ table->updated = now; N.data->indices = *table; }
            N.data->state = stocksOk ? KBT_OK : st;
            snprintf(N.data->text, sizeof(N.data->text), "%s", stocksOk ? "" : why);
            N.data->callsToday = calls;
            N.version++;
            unlock();
            lastPrices = now;
            int wd;
            uint16_t m;
            lagos(now, wd, m);
            const uint32_t left = kbt_Daily_Limit(plan == KBT_UNKNOWN ? KBT_PERSONAL : plan);
            const uint32_t interval = kbt_Refresh_Seconds(left > calls ? left - calls : 0, 3, m);
            nextPrices = stocksOk ? next_Due(now, lastPrices, interval) : now + 300;
            lock();
            N.data->nextRefresh = nextPrices;
            unlock();
        }
        // The chosen stock's fundamentals (Starter and above), once a day or when it changes.
        if (s->focus[0] && plan != KBT_UNKNOWN && plan >= KBT_STARTER && strcmp(factsFor, s->focus)){
            char path[80], lines[6][40];
            snprintf(path, sizeof(path), "/api/ngxdata/fundamentals?symbols=%s", s->focus);
            char* body = get(path, s->apiKey, st, why, sizeof(why), 32 * 1024);
            const int n = st == KBT_OK ? kbt_Parse_Fundamentals(body, strlen(body ? body : ""), s->focus, lines) : 0;
            heap_caps_free(body);
            lock();
            memcpy(N.data->facts, lines, sizeof(lines));
            N.data->factsCount = n;
            snprintf(N.data->factsSymbol, sizeof(N.data->factsSymbol), "%s", s->focus);
            N.data->callsToday = calls;
            N.version++;
            unlock();
            snprintf(factsFor, sizeof(factsFor), "%s", s->focus);
        }
    } while (0);
}

bool kbt_Net_Start(const Kbt_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Kbt_Settings_t*)heap_caps_calloc(1, sizeof(Kbt_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Kbt_Data_t*)heap_caps_calloc(1, sizeof(Kbt_Data_t), MALLOC_CAP_SPIRAM);
        N.stocks = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data || !N.stocks) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Kbt_Data_t));
    N.data->board.stocks = N.stocks;
    N.data->plan = KBT_UNKNOWN;
    N.data->state = KBT_WAIT;
    N.data->dailyLimit = 100;
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

void kbt_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    lock();
    memset(N.settings->apiKey, 0, sizeof(N.settings->apiKey));
    unlock();
}

void kbt_Net_Set_Settings(const Kbt_Settings_t& settings){
    if (!made) return;
    lock();
    const bool keyChanged = strcmp(N.settings->apiKey, settings.apiKey) != 0;
    *N.settings = settings;
    if (keyChanged) N.changed = true;
    unlock();
}

void kbt_Net_Refresh(void){ N.refresh = true; }

bool kbt_Net_Copy(Kbt_Data_t& d, uint32_t& version){
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
