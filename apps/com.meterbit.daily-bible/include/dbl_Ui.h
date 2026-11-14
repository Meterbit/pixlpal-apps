/**
 * @file dbl_Ui.h
 * @brief Daily Bible's 256x128 screen (from y = 10): the verse of the day, and today (the church season and its
 *        next great day, the reading plan and its streak, the next reminder).
 */
#ifndef DBL_UI_H
#define DBL_UI_H

#include "dbl_Model.h"

enum Dbl_Page_t : uint8_t { DBL_PAGE_VERSE = 0, DBL_PAGE_TODAY };

struct Dbl_View_t {
    const Dbl_Settings_t* settings;
    const Fth_Verse_t* verse;
    const char* verseError;
    uint32_t now;
    uint8_t page;
    int scroll;
    const char* alert;
    const char* alertDetail;
    bool flash;
};

void dbl_Draw(const Dbl_View_t& v);
void dbl_Draw_Alert(const Dbl_View_t& v);

#endif
