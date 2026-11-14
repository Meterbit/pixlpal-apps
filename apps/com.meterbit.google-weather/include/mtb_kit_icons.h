/**
 * @file mtb_kit_icons.h
 * @brief Icons drawn from shapes, so they look right at any size: weather symbols and the moon's phase.
 */
#ifndef MTB_KIT_ICONS_H
#define MTB_KIT_ICONS_H

#include <stdint.h>

enum Mtb_Kit_Weather_t : uint8_t {
    KIT_WX_UNKNOWN = 0, KIT_WX_CLEAR, KIT_WX_PARTLY, KIT_WX_CLOUDY, KIT_WX_FOG, KIT_WX_DRIZZLE, KIT_WX_RAIN,
    KIT_WX_HEAVY_RAIN, KIT_WX_SNOW, KIT_WX_SLEET, KIT_WX_STORM, KIT_WX_WIND, KIT_WX_HAIL, KIT_WX_HOT, KIT_WX_COUNT
};

/**
 * @brief A weather symbol filling a @p size x @p size box at (x, y); sizes from 12 to 64 look right.
 * @param night  The moon instead of the sun, for clear and partly cloudy skies.
 */
void mtb_Kit_Weather_Icon(int16_t x, int16_t y, int16_t size, uint8_t weather, bool night);

/**
 * @brief The moon at (cx, cy), radius r, lit as on @p age days into the cycle (0 new, ~14.77 full).
 */
void mtb_Kit_Moon(int16_t cx, int16_t cy, int16_t r, float age);
/** @brief The moon's age in days (0-29.53) at a Unix time, from the mean synodic month. */
float mtb_Kit_Moon_Age(uint32_t unixTime);
/** @brief "NEW MOON", "WAXING CRESCENT"... "WANING CRESCENT" for an age. */
const char* mtb_Kit_Moon_Name(float age);

#endif
