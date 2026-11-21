/**
 * @file bgc_Model.h
 * @brief Big Clock's settings, colour themes, night mode, and the clock's words. No drawing and no Arduino, so it can
 *        be tested on a PC.
 */
#ifndef BGC_MODEL_H
#define BGC_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define BGC_SETTINGS_MAGIC  0x42474331UL    // "BGC1"
#define BGC_NVS_KEY         "bigClockCfg"

enum Bgc_Face_t : uint8_t { BGC_FACE_SEGMENT = 0, BGC_FACE_BOLD, BGC_FACE_ANALOG, BGC_FACE_WORDS, BGC_FACE_COUNT };
enum Bgc_Theme_t : uint8_t {
    BGC_THEME_CLASSIC = 0, BGC_THEME_AMBER, BGC_THEME_ICE, BGC_THEME_MATRIX, BGC_THEME_SUNSET, BGC_THEME_RAINBOW,
    BGC_THEME_CUSTOM, BGC_THEME_COUNT
};
enum Bgc_Night_Style_t : uint8_t { BGC_NIGHT_RED = 0, BGC_NIGHT_DIM };
/** What the panel shows: the face with its date, the time alone across the panel, or the date alone, large (two
 *  Pixlpals side by side, one a clock and one a calendar, read from across a room). */
enum Bgc_Show_t : uint8_t { BGC_SHOW_BOTH = 0, BGC_SHOW_CLOCK, BGC_SHOW_CALENDAR, BGC_SHOW_COUNT };

struct Bgc_Settings_t {
    uint32_t magic;
    uint8_t face;               /**< Bgc_Face_t */
    uint8_t theme;              /**< Bgc_Theme_t */
    uint8_t hour24;
    uint8_t showSeconds;
    uint8_t showDate;
    uint8_t autoFaceMinutes;    /**< Next face every this many minutes; 0 off. */
    uint8_t night;              /**< Night mode on. */
    uint8_t nightStyle;         /**< Bgc_Night_Style_t */
    uint16_t nightFrom;         /**< Minutes after midnight. */
    uint16_t nightTo;
    uint16_t customColor;       /**< RGB565, for BGC_THEME_CUSTOM. */
    uint8_t show;               /**< Bgc_Show_t. In what was the struct's end padding, so the size is unchanged and
                                 *   settings saved before it read as 0, the clock with its date. */
};

// Saved settings must keep their size: a different size reads as nothing saved, and owners would lose theirs.
static_assert(sizeof(Bgc_Settings_t) == 20, "Big Clock's saved settings changed size");

/** @brief The colours a face uses. */
struct Bgc_Palette_t {
    uint16_t main;              /**< Hours and minutes. */
    uint16_t accent;            /**< Seconds, the second hand, highlighted words. */
    uint16_t soft;              /**< Date, labels, unlit words. */
    uint16_t ghost;             /**< Unlit segments, the dial's ticks. */
    uint16_t top, bottom;       /**< A vertical gradient for the digits (equal when there is none). */
};

void bgc_Settings_Default(Bgc_Settings_t& settings);
void bgc_Settings_Clamp(Bgc_Settings_t& settings);

/** @brief Whether @p minuteOfDay is in the night window (which may cross midnight). */
bool bgc_In_Window(uint16_t from, uint16_t to, uint16_t minuteOfDay);

/**
 * @brief The palette at a moment: the theme, the rainbow's hue (it turns once an hour), or night colours.
 * @param nowUnix  For the rainbow; any value for the other themes.
 */
Bgc_Palette_t bgc_Palette(const Bgc_Settings_t& settings, bool night, uint32_t nowUnix);

/** @brief RGB565 of a hue (0-359) at full saturation and @p value (0-255). */
uint16_t bgc_Hue(uint16_t hue, uint8_t value);

/**
 * @brief The time in words, to the five minutes begun (7:23 is "TWENTY PAST SEVEN"), in up to 4 lines: "IT IS", "TWENTY", "PAST", "SEVEN";
 *        "IT IS", "SEVEN", "O'CLOCK"; "IT IS", "QUARTER TO", "EIGHT".
 * @param lines  4 rows of lineSize characters.
 * @return The number of lines.
 */
int bgc_Words(int hour, int minute, char* lines, size_t lineSize);

/** @brief "7:05" or "07:05" (24-hour), and "AM"/"PM" ("" in 24-hour mode). */
void bgc_Hour_Minute(char* dest, size_t size, char* meridiem, size_t meridiemSize, int hour, int minute, bool hour24, bool leadingZero);

#endif
