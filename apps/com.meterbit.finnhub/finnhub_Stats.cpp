#include "mtb_kit_body_reader.h"
#include "mtb_engine.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "mtb_github.h"
#include "mtb_text_scroll.h"
#include <time.h>
#include <math.h>
#include <FS.h>
#include <LittleFS.h>
#include "esp_timer.h"
#include "mtb_kit_keys.h"
#include "esp_heap_caps.h"
#include "mtb_kit_json.h"
#include "finnhub_Ui.h"
#include "fh_Symbol.h"
#include "fh_Mobile_Ui.h"

static const char TAG[] = "FINNHUB_STATS";

#define MAX_STOCKS 100

// Finnhub's free tier allows 60 calls per minute; one call at a time with this gap stays well below that.
#define FH_REQUEST_GAP_SEC      2
#define FH_READ_TIMEOUT_MS      6000
#define FH_QUOTE_SHOWN_SEC      5
#define FH_QUOTE_WATCH_SEC      30
#define FH_QUOTE_CLOSED_SEC     120
#define FH_MARKET_SEC           60
#define FH_RETRY_SEC            120
#define FH_DENIED_SEC           (6 * 3600)
#define FH_BACKOFF_RATE_SEC     30
#define FH_BACKOFF_KEY_SEC      60
#define FH_NO_INTERNET_FAILS    3
#define FH_BAD_BODY             (-1000)

#define FH_SETTINGS_MAGIC       0x46484E32UL    // "FHN2"
#define FH_NVS_KEY              "finnhubCfg"
#define FH_PLAN_RECHECK_SEC     (24 * 3600)

// Settings kept in NVS. (The old "stocksStat" record held Arduino Strings, whose saved bytes are pointers from an
// earlier boot, so it is not read back.)
struct Fh_Settings_t {
  uint32_t magic;
  char shown[FH_SYMBOL_LEN];        // The stock on screen.
  char apiToken[72];
  int16_t stockChangeInterval;      // Seconds per stock while cycling (10-600).
  uint8_t autoCycle;
};

EXT_RAM_BSS_ATTR Fh_Settings_t currentStocks;

// What Finnhub's answers say about the key, for the phone and the price targets.
enum Fh_Plan_t : uint8_t { FH_PLAN_UNKNOWN = 0, FH_PLAN_FREE, FH_PLAN_PREMIUM };
static volatile uint8_t fhPlan = FH_PLAN_UNKNOWN;
static uint32_t fhPlanCheckedSec = 0;
static volatile bool fhKeyRejected = false, fhKeyAccepted = false;
static volatile uint32_t fhLastAnswerUnix = 0;

EXT_RAM_BSS_ATTR TimerHandle_t stockChangeTimer_H = NULL;
EXT_RAM_BSS_ATTR TaskHandle_t finhubStats_Task_H = NULL;

int8_t stockIterate = 0;
EXT_RAM_BSS_ATTR String stockSymbols[MAX_STOCKS];
int stockCount = 0;
static const char stockSymbolsFilePath[] = "/stocks/dStocks.csv";

// Requests from the button, timer and BLE tasks. The app task acts on them, so only it touches the stock data.
static volatile uint8_t stockStepsRequested = 0;
static volatile uint8_t stockStepPersist = 0;
static volatile bool symbolRequested = false;
static volatile bool watchlistChanged = false;
static volatile bool autoCycleToggled = false;
static volatile bool cycleChanged = false;
static volatile int8_t pendingCycle = -1;          // From the phone: 1 on, 0 off, -1 unchanged.
static volatile int16_t pendingInterval = 0;       // From the phone: seconds, 0 unchanged.
static char requestedSymbol[FH_SYMBOL_LEN];

// One slot per symbol, allocated in PSRAM on first use and kept for the whole power-on session, so charts and
// fundamentals survive switching to another app and back. One spare for a symbol shown from outside the watchlist.
EXT_RAM_BSS_ATTR static Fh_Stock_t* stockSlots[MAX_STOCKS + 1];
EXT_RAM_BSS_ATTR static Fh_Stock_t* watchedStocks[MAX_STOCKS];
EXT_RAM_BSS_ATTR static Fh_Market_t usMarket;
static Fh_Stock_t* shownStock = nullptr;
static int shownIndex = -1;
static uint32_t marketDue = 0;
static uint32_t requestsBlockedUntil = 0;
static uint8_t networkFails = 0;
static char statusMessage[80] = "";

void finhubStats_App_Task(void *);
void readStockSymbols(const char *filename, String stockSymbols[], int &count, const int maxSymbols);

void buttonChangeDisplayStock(button_event_t button_Data);
void stockChange_TimerCallback(TimerHandle_t);

void showParticularStock(JsonDocument&);
void add_RemoveStockSymbol(JsonDocument&);
void setStockChangeInterval(JsonDocument&);
void saveAPI_key(JsonDocument&);
static void fh_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *finnhub_Stats_App;
Mtb_Applications* finnhub_Stats_App_GetInstance() {
    if (!finnhub_Stats_App){
        finnhub_Stats_App = new Mtb_Applications_StatusBar(finhubStats_App_Task, &finhubStats_Task_H, "Finnhub Stats", {4,0}, 10240);
        finnhub_Stats_App->mtb_App_Set_Mobile_UI(FH_UI_MANIFEST, FH_UI_API, nullptr, fh_Mobile_Ui_Values);
    }
    return finnhub_Stats_App;
}

MTB_REGISTER_APP(finnhub_Stats_App, 4, 0)

//##############################################################################################################
static uint32_t fh_Uptime_Sec(void){ return (uint32_t)(esp_timer_get_time() / 1000000); }

// Copies text from Finnhub to a plain ASCII string, the only characters the fonts have. Typographic
// punctuation is replaced with its ASCII look-alike and anything else that is not ASCII is dropped.
static void fh_Copy_Ascii(char* dest, size_t size, const char* src){
    size_t n = 0;
    if (src == nullptr) src = "";
    while (*src && n + 1 < size){
        const uint8_t c = (uint8_t)*src;
        if (c == 0xE2 && (uint8_t)src[1] == 0x80 && src[2]){
            const uint8_t third = (uint8_t)src[2];
            char mapped = 0;
            if (third == 0x98 || third == 0x99) mapped = '\'';
            else if (third == 0x9C || third == 0x9D) mapped = '"';
            else if (third == 0x93 || third == 0x94) mapped = '-';
            if (mapped) dest[n++] = mapped;
            src += 3;
            continue;
        }
        if (c >= ' ' && c <= '~') dest[n++] = (char)c;
        else if (c == '\n' || c == '\r' || c == '\t') dest[n++] = ' ';
        src++;
    }
    dest[n] = '\0';
}

// The response reader is the kit's (mtb_kit_body_reader.h): one shared type, so one copy of the JSON parser.

static float fh_Metric(JsonObjectConst metric, const char* key){
    JsonVariantConst value = metric[key];
    return value.is<float>() ? value.as<float>() : NAN;
}

// HTTP/1.0 keeps the response free of chunk markers, so it can be parsed straight from the stream.
static HTTPClient& fh_Http(void){
    static HTTPClient http;
    return http;
}

// Verifies the server against the CA bundle in the firmware. finnhub.io's chain ends in a cross-signed root, which
// needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY.
static WiFiClientSecure& fh_Tls_Client(void){
    static WiFiClientSecure client;
    static bool configured = false;
    if (!configured){
        client.useBuiltinCACertBundle();
        configured = true;
    }
    return client;
}

static int fh_Get(const char* pathAndQuery){
    char url[256];
    const char separator = strchr(pathAndQuery, '?') ? '&' : '?';
    snprintf(url, sizeof(url), "https://finnhub.io/api/v1/%s%ctoken=%s", pathAndQuery, separator, currentStocks.apiToken);
    HTTPClient& http = fh_Http();
    http.useHTTP10(true);
    http.setConnectTimeout(6000);
    http.setTimeout(FH_READ_TIMEOUT_MS);
    if (!http.begin(fh_Tls_Client(), url)) return -1;
    return http.GET();
}

//##############################################################################################################
// Adds a quote to the chart. Quote time only advances while the market trades, so a closed market adds nothing,
// and a gap longer than the chart (i.e. overnight) starts a new one.
static void fh_Add_History(Fh_Stock_t& stock){
    if (stock.quoteTime <= 0) return;
    const uint32_t bucket = (uint32_t)(stock.quoteTime / FH_HISTORY_SEC);
    if (stock.historyCount > 0 && bucket <= stock.historyBucket){
        if (bucket == stock.historyBucket) stock.history[stock.historyCount - 1] = (float)stock.price;
        return;
    }
    const uint32_t gap = (stock.historyCount > 0) ? bucket - stock.historyBucket : 1;
    if (gap > FH_HISTORY_POINTS) stock.historyCount = 0;
    const uint32_t blanks = (stock.historyCount > 0 && gap > 1) ? gap - 1 : 0;
    for (uint32_t i = 0; i <= blanks; i++){
        if (stock.historyCount == FH_HISTORY_POINTS){
            memmove(stock.history, stock.history + 1, (FH_HISTORY_POINTS - 1) * sizeof(float));
            stock.historyCount--;
        }
        stock.history[stock.historyCount++] = (i == blanks) ? (float)stock.price : NAN;
    }
    stock.historyBucket = bucket;
}

static int fh_Fetch_Quote(Fh_Stock_t& stock){
    char path[48];
    snprintf(path, sizeof(path), "quote?symbol=%s", stock.symbol);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;

    const double price = doc["c"] | 0.0;
    const time_t quoteTime = (time_t)(doc["t"] | (int64_t)0);
    if (price <= 0 || quoteTime == 0){       // Unknown symbol, or one the free tier has no price for.
        stock.hasQuote = false;
        stock.noQuoteForSymbol = true;
        return code;
    }
    if (stock.hasQuote && price != stock.price){
        stock.tickDirection = (price > stock.price) ? 1 : -1;
        stock.tickMs = millis();
    }
    stock.price = price;
    stock.change = doc["d"] | 0.0;
    stock.changePct = doc["dp"] | 0.0;
    stock.high = doc["h"] | 0.0;
    stock.low = doc["l"] | 0.0;
    stock.open = doc["o"] | 0.0;
    stock.prevClose = doc["pc"] | 0.0;
    stock.quoteTime = quoteTime;
    stock.hasQuote = true;
    stock.noQuoteForSymbol = false;
    fh_Add_History(stock);
    return code;
}

static int fh_Fetch_Profile(Fh_Stock_t& stock){
    char path[56];
    snprintf(path, sizeof(path), "stock/profile2?symbol=%s", stock.symbol);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;

    fh_Copy_Ascii(stock.name, sizeof(stock.name), doc["name"] | "");
    fh_Copy_Ascii(stock.industry, sizeof(stock.industry), doc["finnhubIndustry"] | "");
    fh_Copy_Ascii(stock.currency, sizeof(stock.currency), doc["currency"] | "");
    fh_Copy_Ascii(stock.ipoDate, sizeof(stock.ipoDate), doc["ipo"] | "");
    stock.marketCapMillions = doc["marketCapitalization"] | (double)NAN;

    // "NASDAQ NMS - GLOBAL MARKET" -> "NASDAQ"
    const char* exchange = doc["exchange"] | "";
    if (strncmp(exchange, "NEW YORK", 8) == 0) snprintf(stock.exchange, sizeof(stock.exchange), "NYSE");
    else {
        fh_Copy_Ascii(stock.exchange, sizeof(stock.exchange), exchange);
        char* space = strchr(stock.exchange, ' ');
        if (space) *space = '\0';
    }
    // "https://www.apple.com/" -> "apple.com"
    const char* site = doc["weburl"] | "";
    if (strstr(site, "://")) site = strstr(site, "://") + 3;
    if (strncmp(site, "www.", 4) == 0) site += 4;
    fh_Copy_Ascii(stock.website, sizeof(stock.website), site);
    char* slash = strchr(stock.website, '/');
    if (slash) *slash = '\0';

    stock.hasProfile = stock.name[0] != '\0';
    return code;
}

static int fh_Fetch_Metrics(Fh_Stock_t& stock){
    char path[64];
    snprintf(path, sizeof(path), "stock/metric?symbol=%s&metric=all", stock.symbol);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    // {"metric":{...},"metricType":"all","series":{...}}: read the "metric" object, then hang up.
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (!reader.skipPast(':')) return FH_BAD_BODY;
    JsonDocument filter;
    static const char* const keys[] = {"52WeekHigh", "52WeekLow", "52WeekHighDate", "52WeekLowDate", "peTTM", "epsTTM", "beta",
        "dividendYieldIndicatedAnnual", "netProfitMarginTTM", "5DayPriceReturnDaily", "monthToDatePriceReturnDaily",
        "13WeekPriceReturnDaily", "26WeekPriceReturnDaily", "yearToDatePriceReturnDaily", "52WeekPriceReturnDaily"};
    for (const char* key : keys) filter[key] = true;
    JsonDocument doc;
    if (deserializeJson(doc, reader, DeserializationOption::Filter(filter))) return FH_BAD_BODY;

    JsonObjectConst metric = doc.as<JsonObjectConst>();
    stock.week52High = fh_Metric(metric, "52WeekHigh");
    stock.week52Low = fh_Metric(metric, "52WeekLow");
    fh_Copy_Ascii(stock.week52HighDate, sizeof(stock.week52HighDate), metric["52WeekHighDate"] | "");
    fh_Copy_Ascii(stock.week52LowDate, sizeof(stock.week52LowDate), metric["52WeekLowDate"] | "");
    stock.peRatio = fh_Metric(metric, "peTTM");
    stock.eps = fh_Metric(metric, "epsTTM");
    stock.beta = fh_Metric(metric, "beta");
    stock.dividendYield = fh_Metric(metric, "dividendYieldIndicatedAnnual");
    stock.netMargin = fh_Metric(metric, "netProfitMarginTTM");
    stock.return5D = fh_Metric(metric, "5DayPriceReturnDaily");
    stock.returnMTD = fh_Metric(metric, "monthToDatePriceReturnDaily");
    stock.return13W = fh_Metric(metric, "13WeekPriceReturnDaily");
    stock.return26W = fh_Metric(metric, "26WeekPriceReturnDaily");
    stock.returnYTD = fh_Metric(metric, "yearToDatePriceReturnDaily");
    stock.return52W = fh_Metric(metric, "52WeekPriceReturnDaily");
    stock.hasMetrics = true;
    return code;
}

static int fh_Fetch_Analysts(Fh_Stock_t& stock){
    char path[64];
    snprintf(path, sizeof(path), "stock/recommendation?symbol=%s", stock.symbol);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;
    JsonObjectConst latest = doc[0].as<JsonObjectConst>();     // Newest month first.
    if (latest.isNull()) return code;
    stock.strongBuy = latest["strongBuy"] | 0;
    stock.buy = latest["buy"] | 0;
    stock.hold = latest["hold"] | 0;
    stock.sell = latest["sell"] | 0;
    stock.strongSell = latest["strongSell"] | 0;
    stock.hasAnalysts = true;
    return code;
}

static int fh_Fetch_Earnings(Fh_Stock_t& stock){
    char path[64];
    snprintf(path, sizeof(path), "stock/earnings?symbol=%s&limit=%d", stock.symbol, FH_MAX_EARNINGS);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;
    uint8_t count = 0;
    for (JsonObjectConst quarter : doc.as<JsonArrayConst>()){    // Newest quarter first.
        if (count == FH_MAX_EARNINGS) break;
        JsonVariantConst surprise = quarter["surprisePercent"];
        stock.earningsSurprisePct[count] = surprise.is<float>() ? surprise.as<float>() : NAN;
        stock.earningsQuarter[count] = quarter["quarter"] | 0;
        count++;
    }
    stock.earningsCount = count;
    return code;
}

static int fh_Fetch_Next_Earnings(Fh_Stock_t& stock){
    time_t now = time(nullptr);
    struct tm day;
    char from[11], to[11], path[96];
    gmtime_r(&now, &day);
    if (day.tm_year < 124) return HTTP_CODE_REQUEST_TIMEOUT;   // The clock is not set yet.
    strftime(from, sizeof(from), "%Y-%m-%d", &day);
    now += 120L * 86400L;
    gmtime_r(&now, &day);
    strftime(to, sizeof(to), "%Y-%m-%d", &day);
    snprintf(path, sizeof(path), "calendar/earnings?symbol=%s&from=%s&to=%s", stock.symbol, from, to);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;
    stock.nextEarningsDate[0] = '\0';
    for (JsonObjectConst entry : doc["earningsCalendar"].as<JsonArrayConst>()){
        const char* date = entry["date"] | "";
        if (stock.nextEarningsDate[0] == '\0' || strcmp(date, stock.nextEarningsDate) < 0) fh_Copy_Ascii(stock.nextEarningsDate, sizeof(stock.nextEarningsDate), date);
    }
    return code;
}

static int fh_Fetch_News(Fh_Stock_t& stock){
    time_t now = time(nullptr);
    struct tm day;
    char from[11], to[11], path[96];
    gmtime_r(&now, &day);
    if (day.tm_year < 124) return HTTP_CODE_REQUEST_TIMEOUT;   // The clock is not set yet.
    strftime(to, sizeof(to), "%Y-%m-%d", &day);
    now -= 3L * 86400L;
    gmtime_r(&now, &day);
    strftime(from, sizeof(from), "%Y-%m-%d", &day);
    snprintf(path, sizeof(path), "company-news?symbol=%s&from=%s&to=%s", stock.symbol, from, to);
    const int code = fh_Get(path);
    if (code != HTTP_CODE_OK) return code;

    // Newest first and over 100 KB in total for a busy stock: read one article at a time and hang up after the last wanted.
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (!reader.skipPast('[')) return FH_BAD_BODY;
    JsonDocument filter;
    filter["headline"] = true;
    filter["source"] = true;
    uint8_t count = 0;
    while (count < FH_MAX_HEADLINES){
        JsonDocument article;
        if (deserializeJson(article, reader, DeserializationOption::Filter(filter))) break;   // An empty list ends here too.
        char headline[FH_HEADLINE_LEN];
        char line[FH_HEADLINE_LEN + 24];
        fh_Copy_Ascii(headline, sizeof(headline), article["headline"] | "");
        if (headline[0]){
            snprintf(line, sizeof(line), "%s: %s", article["source"] | "News", headline);
            fh_Copy_Ascii(stock.headlines[count], FH_HEADLINE_LEN, line);
            count++;
        }
        int next;
        while ((next = reader.read()) >= 0 && next != ',' && next != ']') {}
        if (next != ',') break;
    }
    stock.headlineCount = count;
    return code;
}

// Analysts' price target. Finnhub's free plan answers 403: that answer is how the app learns the plan.
static int fh_Fetch_Target(Fh_Stock_t& stock){
    char path[56];
    snprintf(path, sizeof(path), "stock/price-target?symbol=%s", stock.symbol);
    const int code = fh_Get(path);
    if (code == HTTP_CODE_FORBIDDEN){ fhPlan = FH_PLAN_FREE; fhPlanCheckedSec = fh_Uptime_Sec(); }
    if (code != HTTP_CODE_OK) return code;
    fhPlan = FH_PLAN_PREMIUM;
    fhPlanCheckedSec = fh_Uptime_Sec();
    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;
    stock.targetMean = doc["targetMean"] | 0.0f;
    stock.targetHigh = doc["targetHigh"] | 0.0f;
    stock.targetLow = doc["targetLow"] | 0.0f;
    stock.hasTarget = stock.targetMean > 0;
    return code;
}

static int (*const detailFetchers[FH_DETAIL_COUNT])(Fh_Stock_t&) = {
    fh_Fetch_Profile, fh_Fetch_Metrics, fh_Fetch_Analysts, fh_Fetch_Earnings, fh_Fetch_Next_Earnings, fh_Fetch_News, fh_Fetch_Target};
static const uint32_t detailRefreshSec[FH_DETAIL_COUNT] = {24 * 3600, 6 * 3600, 24 * 3600, 6 * 3600, 6 * 3600, 15 * 60, 24 * 3600};

static int fh_Fetch_Market(void){
    const int code = fh_Get("stock/market-status?exchange=US");
    if (code != HTTP_CODE_OK) return code;
    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(fh_Http().getStream(), FH_READ_TIMEOUT_MS, 0);
    if (deserializeJson(doc, reader)) return FH_BAD_BODY;
    const char* session = doc["session"] | "";
    usMarket.isOpen = doc["isOpen"] | false;
    const char* label = usMarket.isOpen ? "OPEN" : strcmp(session, "pre-market") == 0 ? "PRE" : strcmp(session, "post-market") == 0 ? "POST" : "CLOSED";
    snprintf(usMarket.session, sizeof(usMarket.session), "%s", label);
    usMarket.known = true;
    return code;
}

//##############################################################################################################
static Fh_Stock_t* fh_Stock_Slot(const char* symbol){
    int freeSlot = -1;
    for (int i = 0; i <= MAX_STOCKS; i++){
        if (stockSlots[i] == nullptr) { if (freeSlot < 0) freeSlot = i; continue; }
        if (strcmp(stockSlots[i]->symbol, symbol) == 0) return stockSlots[i];
    }
    Fh_Stock_t* slot = nullptr;
    if (freeSlot >= 0){
        slot = (Fh_Stock_t*)heap_caps_malloc(sizeof(Fh_Stock_t), MALLOC_CAP_SPIRAM);
        if (slot != nullptr) stockSlots[freeSlot] = slot;
    } else {
        // All in use: take over one for a symbol that has since left the watchlist.
        for (int i = 0; i <= MAX_STOCKS && slot == nullptr; i++){
            bool inUse = (stockSlots[i] == shownStock);
            for (int w = 0; w < stockCount && !inUse; w++) inUse = (stockSlots[i] == watchedStocks[w]);
            if (!inUse) slot = stockSlots[i];
        }
    }
    if (slot == nullptr) return nullptr;
    memset(slot, 0, sizeof(Fh_Stock_t));
    snprintf(slot->symbol, sizeof(slot->symbol), "%s", symbol);
    slot->week52High = slot->week52Low = slot->peRatio = slot->eps = slot->beta = slot->dividendYield = slot->netMargin = NAN;
    slot->return5D = slot->returnMTD = slot->return13W = slot->return26W = slot->returnYTD = slot->return52W = NAN;
    slot->marketCapMillions = NAN;
    return slot;
}

static void fh_Attach_Watchlist(void){
    for (int i = 0; i < stockCount; i++) watchedStocks[i] = fh_Stock_Slot(stockSymbols[i].c_str());
}

static void fh_Show_Stock(const char* symbol){
    Fh_Stock_t* stock = fh_Stock_Slot(symbol);
    if (stock == nullptr) return;
    shownStock = stock;
    shownIndex = -1;
    for (int i = 0; i < stockCount; i++) if (watchedStocks[i] == stock) shownIndex = i;
    if (shownIndex >= 0) stockIterate = shownIndex;
    if (strcmp(currentStocks.shown, symbol) != 0) snprintf(currentStocks.shown, sizeof(currentStocks.shown), "%s", symbol);
    stock->quoteDue = 0;
}

static void fh_Check_Logo(Fh_Stock_t& stock, Mtb_LocalImage_t& logo){
    snprintf(logo.imagePath, sizeof(logo.imagePath), "/stocks/%s.png", stock.symbol);
    if (stock.logoChecked) return;
    stock.hasLogo = LittleFS.exists(logo.imagePath);
    if (!stock.hasLogo && Mtb_Applications::internetConnectStatus == true){
        stock.hasLogo = mtb_Download_Github_File_To_SPIFFS("stocks_Icons/_" + String(stock.symbol) + ".png", String(logo.imagePath));
    }
    stock.logoChecked = (Mtb_Applications::internetConnectStatus == true);
}

// Common handling of a request's outcome. Returns true if it succeeded.
static bool fh_Check_Response(int code, uint32_t now){
    fh_Http().end();
    if (code == HTTP_CODE_OK){
        networkFails = 0;
        statusMessage[0] = '\0';
        fhKeyAccepted = true;
        fhKeyRejected = false;
        const time_t unix = time(nullptr);
        if (unix > 1700000000) fhLastAnswerUnix = (uint32_t)unix;
        return true;
    }
    if (code == HTTP_CODE_TOO_MANY_REQUESTS){
        requestsBlockedUntil = now + FH_BACKOFF_RATE_SEC;
    } else if (code == HTTP_CODE_UNAUTHORIZED){
        requestsBlockedUntil = now + FH_BACKOFF_KEY_SEC;
        fhKeyRejected = true;
        fhKeyAccepted = false;
        snprintf(statusMessage, sizeof(statusMessage), "Finnhub rejected the API key. Get a free key at finnhub.io/register");
    } else if (code < 0 && ++networkFails >= FH_NO_INTERNET_FAILS){
        snprintf(statusMessage, sizeof(statusMessage), "Cannot reach Finnhub. Showing the last data received.");
    }
    ESP_LOGI(TAG, "Request failed: %d\n", code);
    return false;
}

static int fh_First_Due_Detail(const Fh_Stock_t& stock, uint32_t now){
    for (int i = 0; i < FH_DETAIL_COUNT; i++) if (now >= stock.detailDue[i]) return i;
    return -1;
}

enum { FH_DIRTY_HERO = 1, FH_DIRTY_PAGE = 2, FH_DIRTY_WATCH = 4, FH_DIRTY_NEWS = 8 };

static uint8_t fh_Update_Quote(Fh_Stock_t& stock, uint32_t now){
    const double oldPrice = stock.price, oldChange = stock.change;
    const bool hadQuote = stock.hasQuote;
    const uint16_t oldCount = stock.historyCount;
    const time_t oldTime = stock.quoteTime;
    const bool closed = usMarket.known && !usMarket.isOpen;
    const bool shown = (&stock == shownStock);
    const uint32_t interval = shown ? (closed ? FH_QUOTE_WATCH_SEC : FH_QUOTE_SHOWN_SEC) : (closed ? FH_QUOTE_CLOSED_SEC : FH_QUOTE_WATCH_SEC);

    const int code = fh_Fetch_Quote(stock);
    if (!fh_Check_Response(code, now)){
        stock.quoteDue = now + (code == HTTP_CODE_FORBIDDEN ? FH_RETRY_SEC : interval);
        return 0;
    }
    stock.quoteDue = now + interval;
    if (stock.hasQuote == hadQuote && stock.price == oldPrice && stock.change == oldChange && stock.quoteTime == oldTime && stock.historyCount == oldCount) return 0;
    uint8_t dirty = FH_DIRTY_WATCH;
    if (shown) dirty |= FH_DIRTY_HERO | FH_DIRTY_PAGE;
    return dirty;
}

static uint8_t fh_Update_Detail(Fh_Stock_t& stock, int detail, uint32_t now){
    // Price targets are on paid plans only: on the free plan, ask again once a day (it shows an upgrade).
    if (detail == FH_TARGET && fhPlan == FH_PLAN_FREE && now - fhPlanCheckedSec < FH_PLAN_RECHECK_SEC){
        stock.detailDue[detail] = fhPlanCheckedSec + FH_PLAN_RECHECK_SEC;
        return 0;
    }
    const int code = detailFetchers[detail](stock);
    if (fh_Check_Response(code, now)) stock.detailDue[detail] = now + detailRefreshSec[detail];
    else {
        stock.detailDue[detail] = now + (code == HTTP_CODE_FORBIDDEN ? FH_DENIED_SEC : FH_RETRY_SEC);
        return 0;
    }
    if (&stock != shownStock) return 0;
    if (detail == FH_NEWS) return FH_DIRTY_NEWS;
    if (detail == FH_TARGET && !stock.hasTarget) return 0;
    return (detail == FH_PROFILE) ? (FH_DIRTY_HERO | FH_DIRTY_PAGE | FH_DIRTY_NEWS) : FH_DIRTY_PAGE;
}

// Runs the most useful single request and reports which parts of the screen it changed:
// market state, then the shown stock (price first), then the rest of the watchlist.
static uint8_t fh_Run_Next_Request(uint32_t now){
    if (now >= marketDue){
        const bool wasOpen = usMarket.isOpen;
        char wasSession[sizeof(usMarket.session)];
        memcpy(wasSession, usMarket.session, sizeof(wasSession));
        marketDue = now + (fh_Check_Response(fh_Fetch_Market(), now) ? FH_MARKET_SEC : FH_RETRY_SEC);
        return (wasOpen != usMarket.isOpen || strcmp(wasSession, usMarket.session) != 0) ? FH_DIRTY_HERO : 0;
    }
    if (now >= shownStock->quoteDue) return fh_Update_Quote(*shownStock, now);
    int detail = fh_First_Due_Detail(*shownStock, now);
    if (detail >= 0) return fh_Update_Detail(*shownStock, detail, now);

    Fh_Stock_t* oldest = nullptr;
    for (int i = 0; i < stockCount; i++){
        Fh_Stock_t* stock = watchedStocks[i];
        if (stock != nullptr && now >= stock->quoteDue && (oldest == nullptr || stock->quoteDue < oldest->quoteDue)) oldest = stock;
    }
    if (oldest != nullptr) return fh_Update_Quote(*oldest, now);

    for (int i = 0; i < stockCount; i++){
        if (watchedStocks[i] == nullptr) continue;
        detail = fh_First_Due_Detail(*watchedStocks[i], now);
        if (detail >= 0) return fh_Update_Detail(*watchedStocks[i], detail, now);
    }
    return 0;
}

// What the bottom row should say next: a problem if there is one, then this stock's headlines, then its profile.
static bool fh_Next_Ticker_Text(const Fh_Stock_t& stock, uint8_t& headline, char* text, size_t size, uint16_t& color){
    if (statusMessage[0]){
        snprintf(text, size, "%s", statusMessage);
        color = ORANGE;
        return true;
    }
    if (stock.headlineCount > 0){
        if (headline >= stock.headlineCount) headline = 0;
        snprintf(text, size, "%s", stock.headlines[headline++]);
        color = YELLOW;
        return true;
    }
    if (stock.hasProfile){
        snprintf(text, size, "%s  -  %s  -  %s  -  Listed %s  -  %s", stock.name, stock.exchange, stock.industry, stock.ipoDate, stock.website);
        color = CYAN;
        return true;
    }
    return false;
}

//##############################################################################################################
static void fh_Read_Settings(Fh_Settings_t& s){
    Fh_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(FH_NVS_KEY, &saved, sizeof(saved));
    if (saved.magic == FH_SETTINGS_MAGIC) s = saved;
    else {
        memset(&s, 0, sizeof(s));
        s.magic = FH_SETTINGS_MAGIC;
        snprintf(s.shown, sizeof(s.shown), "AAPL");
        s.apiToken[0] = '\0';           // The user's own Finnhub key, sent from the phone.
        s.stockChangeInterval = 30;
        s.autoCycle = 1;
    }
    s.shown[FH_SYMBOL_LEN - 1] = '\0';
    s.apiToken[sizeof(s.apiToken) - 1] = '\0';
    mtb_Kit_Clear_Retired_Key(s.apiToken, sizeof(s.apiToken));
    if (!fh_Symbol_Valid(s.shown)) snprintf(s.shown, sizeof(s.shown), "AAPL");
    if (s.stockChangeInterval < 10 || s.stockChangeInterval > 600) s.stockChangeInterval = 30;
    s.autoCycle = s.autoCycle ? 1 : 0;
}

static void fh_Save_Settings(void){ mtb_Write_Nvs_Struct(FH_NVS_KEY, &currentStocks, sizeof(Fh_Settings_t)); }

// The phone's cycle settings, applied on the app task (which owns the timer).
static void fh_Apply_Cycle(void){
    const int8_t cycle = pendingCycle;
    const int16_t interval = pendingInterval;
    pendingCycle = -1;
    pendingInterval = 0;
    if (interval >= 10 && interval <= 600 && interval != currentStocks.stockChangeInterval){
        currentStocks.stockChangeInterval = interval;
        xTimerChangePeriod(stockChangeTimer_H, pdMS_TO_TICKS(interval * 1000), 0);
        if (!currentStocks.autoCycle) xTimerStop(stockChangeTimer_H, 0);    // ChangePeriod starts a stopped timer.
    }
    if (cycle >= 0){
        currentStocks.autoCycle = (uint8_t)cycle;
        if (cycle) xTimerStart(stockChangeTimer_H, 0);
        else xTimerStop(stockChangeTimer_H, 0);
    }
    fh_Save_Settings();
}

//##############################################################################################################
void finhubStats_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(buttonChangeDisplayStock, mtb_Brightness_Control);
    // 4 and 5 were the stock searches; the phone searches its own list now.
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(showParticularStock, add_RemoveStockSymbol, setStockChangeInterval, saveAPI_key);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    fh_Read_Settings(currentStocks);

    if(stockChangeTimer_H == NULL) stockChangeTimer_H = xTimerCreate("stockChange Timer", pdMS_TO_TICKS(currentStocks.stockChangeInterval > 0 ? (currentStocks.stockChangeInterval * 1000) : (30 * 1000)), true, NULL, stockChange_TimerCallback);

    Mtb_ScrollText_t newsTicker(0, FH_NEWS_Y, PANEL_RES_X, Terminal6x8, YELLOW, 20, 1);
//##############################################################################################################

    // Read stock symbols from the CSV file
    readStockSymbols(stockSymbolsFilePath, stockSymbols, stockCount, MAX_STOCKS);
    // ESP_LOGI(TAG, "Found %d stock symbols:\n", stockCount);
    // for (int i = 0; i < stockCount; i++) {
    //     ESP_LOGI(TAG, "%s\n",stockSymbols[i].c_str());
    // }
    // If no symbols are found, write default symbols to the file
    if (stockCount == 0) {
        File file = LittleFS.open(stockSymbolsFilePath, "w");
        if (file) {
            String defaultSymbols = "AAPL,NVDA,AMZN,TSLA,MSFT,GOOG";
            file.print(defaultSymbols);
            file.close();
            ESP_LOGI(TAG, "Default stock symbols written to file.\n");

            // Read the symbols again after writing defaults
            readStockSymbols(stockSymbolsFilePath, stockSymbols, stockCount, MAX_STOCKS);
            ESP_LOGI(TAG, "After writing defaults, found %d stock symbols:\n", stockCount);
            for (int i = 0; i < stockCount; i++) {
                ESP_LOGI(TAG, "%s\n",stockSymbols[i].c_str());
            }
        } else {
            ESP_LOGI(TAG, "Failed to write default symbols to file.\n");
        }
    } 
    
    if (currentStocks.autoCycle) xTimerStart(stockChangeTimer_H, 0);
//##############################################################################################################
    stockStepsRequested = 0;
    symbolRequested = false;
    watchlistChanged = false;
    autoCycleToggled = false;
    cycleChanged = false;
    pendingCycle = -1;
    pendingInterval = 0;
    fhKeyRejected = false;
    fhKeyAccepted = false;
    marketDue = 0;
    requestsBlockedUntil = 0;
    networkFails = 0;
    statusMessage[0] = '\0';

    fh_Attach_Watchlist();
    fh_Show_Stock(currentStocks.shown);

    Mtb_LocalImage_t logo;
    logo.xAxis = 3; logo.yAxis = 11; logo.scale = 1;
    uint8_t page = 0;
    uint8_t headline = 0;
    uint32_t pageChangeMs = millis();
    uint32_t nextRequest = 0;
    uint32_t nextTickerCheckMs = 0;
    bool flashing = false;
    bool drawLogo = true;
    uint8_t dirty = FH_DIRTY_HERO | FH_DIRTY_PAGE | FH_DIRTY_WATCH | FH_DIRTY_NEWS;
    bool wholeScreen = true;
    char tickerText[FH_HEADLINE_LEN + 120];
    uint16_t tickerColor = YELLOW;

    for (;;){
        const uint32_t beforeCheckpoint = millis();
        if (THIS_APP_IS_ACTIVE != pdTRUE) break;
        // The checkpoint only takes this long if the app was paused, and then the screen has been used by something else.
        if (millis() - beforeCheckpoint > 300) { wholeScreen = true; drawLogo = true; }
        if (shownStock == nullptr){     // Out of PSRAM when the stock was chosen: keep trying.
            fh_Show_Stock(currentStocks.shown);
            delay(1000);
            continue;
        }

        if (watchlistChanged){
            watchlistChanged = false;
            readStockSymbols(stockSymbolsFilePath, stockSymbols, stockCount, MAX_STOCKS);
            fh_Attach_Watchlist();
            fh_Show_Stock(currentStocks.shown);
            dirty |= FH_DIRTY_WATCH;
            finnhub_Stats_App->mtb_App_Push_Mobile_Values();
        }
        if (cycleChanged){
            cycleChanged = false;
            fh_Apply_Cycle();
            dirty |= FH_DIRTY_PAGE;
        }
        if (autoCycleToggled){
            autoCycleToggled = false;
            if (xTimerIsTimerActive(stockChangeTimer_H) == pdTRUE) xTimerStop(stockChangeTimer_H, 0);
            else xTimerStart(stockChangeTimer_H, 0);
            currentStocks.autoCycle = xTimerIsTimerActive(stockChangeTimer_H) == pdTRUE;
            fh_Save_Settings();
            dirty |= FH_DIRTY_PAGE;
            finnhub_Stats_App->mtb_App_Push_Mobile_Values();
        }
        const uint8_t steps = stockStepsRequested;
        if (symbolRequested || (steps > 0 && stockCount > 0)){
            if (symbolRequested){
                symbolRequested = false;
                fh_Show_Stock(requestedSymbol);
            } else {
                stockStepsRequested = 0;
                stockIterate = (stockIterate + steps) % stockCount;
                fh_Show_Stock(stockSymbols[stockIterate].c_str());
            }
            // Only a choice made by the user is stored: the timer would wear the flash with a write per cycle.
            if (stockStepPersist){
                stockStepPersist = 0;
                fh_Save_Settings();
            }
            finnhub_Stats_App->mtb_App_Push_Mobile_Values();
            headline = 0;
            drawLogo = true;
            dirty |= FH_DIRTY_HERO | FH_DIRTY_PAGE | FH_DIRTY_WATCH | FH_DIRTY_NEWS;
        }

        const uint32_t now = fh_Uptime_Sec();
        if (Mtb_Applications::internetConnectStatus == true && !drawLogo && now >= nextRequest && now >= requestsBlockedUntil){
            const char hadStatus = statusMessage[0];
            dirty |= fh_Run_Next_Request(now);
            if ((hadStatus != 0) != (statusMessage[0] != 0)) dirty |= FH_DIRTY_NEWS;
            nextRequest = fh_Uptime_Sec() + FH_REQUEST_GAP_SEC;
        }

        // Pages share the time a stock is on screen, so a full auto-cycle shows them all.
        const uint32_t cycleSec = (xTimerIsTimerActive(stockChangeTimer_H) == pdTRUE) ? currentStocks.stockChangeInterval : 40;
        uint32_t pageMs = cycleSec * 1000 / FH_PAGE_COUNT;
        if (pageMs < 5000) pageMs = 5000;
        if (pageMs > 15000) pageMs = 15000;
        if (millis() - pageChangeMs >= pageMs){
            pageChangeMs = millis();
            page = (page + 1) % FH_PAGE_COUNT;
            dirty |= FH_DIRTY_PAGE;
        }

        // End of the flash after a price change.
        const bool flashingNow = shownStock->tickDirection != 0 && (millis() - shownStock->tickMs) < 1200;
        if (flashing && !flashingNow) dirty |= FH_DIRTY_HERO;
        flashing = flashingNow;

        if (drawLogo) fh_Check_Logo(*shownStock, logo);
        if (wholeScreen) dirty |= FH_DIRTY_HERO | FH_DIRTY_PAGE | FH_DIRTY_WATCH;
        if ((dirty & (FH_DIRTY_HERO | FH_DIRTY_PAGE | FH_DIRTY_WATCH)) || drawLogo){
            mtb_Panel_Begin_Update();
            if (wholeScreen) fh_Ui_Clear_Content();
            if (drawLogo) fh_Ui_Draw_Logo_Placeholder(*shownStock);
            if (dirty & FH_DIRTY_HERO) fh_Ui_Draw_Hero(*shownStock, usMarket, millis());
            if (dirty & FH_DIRTY_PAGE) fh_Ui_Draw_Page(page, *shownStock, xTimerIsTimerActive(stockChangeTimer_H) != pdTRUE);
            if (dirty & FH_DIRTY_WATCH) fh_Ui_Draw_Watchlist(watchedStocks, stockCount, shownIndex);
            mtb_Panel_End_Update();
            if (drawLogo && shownStock->hasLogo) mtb_Draw_Local_Png(logo);
        }
        const bool restartTicker = (dirty & FH_DIRTY_NEWS) != 0;
        wholeScreen = false;
        drawLogo = false;
        dirty = 0;

        if (restartTicker){
            newsTicker.mtb_Scroll_Active(STOP_SCROLL);
            nextTickerCheckMs = 0;
        }
        if ((int32_t)(millis() - nextTickerCheckMs) >= 0){
            nextTickerCheckMs = millis() + 500;
            if (!newsTicker.mtb_Scroll_Active() && fh_Next_Ticker_Text(*shownStock, headline, tickerText, sizeof(tickerText), tickerColor)){
                newsTicker.mtb_Scroll_This_Text(tickerText, tickerColor);
            }
        }
        delay(50);
    }

    newsTicker.mtb_Scroll_Active(STOP_SCROLL);

    // Clean up and free resources
    xTimerDelete(stockChangeTimer_H, 0);
    stockChangeTimer_H = NULL;
    shownStock = nullptr;

  mtb_Delete_This_App(THIS_APP);
}
//##############################################################################################################

// Streaming CSV reader: avoids readString()/substring(), minimizes heap churn.
void readStockSymbols(const char* filename, String stockSymbols[], int& count, const int maxSymbols)
{
    count = 0;

    // Ensure parent directories exist (uses Arduino LittleFS helpers below)
    if (!mtb_Prepare_Flash_File_Path(filename)) {
        ESP_LOGI(TAG, "Path prep failed");
        return;
    }

    // Create file if it doesn't exist (empty file → 0 symbols)
    if (!LittleFS.exists(filename)) {
        File nf = LittleFS.open(filename, "w");
        if (nf) nf.close();
        return;
    }

    File f = LittleFS.open(filename, "r");
    if (!f) return;

    String token;             // reuse one buffer (kept small by frequent trims)
    token.reserve(32);        // pre-reserve to reduce reallocs

    while (count < maxSymbols && f.available()) {
        int c = f.read();
        if (c == ',' || c == '\n' || c == '\r' || c < 0) {
            token.trim();
            if (token.length()) stockSymbols[count++] = token;
            token = ""; // reuse buffer
        } else {
            token += (char)c;
        }
    }

    // last token if file didn't end with comma/newline
    token.trim();
    if (count < maxSymbols && token.length()) stockSymbols[count++] = token;

    f.close();
}

void buttonChangeDisplayStock(button_event_t button_Data){
            switch (button_Data.type){
            case BUTTON_RELEASED:
            break;

            case BUTTON_PRESSED:
                stockStepPersist = 1;
                stockStepsRequested = stockStepsRequested + 1;
                break;

            case BUTTON_PRESSED_LONG:
            autoCycleToggled = true;
            break;

            case BUTTON_CLICKED:
            //ESP_LOGI(TAG, "Button Clicked: %d Times\n",button_Data.count);
            switch (button_Data.count){
            case 1:
                break;
            case 2:
                break;
            case 3:
                break;
            default:
                break;
            }
                break;
            default:
            break;
			}
}

void stockChange_TimerCallback(TimerHandle_t stockPrompt){
    stockStepsRequested = stockStepsRequested + 1;
}

// Writes the watchlist file; the app task reads it back.
static bool fh_Write_Watchlist(const char (*symbols)[FH_SYMBOL_LEN], int count){
    if (!mtb_Prepare_Flash_File_Path(stockSymbolsFilePath)) return false;
    File file = LittleFS.open(stockSymbolsFilePath, "w");
    if (!file) return false;
    for (int i = 0; i < count; i++){ if (i) file.print(","); file.print(symbols[i]); }
    file.close();
    watchlistChanged = true;
    return true;
}

// Command 0: show a stock now: {"stock": {"name", "value": "AAPL"}} from the phone's search, or {"stkSymbol": "AAPL"}.
void showParticularStock(JsonDocument& dCommand){
    const char* text = dCommand["stock"]["value"] | (dCommand["stkSymbol"] | (const char*)nullptr);
    char symbol[FH_SYMBOL_LEN];
    if (!fh_Symbol_Normalise(text, symbol, sizeof(symbol))){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(requestedSymbol, sizeof(requestedSymbol), "%s", symbol);
    stockStepPersist = 1;
    symbolRequested = true;
}

// Command 1: the watchlist: {"stocks": [{"name", "value": "AAPL"}, ...]}, or the old page's {"stkList": "[AAPL, NVDA]"}.
void add_RemoveStockSymbol(JsonDocument& dCommand){
    char (*symbols)[FH_SYMBOL_LEN] = (char (*)[FH_SYMBOL_LEN])heap_caps_malloc(MAX_STOCKS * FH_SYMBOL_LEN, MALLOC_CAP_SPIRAM);
    if (symbols == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    int count = 0;
    bool valid = true;
    auto add = [&](const char* text){
        char symbol[FH_SYMBOL_LEN];
        if (!fh_Symbol_Normalise(text, symbol, sizeof(symbol))){ valid = false; return; }
        for (int k = 0; k < count; k++) if (strcmp(symbols[k], symbol) == 0) return;
        if (count >= MAX_STOCKS){ valid = false; return; }
        memcpy(symbols[count++], symbol, FH_SYMBOL_LEN);
    };
    JsonVariant stocks = dCommand["stocks"];
    if (stocks.is<JsonArray>()){
        for (JsonVariant item : stocks.as<JsonArray>()) add(item["value"] | (item.is<const char*>() ? item.as<const char*>() : ""));
    } else if (dCommand["stkList"].is<const char*>()){
        // "[AAPL, NVDA]": the phone's former list text.
        char text[MAX_STOCKS * 8];
        snprintf(text, sizeof(text), "%s", dCommand["stkList"].as<const char*>());
        for (char* c = text; *c; c++) if (*c == '[' || *c == ']' || *c == '"') *c = ' ';
        for (char* token = strtok(text, ", "); token && valid; token = strtok(nullptr, ", ")) add(token);
    } else valid = false;
    if (!valid || count == 0 || !fh_Write_Watchlist(symbols, count)) mtb_Ble_App_Cmd_Reject();
    heap_caps_free(symbols);
}

// Command 2: {"cycleStocks": 0-1} and/or {"dInterval": 10-600} seconds per stock.
void setStockChangeInterval(JsonDocument& dCommand){
    long cycle = -1, interval = 0;
    const bool hasCycle = mtb_Kit_Json_Range(dCommand["cycleStocks"], 0, 1, cycle);
    const bool hasInterval = mtb_Kit_Json_Range(dCommand["dInterval"], 10, 600, interval);
    if (!hasCycle && !hasInterval){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) pendingCycle = (int8_t)cycle;
    if (hasInterval) pendingInterval = (int16_t)interval;
    cycleChanged = true;
}

// Command 3: {"api_key": "..."}: Finnhub keys are 20 letters and digits.
void saveAPI_key(JsonDocument& dCommand){
    const char* key = dCommand["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length >= 16 && length < sizeof(currentStocks.apiToken);
    for (size_t i = 0; valid && i < length; i++) if (!isalnum((unsigned char)key[i])) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Fh_Settings_t s;
    fh_Read_Settings(s);
    memset(s.apiToken, 0, sizeof(s.apiToken));
    memcpy(s.apiToken, key, length);
    mtb_Write_Nvs_Struct(FH_NVS_KEY, &s, sizeof(s));
    memcpy(currentStocks.apiToken, s.apiToken, sizeof(s.apiToken));     // Used from the next request.
    memset(&s, 0, sizeof(s));
    fhKeyRejected = false;
    fhPlan = FH_PLAN_UNKNOWN;
    requestsBlockedUntil = 0;
    statusMessage[0] = '\0';
}

// What the phone shows. The key never leaves the device. Runs on the BLE parser task.
static void fh_Mobile_Ui_Values(JsonObject values){
    Fh_Settings_t* s = (Fh_Settings_t*)heap_caps_malloc(sizeof(Fh_Settings_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    fh_Read_Settings(*s);
    // The watchlist, from its file.
    JsonArray stocks = values["stocks"].to<JsonArray>();
    File file = LittleFS.open(stockSymbolsFilePath, "r");
    int listed = 0;
    if (file){
        char symbol[FH_SYMBOL_LEN];
        size_t n = 0;
        while (listed < MAX_STOCKS){
            const int c = file.read();
            if (c == ',' || c == '\n' || c == '\r' || c == ' ' || c < 0){
                symbol[n] = '\0';
                if (n){ JsonObject item = stocks.add<JsonObject>(); item["name"] = symbol; item["value"] = symbol; listed++; }
                n = 0;
                if (c < 0) break;
            } else if (n + 1 < sizeof(symbol)) symbol[n++] = (char)c;
        }
        file.close();
    }
    JsonObject shown = values["stock"].to<JsonObject>();
    shown["name"] = s->shown;
    shown["value"] = s->shown;
    values["cycleStocks"] = s->autoCycle;
    values["dInterval"] = s->stockChangeInterval;
    values["api_key_set"] = s->apiToken[0] ? 1 : 0;
    // The stock on screen, while the app runs.
    const Fh_Stock_t* stock = shownStock;
    if (stock != nullptr && stock->hasQuote){
        char text[80];
        const char* name = stock->hasProfile ? stock->name : "";
        snprintf(text, sizeof(text), "%s %.2f, %+.2f%%%s%.24s", stock->symbol, stock->price, stock->changePct, name[0] ? "  " : "", name);
        values["shownText"] = text;
        values["shownState"] = stock->changePct > 0 ? "ok" : stock->changePct < 0 ? "error" : "info";
    }
    if (usMarket.known){
        values["market"] = usMarket.isOpen ? "Open" : strcmp(usMarket.session, "PRE") == 0 ? "Pre-market" : strcmp(usMarket.session, "POST") == 0 ? "After hours" : "Closed";
        values["marketState"] = usMarket.isOpen ? "ok" : "off";
    }
    const uint8_t plan = fhPlan;
    values["plan"] = plan == FH_PLAN_PREMIUM ? "Paid: price targets shown" : plan == FH_PLAN_FREE ? "Free: quotes, news and fundamentals" : "Checking";
    values["planState"] = plan == FH_PLAN_PREMIUM ? "ok" : plan == FH_PLAN_FREE ? "info" : "off";
    values["paid"] = plan == FH_PLAN_PREMIUM ? 1 : 0;
    if (fhKeyRejected){ values["keyText"] = "Not accepted by Finnhub"; values["keyState"] = "error"; }
    else if (fhKeyAccepted){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    values["watchCount"] = listed;
    if (fhLastAnswerUnix) values["updated"] = fhLastAnswerUnix;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}

// void readStockSymbols(const char* filename, String stockSymbols[], int& count, const int maxSymbols){
    
//     if (mtb_Prepare_Flash_File_Path(filename)){
//         ESP_LOGI(TAG, "File path prepared successfully: %s\n", filename);
//     } else {
//         ESP_LOGI(TAG, "Failed to prepare file path.\n");
//         count = 0;
//         return;
//     }
//     // Check if the file exists
//     if (!LittleFS.exists(filename)) {
//         // Create a new file if it doesn't exist
//         File file = LittleFS.open(filename, "w");
//         if (!file) {
//             ESP_LOGI(TAG, "Failed to create file.\n");
//             count = 0;
//             return;
//         }
//         file.close();
//         ESP_LOGI(TAG, "File created successfully.\n");
//         count = 0;
//         return;
//     }
    
//     // Open the file for reading
//     File file = LittleFS.open(filename, "r");
//     if (!file) {
//         ESP_LOGI(TAG, "Failed to open file.\n");
//         count = 0;
//         return;
//     }
    
//     // Read the entire content of the file
//     String content = file.readString();
//     file.close();
    
//     // Parse the CSV data to extract stock symbols
//     int start = 0;
//     count = 0;
//     content.trim();  // Remove leading and trailing whitespace
//     while (count < maxSymbols && start < content.length()) {
//         int commaIndex = content.indexOf(',', start);
//         if (commaIndex == -1) {
//             // No more commas; read until the end
//             String symbol = content.substring(start);
//             symbol.trim();
//             if (symbol.length() > 0) {
//                 stockSymbols[count++] = symbol;
//             }
//             break;
//         } else {
//             String symbol = content.substring(start, commaIndex);
//             symbol.trim();
//             if (symbol.length() > 0) {
//                 stockSymbols[count++] = symbol;
//             }
//             start = commaIndex + 1;
//         }
//     }
// }