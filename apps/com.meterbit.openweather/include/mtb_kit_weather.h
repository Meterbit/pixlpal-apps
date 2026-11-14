/**
 * @file mtb_kit_weather.h
 * @brief A weather layer shared by the weather apps: the weather kept (now, hours, days, alerts, air), a fetch engine
 *        that reads each of a provider's feeds on the provider's schedule with back-off, counts calls per day and
 *        month, searches places and keeps a flash cache, and the screens below the status bar.
 *        Each app supplies a provider: how to ask for a feed and read the answer, and how often.
 *
 * Three layers:
 *   model   the data and helpers, no Arduino (mtb_kit_weather_model.cpp): tested on a PC
 *   engine  the fetch task and cache (mtb_kit_weather_engine.cpp)
 *   screens mtb_Kit_Wx_Draw() (mtb_kit_weather_ui.cpp)
 */
#ifndef MTB_KIT_WEATHER_H
#define MTB_KIT_WEATHER_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define KIT_WX_HOURS        48
#define KIT_WX_DAYS         16
#define KIT_WX_ALERTS       3
#define KIT_WX_FEEDS        6
#define KIT_WX_PLACES       6
#define KIT_WX_PLACE_LEN    48
#define KIT_WX_KEY_LEN      72

enum Mtb_Kit_Wx_Result_t : uint8_t {
    KIT_WX_OK = 0, KIT_WX_NET, KIT_WX_PARSE, KIT_WX_KEY, KIT_WX_QUOTA, KIT_WX_RATE, KIT_WX_PLAN, KIT_WX_BAD, KIT_WX_SERVER
};

struct Mtb_Kit_Wx_Current_t {
    uint32_t time;                  /**< When observed. */
    uint8_t valid;
    uint8_t day;                    /**< Daylight. */
    uint8_t icon;                   /**< Mtb_Kit_Weather_t */
    char text[28];                  /**< "Partly cloudy" (ASCII). */
    float temp, feels, humidity, uv, wind, gust, windDegrees, pressure, rainChance, cloud, visibility;  /**< NAN: unknown. */
};

struct Mtb_Kit_Wx_Hour_t { uint32_t start; float temp; uint8_t rain; uint8_t icon; uint8_t day; };

struct Mtb_Kit_Wx_Day_t {
    int16_t year; uint8_t month, mday;          /**< The place's date. */
    float high, low;
    uint8_t rain;                   /**< Chance, per cent. */
    uint8_t icon;
    uint32_t sunrise, sunset;       /**< 0 if none. */
    float uvMax;                    /**< NAN: unknown. */
    float precip;                   /**< mm (or inches, imperial); NAN: unknown. */
};

enum Mtb_Kit_Wx_Severity_t : uint8_t { KIT_WX_SEV_UNKNOWN = 0, KIT_WX_SEV_MINOR, KIT_WX_SEV_MODERATE, KIT_WX_SEV_SEVERE, KIT_WX_SEV_EXTREME };

struct Mtb_Kit_Wx_Alert_t {
    uint8_t severity;
    uint32_t start, end;            /**< end 0: until further notice. */
    char title[64];
    char area[40];
    char source[40];                /**< The issuing agency, shown with the alert. */
};

/** @brief The scale of an air quality index. */
enum Mtb_Kit_Aqi_t : uint8_t { KIT_AQI_US = 0, KIT_AQI_EU, KIT_AQI_FIVE };

struct Mtb_Kit_Wx_Air_t {
    uint8_t valid;
    uint8_t scale;                  /**< Mtb_Kit_Aqi_t */
    uint32_t time;
    int16_t index;
    float pm25, pm10, ozone, no2;   /**< µg/m³; NAN: unknown. */
    float pollen[3];                /**< Grass, birch, ragweed, grains/m³; NAN where not measured (outside Europe). */
};

/** @brief What the provider's plan allows, as the provider learned it from its answers. */
struct Mtb_Kit_Wx_Plan_t {
    uint8_t known;
    uint8_t paid;
    uint32_t flags;                 /**< The provider's: features found on this key. */
    uint32_t tested;                /**< The provider's: features tried at least once. */
    uint32_t checked;               /**< When last learned. */
    char name[28];
};

struct Mtb_Kit_Wx_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    double latitude, longitude;     /**< The place the data is for. */
    uint8_t imperial;
    int32_t utcOffset;              /**< Seconds: the place's. */
    Mtb_Kit_Wx_Current_t current;
    uint8_t hourCount;
    uint8_t hourStep;               /**< Hours between entries: 1, or 3 for three-hourly forecasts. */
    Mtb_Kit_Wx_Hour_t hours[KIT_WX_HOURS];
    uint8_t dayCount;
    Mtb_Kit_Wx_Day_t days[KIT_WX_DAYS];
    uint8_t alertCount;
    Mtb_Kit_Wx_Alert_t alerts[KIT_WX_ALERTS];
    Mtb_Kit_Wx_Air_t air;
    uint32_t fetched[KIT_WX_FEEDS];
    uint8_t feedResult[KIT_WX_FEEDS];
    uint32_t usedDay;               /**< UTC day of usedToday. */
    uint32_t usedToday;
    uint32_t usedMonth;             /**< yyyymm of usedCalls. */
    uint32_t usedCalls;
    uint8_t lastError;
    char errorText[96];             /**< The provider's message, for the phone. */
    uint32_t keyHash;               /**< Of the key the plan was learned with. */
    Mtb_Kit_Wx_Plan_t plan;
};

struct Mtb_Kit_Wx_Config_t {
    char place[KIT_WX_PLACE_LEN];   /**< As the search found it (UTF-8). */
    double latitude, longitude;
    uint8_t imperial;
    uint8_t options;                /**< The app's switches (air, alerts...). */
    char apiKey[KIT_WX_KEY_LEN];
};

struct Mtb_Kit_Wx_Place_t { char name[KIT_WX_PLACE_LEN]; char detail[48]; double latitude, longitude; };

struct Mtb_Kit_Wx_Status_t {
    bool started, busy, keyMissing, keyRejected, quotaUsed, rateLimited, failing;
    uint32_t lastFetch, usedToday, usedCalls;
    Mtb_Kit_Wx_Plan_t plan;
};

/** @brief What a provider does. All functions but none of the screens run on the engine's fetch task. */
struct Mtb_Kit_Wx_Provider_t {
    const char* label;              /**< For the log. */
    const char* userAgent;
    const char* cacheFile;          /**< LittleFS path. */
    size_t maxBody;
    uint8_t feedCount;              /**< At most KIT_WX_FEEDS. */
    bool keyOptional;               /**< Works without a key (a key unlocks a paid plan). */
    /** @brief The request for a feed: URL and up to two header pairs (ending in nullptr). False: not to be read now. */
    bool (*request)(const Mtb_Kit_Wx_Config_t& config, uint8_t feed, const Mtb_Kit_Wx_Data_t& data, char* url, size_t urlSize, const char** headers);
    /** @brief Reads an answer (null doc if not JSON) into @p work, which may also update work.plan. */
    uint8_t (*parse)(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& config, Mtb_Kit_Wx_Data_t& work, char* error, size_t errorSize);
    /** @brief Seconds until the feed is read again after a good answer (pacing, plan); 0: not read at all now. */
    uint32_t (*every)(uint8_t feed, const Mtb_Kit_Wx_Config_t& config, const Mtb_Kit_Wx_Data_t& data, uint32_t now);
    /** @brief A place search's URL. */
    bool (*searchRequest)(const Mtb_Kit_Wx_Config_t& config, const char* query, char* url, size_t urlSize);
    /** @brief The places in a search answer; -1 with @p error set on a refusal. */
    int (*parsePlaces)(int httpCode, JsonVariantConst doc, Mtb_Kit_Wx_Place_t* places, int max, char* error, size_t errorSize);
};

using Mtb_Kit_Wx_Search_Fn = void (*)(const char* query, const Mtb_Kit_Wx_Place_t* places, int count, const char* error);

/** @brief The engine of one app: create one static instance and leave it to the functions below. */
struct Mtb_Kit_Wx_t {
    const Mtb_Kit_Wx_Provider_t* provider;
    void* lock;                     /**< SemaphoreHandle_t */
    Mtb_Kit_Wx_Data_t* published;
    Mtb_Kit_Wx_Config_t config;
    uint32_t version;
    uint32_t generation;
    uint32_t due[KIT_WX_FEEDS];
    uint32_t lastManual, lastFetch, lastSave;
    uint8_t fails;
    bool started, busy;
    char searchQuery[64];
    Mtb_Kit_Wx_Search_Fn searchFn;
};

// Model ------------------------------------------------------------------------------------------------------
/** @brief Clears the data: nothing known, every number NAN. */
void mtb_Kit_Wx_Clear(Mtb_Kit_Wx_Data_t& data);
/** @brief FNV-1a of a key: which key a cached plan belongs to. */
uint32_t mtb_Kit_Wx_Key_Hash(const char* key);
/** @brief yyyymm of a Unix time (UTC). */
uint32_t mtb_Kit_Wx_Month_Of(uint32_t unixTime);
/** @brief "NNW" from degrees the wind comes from. */
const char* mtb_Kit_Wx_Cardinal(float degrees);
/** @brief The index's band: 0 good ... 5 hazardous, its name ("MODERATE") and colour. */
int mtb_Kit_Aqi_Band(uint8_t scale, int index);
const char* mtb_Kit_Aqi_Name(uint8_t scale, int index);
uint16_t mtb_Kit_Aqi_Color(uint8_t scale, int index);
/** @brief The civil date of a Unix time at @p offset seconds from UTC. */
void mtb_Kit_Wx_Date_Of(uint32_t unixTime, int32_t offset, int16_t& year, uint8_t& month, uint8_t& mday);

// Engine -----------------------------------------------------------------------------------------------------
void mtb_Kit_Wx_Begin(Mtb_Kit_Wx_t& engine, const Mtb_Kit_Wx_Provider_t& provider, const Mtb_Kit_Wx_Config_t& config);
void mtb_Kit_Wx_Set_Config(Mtb_Kit_Wx_t& engine, const Mtb_Kit_Wx_Config_t& config);
void mtb_Kit_Wx_Poll(Mtb_Kit_Wx_t& engine, bool online, uint32_t now);
bool mtb_Kit_Wx_Copy(Mtb_Kit_Wx_t& engine, Mtb_Kit_Wx_Data_t& out, uint32_t& version);
void mtb_Kit_Wx_Get_Status(Mtb_Kit_Wx_t& engine, Mtb_Kit_Wx_Status_t& status, char* errorText = nullptr, size_t errorSize = 0);
/** @brief Reads every feed now. False within five minutes of the last time, or without a key the provider needs. */
bool mtb_Kit_Wx_Refresh(Mtb_Kit_Wx_t& engine, uint32_t now);
/** @brief Searches places at the next poll; @p fn gets the answer on the fetch task. False with a search waiting. */
bool mtb_Kit_Wx_Search(Mtb_Kit_Wx_t& engine, const char* query, Mtb_Kit_Wx_Search_Fn fn);

// Screens ----------------------------------------------------------------------------------------------------
enum Mtb_Kit_Wx_Page_t : uint8_t {
    KIT_WX_PAGE_NOW = 0, KIT_WX_PAGE_HOURS, KIT_WX_PAGE_DAYS, KIT_WX_PAGE_WEEKS, KIT_WX_PAGE_SKY, KIT_WX_PAGE_AIR,
    KIT_WX_PAGE_ALERTS, KIT_WX_PAGE_APP, KIT_WX_PAGE_COUNT
};

struct Mtb_Kit_Wx_View_t {
    const Mtb_Kit_Wx_Data_t* data;
    const char* place;              /**< UTF-8. */
    uint8_t page;
    uint32_t now;
    const char* credit;             /**< The provider's attribution, on every screen. */
    const char* appName;            /**< For a message screen's band. */
    const char* message;            /**< Set: a message screen instead. */
    const char* detail;
};

/** @brief The page after @p page that has something to show (the weeks with over 7 days, air when read, alerts when
 *         any are in force); KIT_WX_PAGE_APP only if @p withApp. */
uint8_t mtb_Kit_Wx_Next_Page(uint8_t page, const Mtb_Kit_Wx_Data_t& data, bool withApp);
/** @brief Draws a page (not KIT_WX_PAGE_APP, which the app draws) or the message. */
void mtb_Kit_Wx_Draw(const Mtb_Kit_Wx_View_t& view);
/** @brief The band across the top of a screen, and the credit at the bottom right: for an app's own page. */
void mtb_Kit_Wx_Band(const char* left, const char* right, uint16_t color);
void mtb_Kit_Wx_Credit(const char* credit);

#endif
