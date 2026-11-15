/**
 * @file cth_Ui.h
 * @brief Catholic Daily's 256x128 screen (from y = 10): today's liturgical day in its colour, the Rosary's mysteries,
 *        the verse of the day, and the devotion alert.
 */
#ifndef CTH_UI_H
#define CTH_UI_H

#include "cth_Model.h"

enum Cth_Page_t : uint8_t { CTH_PAGE_TODAY = 0, CTH_PAGE_ROSARY, CTH_PAGE_VERSE };

/** @brief The next devotion today or tomorrow: its name and when. */
struct Cth_Next_t { char name[32]; uint32_t at; };

struct Cth_View_t {
    const Cth_Settings_t* settings;
    const Cth_Day_t* day;
    const Fth_Verse_t* verse;
    const char* verseError;
    const Cth_Next_t* next;             /**< nullptr none */
    uint32_t now;
    uint8_t page;
    int scroll;
    const char* alert;
    const char* alertDetail;
    bool flash;
};

void cth_Draw(const Cth_View_t& v);
void cth_Draw_Alert(const Cth_View_t& v);

#endif
