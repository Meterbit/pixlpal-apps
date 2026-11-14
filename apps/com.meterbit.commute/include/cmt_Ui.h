/**
 * @file cmt_Ui.h
 * @brief Commute's 256x128 screen (from y = 10): an overview of every board, a stop's departure board, a drive with
 *        its traffic, and the leave alert.
 */
#ifndef CMT_UI_H
#define CMT_UI_H

#include "cmt_App.h"

struct Cmt_View_t {
    const Cmt_Settings_t* settings;
    const Cmt_Result_t* results;        /**< CMT_BOARDS */
    const Cmt_Status_t* status;
    uint32_t now;                       /**< Unix */
    int board;                          /**< The board shown, or -1 for the overview */
    bool online;
    const char* alert;                  /**< "LEAVE IN 4 MIN", or nullptr */
    const char* alertDetail;
    bool flash;
};

void cmt_Draw(const Cmt_View_t& v);
/** @brief Only the alert box over what is there (each second, for the flashing border). */
void cmt_Draw_Alert(const Cmt_View_t& v);
/** @brief When to leave for a board's next departure (time - walk), 0 if none. */
uint32_t cmt_Leave_At(const Cmt_Board_t& b, const Cmt_Result_t& r, uint32_t now, const Cmt_Departure_t** which);

#endif
