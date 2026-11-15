/**
 * @file fbl_Ui_Screens.h
 * @brief Between the Live Football screen files: the match screens (fbl_Ui_Match.cpp) and the shared pieces they use
 *        from fbl_Ui.cpp. Not for the app.
 */
#ifndef FBL_UI_SCREENS_H
#define FBL_UI_SCREENS_H

#include "fbl_Ui.h"

void fbl_Band(const char* left, const char* right, uint16_t background);
void fbl_Footer(const char* left, const char* right, uint16_t rightColor);
/** @brief "15:00" local, or "SAT 15:00" if not today, or "07 NOV" if a week or more away. */
void fbl_Kickoff_Text(char* dest, size_t size, uint32_t kickoff, uint32_t now, bool withDay);
/** @brief The league's board name. */
const char* fbl_League_Name(uint16_t leagueId);
/** @brief ...or, for a league not in the list, the name the API gave with the match. */
const char* fbl_Match_League(const Fbl_Match_t& match);
/** @brief "67'", "45+2'", "HT", "FT", "PENS"... */
void fbl_Minute_Text(char* dest, size_t size, const Fbl_Match_t& match);
uint16_t fbl_Minute_Color(const Fbl_Match_t& match);

void fbl_Draw_Live(const Fbl_View_t& view);
void fbl_Animate_Live(const Fbl_View_t& view);
void fbl_Draw_Countdown(const Fbl_View_t& view);
void fbl_Animate_Countdown(const Fbl_View_t& view);
void fbl_Draw_Goal(const Fbl_View_t& view);
void fbl_Draw_Banner(const Fbl_View_t& view);

#endif
