/**
 * @file cfx_Model.h
 * @brief Currency Exchange's model: the currencies it names, the rate table, daily history, the plan, and number
 *        formatting. No network and no Arduino, so it can be tested on a PC.
 *
 * ExchangeRate-API (checked 6 November 2026):
 *   keyless   https://open.er-api.com/v6/latest/USD   daily, attribution required, rate limited ("rates")
 *   key       https://v6.exchangerate-api.com/v6/KEY/latest/USD   ("conversion_rates")
 *             .../v6/KEY/quota   {plan_quota, requests_remaining, refresh_day_of_month}  (counts as a request)
 *             .../v6/KEY/history/USD/2026/11/5   paid plans; "plan-upgrade-required" otherwise
 *   errors    {"result":"error","error-type":"invalid-key"|"inactive-account"|"quota-reached"|"unsupported-code"|
 *             "malformed-request"|"plan-upgrade-required"|"no-data-available"}
 *   Plans: Free 1,500 a month, daily; Pro 30,000, hourly; Business 125,000, every 5 minutes; Volume.
 */
#ifndef CFX_MODEL_H
#define CFX_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define CFX_MAX_RATES       180
#define CFX_MAX_WATCH       12
#define CFX_HISTORY_DAYS    32
#define CFX_KEY_LEN         48

#define CFX_DATA_MAGIC      0x43465831UL    // "CFX1"
#define CFX_DATA_LAYOUT     1

enum Cfx_Plan_t : uint8_t { CFX_PLAN_OPEN = 0, CFX_PLAN_UNKNOWN, CFX_PLAN_FREE, CFX_PLAN_PRO, CFX_PLAN_BUSINESS, CFX_PLAN_VOLUME };
enum Cfx_Error_t : uint8_t {
    CFX_ERR_NONE = 0, CFX_ERR_KEY, CFX_ERR_INACTIVE, CFX_ERR_QUOTA, CFX_ERR_CODE, CFX_ERR_PLAN, CFX_ERR_NO_DATA, CFX_ERR_OTHER
};

struct Cfx_Currency_t {
    const char* code;
    const char* name;       /**< As the screens show it: "NIGERIAN NAIRA". */
};

/** @brief The currencies the phone offers, by region. Any code the API returns can still be shown. */
const Cfx_Currency_t* cfx_Catalogue(size_t& count);
/** @brief The name of a code, or the code itself. */
const char* cfx_Name(const char* code);

struct Cfx_Quota_t {
    uint32_t seen;              /**< When the quota endpoint was read; 0 never. */
    uint32_t planQuota;
    uint32_t remaining;
    uint8_t refreshDay;         /**< Day of the month the quota starts again. */
    uint8_t exhausted;
};

/** @brief One watched currency's daily rates against the base, newest last. */
struct Cfx_History_t {
    char code[4];
    uint8_t count;
    uint32_t day[CFX_HISTORY_DAYS];     /**< Days since 1970 (UTC) of the provider's update. */
    float rate[CFX_HISTORY_DAYS];
};

struct Cfx_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    char base[4];
    uint16_t rateCount;
    char codes[CFX_MAX_RATES][4];
    float rates[CFX_MAX_RATES];         /**< Units of each currency for one of the base. */
    uint32_t lastUpdate;                /**< The provider's time_last_update_unix. */
    uint32_t nextUpdate;                /**< ...and time_next_update_unix. */
    uint32_t fetched;                   /**< When this device received them. */
    uint8_t plan;                       /**< Cfx_Plan_t */
    uint8_t historyLocked;              /**< The plan refused history. */
    uint32_t historyRetryAt;
    Cfx_Quota_t quota;
    Cfx_History_t history[CFX_MAX_WATCH];
};

/** @brief "invalid-key"... as a Cfx_Error_t. */
uint8_t cfx_Error_Of(const char* errorType);

/**
 * @brief Reads a latest answer (keyless or keyed) into the table. Returns CFX_ERR_NONE, or the error the API gave
 *        (the table is then left alone).
 */
uint8_t cfx_Parse_Latest(JsonVariantConst doc, Cfx_Data_t& data);
/** @brief Reads a history answer: the rate of each watched code on that day. Returns the error, if any. */
uint8_t cfx_Parse_History(JsonVariantConst doc, const char (*watch)[4], uint8_t watchCount, float* rates, uint32_t& day);
uint8_t cfx_Parse_Quota(JsonVariantConst doc, Cfx_Quota_t& quota);
/** @brief The plan from its monthly quota: 1,500 Free, 30,000 Pro, 125,000 Business, more Volume. */
uint8_t cfx_Plan_Of_Quota(uint32_t planQuota);
const char* cfx_Plan_Name(uint8_t plan);

/** @brief A rate in the table, or 0 if the code is not there. */
float cfx_Rate(const Cfx_Data_t& data, const char* code);

/** @brief Records the day's rate of a code (one sample a day: a later one that day replaces it). */
void cfx_History_Put(Cfx_Data_t& data, const char* code, uint32_t day, float rate);
/** @brief The history of a code, or nullptr. */
const Cfx_History_t* cfx_History_Of(const Cfx_Data_t& data, const char* code);
/** @brief Drops history of codes no longer watched, and makes room for new ones. */
void cfx_History_Keep(Cfx_Data_t& data, const char (*watch)[4], uint8_t watchCount);
/** @brief The rate @p days before the newest sample (the nearest sample at or before then), or 0 if not known. */
float cfx_Rate_Days_Ago(const Cfx_History_t& history, uint32_t newestDay, int days);

/** @brief "1,348.02", "0.8905", "0.0007418": enough digits to read a rate. */
void cfx_Format_Rate(char* dest, size_t size, double value);
/** @brief "+1.24%" / "-0.30%" / "0.00%". */
void cfx_Format_Change(char* dest, size_t size, double percent);

/** @brief Whether a text is a currency code (three capital letters). */
bool cfx_Is_Code(const char* code);

#endif
