/**
 * @file ngx_Model.h
 * @brief The Nigerian Exchange (NGX) for Pixlpal apps, whatever the data provider: stocks, the market's day, a table of
 *        rows (indices, forex, dividends, T-bills, bonds), naira formatting, movers, trading hours. Shared by NGX Market
 *        (NGN Market) and Kobo Terminal. No Arduino, so it can be tested on a PC.
 */
#ifndef NGX_MODEL_H
#define NGX_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define NGX_STOCKS_MAX  240
#define NGX_ROWS_MAX    24
#define NGX_WATCH_MAX   12
#define NGX_CHART_MAX   120

struct Ngx_Stock_t {
    char symbol[16];
    char name[40];
    char sector[24];
    float price, change, changePct;     /**< Naira; change may be 0 when only the percent is known */
    float high, low, prevClose;         /**< 0 if not known */
    float high52, low52;
    float pe;                           /**< 0 if not known */
    double volume, marketCap;           /**< Naira */
};

struct Ngx_Market_t {
    bool known;
    float asi, asiChange, asiChangePct, ytdPct;
    double value, volume, marketCap;    /**< Naira traded today, shares, all equities */
    uint32_t deals;
    uint16_t advancers, decliners, unchanged;
    char date[12];                      /**< The session: "2026-11-06" */
};

/** @brief A line of a table screen: label, value, change (with its colour), detail. */
struct Ngx_Row_t {
    char label[24];
    char value[20];
    char change[14];
    int8_t sign;                        /**< 1 up (green), -1 down (red), 0 neither */
    char detail[40];
};
struct Ngx_Table_t {
    char title[32];
    Ngx_Row_t rows[NGX_ROWS_MAX];
    uint8_t count;
    uint32_t updated;                   /**< Unix */
};

struct Ngx_Board_t {
    Ngx_Stock_t* stocks;                /**< NGX_STOCKS_MAX (PSRAM on the device) */
    int count;
    Ngx_Market_t market;
    uint32_t updated;                   /**< Unix of the last prices */
};

int ngx_Find(const Ngx_Board_t& b, const char* symbol);
/** @brief The best and worst by percent (only stocks that moved), up to @p max of each. */
void ngx_Movers(const Ngx_Board_t& b, int* gainers, int& gainerCount, int* losers, int& loserCount, int max);
/** @brief Indices of the stocks, largest market value first (by price when the market value is unknown). */
int ngx_By_Size(const Ngx_Board_t& b, int* out, int max);
/** @brief The market's breadth from the stocks, when the provider gives none. */
void ngx_Breadth(const Ngx_Board_t& b, Ngx_Market_t& m);

/** @brief "N302.50", "N1.25", "N12.4B", "N52.1T" (N for the naira sign the panel's font lacks). */
void ngx_Naira(char* dest, size_t size, double value, bool compact);
/** @brief "1.28M", "312.8M", "4,821" */
void ngx_Count(char* dest, size_t size, double value);
/** @brief "+0.84%", "-1.20%", "0.00%" */
void ngx_Percent(char* dest, size_t size, float pct);

/** @brief NGX trades Monday to Friday, 09:00 to 16:00 Lagos time (holidays aside): @p weekday 0 Sunday. */
bool ngx_Trading_Time(int weekday, uint16_t minute);
/** @brief "dangcem" or " DANGCEM " to "DANGCEM"; false if not 1-15 of A-Z, 0-9, '.', '-'. */
bool ngx_Symbol(const char* text, char* dest, size_t size);
/** @brief "DANGCEM, mtnn GTCO" into up to NGX_WATCH_MAX symbols; returns how many (-1 if any is not a symbol). */
int ngx_Parse_Watchlist(const char* text, char list[NGX_WATCH_MAX][16]);

#endif
