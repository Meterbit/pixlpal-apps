/**
 * @file sky_Astro.h
 * @brief The sun and the moon from a place and a time, on the device (no internet). Low-precision formulas (those of
 *        "Astronomy Answers" by Aa. Hoogendoorn, as used by suncalc): about a minute for sunrise and sunset, a few
 *        minutes for the moon, which is plenty for a display. Pure C++: tested on a PC.
 */
#ifndef SKY_ASTRO_H
#define SKY_ASTRO_H

#include <stdint.h>
#include <time.h>

/** Altitude above the horizon, degrees, of the sun or the moon at @p t (UTC seconds) for a place. */
double sky_Sun_Altitude(int64_t t, double lat, double lon);
double sky_Moon_Altitude(int64_t t, double lat, double lon);

/** The moon at @p t: lit fraction 0-1, and phase 0-1 (0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter). */
void sky_Moon_Phase(int64_t t, double& fraction, double& phase);
/** "NEW MOON", "WAXING CRESCENT" ... */
const char* sky_Phase_Name(double phase);

/** Rise and set between @p from and @p from + 24 h (scanned a minute apart); 0 if none (polar day or night). */
struct Sky_Rise_Set_t { int64_t rise, set; double maxAlt; };
Sky_Rise_Set_t sky_Sun_Rise_Set(int64_t from, double lat, double lon);
Sky_Rise_Set_t sky_Moon_Rise_Set(int64_t from, double lat, double lon);

/** The next time from @p t the phase reaches @p target (0.5 full, 0 new), within 31 days; 0 if none. */
int64_t sky_Next_Phase(int64_t t, double target);

/** Great-circle distance in km. */
double sky_Distance_Km(double lat1, double lon1, double lat2, double lon2);

/** The aurora's chance at a latitude for a planetary K index: 0 unlikely, 1 possible (low on the horizon), 2 likely. */
int sky_Aurora_Chance(double kp, double lat);

#endif
