/**
 * @file oil_Ui.h
 * @brief Drawing of the Oil price screen (256x128). Draws from an Oil_Data_t only; knows nothing of providers.
 *
 * Screen layout (the 10 rows above y = 10 belong to the status bar):
 *   y  10-40   The selected benchmark: code, change, price, unit, what it is, source and date of the value
 *   y  43-106  Rotating page: 3-month chart, price board, change statistics, crude output, headlines
 *   y 110-117  Benchmark strip, the selected one highlighted
 *   y 120-127  Ticker (a Mtb_ScrollText_t owned by the app task)
 */
#ifndef OIL_UI_H
#define OIL_UI_H

#include "oil_Model.h"

#define OIL_TICKER_Y        120

enum Oil_Page_t : uint8_t {
    OIL_PAGE_TREND = 0,
    OIL_PAGE_BOARD,
    OIL_PAGE_STATS,
    OIL_PAGE_SUPPLY,
    OIL_PAGE_NEWS,
    OIL_PAGE_COUNT
};

struct Oil_View_t {
    const Oil_Data_t* data;
    int selected;           /**< Index into data->series of the benchmark in the large display. */
    uint8_t page;           /**< Oil_Page_t */
    uint8_t newsFirst;      /**< Index of the first headline on the news page. */
    bool stale;             /**< The data on screen is older than it should be: its date is drawn in amber. */
    const char* notice;     /**< Shown instead of a page that has no data yet, or nullptr. */
};

void oil_Ui_Clear(void);
void oil_Ui_Draw_Hero(const Oil_View_t& view);
void oil_Ui_Draw_Page(const Oil_View_t& view);
void oil_Ui_Draw_Strip(const Oil_View_t& view);

/** @brief "107.03", "2.728" for a price per gallon, "23,929" for a volume. */
void oil_Ui_Format_Value(char* dest, size_t size, float value, uint8_t unit);
/** @brief "$/bbl", "$/gal", "$/MMBtu" or "kb/d". */
const char* oil_Ui_Unit_Text(uint8_t unit);
/** @brief "+0.34%", or "--" for NAN. */
void oil_Ui_Format_Percent(char* dest, size_t size, float percent);

#endif
