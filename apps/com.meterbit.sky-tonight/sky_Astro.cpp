// The sun and the moon (sky_Astro.h). Angles in radians inside, degrees outside.
#include "sky_Astro.h"
#include <math.h>

static const double RAD = M_PI / 180.0;
static const double E = RAD * 23.4397;          // the obliquity of the ecliptic

static double days_J2000(int64_t t){ return (double)t / 86400.0 + 2440587.5 - 2451545.0; }

static double right_Ascension(double l, double b){ return atan2(sin(l) * cos(E) - tan(b) * sin(E), cos(l)); }
static double declination(double l, double b){ return asin(sin(b) * cos(E) + cos(b) * sin(E) * sin(l)); }
static double sidereal(double d, double lw){ return RAD * (280.16 + 360.9856235 * d) - lw; }
static double altitude(double H, double phi, double dec){ return asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H)); }

struct Coords { double ra, dec, dist; };

static Coords sun_Coords(double d){
    const double M = RAD * (357.5291 + 0.98560028 * d);
    const double C = RAD * (1.9148 * sin(M) + 0.02 * sin(2 * M) + 0.0003 * sin(3 * M));
    const double L = M + C + RAD * 102.9372 + M_PI;
    return {right_Ascension(L, 0), declination(L, 0), 149598000.0};
}

static Coords moon_Coords(double d){
    const double L = RAD * (218.316 + 13.176396 * d);
    const double M = RAD * (134.963 + 13.064993 * d);
    const double F = RAD * (93.272 + 13.229350 * d);
    const double l = L + RAD * 6.289 * sin(M);
    const double b = RAD * 5.128 * sin(F);
    return {right_Ascension(l, b), declination(l, b), 385001.0 - 20905.0 * cos(M)};
}

double sky_Sun_Altitude(int64_t t, double lat, double lon){
    const double d = days_J2000(t), lw = RAD * -lon, phi = RAD * lat;
    const Coords c = sun_Coords(d);
    return altitude(sidereal(d, lw) - c.ra, phi, c.dec) / RAD;
}

double sky_Moon_Altitude(int64_t t, double lat, double lon){
    const double d = days_J2000(t), lw = RAD * -lon, phi = RAD * lat;
    const Coords c = moon_Coords(d);
    const double H = sidereal(d, lw) - c.ra;
    double h = altitude(H, phi, c.dec);
    h -= asin(6378.14 / c.dist) * cos(h);      // parallax: the moon is close, so from the surface it is lower
    return h / RAD;
}

void sky_Moon_Phase(int64_t t, double& fraction, double& phase){
    const double d = days_J2000(t);
    const Coords s = sun_Coords(d), m = moon_Coords(d);
    const double phi = acos(sin(s.dec) * sin(m.dec) + cos(s.dec) * cos(m.dec) * cos(s.ra - m.ra));
    const double inc = atan2(s.dist * sin(phi), m.dist - s.dist * cos(phi));
    const double angle = atan2(cos(s.dec) * sin(s.ra - m.ra), sin(s.dec) * cos(m.dec) - cos(s.dec) * sin(m.dec) * cos(s.ra - m.ra));
    fraction = (1 + cos(inc)) / 2;
    phase = 0.5 + 0.5 * inc * (angle < 0 ? -1 : 1) / M_PI;
    if (phase < 0) phase += 1;
    if (phase >= 1) phase -= 1;
}

const char* sky_Phase_Name(double p){
    if (p < 0.03 || p >= 0.97) return "NEW MOON";
    if (p < 0.22) return "WAXING CRESCENT";
    if (p < 0.28) return "FIRST QUARTER";
    if (p < 0.47) return "WAXING GIBBOUS";
    if (p < 0.53) return "FULL MOON";
    if (p < 0.72) return "WANING GIBBOUS";
    if (p < 0.78) return "LAST QUARTER";
    return "WANING CRESCENT";
}

/** Crossings of @p h0 degrees, a minute apart, refined to the second by halving. */
static Sky_Rise_Set_t rise_Set(double (*alt)(int64_t, double, double), int64_t from, double lat, double lon, double h0, int step){
    Sky_Rise_Set_t r = {0, 0, -90};
    double prev = alt(from, lat, lon) - h0;
    if (prev + h0 > r.maxAlt) r.maxAlt = prev + h0;
    for (int64_t t = from + step; t <= from + 86400; t += step){
        const double now = alt(t, lat, lon) - h0;
        if (now + h0 > r.maxAlt) r.maxAlt = now + h0;
        if ((prev < 0) != (now < 0)){
            int64_t a = t - step, b = t;
            double fa = prev;
            while (b - a > 1){
                const int64_t m = (a + b) / 2;
                const double fm = alt(m, lat, lon) - h0;
                if ((fa < 0) == (fm < 0)){ a = m; fa = fm; } else b = m;
            }
            if (prev < 0 && !r.rise) r.rise = b;
            if (prev >= 0 && !r.set) r.set = b;
        }
        prev = now;
    }
    return r;
}

Sky_Rise_Set_t sky_Sun_Rise_Set(int64_t from, double lat, double lon){ return rise_Set(sky_Sun_Altitude, from, lat, lon, -0.833, 60); }
// The moon's altitude above is from the surface (parallax); like the sun's, its top edge shows at -0.833 (refraction
// and half its size).
Sky_Rise_Set_t sky_Moon_Rise_Set(int64_t from, double lat, double lon){ return rise_Set(sky_Moon_Altitude, from, lat, lon, -0.833, 120); }

int64_t sky_Next_Phase(int64_t t, double target){
    double f, p;
    sky_Moon_Phase(t, f, p);
    double prevDiff = p - target;
    if (prevDiff < 0) prevDiff += 1;
    for (int64_t k = 3600; k <= 31 * 86400; k += 3600){
        sky_Moon_Phase(t + k, f, p);
        double diff = p - target;
        if (diff < 0) diff += 1;
        if (diff < prevDiff && diff < 0.5 && prevDiff - diff > 0.5) return t + k;    // wrapped past the target
        if (prevDiff > 0.9 && diff < 0.1) return t + k;
        prevDiff = diff;
    }
    return 0;
}

double sky_Distance_Km(double lat1, double lon1, double lat2, double lon2){
    const double p1 = lat1 * RAD, p2 = lat2 * RAD, dp = (lat2 - lat1) * RAD, dl = (lon2 - lon1) * RAD;
    const double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

int sky_Aurora_Chance(double kp, double lat){
    const double a = fabs(lat);
    // The latitude down to which the aurora tends to be seen for each K index (NOAA's rule of thumb, geographic).
    static const double SEEN[10] = {66.5, 64.5, 62.4, 60.4, 58.3, 56.3, 54.2, 52.2, 50.1, 48.1};
    int k = (int)floor(kp);
    if (k < 0) k = 0;
    if (k > 9) k = 9;
    if (a >= SEEN[k]) return 2;
    if (a >= SEEN[k] - 4.0) return 1;       // low on the horizon, towards the pole
    return 0;
}
