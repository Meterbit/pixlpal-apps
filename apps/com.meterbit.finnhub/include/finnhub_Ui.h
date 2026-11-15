/**
 * @file finnhub_Ui.h
 * @brief Data model and drawing functions of the Finnhub Stats "market terminal" screen (256x128).
 *
 * Screen layout (the 10 rows above y = 10 belong to the status bar):
 *   y  11-66   Logo, ticker, name, market session, price, day change, day-range gauge
 *   y  72-107  Rotating panel: session chart, returns heatmap, fundamentals, 52-week range, analysts + earnings
 *   y 109-117  Watchlist carousel
 *   y 120-127  News ticker (a Mtb_ScrollText_t owned by the app task)
 */
#ifndef FINNHUB_UI_H
#define FINNHUB_UI_H

#include <stdint.h>
#include <time.h>

#define FH_SYMBOL_LEN      12
#define FH_HISTORY_POINTS  176  /**< One point per chart column. */
#define FH_HISTORY_SEC     150  /**< Seconds per point: 176 points = 7.3 h, a whole US session. */
#define FH_MAX_HEADLINES   3
#define FH_HEADLINE_LEN    140
#define FH_MAX_EARNINGS    4
#define FH_PAGE_COUNT      5
#define FH_NEWS_Y          120

enum Fh_Detail_t { FH_PROFILE, FH_METRICS, FH_ANALYSTS, FH_EARNINGS, FH_NEXT_EARNINGS, FH_NEWS, FH_TARGET, FH_DETAIL_COUNT };

/** @brief Everything known about one symbol. Metrics Finnhub does not report are NAN and draw as "--". */
struct Fh_Stock_t {
    char symbol[FH_SYMBOL_LEN];

    bool hasQuote;
    bool noQuoteForSymbol;      /**< Finnhub answered, but has no price for this symbol. */
    double price, change, changePct, high, low, open, prevClose;
    time_t quoteTime;
    int8_t tickDirection;       /**< Sign of the last price move. */
    uint32_t tickMs;            /**< millis() of the last price move, for the price flash. */

    bool hasProfile;
    char name[32], exchange[10], industry[24], currency[6], website[32], ipoDate[11];
    double marketCapMillions;

    bool hasMetrics;
    float week52High, week52Low;
    char week52HighDate[11], week52LowDate[11];
    float peRatio, eps, beta, dividendYield, netMargin;
    float return5D, returnMTD, return13W, return26W, returnYTD, return52W;

    bool hasAnalysts;
    uint16_t strongBuy, buy, hold, sell, strongSell;

    uint8_t earningsCount;      /**< Valid entries of the two arrays below, newest first. */
    float earningsSurprisePct[FH_MAX_EARNINGS];
    uint8_t earningsQuarter[FH_MAX_EARNINGS];
    char nextEarningsDate[11];

    bool hasTarget;             /**< Analysts' price target: Finnhub's paid plans only. */
    float targetMean, targetHigh, targetLow;

    uint8_t headlineCount;
    char headlines[FH_MAX_HEADLINES][FH_HEADLINE_LEN];

    float history[FH_HISTORY_POINTS]; /**< Prices oldest first; NAN marks a gap. */
    uint16_t historyCount;
    uint32_t historyBucket;     /**< Quote time / FH_HISTORY_SEC of the newest point. */

    uint32_t quoteDue;          /**< Uptime in seconds at which the quote may be fetched again. */
    uint32_t detailDue[FH_DETAIL_COUNT];
    bool logoChecked;
    bool hasLogo;
};

/** @brief US market state. */
struct Fh_Market_t {
    bool known;
    bool isOpen;
    char session[8];  /**< "OPEN", "PRE", "POST" or "CLOSED". */
};

void fh_Ui_Clear_Content(void);
/** @brief Clear the logo area; draws a one-letter tile when the symbol has no logo file. The app queues the PNG itself. */
void fh_Ui_Draw_Logo_Placeholder(const Fh_Stock_t& stock);
void fh_Ui_Draw_Hero(const Fh_Stock_t& stock, const Fh_Market_t& market, uint32_t nowMs);
void fh_Ui_Draw_Page(uint8_t page, const Fh_Stock_t& stock, bool autoCycleHeld);
/**
 * @brief Draw the watchlist row. Up to five symbols stay in place; longer lists keep the selected one in the middle.
 * @param selected  Index of the displayed symbol in @p watch, or -1 if it is not in the list.
 */
void fh_Ui_Draw_Watchlist(Fh_Stock_t* const* watch, int count, int selected);
/** @brief Number of history points that hold a price. */
uint16_t fh_Ui_History_Points(const Fh_Stock_t& stock);

#endif
