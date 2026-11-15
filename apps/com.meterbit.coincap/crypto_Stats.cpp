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
#include "crypto_Ui.h"
#include "cc_Coin_Id.h"
#include "cc_Mobile_Ui.h"

static const char TAG[] = "CRYPTO_STATS";

#define MAX_COINS 100

// CoinCap's free plan: 500 credits a month and 5 calls a minute. A call costs 1 credit, plus 1 per 2,500 bytes.
// Credits are earned at a steady rate and banked up to a limit, so any 30 days spend at most 45 + 30 x 15 = 495,
// whichever day CoinCap's month starts on. The ledger below rounds the cost of every call up.
// Every answer's x-ratelimit-limit header gives the plan's calls a minute: 5 on the free plan, more on paid ones.
// CoinCap publishes no credit counts per paid tier, so on a paid plan the user sets the month's credits, and every
// figure below scales with them (cc_Scale()).
#define CC_FREE_CREDITS         500
#define CC_FREE_CALLS_PER_MIN   5
#define CC_CREDITS_PER_DAY      (15.0f * cc_Scale())
#define CC_CREDITS_MAX          (45.0f * cc_Scale())
#define CC_CREDITS_FIRST_RUN    30.0f
#define CC_BYTES_PER_CREDIT     2500

// One request for up to this many coins stays below 2,500 bytes.
#define CC_BATCH_COINS          5
#define CC_QUOTE_COST           2
#define CC_CHART_COST           3

// The more credits are banked, the sooner prices refresh. Below CC_CREDITS_LOW everything together uses less than is
// earned, so the balance climbs back up, and a device left on this app settles around that level.
#define CC_CREDITS_HIGH         (30.0f * cc_Scale())
#define CC_CREDITS_LOW          (12.0f * cc_Scale())
#define CC_QUOTE_FAST_SEC       cc_Scaled_Sec(30 * 60, 120)
#define CC_QUOTE_NORMAL_SEC     cc_Scaled_Sec(3 * 3600, 300)
#define CC_QUOTE_SLOW_SEC       cc_Scaled_Sec(6 * 3600, 900)
#define CC_CHART_NORMAL_SEC     cc_Scaled_Sec(24 * 3600, 3600)
#define CC_CHART_SLOW_SEC       cc_Scaled_Sec(120 * 3600, 6 * 3600)
#define CC_NEWS_SEC             (24 * 3600)
#define CC_MOVERS_SEC           (48 * 3600)
#define CC_CHART_DWELL_SEC      8       // A coin the user is only passing through does not get a chart.
#define CC_MISSING_QUOTE_SEC    600

#define CC_REQUEST_GAP_SEC      cc_Request_Gap_Sec()
#define CC_READ_TIMEOUT_MS      6000
#define CC_RETRY_SEC            120
#define CC_BACKOFF_ERROR_SEC    600
#define CC_BACKOFF_LIMIT_SEC    3600
#define CC_NO_INTERNET_FAILS    3
#define CC_BAD_BODY             (-1000)
#define CC_NO_REQUEST           0
#define CC_MAX_ICON_BYTES       (64 * 1024)
#define CC_CLOCK_VALID_AFTER    1700000000UL

#define CC_COIN_MAGIC           0x43433031UL
#define CC_MARKET_MAGIC         0x434D3031UL

#define CC_SETTINGS_MAGIC       0x43434332UL    // "CCC2"
#define CC_NVS_KEY              "coinCapCfg"

// The settings before the phone UI ("cryptoCfg"): read once to keep the user's key and coin.
struct Crypto_Stat_Old_t {
  char coinID[CC_ID_LEN];
  char coinSymbol[CC_SYMBOL_LEN];
  char apiToken[150];
  int16_t cryptoChangeInterval;
};

struct Crypto_Stat_t {
  uint32_t magic;
  char coinID[CC_ID_LEN];
  char coinSymbol[CC_SYMBOL_LEN];
  char apiToken[150];
  int16_t cryptoChangeInterval;     // Seconds per coin while cycling (10-600).
  uint8_t autoCycle;
  uint32_t monthlyCredits;          // A paid plan's credits a month, as the user set them; the free plan's 500 otherwise.
};

struct Cc_Credit_Ledger_t {
  float credits;
  uint32_t updated;
};

// Default CryptoCurrency
EXT_RAM_BSS_ATTR Crypto_Stat_t currentCryptoCurrency;
EXT_RAM_BSS_ATTR static Cc_Credit_Ledger_t creditLedger;

EXT_RAM_BSS_ATTR TimerHandle_t cryptoChangeTimer_H = NULL;
int8_t cryptoIterate = 0;

EXT_RAM_BSS_ATTR String cryptoSymbols[MAX_COINS];
EXT_RAM_BSS_ATTR String cryptoIDs[MAX_COINS];

int cryptoCount = 0;
static const char cryptoSymbolsFilePath[] = "/crypto/dCrypto.csv";
static const char cryptoIDsFilePath[] = "/crypto/dCryptoID.csv";
static const char cryptoMarketFilePath[] = "/crypto/market.bin";

EXT_RAM_BSS_ATTR TaskHandle_t cryptoStats_Task_H = NULL;

// Requests from the button, timer and BLE tasks. The app task acts on them, so only it touches the coin data.
static volatile uint8_t coinStepsRequested = 0;
static volatile uint8_t coinStepPersist = 0;
static volatile bool coinRequested = false;
static volatile bool watchlistChanged = false;
static volatile bool autoCycleToggled = false;
static char requestedCoinID[CC_ID_LEN];
static char requestedCoinSymbol[CC_SYMBOL_LEN];

// One slot per coin, allocated in PSRAM on first use and kept for the whole power-on session.
// One spare for a coin shown from outside the watchlist.
EXT_RAM_BSS_ATTR static Cc_Coin_t* coinSlots[MAX_COINS + 1];
EXT_RAM_BSS_ATTR static Cc_Coin_t* watchedCoins[MAX_COINS];
EXT_RAM_BSS_ATTR static Cc_Market_t cryptoMarket;
static Cc_Coin_t* shownCoin = nullptr;
static int shownIndex = -1;
static uint32_t requestsBlockedUntil = 0;
static uint32_t shownSinceSec = 0;
static uint32_t missingQuoteFetchSec = 0;
static uint8_t networkFails = 0;
static char statusMessage[80] = "";

// What CoinCap's answers say about the key.
static volatile uint16_t ccCallsPerMinute = 0;      // From x-ratelimit-limit; 0 until an answer has it.
static volatile bool ccKeyRejected = false, ccKeyAccepted = false;
static volatile uint32_t ccLastAnswerUnix = 0;
static volatile bool cycleChanged = false;
static volatile int8_t pendingCycle = -1;
static volatile int16_t pendingInterval = 0;

static bool cc_Paid(void){ return ccCallsPerMinute > CC_FREE_CALLS_PER_MIN; }

// The month's credits the app spends to: the free plan's, or what the user set for a paid plan.
static uint32_t cc_Monthly_Credits(void){
    const uint32_t set = currentCryptoCurrency.monthlyCredits;
    return cc_Paid() && set > CC_FREE_CREDITS ? set : CC_FREE_CREDITS;
}
static float cc_Scale(void){ return (float)cc_Monthly_Credits() / CC_FREE_CREDITS; }
static uint32_t cc_Scaled_Sec(uint32_t seconds, uint32_t floor){
    const uint32_t scaled = (uint32_t)(seconds / cc_Scale());
    return scaled < floor ? floor : scaled;
}
// Under the plan's calls a minute, with a margin.
static uint32_t cc_Request_Gap_Sec(void){
    const uint16_t perMinute = ccCallsPerMinute ? ccCallsPerMinute : CC_FREE_CALLS_PER_MIN;
    const uint32_t gap = 60 / perMinute + 3;
    return gap > 15 ? 15 : gap;
}

void cryptoStats_App_Task(void *);
void readCryptoSymbols(const char *filename, String coinSymbols[], int &count, const int maxSymbols);
void buttonChangeDisplayCrypto(button_event_t button_Data);
void crytoChange_TimerCallback(TimerHandle_t);

void showParticularCrypto(JsonDocument&);
void add_RemoveCryptoSymbol(JsonDocument&);
void setCryptoChangeInterval(JsonDocument&);
void setCrytoAPI_key(JsonDocument&);
static void cc_Ble_Credits(JsonDocument&);
static void cc_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *coinCap_Stats_App;
Mtb_Applications* coinCap_Stats_App_GetInstance() {
    if (!coinCap_Stats_App){
        coinCap_Stats_App = new Mtb_Applications_StatusBar(cryptoStats_App_Task, &cryptoStats_Task_H, "Crypto Data", {4,1}, 10240);
        coinCap_Stats_App->mtb_App_Set_Mobile_UI(CC_UI_MANIFEST, CC_UI_API, nullptr, cc_Mobile_Ui_Values);
    }
    return coinCap_Stats_App;
}
MTB_REGISTER_APP(coinCap_Stats_App, 4, 1)

//##############################################################################################################
static uint32_t cc_Uptime_Sec(void){ return (uint32_t)(esp_timer_get_time() / 1000000); }

static uint32_t cc_Now(void){
    const time_t now = time(nullptr);
    return (now > (time_t)CC_CLOCK_VALID_AFTER) ? (uint32_t)now : 0;
}

// Copies text from CoinCap to a plain ASCII string, the only characters the fonts have. Typographic
// punctuation is replaced with its ASCII look-alike and anything else that is not ASCII is dropped.
static void cc_Copy_Ascii(char* dest, size_t size, const char* src){
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

// CoinCap sends its numbers as strings, or null when it has no value.
static double cc_Number(JsonVariantConst value){
    const char* text = value.as<const char*>();
    return (text != nullptr && text[0] != '\0') ? strtod(text, nullptr) : NAN;
}

//##############################################################################################################
static void cc_Credits_Accrue(uint32_t now){
    if (now == 0) return;
    if (creditLedger.updated != 0 && now > creditLedger.updated){
        creditLedger.credits += (now - creditLedger.updated) * (CC_CREDITS_PER_DAY / 86400.0f);
        if (creditLedger.credits > CC_CREDITS_MAX) creditLedger.credits = CC_CREDITS_MAX;
    }
    creditLedger.updated = now;
}

static void cc_Credits_Charge(size_t bodyBytes){
    const size_t cost = 1 + (bodyBytes + CC_BYTES_PER_CREDIT - 1) / CC_BYTES_PER_CREDIT;
    creditLedger.credits -= (float)cost;
    if (creditLedger.credits < 0) creditLedger.credits = 0;
    mtb_Write_Nvs_Struct("cryptoCredits", &creditLedger, sizeof(creditLedger));
}

static uint32_t cc_Quote_Interval(void){
    if (creditLedger.credits >= CC_CREDITS_HIGH) return CC_QUOTE_FAST_SEC;
    return (creditLedger.credits >= CC_CREDITS_LOW) ? CC_QUOTE_NORMAL_SEC : CC_QUOTE_SLOW_SEC;
}

//##############################################################################################################
static void cc_Coin_File_Path(char* dest, size_t size, const char* id){
    snprintf(dest, size, "/crypto/c_%s.bin", id);
}

static void cc_Save_Coin(const Cc_Coin_t& coin){
    char path[64];
    cc_Coin_File_Path(path, sizeof(path), coin.id);
    if (!mtb_Prepare_Flash_File_Path(path)) return;
    File file = LittleFS.open(path, "w");
    if (!file) return;
    file.write((const uint8_t*)&coin, sizeof(Cc_Coin_t));
    file.close();
}

static void cc_Load_Coin(Cc_Coin_t& coin){
    char path[64];
    cc_Coin_File_Path(path, sizeof(path), coin.id);
    if (!LittleFS.exists(path)) return;
    File file = LittleFS.open(path, "r");
    if (!file) return;
    Cc_Coin_t* saved = (Cc_Coin_t*)heap_caps_malloc(sizeof(Cc_Coin_t), MALLOC_CAP_SPIRAM);
    if (saved != nullptr){
        const bool complete = file.size() == sizeof(Cc_Coin_t) && file.read((uint8_t*)saved, sizeof(Cc_Coin_t)) == sizeof(Cc_Coin_t);
        if (complete && saved->magic == CC_COIN_MAGIC && strcmp(saved->id, coin.id) == 0 && saved->historyCount <= CC_HISTORY_POINTS){
            memcpy(&coin, saved, sizeof(Cc_Coin_t));
            coin.unknownToApi = coin.iconChecked = coin.hasIcon = false;
        }
        free(saved);
    }
    file.close();
}

static void cc_Save_Market(void){
    if (!mtb_Prepare_Flash_File_Path(cryptoMarketFilePath)) return;
    File file = LittleFS.open(cryptoMarketFilePath, "w");
    if (!file) return;
    file.write((const uint8_t*)&cryptoMarket, sizeof(Cc_Market_t));
    file.close();
}

static void cc_Load_Market(void){
    memset(&cryptoMarket, 0, sizeof(Cc_Market_t));
    cryptoMarket.magic = CC_MARKET_MAGIC;
    if (!LittleFS.exists(cryptoMarketFilePath)) return;
    File file = LittleFS.open(cryptoMarketFilePath, "r");
    if (!file) return;
    Cc_Market_t saved;
    if (file.size() == sizeof(Cc_Market_t) && file.read((uint8_t*)&saved, sizeof(Cc_Market_t)) == sizeof(Cc_Market_t) && saved.magic == CC_MARKET_MAGIC
        && saved.gainerCount <= CC_MAX_GAINERS && saved.headlineCount <= CC_MAX_HEADLINES) memcpy(&cryptoMarket, &saved, sizeof(Cc_Market_t));
    file.close();
}

//##############################################################################################################
// HTTP/1.0 keeps the response free of chunk markers, so it can be parsed straight from the stream.
static HTTPClient& cc_Http(void){
    static HTTPClient http;
    return http;
}

// Verifies the server against the CA bundle in the firmware.
static WiFiClientSecure& cc_Tls_Client(void){
    static WiFiClientSecure client;
    static bool configured = false;
    if (!configured){
        client.useBuiltinCACertBundle();
        configured = true;
    }
    return client;
}

static int cc_Get(const char* pathAndQuery){
    char url[320];
    snprintf(url, sizeof(url), "https://rest.coincap.io/v3/%s", pathAndQuery);
    HTTPClient& http = cc_Http();
    http.useHTTP10(true);
    http.setConnectTimeout(6000);
    http.setTimeout(CC_READ_TIMEOUT_MS);
    if (!http.begin(cc_Tls_Client(), url)) return -1;
    char authorization[170];
    snprintf(authorization, sizeof(authorization), "Bearer %s", currentCryptoCurrency.apiToken);
    http.addHeader("Authorization", authorization);
    static const char* HEADERS[] = {"x-ratelimit-limit"};
    http.collectHeaders(HEADERS, 1);
    const int code = http.GET();
    // The plan's calls a minute: 5 on the free plan.
    const long perMinute = http.header("x-ratelimit-limit").toInt();
    if (code > 0 && perMinute > 0 && perMinute < 100000) ccCallsPerMinute = (uint16_t)perMinute;
    return code;
}

// Common handling of a request's outcome: bills it and returns true if it succeeded. Anything the server answered
// may have been billed, so only a request that never got an answer is free.
static bool cc_Check_Response(int code, size_t bodyBytes, uint32_t upSec){
    cc_Http().end();
    if (code > 0 || code == CC_BAD_BODY) cc_Credits_Charge(bodyBytes);
    if (code == HTTP_CODE_OK){
        networkFails = 0;
        statusMessage[0] = '\0';
        ccKeyAccepted = true;
        ccKeyRejected = false;
        const uint32_t unix = cc_Now();
        if (unix) ccLastAnswerUnix = unix;
        return true;
    }
    if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN){
        requestsBlockedUntil = upSec + CC_BACKOFF_LIMIT_SEC;
        ccKeyRejected = true;
        ccKeyAccepted = false;
        snprintf(statusMessage, sizeof(statusMessage), "CoinCap rejected the API key. Get a free key at pro.coincap.io");
    } else if (code == HTTP_CODE_TOO_MANY_REQUESTS || code == 402){
        requestsBlockedUntil = upSec + CC_BACKOFF_LIMIT_SEC;
        snprintf(statusMessage, sizeof(statusMessage), "CoinCap credit limit reached. Showing the last data received.");
    } else if (code > 0 || code == CC_BAD_BODY){
        requestsBlockedUntil = upSec + CC_BACKOFF_ERROR_SEC;
    } else {
        requestsBlockedUntil = upSec + CC_RETRY_SEC;
        if (++networkFails >= CC_NO_INTERNET_FAILS) snprintf(statusMessage, sizeof(statusMessage), "Cannot reach CoinCap. Showing the last data received.");
    }
    ESP_LOGI(TAG, "Request failed: %d\n", code);
    return false;
}

//##############################################################################################################
// Adds a price to the chart. Two prices within the hour share a point, so the history keeps reaching seven days back.
static void cc_Add_History(Cc_Coin_t& coin, uint32_t when, float price){
    if (coin.historyCount > 0){
        Cc_Point_t& last = coin.history[coin.historyCount - 1];
        if (when <= last.time) return;
        if (coin.historyCount >= 2 && when - coin.history[coin.historyCount - 2].time < 3600){
            last.time = when;
            last.price = price;
            return;
        }
    }
    if (coin.historyCount == CC_HISTORY_POINTS){
        memmove(coin.history, coin.history + 1, (CC_HISTORY_POINTS - 1) * sizeof(Cc_Point_t));
        coin.historyCount--;
    }
    coin.history[coin.historyCount].time = when;
    coin.history[coin.historyCount].price = price;
    coin.historyCount++;
}

static void cc_Store_Quote(Cc_Coin_t& coin, JsonObjectConst asset, uint32_t when){
    const double price = cc_Number(asset["priceUsd"]);
    if (isnan(price) || price <= 0) return;
    cc_Copy_Ascii(coin.symbol, sizeof(coin.symbol), asset["symbol"] | (const char*)coin.symbol);
    cc_Copy_Ascii(coin.name, sizeof(coin.name), asset["name"] | "");
    const double rank = cc_Number(asset["rank"]);
    coin.rank = isnan(rank) ? 0 : (uint16_t)rank;
    coin.price = price;
    const double change = cc_Number(asset["changePercent24Hr"]);
    coin.changePct = isnan(change) ? 0 : (float)change;
    coin.marketCap = cc_Number(asset["marketCapUsd"]);
    coin.volume24h = cc_Number(asset["volumeUsd24Hr"]);
    coin.vwap24h = cc_Number(asset["vwap24Hr"]);
    coin.supply = cc_Number(asset["supply"]);
    coin.maxSupply = cc_Number(asset["maxSupply"]);
    coin.quoteTime = when;
    coin.hasQuote = true;
    coin.unknownToApi = false;
    cc_Add_History(coin, when, (float)price);
}

// Prices of the shown coin and the watchlist coins that have waited longest, all in one call.
static int cc_Fetch_Quotes(uint32_t now, size_t& bodyBytes){
    Cc_Coin_t* batch[CC_BATCH_COINS];
    uint8_t batchCount = 0;
    if (!shownCoin->unknownToApi) batch[batchCount++] = shownCoin;
    bool chosen[MAX_COINS] = {false};
    while (batchCount < CC_BATCH_COINS){
        int oldest = -1;
        for (int i = 0; i < cryptoCount; i++){
            const Cc_Coin_t* coin = watchedCoins[i];
            if (coin == nullptr || coin == shownCoin || chosen[i] || coin->unknownToApi) continue;
            if (oldest < 0 || coin->quoteTime < watchedCoins[oldest]->quoteTime) oldest = i;
        }
        if (oldest < 0) break;
        chosen[oldest] = true;
        batch[batchCount++] = watchedCoins[oldest];
    }
    if (batchCount == 0){
        cryptoMarket.quotesTime = now;
        return CC_NO_REQUEST;
    }

    char path[280];
    int used = snprintf(path, sizeof(path), "assets?ids=");
    for (uint8_t i = 0; i < batchCount; i++){
        used += snprintf(path + used, sizeof(path) - used, "%s%s", i ? "," : "", batch[i]->id);
    }
    const int code = cc_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument filter;
    JsonObject wanted = filter["data"][0].to<JsonObject>();
    static const char* const keys[] = {"id", "rank", "symbol", "name", "supply", "maxSupply", "marketCapUsd", "volumeUsd24Hr", "priceUsd", "changePercent24Hr", "vwap24Hr"};
    for (const char* key : keys) wanted[key] = true;
    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(cc_Http().getStream(), CC_READ_TIMEOUT_MS, 0);
    const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
    bodyBytes = reader.bytesReceived();
    if (error) return CC_BAD_BODY;

    JsonArrayConst assets = doc["data"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < batchCount; i++){
        bool found = false;
        for (JsonObjectConst asset : assets){
            if (strcmp(asset["id"] | "", batch[i]->id) != 0) continue;
            cc_Store_Quote(*batch[i], asset, now);
            found = true;
            break;
        }
        if (!found) batch[i]->unknownToApi = true;      // Not asked for again until the next power-on.
        if (found) cc_Save_Coin(*batch[i]);
    }
    cryptoMarket.quotesTime = now;
    cc_Save_Market();
    return code;
}

// Seven days at six-hour steps: 28 points and just under 3 KB.
static int cc_Fetch_Chart(Cc_Coin_t& coin, uint32_t now, size_t& bodyBytes){
    char path[140];
    snprintf(path, sizeof(path), "assets/%s/history?interval=h6&start=%llu000&end=%llu000", coin.id, (unsigned long long)(now - 7 * 86400UL), (unsigned long long)now);
    const int code = cc_Get(path);
    if (code != HTTP_CODE_OK) return code;

    JsonDocument filter;
    filter["data"][0]["time"] = true;
    filter["data"][0]["priceUsd"] = true;
    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(cc_Http().getStream(), CC_READ_TIMEOUT_MS, 0);
    const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
    bodyBytes = reader.bytesReceived();
    if (error) return CC_BAD_BODY;

    uint16_t count = 0;
    for (JsonObjectConst point : doc["data"].as<JsonArrayConst>()){
        if (count == CC_HISTORY_POINTS) break;
        const double price = cc_Number(point["priceUsd"]);
        const uint32_t when = (uint32_t)((point["time"] | (int64_t)0) / 1000);
        if (isnan(price) || when == 0) continue;
        coin.history[count].time = when;
        coin.history[count].price = (float)price;
        count++;
    }
    coin.historyCount = count;
    if (coin.hasQuote) cc_Add_History(coin, coin.quoteTime, (float)coin.price);
    coin.chartTime = now;
    cc_Save_Coin(coin);
    return code;
}

static int cc_Fetch_Movers(uint32_t now, size_t& bodyBytes){
    const int code = cc_Get("agentFriendly/top_movers?direction=gainers&limit=3&topN=100");
    if (code != HTTP_CODE_OK) return code;

    JsonDocument filter;
    filter["data"][0]["symbol"] = true;
    filter["data"][0]["changePercent24Hr"] = true;
    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(cc_Http().getStream(), CC_READ_TIMEOUT_MS, 0);
    const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
    bodyBytes = reader.bytesReceived();
    if (error) return CC_BAD_BODY;

    uint8_t count = 0;
    for (JsonObjectConst mover : doc["data"].as<JsonArrayConst>()){
        if (count == CC_MAX_GAINERS) break;
        const double change = cc_Number(mover["changePercent24Hr"]);
        if (isnan(change)) continue;
        cc_Copy_Ascii(cryptoMarket.gainers[count].symbol, CC_SYMBOL_LEN, mover["symbol"] | "");
        cryptoMarket.gainers[count].changePct = (float)change;
        count++;
    }
    cryptoMarket.gainerCount = count;
    cryptoMarket.moversTime = now;
    cc_Save_Market();
    return code;
}

static int cc_Fetch_News(uint32_t now, size_t& bodyBytes){
    const int code = cc_Get("agentFriendly/news_top?limit=4");
    if (code != HTTP_CODE_OK) return code;

    JsonDocument doc;
    Mtb_Kit_Body_Reader_t reader(cc_Http().getStream(), CC_READ_TIMEOUT_MS, 0);
    const DeserializationError error = deserializeJson(doc, reader);
    bodyBytes = reader.bytesReceived();
    if (error) return CC_BAD_BODY;

    uint8_t count = 0;
    for (JsonVariantConst item : doc["data"].as<JsonArrayConst>()){
        if (count == CC_MAX_HEADLINES) break;
        const char* headline = item.as<const char*>();
        // The feed can list a news site's own title, which is not news.
        if (headline == nullptr || strstr(headline, "News and Price Data") != nullptr) continue;
        cc_Copy_Ascii(cryptoMarket.headlines[count], CC_HEADLINE_LEN, headline);
        if (cryptoMarket.headlines[count][0]) count++;
    }
    cryptoMarket.headlineCount = count;
    cryptoMarket.newsTime = now;
    cc_Save_Market();
    return code;
}

//##############################################################################################################
static Cc_Coin_t* cc_Coin_Slot(const char* id, const char* symbol){
    int freeSlot = -1;
    for (int i = 0; i <= MAX_COINS; i++){
        if (coinSlots[i] == nullptr) { if (freeSlot < 0) freeSlot = i; continue; }
        if (strcmp(coinSlots[i]->id, id) == 0) return coinSlots[i];
    }
    Cc_Coin_t* slot = nullptr;
    if (freeSlot >= 0){
        slot = (Cc_Coin_t*)heap_caps_malloc(sizeof(Cc_Coin_t), MALLOC_CAP_SPIRAM);
        if (slot != nullptr) coinSlots[freeSlot] = slot;
    } else {
        // All in use: take over one for a coin that has since left the watchlist.
        for (int i = 0; i <= MAX_COINS && slot == nullptr; i++){
            bool inUse = (coinSlots[i] == shownCoin);
            for (int w = 0; w < cryptoCount && !inUse; w++) inUse = (coinSlots[i] == watchedCoins[w]);
            if (!inUse) slot = coinSlots[i];
        }
    }
    if (slot == nullptr) return nullptr;
    memset(slot, 0, sizeof(Cc_Coin_t));
    slot->magic = CC_COIN_MAGIC;
    snprintf(slot->id, sizeof(slot->id), "%s", id);
    snprintf(slot->symbol, sizeof(slot->symbol), "%s", symbol);
    slot->price = slot->marketCap = slot->volume24h = slot->vwap24h = slot->supply = slot->maxSupply = NAN;
    cc_Load_Coin(*slot);
    return slot;
}

static void cc_Attach_Watchlist(void){
    for (int i = 0; i < cryptoCount; i++) watchedCoins[i] = cc_Coin_Slot(cryptoIDs[i].c_str(), cryptoSymbols[i].c_str());
}

static void cc_Show_Coin(const char* id, const char* symbol){
    Cc_Coin_t* coin = cc_Coin_Slot(id, symbol);
    if (coin == nullptr) return;
    shownCoin = coin;
    shownIndex = -1;
    for (int i = 0; i < cryptoCount; i++) if (watchedCoins[i] == coin) shownIndex = i;
    if (shownIndex >= 0) cryptoIterate = shownIndex;
    snprintf(currentCryptoCurrency.coinID, sizeof(currentCryptoCurrency.coinID), "%s", id);
    snprintf(currentCryptoCurrency.coinSymbol, sizeof(currentCryptoCurrency.coinSymbol), "%s", symbol);
    shownSinceSec = cc_Uptime_Sec();
}

static const Cc_Coin_t* cc_Bitcoin(void){
    for (int i = 0; i <= MAX_COINS; i++) if (coinSlots[i] != nullptr && strcmp(coinSlots[i]->id, "bitcoin") == 0) return coinSlots[i];
    return nullptr;
}

// Icons come from the project's asset repository. The repository holds them under the lower-case symbol.
static bool cc_Download_Icon(const char* symbol, const char* flashPath){
    char url[120], temporary[64];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/Meterbit/PXP_X1_STORAGE/HEAD/cryp_Icons/%s.png", symbol);
    snprintf(temporary, sizeof(temporary), "%s.tmp", flashPath);
    HTTPClient& http = cc_Http();
    http.useHTTP10(true);
    http.setConnectTimeout(6000);
    http.setTimeout(CC_READ_TIMEOUT_MS);
    if (!http.begin(cc_Tls_Client(), url)) return false;
    bool saved = false;
    const int code = http.GET();
    const int expected = http.getSize();
    if (code == HTTP_CODE_OK && expected > 0 && expected <= CC_MAX_ICON_BYTES && mtb_Prepare_Flash_File_Path(flashPath)){
        File file = LittleFS.open(temporary, "w");
        if (file){
            Mtb_Kit_Body_Reader_t reader(http.getStream(), CC_READ_TIMEOUT_MS, 0);
            uint8_t chunk[128];
            int total = 0;
            size_t got;
            while (total < expected && (got = reader.readBytes((char*)chunk, sizeof(chunk))) > 0){
                file.write(chunk, got);
                total += got;
            }
            file.close();
            saved = (total == expected) && LittleFS.rename(temporary, flashPath);
            if (!saved) LittleFS.remove(temporary);
        }
    }
    http.end();
    return saved;
}

static void cc_Check_Icon(Cc_Coin_t& coin, Mtb_LocalImage_t& icon){
    char lower[CC_SYMBOL_LEN];
    snprintf(lower, sizeof(lower), "%s", coin.symbol);
    for (char* c = lower; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
    snprintf(icon.imagePath, sizeof(icon.imagePath), "/crypto/%s.png", lower);
    if (coin.iconChecked) return;
    coin.hasIcon = LittleFS.exists(icon.imagePath);
    if (!coin.hasIcon && Mtb_Applications::internetConnectStatus == true){
        coin.hasIcon = cc_Download_Icon(lower, icon.imagePath);
    }
    coin.iconChecked = (Mtb_Applications::internetConnectStatus == true);
}

enum { CC_DIRTY_HERO = 1, CC_DIRTY_PAGE = 2, CC_DIRTY_WATCH = 4, CC_DIRTY_TICKER = 8 };

static bool cc_Quote_Missing(uint32_t upSec){
    return !shownCoin->hasQuote && !shownCoin->unknownToApi && (missingQuoteFetchSec == 0 || upSec - missingQuoteFetchSec >= CC_MISSING_QUOTE_SEC);
}

// The chart is only fetched while the coin is on screen, and leaves enough credits for the next price update.
static bool cc_Chart_Due(uint32_t now, uint32_t upSec){
    if (creditLedger.credits < CC_CHART_COST + CC_QUOTE_COST || shownCoin->unknownToApi) return false;
    if (upSec - shownSinceSec < CC_CHART_DWELL_SEC) return false;
    const uint32_t interval = (creditLedger.credits >= CC_CREDITS_LOW) ? CC_CHART_NORMAL_SEC : CC_CHART_SLOW_SEC;
    return now - shownCoin->chartTime >= interval;
}

static uint8_t cc_Run_Quotes(uint32_t now, uint32_t upSec){
    size_t bodyBytes = 0;
    if (!shownCoin->hasQuote) missingQuoteFetchSec = upSec;
    const int code = cc_Fetch_Quotes(now, bodyBytes);
    if (code == CC_NO_REQUEST) return 0;
    if (!cc_Check_Response(code, bodyBytes, upSec)) return CC_DIRTY_TICKER;
    return CC_DIRTY_HERO | CC_DIRTY_PAGE | CC_DIRTY_WATCH;
}

// Runs the most useful single request and reports which parts of the screen it changed. A price that is missing or
// overdue comes first. News and the market's top gainers then go ahead of a routine price update: at the balance a
// device left on this app settles at, they would otherwise never get a turn.
static uint8_t cc_Run_Next_Request(uint32_t now, uint32_t upSec){
    size_t bodyBytes = 0;
    const bool canAffordQuote = creditLedger.credits >= CC_QUOTE_COST;
    const uint32_t quoteAge = now - cryptoMarket.quotesTime;
    if (canAffordQuote && (cc_Quote_Missing(upSec) || quoteAge >= CC_QUOTE_SLOW_SEC)) return cc_Run_Quotes(now, upSec);

    if (creditLedger.credits >= CC_CREDITS_LOW){
        if (now - cryptoMarket.newsTime >= CC_NEWS_SEC){
            const int code = cc_Fetch_News(now, bodyBytes);
            return cc_Check_Response(code, bodyBytes, upSec) ? 0 : CC_DIRTY_TICKER;
        }
        if (now - cryptoMarket.moversTime >= CC_MOVERS_SEC){
            const int code = cc_Fetch_Movers(now, bodyBytes);
            return cc_Check_Response(code, bodyBytes, upSec) ? CC_DIRTY_PAGE : CC_DIRTY_TICKER;
        }
    }
    if (canAffordQuote && quoteAge >= cc_Quote_Interval()) return cc_Run_Quotes(now, upSec);
    if (cc_Chart_Due(now, upSec)){
        const int code = cc_Fetch_Chart(*shownCoin, now, bodyBytes);
        if (!cc_Check_Response(code, bodyBytes, upSec)) return CC_DIRTY_TICKER;
        return CC_DIRTY_PAGE;
    }
    return 0;
}

// What the bottom row should say next: a problem if there is one, otherwise news, the coin's facts and the credit balance in turn.
static bool cc_Next_Ticker_Text(const Cc_Coin_t& coin, uint8_t& item, char* text, size_t size, uint16_t& color){
    if (statusMessage[0]){
        snprintf(text, size, "%s", statusMessage);
        color = ORANGE;
        return true;
    }
    const uint8_t items = cryptoMarket.headlineCount + 2;
    if (item >= items) item = 0;
    const uint8_t current = item++;
    if (current < cryptoMarket.headlineCount){
        snprintf(text, size, "%s", cryptoMarket.headlines[current]);
        color = YELLOW;
        return true;
    }
    if (current == cryptoMarket.headlineCount){
        if (!coin.hasQuote) return false;
        char supply[14], maxSupply[14], volume[14];
        cc_Ui_Format_Big(supply, sizeof(supply), coin.supply);
        cc_Ui_Format_Big(maxSupply, sizeof(maxSupply), coin.maxSupply);
        cc_Ui_Format_Big(volume, sizeof(volume), coin.volume24h);
        if (coin.maxSupply > 0) snprintf(text, size, "%s is #%u by market cap  -  Supply %s of %s %s  -  24h volume $%s", coin.name, coin.rank, supply, maxSupply, coin.symbol, volume);
        else snprintf(text, size, "%s is #%u by market cap  -  Supply %s %s  -  24h volume $%s", coin.name, coin.rank, supply, coin.symbol, volume);
        color = CYAN;
        return true;
    }
    const uint32_t now = cc_Now();
    const uint32_t interval = cc_Quote_Interval();
    const uint32_t since = (now > cryptoMarket.quotesTime) ? now - cryptoMarket.quotesTime : 0;
    char every[16], next[16];
    cc_Ui_Format_Duration(every, sizeof(every), interval);
    cc_Ui_Format_Duration(next, sizeof(next), since < interval ? interval - since : 0);
    snprintf(text, size, "Prices refresh every %s to stay inside %u credits a month  -  Next in %s  -  %d credits banked", every, (unsigned)cc_Monthly_Credits(), next, (int)creditLedger.credits);
    color = GREEN;
    return true;
}

//##############################################################################################################
static void cc_Read_Settings(Crypto_Stat_t& s){
    Crypto_Stat_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(CC_NVS_KEY, &saved, sizeof(saved));
    if (saved.magic == CC_SETTINGS_MAGIC) s = saved;
    else {
        memset(&s, 0, sizeof(s));
        s.magic = CC_SETTINGS_MAGIC;
        snprintf(s.coinID, sizeof(s.coinID), "bitcoin");
        snprintf(s.coinSymbol, sizeof(s.coinSymbol), "BTC");
        s.apiToken[0] = '\0';           // The user's own CoinCap key, sent from the phone.
        s.cryptoChangeInterval = 30;
        s.autoCycle = 1;
        s.monthlyCredits = CC_FREE_CREDITS;
        // The user's key and coin from before the phone UI.
        Crypto_Stat_Old_t old;
        memset(&old, 0, sizeof(old));
        mtb_Read_Nvs_Struct("cryptoCfg", &old, sizeof(old));
        old.coinID[CC_ID_LEN - 1] = old.coinSymbol[CC_SYMBOL_LEN - 1] = old.apiToken[sizeof(old.apiToken) - 1] = '\0';
        if (cc_Id_Valid(old.coinID) && old.coinSymbol[0]){ memcpy(s.coinID, old.coinID, sizeof(s.coinID)); memcpy(s.coinSymbol, old.coinSymbol, sizeof(s.coinSymbol)); }
        if (strlen(old.apiToken) >= 16) memcpy(s.apiToken, old.apiToken, sizeof(s.apiToken));
        if (old.cryptoChangeInterval >= 10 && old.cryptoChangeInterval <= 600) s.cryptoChangeInterval = old.cryptoChangeInterval;
        memset(&old, 0, sizeof(old));
    }
    s.coinID[CC_ID_LEN - 1] = s.coinSymbol[CC_SYMBOL_LEN - 1] = s.apiToken[sizeof(s.apiToken) - 1] = '\0';
    mtb_Kit_Clear_Retired_Key(s.apiToken, sizeof(s.apiToken));
    if (!cc_Id_Valid(s.coinID)){ snprintf(s.coinID, sizeof(s.coinID), "bitcoin"); snprintf(s.coinSymbol, sizeof(s.coinSymbol), "BTC"); }
    if (s.cryptoChangeInterval < 10 || s.cryptoChangeInterval > 600) s.cryptoChangeInterval = 30;
    s.autoCycle = s.autoCycle ? 1 : 0;
    if (s.monthlyCredits < CC_FREE_CREDITS) s.monthlyCredits = CC_FREE_CREDITS;
    if (s.monthlyCredits > 10000000) s.monthlyCredits = 10000000;
}

static void cc_Save_Settings(void){ mtb_Write_Nvs_Struct(CC_NVS_KEY, &currentCryptoCurrency, sizeof(Crypto_Stat_t)); }

// The phone's cycle settings, applied on the app task (which owns the timer).
static void cc_Apply_Cycle(void){
    const int8_t cycle = pendingCycle;
    const int16_t interval = pendingInterval;
    pendingCycle = -1;
    pendingInterval = 0;
    if (interval >= 10 && interval <= 600 && interval != currentCryptoCurrency.cryptoChangeInterval){
        currentCryptoCurrency.cryptoChangeInterval = interval;
        xTimerChangePeriod(cryptoChangeTimer_H, pdMS_TO_TICKS(interval * 1000), 0);
        if (!currentCryptoCurrency.autoCycle) xTimerStop(cryptoChangeTimer_H, 0);
    }
    if (cycle >= 0){
        currentCryptoCurrency.autoCycle = (uint8_t)cycle;
        if (cycle) xTimerStart(cryptoChangeTimer_H, 0);
        else xTimerStop(cryptoChangeTimer_H, 0);
    }
    cc_Save_Settings();
}

//##############################################################################################################
void cryptoStats_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(buttonChangeDisplayCrypto, mtb_Brightness_Control);
    // 4 and 5 were the coin searches; the phone searches its own list now. They stay empty so 6 keeps its number.
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(showParticularCrypto, add_RemoveCryptoSymbol, setCryptoChangeInterval, setCrytoAPI_key, nullptr,
                                       nullptr, cc_Ble_Credits);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    cc_Read_Settings(currentCryptoCurrency);

    creditLedger.credits = CC_CREDITS_FIRST_RUN;
    creditLedger.updated = 0;
    mtb_Read_Nvs_Struct("cryptoCredits", &creditLedger, sizeof(creditLedger));

    if(cryptoChangeTimer_H == NULL) cryptoChangeTimer_H = xTimerCreate("cryptoIntvTim", pdMS_TO_TICKS(currentCryptoCurrency.cryptoChangeInterval>0 ? (currentCryptoCurrency.cryptoChangeInterval * 1000) : (30 * 1000)), true, NULL, crytoChange_TimerCallback);

    Mtb_ScrollText_t ticker(0, CC_TICKER_Y, PANEL_RES_X, Terminal6x8, YELLOW, 20, 1);
    //*********************************************************************************************************

    // Read cryto symbols and IDs from the CSV files
    int symbolCount = 0, idCount = 0;
    readCryptoSymbols(cryptoSymbolsFilePath, cryptoSymbols, symbolCount, MAX_COINS);
    readCryptoSymbols(cryptoIDsFilePath, cryptoIDs, idCount, MAX_COINS);

    // If no symbols are found, write default symbols to the file
    if (symbolCount == 0 || idCount == 0) {
        File file = LittleFS.open(cryptoSymbolsFilePath, "w");
        if (file) {
            file.print(String("BTC,ETH,DOGE"));
            file.close();
        } else {
            ESP_LOGI(TAG, "Failed to write default symbols to file.\n");
        }

        File file2 = LittleFS.open(cryptoIDsFilePath, "w");
        if (file2) {
            file2.print(String("bitcoin,ethereum,dogecoin"));
            file2.close();
        } else {
            ESP_LOGI(TAG, "Failed to write default IDs to file.\n");
        }
        readCryptoSymbols(cryptoSymbolsFilePath, cryptoSymbols, symbolCount, MAX_COINS);
        readCryptoSymbols(cryptoIDsFilePath, cryptoIDs, idCount, MAX_COINS);
    }
    cryptoCount = (symbolCount < idCount) ? symbolCount : idCount;

    if (currentCryptoCurrency.autoCycle) xTimerStart(cryptoChangeTimer_H, 0);
//##############################################################################################################
    coinStepsRequested = 0;
    coinRequested = false;
    watchlistChanged = false;
    autoCycleToggled = false;
    cycleChanged = false;
    pendingCycle = -1;
    pendingInterval = 0;
    ccKeyRejected = false;
    ccKeyAccepted = false;
    requestsBlockedUntil = 0;
    missingQuoteFetchSec = 0;
    networkFails = 0;
    statusMessage[0] = '\0';

    cc_Load_Market();
    cc_Attach_Watchlist();
    cc_Show_Coin(currentCryptoCurrency.coinID, currentCryptoCurrency.coinSymbol);

    Mtb_LocalImage_t icon;
    icon.xAxis = 3; icon.yAxis = 11; icon.scale = 1;
    uint8_t page = 0;
    uint8_t tickerItem = 0;
    uint32_t pageChangeMs = millis();
    uint32_t nextRequest = 0;
    uint32_t nextTickerCheckMs = 0;
    uint32_t heroMinute = 0;
    bool drawIcon = true;
    uint8_t dirty = CC_DIRTY_HERO | CC_DIRTY_PAGE | CC_DIRTY_WATCH | CC_DIRTY_TICKER;
    bool wholeScreen = true;
    char tickerText[CC_HEADLINE_LEN + 120];
    uint16_t tickerColor = YELLOW;

    for (;;){
        const uint32_t beforeCheckpoint = millis();
        if (THIS_APP_IS_ACTIVE != pdTRUE) break;
        // The checkpoint only takes this long if the app was paused, and then the screen has been used by something else.
        if (millis() - beforeCheckpoint > 300) { wholeScreen = true; drawIcon = true; }
        if (shownCoin == nullptr){     // Out of PSRAM when the coin was chosen: keep trying.
            cc_Show_Coin(currentCryptoCurrency.coinID, currentCryptoCurrency.coinSymbol);
            delay(1000);
            continue;
        }

        if (watchlistChanged){
            watchlistChanged = false;
            readCryptoSymbols(cryptoSymbolsFilePath, cryptoSymbols, symbolCount, MAX_COINS);
            readCryptoSymbols(cryptoIDsFilePath, cryptoIDs, idCount, MAX_COINS);
            cryptoCount = (symbolCount < idCount) ? symbolCount : idCount;
            cc_Attach_Watchlist();
            cc_Show_Coin(currentCryptoCurrency.coinID, currentCryptoCurrency.coinSymbol);
            dirty |= CC_DIRTY_WATCH | CC_DIRTY_PAGE;
            coinCap_Stats_App->mtb_App_Push_Mobile_Values();
        }
        if (cycleChanged){
            cycleChanged = false;
            cc_Apply_Cycle();
            dirty |= CC_DIRTY_PAGE;
        }
        if (autoCycleToggled){
            autoCycleToggled = false;
            if (xTimerIsTimerActive(cryptoChangeTimer_H) == pdTRUE) xTimerStop(cryptoChangeTimer_H, 0);
            else xTimerStart(cryptoChangeTimer_H, 0);
            currentCryptoCurrency.autoCycle = xTimerIsTimerActive(cryptoChangeTimer_H) == pdTRUE;
            cc_Save_Settings();
            dirty |= CC_DIRTY_PAGE;
            coinCap_Stats_App->mtb_App_Push_Mobile_Values();
        }
        const uint8_t steps = coinStepsRequested;
        if (coinRequested || (steps > 0 && cryptoCount > 0)){
            if (coinRequested){
                coinRequested = false;
                cc_Show_Coin(requestedCoinID, requestedCoinSymbol);
            } else {
                coinStepsRequested = 0;
                cryptoIterate = (cryptoIterate + steps) % cryptoCount;
                cc_Show_Coin(cryptoIDs[cryptoIterate].c_str(), cryptoSymbols[cryptoIterate].c_str());
            }
            // Only a choice made by the user is stored: the timer would wear the flash with a write per cycle.
            if (coinStepPersist){
                coinStepPersist = 0;
                cc_Save_Settings();
            }
            coinCap_Stats_App->mtb_App_Push_Mobile_Values();
            drawIcon = true;
            dirty |= CC_DIRTY_HERO | CC_DIRTY_PAGE | CC_DIRTY_WATCH | CC_DIRTY_TICKER;
        }

        const uint32_t now = cc_Now();
        const uint32_t upSec = cc_Uptime_Sec();
        cc_Credits_Accrue(now);
        // Credits are worked out from the clock, so nothing is requested until the clock has been set.
        if (Mtb_Applications::internetConnectStatus == true && now != 0 && !drawIcon && upSec >= nextRequest && upSec >= requestsBlockedUntil){
            const char hadStatus = statusMessage[0];
            dirty |= cc_Run_Next_Request(now, upSec);
            if ((hadStatus != 0) != (statusMessage[0] != 0)) dirty |= CC_DIRTY_TICKER;
            nextRequest = cc_Uptime_Sec() + CC_REQUEST_GAP_SEC;
        }

        // Pages share the time a coin is on screen, so a full auto-cycle shows them all.
        const uint32_t cycleSec = (xTimerIsTimerActive(cryptoChangeTimer_H) == pdTRUE) ? currentCryptoCurrency.cryptoChangeInterval : 40;
        uint32_t pageMs = cycleSec * 1000 / CC_PAGE_COUNT;
        if (pageMs < 5000) pageMs = 5000;
        if (pageMs > 15000) pageMs = 15000;
        if (millis() - pageChangeMs >= pageMs){
            pageChangeMs = millis();
            page = (page + 1) % CC_PAGE_COUNT;
            dirty |= CC_DIRTY_PAGE;
        }

        // The age of the price, on the top row.
        if (now / 60 != heroMinute){
            heroMinute = now / 60;
            dirty |= CC_DIRTY_HERO;
        }

        if (drawIcon) cc_Check_Icon(*shownCoin, icon);
        if (wholeScreen) dirty |= CC_DIRTY_HERO | CC_DIRTY_PAGE | CC_DIRTY_WATCH;
        if ((dirty & (CC_DIRTY_HERO | CC_DIRTY_PAGE | CC_DIRTY_WATCH)) || drawIcon){
            mtb_Panel_Begin_Update();
            if (wholeScreen) cc_Ui_Clear_Content();
            if (drawIcon) cc_Ui_Draw_Icon_Placeholder(*shownCoin);
            if (dirty & CC_DIRTY_HERO) cc_Ui_Draw_Hero(*shownCoin, now);
            if (dirty & CC_DIRTY_PAGE) cc_Ui_Draw_Page(page, *shownCoin, watchedCoins, cryptoCount, cc_Bitcoin(), cryptoMarket, now != 0 ? now : shownCoin->quoteTime, xTimerIsTimerActive(cryptoChangeTimer_H) != pdTRUE);
            if (dirty & CC_DIRTY_WATCH) cc_Ui_Draw_Watchlist(watchedCoins, cryptoCount, shownIndex);
            mtb_Panel_End_Update();
            if (drawIcon && shownCoin->hasIcon) mtb_Draw_Local_Png(icon);
        }
        const bool restartTicker = (dirty & CC_DIRTY_TICKER) != 0;
        wholeScreen = false;
        drawIcon = false;
        dirty = 0;

        if (restartTicker){
            ticker.mtb_Scroll_Active(STOP_SCROLL);
            nextTickerCheckMs = 0;
        }
        if ((int32_t)(millis() - nextTickerCheckMs) >= 0){
            nextTickerCheckMs = millis() + 500;
            if (!ticker.mtb_Scroll_Active() && cc_Next_Ticker_Text(*shownCoin, tickerItem, tickerText, sizeof(tickerText), tickerColor)){
                ticker.mtb_Scroll_This_Text(tickerText, tickerColor);
            }
        }
        delay(50);
    }

    ticker.mtb_Scroll_Active(STOP_SCROLL);

    // Clean up and free resources
    xTimerDelete(cryptoChangeTimer_H, 0);
    cryptoChangeTimer_H = NULL;
    shownCoin = nullptr;

  mtb_Delete_This_App(THIS_APP);
}
//##############################################################################################################

// Streaming CSV reader: avoids readString()/substring(), minimizes heap churn.
void readCryptoSymbols(const char* filename, String coinSymbols[], int& count, const int maxSymbols){
    count = 0;

    if (!mtb_Prepare_Flash_File_Path(filename)) {
        ESP_LOGI(TAG, "Path prep failed");
        return;
    }

    // Create file if it doesn't exist (empty file: 0 symbols)
    if (!LittleFS.exists(filename)) {
        File newFile = LittleFS.open(filename, "w");
        if (newFile) newFile.close();
        return;
    }

    File file = LittleFS.open(filename, "r");
    if (!file) return;

    String token;
    token.reserve(32);

    while (count < maxSymbols && file.available()) {
        int c = file.read();
        if (c == ',' || c == '\n' || c == '\r' || c < 0) {
            token.trim();
            if (token.length()) coinSymbols[count++] = token;
            token = "";
        } else {
            token += (char)c;
        }
    }

    // last token if file didn't end with comma/newline
    token.trim();
    if (count < maxSymbols && token.length()) coinSymbols[count++] = token;

    file.close();
}

void buttonChangeDisplayCrypto(button_event_t button_Data){
            switch (button_Data.type){
            case BUTTON_RELEASED:
            break;

            case BUTTON_PRESSED:
                coinStepPersist = 1;
                coinStepsRequested = coinStepsRequested + 1;
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

void crytoChange_TimerCallback(TimerHandle_t coinPrompt){
    coinStepsRequested = coinStepsRequested + 1;
}

// Writes the watchlist files (symbols and CoinCap ids, in step); the app task reads them back.
static bool cc_Write_Watchlist(const Cc_Coin_Ref_t* coins, int count){
    if (!mtb_Prepare_Flash_File_Path(cryptoSymbolsFilePath)) return false;
    File symbols = LittleFS.open(cryptoSymbolsFilePath, "w");
    File ids = LittleFS.open(cryptoIDsFilePath, "w");
    const bool ok = symbols && ids;
    for (int i = 0; ok && i < count; i++){
        if (i){ symbols.print(","); ids.print(","); }
        symbols.print(coins[i].symbol);
        ids.print(coins[i].id);
    }
    if (symbols) symbols.close();
    if (ids) ids.close();
    if (ok) watchlistChanged = true;
    return ok;
}

// A coin from the phone: {"name", "value": "bitcoin|BTC"}.
static bool cc_Coin_Of(JsonVariantConst item, Cc_Coin_Ref_t& coin){
    memset(&coin, 0, sizeof(coin));
    return cc_Parse_Value(item["value"] | "", coin.id, sizeof(coin.id), coin.symbol, sizeof(coin.symbol));
}

// Command 0: show a coin now: {"coin": {"name", "value": "solana|SOL"}}, or the old {"coinID", "coinSymbol"}.
void showParticularCrypto(JsonDocument& dCommand){
    Cc_Coin_Ref_t coin;
    if (dCommand["coin"].is<JsonObject>()){
        if (!cc_Coin_Of(dCommand["coin"], coin)){ mtb_Ble_App_Cmd_Reject(); return; }
    } else {
        const char* id = dCommand["coinID"];
        const char* symbol = dCommand["coinSymbol"];
        if (!cc_Id_Valid(id) || symbol == nullptr || symbol[0] == '\0' || strlen(symbol) >= CC_SYMBOL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
        snprintf(coin.id, sizeof(coin.id), "%s", id);
        snprintf(coin.symbol, sizeof(coin.symbol), "%s", symbol);
    }
    snprintf(requestedCoinSymbol, sizeof(requestedCoinSymbol), "%s", coin.symbol);
    snprintf(requestedCoinID, sizeof(requestedCoinID), "%s", coin.id);
    coinStepPersist = 1;
    coinRequested = true;
}

// Command 1: the watchlist: {"coins": [{"name", "value": "bitcoin|BTC"}, ...]}, or the old page's
// {"coinSymbols": "[BTC, ETH]", "coinIDs": "[bitcoin, ethereum]"}.
void add_RemoveCryptoSymbol(JsonDocument& dCommand){
    Cc_Coin_Ref_t* coins = (Cc_Coin_Ref_t*)heap_caps_calloc(MAX_COINS, sizeof(Cc_Coin_Ref_t), MALLOC_CAP_SPIRAM);
    if (coins == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    int count = 0;
    bool valid = true;
    auto add = [&](const Cc_Coin_Ref_t& coin){
        for (int k = 0; k < count; k++) if (strcmp(coins[k].id, coin.id) == 0) return;
        if (count >= MAX_COINS){ valid = false; return; }
        coins[count++] = coin;
    };
    if (dCommand["coins"].is<JsonArray>()){
        for (JsonVariant item : dCommand["coins"].as<JsonArray>()){
            Cc_Coin_Ref_t coin;
            if (!cc_Coin_Of(item, coin)){ valid = false; break; }
            add(coin);
        }
    } else if (dCommand["coinSymbols"].is<const char*>() && dCommand["coinIDs"].is<const char*>()){
        // The phone's former lists, as text, in step.
        char symbolText[MAX_COINS * 8], idText[MAX_COINS * 24];
        snprintf(symbolText, sizeof(symbolText), "%s", dCommand["coinSymbols"].as<const char*>());
        snprintf(idText, sizeof(idText), "%s", dCommand["coinIDs"].as<const char*>());
        for (char* c = symbolText; *c; c++) if (*c == '[' || *c == ']' || *c == '"') *c = ' ';
        for (char* c = idText; *c; c++) if (*c == '[' || *c == ']' || *c == '"') *c = ' ';
        char *symbolSave = nullptr, *idSave = nullptr;
        char* symbol = strtok_r(symbolText, ", ", &symbolSave);
        char* id = strtok_r(idText, ", ", &idSave);
        while (symbol && id && valid){
            Cc_Coin_Ref_t coin = {};
            if (!cc_Id_Valid(id) || strlen(symbol) >= CC_SYMBOL_LEN){ valid = false; break; }
            snprintf(coin.id, sizeof(coin.id), "%s", id);
            snprintf(coin.symbol, sizeof(coin.symbol), "%s", symbol);
            add(coin);
            symbol = strtok_r(nullptr, ", ", &symbolSave);
            id = strtok_r(nullptr, ", ", &idSave);
        }
    } else valid = false;
    if (!valid || count == 0 || !cc_Write_Watchlist(coins, count)) mtb_Ble_App_Cmd_Reject();
    heap_caps_free(coins);
}

// Command 2: {"cycleCoins": 0-1} and/or {"dInterval": 10-600} seconds per coin.
void setCryptoChangeInterval(JsonDocument& dCommand){
    long cycle = -1, interval = 0;
    const bool hasCycle = mtb_Kit_Json_Range(dCommand["cycleCoins"], 0, 1, cycle);
    const bool hasInterval = mtb_Kit_Json_Range(dCommand["dInterval"], 10, 600, interval);
    if (!hasCycle && !hasInterval){ mtb_Ble_App_Cmd_Reject(); return; }
    if (hasCycle) pendingCycle = (int8_t)cycle;
    if (hasInterval) pendingInterval = (int16_t)interval;
    cycleChanged = true;
}

// Command 3: {"api_key": "..."}
void setCrytoAPI_key(JsonDocument& dCommand){
    const char* key = dCommand["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length >= 16 && length < sizeof(currentCryptoCurrency.apiToken);
    for (size_t i = 0; valid && i < length; i++) if (!(isalnum((unsigned char)key[i]) || key[i] == '-' || key[i] == '_')) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Crypto_Stat_t s;
    cc_Read_Settings(s);
    memset(s.apiToken, 0, sizeof(s.apiToken));
    memcpy(s.apiToken, key, length);
    mtb_Write_Nvs_Struct(CC_NVS_KEY, &s, sizeof(s));
    memcpy(currentCryptoCurrency.apiToken, s.apiToken, sizeof(s.apiToken));    // Used from the next request.
    memset(&s, 0, sizeof(s));
    ccKeyRejected = false;
    ccCallsPerMinute = 0;
    requestsBlockedUntil = 0;
    statusMessage[0] = '\0';
}

// Command 6: {"credits": 500-10000000}: a paid plan's credits a month. The free plan stays at 500 whatever is set.
static void cc_Ble_Credits(JsonDocument& dCommand){
    long credits;
    if (!mtb_Kit_Json_Range(dCommand["credits"], CC_FREE_CREDITS, 10000000, credits)){ mtb_Ble_App_Cmd_Reject(); return; }
    Crypto_Stat_t s;
    cc_Read_Settings(s);
    s.monthlyCredits = (uint32_t)credits;
    mtb_Write_Nvs_Struct(CC_NVS_KEY, &s, sizeof(s));
    currentCryptoCurrency.monthlyCredits = s.monthlyCredits;
    memset(&s, 0, sizeof(s));
}

// What the phone shows. The key never leaves the device. Runs on the BLE parser task.
static void cc_Mobile_Ui_Values(JsonObject values){
    Crypto_Stat_t* s = (Crypto_Stat_t*)heap_caps_malloc(sizeof(Crypto_Stat_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr) return;
    cc_Read_Settings(*s);
    // The watchlist, from its files.
    JsonArray coins = values["coins"].to<JsonArray>();
    File symbols = LittleFS.open(cryptoSymbolsFilePath, "r");
    File ids = LittleFS.open(cryptoIDsFilePath, "r");
    int listed = 0;
    if (symbols && ids){
        auto next = [](File& file, char* dest, size_t size) -> bool {
            size_t n = 0;
            int c;
            while ((c = file.read()) >= 0 && c != ','){ if (c != ' ' && c != '\n' && c != '\r' && n + 1 < size) dest[n++] = (char)c; }
            dest[n] = '\0';
            return n > 0;
        };
        char symbol[CC_SYMBOL_LEN], id[CC_ID_LEN];
        while (listed < MAX_COINS && next(symbols, symbol, sizeof(symbol)) && next(ids, id, sizeof(id))){
            char value[56];
            snprintf(value, sizeof(value), "%s|%s", id, symbol);
            JsonObject item = coins.add<JsonObject>();
            item["name"] = symbol;
            item["value"] = value;
            listed++;
        }
    }
    if (symbols) symbols.close();
    if (ids) ids.close();
    JsonObject shown = values["coin"].to<JsonObject>();
    char value[56];
    snprintf(value, sizeof(value), "%s|%s", s->coinID, s->coinSymbol);
    shown["name"] = s->coinSymbol;
    shown["value"] = value;
    values["cycleCoins"] = s->autoCycle;
    values["dInterval"] = s->cryptoChangeInterval;
    values["credits"] = s->monthlyCredits;
    values["api_key_set"] = s->apiToken[0] ? 1 : 0;
    const Cc_Coin_t* coin = shownCoin;
    if (coin != nullptr && coin->hasQuote){
        char text[80];
        snprintf(text, sizeof(text), "%s $%.*f, %+.2f%% in 24 h", coin->symbol, coin->price >= 100 ? 2 : coin->price >= 1 ? 3 : 6, coin->price, coin->changePct);
        values["shownText"] = text;
        values["shownState"] = coin->changePct > 0 ? "ok" : coin->changePct < 0 ? "error" : "info";
    }
    const uint16_t perMinute = ccCallsPerMinute;
    const bool paid = perMinute > CC_FREE_CALLS_PER_MIN;
    char plan[64];
    if (perMinute == 0) snprintf(plan, sizeof(plan), "Checking");
    else if (paid) snprintf(plan, sizeof(plan), "Paid: %u calls a minute", (unsigned)perMinute);
    else snprintf(plan, sizeof(plan), "Free: 500 credits a month");
    values["plan"] = plan;
    values["planState"] = perMinute == 0 ? "off" : paid ? "ok" : "info";
    values["paid"] = paid ? 1 : 0;
    const uint32_t monthly = paid && s->monthlyCredits > CC_FREE_CREDITS ? s->monthlyCredits : CC_FREE_CREDITS;
    values["budget"] = monthly;
    values["banked"] = (int)creditLedger.credits;
    values["bankMax"] = (int)(45.0f * monthly / CC_FREE_CREDITS);
    char pace[64], every[16];
    cc_Ui_Format_Duration(every, sizeof(every), cc_Quote_Interval());
    snprintf(pace, sizeof(pace), "Prices every %s at the current balance", every);
    values["pace"] = pace;
    if (ccKeyRejected){ values["keyText"] = "Not accepted by CoinCap"; values["keyState"] = "error"; }
    else if (ccKeyAccepted){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    values["watchCount"] = listed;
    if (ccLastAnswerUnix) values["updated"] = ccLastAnswerUnix;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}
