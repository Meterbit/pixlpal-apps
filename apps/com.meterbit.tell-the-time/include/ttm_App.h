/**
 * @file ttm_App.h
 * @brief Tell the Time's screens (ttm_Ui.cpp) and what the app task (tellTime.cpp) shows.
 */
#ifndef TTM_APP_H
#define TTM_APP_H

#include "ttm_Model.h"
#include "kk_Kids.h"

enum Ttm_Screen_t : uint8_t { TTM_SCR_PICK = 0, TTM_SCR_MENU, TTM_SCR_CLOCK, TTM_SCR_QUESTION, TTM_SCR_RESULT, TTM_SCR_STOP };

struct Ttm_View_t {
    uint8_t screen;
    const Ttm_Settings_t* s;
    const Kk_Session_t* k;
    uint8_t level;
    uint8_t menu;                       /**< 0 the clock, 1 play */
    uint8_t pick;                       /**< who is playing: the child selected */
    uint8_t stop;                       /**< Kk_Stop_t */
    // The time now (local).
    uint16_t minute;                    /**< after midnight */
    uint8_t second;
    // A round.
    const Ttm_Question_t* q;
    uint16_t set;                       /**< SET: where the hands are */
    uint8_t choice;
    uint8_t number, right;              /**< question 1-8, right so far */
    uint8_t feedback;                   /**< 0 none, 1 right, 2 try again */
    bool levelUp;
    uint32_t confettiMs;                /**< since the celebration began, 0 none */
};

void ttm_Draw(const Ttm_View_t& v);
/** @brief A clock face: hands at @p minute (0-1439) and, if @p second < 60, a second hand. */
void ttm_Clock_Face(int16_t cx, int16_t cy, int16_t r, uint16_t minute, uint8_t second, bool numbers);
const char* ttm_Level_Name(uint8_t level);

#endif
