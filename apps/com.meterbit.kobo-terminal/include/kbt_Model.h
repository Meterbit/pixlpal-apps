/**
 * @file kbt_Model.h
 * @brief Kobo Terminal {4,7}: the settings, Kobo Terminal's answers (https://koboterminal.com, "X-API-Key"), its plans
 *        and the day's calls. No Arduino, so it can be tested on a PC.
 *
 * Plans (calls a day / a minute): Personal (free) 100 / 10: NGX prices and the market; Starter 1,000 / 20 adds price
 * history, fundamentals, NASD and disclosures; Professional 10,000 / 60 adds FX and historical data; Institutional
 * 50,000 / 120. The answers do not say the plan, so it is found by trying a Starter call (fundamentals) and a
 * Professional one (forex) once a day. Prices are delayed by 30 minutes and refreshed every 20 during trading.
 */
#ifndef KBT_MODEL_H
#define KBT_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ngx_Model.h"

#define KBT_NVS_KEY         "kbtSet"
#define KBT_SETTINGS_MAGIC  0x46040701u

enum Kbt_Plan_t : uint8_t { KBT_PERSONAL = 0, KBT_STARTER, KBT_PROFESSIONAL, KBT_INSTITUTIONAL, KBT_UNKNOWN };
enum Kbt_State_t : uint8_t { KBT_OK = 0, KBT_NO_KEY, KBT_KEY, KBT_RATE, KBT_DAY_USED, KBT_PLAN, KBT_ERROR, KBT_WAIT };
enum Kbt_Screen_t : uint8_t { KBT_P_OVERVIEW = 0, KBT_P_HEATMAP, KBT_P_MOVERS, KBT_P_WATCH, KBT_P_STOCK, KBT_P_INDICES, KBT_P_TBILLS,
                              KBT_P_BONDS, KBT_PAGES };

struct Kbt_Settings_t {
    uint32_t magic;
    char apiKey[80];                    /**< Secret */
    char watch[NGX_WATCH_MAX][16];
    uint8_t watchCount;
    char focus[16];
    uint8_t pages, cycle, seconds;
};
void kbt_Settings_Default(Kbt_Settings_t& s);
void kbt_Settings_Clamp(Kbt_Settings_t& s);

const char* kbt_Plan_Name(uint8_t plan);
/** @brief Calls a day the plan allows. */
uint32_t kbt_Daily_Limit(uint8_t plan);

/** @brief The answer's state and a reason. */
uint8_t kbt_Check(const char* body, size_t length, int httpCode, char* why, size_t whySize);

int kbt_Parse_Stocks(const char* body, size_t length, Ngx_Board_t& b);
bool kbt_Parse_Market(const char* body, size_t length, Ngx_Market_t& m);
bool kbt_Parse_Indices(const char* body, size_t length, Ngx_Table_t& t);
bool kbt_Parse_Auctions(const char* body, size_t length, Ngx_Table_t& t);
bool kbt_Parse_Bonds(const char* body, size_t length, Ngx_Table_t& t);
/** @brief The open market or not, and why ("open", "closed"). */
bool kbt_Parse_Status(const char* body, size_t length, bool& open, char* message, size_t size);
/** @brief Fundamentals of @p symbol as up to six display lines ("P/E 9.4   EPS N3.20"). Returns how many. */
int kbt_Parse_Fundamentals(const char* body, size_t length, const char* symbol, char lines[6][40]);

/** @brief Seconds between price refreshes (3 calls each) so the day's allowance lasts the trading hours left. */
uint32_t kbt_Refresh_Seconds(uint32_t callsLeftToday, uint32_t callsPerRefresh, uint16_t lagosMinute);

#endif
