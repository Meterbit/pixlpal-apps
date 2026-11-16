/**
 * CoinMarketCap Crypto: a crypto watchlist from CoinMarketCap (pro-api.coinmarketcap.com) with the owner's own API key.
 *
 * Pages in turn: each coin (price, rank, changes over 1 hour to 90 days, market value and the day's volume, and a 7-day
 * chart if the plan has price history, else the changes as bars); the market (its total value, the day's volume,
 * Bitcoin's and Ether's shares, the Fear & Greed index); and the day's biggest movers among the top 50.
 *
 * The plan: /v1/key/info (no credits) gives the credits left this month and when they renew. The app spends the share
 * of them the owner allows, evenly over the time left: a bucket fills at that rate and every request takes its credits
 * from it; prices come first, the market, Fear & Greed, the movers and the chart only when there are credits to spare.
 * The free plan's 15,000 credits a month give prices every few minutes; paid plans down to every 30 seconds. What the
 * plan doesn't include (CoinMarketCap answers 1006) is left out, and a plan out of credits waits for its renewal.
 *
 * Symbols are looked up once (/v1/cryptocurrency/map, the most valued coin of each symbol); prices are asked by id.
 * The newer v3 paths are tried first, and their v2 or v1 forms if CoinMarketCap answers 404. NVS "cmcSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include "cmc_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#ifndef CMC_BASE
#define CMC_BASE    "https://pro-api.coinmarketcap.com"
#endif
#define W           256
#define H           128
#define COINS       10
#define CHART       48
#define MOVERS      5
#define KEY_LEN     72
#define BODY_MAX    (60 * 1024)
#define NVS_KEY     "cmcSave"
#define SAVE_MAGIC  0x434D4301u
#define UA          "Pixlpal-CoinMarketCap/1.0"

extern "C" void pxl_compat_breathe(void);

EXT_RAM_BSS_ATTR TaskHandle_t coinMarketCap_Task_H = NULL;
static void coinMarketCap_App_Task(void* dApplication);
static void cmc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* coinMarketCap_App;
Mtb_Applications* coinMarketCap_App_GetInstance(){
    if (!coinMarketCap_App){
        coinMarketCap_App = new Mtb_Applications_FullScreen(coinMarketCap_App_Task, &coinMarketCap_Task_H, "CoinMarketCap", {0,0}, 8192);
        coinMarketCap_App->mtb_App_Set_Mobile_UI(CMC_UI_MANIFEST, CMC_UI_API, nullptr, cmc_Values);
    }
    return coinMarketCap_App;
}
MTB_REGISTER_APP(coinMarketCap_App, 0, 0)     // the catalogue gives the app its number

// What is kept ----------------------------------------------------------------------------------------------------------------------
struct Cmc_Save_t {
    uint32_t magic;
    char key[KEY_LEN];
    char watch[COINS][12];              // symbols, as given
    uint8_t watchN;
    char convert[8];
    uint8_t share, seconds, market, movers;
    int32_t id[COINS];                  // the symbols' CoinMarketCap ids (0: not looked up yet; -1: none)
};

// What was read ---------------------------------------------------------------------------------------------------------------------
struct Cmc_Coin_t {
    char name[24];
    int rank;
    double price, mcap, vol, ch[5];     // ch: 1h, 24h, 7d, 30d, 90d (%)
    bool ok;
    float chart[CHART];
    uint8_t chartN;
    uint32_t chartAt;                   // Unix seconds the chart was read
};
struct Cmc_Mover_t { char sym[12]; double ch; };

enum : uint8_t { K_NONE, K_OK, K_BAD, K_NO_ANSWER, K_NOT_ALLOWED };
enum : uint8_t { P_OK, P_MINUTE, P_DAY, P_MONTH };

struct Cmc_Shared_t {
    Cmc_Coin_t coin[COINS];
    bool idsOk;                         // the watchlist's ids looked up
    // The market.
    bool marketOk; double totalCap, totalVol, btcDom, ethDom; uint32_t marketAt;
    bool fngOk; int fng; char fngText[20]; uint32_t fngAt;
    bool moversOk; Cmc_Mover_t up[MOVERS], down[MOVERS]; uint8_t upN, downN; uint32_t moversAt;
    // The plan.
    uint8_t key, pause;                 // K_..., P_...
    uint32_t pauseUntil;
    bool planOk; uint32_t monthly, left, usedToday; int64_t renews; uint16_t perMinute; uint32_t planAt;
    bool noChart, noMarket, noFng, noMovers;   // not in the plan
    float bucket, rate;                 // credits banked; credits a second
    uint32_t quoteEvery, pricesAt;      // seconds between price reads; when they were last read (Unix)
    char problem[64];
    uint32_t version;
};

static EXT_RAM_BSS_ATTR Cmc_Save_t save;
static EXT_RAM_BSS_ATTR Cmc_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, readNow = false, workerStop = false, workerRunning = false;
static volatile int shownCoin = -1;

static void read_Save(Cmc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC;
        const char* const D[] = {"BTC", "ETH", "SOL", "XRP", "BNB"};
        for (int i = 0; i < 5; i++) snprintf(s.watch[i], sizeof(s.watch[i]), "%s", D[i]);
        s.watchN = 5;
        snprintf(s.convert, sizeof(s.convert), "USD");
        s.share = 80; s.seconds = 10; s.market = 1; s.movers = 1;
    }
    if (s.watchN > COINS) s.watchN = 0;
    if (s.share < 10 || s.share > 100) s.share = 80;
    if (s.seconds < 5 || s.seconds > 60) s.seconds = 10;
    s.key[KEY_LEN - 1] = '\0';
    s.convert[sizeof(s.convert) - 1] = '\0';
    if (!s.convert[0]) snprintf(s.convert, sizeof(s.convert), "USD");
    for (int i = 0; i < COINS; i++) s.watch[i][sizeof(s.watch[i]) - 1] = '\0';
}

static uint32_t now_S(void){ return (uint32_t)time(nullptr); }

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void clear_Read(void){        // under the lock: what was read for the old watchlist, currency or key goes
    memset(sh.coin, 0, sizeof(sh.coin));
    sh.idsOk = false; sh.marketOk = sh.moversOk = false; sh.pricesAt = 0; sh.marketAt = sh.moversAt = 0;
    sh.version++;
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["api_key"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    if (n >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (size_t i = 0; i < n; i++) if ((unsigned char)v[i] <= ' ' || (unsigned char)v[i] > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.key, sizeof(save.key), "%.*s", (int)n, v);
    for (int i = 0; i < COINS; i++) save.id[i] = 0;
    clear_Read();
    sh.key = K_NONE; sh.planOk = false; sh.pause = P_OK; sh.noChart = sh.noMarket = sh.noFng = sh.noMovers = false; sh.problem[0] = '\0';
    portEXIT_CRITICAL(&lock);
    readNow = true;
    phoneChanged = true;
}
static void cmd_Watch(JsonDocument& c){
    const char* v = c["watch"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[160]; snprintf(t, sizeof(t), "%s", v);
    char list[COINS][12]; int n = 0;
    for (char* w = strtok(t, " ,;"); w; w = strtok(nullptr, " ,;")){
        const size_t len = strlen(w);
        if (n >= COINS || len > 10){ mtb_Ble_App_Cmd_Reject(); return; }
        for (size_t i = 0; i < len; i++){ if (!isalnum((unsigned char)w[i])){ mtb_Ble_App_Cmd_Reject(); return; } w[i] = (char)toupper((unsigned char)w[i]); }
        bool dup = false;
        for (int k = 0; k < n; k++) if (!strcmp(list[k], w)) dup = true;
        if (!dup) snprintf(list[n++], sizeof(list[0]), "%s", w);
    }
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    memcpy(save.watch, list, sizeof(list));
    save.watchN = (uint8_t)n;
    for (int i = 0; i < COINS; i++) save.id[i] = 0;
    clear_Read();
    portEXIT_CRITICAL(&lock);
    readNow = true;
    phoneChanged = true;
}
static void cmd_Convert(JsonDocument& c){
    const char* v = c["convert"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[16]; snprintf(t, sizeof(t), "%s", v);
    char* p = t; while (*p == ' ') p++;
    size_t n = strlen(p); while (n && p[n - 1] == ' ') p[--n] = '\0';
    if (n < 3 || n > 5){ mtb_Ble_App_Cmd_Reject(); return; }
    for (size_t i = 0; i < n; i++){ if (!isalpha((unsigned char)p[i])){ mtb_Ble_App_Cmd_Reject(); return; } p[i] = (char)toupper((unsigned char)p[i]); }
    portENTER_CRITICAL(&lock);
    snprintf(save.convert, sizeof(save.convert), "%s", p);
    clear_Read();
    portEXIT_CRITICAL(&lock);
    readNow = true;
    phoneChanged = true;
}
static void cmd_Share(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["share"], 10, 100, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.share = (uint8_t)v; phoneChanged = true; }
static void cmd_Seconds(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seconds"], 5, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.seconds = (uint8_t)v; phoneChanged = true; }
static void cmd_Market(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["market"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.market = (uint8_t)v; phoneChanged = true; }
static void cmd_Movers(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["movers"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.movers = (uint8_t)v; phoneChanged = true; }
static void cmd_Read(JsonDocument&){ if (!appOpen || !save.key[0]){ mtb_Ble_App_Cmd_Reject(); return; } readNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Asking CoinMarketCap ------------------------------------------------------------------------------------------------------------
/** A reader for deserializeJson over a body in memory that lets the parse wait its turn every 2 KB. */
struct Cmc_Reader_t {
    const char* p; const char* end; size_t sinceBreath = 0;
    int read(){ if (p >= end) return -1; if (++sinceBreath >= 2048){ sinceBreath = 0; pxl_compat_breathe(); } return (uint8_t)*p++; }
    size_t readBytes(char* out, size_t n){ size_t k = 0; while (k < n && p < end){ out[k++] = *p++; if (++sinceBreath >= 2048){ sinceBreath = 0; pxl_compat_breathe(); } } return k; }
};

enum Cmc_Result_t : uint8_t { R_OK, R_NO_ANSWER, R_NOT_ALLOWED, R_KEY, R_PLAN, R_MINUTE, R_DAY, R_MONTH, R_MISSING, R_BAD };

/** One request: the JSON answer in @p doc, the credits it cost in @p credits. */
static Cmc_Result_t cmc_Ask(const char* path, const char* key, JsonDocument& doc, int& credits, int& http){
    char url[400];
    snprintf(url, sizeof(url), "%s%s", CMC_BASE, path);
    const char* headers[] = {"X-CMC_PRO_API_KEY", key, "Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, BODY_MAX, r, UA);
    http = r.code;
    credits = 0;
    doc.clear();
    if (r.code == PXL_HTTP_NOT_ALLOWED){ heap_caps_free(body); return R_NOT_ALLOWED; }
    if (r.code <= 0){ heap_caps_free(body); return R_NO_ANSWER; }
    bool parsed = false;
    if (body && !r.truncated){ Cmc_Reader_t rd{body, body + r.length}; parsed = deserializeJson(doc, rd) == DeserializationError::Ok; }
    heap_caps_free(body);
    int code = 0;
    if (parsed){
        JsonVariantConst st = doc["status"];
        JsonVariantConst ec = st["error_code"];
        code = ec.is<const char*>() ? atoi(ec.as<const char*>()) : ec.as<int>();
        credits = st["credit_count"] | 0;
    }
    if (code == 1001 || code == 1002 || code == 1003 || code == 1004 || code == 1005 || code == 1007 || r.code == 401) return R_KEY;
    if (code == 1006) return R_PLAN;
    if (code == 1008 || code == 1011) return R_MINUTE;
    if (code == 1009) return R_DAY;
    if (code == 1010) return R_MONTH;
    if (r.code == 429) return R_MINUTE;
    if (r.code == 404) return R_MISSING;
    if (r.code == 403 || r.code == 402) return R_PLAN;
    if (r.code < 200 || r.code >= 300 || !parsed || !doc["data"]) return R_BAD;
    return R_OK;
}

/** A coin's quote in the currency asked for: {"USD": {...}} (v1, v2) or [{"symbol": "USD", ...}] (v3). */
static JsonVariantConst quote_Of(JsonVariantConst coin, const char* convert){
    JsonVariantConst q = coin["quote"];
    if (q.is<JsonObjectConst>()){
        if (q[convert].is<JsonObjectConst>()) return q[convert];
        for (JsonPairConst p : q.as<JsonObjectConst>()) return p.value();
    } else if (q.is<JsonArrayConst>()){
        for (JsonVariantConst e : q.as<JsonArrayConst>()){ const char* s = e["symbol"] | ""; if (!strcmp(s, convert)) return e; }
        return q[0];
    }
    return JsonVariantConst();
}

/** Calls @p f on each coin of an answer's data: an array, or an object of coins (or of arrays of coins) by id or symbol. */
template <typename F> static void each_Coin(JsonVariantConst data, F f){
    if (data.is<JsonArrayConst>()){ for (JsonVariantConst c : data.as<JsonArrayConst>()) f(c); return; }
    if (!data.is<JsonObjectConst>()) return;
    if (!data["id"].isNull()){ f(data); return; }
    for (JsonPairConst p : data.as<JsonObjectConst>()){
        JsonVariantConst v = p.value();
        if (v.is<JsonArrayConst>()){ for (JsonVariantConst c : v.as<JsonArrayConst>()) f(c); }
        else if (v.is<JsonObjectConst>()) f(v);
    }
}

static int64_t iso_Utc(const char* s){
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) < 3) return 0;
    return (int64_t)mtb_Kit_Days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}
static void iso_Text(int64_t t, char* out, size_t n){
    time_t x = (time_t)t; struct tm g; gmtime_r(&x, &g);
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
}

// The worker: the plan, the pace, the requests -----------------------------------------------------------------------------------------
#define QUOTE_MIN_S     30u
#define QUOTE_MAX_S     3600u
#define PLAN_EVERY_S    600u
#define MARKET_EVERY_S  900u
#define FNG_EVERY_S     1800u
#define MOVERS_EVERY_S  1800u
#define CHART_EVERY_S   10800u

static JsonDocument* doc = nullptr;
static bool v3Quotes = true, v3Listings = true, v3History = true;

/** Takes a request's result into the shared state (under the lock). True if the request should be counted as done. */
static bool settle(Cmc_Result_t r, int credits, int http, const char* what){
    const uint32_t now = now_S();
    if (credits > 0){ sh.bucket -= credits; if (sh.left >= (uint32_t)credits) sh.left -= credits; }
    switch (r){
        case R_OK: sh.key = K_OK; sh.pause = P_OK; sh.problem[0] = '\0'; return true;
        case R_NO_ANSWER: sh.key = sh.key == K_NONE ? K_NO_ANSWER : sh.key; snprintf(sh.problem, sizeof(sh.problem), "COINMARKETCAP DIDN'T ANSWER"); return false;
        case R_NOT_ALLOWED: sh.key = K_NOT_ALLOWED; snprintf(sh.problem, sizeof(sh.problem), "NOT ALLOWED TO REACH COINMARKETCAP"); return false;
        case R_KEY: sh.key = K_BAD; snprintf(sh.problem, sizeof(sh.problem), "API KEY REFUSED: CHECK IT ON THE PHONE"); return false;
        case R_MINUTE: sh.pause = P_MINUTE; sh.pauseUntil = now + 65; snprintf(sh.problem, sizeof(sh.problem), "TOO MANY CALLS A MINUTE: WAITING"); return false;
        case R_DAY: sh.pause = P_DAY; sh.pauseUntil = (now / 86400 + 1) * 86400 + 60; snprintf(sh.problem, sizeof(sh.problem), "TODAY'S CREDITS USED: BACK TOMORROW"); return false;
        case R_MONTH: sh.pause = P_MONTH; sh.pauseUntil = sh.renews > now ? (uint32_t)sh.renews + 60 : now + 6 * 3600; snprintf(sh.problem, sizeof(sh.problem), "THIS MONTH'S CREDITS ARE USED UP"); return false;
        case R_PLAN: return true;      // the caller notes what the plan leaves out
        default: snprintf(sh.problem, sizeof(sh.problem), "%s: ANSWER %d", what, http); return false;
    }
}

static void ask_Plan(const Cmc_Save_t& s){
    int credits, http;
    const Cmc_Result_t r = cmc_Ask("/v1/key/info", s.key, *doc, credits, http);
    portENTER_CRITICAL(&lock);
    settle(r, credits, http, "PLAN");
    if (r == R_OK){
        JsonVariantConst plan = (*doc)["data"]["plan"], use = (*doc)["data"]["usage"];
        sh.monthly = plan["credit_limit_monthly"] | 0u;
        sh.perMinute = plan["rate_limit_minute"] | 0;
        sh.renews = iso_Utc(plan["credit_limit_monthly_reset_timestamp"] | "");
        sh.left = use["current_month"]["credits_left"] | sh.monthly;
        sh.usedToday = use["current_day"]["credits_used"] | 0u;
        sh.planOk = true;
        sh.planAt = now_S();
        if (sh.pause == P_MONTH && sh.left > 0) sh.pause = P_OK;
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

/** The watchlist's ids: for each symbol, the coin with the best rank. */
static void ask_Ids(Cmc_Save_t& s){
    char path[200] = "/v1/cryptocurrency/map?symbol=";
    for (int i = 0; i < s.watchN; i++){ if (i) strlcat(path, ",", sizeof(path)); strlcat(path, s.watch[i], sizeof(path)); }
    int credits, http;
    const Cmc_Result_t r = cmc_Ask(path, s.key, *doc, credits, http);
    int32_t id[COINS]; int best[COINS];
    for (int i = 0; i < COINS; i++){ id[i] = -1; best[i] = 1 << 30; }
    if (r == R_OK) each_Coin((*doc)["data"], [&](JsonVariantConst c){
        const char* sym = c["symbol"] | "";
        const int rank = c["rank"].isNull() ? (1 << 29) : (c["rank"] | (1 << 29));
        if ((c["is_active"] | 1) == 0) return;
        for (int i = 0; i < s.watchN; i++) if (!strcasecmp(sym, s.watch[i]) && rank < best[i]){ best[i] = rank; id[i] = c["id"] | -1; }
    });
    portENTER_CRITICAL(&lock);
    settle(r, credits, http, "COIN LIST");
    if (r == R_OK && !memcmp(save.watch, s.watch, sizeof(s.watch))){
        for (int i = 0; i < s.watchN; i++) save.id[i] = id[i];
        sh.idsOk = true;
        phoneChanged = true;           // the ids are kept
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

static void ask_Prices(const Cmc_Save_t& s){
    char ids[160] = "";
    for (int i = 0; i < s.watchN; i++) if (s.id[i] > 0){ char one[16]; snprintf(one, sizeof(one), "%s%ld", ids[0] ? "," : "", (long)s.id[i]); strlcat(ids, one, sizeof(ids)); }
    if (!ids[0]) return;
    Cmc_Coin_t got[COINS] = {};
    int credits = 0, http = 0;
    Cmc_Result_t r = R_MISSING;
    for (int attempt = 0; attempt < 2 && r == R_MISSING; attempt++){
        char path[260];
        snprintf(path, sizeof(path), "/%s/cryptocurrency/quotes/latest?id=%s&convert=%s", v3Quotes ? "v3" : "v2", ids, s.convert);
        r = cmc_Ask(path, s.key, *doc, credits, http);
        if (r == R_MISSING && v3Quotes) v3Quotes = false; else break;
    }
    if (r == R_OK) each_Coin((*doc)["data"], [&](JsonVariantConst c){
        const long id = c["id"] | 0L;
        for (int i = 0; i < s.watchN; i++){
            if (s.id[i] != id) continue;
            JsonVariantConst q = quote_Of(c, s.convert);
            Cmc_Coin_t& g = got[i];
            char nm[48]; mtb_Kit_To_Ascii(nm, sizeof(nm), c["name"] | "");
            snprintf(g.name, sizeof(g.name), "%s", nm);
            g.rank = c["cmc_rank"] | 0;
            g.price = q["price"] | 0.0; g.mcap = q["market_cap"] | 0.0; g.vol = q["volume_24h"] | 0.0;
            static const char* const CH[] = {"percent_change_1h", "percent_change_24h", "percent_change_7d", "percent_change_30d", "percent_change_90d"};
            for (int k = 0; k < 5; k++) g.ch[k] = q[CH[k]] | 0.0;
            g.ok = !q.isNull();
        }
    });
    portENTER_CRITICAL(&lock);
    if (settle(r, credits, http, "PRICES")) sh.pricesAt = now_S();
    if (r == R_OK && !memcmp(save.id, s.id, sizeof(s.id)) && !strcmp(save.convert, s.convert)){
        for (int i = 0; i < s.watchN; i++){
            if (!got[i].ok) continue;
            memcpy(got[i].chart, sh.coin[i].chart, sizeof(got[i].chart));
            got[i].chartN = sh.coin[i].chartN; got[i].chartAt = sh.coin[i].chartAt;
            sh.coin[i] = got[i];
        }
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

static void ask_Market(const Cmc_Save_t& s){
    char path[120]; snprintf(path, sizeof(path), "/v1/global-metrics/quotes/latest?convert=%s", s.convert);
    int credits, http;
    const Cmc_Result_t r = cmc_Ask(path, s.key, *doc, credits, http);
    portENTER_CRITICAL(&lock);
    if (settle(r, credits, http, "MARKET")) sh.marketAt = now_S();
    if (r == R_PLAN) sh.noMarket = true;
    if (r == R_OK){
        JsonVariantConst d = (*doc)["data"];
        JsonVariantConst q = quote_Of(d, s.convert);
        sh.totalCap = q["total_market_cap"] | 0.0; sh.totalVol = q["total_volume_24h"] | 0.0;
        sh.btcDom = d["btc_dominance"] | 0.0; sh.ethDom = d["eth_dominance"] | 0.0;
        sh.marketOk = sh.totalCap > 0;
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

static void ask_Fng(const Cmc_Save_t& s){
    int credits, http;
    const Cmc_Result_t r = cmc_Ask("/v3/fear-and-greed/latest", s.key, *doc, credits, http);
    portENTER_CRITICAL(&lock);
    if (settle(r, credits, http, "FEAR & GREED")) sh.fngAt = now_S();
    if (r == R_PLAN || r == R_MISSING){ sh.noFng = true; sh.fngAt = now_S(); if (r == R_MISSING) sh.problem[0] = '\0'; }
    if (r == R_OK){
        JsonVariantConst d = (*doc)["data"];
        sh.fng = d["value"] | -1;
        snprintf(sh.fngText, sizeof(sh.fngText), "%s", d["value_classification"] | "");
        sh.fngOk = sh.fng >= 0 && sh.fng <= 100;
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

/** The day's biggest rises and falls among the 50 most valued coins: one request on every plan. */
static void ask_Movers(const Cmc_Save_t& s){
    Cmc_Mover_t all[50]; int n = 0;
    int credits = 0, http = 0;
    Cmc_Result_t r = R_MISSING;
    for (int attempt = 0; attempt < 2 && r == R_MISSING; attempt++){
        char path[160];
        snprintf(path, sizeof(path), "/%s/cryptocurrency/listings/latest?start=1&limit=50&convert=%s&aux=cmc_rank", v3Listings ? "v3" : "v1", s.convert);
        r = cmc_Ask(path, s.key, *doc, credits, http);
        if (r == R_MISSING && v3Listings) v3Listings = false; else break;
    }
    if (r == R_OK) each_Coin((*doc)["data"], [&](JsonVariantConst c){
        if (n >= 50) return;
        JsonVariantConst q = quote_Of(c, s.convert);
        if (q["percent_change_24h"].isNull()) return;
        snprintf(all[n].sym, sizeof(all[n].sym), "%s", c["symbol"] | "?");
        all[n].ch = q["percent_change_24h"] | 0.0;
        n++;
    });
    // The five highest and five lowest.
    Cmc_Mover_t up[MOVERS], down[MOVERS]; int upN = 0, downN = 0;
    bool used[50] = {};
    for (int k = 0; k < MOVERS; k++){
        int hi = -1, lo = -1;
        for (int i = 0; i < n; i++){ if (used[i]) continue; if (all[i].ch > 0 && (hi < 0 || all[i].ch > all[hi].ch)) hi = i; }
        if (hi >= 0){ used[hi] = true; up[upN++] = all[hi]; }
        for (int i = 0; i < n; i++){ if (used[i]) continue; if (all[i].ch < 0 && (lo < 0 || all[i].ch < all[lo].ch)) lo = i; }
        if (lo >= 0){ used[lo] = true; down[downN++] = all[lo]; }
    }
    portENTER_CRITICAL(&lock);
    if (settle(r, credits, http, "MOVERS")) sh.moversAt = now_S();
    if (r == R_PLAN) sh.noMovers = true;
    if (r == R_OK){ memcpy(sh.up, up, sizeof(up)); memcpy(sh.down, down, sizeof(down)); sh.upN = (uint8_t)upN; sh.downN = (uint8_t)downN; sh.moversOk = n > 0; }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

/** The shown coin's last 7 days, every 4 hours (42 points: 1 credit), where the plan has price history. */
static void ask_Chart(const Cmc_Save_t& s, int i){
    const uint32_t now = now_S();
    char from[24], to[24];
    iso_Text((int64_t)now - 7 * 86400, from, sizeof(from)); iso_Text(now, to, sizeof(to));
    float pts[CHART]; int n = 0;
    int credits = 0, http = 0;
    Cmc_Result_t r = R_MISSING;
    for (int attempt = 0; attempt < 2 && r == R_MISSING; attempt++){
        char path[260];
        snprintf(path, sizeof(path), "/%s/cryptocurrency/quotes/historical?id=%ld&time_start=%s&time_end=%s&interval=4h&count=%d&convert=%s",
                 v3History ? "v3" : "v2", (long)s.id[i], from, to, CHART, s.convert);
        r = cmc_Ask(path, s.key, *doc, credits, http);
        if (r == R_MISSING && v3History) v3History = false; else break;
    }
    if (r == R_OK){
        JsonVariantConst d = (*doc)["data"];
        JsonVariantConst quotes = d["quotes"];
        if (quotes.isNull()) each_Coin(d, [&](JsonVariantConst c){ if (quotes.isNull() && c["quotes"].is<JsonArrayConst>()) quotes = c["quotes"]; });
        if (quotes.isNull() && d.is<JsonObjectConst>())
            for (JsonPairConst p : d.as<JsonObjectConst>()) if (p.value()["quotes"].is<JsonArrayConst>()){ quotes = p.value()["quotes"]; break; }
        for (JsonVariantConst q : quotes.as<JsonArrayConst>()){
            if (n >= CHART) break;
            JsonVariantConst v = quote_Of(q, s.convert);
            const double p = v["price"] | 0.0;
            if (p > 0) pts[n++] = (float)p;
        }
    }
    portENTER_CRITICAL(&lock);
    settle(r, credits, http, "CHART");
    if (r == R_PLAN) sh.noChart = true;
    if (s.id[i] == save.id[i]){
        sh.coin[i].chartAt = now;                                            // tried: not again for a while, either way
        if (r == R_OK && n >= 2){ memcpy(sh.coin[i].chart, pts, sizeof(float) * n); sh.coin[i].chartN = (uint8_t)n; }
    }
    sh.version++;
    portEXIT_CRITICAL(&lock);
}

static void worker(void*){
    doc = new JsonDocument();
    Cmc_Save_t* s = (Cmc_Save_t*)heap_caps_malloc(sizeof(Cmc_Save_t), MALLOC_CAP_SPIRAM);
    uint32_t lastTick = now_S(), lastCall = 0, holdUntil = 0;
    int failStreak = 0;
    bool started = false, forcePlan = false, forcePrices = false;
    // After each request: a problem holds the next one back, longer each time (15 s to 10 minutes).
    auto after = [&](uint32_t now){
        lastCall = now;
        portENTER_CRITICAL(&lock); const bool bad = sh.problem[0] && sh.pause == P_OK; portEXIT_CRITICAL(&lock);
        if (bad){ failStreak = failStreak < 6 ? failStreak + 1 : 6; const uint32_t wait = 15u << failStreak; holdUntil = now + (wait < 600 ? wait : 600); }
        else failStreak = 0;
    };
    while (s && doc && !workerStop){
        vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t now = now_S();
        if (now < 1700000000u) continue;                                        // no time from the internet yet
        portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
        if (!s->key[0]) continue;
        if (readNow){ readNow = false; forcePlan = forcePrices = true; holdUntil = 0; }
        if ((int32_t)(holdUntil - now) > 0 || now - lastCall < 2) continue;      // never more than one call in two seconds
        // The plan: first, every 10 minutes, and when asked.
        if (!sh.planOk || now - sh.planAt >= PLAN_EVERY_S || forcePlan){
            if (sh.pause == P_MINUTE && (int32_t)(sh.pauseUntil - now) > 0) continue;
            forcePlan = false;
            ask_Plan(*s);
            after(now);
            continue;
        }
        // The pace: the share of what is left, spread over the time to the renewal.
        portENTER_CRITICAL(&lock);
        const int64_t span = sh.renews > now ? sh.renews - now : 30 * 86400;
        sh.rate = (float)((double)sh.left * s->share / 100.0 / (double)(span > 3600 ? span : 3600));
        if (!started){ started = true; sh.bucket = sh.left < 8 ? (float)sh.left : 8.0f; }       // a start: prices, the chart, the market, the movers
        sh.bucket += sh.rate * (float)(now - lastTick);
        const float cap = sh.rate * 1800.0f > 8.0f ? sh.rate * 1800.0f : 8.0f;
        if (sh.bucket > cap) sh.bucket = cap;
        if (sh.bucket > (float)sh.left) sh.bucket = (float)sh.left;
        const float priceRate = sh.rate * 0.6f;
        uint32_t every = priceRate > 0 ? (uint32_t)(1.0f / priceRate) : QUOTE_MAX_S;
        if (every < QUOTE_MIN_S) every = QUOTE_MIN_S;
        if (every > QUOTE_MAX_S) every = QUOTE_MAX_S;
        sh.quoteEvery = every;
        const bool paused = sh.pause != P_OK && (int32_t)(sh.pauseUntil - now) > 0;
        const float bucket = sh.bucket;
        const bool idsOk = sh.idsOk;
        const uint32_t pricesAt = sh.pricesAt, marketAt = sh.marketAt, fngAt = sh.fngAt, moversAt = sh.moversAt;
        const bool noMarket = sh.noMarket, noFng = sh.noFng, noMovers = sh.noMovers, noChart = sh.noChart;
        const int shown = shownCoin;
        const uint32_t chartAt = shown >= 0 && shown < COINS ? sh.coin[shown].chartAt : now;
        if (!paused && sh.pause != P_OK){ sh.pause = P_OK; sh.problem[0] = '\0'; }
        portEXIT_CRITICAL(&lock);
        lastTick = now;
        if (paused) continue;
        bool needIds = !idsOk;
        for (int i = 0; i < s->watchN; i++) if (!s->id[i]) needIds = true;
        if (needIds && bucket >= 1){ ask_Ids(*s); after(now); continue; }
        if (bucket >= 1 && (forcePrices || !pricesAt || now - pricesAt >= every)){ forcePrices = false; ask_Prices(*s); after(now); continue; }
        if (bucket < 2) continue;                                                // keep one for the next prices
        if (!noChart && shown >= 0 && shown < s->watchN && s->id[shown] > 0 && (!chartAt || now - chartAt >= CHART_EVERY_S)){ ask_Chart(*s, shown); after(now); continue; }
        if (s->market && !noMarket && (!marketAt || now - marketAt >= MARKET_EVERY_S)){ ask_Market(*s); after(now); continue; }
        if (s->market && !noFng && (!fngAt || now - fngAt >= FNG_EVERY_S)){ ask_Fng(*s); after(now); continue; }
        if (s->movers && !noMovers && (!moversAt || now - moversAt >= MOVERS_EVERY_S)){ ask_Movers(*s); after(now); continue; }
    }
    delete doc; doc = nullptr;
    heap_caps_free(s);
    workerRunning = false;
    vTaskDelete(nullptr);
}

// Numbers ------------------------------------------------------------------------------------------------------------------------------
static void commas(double v, int decimals, char* out, size_t n){
    char d[40]; snprintf(d, sizeof(d), "%.*f", decimals, v);
    const char* dot = strchr(d, '.');
    const size_t whole = dot ? (size_t)(dot - d) : strlen(d);
    size_t k = 0;
    for (size_t i = 0; i < whole && k + 1 < n; i++){
        out[k++] = d[i];
        const size_t left = whole - 1 - i;
        if (left && left % 3 == 0 && k + 1 < n) out[k++] = ',';
    }
    if (dot) for (const char* p = dot; *p && k + 1 < n; p++) out[k++] = *p;
    out[k] = '\0';
}
static void price_Text(double v, char* out, size_t n){
    if (v >= 100000) commas(v, 0, out, n);
    else if (v >= 1) commas(v, 2, out, n);
    else if (v > 0){ int dec = (int)-floor(log10(v)) + 3; if (dec > 10) dec = 10; snprintf(out, n, "%.*f", dec, v); }
    else snprintf(out, n, "-");
}
static void short_Text(double v, char* out, size_t n){
    static const char* const U[] = {"", "K", "M", "B", "T"};
    int u = 0;
    while (v >= 1000 && u < 4){ v /= 1000; u++; }
    snprintf(out, n, v >= 100 ? "%.0f%s" : v >= 10 ? "%.1f%s" : "%.2f%s", v, U[u]);
}
static void change_Text(double v, char* out, size_t n){ snprintf(out, n, fabs(v) >= 100 ? "%+.0f%%" : fabs(v) >= 10 ? "%+.1f%%" : "%+.2f%%", v); }
static void every_Text(uint32_t s, char* out, size_t n){
    if (s < 90) snprintf(out, n, "%us", (unsigned)s);
    else if (s < 5400) snprintf(out, n, "%u min", (unsigned)((s + 30) / 60));
    else snprintf(out, n, "%.1f h", s / 3600.0);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 10, 20), C_HEAD = MTB_KIT_RGB(16, 30, 64), C_PANEL = MTB_KIT_RGB(12, 20, 38);
static const uint16_t C_UP = MTB_KIT_RGB(22, 199, 132), C_DOWN = MTB_KIT_RGB(234, 57, 67), C_BLUE = MTB_KIT_RGB(80, 140, 255);
static uint16_t change_Color(double v){ return v > 0 ? C_UP : v < 0 ? C_DOWN : KIT_LABEL; }

static void header(const char* left, uint16_t color){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    char fit[64]; mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, left, W - 44);
    mtb_Kit_Text(3, 2, Terminal6x8, color, fit, C_HEAD);
    pxl_time_t pt;
    if (pxl_time(&pt)){ char t[8]; snprintf(t, sizeof(t), "%02d:%02d", (int)pt.hour, (int)pt.minute); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD); }
}

/** The bottom line: a problem, else the pace. */
static void footer(const Cmc_Shared_t& d, const Cmc_Save_t& s, int page, int pages){
    char t[96], a[24], fit[96];
    mtb_Panel_Fill_Rect(0, H - 9, W - 1, H - 1, C_HEAD);
    snprintf(a, sizeof(a), "%d/%d", page + 1, pages);
    mtb_Kit_Text_Right(W - 3, H - 7, Terminal4x6, KIT_LABEL, a, C_HEAD);
    uint16_t color = KIT_LABEL;
    if (d.problem[0]){ snprintf(t, sizeof(t), "%s", d.problem); color = KIT_ORANGE; }
    else if (d.planOk){
        every_Text(d.quoteEvery, a, sizeof(a));
        char left[24]; commas(d.left, 0, left, sizeof(left));
        snprintf(t, sizeof(t), "PRICES EVERY %s  -  %s CREDITS LEFT THIS MONTH", a, left);
        mtb_Kit_Upper(t);
    } else snprintf(t, sizeof(t), "READING YOUR PLAN");
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - 30);
    mtb_Kit_Text(3, H - 7, Terminal4x6, color, fit, C_HEAD);
    (void)s;
}

static void draw_Message(const char* big, const char* small){
    header("COINMARKETCAP", KIT_TEXT);
    mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, big, C_BG);
    mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, small, C_BG);
}

static void draw_Coin(const Cmc_Shared_t& d, const Cmc_Save_t& s, int i){
    const Cmc_Coin_t& c = d.coin[i];
    char t[64], a[32];
    if (c.rank) snprintf(t, sizeof(t), "#%d  %s", c.rank, c.name); else snprintf(t, sizeof(t), "%s", c.name[0] ? c.name : s.watch[i]);
    mtb_Kit_Upper(t);
    header(t, KIT_TEXT);
    mtb_Kit_Text(4, 15, PT_Sans_Narrow_Bold18x22, KIT_GOLD, s.watch[i], C_BG);
    if (s.id[i] < 0){
        mtb_Kit_Text_Right(W - 4, 20, Terminal6x8, KIT_ORANGE, "NOT ON COINMARKETCAP", C_BG);
        return;
    }
    if (!c.ok){ mtb_Kit_Text_Right(W - 4, 20, Terminal6x8, KIT_LABEL, "READING...", C_BG); return; }
    snprintf(t, sizeof(t), "IN %s", s.convert);
    mtb_Kit_Text(4, 39, Terminal4x6, KIT_LABEL, t, C_BG);
    price_Text(c.price, t, sizeof(t));
    const uint8_t* pf = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, t) <= W - 70 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
    mtb_Kit_Text_Right(W - 4, (int16_t)(pf == PT_Sans_Narrow_Bold28x34 ? 12 : 18), pf, KIT_TEXT, t, C_BG);
    // The changes: five columns, each its time over its change.
    static const char* const L[] = {"1 HOUR", "24 HOURS", "7 DAYS", "30 DAYS", "90 DAYS"};
    const int cw = (W - 8) / 5;
    for (int k = 0; k < 5; k++){
        const int mid = 4 + k * cw + cw / 2;
        mtb_Kit_Text_Centre((int16_t)mid, 46, Terminal4x6, KIT_LABEL, L[k], C_BG);
        change_Text(c.ch[k], a, sizeof(a));
        mtb_Kit_Text_Centre((int16_t)mid, 53, Terminal6x8, change_Color(c.ch[k]), a, C_BG);
    }
    // The chart, or the changes as bars under their columns.
    const int cy = 63, ch = 38, cx = 4, cwid = W - 8;
    mtb_Panel_Fill_Rect(cx, cy, (int16_t)(cx + cwid - 1), (int16_t)(cy + ch - 1), C_PANEL);
    if (c.chartN >= 2){
        float lo = c.chart[0], hi = c.chart[0];
        for (int k = 1; k < c.chartN; k++){ if (c.chart[k] < lo) lo = c.chart[k]; if (c.chart[k] > hi) hi = c.chart[k]; }
        const uint16_t col = c.chart[c.chartN - 1] >= c.chart[0] ? C_UP : C_DOWN;
        mtb_Kit_Sparkline((int16_t)(cx + 2), (int16_t)(cy + 8), (int16_t)(cwid - 4), (int16_t)(ch - 10), c.chart, c.chartN, col, C_PANEL);
        mtb_Kit_Text((int16_t)(cx + 2), (int16_t)(cy + 1), Terminal4x6, KIT_LABEL, "7 DAYS", C_PANEL);
        char hl[48], h1[20], l1[20]; price_Text(hi, h1, sizeof(h1)); price_Text(lo, l1, sizeof(l1));
        snprintf(hl, sizeof(hl), "HIGH %s  LOW %s", h1, l1);
        mtb_Kit_Text_Right((int16_t)(cx + cwid - 3), (int16_t)(cy + 1), Terminal4x6, KIT_LABEL, hl, C_PANEL);
    } else {
        double m = 1;
        for (int k = 0; k < 5; k++) if (fabs(c.ch[k]) > m) m = fabs(c.ch[k]);
        const int mid = cy + ch / 2, half = ch / 2 - 3;
        mtb_Panel_Draw_HLine((int16_t)mid, (int16_t)(cx + 2), (int16_t)(cx + cwid - 3), KIT_DIM);
        for (int k = 0; k < 5; k++){
            const int bx = 4 + k * cw + cw / 2 - 8, h = (int)lround(fabs(c.ch[k]) / m * half);
            if (h > 0){
                if (c.ch[k] > 0) mtb_Panel_Fill_Rect((int16_t)bx, (int16_t)(mid - h), (int16_t)(bx + 15), (int16_t)(mid - 1), C_UP);
                else mtb_Panel_Fill_Rect((int16_t)bx, (int16_t)(mid + 1), (int16_t)(bx + 15), (int16_t)(mid + h), C_DOWN);
            }
        }
        if (d.noChart) mtb_Kit_Text((int16_t)(cx + 2), (int16_t)(cy + 1), Terminal4x6, KIT_DIM, "NO PRICE HISTORY IN THIS PLAN", C_PANEL);
    }
    // Its value and the day's trading.
    char m1[16], v1[16];
    short_Text(c.mcap, m1, sizeof(m1)); short_Text(c.vol, v1, sizeof(v1));
    mtb_Kit_Text(4, 105, Terminal4x6, KIT_LABEL, "MARKET VALUE", C_BG);
    mtb_Kit_Text(4 + mtb_Kit_Text_Width(Terminal4x6, "MARKET VALUE") + 4, 104, Terminal6x8, KIT_TEXT, m1, C_BG);
    mtb_Kit_Text(132, 105, Terminal4x6, KIT_LABEL, "24H VOLUME", C_BG);
    mtb_Kit_Text(132 + mtb_Kit_Text_Width(Terminal4x6, "24H VOLUME") + 4, 104, Terminal6x8, KIT_TEXT, v1, C_BG);
}

static void dominance(int y, const char* label, double pct, uint16_t color){
    char t[24]; snprintf(t, sizeof(t), "%s %.1f%%", label, pct);
    mtb_Kit_Text(4, (int16_t)y, Terminal4x6, KIT_LABEL, t, C_BG);
    mtb_Kit_Bar(4, (int16_t)(y + 7), 120, 4, (float)(pct / 100.0), color, C_PANEL);
}

static void draw_Market(const Cmc_Shared_t& d, const Cmc_Save_t& s){
    header("THE CRYPTO MARKET", KIT_TEXT);
    char t[48], v[24];
    if (!d.marketOk){ mtb_Kit_Text(4, 30, Terminal8x12, KIT_LABEL, d.noMarket ? "NOT IN THIS PLAN" : "READING...", C_BG); }
    else {
        snprintf(t, sizeof(t), "ALL CRYPTO, IN %s", s.convert);
        mtb_Kit_Text(4, 15, Terminal4x6, KIT_LABEL, t, C_BG);
        short_Text(d.totalCap, v, sizeof(v));
        mtb_Kit_Text(4, 22, PT_Sans_Narrow_Bold28x34, KIT_TEXT, v, C_BG);
        short_Text(d.totalVol, v, sizeof(v));
        snprintf(t, sizeof(t), "TRADED IN 24H: %s", v);
        mtb_Kit_Text(4, 59, Terminal6x8, KIT_LABEL, t, C_BG);
        dominance(72, "BITCOIN'S SHARE", d.btcDom, KIT_GOLD);
        dominance(90, "ETHER'S SHARE", d.ethDom, C_BLUE);
    }
    // Fear & Greed: a scale from red to green, and where the market is on it.
    const int gx = 140, gw = 110;
    mtb_Kit_Text(gx, 15, Terminal4x6, KIT_LABEL, "FEAR & GREED", C_BG);
    if (d.fngOk){
        static const uint16_t SEG[] = {MTB_KIT_RGB(234, 57, 67), MTB_KIT_RGB(245, 140, 50), MTB_KIT_RGB(240, 200, 40), MTB_KIT_RGB(150, 210, 60), MTB_KIT_RGB(22, 199, 132)};
        static const int EDGE[] = {0, 25, 45, 55, 75, 100};
        int seg = 0;
        for (int k = 0; k < 5; k++){
            mtb_Panel_Fill_Rect((int16_t)(gx + gw * EDGE[k] / 100), 62, (int16_t)(gx + gw * EDGE[k + 1] / 100 - 1), 68, SEG[k]);
            if (d.fng >= EDGE[k]) seg = k;
        }
        const int mx = gx + gw * d.fng / 100;
        mtb_Panel_Fill_Triangle((int16_t)(mx - 4), 55, (int16_t)(mx + 4), 55, (int16_t)mx, 60, KIT_TEXT);
        snprintf(v, sizeof(v), "%d", d.fng);
        mtb_Kit_Text(gx, 23, PT_Sans_Narrow_Bold28x34, SEG[seg], v, C_BG);
        snprintf(t, sizeof(t), "%s", d.fngText); mtb_Kit_Upper(t);
        mtb_Kit_Text((int16_t)(gx + mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, v) + 6), 38, Terminal6x8, SEG[seg], t, C_BG);
        mtb_Kit_Text(gx, 71, Terminal4x6, KIT_DIM, "FEAR", C_BG);
        mtb_Kit_Text_Right((int16_t)(gx + gw), 71, Terminal4x6, KIT_DIM, "GREED", C_BG);
    } else mtb_Kit_Text(gx, 30, Terminal6x8, KIT_LABEL, d.noFng ? "NOT IN THIS PLAN" : "READING...", C_BG);
}

static void draw_Movers(const Cmc_Shared_t& d){
    header("TOP 50: THE DAY'S BIGGEST MOVES", KIT_TEXT);
    if (!d.moversOk){ mtb_Kit_Text(4, 30, Terminal8x12, KIT_LABEL, d.noMovers ? "NOT IN THIS PLAN" : "READING...", C_BG); return; }
    char a[24];
    mtb_Kit_Text(6, 15, Terminal6x8, C_UP, "UP", C_BG);
    mtb_Kit_Text(134, 15, Terminal6x8, C_DOWN, "DOWN", C_BG);
    mtb_Panel_Draw_VLine(127, 14, 112, KIT_LINE);
    for (int k = 0; k < MOVERS; k++){
        const int y = 27 + k * 17;
        if (k < d.upN){
            mtb_Kit_Text(6, (int16_t)y, Terminal8x12, KIT_TEXT, d.up[k].sym, C_BG);
            change_Text(d.up[k].ch, a, sizeof(a));
            mtb_Kit_Text_Right(122, (int16_t)y, Terminal8x12, C_UP, a, C_BG);
        }
        if (k < d.downN){
            mtb_Kit_Text(134, (int16_t)y, Terminal8x12, KIT_TEXT, d.down[k].sym, C_BG);
            change_Text(d.down[k].ch, a, sizeof(a));
            mtb_Kit_Text_Right(W - 6, (int16_t)y, Terminal8x12, C_DOWN, a, C_BG);
        }
    }
}

/** The pages: each coin, then the market and the movers if chosen. */
static int page_Count(const Cmc_Save_t& s){ return s.watchN + (s.market ? 1 : 0) + (s.movers ? 1 : 0); }

static void draw(const Cmc_Shared_t& d, const Cmc_Save_t& s, int page, int pages){
    mtb_Panel_Fill_Screen(C_BG);
    if (!s.key[0]){ draw_Message("ADD YOUR API KEY", "IN THE PHONE APP: FROM PRO.COINMARKETCAP.COM"); return; }
    if (d.key == K_BAD){ draw_Message("KEY REFUSED", "CHECK YOUR COINMARKETCAP API KEY IN THE PHONE APP"); footer(d, s, 0, 1); return; }
    if (page < s.watchN) draw_Coin(d, s, page);
    else if (s.market && page == s.watchN) draw_Market(d, s);
    else draw_Movers(d);
    footer(d, s, page, pages);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void coinMarketCap_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Key, cmd_Watch, cmd_Convert, cmd_Share, cmd_Seconds, cmd_Market, cmd_Movers, cmd_Read);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    memset(&sh, 0, sizeof(sh));
    turns = 0; knobClick = false; phoneChanged = false; readNow = false; workerStop = false;
    v3Quotes = v3Listings = v3History = true;
    Cmc_Save_t* s = (Cmc_Save_t*)heap_caps_malloc(sizeof(Cmc_Save_t), MALLOC_CAP_SPIRAM);
    Cmc_Shared_t* d = (Cmc_Shared_t*)heap_caps_malloc(sizeof(Cmc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); mtb_Delete_This_App(THIS_APP); return; }
    workerRunning = true;
    if (xTaskCreatePinnedToCore(worker, "cmcNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) workerRunning = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, pageAt = millis(), handUntil = 0, version = 0xFFFFFFFF;
    int page = 0;
    bool push = true, saveNow = false, redraw = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = redraw = true; }
        const int t = turns;
        if (t){ turns -= t; page += t; pageAt = ms; handUntil = ms + 30000; redraw = true; }
        if (knobClick){ knobClick = false; readNow = true; }
        portENTER_CRITICAL(&lock);
        *s = save;
        if (sh.version != version){ version = sh.version; *d = sh; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        const int pages = page_Count(*s) ? page_Count(*s) : 1;
        if ((int32_t)(handUntil - ms) <= 0 && ms - pageAt >= s->seconds * 1000u){ pageAt = ms; page++; redraw = true; }
        page = ((page % pages) + pages) % pages;
        shownCoin = page < s->watchN ? page : -1;
        if (redraw || ms - lastDraw >= 5000){
            redraw = false; lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(*d, *s, page, pages);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    workerStop = true;
    for (int i = 0; i < 400 && workerRunning; i++) vTaskDelay(pdMS_TO_TICKS(50));
    portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
    mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s));
    heap_caps_free(s); heap_caps_free(d);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task): never the key ---------------------------------------------------------------------------------
static void cmc_Values(JsonObject values){
    Cmc_Save_t* s = (Cmc_Save_t*)heap_caps_malloc(sizeof(Cmc_Save_t), MALLOC_CAP_SPIRAM);
    Cmc_Shared_t* d = (Cmc_Shared_t*)heap_caps_malloc(sizeof(Cmc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    char t[160], a[32], b[32];
    values["api_key_set"] = s->key[0] ? 1 : 0;
    t[0] = '\0';
    for (int i = 0; i < s->watchN; i++){ if (i) strlcat(t, " ", sizeof(t)); strlcat(t, s->watch[i], sizeof(t)); }
    values["watch"] = t;
    values["convert"] = s->convert;
    values["share"] = s->share;
    values["seconds"] = s->seconds;
    values["market"] = s->market;
    values["movers"] = s->movers;
    JsonArray rows = values["cmc"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = "API key";
    if (!s->key[0]){ r["value"] = "None yet: add yours below"; r["state"] = "warn"; }
    else if (!appOpen){ r["value"] = "Saved on Pixlpal (checked while the app is open)"; r["state"] = "off"; }
    else if (d->key == K_BAD){ r["value"] = "CoinMarketCap refused it: check it"; r["state"] = "error"; }
    else if (d->key == K_NOT_ALLOWED){ r["value"] = "Pixlpal may not reach CoinMarketCap"; r["state"] = "error"; }
    else if (d->key == K_NO_ANSWER){ r["value"] = "CoinMarketCap didn't answer"; r["state"] = "error"; }
    else if (d->planOk){ r["value"] = "Working"; r["state"] = "ok"; }
    else { r["value"] = "Checking"; r["state"] = "info"; }
    if (appOpen && d->planOk){
        const char* name = d->monthly == 15000 ? "Basic (free)" : d->monthly == 150000 ? "Builder" : d->monthly == 450000 ? "Startup" :
                           d->monthly == 2000000 ? "Growth" : d->monthly == 5000000 ? "Professional" : nullptr;
        commas(d->monthly, 0, a, sizeof(a));
        if (name) snprintf(t, sizeof(t), "%s: %s credits a month", name, a); else snprintf(t, sizeof(t), "%s credits a month", a);
        r = rows.add<JsonObject>(); r["label"] = "Plan"; r["value"] = t; r["state"] = "info";
        commas(d->left, 0, a, sizeof(a));
        if (d->renews > 0){ time_t x = (time_t)d->renews; struct tm g; gmtime_r(&x, &g); static const char* const M[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; snprintf(b, sizeof(b), "; renews %d %s", g.tm_mday, M[g.tm_mon]); } else b[0] = '\0';
        snprintf(t, sizeof(t), "%s left this month%s", a, b);
        values["left"] = d->left; values["monthly"] = d->monthly;
        r = rows.add<JsonObject>(); r["label"] = "Credits"; r["value"] = t; r["state"] = d->left ? "ok" : "error";
        JsonObject meter = r["meter"].to<JsonObject>(); meter["value"] = "left"; meter["max"] = "monthly";
        every_Text(d->quoteEvery, a, sizeof(a));
        snprintf(t, sizeof(t), "Prices every %s, using %u%% of what is left", a, (unsigned)s->share);
        r = rows.add<JsonObject>(); r["label"] = "Pace"; r["value"] = t; r["state"] = "info";
        t[0] = '\0';
        if (d->noChart) strlcat(t, "7-day chart (shown as bars instead)", sizeof(t));
        if (d->noMarket){ if (t[0]) strlcat(t, ", ", sizeof(t)); strlcat(t, "market page", sizeof(t)); }
        if (d->noFng){ if (t[0]) strlcat(t, ", ", sizeof(t)); strlcat(t, "Fear & Greed", sizeof(t)); }
        if (d->noMovers){ if (t[0]) strlcat(t, ", ", sizeof(t)); strlcat(t, "movers", sizeof(t)); }
        if (t[0]){ r = rows.add<JsonObject>(); r["label"] = "Not in your plan"; r["value"] = t; r["state"] = "warn"; }
    }
    if (appOpen && d->problem[0]){
        snprintf(t, sizeof(t), "%c%s", d->problem[0], d->problem + 1);
        for (char* k = t + 1; *k; k++) *k = (char)tolower((unsigned char)*k);
        r = rows.add<JsonObject>(); r["label"] = "Now"; r["value"] = t; r["state"] = "warn";
    }
    for (int i = 0; appOpen && i < s->watchN; i++){
        if (s->id[i] < 0){ snprintf(t, sizeof(t), "%s: not found on CoinMarketCap", s->watch[i]); r = rows.add<JsonObject>(); r["label"] = "Watchlist"; r["value"] = t; r["state"] = "warn"; }
    }
    if (appOpen && d->pricesAt){ values["pricesAt"] = d->pricesAt; r = rows.add<JsonObject>(); r["label"] = "Prices read"; r["ago"] = "pricesAt"; }
    heap_caps_free(s); heap_caps_free(d);
}
