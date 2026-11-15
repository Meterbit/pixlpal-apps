/**
 * @file stw_Ui.h
 * @brief Timers' screens below the status bar, one per mode, with the modes as tabs across the top:
 *   Stopwatch  the time in large segments with hundredths, the laps beside it (fastest green, slowest red)
 *   Timer      the time left, a bar, when it ends by the clock; at zero the panel flashes and counts the time over
 *   Intervals  the phase (get ready, work, rest) in its colour, the seconds left in it, the round, the time left
 * A timer running in another mode shows at the bottom right.
 */
#ifndef STW_UI_H
#define STW_UI_H

#include "stw_Model.h"

struct Stw_View_t {
    const Stw_Settings_t* settings;
    const Stw_Watch_t* watch;
    const Stw_Timer_t* timer;
    const Stw_Intervals_t* intervals;
    int64_t nowUs;
    const char* endsAt;             /**< "14:32": when the countdown ends, by the clock; nullptr if unknown. */
    bool flash;                     /**< The flashing frame of an alarm or a phase change. */
};

void stw_Draw(const Stw_View_t& view);
/** @brief The digits' colour of a theme. */
uint16_t stw_Theme_Color(uint8_t theme);

#endif
