/**
 * @file mtb_kit_ui.h
 * @brief Drawing helpers shared by the 256x128 app screens: measured text, text cut to fit, word wrap, big scaled
 *        text, seven-segment digits, bars, sparklines, arrows, and text helpers (UTF-8 to the panel's ASCII,
 *        "5 min ago"). Nothing here touches the network, so all of it can be tested on a PC.
 */
#ifndef MTB_KIT_UI_H
#define MTB_KIT_UI_H

#include <stdint.h>
#include <stddef.h>
#include "mtb_graphics.h"

#define MTB_KIT_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t KIT_TEXT   = MTB_KIT_RGB(240, 240, 240);
static const uint16_t KIT_LABEL  = MTB_KIT_RGB(150, 150, 150);
static const uint16_t KIT_DIM    = MTB_KIT_RGB(85, 85, 85);
static const uint16_t KIT_LINE   = MTB_KIT_RGB(40, 40, 40);
static const uint16_t KIT_GOLD   = MTB_KIT_RGB(255, 196, 0);
static const uint16_t KIT_RED    = MTB_KIT_RGB(235, 40, 40);
static const uint16_t KIT_GREEN  = MTB_KIT_RGB(40, 210, 90);
static const uint16_t KIT_CYAN   = MTB_KIT_RGB(80, 200, 255);
static const uint16_t KIT_ORANGE = MTB_KIT_RGB(255, 140, 0);
static const uint16_t KIT_BLUE   = MTB_KIT_RGB(60, 120, 255);

/** @brief The status bar of a status-bar app covers rows 0-9; its screens start here. */
#define MTB_KIT_TOP 10

int16_t mtb_Kit_Text_Width(const uint8_t* font, const char* text);
/** Pixlpal compat: a text's glyph bits (bit 0 leftmost) into a w x h buffer of rows of stride bytes, from column x. */
void pxl_compat_glyphs(uint8_t* bits, int16_t stride, int16_t w, int16_t h, const uint8_t* font, const char* text, int x);
/** @brief Text at (x, y), top left. Returns the x after it. */
int16_t mtb_Kit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
/** @brief Text whose last column is @p rightX. Returns its left edge. */
int16_t mtb_Kit_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
void mtb_Kit_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
/** @brief @p text cut to fit @p maxWidth, ending in "." when cut. */
void mtb_Kit_Fit(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth);
void mtb_Kit_Upper(char* text);

/**
 * @brief Breaks @p text into at most @p maxLines lines of at most @p maxWidth pixels, at spaces where it can (a word
 *        too long for a line is cut). The last line ends in ".." if text was left over.
 * @param lines  maxLines rows of lineSize characters.
 * @return The number of lines used.
 */
int mtb_Kit_Wrap(const char* text, const uint8_t* font, int16_t maxWidth, char* lines, size_t lineSize, int maxLines);

/** @brief Text @p scale times larger, glyph by glyph, transparent background. */
int16_t mtb_Kit_Big_Text_Width(const uint8_t* font, int scale, const char* text);
void mtb_Kit_Big_Text(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, const char* text);

/** @brief A seven-segment digit in a w x h box with segments t thick; @p ghost (if not BLACK) draws the unlit ones. */
void mtb_Kit_Segment_Digit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int digit, uint16_t color, uint16_t ghost = BLACK);

/** @brief A bar of @p fraction (0-1) of its width, on a dim track. */
void mtb_Kit_Bar(int16_t x, int16_t y, int16_t w, int16_t h, float fraction, uint16_t color, uint16_t track = KIT_LINE);
/** @brief A line chart of @p count values scaled to the box; @p fill (if not BLACK) shades under it. */
void mtb_Kit_Sparkline(int16_t x, int16_t y, int16_t w, int16_t h, const float* values, int count, uint16_t color, uint16_t fill = BLACK);
/** @brief A filled arrow head pointing up (@p up) or down, centred on (cx, cy). */
void mtb_Kit_Arrow(int16_t cx, int16_t cy, int16_t size, bool up, uint16_t color);
/** @brief Colour a fraction @p t (0-1) of the way from @p a to @p b. */
uint16_t mtb_Kit_Blend(uint16_t a, uint16_t b, float t);
/** @brief @p color at @p level (0-255) brightness. */
uint16_t mtb_Kit_Dim(uint16_t color, uint8_t level);

/** @brief UTF-8 to the panel's ASCII: accents dropped ("Atlético" -> "Atletico"), curly quotes and dashes made plain,
 *         anything else left out. */
void mtb_Kit_To_Ascii(char* dest, size_t size, const char* text);
/** @brief "just now", "5 min ago", "3 h ago", "2 d ago" from @p seconds elapsed. */
void mtb_Kit_Ago(char* dest, size_t size, int32_t seconds);
/** @brief Unix time from ISO 8601 "2026-11-05T19:08:32Z" (or with +01:00, or a date alone). 0 if not a date. */
uint32_t mtb_Kit_Parse_Iso_Time(const char* text);
/** @brief Days from 1970-01-01 to the civil date. */
int32_t mtb_Kit_Days_From_Civil(int y, int m, int d);
/** @brief Monday = 0 ... Sunday = 6 weekday of a civil date. */
int mtb_Kit_Weekday(int y, int m, int d);
int mtb_Kit_Days_In_Month(int y, int m);

/** @brief "0xAARRGGBB" (the phone's colours) or "#RRGGBB" to RGB565. False if not one. */
bool mtb_Kit_Parse_Color(const char* text, uint16_t& color);
/** @brief "HH:MM", 24-hour, to minutes after midnight; -1 if not one. */
int mtb_Kit_Parse_Clock(const char* text);
/** @brief Percent-encodes @p text for a URL query value. */
void mtb_Kit_Url_Encode(char* dest, size_t size, const char* text);
/** @brief "07:30" from minutes after midnight. */
void mtb_Kit_Clock_Text(char* dest, size_t size, uint16_t minutes);

#endif
