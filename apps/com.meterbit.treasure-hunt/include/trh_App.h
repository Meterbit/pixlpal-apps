/**
 * @file trh_App.h
 * @brief Treasure Hunt's screens (trh_Ui.cpp) and what the app task (treasureHunt.cpp) shows.
 */
#ifndef TRH_APP_H
#define TRH_APP_H

#include "trh_Model.h"
#include "kk_Kids.h"

enum Trh_Screen_t : uint8_t { TRH_SCR_PICK = 0, TRH_SCR_READY, TRH_SCR_CLUE, TRH_SCR_CODE, TRH_SCR_TREASURE, TRH_SCR_EMPTY };

struct Trh_View_t {
    uint8_t screen;
    const Trh_Settings_t* s;
    const Kk_Session_t* k;
    uint8_t pick;
    uint8_t clue;                       /**< the clue shown (0-based) */
    uint32_t seconds;                   /**< since the hunt began */
    bool hint;                          /**< the clue's hint is shown */
    uint8_t digits[3];
    uint8_t digit;                      /**< the one being turned */
    uint8_t wrong;                      /**< a wrong code: shake for a moment */
    uint32_t ms;                        /**< for movement */
    bool best;                          /**< a new best time */
};

void trh_Draw(const Trh_View_t& v);

#endif
