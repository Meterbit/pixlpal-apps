// The space station's orbit (iss_Orbit.h). Angles in radians inside, degrees outside.
#include "iss_Orbit.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const double PI2 = 2.0 * M_PI;
static const double RAD = M_PI / 180.0;
static const double MU = 398600.4418;           // km^3/s^2
static const double RE = 6378.137;              // km, WGS84
static const double FLAT = 1.0 / 298.257223563;
static const double J2 = 1.08262668e-3;

// Columns of a TLE line, 1-based as the format gives them, as a number.
static double field(const char* line, int from, int to){
    char t[24];
    int n = 0;
    for (int i = from - 1; i < to && line[i] && n < (int)sizeof(t) - 1; i++) t[n++] = line[i];
    t[n] = '\0';
    return atof(t);
}

static bool checksum_Ok(const char* line){
    if (strlen(line) < 69) return false;
    int sum = 0;
    for (int i = 0; i < 68; i++){
        const char c = line[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    return line[68] - '0' == sum % 10;
}

static int64_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool iss_Tle_Parse(const char* l1, const char* l2, Iss_Tle_t& tle){
    memset(&tle, 0, sizeof(tle));
    if (!l1 || !l2 || l1[0] != '1' || l2[0] != '2' || !checksum_Ok(l1) || !checksum_Ok(l2)) return false;
    const int yy = (int)field(l1, 19, 20);
    const double doy = field(l1, 21, 32);
    const int year = yy < 57 ? 2000 + yy : 1900 + yy;
    tle.epoch = days_From_Civil(year, 1, 1) * 86400 + (int64_t)llround((doy - 1.0) * 86400.0);
    tle.ndot2 = field(l1, 34, 43);
    tle.incl = field(l2, 9, 16) * RAD;
    tle.raan = field(l2, 18, 25) * RAD;
    char e[12] = "0.";
    memcpy(e + 2, l2 + 26, 7); e[9] = '\0';
    tle.ecc = atof(e);
    tle.argp = field(l2, 35, 42) * RAD;
    tle.m0 = field(l2, 44, 51) * RAD;
    const double revs = field(l2, 53, 63);
    if (revs < 10 || revs > 18 || tle.ecc >= 0.1) return false;    // a low earth orbit, as the station's
    tle.n = revs * PI2 / 86400.0;
    tle.a = cbrt(MU / (tle.n * tle.n));
    tle.ok = true;
    return true;
}

// Greenwich mean sidereal time, radians.
static double gmst(double t){
    const double d = t / 86400.0 - 10957.5;      // days from J2000.0 (2000-01-01 12:00 UTC)
    double g = fmod((280.46061837 + 360.98564736629 * d) * RAD, PI2);
    return g < 0 ? g + PI2 : g;
}

// The sun's direction, earth-centred inertial, a unit vector.
static void sun_Eci(double t, double v[3]){
    const double d = t / 86400.0 - 10957.5;
    const double L = (280.460 + 0.9856474 * d) * RAD;
    const double g = (357.528 + 0.9856003 * d) * RAD;
    const double lambda = L + (1.915 * sin(g) + 0.020 * sin(2 * g)) * RAD;
    const double eps = (23.439 - 0.0000004 * d) * RAD;
    v[0] = cos(lambda); v[1] = cos(eps) * sin(lambda); v[2] = sin(eps) * sin(lambda);
}

static void eci_To_Ecef(const double in[3], double t, double out[3]){
    const double g = gmst(t), c = cos(g), s = sin(g);
    out[0] = c * in[0] + s * in[1];
    out[1] = -s * in[0] + c * in[1];
    out[2] = in[2];
}

void iss_State_At(const Iss_Tle_t& k, double t, Iss_State_t& s){
    const double dt = t - (double)k.epoch, days = dt / 86400.0;
    const double p = k.a * (1 - k.ecc * k.ecc);
    const double ci = cos(k.incl), si = sin(k.incl);
    const double rate = 1.5 * J2 * (RE / p) * (RE / p) * k.n;
    const double raan = k.raan - rate * ci * dt;
    const double argp = k.argp + rate * (2.0 - 2.5 * si * si) * dt;
    double M = fmod(k.m0 + k.n * dt + PI2 * k.ndot2 * days * days, PI2);
    if (M < 0) M += PI2;
    double E = M;
    for (int i = 0; i < 8; i++){
        const double f = E - k.ecc * sin(E) - M;
        E -= f / (1 - k.ecc * cos(E));
        if (fabs(f) < 1e-10) break;
    }
    const double nu = atan2(sqrt(1 - k.ecc * k.ecc) * sin(E), cos(E) - k.ecc);
    // The decay term shrinks the orbit with the rising mean motion (Kepler's third law).
    const double nNow = k.n + 2.0 * PI2 * k.ndot2 * days / 86400.0;
    const double a = cbrt(MU / (nNow * nNow));
    const double r = a * (1 - k.ecc * cos(E));
    const double u = argp + nu, cu = cos(u), su = sin(u), cO = cos(raan), sO = sin(raan);
    const double eci[3] = {r * (cO * cu - sO * su * ci), r * (sO * cu + cO * su * ci), r * su * si};
    double ecef[3];
    eci_To_Ecef(eci, t, ecef);
    s.x = ecef[0]; s.y = ecef[1]; s.z = ecef[2];
    // Geodetic latitude on WGS84 (a few rounds).
    const double e2 = FLAT * (2 - FLAT);
    const double rxy = sqrt(s.x * s.x + s.y * s.y);
    double lat = atan2(s.z, rxy), N = RE;
    for (int i = 0; i < 4; i++){
        const double sl = sin(lat);
        N = RE / sqrt(1 - e2 * sl * sl);
        lat = atan2(s.z + N * e2 * sl, rxy);
    }
    s.lat = lat / RAD;
    s.lon = atan2(s.y, s.x) / RAD;
    s.alt = rxy / cos(lat) - N;
    s.speed = sqrt(MU * (2.0 / r - 1.0 / a)) * 3600.0;
    // In the earth's shadow: behind the earth, within its (cylindrical) shadow.
    double sun[3];
    sun_Eci(t, sun);
    const double along = eci[0] * sun[0] + eci[1] * sun[1] + eci[2] * sun[2];
    if (along >= 0) s.sunlit = true;
    else {
        const double px = eci[0] - along * sun[0], py = eci[1] - along * sun[1], pz = eci[2] - along * sun[2];
        s.sunlit = sqrt(px * px + py * py + pz * pz) > RE;
    }
}

void iss_Sub_Solar(double t, double& lat, double& lon){
    double sun[3], e[3];
    sun_Eci(t, sun);
    eci_To_Ecef(sun, t, e);
    lat = asin(e[2]) / RAD;
    lon = atan2(e[1], e[0]) / RAD;
}

double iss_Sun_Altitude(double t, double lat, double lon){
    double sLat, sLon;
    iss_Sub_Solar(t, sLat, sLon);
    const double a = lat * RAD, b = sLat * RAD, dl = (lon - sLon) * RAD;
    return asin(sin(a) * sin(b) + cos(a) * cos(b) * cos(dl)) / RAD;
}

void iss_Look(const Iss_State_t& s, double lat, double lon, double& el, double& az, double& range){
    const double e2 = FLAT * (2 - FLAT);
    const double phi = lat * RAD, lam = lon * RAD, sp = sin(phi), cp = cos(phi), sl = sin(lam), cl = cos(lam);
    const double N = RE / sqrt(1 - e2 * sp * sp);
    const double ox = N * cp * cl, oy = N * cp * sl, oz = N * (1 - e2) * sp;
    const double rx = s.x - ox, ry = s.y - oy, rz = s.z - oz;
    const double S = sp * cl * rx + sp * sl * ry - cp * rz;
    const double E = -sl * rx + cl * ry;
    const double Z = cp * cl * rx + cp * sl * ry + sp * rz;
    range = sqrt(rx * rx + ry * ry + rz * rz);
    el = asin(Z / range) / RAD;
    az = atan2(E, -S) / RAD;
    if (az < 0) az += 360.0;
}

static double elevation_At(const Iss_Search_t& s, double t, double* az = nullptr, bool* sunlit = nullptr){
    Iss_State_t st;
    iss_State_At(s.tle, t, st);
    double el, a, range;
    iss_Look(st, s.lat, s.lon, el, a, range);
    if (az) *az = a;
    if (sunlit) *sunlit = st.sunlit;
    return el;
}

// The moment, to a second, between @p lo and @p hi where the station crosses the horizon (rising if @p rising).
static int64_t crossing(const Iss_Search_t& s, int64_t lo, int64_t hi, bool rising){
    while (hi - lo > 1){
        const int64_t mid = (lo + hi) / 2;
        const bool up = elevation_At(s, (double)mid) > 0;
        if (up == rising) hi = mid; else lo = mid;
    }
    return hi;
}

static void finish_Pass(Iss_Search_t& s, int64_t rise, int64_t set){
    Iss_Pass_t p = {};
    p.rise = rise; p.set = set;
    double best = -90, bestAz = 0;
    int64_t bestT = rise;
    for (int64_t t = rise; t <= set; t += 5){
        double az;
        bool lit;
        const double el = elevation_At(s, (double)t, &az, &lit);
        if (el > best){ best = el; bestAz = az; bestT = t; }
        if (lit && iss_Sun_Altitude((double)t, s.lat, s.lon) < -6.0) p.visible = true;
    }
    if (best < s.minEl) return;
    double az;
    elevation_At(s, (double)rise, &az); p.riseAz = (float)az;
    elevation_At(s, (double)set, &az); p.setAz = (float)az;
    p.top = bestT; p.maxEl = (float)best; p.topAz = (float)bestAz;
    if (s.count < s.max) s.out[s.count++] = p;
}

void iss_Search_Begin(Iss_Search_t& s, const Iss_Tle_t& tle, double lat, double lon, int64_t from, int hours, double minEl,
                      Iss_Pass_t* out, int max){
    memset(&s, 0, sizeof(s));
    s.tle = tle; s.lat = lat; s.lon = lon; s.minEl = minEl;
    s.t = from; s.end = from + (int64_t)hours * 3600;
    s.out = out; s.max = max;
    s.up = elevation_At(s, (double)from) > 0;
    s.riseAt = s.up ? from : 0;         // a pass already under way counts from now
}

bool iss_Search_Step(Iss_Search_t& s, int steps){
    for (int i = 0; i < steps; i++){
        if (s.t >= s.end || s.count >= s.max) return false;
        const int64_t next = s.t + 60;
        const bool up = elevation_At(s, (double)next) > 0;
        if (up && !s.up) s.riseAt = crossing(s, s.t, next, true);
        else if (!up && s.up) finish_Pass(s, s.riseAt, crossing(s, s.t, next, false));
        s.up = up;
        s.t = next;
    }
    return s.t < s.end && s.count < s.max;
}

const char* iss_Compass(double az){
    static const char* const P[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    int k = (int)floor((az + 11.25) / 22.5);
    k %= 16;
    if (k < 0) k += 16;
    return P[k];
}
