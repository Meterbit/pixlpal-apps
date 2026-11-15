/**
 * @file tdw_Model.cpp
 * @brief Tides & Water: settings, requests, the providers' answers, tide arithmetic, judgements and alerts.
 */
#include "tdw_Model.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <algorithm>

/** @brief PSRAM on the device (tdw_Net.cpp), the heap in tests. */
ArduinoJson::Allocator* tdw_Json_Allocator(void);
uint32_t mtb_Kit_Parse_Iso_Time(const char* text);

// newlib has no strcasestr.
static bool has_Word(const char* text, const char* word){
    const size_t n = strlen(word);
    for (const char* p = text; *p; p++) if (!strncasecmp(p, word, n)) return true;
    return false;
}

// Settings -------------------------------------------------------------------------------------------------------
void tdw_Settings_Default(Tdw_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = TDW_SETTINGS_MAGIC;
    snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
    s.lat = 6.4541;
    s.lon = 3.3947;
    s.source = TDW_SRC_AUTO;
    s.datum = 0;
    s.windUnit = TDW_KMH;
    s.pages = (uint8_t)((1u << TDW_PAGES) - 1);
    s.cycle = 1;
    s.seconds = 12;
    s.alertOn = 1;
    s.stormAlert = 1;
    s.riverAlert = TDW_RIVER_VERY_HIGH;
    s.waveAlertDm = 25;
    s.gustAlert = 60;
    s.highWaterCm = 0;
    s.leadHours = 6;
    snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

void tdw_Settings_Clamp(Tdw_Settings_t& s){
    s.magic = TDW_SETTINGS_MAGIC;
    s.place[sizeof(s.place) - 1] = '\0';
    s.riverPlace[sizeof(s.riverPlace) - 1] = '\0';
    if (!(s.lat >= -90 && s.lat <= 90) || !(s.lon >= -180 && s.lon <= 180)){ s.lat = 6.4541; s.lon = 3.3947; }
    if (!(s.riverLat >= -90 && s.riverLat <= 90) || !(s.riverLon >= -180 && s.riverLon <= 180)) s.riverPlace[0] = '\0';
    if (s.source > TDW_SRC_OPENMETEO) s.source = TDW_SRC_AUTO;
    s.datum = s.datum ? 1 : 0;
    s.imperial = s.imperial ? 1 : 0;
    if (s.windUnit > TDW_MS) s.windUnit = TDW_KMH;
    s.pages &= (uint8_t)((1u << TDW_PAGES) - 1);
    if (s.pages == 0) s.pages = 1u << TDW_P_TIDE;
    s.cycle = s.cycle ? 1 : 0;
    if (s.seconds < 4) s.seconds = 4;
    if (s.seconds > 60) s.seconds = 60;
    s.alertOn = s.alertOn ? 1 : 0;
    s.phoneAlerts = s.phoneAlerts ? 1 : 0;
    s.stormAlert = s.stormAlert ? 1 : 0;
    if (s.riverAlert > TDW_RIVER_EXTREME || s.riverAlert == TDW_RIVER_LOW || s.riverAlert == TDW_RIVER_NORMAL) s.riverAlert = 0;
    if (s.waveAlertDm > 100) s.waveAlertDm = 100;
    if (s.gustAlert > 200) s.gustAlert = 200;
    if (s.highWaterCm > 2000) s.highWaterCm = 2000;
    if (s.leadHours < 1) s.leadHours = 1;
    if (s.leadHours > 24) s.leadHours = 24;
    s.worldTidesKey[sizeof(s.worldTidesKey) - 1] = '\0';
    s.stormglassKey[sizeof(s.stormglassKey) - 1] = '\0';
    s.noaaId[sizeof(s.noaaId) - 1] = '\0';
    s.noaaName[sizeof(s.noaaName) - 1] = '\0';
    if (!s.notify.ntfyServer[0]) snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

bool tdw_Noaa_Region(double lat, double lon){
    if (lat >= 13 && lat <= 72 && lon >= -180 && lon <= -64) return true;      // the mainland, Alaska, Hawaii, Puerto Rico
    if (lat >= 13 && lat <= 21 && lon >= 144 && lon <= 147) return true;       // Guam, the Northern Marianas
    if (lat >= -15 && lat <= -10 && lon >= -172 && lon <= -168) return true;   // American Samoa
    if (lat >= 17 && lat <= 19 && lon >= -66 && lon <= -64) return true;       // the Virgin Islands
    return false;
}

float tdw_Km(double lat1, double lon1, double lat2, double lon2){
    const double r = 6371.0, k = M_PI / 180.0;
    const double dLat = (lat2 - lat1) * k, dLon = (lon2 - lon1) * k;
    const double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * k) * cos(lat2 * k) * sin(dLon / 2) * sin(dLon / 2);
    return (float)(2 * r * atan2(sqrt(a), sqrt(1 - a)));
}

bool tdw_Parse_Coordinates(const char* text, double& lat, double& lon){
    if (!text) return false;
    char* end = nullptr;
    const double a = strtod(text, &end);
    if (end == text) return false;
    while (*end == ' ') end++;
    if (*end != ',') return false;
    const char* p = end + 1;
    const double b = strtod(p, &end);
    if (end == p) return false;
    while (*end == ' ') end++;
    if (*end) return false;
    if (!(a >= -90 && a <= 90) || !(b >= -180 && b <= 180)) return false;
    lat = a;
    lon = b;
    return true;
}

// Requests -------------------------------------------------------------------------------------------------------
static bool fits(int n, size_t size){ return n > 0 && (size_t)n < size; }

static void utc_Date(uint32_t t, char* out, size_t size, bool dashes){
    const int32_t z = (int32_t)(t / 86400) + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t y = (int32_t)yoe + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    const int32_t year = y + (m <= 2);
    if (dashes) snprintf(out, size, "%04d-%02u-%02u", (int)year, (unsigned)m, (unsigned)d);
    else snprintf(out, size, "%04d%02u%02u", (int)year, (unsigned)m, (unsigned)d);
}

bool tdw_Url_Marine(char* url, size_t size, double lat, double lon){
    return fits(snprintf(url, size,
        "https://marine-api.open-meteo.com/v1/marine?latitude=%.4f&longitude=%.4f"
        "&current=wave_height,wave_direction,wave_period,swell_wave_height,swell_wave_direction,swell_wave_period,"
        "sea_surface_temperature,ocean_current_velocity,ocean_current_direction"
        "&hourly=sea_level_height_msl,wave_height&past_days=1&forecast_days=7&timezone=auto&timeformat=unixtime", lat, lon), size);
}

bool tdw_Url_Weather(char* url, size_t size, double lat, double lon){
    return fits(snprintf(url, size,
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,precipitation,is_day"
        "&hourly=precipitation_probability,precipitation,wind_gusts_10m,weather_code&daily=sunrise,sunset"
        "&forecast_days=2&timezone=auto&timeformat=unixtime", lat, lon), size);
}

bool tdw_Url_Flood(char* url, size_t size, double lat, double lon){
    return fits(snprintf(url, size, "https://flood-api.open-meteo.com/v1/flood?latitude=%.4f&longitude=%.4f"
        "&daily=river_discharge&past_days=7&forecast_days=30&timeformat=unixtime", lat, lon), size);
}

bool tdw_Url_Flood_Climate(char* url, size_t size, double lat, double lon, uint32_t now){
    char from[12], to[12];
    utc_Date(now - 1827u * 86400u, from, sizeof(from), true);      // 5 years: one answer of about 36 KB
    utc_Date(now - 2u * 86400u, to, sizeof(to), true);
    return fits(snprintf(url, size, "https://flood-api.open-meteo.com/v1/flood?latitude=%.4f&longitude=%.4f"
        "&daily=river_discharge&start_date=%s&end_date=%s", lat, lon, from, to), size);
}

bool tdw_Url_Places(char* url, size_t size, const char* query){
    char q[160];
    size_t k = 0;
    for (const unsigned char* p = (const unsigned char*)query; *p && k + 4 < sizeof(q); p++){
        if (isalnum(*p) || *p == '-' || *p == '.' || *p == '_') q[k++] = (char)*p;
        else { snprintf(q + k, sizeof(q) - k, "%%%02X", *p); k += 3; }
    }
    q[k] = '\0';
    return fits(snprintf(url, size, "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=8&language=en&format=json", q), size);
}

bool tdw_Url_Noaa_Hilo(char* url, size_t size, const char* station, uint8_t datum, uint32_t now){
    char from[12];
    utc_Date(now - 86400u, from, sizeof(from), false);
    return fits(snprintf(url, size, "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?begin_date=%s&range=120&station=%s"
        "&product=predictions&datum=%s&time_zone=gmt&interval=hilo&units=metric&application=Pixlpal&format=json",
        from, station, datum ? "MSL" : "MLLW"), size);
}

bool tdw_Url_Noaa_Level(char* url, size_t size, const char* station, uint8_t datum){
    return fits(snprintf(url, size, "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?date=latest&station=%s"
        "&product=water_level&datum=%s&time_zone=gmt&units=metric&application=Pixlpal&format=json", station, datum ? "MSL" : "MLLW"), size);
}

bool tdw_Url_WorldTides(char* url, size_t size, double lat, double lon, uint8_t datum, uint32_t now, const char* key){
    char from[12];
    utc_Date(now - 86400u, from, sizeof(from), true);
    return fits(snprintf(url, size, "https://www.worldtides.info/api/v3?heights&extremes&lat=%.4f&lon=%.4f&date=%s&days=4&step=1800"
        "&datum=%s&key=%s", lat, lon, from, datum ? "MSL" : "CD", key), size);
}

bool tdw_Url_Stormglass(char* url, size_t size, double lat, double lon, uint8_t datum, uint32_t now){
    return fits(snprintf(url, size, "https://api.stormglass.io/v2/tide/extremes/point?lat=%.4f&lng=%.4f&start=%lu&end=%lu&datum=%s",
        lat, lon, (unsigned long)(now - 12u * 3600u), (unsigned long)(now + 84u * 3600u), datum ? "MSL" : "MLLW"), size);
}

// Answers --------------------------------------------------------------------------------------------------------
static bool parse(JsonDocument& doc, const char* body, size_t length){
    if (!body || !length) return false;
    return !deserializeJson(doc, body, length);
}

static float num(JsonVariantConst v){
    if (v.is<float>() || v.is<long>()) return v.as<float>();
    if (v.is<const char*>()){ char* e; const float f = strtof(v.as<const char*>(), &e); return e != v.as<const char*>() ? f : NAN; }
    return NAN;
}

static void copy_Text(char* dest, size_t size, const char* text){ snprintf(dest, size, "%s", text ? text : ""); }

static void om_Error(JsonDocument& doc, int code, char* why, size_t size){
    if (doc["reason"].is<const char*>()) snprintf(why, size, "Open-Meteo: %s", doc["reason"].as<const char*>());
    else snprintf(why, size, code > 0 ? "Open-Meteo answered %d" : "No answer from Open-Meteo", code);
}

bool tdw_Parse_Marine(const char* body, size_t length, int httpCode, uint32_t now, double lat, double lon, Tdw_Sea_t& sea,
                      Tdw_Tide_t* tide, int32_t& utcOffset, char* tz, size_t tzSize, char* why, size_t whySize){
    JsonDocument doc(tdw_Json_Allocator());
    if (!parse(doc, body, length) || httpCode != 200 || doc["error"].as<bool>()){ om_Error(doc, httpCode, why, whySize); return false; }
    utcOffset = doc["utc_offset_seconds"] | 0;
    copy_Text(tz, tzSize, doc["timezone_abbreviation"] | "");
    const float cellKm = tdw_Km(lat, lon, doc["latitude"] | lat, doc["longitude"] | lon);
    JsonObjectConst c = doc["current"];
    Tdw_Sea_t s = {};
    s.cellKm = cellKm;
    s.wave = num(c["wave_height"]);
    s.waveDir = num(c["wave_direction"]);
    s.wavePeriod = num(c["wave_period"]);
    s.swell = num(c["swell_wave_height"]);
    s.swellDir = num(c["swell_wave_direction"]);
    s.swellPeriod = num(c["swell_wave_period"]);
    s.sst = num(c["sea_surface_temperature"]);
    s.current = num(c["ocean_current_velocity"]);
    s.currentDir = num(c["ocean_current_direction"]);
    s.waveMax24 = NAN;
    JsonArrayConst times = doc["hourly"]["time"];
    JsonArrayConst waves = doc["hourly"]["wave_height"];
    JsonArrayConst level = doc["hourly"]["sea_level_height_msl"];
    for (size_t i = 0; i < times.size(); i++){
        const uint32_t t = times[i] | 0u;
        const float w = num(waves[i]);
        if (t + 3600 <= now || t > now + 24 * 3600 || isnan(w)) continue;
        if (isnan(s.waveMax24) || w > s.waveMax24){ s.waveMax24 = w; s.waveMaxAt = t; }
    }
    s.known = cellKm <= TDW_COAST_KM && !isnan(s.wave);
    s.updated = now;
    sea = s;
    if (tide){
        Tdw_Tide_t& t = *tide;
        t.source = TDW_SRC_OPENMETEO;
        t.station[0] = '\0';
        t.stationKm = cellKm;
        copy_Text(t.datum, sizeof(t.datum), "MSL");
        t.observed = false;
        t.n = 0;
        // Hourly over 8 days: at most TDW_SERIES_MAX points, the oldest dropped.
        const size_t first = times.size() > TDW_SERIES_MAX ? times.size() - TDW_SERIES_MAX : 0;
        for (size_t i = first; i < times.size(); i++){
            const float h = num(level[i]);
            if (isnan(h)) continue;
            t.s[t.n].t = times[i] | 0u;
            t.s[t.n].h = h;
            t.n++;
        }
        t.fromExtremes = false;
        t.ne = (uint8_t)tdw_Extremes_From_Series(t.s, t.n, t.e, TDW_EXT_MAX);
        if (cellKm > TDW_COAST_KM || t.n < 24){
            t.n = 0;
            t.ne = 0;
            t.state = TDW_NONE;
            snprintf(t.why, sizeof(t.why), "No sea near this place in Open-Meteo's ocean model");
        } else { t.state = TDW_OK; t.why[0] = '\0'; t.updated = now; }
    }
    return true;
}

bool tdw_Parse_Weather(const char* body, size_t length, int httpCode, uint32_t now, uint8_t leadHours, Tdw_Weather_t& wx,
                       char* why, size_t whySize){
    JsonDocument doc(tdw_Json_Allocator());
    if (!parse(doc, body, length) || httpCode != 200 || doc["error"].as<bool>()){ om_Error(doc, httpCode, why, whySize); return false; }
    Tdw_Weather_t w = {};
    JsonObjectConst c = doc["current"];
    w.temp = num(c["temperature_2m"]);
    w.code = c["weather_code"] | -1;
    w.wind = num(c["wind_speed_10m"]);
    w.windDir = num(c["wind_direction_10m"]);
    w.gust = num(c["wind_gusts_10m"]);
    w.rain = num(c["precipitation"]);
    w.day = (c["is_day"] | 1) != 0;
    w.stormIn = -1;
    w.gustMax = w.gust;
    w.gustMaxAt = now;
    JsonArrayConst times = doc["hourly"]["time"];
    JsonArrayConst prob = doc["hourly"]["precipitation_probability"];
    JsonArrayConst mm = doc["hourly"]["precipitation"];
    JsonArrayConst gusts = doc["hourly"]["wind_gusts_10m"];
    JsonArrayConst codes = doc["hourly"]["weather_code"];
    int start = -1;
    for (size_t i = 0; i < times.size(); i++){
        const uint32_t t = times[i] | 0u;
        if (t <= now && now < t + 3600){ start = (int)i; break; }
    }
    if (start >= 0){
        for (int k = 0; k < TDW_RAIN_HOURS && start + k < (int)times.size(); k++){
            const float p = num(prob[start + k]);
            w.rainProb[k] = isnan(p) ? 0 : (uint8_t)std::min(100.0f, std::max(0.0f, p));
            const float r = num(mm[start + k]);
            w.rainMm[k] = isnan(r) ? 0 : r;
            const int code = codes[start + k] | 0;
            if (code >= 95 && w.stormIn < 0) w.stormIn = (int8_t)k;
        }
        for (int k = 0; k <= leadHours && start + k < (int)times.size(); k++){
            const float g = num(gusts[start + k]);
            if (!isnan(g) && (isnan(w.gustMax) || g > w.gustMax)){ w.gustMax = g; w.gustMaxAt = times[start + k] | 0u; }
        }
    }
    JsonArrayConst rise = doc["daily"]["sunrise"], set = doc["daily"]["sunset"];
    for (int i = 0; i < 2; i++){ w.sunrise[i] = rise[i] | 0u; w.sunset[i] = set[i] | 0u; }
    w.known = !isnan(w.wind);
    w.updated = now;
    wx = w;
    return true;
}

bool tdw_Parse_Flood(const char* body, size_t length, int httpCode, uint32_t now, double lat, double lon, Tdw_River_t& r,
                     char* why, size_t whySize){
    JsonDocument doc(tdw_Json_Allocator());
    if (!parse(doc, body, length) || httpCode != 200 || doc["error"].as<bool>()){ om_Error(doc, httpCode, why, whySize); return false; }
    JsonArrayConst times = doc["daily"]["time"];
    JsonArrayConst q = doc["daily"]["river_discharge"];
    r.n = 0;
    r.known = false;
    r.cellKm = tdw_Km(lat, lon, doc["latitude"] | lat, doc["longitude"] | lon);
    for (size_t i = 0; i < times.size() && r.n < TDW_RIVER_DAYS; i++){
        const float v = num(q[i]);
        if (r.n == 0) r.day0 = times[i] | 0u;
        r.q[r.n++] = isnan(v) ? -1.0f : v;
    }
    r.today = 0;
    for (uint8_t i = 0; i < r.n; i++) if (r.day0 + i * 86400u <= now) r.today = i;
    r.peak = -1;
    for (uint8_t i = r.today; i < r.n; i++) if (r.q[i] > r.peak){ r.peak = r.q[i]; r.peakDay = r.day0 + i * 86400u; }
    // No river (the ocean, a desert): every day is missing or nothing flows.
    r.known = r.n > r.today && r.q[r.today] > 0.5f;
    r.updated = now;
    if (!r.known) snprintf(why, whySize, "No river big enough for the flood model near this place");
    return true;
}

// The discharge numbers are read straight from the text: a JSON document of thousands of values would take an
// installed app (interpreted) a few seconds to build.
bool tdw_Parse_Flood_Climate(const char* body, size_t length, int httpCode, Tdw_River_t& r, float* scratch, size_t scratchCount){
    if (!body || !length || httpCode != 200) return false;
    static const char KEY[] = "\"river_discharge\":[";
    const char* end = body + length;
    const char* p = nullptr;
    for (const char* q = body; q + sizeof(KEY) - 1 <= end; q++) if (memcmp(q, KEY, sizeof(KEY) - 1) == 0){ p = q + sizeof(KEY) - 1; break; }
    if (!p) return false;
    size_t n = 0;
    while (p < end && *p != ']'){
        while (p < end && (*p == ' ' || *p == ',')) p++;
        if (p >= end || *p == ']') break;
        if (*p == 'n'){ p += 4; continue; }                                  // null: a missing day
        char* stop;
        const float f = strtof(p, &stop);
        if (stop == p) return false;
        if (!isnan(f) && n < scratchCount) scratch[n++] = f;
        p = stop;
    }
    if (n < 365) return false;
    std::sort(scratch, scratch + n);
    auto pct = [&](float p){ return scratch[std::min(n - 1, (size_t)(p * (n - 1) + 0.5f))]; };
    r.p10 = pct(0.10f);
    r.p50 = pct(0.50f);
    r.p90 = pct(0.90f);
    r.p99 = pct(0.99f);
    r.max = scratch[n - 1];
    r.climate = r.max > 0.5f;
    return r.climate;
}

int tdw_Parse_Places(const char* body, size_t length, int httpCode, char names[][TDW_PLACE_LEN], char details[][48],
                     double* lats, double* lons, int max){
    JsonDocument doc(tdw_Json_Allocator());
    if (!parse(doc, body, length) || httpCode != 200 || doc["error"].as<bool>()) return -1;
    int count = 0;
    for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()){
        if (count >= max) break;
        const char* name = r["name"] | "";
        if (!name[0] || !r["latitude"].is<double>() || !r["longitude"].is<double>()) continue;
        const char* country = r["country"] | (r["country_code"] | "");
        snprintf(names[count], TDW_PLACE_LEN, "%s%s%s", name, country[0] ? ", " : "", country);
        snprintf(details[count], 48, "%s", r["admin1"] | "");
        lats[count] = r["latitude"];
        lons[count] = r["longitude"];
        count++;
    }
    return count;
}

uint32_t tdw_Parse_Noaa_Time(const char* text){
    int y, mo, d, h, mi;
    if (!text || sscanf(text, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400 + h * 3600 + mi * 60);
}

static void finish_From_Extremes(Tdw_Tide_t& t){
    // A smooth curve between the highs and lows, as many points as fit.
    uint32_t step = 1800;
    if (t.ne >= 2){
        const uint32_t span = t.e[t.ne - 1].t - t.e[0].t;
        while (span / step + 1 > TDW_SERIES_MAX) step += 300;
    }
    t.n = (uint16_t)tdw_Series_From_Extremes(t.e, t.ne, step, t.s, TDW_SERIES_MAX);
    t.fromExtremes = true;
}

uint8_t tdw_Parse_Noaa_Hilo(const char* body, size_t length, int httpCode, Tdw_Tide_t& t){
    JsonDocument doc(tdw_Json_Allocator());
    t.why[0] = '\0';
    if (!parse(doc, body, length)){
        snprintf(t.why, sizeof(t.why), httpCode > 0 ? "NOAA answered %d" : "No answer from NOAA", httpCode);
        return TDW_ERROR;
    }
    if (doc["error"]["message"].is<const char*>()){
        snprintf(t.why, sizeof(t.why), "NOAA: %s", doc["error"]["message"].as<const char*>());
        return TDW_NONE;
    }
    t.ne = 0;
    for (JsonVariantConst p : doc["predictions"].as<JsonArrayConst>()){
        if (t.ne >= TDW_EXT_MAX) break;
        const uint32_t at = tdw_Parse_Noaa_Time(p["t"] | "");
        const float h = num(p["v"]);
        const char* type = p["type"] | (p["ty"] | "");
        if (!at || isnan(h) || !type[0]) continue;
        t.e[t.ne].t = at;
        t.e[t.ne].h = h;
        t.e[t.ne].high = toupper((unsigned char)type[0]) == 'H';
        t.ne++;
    }
    if (t.ne < 2){ snprintf(t.why, sizeof(t.why), "NOAA sent no tides for this station"); return TDW_NONE; }
    t.source = TDW_SRC_NOAA;
    finish_From_Extremes(t);
    return TDW_OK;
}

bool tdw_Parse_Noaa_Level(const char* body, size_t length, float& h, uint32_t& at){
    JsonDocument doc(tdw_Json_Allocator());
    if (!parse(doc, body, length) || !doc["data"].is<JsonArrayConst>()) return false;
    JsonArrayConst data = doc["data"];
    if (data.size() == 0) return false;
    JsonVariantConst last = data[data.size() - 1];
    h = num(last["v"]);
    at = tdw_Parse_Noaa_Time(last["t"] | "");
    return !isnan(h) && at;
}

uint8_t tdw_Parse_WorldTides(const char* body, size_t length, int httpCode, Tdw_Tide_t& t, uint32_t& credits){
    JsonDocument doc(tdw_Json_Allocator());
    t.why[0] = '\0';
    credits = 0;
    if (!parse(doc, body, length)){
        snprintf(t.why, sizeof(t.why), httpCode > 0 ? "WorldTides answered %d" : "No answer from WorldTides", httpCode);
        return TDW_ERROR;
    }
    const int status = doc["status"] | httpCode;
    credits = doc["callCount"] | 0u;
    if (status != 200){
        const char* e = doc["error"] | "refused the request";
        snprintf(t.why, sizeof(t.why), "WorldTides: %s", e);
        if (has_Word(e, "key")) return TDW_KEY;
        if (has_Word(e, "credit")) return TDW_QUOTA;
        return TDW_ERROR;
    }
    t.n = 0;
    JsonArrayConst heights = doc["heights"];
    const size_t every = heights.size() > TDW_SERIES_MAX ? (heights.size() + TDW_SERIES_MAX - 1) / TDW_SERIES_MAX : 1;
    for (size_t i = 0; i < heights.size() && t.n < TDW_SERIES_MAX; i += every){
        const float h = num(heights[i]["height"]);
        const uint32_t at = heights[i]["dt"] | 0u;
        if (isnan(h) || !at) continue;
        t.s[t.n].t = at;
        t.s[t.n].h = h;
        t.n++;
    }
    t.ne = 0;
    for (JsonVariantConst e : doc["extremes"].as<JsonArrayConst>()){
        if (t.ne >= TDW_EXT_MAX) break;
        const float h = num(e["height"]);
        const uint32_t at = e["dt"] | 0u;
        if (isnan(h) || !at) continue;
        t.e[t.ne].t = at;
        t.e[t.ne].h = h;
        t.e[t.ne].high = toupper((unsigned char)(e["type"] | "L")[0]) == 'H';
        t.ne++;
    }
    if (t.n < 2 && t.ne < 2){ snprintf(t.why, sizeof(t.why), "WorldTides has no tides for this place"); return TDW_NONE; }
    t.fromExtremes = false;
    if (t.n < 2) finish_From_Extremes(t);
    t.source = TDW_SRC_WORLDTIDES;
    copy_Text(t.station, sizeof(t.station), doc["station"] | "");
    copy_Text(t.datum, sizeof(t.datum), doc["responseDatum"] | "");
    t.stationKm = tdw_Km(doc["requestLat"] | 0.0, doc["requestLon"] | 0.0, doc["responseLat"] | 0.0, doc["responseLon"] | 0.0);
    t.observed = false;
    return TDW_OK;
}

uint8_t tdw_Parse_Stormglass(const char* body, size_t length, int httpCode, Tdw_Tide_t& t, uint32_t& quota, uint32_t& used){
    JsonDocument doc(tdw_Json_Allocator());
    t.why[0] = '\0';
    const bool ok = parse(doc, body, length);
    if (httpCode == 401 || httpCode == 403){
        snprintf(t.why, sizeof(t.why), "Stormglass: %s", ok && doc["errors"]["key"].is<const char*>() ? doc["errors"]["key"].as<const char*>() : "the key was refused");
        return TDW_KEY;
    }
    if (httpCode == 402 || httpCode == 429){ snprintf(t.why, sizeof(t.why), "Stormglass: today's requests are used up"); return TDW_QUOTA; }
    if (!ok || httpCode != 200){
        snprintf(t.why, sizeof(t.why), httpCode > 0 ? "Stormglass answered %d" : "No answer from Stormglass", httpCode);
        return TDW_ERROR;
    }
    JsonObjectConst meta = doc["meta"];
    quota = meta["dailyQuota"] | quota;
    used = meta["requestCount"] | used;
    t.ne = 0;
    for (JsonVariantConst e : doc["data"].as<JsonArrayConst>()){
        if (t.ne >= TDW_EXT_MAX) break;
        const float h = num(e["height"]);
        const uint32_t at = mtb_Kit_Parse_Iso_Time(e["time"] | "");
        if (isnan(h) || !at) continue;
        t.e[t.ne].t = at;
        t.e[t.ne].h = h;
        t.e[t.ne].high = toupper((unsigned char)(e["type"] | "l")[0]) == 'H';
        t.ne++;
    }
    if (t.ne < 2){ snprintf(t.why, sizeof(t.why), "Stormglass has no tides for this place"); return TDW_NONE; }
    t.source = TDW_SRC_STORMGLASS;
    copy_Text(t.station, sizeof(t.station), meta["station"]["name"] | "");
    t.stationKm = num(meta["station"]["distance"]);
    if (isnan(t.stationKm)) t.stationKm = 0;
    copy_Text(t.datum, sizeof(t.datum), meta["datum"] | "");
    t.observed = false;
    finish_From_Extremes(t);
    return TDW_OK;
}

const char* tdw_Stormglass_Plan(uint32_t quota){
    if (quota == 0) return "Stormglass";
    if (quota <= 50) return "Stormglass Free";
    if (quota <= 500) return "Stormglass Small";
    if (quota <= 5000) return "Stormglass Medium";
    return "Stormglass Large";
}

// The NOAA station list ------------------------------------------------------------------------------------------
// {"count":3502,"units":null,"stations":[{"state":"HI","tidepredoffsets":{...},"type":"S","id":"1610367",
//  "name":"Nonopapa, Niihau Island","lat":21.87,"lng":-160.235,...},...]}: each station is an object at depth 3.
void tdw_Noaa_Scan_Begin(Tdw_Noaa_Scan_t& s, double lat, double lon){
    memset(&s, 0, sizeof(s));
    s.lat = lat;
    s.lon = lon;
    s.bestKm = 1e9f;
}

static void scan_Number_End(Tdw_Noaa_Scan_t& s){
    if (s.depth == 3 && s.expectValue && s.tokenLen){
        s.token[s.tokenLen] = '\0';
        if (!strcmp(s.key, "lat")){ s.sLat = atof(s.token); s.have |= 4; }
        else if (!strcmp(s.key, "lng")){ s.sLon = atof(s.token); s.have |= 8; }
        s.expectValue = false;
    }
    s.tokenLen = 0;
}

void tdw_Noaa_Scan_Feed(Tdw_Noaa_Scan_t& s, const char* data, size_t length){
    for (size_t i = 0; i < length; i++){
        const char c = data[i];
        if (s.inString){
            if (s.escape){ s.escape = false; if (s.tokenLen < sizeof(s.token) - 1) s.token[s.tokenLen++] = c; }
            else if (c == '\\') s.escape = true;
            else if (c == '"'){
                s.inString = false;
                s.token[s.tokenLen] = '\0';
                if (s.depth == 3){
                    if (s.readingKey){ snprintf(s.key, sizeof(s.key), "%s", s.token); s.readingKey = false; }
                    else if (s.expectValue){
                        if (!strcmp(s.key, "id")){ snprintf(s.id, sizeof(s.id), "%s", s.token); s.have |= 1; }
                        else if (!strcmp(s.key, "name")){ snprintf(s.name, sizeof(s.name), "%s", s.token); s.have |= 2; }
                        s.expectValue = false;
                    }
                }
                s.tokenLen = 0;
            }
            else if (s.tokenLen < sizeof(s.token) - 1) s.token[s.tokenLen++] = c;
            continue;
        }
        switch (c){
            case '"': s.inString = true; s.tokenLen = 0; break;
            case '{': case '[':
                s.depth++;
                if (c == '{' && s.depth == 3){ s.have = 0; s.readingKey = true; s.expectValue = false; }
                break;
            case '}': case ']':
                scan_Number_End(s);
                if (c == '}' && s.depth == 3){
                    if ((s.have & 13) == 13){
                        s.stations++;
                        const float km = tdw_Km(s.lat, s.lon, s.sLat, s.sLon);
                        if (km < s.bestKm){
                            s.bestKm = km;
                            snprintf(s.bestId, sizeof(s.bestId), "%s", s.id);
                            snprintf(s.bestName, sizeof(s.bestName), "%s", (s.have & 2) ? s.name : s.id);
                        }
                    }
                    s.have = 0;
                }
                if (s.depth > 0) s.depth--;
                if (s.depth == 3) s.expectValue = false;        // a nested value ended
                break;
            case ':': if (s.depth == 3){ s.expectValue = true; s.tokenLen = 0; } break;
            case ',':
                scan_Number_End(s);
                if (s.depth == 3){ s.readingKey = true; s.expectValue = false; }
                break;
            default:
                if (s.depth == 3 && s.expectValue && (isdigit((unsigned char)c) || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+')
                    && s.tokenLen < sizeof(s.token) - 1) s.token[s.tokenLen++] = c;
                break;
        }
    }
}

// Tide arithmetic ----------------------------------------------------------------------------------------------
int tdw_Extremes_From_Series(const Tdw_Point_t* s, int n, Tdw_Extreme_t* e, int max){
    int k = 0;
    for (int i = 1; i + 1 < n; i++){
        const float a = s[i - 1].h, b = s[i].h, c = s[i + 1].h;
        const bool high = b >= a && b > c, low = b <= a && b < c;
        if (!high && !low) continue;
        // The parabola through the three points: its top or bottom.
        const float den = a - 2 * b + c;
        float off = den != 0 ? 0.5f * (a - c) / den : 0;
        if (off > 1) off = 1;
        if (off < -1) off = -1;
        const float dt = (float)(s[i + 1].t - s[i - 1].t) / 2;
        Tdw_Extreme_t x = {(uint32_t)((float)s[i].t + off * dt), b - 0.25f * (a - c) * off, (uint8_t)high};
        if (k > 0 && e[k - 1].high == x.high){
            if (x.high ? x.h > e[k - 1].h : x.h < e[k - 1].h) e[k - 1] = x;
            continue;
        }
        if (k > 0 && fabsf(x.h - e[k - 1].h) < 0.03f){ k--; continue; }     // a wiggle, not a tide
        if (k < max) e[k++] = x;
    }
    return k;
}

int tdw_Series_From_Extremes(const Tdw_Extreme_t* e, int ne, uint32_t step, Tdw_Point_t* s, int max){
    if (ne < 2 || step == 0) return 0;
    int n = 0, j = 0;
    for (uint32_t t = e[0].t; t <= e[ne - 1].t && n < max; t += step){
        while (j + 2 < ne && t > e[j + 1].t) j++;
        const float span = (float)(e[j + 1].t - e[j].t);
        const float f = span > 0 ? (float)(t - e[j].t) / span : 0;
        s[n].t = t;
        s[n].h = e[j].h + (e[j + 1].h - e[j].h) * (1 - cosf((float)M_PI * f)) / 2;
        n++;
    }
    return n;
}

bool tdw_Level_At(const Tdw_Tide_t& t, uint32_t at, float& h){
    // Between highs and lows given by the provider: the half cosine itself, exact at the turns.
    if (t.fromExtremes && t.ne >= 2 && at >= t.e[0].t && at <= t.e[t.ne - 1].t){
        int j = 0;
        while (j + 2 < t.ne && at > t.e[j + 1].t) j++;
        const float span = (float)(t.e[j + 1].t - t.e[j].t);
        const float f = span > 0 ? (float)(at - t.e[j].t) / span : 0;
        h = t.e[j].h + (t.e[j + 1].h - t.e[j].h) * (1 - cosf((float)M_PI * f)) / 2;
        return true;
    }
    if (t.n < 2 || at < t.s[0].t || at > t.s[t.n - 1].t) return false;
    int lo = 0, hi = t.n - 1;
    while (hi - lo > 1){ const int mid = (lo + hi) / 2; if (t.s[mid].t <= at) lo = mid; else hi = mid; }
    const float span = (float)(t.s[hi].t - t.s[lo].t);
    const float f = span > 0 ? (float)(at - t.s[lo].t) / span : 0;
    h = t.s[lo].h + (t.s[hi].h - t.s[lo].h) * f;
    return true;
}

int tdw_Next_Extreme(const Tdw_Tide_t& t, uint32_t now, int wantHigh){
    for (int i = 0; i < t.ne; i++){
        if (t.e[i].t <= now) continue;
        if (wantHigh >= 0 && t.e[i].high != (wantHigh != 0)) continue;
        return i;
    }
    return -1;
}

float tdw_Range(const Tdw_Tide_t& t, uint32_t from){
    float hi = -1e9f, lo = 1e9f;
    for (int i = 0; i < t.ne; i++){
        if (t.e[i].t < from || t.e[i].t > from + 86400) continue;
        if (t.e[i].high) hi = std::max(hi, t.e[i].h); else lo = std::min(lo, t.e[i].h);
    }
    return hi > -1e8f && lo < 1e8f ? hi - lo : NAN;
}

// Judgements -----------------------------------------------------------------------------------------------------
const char* tdw_Sea_State(float m){
    if (isnan(m)) return "";
    if (m < 0.1f) return "Calm";
    if (m < 0.5f) return "Smooth";
    if (m < 1.25f) return "Slight";
    if (m < 2.5f) return "Moderate";
    if (m < 4.0f) return "Rough";
    if (m < 6.0f) return "Very rough";
    if (m < 9.0f) return "High";
    return "Very high";
}

uint8_t tdw_Sea_Severity(float m){
    if (isnan(m) || m < 0.5f) return 0;
    if (m < 1.25f) return 1;
    if (m < 2.5f) return 2;
    if (m < 4.0f) return 3;
    return 4;
}

int tdw_Beaufort(float kmh){
    static const float top[] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
    if (isnan(kmh)) return 0;
    for (int b = 0; b < 12; b++) if (kmh < top[b]) return b;
    return 12;
}

const char* tdw_Beaufort_Name(int b){
    static const char* const names[] = {"Calm", "Light air", "Light breeze", "Gentle breeze", "Moderate breeze", "Fresh breeze",
                                        "Strong breeze", "Near gale", "Gale", "Strong gale", "Storm", "Violent storm", "Hurricane force"};
    return names[b < 0 ? 0 : b > 12 ? 12 : b];
}

const char* tdw_Compass(float d){
    static const char* const points[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    if (isnan(d)) return "";
    const int i = (int)floorf(fmodf(d + 360.0f + 11.25f, 360.0f) / 22.5f);
    return points[i & 15];
}

uint8_t tdw_River_Level(const Tdw_River_t& r, float q){
    if (!r.climate || q < 0) return TDW_RIVER_UNKNOWN;
    if (q > r.max) return TDW_RIVER_EXTREME;
    if (q > r.p99) return TDW_RIVER_VERY_HIGH;
    if (q > r.p90) return TDW_RIVER_HIGH;
    if (q < r.p10) return TDW_RIVER_LOW;
    return TDW_RIVER_NORMAL;
}

const char* tdw_River_Name(uint8_t level){
    switch (level){
        case TDW_RIVER_LOW: return "Low";
        case TDW_RIVER_NORMAL: return "Normal";
        case TDW_RIVER_HIGH: return "High";
        case TDW_RIVER_VERY_HIGH: return "Very high";
        case TDW_RIVER_EXTREME: return "Highest in 10 years";
        default: return "";
    }
}

const char* tdw_Spring_Neap(float age){
    const float synodic = 29.53f;
    const float toNewOrFull = std::min({fabsf(age), fabsf(age - synodic / 2), fabsf(age - synodic)});
    const float toQuarter = std::min(fabsf(age - synodic / 4), fabsf(age - 3 * synodic / 4));
    if (toNewOrFull <= 2.0f) return "Spring tides";
    if (toQuarter <= 2.0f) return "Neap tides";
    return "";
}

float tdw_Days_To_Spring(float age){
    const float half = 29.53f / 2;
    return age < half ? half - age : 29.53f - age;
}

const char* tdw_Weather_Text(int code){
    switch (code){
        case 0: return "Clear";
        case 1: return "Mostly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: case 77: return "Snow";
        case 80: case 81: return "Showers";
        case 82: return "Violent showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm, hail";
        default: return "";
    }
}

// Alerts ---------------------------------------------------------------------------------------------------------
int tdw_Alerts(const Tdw_Settings_t& s, const Tdw_Data_t& d, uint32_t now, Tdw_Alert_t* out, int max){
    int n = 0;
    if (!s.alertOn) return 0;
    const uint32_t lead = (uint32_t)s.leadHours * 3600u;
    char a[24], b[24], c[16];
    auto add = [&](uint8_t kind, uint32_t at) -> char* {
        if (n >= max) return nullptr;
        out[n].kind = kind;
        out[n].at = at;
        out[n].text[0] = '\0';
        return out[n++].text;
    };
    // High water: a high tide at or over the level, coming within the lead time (or the gauge over it now).
    if (s.highWaterCm && d.tide.state == TDW_OK){
        const float limit = s.highWaterCm / 100.0f;
        tdw_Height_Text(b, sizeof(b), limit, s.imperial);
        if (d.tide.observed && now - d.tide.obsT < 3600 && d.tide.obsH >= limit){
            tdw_Height_Text(a, sizeof(a), d.tide.obsH, s.imperial);
            char* t = add(TDW_A_HIGH_WATER, now);
            if (t) snprintf(t, sizeof(out[0].text), "High water now: the gauge reads %s (your alert: %s)", a, b);
        } else for (int i = 0; i < d.tide.ne; i++){
            const Tdw_Extreme_t& e = d.tide.e[i];
            if (!e.high || e.h < limit || e.t + 3600 < now || e.t > now + lead) continue;
            tdw_Height_Text(a, sizeof(a), e.h, s.imperial);
            tdw_Clock(c, sizeof(c), e.t, d.utcOffset);
            char* t = add(TDW_A_HIGH_WATER, e.t);
            if (t) snprintf(t, sizeof(out[0].text), "High water %s at %s (your alert: %s)", a, c, b);
            break;
        }
    }
    // Waves.
    if (s.waveAlertDm && d.sea.known){
        const float limit = s.waveAlertDm / 10.0f;
        const bool nowOver = !isnan(d.sea.wave) && d.sea.wave >= limit;
        const bool soon = !isnan(d.sea.waveMax24) && d.sea.waveMax24 >= limit && d.sea.waveMaxAt <= now + lead;
        if (nowOver || soon){
            const float h = nowOver ? d.sea.wave : d.sea.waveMax24;
            tdw_Height_Text(a, sizeof(a), h, s.imperial);
            tdw_Clock(c, sizeof(c), d.sea.waveMaxAt, d.utcOffset);
            char* t = add(TDW_A_WAVES, nowOver ? now : d.sea.waveMaxAt);
            if (t){
                if (nowOver) snprintf(t, sizeof(out[0].text), "%s sea: waves %s", tdw_Sea_State(h), a);
                else snprintf(t, sizeof(out[0].text), "%s sea by %s: waves up to %s", tdw_Sea_State(h), c, a);
            }
        }
    }
    // Gusts.
    if (s.gustAlert && d.wx.known && !isnan(d.wx.gustMax) && d.wx.gustMax >= s.gustAlert && d.wx.gustMaxAt <= now + lead){
        tdw_Wind_Text(a, sizeof(a), d.wx.gustMax, s.windUnit);
        const bool nowOver = !isnan(d.wx.gust) && d.wx.gust >= s.gustAlert;
        tdw_Clock(c, sizeof(c), d.wx.gustMaxAt, d.utcOffset);
        char* t = add(TDW_A_GUSTS, nowOver ? now : d.wx.gustMaxAt);
        if (t){
            if (nowOver) snprintf(t, sizeof(out[0].text), "Strong wind: gusts of %s now", a);
            else snprintf(t, sizeof(out[0].text), "Strong wind by %s: gusts up to %s", c, a);
        }
    }
    // Thunder.
    if (s.stormAlert && d.wx.known && d.wx.stormIn >= 0 && (uint32_t)d.wx.stormIn * 3600u <= lead){
        char* t = add(TDW_A_STORM, now + (uint32_t)d.wx.stormIn * 3600u);
        if (t){
            if (d.wx.stormIn == 0) snprintf(t, sizeof(out[0].text), "Thunderstorms now: stay off the water");
            else snprintf(t, sizeof(out[0].text), "Thunderstorms likely in %d h: stay off the water", d.wx.stormIn);
        }
    }
    // The river: now, or its peak in the coming weeks.
    if (s.riverAlert && d.river.known && d.river.climate){
        const float nowQ = d.river.q[d.river.today];
        const uint8_t nowLevel = tdw_River_Level(d.river, nowQ), peakLevel = tdw_River_Level(d.river, d.river.peak);
        if (nowLevel >= s.riverAlert){
            char* t = add(TDW_A_RIVER, now);
            if (t) snprintf(t, sizeof(out[0].text), "River %s: %.0f m3/s, %s", tdw_River_Name(nowLevel), (double)nowQ,
                            nowLevel == TDW_RIVER_EXTREME ? "more than any day in 10 years" : nowLevel == TDW_RIVER_VERY_HIGH ? "top 1% of the last 10 years" : "top 10% of the last 10 years");
        } else if (peakLevel >= s.riverAlert){
            char* t = add(TDW_A_RIVER, d.river.peakDay);
            if (t) snprintf(t, sizeof(out[0].text), "River forecast %s by %s: %.0f m3/s", tdw_River_Name(peakLevel),
                            tdw_Weekday(d.river.peakDay + 43200, 0), (double)d.river.peak);
        }
    }
    std::sort(out, out + n, [](const Tdw_Alert_t& x, const Tdw_Alert_t& y){ return x.at < y.at; });
    return n;
}

// Units ----------------------------------------------------------------------------------------------------------
void tdw_Height_Text(char* dest, size_t size, float m, bool imperial, bool sign){
    if (isnan(m)){ snprintf(dest, size, "--"); return; }
    if (imperial) snprintf(dest, size, sign ? "%+.1f ft" : "%.1f ft", (double)(m * 3.28084f));
    else snprintf(dest, size, sign ? "%+.2f m" : "%.2f m", (double)m);
}

const char* tdw_Wind_Unit(uint8_t unit){
    switch (unit){ case TDW_KNOTS: return "kn"; case TDW_MPH: return "mph"; case TDW_MS: return "m/s"; default: return "km/h"; }
}

void tdw_Wind_Text(char* dest, size_t size, float kmh, uint8_t unit, bool withUnit){
    if (isnan(kmh)){ snprintf(dest, size, "--"); return; }
    float v = kmh;
    if (unit == TDW_KNOTS) v = kmh / 1.852f;
    else if (unit == TDW_MPH) v = kmh / 1.609344f;
    else if (unit == TDW_MS) v = kmh / 3.6f;
    if (unit == TDW_MS) snprintf(dest, size, withUnit ? "%.1f %s" : "%.1f", (double)v, tdw_Wind_Unit(unit));
    else snprintf(dest, size, withUnit ? "%.0f %s" : "%.0f", (double)v, tdw_Wind_Unit(unit));
}

void tdw_Temp_Text(char* dest, size_t size, float c, bool imperial){
    if (isnan(c)){ snprintf(dest, size, "--"); return; }
    if (imperial) snprintf(dest, size, "%.0fF", (double)(c * 9 / 5 + 32));
    else snprintf(dest, size, "%.1fC", (double)c);
}

void tdw_Clock(char* dest, size_t size, uint32_t t, int32_t off){
    const uint32_t local = (uint32_t)((int64_t)t + off);
    snprintf(dest, size, "%02u:%02u", (unsigned)(local % 86400 / 3600), (unsigned)(local % 3600 / 60));
}

const char* tdw_Weekday(uint32_t t, int32_t off){
    static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const uint32_t local = (uint32_t)((int64_t)t + off);
    return days[(local / 86400 + 4) % 7];
}

void tdw_Span(char* dest, size_t size, int32_t s){
    if (s < 0) s = 0;
    const int h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(dest, size, "%d h %02d min", h, m);
    else snprintf(dest, size, "%d min", m);
}

const char* tdw_Source_Name(uint8_t source){
    switch (source){
        case TDW_SRC_AUTO: return "Auto";
        case TDW_SRC_NOAA: return "NOAA";
        case TDW_SRC_WORLDTIDES: return "WorldTides";
        case TDW_SRC_STORMGLASS: return "Stormglass";
        case TDW_SRC_OPENMETEO: return "Open-Meteo";
        default: return "None";
    }
}
