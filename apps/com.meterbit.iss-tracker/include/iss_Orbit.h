/**
 * @file iss_Orbit.h
 * @brief The space station's orbit on the device, from its two-line elements (TLE): where it is at any time, where it
 *        will pass, whether it is in sunlight, and its passes over a place. Mean elements with the earth's J2 drift
 *        and the TLE's decay term, without SGP4's short-period terms: within about 10-20 km of SGP4 over a day, a few
 *        seconds on a pass's times, which is plenty for a map and a pass list. Pure C++: tested on a PC.
 */
#ifndef ISS_ORBIT_H
#define ISS_ORBIT_H

#include <stdint.h>

struct Iss_Tle_t {
    int64_t epoch;                      // unix seconds (UTC)
    double incl, raan, ecc, argp, m0;   // radians
    double n;                           // mean motion, radians a second
    double ndot2;                       // the TLE's first derivative / 2, revolutions a day squared
    double a;                           // semi-major axis, km
    bool ok;
};

/** Reads the two lines; false if they are not a TLE (or a checksum is wrong). */
bool iss_Tle_Parse(const char* line1, const char* line2, Iss_Tle_t& tle);

struct Iss_State_t {
    double x, y, z;                     // earth-fixed (ECEF), km
    double lat, lon, alt;               // degrees, degrees, km
    double speed;                       // orbital speed, km/h
    bool sunlit;
};

/** The station at @p t (unix seconds). */
void iss_State_At(const Iss_Tle_t& tle, double t, Iss_State_t& s);

/** The sub-solar point at @p t (where the sun is overhead), degrees. */
void iss_Sub_Solar(double t, double& lat, double& lon);

/** The sun's height above the horizon at a place, degrees (no refraction). */
double iss_Sun_Altitude(double t, double lat, double lon);

/** The station seen from a place: elevation and azimuth (from north, clockwise), degrees; range, km. */
void iss_Look(const Iss_State_t& s, double lat, double lon, double& el, double& az, double& range);

struct Iss_Pass_t {
    int64_t rise, top, set;             // unix seconds
    float maxEl, riseAz, topAz, setAz;  // degrees
    bool visible;                       // the station sunlit while the sky is dark (sun below -6 degrees) at some point
};

/**
 * Finds passes over a place in steps, so a caller can wait between them: start with iss_Search_Begin, then call
 * iss_Search_Step until it returns false. Passes whose top is under @p minEl are left out.
 */
struct Iss_Search_t {
    Iss_Tle_t tle;
    double lat, lon, minEl;
    int64_t t, end;
    bool up;                            // above the horizon at t
    int64_t riseAt;
    Iss_Pass_t* out;
    int max, count;
};
void iss_Search_Begin(Iss_Search_t& s, const Iss_Tle_t& tle, double lat, double lon, int64_t from, int hours, double minEl,
                      Iss_Pass_t* out, int max);
/** Advances up to @p steps minutes. False when the search is done (the span covered, or @p max passes found). */
bool iss_Search_Step(Iss_Search_t& s, int steps);

/** "NNE" from an azimuth in degrees. */
const char* iss_Compass(double az);

#endif
