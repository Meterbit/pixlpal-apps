/**
 * @file bgc_Ui.h
 * @brief Big Clock's faces on the whole 256x128 panel:
 *   Segment  four giant seven-segment digits with dim unlit segments, a seconds bar and the date
 *   Bold     the time in large type, seconds beside it, the weekday and date below
 *   Analog   a dial with hour, minute and second hands, the digital time and date beside it
 *   Words    the time in words to five minutes ("TWENTY PAST SEVEN"), dots for the minutes between
 * Or, by the Show setting, the time alone across the panel (each face at its largest), or the date alone: the day of
 * the month in giant digits, the month, weekday and year beside it. Both for reading from across a room.
 */
#ifndef BGC_UI_H
#define BGC_UI_H

#include "bgc_Model.h"

struct Bgc_View_t {
    const Bgc_Settings_t* settings;
    Bgc_Palette_t palette;
    bool clockValid;
    int year, month, day;       /**< month 1-12 */
    int weekday;                /**< 0 Sunday */
    int hour, minute, second;
    uint8_t face;               /**< The face shown (it can differ from the setting while faces rotate). */
};

/** @brief Draws the whole face. */
void bgc_Draw(const Bgc_View_t& view);

#endif
