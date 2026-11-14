/**
 * @file ngx_Ui.h
 * @brief NGX screens shared by the NGX apps (256x128, from y = 10): the market's day, a heatmap of every stock, the
 *        gainers and losers, a watchlist, one stock, and a table (indices, forex, dividends, T-bills, bonds). The data
 *        provider is named at the foot of each screen, as the providers' terms ask.
 */
#ifndef NGX_UI_H
#define NGX_UI_H

#include "ngx_Model.h"

enum Ngx_Page_t : uint8_t { NGX_OVERVIEW = 0, NGX_HEATMAP, NGX_MOVERS, NGX_WATCH, NGX_STOCK, NGX_TABLE, NGX_MESSAGE };

struct Ngx_View_t {
    uint8_t page;
    const Ngx_Board_t* board;
    const char* source;                 /**< "NGN Market" */
    const char* status;                 /**< "Open", "Closed: weekend", "30 min delayed" */
    bool open;
    uint32_t now;                       /**< Unix */
    // The watchlist.
    const char (*watch)[16];
    int watchCount, scroll;
    // One stock.
    int focus;                          /**< Index in the board, -1 none */
    const float* chart;
    int chartCount;
    const char* chartLabel;             /**< "30 days" */
    const char* facts[6];               /**< Lines of fundamentals, or nullptr */
    // A table.
    const Ngx_Table_t* table;
    // A message (no key yet, a refusal).
    const char* title;
    const char* message;
};

void ngx_Draw(const Ngx_View_t& v);
/** @brief Rows a table or the watchlist shows at once (for scrolling). */
#define NGX_VISIBLE_ROWS 8

#endif
