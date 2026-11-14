/**
 * @file owm_Model.h
 * @brief OpenWeather's model: settings, and the provider for the shared weather engine (mtb_kit_weather.h): the
 *        requests, the readers of each API's answers, the pacing, and the plan the key turns out to have.
 *        No Arduino, so it can be tested on a PC.
 *
 * OpenWeather (checked 6 November 2026), key as &appid=, 32 hexadecimal characters, active up to 2 hours after sign-up:
 *   Free: 60 calls a minute, 1,000,000 a month: Current Weather (/data/2.5/weather), 5 day / 3 hour forecast
 *   (/data/2.5/forecast), Air Pollution (/data/2.5/air_pollution), Geocoding (/geo/1.0/direct).
 *   Startup (30 GBP a month) adds the 16-day daily forecast (/data/2.5/forecast/daily); Developer (140 GBP) and up
 *   add the 4-day hourly forecast (pro.openweathermap.org/data/2.5/forecast/hourly).
 *   One Call API 3.0 (/data/3.0/onecall) is a separate "One Call by Call" subscription: 1,000 calls a day free, then
 *   0.0012 GBP a call; it gives 48 hours, 8 days and government alerts. Without it: 401 "Please note that using One
 *   Call 3.0 requires a separate subscription...".
 *   A key without access to an API gets 401 "Invalid API key" from it, the same as a wrong key: the app tells them
 *   apart by whether the free Current Weather API accepts the key. 429: the plan's calls are used up.
 *   Data under ODbL; visible attribution required: "Weather data (c) OpenWeather".
 */
#ifndef OWM_MODEL_H
#define OWM_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "mtb_kit_weather.h"

#define OWM_SETTINGS_MAGIC  0x4F574D31UL    // "OWM1"
#define OWM_NVS_KEY         "openWeatherCfg"
#define OWM_KEY_LEN         40

enum Owm_Feed_t : uint8_t { OWM_FEED_CURRENT = 0, OWM_FEED_ONECALL, OWM_FEED_FORECAST, OWM_FEED_DAILY, OWM_FEED_HOURLY, OWM_FEED_AIR, OWM_FEED_COUNT };

// What the key has (Mtb_Kit_Wx_Plan_t.flags), and what has been tried (.tested).
#define OWM_HAS_KEY         0x01            // The free Current Weather API accepts it.
#define OWM_HAS_ONECALL     0x02
#define OWM_HAS_DAILY       0x04            // 16-day daily: Startup and up.
#define OWM_HAS_HOURLY      0x08            // 4-day hourly: Developer and up.

// The engine's config.options.
#define OWM_OPTION_AIR      0x01
#define OWM_OPTION_ALERTS   0x02
#define OWM_OPTION_ONECALL  0x04            // Use One Call 3.0 when the key is subscribed.

/** @brief One Call calls a day the app makes at most: well inside the 1,000 free. */
#define OWM_ONECALL_EVERY   900

struct Owm_Settings_t {
    uint32_t magic;
    char place[KIT_WX_PLACE_LEN];
    double latitude;
    double longitude;
    uint8_t imperial;
    uint8_t air;
    uint8_t alerts;
    uint8_t oneCall;
    uint8_t pageSeconds;
    uint8_t autoCycle;
    char apiKey[OWM_KEY_LEN];
};

void owm_Settings_Default(Owm_Settings_t& settings);
void owm_Settings_Clamp(Owm_Settings_t& settings);
bool owm_Key_Valid(const char* key);
void owm_Kit_Config(const Owm_Settings_t& settings, Mtb_Kit_Wx_Config_t& config);

uint8_t owm_Icon_Of(int id);
/** @brief The plan's name from what the key has: "Free", "Startup or higher + One Call"... */
void owm_Plan_Name(uint32_t flags, char* dest, size_t size);
/** @brief The One Call API is in use: subscribed, and allowed in the settings. */
bool owm_One_Call_Active(const Mtb_Kit_Wx_Config_t& config, const Mtb_Kit_Wx_Data_t& data);

const Mtb_Kit_Wx_Provider_t& owm_Provider(void);

// The provider's parts, for tests.
bool owm_Request(const Mtb_Kit_Wx_Config_t& config, uint8_t feed, const Mtb_Kit_Wx_Data_t& data, char* url, size_t size, const char** headers);
uint8_t owm_Parse(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& config, Mtb_Kit_Wx_Data_t& work, char* error, size_t errorSize);
uint32_t owm_Every(uint8_t feed, const Mtb_Kit_Wx_Config_t& config, const Mtb_Kit_Wx_Data_t& data, uint32_t now);
bool owm_Search_Request(const Mtb_Kit_Wx_Config_t& config, const char* query, char* url, size_t size);
int owm_Parse_Places(int httpCode, JsonVariantConst doc, Mtb_Kit_Wx_Place_t* places, int max, char* error, size_t errorSize);

#endif
