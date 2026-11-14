#include "omt_Model.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <cmath>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define OMT_FREE_FLOOR_MINUTES  10
#define OMT_PAID_FLOOR_MINUTES  5
#define OMT_FORECAST_EVERY      3600
#define OMT_AIR_EVERY           3600

static const char CURRENT_VARS[] = "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,"
                                   "cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index";
static const char HOURLY_VARS[] = "temperature_2m,precipitation_probability,weather_code,is_day";
static const char DAILY_VARS[] = "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset,uv_index_max,precipitation_sum";
static const char AIR_VARS[] = "us_aqi,european_aqi,pm2_5,pm10,ozone,nitrogen_dioxide,grass_pollen,birch_pollen,ragweed_pollen";

void omt_Settings_Default(Omt_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = OMT_SETTINGS_MAGIC;
    snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
    s.latitude = 6.45407;
    s.longitude = 3.39467;
    s.air = 1;
    s.aqiScale = KIT_AQI_US;
    s.pageSeconds = 10;
    s.autoCycle = 1;
    s.refreshMinutes = 15;
}

bool omt_Key_Valid(const char* key){
    if (key == nullptr) return false;
    const size_t n = strlen(key);
    if (n < 10 || n > 40) return false;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)key[i]) && key[i] != '-' && key[i] != '_') return false;
    return true;
}

void omt_Settings_Clamp(Omt_Settings_t& s){
    s.place[sizeof(s.place) - 1] = '\0';
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
    if (!(s.latitude >= -90 && s.latitude <= 90) || !(s.longitude >= -180 && s.longitude <= 180)){
        Omt_Settings_t d;
        omt_Settings_Default(d);
        memcpy(s.place, d.place, sizeof(s.place));
        s.latitude = d.latitude;
        s.longitude = d.longitude;
    }
    if (s.apiKey[0] && !omt_Key_Valid(s.apiKey)) memset(s.apiKey, 0, sizeof(s.apiKey));
    s.imperial = s.imperial ? 1 : 0;
    s.air = s.air ? 1 : 0;
    if (s.aqiScale > KIT_AQI_EU) s.aqiScale = KIT_AQI_US;
    if (s.pageSeconds < 4) s.pageSeconds = 4;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
    const uint8_t floor = s.apiKey[0] ? OMT_PAID_FLOOR_MINUTES : OMT_FREE_FLOOR_MINUTES;
    if (s.refreshMinutes < floor) s.refreshMinutes = floor;
    if (s.refreshMinutes > 60) s.refreshMinutes = 60;
}

void omt_Kit_Config(const Omt_Settings_t& s, Mtb_Kit_Wx_Config_t& config){
    memset(&config, 0, sizeof(config));
    snprintf(config.place, sizeof(config.place), "%s", s.place);
    config.latitude = s.latitude;
    config.longitude = s.longitude;
    config.imperial = s.imperial;
    config.options = OMT_OPTIONS(s.air, s.aqiScale == KIT_AQI_EU, s.refreshMinutes);
    snprintf(config.apiKey, sizeof(config.apiKey), "%s", s.apiKey);
}

//##############################################################################################################
uint8_t omt_Icon_Of(int code){
    switch (code){
        case 0: case 1: return KIT_WX_CLEAR;
        case 2: return KIT_WX_PARTLY;
        case 3: return KIT_WX_CLOUDY;
        case 45: case 48: return KIT_WX_FOG;
        case 51: case 53: case 55: return KIT_WX_DRIZZLE;
        case 56: case 57: case 66: case 67: return KIT_WX_SLEET;
        case 61: case 63: case 80: case 81: return KIT_WX_RAIN;
        case 65: case 82: return KIT_WX_HEAVY_RAIN;
        case 71: case 73: case 75: case 77: case 85: case 86: return KIT_WX_SNOW;
        case 95: return KIT_WX_STORM;
        case 96: case 99: return KIT_WX_HAIL;
        default: return KIT_WX_UNKNOWN;
    }
}

const char* omt_Text_Of(int code){
    switch (code){
        case 0: return "Clear sky";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Freezing fog";
        case 51: return "Light drizzle";
        case 53: return "Drizzle";
        case 55: return "Heavy drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Violent showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Storm with hail";
        default: return "";
    }
}

//##############################################################################################################
// The host and the key's place: free or customer.
static size_t omt_Base(char* url, size_t size, const Mtb_Kit_Wx_Config_t& c, const char* service, const char* path){
    return (size_t)snprintf(url, size, "https://%s%s.open-meteo.com%s?latitude=%.4f&longitude=%.4f&timezone=auto&timeformat=unixtime",
                            c.apiKey[0] ? "customer-" : "", service, path, c.latitude, c.longitude);
}

static void omt_Append(char* url, size_t size, size_t& used, const char* format, const char* value){
    if (used < size) used += (size_t)snprintf(url + used, size - used, format, value);
}

bool omt_Request(const Mtb_Kit_Wx_Config_t& c, uint8_t feed, const Mtb_Kit_Wx_Data_t&, char* url, size_t size, const char** headers){
    (void)headers;
    size_t used;
    switch (feed){
        case OMT_FEED_CURRENT:
            used = omt_Base(url, size, c, "api", "/v1/forecast");
            omt_Append(url, size, used, "&current=%s", CURRENT_VARS);
            break;
        case OMT_FEED_FORECAST:
            used = omt_Base(url, size, c, "api", "/v1/forecast");
            omt_Append(url, size, used, "&hourly=%s", HOURLY_VARS);
            omt_Append(url, size, used, "&daily=%s&forecast_days=16&forecast_hours=48", DAILY_VARS);
            break;
        case OMT_FEED_AIR:
            if (!(c.options & OMT_OPTION_AIR)) return false;
            used = omt_Base(url, size, c, "air-quality-api", "/v1/air-quality");
            omt_Append(url, size, used, "&current=%s", AIR_VARS);
            break;
        default: return false;
    }
    if (c.imperial && feed != OMT_FEED_AIR) omt_Append(url, size, used, "%s", "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch");
    if (c.apiKey[0]) omt_Append(url, size, used, "&apikey=%s", c.apiKey);
    return used < size;
}

static float omt_Number(JsonVariantConst v){ return v.is<float>() || v.is<int>() ? v.as<float>() : NAN; }

// The answer's error, if it is one.
static uint8_t omt_Error(int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& c, Mtb_Kit_Wx_Data_t& work, char* error, size_t size){
    const char* reason = doc["reason"] | "";
    if (httpCode == 200 && !doc["error"].as<bool>()){
        // A good answer says what the key (or its absence) gets.
        Mtb_Kit_Wx_Plan_t& plan = work.plan;
        plan.known = 1;
        plan.paid = c.apiKey[0] ? 1 : 0;
        snprintf(plan.name, sizeof(plan.name), "%s", c.apiKey[0] ? "Paid plan (your key)" : "Free, non-commercial");
        return doc.isNull() ? (uint8_t)KIT_WX_PARSE : (uint8_t)KIT_WX_OK;
    }
    snprintf(error, size, "%s", reason[0] ? reason : "Open-Meteo did not answer as expected");
    if (httpCode == 401 || strstr(reason, "API key") || strstr(reason, "apikey")) return KIT_WX_KEY;
    if (httpCode == 429) return (strstr(reason, "Daily") || strstr(reason, "daily") || strstr(reason, "Monthly")) ? KIT_WX_QUOTA : KIT_WX_RATE;
    if (httpCode >= 500) return KIT_WX_SERVER;
    if (httpCode >= 400) return KIT_WX_BAD;
    return KIT_WX_PARSE;
}

static uint8_t omt_Parse_Current(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& work){
    JsonVariantConst cur = doc["current"];
    if (!cur.is<JsonObjectConst>()) return KIT_WX_PARSE;
    Mtb_Kit_Wx_Current_t& c = work.current;
    const int code = cur["weather_code"] | -1;
    c.time = cur["time"] | 0u;
    c.day = (cur["is_day"] | 1) ? 1 : 0;
    c.icon = omt_Icon_Of(code);
    snprintf(c.text, sizeof(c.text), "%s", omt_Text_Of(code));
    c.temp = omt_Number(cur["temperature_2m"]);
    c.feels = omt_Number(cur["apparent_temperature"]);
    c.humidity = omt_Number(cur["relative_humidity_2m"]);
    c.uv = omt_Number(cur["uv_index"]);
    c.wind = omt_Number(cur["wind_speed_10m"]);
    c.gust = omt_Number(cur["wind_gusts_10m"]);
    c.windDegrees = omt_Number(cur["wind_direction_10m"]);
    c.pressure = omt_Number(cur["pressure_msl"]);
    c.cloud = omt_Number(cur["cloud_cover"]);
    c.rainChance = NAN;             // Not a current variable: the hour's chance is shown instead.
    c.visibility = NAN;
    c.valid = !std::isnan(c.temp);
    work.utcOffset = doc["utc_offset_seconds"] | work.utcOffset;
    return c.valid ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint8_t omt_Parse_Forecast(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& work){
    JsonVariantConst hourly = doc["hourly"], daily = doc["daily"];
    JsonArrayConst hourTimes = hourly["time"], dayTimes = daily["time"];
    if (hourTimes.isNull() || dayTimes.isNull()) return KIT_WX_PARSE;
    work.utcOffset = doc["utc_offset_seconds"] | work.utcOffset;
    work.hourStep = 1;
    work.hourCount = 0;
    for (size_t i = 0; i < hourTimes.size() && work.hourCount < KIT_WX_HOURS; i++){
        Mtb_Kit_Wx_Hour_t& h = work.hours[work.hourCount++];
        h.start = hourTimes[i] | 0u;
        h.temp = omt_Number(hourly["temperature_2m"][i]);
        const int rain = hourly["precipitation_probability"][i] | 0;
        h.rain = (uint8_t)(rain < 0 ? 0 : rain > 100 ? 100 : rain);
        h.icon = omt_Icon_Of(hourly["weather_code"][i] | -1);
        h.day = (hourly["is_day"][i] | 1) ? 1 : 0;
    }
    work.dayCount = 0;
    for (size_t i = 0; i < dayTimes.size() && work.dayCount < KIT_WX_DAYS; i++){
        Mtb_Kit_Wx_Day_t& d = work.days[work.dayCount++];
        mtb_Kit_Wx_Date_Of(dayTimes[i] | 0u, work.utcOffset, d.year, d.month, d.mday);
        d.high = omt_Number(daily["temperature_2m_max"][i]);
        d.low = omt_Number(daily["temperature_2m_min"][i]);
        const int rain = daily["precipitation_probability_max"][i] | 0;
        d.rain = (uint8_t)(rain < 0 ? 0 : rain > 100 ? 100 : rain);
        d.icon = omt_Icon_Of(daily["weather_code"][i] | -1);
        d.sunrise = daily["sunrise"][i] | 0u;
        d.sunset = daily["sunset"][i] | 0u;
        d.uvMax = omt_Number(daily["uv_index_max"][i]);
        d.precip = omt_Number(daily["precipitation_sum"][i]);
    }
    return work.hourCount && work.dayCount ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint8_t omt_Parse_Air(JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& c, Mtb_Kit_Wx_Data_t& work){
    JsonVariantConst cur = doc["current"];
    if (!cur.is<JsonObjectConst>()) return KIT_WX_PARSE;
    Mtb_Kit_Wx_Air_t& a = work.air;
    a.scale = (c.options & OMT_OPTION_EU) ? KIT_AQI_EU : KIT_AQI_US;
    const float index = omt_Number(cur[a.scale == KIT_AQI_EU ? "european_aqi" : "us_aqi"]);
    a.time = cur["time"] | 0u;
    a.pm25 = omt_Number(cur["pm2_5"]);
    a.pm10 = omt_Number(cur["pm10"]);
    a.ozone = omt_Number(cur["ozone"]);
    a.no2 = omt_Number(cur["nitrogen_dioxide"]);
    // Pollen is measured in Europe only; elsewhere it comes back null.
    a.pollen[0] = omt_Number(cur["grass_pollen"]);
    a.pollen[1] = omt_Number(cur["birch_pollen"]);
    a.pollen[2] = omt_Number(cur["ragweed_pollen"]);
    a.valid = !std::isnan(index);
    a.index = a.valid ? (int16_t)lroundf(index) : 0;
    return a.valid ? KIT_WX_OK : KIT_WX_PARSE;
}

uint8_t omt_Parse(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& c, Mtb_Kit_Wx_Data_t& work, char* error, size_t errorSize){
    const uint8_t result = omt_Error(httpCode, doc, c, work, error, errorSize);
    if (result != KIT_WX_OK) return result;
    switch (feed){
        case OMT_FEED_CURRENT: return omt_Parse_Current(doc, work);
        case OMT_FEED_FORECAST: return omt_Parse_Forecast(doc, work);
        case OMT_FEED_AIR: return omt_Parse_Air(doc, c, work);
        default: return KIT_WX_BAD;
    }
}

uint32_t omt_Every(uint8_t feed, const Mtb_Kit_Wx_Config_t& c, const Mtb_Kit_Wx_Data_t&, uint32_t){
    switch (feed){
        case OMT_FEED_CURRENT: {
            uint32_t minutes = OMT_REFRESH_OF(c.options);
            const uint32_t floor = c.apiKey[0] ? OMT_PAID_FLOOR_MINUTES : OMT_FREE_FLOOR_MINUTES;
            if (minutes < floor) minutes = floor;
            return minutes * 60;
        }
        case OMT_FEED_FORECAST: return OMT_FORECAST_EVERY;
        case OMT_FEED_AIR: return (c.options & OMT_OPTION_AIR) ? OMT_AIR_EVERY : 0;
        default: return 0;
    }
}

float omt_Calls_Per_Day(uint8_t refreshMinutes, bool air){
    // Current: 12 variables (1.2 calls); forecast: 12 variables over 16 days (1.2 x 16/14); air: 9 variables (1).
    const float current = 1440.0f / (refreshMinutes ? refreshMinutes : 15) * 1.2f;
    const float forecast = 24 * 1.2f * 16 / 14;
    return current + forecast + (air ? 24.0f : 0.0f);
}

//##############################################################################################################
bool omt_Search_Request(const Mtb_Kit_Wx_Config_t& c, const char* query, char* url, size_t size){
    char encoded[160];
    mtb_Kit_Url_Encode(encoded, sizeof(encoded), query);
    int n = snprintf(url, size, "https://%sgeocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json",
                     c.apiKey[0] ? "customer-" : "", encoded, KIT_WX_PLACES);
    if (c.apiKey[0] && n > 0 && (size_t)n < size) n += snprintf(url + n, size - n, "&apikey=%s", c.apiKey);
    return n > 0 && (size_t)n < size;
}

int omt_Parse_Places(int httpCode, JsonVariantConst doc, Mtb_Kit_Wx_Place_t* places, int max, char* error, size_t errorSize){
    if (httpCode != 200 || doc["error"].as<bool>()){
        snprintf(error, errorSize, "%s", doc["reason"] | "Open-Meteo could not search");
        return -1;
    }
    int count = 0;
    for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()){
        if (count >= max) break;
        const char* name = r["name"] | "";
        if (!name[0] || !r["latitude"].is<double>() || !r["longitude"].is<double>()) continue;
        Mtb_Kit_Wx_Place_t& p = places[count++];
        const char* country = r["country"] | (r["country_code"] | "");
        snprintf(p.name, sizeof(p.name), "%s%s%s", name, country[0] ? ", " : "", country);
        const char* admin = r["admin1"] | "";
        const long population = r["population"] | 0L;
        if (population >= 1000000) snprintf(p.detail, sizeof(p.detail), "%s%s%.1f million people", admin, admin[0] ? ", " : "", population / 1e6);
        else snprintf(p.detail, sizeof(p.detail), "%s", admin);
        p.latitude = r["latitude"];
        p.longitude = r["longitude"];
    }
    return count;
}

const Mtb_Kit_Wx_Provider_t& omt_Provider(void){
    static const Mtb_Kit_Wx_Provider_t provider = {
        "Open-Meteo", "Pixlpal-OpenMeteo/2.0 (ESP32 LED display)", "/weather/openmeteo.bin", 48 * 1024, OMT_FEED_COUNT, true,
        omt_Request, omt_Parse, omt_Every, omt_Search_Request, omt_Parse_Places,
    };
    return provider;
}
