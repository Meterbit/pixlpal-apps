/**
 * @file omt_Model.h
 * @brief Open-Meteo's model: settings, and the provider for the shared weather engine (mtb_kit_weather.h): the
 *        requests, the readers of the forecast, air quality and geocoding answers, the pacing, and the plan.
 *        No Arduino, so it can be tested on a PC.
 *
 * Open-Meteo (checked 6 November 2026):
 *   Free, no key: non-commercial use, under 10,000 calls a day, 5,000 an hour, 600 a minute, per IP. A call asking
 *   for over 10 variables or over 14 days counts as more than one. Data CC BY 4.0: credit Open-Meteo; air quality
 *   also credits CAMS. Errors are {"error": true, "reason": "..."}.
 *   Paid (Standard 1M calls a month, Professional 5M, Enterprise): an API key, the same requests on
 *   customer-api.open-meteo.com with &apikey=. A bad key: 400 "The supplied API key is invalid.", none: 401.
 *   Paid plans add commercial use, reserved servers and volume, not other forecast data.
 */
#ifndef OMT_MODEL_H
#define OMT_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "mtb_kit_weather.h"

#define OMT_SETTINGS_MAGIC  0x4F4D5431UL    // "OMT1"
#define OMT_NVS_KEY         "openMeteoCfg"
#define OMT_KEY_LEN         48

enum Omt_Feed_t : uint8_t { OMT_FEED_CURRENT = 0, OMT_FEED_FORECAST, OMT_FEED_AIR, OMT_FEED_COUNT };
// The engine's config.options: air (bit 0), the European index (bit 1), and the refresh in minutes (bits 2-7).
#define OMT_OPTION_AIR      0x01
#define OMT_OPTION_EU       0x02
#define OMT_OPTIONS(air, eu, minutes)   ((uint8_t)(((air) ? OMT_OPTION_AIR : 0) | ((eu) ? OMT_OPTION_EU : 0) | (((minutes) & 0x3F) << 2)))
#define OMT_REFRESH_OF(options)         ((uint8_t)((options) >> 2))

struct Omt_Settings_t {
    uint32_t magic;
    char place[KIT_WX_PLACE_LEN];   /**< UTF-8, as the search found it. */
    double latitude;
    double longitude;
    uint8_t imperial;
    uint8_t air;                    /**< Read the air quality (and pollen in Europe). */
    uint8_t aqiScale;               /**< KIT_AQI_US or KIT_AQI_EU. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t refreshMinutes;         /**< Conditions this often: 10-60 free, 5-60 with a key. */
    char apiKey[OMT_KEY_LEN];       /**< Empty: the free API. */
};

void omt_Settings_Default(Omt_Settings_t& settings);
void omt_Settings_Clamp(Omt_Settings_t& settings);
/** @brief Open-Meteo keys: letters and digits, 10-40 of them. */
bool omt_Key_Valid(const char* key);
/** @brief The engine's view of the settings. */
void omt_Kit_Config(const Omt_Settings_t& settings, Mtb_Kit_Wx_Config_t& config);

/** @brief The kit icon and the words for a WMO weather code. */
uint8_t omt_Icon_Of(int code);
const char* omt_Text_Of(int code);

const Mtb_Kit_Wx_Provider_t& omt_Provider(void);

// The provider's parts, for tests.
bool omt_Request(const Mtb_Kit_Wx_Config_t& config, uint8_t feed, const Mtb_Kit_Wx_Data_t& data, char* url, size_t size, const char** headers);
uint8_t omt_Parse(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& config, Mtb_Kit_Wx_Data_t& work, char* error, size_t errorSize);
uint32_t omt_Every(uint8_t feed, const Mtb_Kit_Wx_Config_t& config, const Mtb_Kit_Wx_Data_t& data, uint32_t now);
bool omt_Search_Request(const Mtb_Kit_Wx_Config_t& config, const char* query, char* url, size_t size);
int omt_Parse_Places(int httpCode, JsonVariantConst doc, Mtb_Kit_Wx_Place_t* places, int max, char* error, size_t errorSize);

/** @brief The calls a day the app makes at a refresh (each weighted as Open-Meteo counts it). */
float omt_Calls_Per_Day(uint8_t refreshMinutes, bool air);

#endif
