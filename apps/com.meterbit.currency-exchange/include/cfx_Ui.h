/**
 * @file cfx_Ui.h
 * @brief Currency Exchange's screens, below the status bar:
 *   Pair      the featured rate in big digits, its 1-, 7- and 30-day changes, and a month's sparkline
 *   Board     every watched currency against the base, with the day's change
 *   Convert   an amount of the base in every watched currency
 *   Plan      the plan Pixlpal found, requests left, how often rates update, and what an upgrade adds
 *   Message   no data yet, offline, key refused
 */
#ifndef CFX_UI_H
#define CFX_UI_H

#include "cfx_Service.h"

enum Cfx_Page_t : uint8_t { CFX_PAGE_PAIR = 0, CFX_PAGE_BOARD, CFX_PAGE_CONVERT, CFX_PAGE_PLAN, CFX_PAGE_COUNT };

struct Cfx_View_t {
    const Cfx_Data_t* data;
    const Cfx_Settings_t* settings;
    const Cfx_Status_t* status;
    uint8_t page;
    uint8_t featured;           /**< Index in the watchlist shown on the Pair page. */
    uint8_t boardPage;
    uint32_t now;
    const char* message;        /**< Set: the Message screen. */
    const char* detail;
};

void cfx_Draw(const Cfx_View_t& view);
/** @brief Board pages for this many watched currencies. */
uint8_t cfx_Board_Pages(uint8_t watchCount);

#endif
