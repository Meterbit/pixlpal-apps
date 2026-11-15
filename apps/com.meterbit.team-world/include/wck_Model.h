/**
 * @file wck_Model.h
 * @brief World Clock's model: settings, the cities the phone can pick (wck_City_List.cpp, from the phone app's former
 *        list), the search over them, and local time from a POSIX TZ rule ("EST5EDT,M3.2.0/2,M11.1.0/2", with
 *        :30 and :45 offsets and both hemispheres' summer time). No Arduino, so it can be tested on a PC.
 */
#ifndef WCK_MODEL_H
#define WCK_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define WCK_SETTINGS_MAGIC  0x57434B32UL    // "WCK2"
#define WCK_NVS_KEY         "worldClockCfg"
#define WCK_CITIES          6
#define WCK_TZ_LEN          44

extern const unsigned WCK_CITY_COUNT;
extern const char WCK_CITY_LIST[];

enum Wck_Mode_t : uint8_t { WCK_VIEW_FOCUS = 0, WCK_VIEW_BOARD, WCK_VIEW_PLANNER, WCK_VIEW_COUNT };

struct Wck_City_t {
    char name[24];
    char country[32];
    char code[3];                   /**< The flag's country code: "ng". */
    char tz[WCK_TZ_LEN];            /**< POSIX TZ rule. */
    uint16_t color;                 /**< RGB565 of the name on the board. */
};

struct Wck_Settings_t {
    uint32_t magic;
    uint8_t count;
    Wck_City_t cities[WCK_CITIES];
    uint8_t view;                   /**< Wck_Mode_t */
    uint8_t hour24;
    uint8_t cycle;                  /**< Views (and, in the focus view, cities) change by themselves. */
    uint8_t seconds;                /**< Per view or city while cycling (5-120). */
    uint16_t officeFrom, officeTo;  /**< Working hours, minutes after midnight, for the planner. */
};

void wck_Settings_Default(Wck_Settings_t& settings);
/**
 * @brief Keeps the settings sane. Control characters in a city's texts (a damaged save) are removed, and a damaged
 *        name is repaired from the city list when one city fits. @return how many cities were repaired, described in
 *        @p report ("city 1 name [50 01 67 6f 73] -> \"Lagos\"") if given.
 */
int wck_Settings_Clamp(Wck_Settings_t& settings, char* report = nullptr, size_t reportSize = 0);

// Time zones --------------------------------------------------------------------------------------------------
struct Wck_Tz_t {
    int32_t stdOffset;              /**< Seconds east of UTC (POSIX gives west: "EST5" is -5 h). */
    int32_t dstOffset;
    bool hasDst;
    struct Rule_t { uint8_t month, week, weekday; int32_t time; } start, end;   /**< Mm.w.d/time; time in seconds. */
    char stdName[8], dstName[8];
};

/** @brief Parses a POSIX TZ rule; false if it cannot be read. */
bool wck_Tz_Parse(const char* text, Wck_Tz_t& tz);
/** @brief Seconds east of UTC at @p utc; @p dst tells whether summer time applies. */
int32_t wck_Tz_Offset(const Wck_Tz_t& tz, int64_t utc, bool* dst = nullptr);
/** @brief The same from the rule's text (parsed each time). */
int32_t wck_Offset_Of(const char* tzText, int64_t utc, bool* dst = nullptr);

struct Wck_Local_t { int year, month, day, weekday, hour, minute, second; };   /**< month 1-12, weekday 0 Sunday. */
void wck_Local(int64_t utc, int32_t offset, Wck_Local_t& local);

/** @brief "14:05" or "2:05", "PM"/"AM" (empty in 24-hour). */
void wck_Clock_Text(const Wck_Local_t& t, bool hour24, char* time, size_t timeSize, char* meridiem, size_t meridiemSize);
/** @brief "+5H", "-3:30", "SAME" from an offset difference in seconds. */
void wck_Difference_Text(int32_t seconds, char* dest, size_t size);
/** @brief -1, 0 or 1: the city's date against the device's. */
int wck_Day_Difference(int64_t utc, int32_t cityOffset, int32_t localOffset);

enum Wck_Period_t : uint8_t { WCK_NIGHT = 0, WCK_MORNING, WCK_OFFICE, WCK_EVENING };
/** @brief What part of the day a local time is: office hours (from the settings), morning, evening or night. */
uint8_t wck_Period(int hour, int minute, uint16_t officeFrom, uint16_t officeTo);
const char* wck_Period_Name(uint8_t period);

// Cities ------------------------------------------------------------------------------------------------------
/** @brief Cities whose name or country starts with (or has a word starting with) every word of @p query. */
int wck_Search(const char* query, Wck_City_t* results, int max);
/** @brief "Tokyo|jp|JST-9" (with the country after a second bar when given) to a city, and back. */
bool wck_Parse_Value(const char* value, Wck_City_t& city);
void wck_Value_Of(const Wck_City_t& city, char* dest, size_t size);
/** @brief The catalogue's city named @p name (and, if given, in @p country). */
bool wck_Find(const char* name, const char* country, Wck_City_t& city);

#endif
