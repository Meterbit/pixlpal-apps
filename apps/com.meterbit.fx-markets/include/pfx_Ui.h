/**
 * @file pfx_Ui.h
 * @brief FX Markets' screens below the status bar:
 *   Board     every pair: price, change on the previous close, and 30 days as a sparkline
 *   Chart     the chosen pair as candles: 30 days, or today's 15-minute bars on a paid plan
 *   Sessions  Sydney, Tokyo, London and New York on a 24-hour line in your time, which are open, the weekend close
 *   Plan      what the key has, and today's calls
 */
#ifndef PFX_UI_H
#define PFX_UI_H

#include "pfx_Model.h"

enum Pfx_Page_t : uint8_t { PFX_PAGE_BOARD = 0, PFX_PAGE_CHART, PFX_PAGE_SESSIONS, PFX_PAGE_PLAN, PFX_PAGE_COUNT };

struct Pfx_View_t {
    const Pfx_Data_t* data;
    const Pfx_Settings_t* settings;
    uint8_t page;
    uint32_t now;
    int32_t localOffset;            /**< The device's UTC offset, for the sessions' line. */
    const char* message;
    const char* detail;
};

void pfx_Draw(const Pfx_View_t& view);

#endif
