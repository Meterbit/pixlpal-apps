/**
 * @file fbl_Ui_Kit.h
 * @brief Drawing helpers of the Live Football screens: text, big scaled text, seven-segment digits, the pitch,
 *        crests with a fallback badge, a football.
 */
#ifndef FBL_UI_KIT_H
#define FBL_UI_KIT_H

#include <stdint.h>
#include <stddef.h>
#include "mtb_graphics.h"

#define FBL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t K_TEXT   = FBL_RGB(240, 240, 240);
static const uint16_t K_LABEL  = FBL_RGB(150, 150, 150);
static const uint16_t K_DIM    = FBL_RGB(85, 85, 85);
static const uint16_t K_LINE   = FBL_RGB(40, 40, 40);
static const uint16_t K_GOLD   = FBL_RGB(255, 196, 0);
static const uint16_t K_RED    = FBL_RGB(235, 40, 40);
static const uint16_t K_GREEN  = FBL_RGB(40, 210, 90);
static const uint16_t K_YELLOW = FBL_RGB(255, 220, 0);
static const uint16_t K_CYAN   = FBL_RGB(80, 200, 255);
static const uint16_t K_BAND   = FBL_RGB(0, 72, 34);       // Title bands: deep pitch green.
static const uint16_t K_TURF_A = FBL_RGB(0, 54, 20);
static const uint16_t K_TURF_B = FBL_RGB(0, 66, 26);
static const uint16_t K_TURF_LINE = FBL_RGB(60, 120, 70);
static const uint16_t K_PLATE  = FBL_RGB(0, 28, 10);
static const uint16_t K_FAV_BG = FBL_RGB(70, 52, 0);

int16_t fbl_Text_Width(const uint8_t* font, const char* text);
int16_t fbl_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
int16_t fbl_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
void fbl_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK);
/** @brief Cut to fit, ending in ".." when cut. */
void fbl_Fit(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth);
void fbl_Upper(char* text);

/** @brief Text drawn @p scale times larger, glyph by glyph, transparent background. Returns the width. */
int16_t fbl_Big_Text_Width(const uint8_t* font, int scale, const char* text);
void fbl_Big_Text(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, const char* text);

void fbl_Segment_Digit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int digit, uint16_t color);
/** @brief A number in seven-segment digits, ending at @p rightX. Returns its left edge. */
int16_t fbl_Segment_Number_Right(int16_t rightX, int16_t y, int16_t w, int16_t h, int16_t t, int value, uint16_t color);
int16_t fbl_Segment_Number(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int value, uint16_t color);

/** @brief Mown turf in vertical stripes between y1 and y2, with the halfway line and centre circle. */
void fbl_Pitch(int16_t y1, int16_t y2);
/** @brief The turf colour at column x. */
uint16_t fbl_Turf_At(int16_t x);

/** @brief The team's crest at (x, y), FBL_CREST square; if not loaded, a shield with the team's initials. */
void fbl_Crest(uint32_t teamId, const char* teamName, int16_t x, int16_t y, uint16_t background);
/** @brief "ARS" for "Arsenal", "MCI" for "Manchester City", "RMA" for "Real Madrid". */
void fbl_Team_Code(char* dest, size_t size, const char* name);

/** @brief A football of radius r. */
void fbl_Ball(int16_t cx, int16_t cy, int16_t r);

/** @brief Small event marks for the timeline and scorer lists. */
void fbl_Mark_Goal(int16_t cx, int16_t cy, uint16_t color);
void fbl_Mark_Card(int16_t x, int16_t y, uint16_t color);

#endif
