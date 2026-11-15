/**
 * @file fth_Ui.h
 * @brief Screen parts the FAITH apps share (256x128, from y = 10): a header, a centred message, a verse page that
 *        scrolls, and an alert box with a flashing border.
 */
#ifndef FTH_UI_H
#define FTH_UI_H

#include "fth_Kit.h"
#include "mtb_kit_ui.h"

#define FTH_FOOT 120

/** @brief Text as ASCII, cut to @p maxWidth. */
void fth_Text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back = BLACK);
/** @brief A line at y = 12: @p left in its colour, @p right (dim) on the right; a rule below. Both upper case. */
void fth_Header(const char* left, uint16_t leftColor, const char* right);
void fth_Message(const char* title, const char* detail);
/** @brief How many lines the verse takes (extra first, a gap, then the text); 9 fit on a page. */
int fth_Verse_Lines(const Fth_Verse_t& v);
#define FTH_VERSE_FIT 9
/** @brief A verse page: @p title in the header with the reference, the text from line @p scroll (wrapping round). */
void fth_Draw_Verse(const Fth_Verse_t& v, const char* title, uint16_t titleColor, int scroll, const char* error);
/** @brief The alert box over the screen. */
void fth_Draw_Alert(const char* alert, const char* detail, bool flash, uint16_t color);

#endif
