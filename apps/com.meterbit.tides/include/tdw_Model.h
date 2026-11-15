/**
 * @file tdw_Model.h
 * @brief Tides & Water {3,3}: the settings, the providers' answers, the tide arithmetic, river levels and alerts for
 *        people who live by the sea, a lagoon, a lake or a river. No Arduino, so it can be tested on a PC.
 *
 * Providers (checked 10 November 2026):
 *  - Tides, best first (Auto):
 *     NOAA CO-OPS (US coasts and islands): official predictions at a tide station, free, no key. The nearest
 *       station (within 40 km) is found once per place in the 3,500-station list (api.tidesandcurrents.noaa.gov/mdapi),
 *       read as it streams. Highs and lows {"predictions":[{"t":"2026-11-10 01:07","v":"1.387","type":"H"}]}; the
 *       latest gauge reading where there is one, {"metadata":{...},"data":[{"t","v",...}]}. Errors {"error":{"message"}}.
 *     WorldTides (the user's key, worldwide, harmonic predictions at gauges or from a global model): v3, 1 credit per
 *       7 days of extremes and 1 per 7 days of heights; Pixlpal spends 2 credits a day (the $3.99 yearly plan has
 *       1,000). {"status":200,"callCount":2,"station","responseDatum","heights":[{dt,height}],"extremes":[{dt,height,
 *       type:"High"|"Low"}],"copyright"}; errors {"status":400,"error":"API key is invalid"}.
 *     Stormglass (the user's key, worldwide): /v2/tide/extremes/point, header Authorization: key. Free 10 requests a
 *       day, Small 500, Medium 5,000, Large 25,000: the plan is read from meta.dailyQuota. Pixlpal makes 2 a day.
 *       {"data":[{"height","time","type":"high"|"low"}],"meta":{"dailyQuota","requestCount","datum","station":{...}}};
 *       errors {"errors":{"key":"API key is invalid"}} (403), 402 or 429 when the day's requests are used.
 *     Open-Meteo Marine (free, no key, everywhere at sea): sea_level_height_msl from a 8 km ocean model, above mean sea
 *       level, tides included. Not for navigation; near enclosed coasts and lagoons it can be off.
 *  - Sea state: Open-Meteo Marine (waves, swell, sea temperature, currents). Free.
 *  - Wind, rain and storms, sunrise and sunset: Open-Meteo Forecast. Free.
 *  - Rivers: Open-Meteo Flood (GloFAS v4, 5 km): daily discharge of the largest river within 5 km, a week back and a
 *    month ahead, judged against the same river's last 10 years. Free.
 * Open-Meteo is free for non-commercial use (under 10,000 calls a day); Pixlpal makes about 80.
 */
#ifndef TDW_MODEL_H
#define TDW_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ntf_Notify.h"

#define TDW_NVS_KEY         "tdwSet"
#define TDW_SETTINGS_MAGIC  0x54445701u
#define TDW_PLACE_LEN       48
#define TDW_SERIES_MAX      200         /**< tide heights (hourly over 8 days, or half-hourly over 4) */
#define TDW_EXT_MAX         32          /**< highs and lows */
#define TDW_RIVER_DAYS      40
#define TDW_RAIN_HOURS      12
#define TDW_ALERTS_MAX      6
#define TDW_NOAA_KM         40.0f       /**< a NOAA station farther than this is not used */
#define TDW_COAST_KM        25.0f       /**< Open-Meteo's sea cell farther than this: no coast here */

enum Tdw_Source_t : uint8_t { TDW_SRC_AUTO = 0, TDW_SRC_NOAA, TDW_SRC_WORLDTIDES, TDW_SRC_STORMGLASS, TDW_SRC_OPENMETEO, TDW_SRC_NONE };
enum Tdw_State_t : uint8_t { TDW_WAIT = 0, TDW_OK, TDW_NONE, TDW_KEY, TDW_QUOTA, TDW_ERROR };
enum Tdw_Page_t : uint8_t { TDW_P_TIDE = 0, TDW_P_TABLE, TDW_P_SEA, TDW_P_WIND, TDW_P_RIVER, TDW_P_SKY, TDW_P_ALERTS, TDW_PAGES };
enum Tdw_Wind_Unit_t : uint8_t { TDW_KMH = 0, TDW_KNOTS, TDW_MPH, TDW_MS };
enum Tdw_River_Level_t : uint8_t { TDW_RIVER_UNKNOWN = 0, TDW_RIVER_LOW, TDW_RIVER_NORMAL, TDW_RIVER_HIGH, TDW_RIVER_VERY_HIGH, TDW_RIVER_EXTREME };
enum Tdw_Alert_Kind_t : uint8_t { TDW_A_HIGH_WATER = 0, TDW_A_WAVES, TDW_A_GUSTS, TDW_A_STORM, TDW_A_RIVER, TDW_A_KINDS };

struct Tdw_Settings_t {
    uint32_t magic;
    char place[TDW_PLACE_LEN];          /**< UTF-8 */
    double lat, lon;
    char riverPlace[TDW_PLACE_LEN];     /**< "" = the place itself */
    double riverLat, riverLon;
    uint8_t source;                     /**< Tdw_Source_t: Auto or one provider */
    uint8_t datum;                      /**< 0 chart datum (lowest tides, as on charts), 1 mean sea level */
    uint8_t imperial;                   /**< feet and °F */
    uint8_t windUnit;                   /**< Tdw_Wind_Unit_t */
    uint8_t pages;                      /**< a bit per Tdw_Page_t (the alerts page shows only with alerts) */
    uint8_t cycle, seconds;
    uint8_t alertOn, phoneAlerts, stormAlert;
    uint8_t riverAlert;                 /**< Tdw_River_Level_t from which to warn, 0 off */
    uint8_t waveAlertDm;                /**< waves from this many decimetres, 0 off */
    uint16_t gustAlert;                 /**< gusts from this many km/h, 0 off */
    uint16_t highWaterCm;               /**< water from this many cm (in the datum shown), 0 off */
    uint8_t leadHours;                  /**< warn this long before (high water, waves, gusts) */
    Ntf_Config_t notify;
    char worldTidesKey[48];             /**< secret */
    char stormglassKey[80];             /**< secret */
    // The NOAA station for this place (found once).
    uint8_t noaaChecked;                /**< 1: the list was searched for this place */
    char noaaId[12];
    char noaaName[40];
    float noaaKm;
    // WorldTides credits this month (the API says what each call cost, not what is left).
    uint32_t wtMonth, wtCredits;
};
void tdw_Settings_Default(Tdw_Settings_t& s);
void tdw_Settings_Clamp(Tdw_Settings_t& s);
/** @brief The place is in waters NOAA predicts for (the US, its islands and territories). */
bool tdw_Noaa_Region(double lat, double lon);
/** @brief Great-circle distance in km. */
float tdw_Km(double lat1, double lon1, double lat2, double lon2);
/** @brief "6.45, 3.39" or "6.45,3.39" (decimal degrees) into a place. False if not coordinates. */
bool tdw_Parse_Coordinates(const char* text, double& lat, double& lon);

// Data ---------------------------------------------------------------------------------------------------------
struct Tdw_Point_t { uint32_t t; float h; };
struct Tdw_Extreme_t { uint32_t t; float h; uint8_t high; };

struct Tdw_Tide_t {
    uint8_t source;                     /**< Tdw_Source_t that answered */
    uint8_t state;                      /**< Tdw_State_t */
    char why[72];
    char station[40];                   /**< "The Battery", or "" for a model */
    float stationKm;
    char datum[8];                      /**< "MLLW", "CD", "MSL"... as answered */
    Tdw_Point_t s[TDW_SERIES_MAX];
    uint16_t n;
    Tdw_Extreme_t e[TDW_EXT_MAX];
    uint8_t ne;
    bool fromExtremes;                  /**< the curve was drawn between highs and lows (NOAA, Stormglass) */
    bool observed;                      /**< a gauge reading (NOAA) */
    float obsH;
    uint32_t obsT;
    uint32_t updated;
};

struct Tdw_Sea_t {
    bool known;
    float wave, waveDir, wavePeriod;    /**< m, degrees from, s */
    float swell, swellDir, swellPeriod;
    float sst;                          /**< °C, NAN if unknown */
    float current, currentDir;          /**< km/h, degrees towards */
    float waveMax24;
    uint32_t waveMaxAt;
    float cellKm;                       /**< how far the model's sea cell is */
    uint32_t updated;
};

struct Tdw_Weather_t {
    bool known;
    float temp, wind, gust, windDir, rain;  /**< °C, km/h, km/h, degrees from, mm in the last hour */
    int code;                           /**< WMO weather code */
    bool day;
    float gustMax;                      /**< the most in the next leadHours */
    uint32_t gustMaxAt;
    int8_t stormIn;                     /**< hours until thunder (codes 95-99), -1 none in the next 12 */
    uint8_t rainProb[TDW_RAIN_HOURS];
    float rainMm[TDW_RAIN_HOURS];
    uint32_t sunrise[2], sunset[2];     /**< today, tomorrow */
    uint32_t updated;
};

struct Tdw_River_t {
    bool known;
    float q[TDW_RIVER_DAYS];            /**< m³/s a day */
    uint32_t day0;                      /**< Unix time of q[0]'s day (00:00 UTC) */
    uint8_t n, today;
    float peak;                         /**< the most from today on */
    uint32_t peakDay;
    bool climate;                       /**< the last 5 years are known */
    float p10, p50, p90, p99, max;
    float cellKm;
    uint32_t updated;
};

struct Tdw_Alert_t {
    uint8_t kind;                       /**< Tdw_Alert_Kind_t */
    uint32_t at;                        /**< when it happens (for high water and peaks) or now */
    char text[96];
};

struct Tdw_Data_t {
    Tdw_Tide_t tide;
    Tdw_Sea_t sea;
    Tdw_Weather_t wx;
    Tdw_River_t river;
    int32_t utcOffset;                  /**< the place's, seconds */
    char tzName[8];                     /**< "WAT", "EST" */
    // Plans.
    uint32_t sgQuota, sgUsed;           /**< Stormglass, today */
    uint32_t wtCredits;                 /**< WorldTides, this month */
    uint32_t omCalls;                   /**< Open-Meteo, today */
    char noaaText[64];                  /**< "The Battery, 3 km", "No NOAA station near", "US only" */
    Tdw_Alert_t alerts[TDW_ALERTS_MAX];
    uint8_t alertCount;
};

// Requests ---------------------------------------------------------------------------------------------------
bool tdw_Url_Marine(char* url, size_t size, double lat, double lon);
bool tdw_Url_Weather(char* url, size_t size, double lat, double lon);
bool tdw_Url_Flood(char* url, size_t size, double lat, double lon);
/** @brief The same river's daily discharge for the 10 years before @p now. */
bool tdw_Url_Flood_Climate(char* url, size_t size, double lat, double lon, uint32_t now);
bool tdw_Url_Places(char* url, size_t size, const char* query);
bool tdw_Url_Noaa_Hilo(char* url, size_t size, const char* station, uint8_t datum, uint32_t now);
bool tdw_Url_Noaa_Level(char* url, size_t size, const char* station, uint8_t datum);
bool tdw_Url_WorldTides(char* url, size_t size, double lat, double lon, uint8_t datum, uint32_t now, const char* key);
bool tdw_Url_Stormglass(char* url, size_t size, double lat, double lon, uint8_t datum, uint32_t now);

// Answers ----------------------------------------------------------------------------------------------------
/** @brief Open-Meteo Marine: the sea state, and the tide from sea_level_height_msl (into @p tide when @p useTide). */
bool tdw_Parse_Marine(const char* body, size_t length, int httpCode, uint32_t now, double lat, double lon, Tdw_Sea_t& sea,
                      Tdw_Tide_t* tide, int32_t& utcOffset, char* tz, size_t tzSize, char* why, size_t whySize);
bool tdw_Parse_Weather(const char* body, size_t length, int httpCode, uint32_t now, uint8_t leadHours, Tdw_Weather_t& wx,
                       char* why, size_t whySize);
bool tdw_Parse_Flood(const char* body, size_t length, int httpCode, uint32_t now, double lat, double lon, Tdw_River_t& r,
                     char* why, size_t whySize);
/** @brief The climate of the river: percentiles of @p length days of discharge. @p scratch holds 4,000 floats. */
bool tdw_Parse_Flood_Climate(const char* body, size_t length, int httpCode, Tdw_River_t& r, float* scratch, size_t scratchCount);
/** @brief Places found by Open-Meteo's geocoding: names, details and coordinates. Returns how many, -1 on error. */
int tdw_Parse_Places(const char* body, size_t length, int httpCode, char names[][TDW_PLACE_LEN], char details[][48],
                     double* lats, double* lons, int max);
uint8_t tdw_Parse_Noaa_Hilo(const char* body, size_t length, int httpCode, Tdw_Tide_t& t);
bool tdw_Parse_Noaa_Level(const char* body, size_t length, float& h, uint32_t& at);
uint8_t tdw_Parse_WorldTides(const char* body, size_t length, int httpCode, Tdw_Tide_t& t, uint32_t& credits);
uint8_t tdw_Parse_Stormglass(const char* body, size_t length, int httpCode, Tdw_Tide_t& t, uint32_t& quota, uint32_t& used);
const char* tdw_Stormglass_Plan(uint32_t quota);

// The NOAA station list, read as it arrives ------------------------------------------------------------------
struct Tdw_Noaa_Scan_t {
    double lat, lon;                    /**< the place */
    float bestKm;
    char bestId[12], bestName[40];
    uint32_t stations;
    // The parser.
    int depth;
    bool inString, escape, readingKey;
    char token[48];
    uint8_t tokenLen;
    char key[16];
    bool expectValue;
    char id[12], name[40];
    double sLat, sLon;
    uint8_t have;                       /**< bits: id, name, lat, lng */
};
void tdw_Noaa_Scan_Begin(Tdw_Noaa_Scan_t& s, double lat, double lon);
void tdw_Noaa_Scan_Feed(Tdw_Noaa_Scan_t& s, const char* data, size_t length);

// Tide arithmetic ----------------------------------------------------------------------------------------------
/** @brief Highs and lows of a series (parabola through each turn); tiny wiggles are ignored. */
int tdw_Extremes_From_Series(const Tdw_Point_t* s, int n, Tdw_Extreme_t* e, int max);
/** @brief A smooth curve between highs and lows (half a cosine each), @p step seconds apart. */
int tdw_Series_From_Extremes(const Tdw_Extreme_t* e, int ne, uint32_t step, Tdw_Point_t* s, int max);
/** @brief The height at @p t from the series; false outside it. */
bool tdw_Level_At(const Tdw_Tide_t& t, uint32_t at, float& h);
/** @brief The next high or low after @p now, or -1. */
int tdw_Next_Extreme(const Tdw_Tide_t& t, uint32_t now, int wantHigh = -1);
/** @brief Today's range (highest high minus lowest low in the 24 h from @p from), NAN if not known. */
float tdw_Range(const Tdw_Tide_t& t, uint32_t from);

// Judgements ---------------------------------------------------------------------------------------------------
/** @brief The Douglas sea scale's word for waves of @p metres: "Calm", "Smooth", "Slight", "Moderate", "Rough"... */
const char* tdw_Sea_State(float metres);
uint8_t tdw_Sea_Severity(float metres);     /**< 0 calm .. 4 dangerous */
/** @brief Beaufort number and name for a wind of @p kmh. */
int tdw_Beaufort(float kmh);
const char* tdw_Beaufort_Name(int b);
const char* tdw_Compass(float degrees);     /**< "NNE" */
uint8_t tdw_River_Level(const Tdw_River_t& r, float q);
const char* tdw_River_Name(uint8_t level);
/** @brief Moon age in days -> "Spring tides" (near new and full moon: the biggest range), "Neap tides", or "". */
const char* tdw_Spring_Neap(float moonAge);
/** @brief Days until the next new or full moon (the next spring tides). */
float tdw_Days_To_Spring(float moonAge);
/** @brief Words for a WMO weather code: "Thunderstorm", "Light rain"... */
const char* tdw_Weather_Text(int code);

/** @brief The alerts in force for the settings and the data, soonest first. Returns how many. */
int tdw_Alerts(const Tdw_Settings_t& s, const Tdw_Data_t& d, uint32_t now, Tdw_Alert_t* out, int max);

// Units --------------------------------------------------------------------------------------------------------
void tdw_Height_Text(char* dest, size_t size, float metres, bool imperial, bool sign = false);
void tdw_Wind_Text(char* dest, size_t size, float kmh, uint8_t unit, bool withUnit = true);
const char* tdw_Wind_Unit(uint8_t unit);
void tdw_Temp_Text(char* dest, size_t size, float c, bool imperial);
/** @brief "13:21" at the place (Unix time + its UTC offset). */
void tdw_Clock(char* dest, size_t size, uint32_t t, int32_t utcOffset);
/** @brief "Tue" at the place. */
const char* tdw_Weekday(uint32_t t, int32_t utcOffset);
/** @brief "2 h 05 min", "35 min". */
void tdw_Span(char* dest, size_t size, int32_t seconds);
const char* tdw_Source_Name(uint8_t source);
/** @brief Unix time of "2026-11-10 01:07" (UTC). 0 if not one. */
uint32_t tdw_Parse_Noaa_Time(const char* text);

#endif
