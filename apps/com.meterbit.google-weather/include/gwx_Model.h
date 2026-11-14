/**
 * @file gwx_Model.h
 * @brief Google Weather's model: settings, the weather it keeps, readers of the Weather API's and Geocoding API's
 *        answers, the monthly call budget, and the icon of each condition. No Arduino, so it can be tested on a PC.
 *
 * Google Maps Platform Weather API (checked 6 November 2026), key as ?key=, location.latitude/longitude:
 *   /v1/currentConditions:lookup   /v1/forecast/hours:lookup?hours=   /v1/forecast/days:lookup?days=
 *   /v1/publicAlerts:lookup        unitsSystem=METRIC|IMPERIAL, languageCode=en
 *   Errors {"error":{"code","message","status"}}: PERMISSION_DENIED (key, API not enabled, no billing),
 *   RESOURCE_EXHAUSTED (quota), INVALID_ARGUMENT.
 * Billing must be enabled on the Google Cloud project, even within the free monthly usage. Google publishes no plan
 * through the API, so the user sets a monthly call budget and the app keeps to it. Attribution: "Google Maps", and
 * an alert's dataSource name.
 */
#ifndef GWX_MODEL_H
#define GWX_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define GWX_SETTINGS_MAGIC  0x47575831UL    // "GWX1"
#define GWX_NVS_KEY         "gWeatherCfg"
#define GWX_DATA_MAGIC      0x47574431UL    // "GWD1"
#define GWX_DATA_LAYOUT     1
#define GWX_HOURS           24
#define GWX_DAYS            10
#define GWX_ALERTS          3
#define GWX_PLACE_LEN       48
#define GWX_KEY_LEN         48
#define GWX_PLACES          5

struct Gwx_Settings_t {
    uint32_t magic;
    char place[GWX_PLACE_LEN];      /**< As the search found it: "Lagos, Nigeria" (UTF-8). */
    double latitude;
    double longitude;
    uint8_t imperial;               /**< °F and mph, else °C and km/h. */
    uint8_t alerts;                 /**< Ask for public weather alerts. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint32_t monthlyBudget;         /**< Calls a month the app may make. */
    char apiKey[GWX_KEY_LEN];
};

struct Gwx_Current_t {
    uint32_t time;                  /**< When the conditions were observed. */
    uint8_t valid;
    uint8_t day;                    /**< isDaytime */
    uint8_t icon;                   /**< Mtb_Kit_Weather_t */
    char text[28];                  /**< "Partly cloudy" (ASCII). */
    float temp, feels, humidity, uv, wind, gust, windDegrees, pressure, rainChance, cloud;
    float high, low;                /**< Of the last 24 hours. */
    char windFrom[4];               /**< "NNW". */
};

struct Gwx_Hour_t { uint32_t start; float temp; uint8_t rain; uint8_t icon; uint8_t day; };

struct Gwx_Day_t {
    int16_t year; uint8_t month, mday;
    float high, low;
    uint8_t rain;                   /**< Daytime chance, per cent. */
    uint8_t icon;
    uint32_t sunrise, sunset;       /**< Unix time; 0 if none (polar). */
    uint8_t moonPhase;              /**< 0-7, new to waning crescent; 0xFF unknown. */
};

enum Gwx_Severity_t : uint8_t { GWX_SEV_UNKNOWN = 0, GWX_SEV_MINOR, GWX_SEV_MODERATE, GWX_SEV_SEVERE, GWX_SEV_EXTREME };

struct Gwx_Alert_t {
    uint8_t severity;
    uint32_t start, end;            /**< end 0: until further notice. */
    char title[64];
    char area[40];
    char source[40];                /**< The issuing agency: shown with the alert, as required. */
};

enum Gwx_Error_t : uint8_t { GWX_OK = 0, GWX_ERR_NET, GWX_ERR_PARSE, GWX_ERR_DENIED, GWX_ERR_QUOTA, GWX_ERR_ARGUMENT, GWX_ERR_SERVER };

struct Gwx_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    double latitude, longitude;     /**< The place the data is for. */
    uint8_t imperial;
    int32_t utcOffset;              /**< Seconds, the place's, from the hourly forecast. */
    Gwx_Current_t current;
    uint8_t hourCount;
    Gwx_Hour_t hours[GWX_HOURS];
    uint8_t dayCount;
    Gwx_Day_t days[GWX_DAYS];
    uint8_t alertCount;
    Gwx_Alert_t alerts[GWX_ALERTS];
    uint32_t fetched[4];            /**< Current, hours, days, alerts. */
    uint32_t usedMonth;             /**< yyyymm of usedCalls. */
    uint32_t usedCalls;             /**< Calls made this month (Google's console is the authority). */
    uint8_t lastError;
    char errorText[96];             /**< Google's message, for the phone. */
};

struct Gwx_Place_t { char name[GWX_PLACE_LEN]; char detail[48]; double latitude, longitude; };

void gwx_Settings_Default(Gwx_Settings_t& settings);
void gwx_Settings_Clamp(Gwx_Settings_t& settings);

/** @brief The kit icon of a Weather API condition type ("PARTLY_CLOUDY", "LIGHT_THUNDERSTORM_RAIN", the JP/KR ones). */
uint8_t gwx_Icon_Of(const char* type);
/** @brief "NORTH_NORTHWEST" -> "NNW". */
void gwx_Cardinal(char* dest, size_t size, const char* cardinal);

/** @brief The error in an answer ({"error":{...}}), its message into @p text. GWX_OK when there is none. */
uint8_t gwx_Error_Of(JsonVariantConst doc, int httpCode, char* text, size_t size);
uint8_t gwx_Parse_Current(JsonVariantConst doc, Gwx_Current_t& current);
uint8_t gwx_Parse_Hours(JsonVariantConst doc, Gwx_Data_t& data);
uint8_t gwx_Parse_Days(JsonVariantConst doc, Gwx_Data_t& data);
uint8_t gwx_Parse_Alerts(JsonVariantConst doc, Gwx_Data_t& data);
/** @brief Geocoding results into @p places. Returns the count, or -1 with @p text set on an error status. */
int gwx_Parse_Places(JsonVariantConst doc, Gwx_Place_t* places, int max, char* text, size_t size);

/**
 * @brief Seconds between current-conditions reads so the month's remaining budget lasts to the month's end: hours
 *        are read every 4th time (at least hourly), days every 12th, alerts every 2nd. Never under 10 minutes, never
 *        over 3 hours; 0 if the budget is spent.
 */
uint32_t gwx_Interval(uint32_t budget, uint32_t usedThisMonth, uint32_t secondsLeftInMonth, bool alerts);
/** @brief yyyymm of a Unix time (UTC), and the seconds to that month's end. */
uint32_t gwx_Month_Of(uint32_t unixTime, uint32_t* secondsLeft = nullptr);

#endif
