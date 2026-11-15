/**
 * @file crypto_Ui.h
 * @brief Data model and drawing functions of the Crypto Stats screen (256x128).
 *
 * Screen layout (the 10 rows above y = 10 belong to the status bar):
 *   y  10-56   Icon, rank, symbol, name, age of the data, price, 24h change
 *   y  59-107  Rotating panel: 7-day chart, market numbers, supply, momentum, leaderboard + top gainers
 *   y 109-117  Watchlist carousel
 *   y 120-127  Ticker (a Mtb_ScrollText_t owned by the app task)
 */
#ifndef CRYPTO_UI_H
#define CRYPTO_UI_H

#include <stdint.h>

#define CC_ID_LEN          40
#define CC_SYMBOL_LEN      12
#define CC_NAME_LEN        24
#define CC_HISTORY_POINTS  64   /**< 28 points from a 7-day chart, the rest added from price updates. */
#define CC_MAX_HEADLINES   3
#define CC_HEADLINE_LEN    120
#define CC_MAX_GAINERS     3
#define CC_PAGE_COUNT      5
#define CC_TICKER_Y        120

struct Cc_Point_t {
    uint32_t time;      /**< Unix seconds. */
    float price;
};

/** @brief Everything known about one coin. Stored in flash, so it survives reboots without spending credits. Unknown numbers are NAN. */
struct Cc_Coin_t {
    uint32_t magic;
    char id[CC_ID_LEN];
    char symbol[CC_SYMBOL_LEN];
    char name[CC_NAME_LEN];
    uint16_t rank;

    bool hasQuote;
    double price, marketCap, volume24h, vwap24h, supply, maxSupply;
    float changePct;
    uint32_t quoteTime;     /**< Unix seconds of the response this data came from. */

    Cc_Point_t history[CC_HISTORY_POINTS];  /**< Oldest first. */
    uint16_t historyCount;
    uint32_t chartTime;     /**< When the 7-day chart was last fetched. */

    // Valid for this power-on session only; cleared when loaded from flash.
    bool unknownToApi;      /**< CoinCap answered, without this id. */
    bool iconChecked;
    bool hasIcon;
};

struct Cc_Gainer_t {
    char symbol[CC_SYMBOL_LEN];
    float changePct;
};

/** @brief Market-wide data and the times of the shared requests. Also stored in flash. */
struct Cc_Market_t {
    uint32_t magic;
    uint32_t quotesTime, newsTime, moversTime;
    uint8_t gainerCount;
    Cc_Gainer_t gainers[CC_MAX_GAINERS];
    uint8_t headlineCount;
    char headlines[CC_MAX_HEADLINES][CC_HEADLINE_LEN];
};

void cc_Ui_Clear_Content(void);
/** @brief Clear the icon area; draws a one-letter tile when the coin has no icon file. The app queues the PNG itself. */
void cc_Ui_Draw_Icon_Placeholder(const Cc_Coin_t& coin);
void cc_Ui_Draw_Hero(const Cc_Coin_t& coin, uint32_t nowUnix);
void cc_Ui_Draw_Page(uint8_t page, const Cc_Coin_t& coin, Cc_Coin_t* const* watch, int watchCount, const Cc_Coin_t* bitcoin, const Cc_Market_t& market, uint32_t nowUnix, bool autoCycleHeld);
/**
 * @brief Draw the watchlist row. Up to five coins stay in place; longer lists keep the selected one in the middle.
 * @param selected  Index of the displayed coin in @p watch, or -1 if it is not in the list.
 */
void cc_Ui_Draw_Watchlist(Cc_Coin_t* const* watch, int count, int selected);
/** @brief "12m", "3h", "3h 05m" or "2d 4h". */
void cc_Ui_Format_Duration(char* dest, unsigned size, uint32_t seconds);
/** @brief "1.53T", "28.4B", "20.11M". */
void cc_Ui_Format_Big(char* dest, unsigned size, double value);

#endif
