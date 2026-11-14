/**
 * @file gwx_Ui.h
 * @brief Google Weather's screens, below the status bar:
 *   Now     the conditions: icon, temperature, feels-like, humidity, wind and its direction, UV, rain, today's range
 *   Hours   the next twelve hours as columns (icon, temperature, chance of rain) and a day's temperature curve
 *   Days    seven days: icon, chance of rain, and each day's range on the week's scale
 *   Sky     sunrise and sunset with the sun's place on its arc, day length, and the moon
 *   Alerts  public weather alerts in force, most severe first, with the issuing agency
 *   Usage   calls this month against the budget, and the pace
 * "Google Maps" is shown on each screen, as Google's terms require.
 */
#ifndef GWX_UI_H
#define GWX_UI_H

#include "gwx_Model.h"

enum Gwx_Page_t : uint8_t { GWX_PAGE_NOW = 0, GWX_PAGE_HOURS, GWX_PAGE_DAYS, GWX_PAGE_SKY, GWX_PAGE_ALERTS, GWX_PAGE_USAGE, GWX_PAGE_COUNT };

struct Gwx_View_t {
    const Gwx_Data_t* data;
    const Gwx_Settings_t* settings;
    uint8_t page;
    uint32_t now;
    uint32_t interval;          /**< Seconds between reads, for the Usage page. */
    const char* message;        /**< Set: a message screen instead. */
    const char* detail;
};

void gwx_Draw(const Gwx_View_t& view);

#endif
