/**
 * @file wck_Ui.h
 * @brief World Clock's views below the status bar:
 *   Focus    one city large: its flag (drawn by the app into the box this returns), the time in big type, the date,
 *            how far it is from here and the part of its day
 *   Board    up to six cities, one a row: day or night, name and country, time, difference, tomorrow/yesterday
 *   Planner  every city's hours in columns, the cursor at the time being planned (the knob moves it), coloured by
 *            working hours, morning, evening and night: for finding a meeting time
 */
#ifndef WCK_UI_H
#define WCK_UI_H

#include "wck_Model.h"
#include "mtb_kit_image.h"

struct Wck_View_t {
    const Wck_Settings_t* settings;
    uint8_t view;
    uint8_t focus;                  /**< The city in the focus view. */
    int64_t utc;
    int32_t localOffset;            /**< The device's offset, for "+5H" and "tomorrow". */
    int32_t plannerShift;           /**< Minutes from now the planner's cursor is at. */
    bool clockValid;
};

/** @brief Draws the view; @p flagBox gets the focus view's flag box (zero size in the other views). */
void wck_Draw(const Wck_View_t& view, Mtb_Kit_Box_t* flagBox);

#endif
