/**
 * @file pfx_Model.h
 * @brief FX Markets' model: settings, the currency pairs and their prices, readers of Massive's answers, the plan,
 *        the request pace, the pair search, and the world's forex trading sessions (with daylight saving).
 *        No Arduino, so it can be tested on a PC.
 *
 * Massive (formerly Polygon.io; checked 6 November 2026), https://api.massive.com, key as &apiKey=:
 *   Currencies Basic, free: 5 calls a minute, end-of-day data, 2 years of history: daily bars
 *   /v2/aggs/ticker/C:EURUSD/range/1/day/{from}/{to}, previous close .../prev.
 *   Currencies Starter, $49 a month: unlimited calls, real-time data, snapshots
 *   /v2/snapshot/locale/global/markets/forex/tickers?tickers=C:EURUSD,..., minute bars, quotes.
 *   A plan without access: 403 {"status":"NOT_AUTHORIZED","message":"You are not entitled to this data..."};
 *   a bad key: 401 {"status":"ERROR","error":"Unknown API Key"}; too many calls: 429.
 *   Licence (individual plans): personal, non-commercial use, not to be shown to others.
 */
#ifndef PFX_MODEL_H
#define PFX_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define PFX_SETTINGS_MAGIC  0x50465831UL    // "PFX1"
#define PFX_NVS_KEY         "polygonFxCfg"
#define PFX_PAIRS           8
#define PFX_DAYS            30
#define PFX_INTRADAY        96
#define PFX_KEY_LEN         72

struct Pfx_Pair_Code_t { char from[4]; char to[4]; };

struct Pfx_Settings_t {
    uint32_t magic;
    uint8_t pairCount;
    Pfx_Pair_Code_t pairs[PFX_PAIRS];
    uint8_t focus;                  /**< The pair the chart shows. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t intraday;               /**< The chart shows today's 15-minute bars when the plan has them. */
    char apiKey[PFX_KEY_LEN];
};

struct Pfx_Bar_t { float o, h, l, c; uint32_t t; };

struct Pfx_Quote_t {
    uint8_t valid;
    uint8_t realtime;               /**< From a snapshot (paid plan); else the last daily close. */
    double last, prevClose, dayHigh, dayLow;
    uint32_t updated;               /**< Unix time of the price. */
    uint8_t barCount;
    Pfx_Bar_t bars[PFX_DAYS];       /**< Daily, oldest first. */
    uint8_t error;                  /**< The pair was refused (not a market). */
};

enum Pfx_Result_t : uint8_t { PFX_OK = 0, PFX_NET, PFX_PARSE, PFX_KEY, PFX_PLAN, PFX_RATE, PFX_BAD, PFX_SERVER };

struct Pfx_Data_t {
    Pfx_Quote_t quotes[PFX_PAIRS];
    uint8_t intradayCount;
    uint8_t intradayPair;
    Pfx_Bar_t intraday[PFX_INTRADAY];
    uint8_t planKnown;
    uint8_t realtime;               /**< The key has snapshots: Starter or higher. */
    uint32_t planChecked;
    uint32_t callsToday;
    uint32_t callsDay;
    uint8_t lastError;
    char errorText[96];
};

void pfx_Settings_Default(Pfx_Settings_t& settings);
void pfx_Settings_Clamp(Pfx_Settings_t& settings);
bool pfx_Code_Valid(const char* code);
bool pfx_Key_Valid(const char* key);
/** @brief "EUR/USD", "eurusd", "EUR USD" -> the pair. */
bool pfx_Parse_Pair(const char* text, Pfx_Pair_Code_t& pair);
void pfx_Pair_Text(const Pfx_Pair_Code_t& pair, char* dest, size_t size);

/** @brief Up to @p max pairs for a search: currency codes or names ("naira", "yen", "eur gbp"). */
struct Pfx_Result_Item_t { char pair[8]; char detail[48]; };
int pfx_Search(const char* query, Pfx_Result_Item_t* results, int max);
const char* pfx_Currency_Name(const char* code);

/** @brief Decimals a price is shown with: 2 for 100 and up, 3 from 10, 4 from 1, else 5. */
int pfx_Decimals(double price);

// Answers.
uint8_t pfx_Error_Of(int httpCode, JsonVariantConst doc, char* text, size_t size);
/** @brief Daily bars (range/1/day) into a quote: the bars, the last close as the price, the one before as the
 *         previous close. */
uint8_t pfx_Parse_Daily(JsonVariantConst doc, Pfx_Quote_t& quote);
/** @brief A snapshot of several tickers into the quotes of the matching pairs. */
uint8_t pfx_Parse_Snapshot(JsonVariantConst doc, const Pfx_Settings_t& settings, Pfx_Data_t& data, uint32_t now);
uint8_t pfx_Parse_Intraday(JsonVariantConst doc, Pfx_Data_t& data);

/** @brief "2026-11-06" of a Unix time, UTC. */
void pfx_Date(char* dest, size_t size, uint32_t unixTime);

// Sessions.
enum Pfx_Session_t : uint8_t { PFX_SYDNEY = 0, PFX_TOKYO, PFX_LONDON, PFX_NEW_YORK, PFX_SESSIONS };
struct Pfx_Session_Times_t { uint32_t open, close; };      /**< Unix times of the session's day around @p now. */
const char* pfx_Session_Name(uint8_t session);
/** @brief The session that contains @p now, or the next one today, from 08:00 to 17:00 local time (Tokyo 09:00
 *         to 18:00), with each city's daylight saving. */
Pfx_Session_Times_t pfx_Session(uint8_t session, uint32_t now);
bool pfx_Session_Open(uint8_t session, uint32_t now);
/** @brief The forex week: open from Sunday 17:00 to Friday 17:00 New York time. */
bool pfx_Market_Open(uint32_t now);
/** @brief Seconds until the market next opens (0 while open). */
uint32_t pfx_Until_Open(uint32_t now);
/** @brief UTC offset in seconds of each city at @p now. */
int32_t pfx_City_Offset(uint8_t session, uint32_t now);

#endif
