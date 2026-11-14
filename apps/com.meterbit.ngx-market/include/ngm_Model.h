/**
 * @file ngm_Model.h
 * @brief NGX Market {4,6}: the settings, NGN Market's answers (https://api.ngnmarket.com/v1, "Authorization: Bearer
 *        ngm_..."), its plans, and how often to ask so the month's calls last. No Arduino, so it can be tested on a PC.
 *
 * Every answer carries meta.plan, calls_used, calls_remaining and reset_at, so the plan is known from the first call.
 * Free: 3,000 calls a month, 30 a minute: market snapshot, all companies with prices, indices, forex, market status.
 * Hobby adds price charts; Starter adds dividends (and more not used here). Prices refresh every 20 minutes while NGX
 * trades (Monday to Friday, 09:00 to 16:00 Lagos time).
 */
#ifndef NGM_MODEL_H
#define NGM_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ngx_Model.h"

#define NGM_NVS_KEY         "ngmSet"
#define NGM_SETTINGS_MAGIC  0x46040601u

enum Ngm_Plan_t : uint8_t { NGM_FREE = 0, NGM_HOBBY, NGM_STARTER, NGM_PRO, NGM_BUSINESS, NGM_ENTERPRISE, NGM_UNKNOWN };
enum Ngm_State_t : uint8_t { NGM_OK = 0, NGM_NO_KEY, NGM_KEY, NGM_RATE, NGM_QUOTA, NGM_PLAN, NGM_ERROR, NGM_WAIT };
enum Ngm_Screen_t : uint8_t { NGM_P_OVERVIEW = 0, NGM_P_HEATMAP, NGM_P_MOVERS, NGM_P_WATCH, NGM_P_STOCK, NGM_P_INDICES, NGM_P_FOREX,
                              NGM_P_DIVIDENDS, NGM_PAGES };

struct Ngm_Settings_t {
    uint32_t magic;
    char apiKey[80];                    /**< ngm_live_... (secret) */
    char watch[NGX_WATCH_MAX][16];
    uint8_t watchCount;
    char focus[16];                     /**< The stock shown on its own */
    uint8_t pages;                      /**< Bit per Ngm_Screen_t shown */
    uint8_t cycle;                      /**< Pages turn by themselves */
    uint8_t seconds;                    /**< Per page: 5-120 */
    uint8_t chartPeriod;                /**< 0: 7 days, 1: 30 days, 2: 90 days, 3: a year */
};
void ngm_Settings_Default(Ngm_Settings_t& s);
void ngm_Settings_Clamp(Ngm_Settings_t& s);

uint8_t ngm_Plan_Of(const char* name);
const char* ngm_Plan_Name(uint8_t plan);
/** @brief The chart period's API value ("30d") and its label ("30 days"). */
const char* ngm_Period(uint8_t period);
const char* ngm_Period_Label(uint8_t period);
/** @brief The plan a screen or call needs: charts Hobby, dividends Starter, the rest Free. */
uint8_t ngm_Needs(uint8_t screen);

/** @brief meta: the plan and the month's calls, from any answer. */
struct Ngm_Meta_t {
    uint8_t plan;
    uint32_t callsUsed, callsRemaining;
    uint32_t resetAt;                   /**< Unix, 0 unknown */
    bool known;
};

/** @brief The answer's state (NGM_OK when read) and a reason; @p meta is filled whenever the answer has it. */
uint8_t ngm_Check(const char* body, size_t length, int httpCode, Ngm_Meta_t& meta, char* why, size_t whySize);

bool ngm_Parse_Snapshot(const char* body, size_t length, Ngx_Market_t& m);
/** @brief A page of /companies into the board (appended). @return Pages in all, 0 if it could not be read. */
int ngm_Parse_Companies(const char* body, size_t length, Ngx_Board_t& b);
struct Ngm_Status_t { bool known, open; char reason[16]; char nextOpen[48]; char holiday[32]; };
bool ngm_Parse_Status(const char* body, size_t length, Ngm_Status_t& s);
bool ngm_Parse_Forex(const char* body, size_t length, Ngx_Table_t& t);
bool ngm_Parse_Indices(const char* body, size_t length, Ngx_Table_t& t);
bool ngm_Parse_Dividends(const char* body, size_t length, Ngx_Table_t& t);
/** @brief format=chart: [[timestamp, close], ...] into @p prices (oldest first). Returns how many. */
int ngm_Parse_Chart(const char* body, size_t length, float* prices, int max);

/**
 * @brief Seconds between price refreshes so the calls left last until the quota resets: trading hours left times
 *        @p callsPerRefresh over the calls remaining (a tenth kept back), never under 20 minutes (NGN Market's refresh)
 *        nor over 6 hours.
 */
uint32_t ngm_Refresh_Seconds(uint32_t remaining, uint32_t callsPerRefresh, uint32_t now, uint32_t resetAt);
/** @brief Lagos time (UTC+1, no summer time): weekday (0 Sunday) and minute of the day. */
void ngm_Lagos(uint32_t unix, int& weekday, uint16_t& minute);

#endif
