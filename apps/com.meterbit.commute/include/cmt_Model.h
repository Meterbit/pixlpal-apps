/**
 * @file cmt_Model.h
 * @brief Commute & Departures {10,0} apart from the network: places, boards (a stop's departures, or a drive between
 *        two places), active times, the monthly request budgets, and reading Transitland, TomTom and Google answers.
 *        No device code: tested on a PC.
 */
#ifndef CMT_MODEL_H
#define CMT_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define CMT_PLACES          4       /**< Home, Work, School, Other */
#define CMT_BOARDS          6
#define CMT_DEPARTURES      12      /**< Kept per departure board */
#define CMT_HISTORY         24      /**< Drive times kept for the trend (one per refresh) */
#define CMT_KEY_LEN         64
#define CMT_SETTINGS_MAGIC  0x434D5431u   // "CMT1"
#define CMT_NVS_KEY         "cmtSettings"
#define CMT_USAGE_KEY       "cmtUsage"

enum Cmt_Kind_t : uint8_t { CMT_DEPARTURES_BOARD = 0, CMT_DRIVE_BOARD };
enum Cmt_Drive_t : uint8_t { CMT_TOMTOM = 0, CMT_GOOGLE };
enum Cmt_Plan_t : uint8_t { CMT_PLAN_EXPLORER = 0, CMT_PLAN_HOBBYIST, CMT_PLAN_PAYG };
enum Cmt_Days_t : uint8_t { CMT_DAYS_ALL = 0, CMT_DAYS_WEEKDAYS, CMT_DAYS_WEEKENDS };

struct Cmt_Place_t {
    uint8_t set;
    int32_t latE6, lonE6;       /**< Degrees x 1e6 */
    char address[72];
};

struct Cmt_Board_t {
    uint8_t used;
    uint8_t kind;               /**< Cmt_Kind_t */
    char label[24];
    // A stop's departures.
    char stopKey[72];           /**< Transitland stop key: onestop id, or "<feed onestop id>:<stop id>" */
    char stopName[48];
    char lines[48];             /**< Route short names to show, comma separated; "" all */
    uint8_t walkMinutes;        /**< To the stop: "leave in" = departure - walk */
    // A drive.
    uint8_t from, to;           /**< Place indices */
    uint16_t arriveBy;          /**< Minutes after midnight, 0xFFFF none: "leave by" */
    // When it is fetched and shown.
    uint8_t days;               /**< Cmt_Days_t */
    uint16_t fromMinute, toMinute;   /**< Equal: all day */
    uint8_t alert;              /**< A leave alert (chime and banner) */
};

struct Cmt_Settings_t {
    uint32_t magic;
    Cmt_Place_t places[CMT_PLACES];
    Cmt_Board_t boards[CMT_BOARDS];
    uint8_t drive;              /**< Cmt_Drive_t */
    uint8_t plan;               /**< Cmt_Plan_t (Transitland) */
    uint16_t paygCap;           /**< Pay As You Go: Transitland requests a month to stop at */
    uint16_t googleCap;         /**< Google Routes (traffic) requests a month to stop at (5,000 are free) */
    uint8_t hour24, chime, alertMinutes, miles, avoidTolls, avoidHighways;
    uint8_t editing;            /**< The board the phone edits */
    char transitKey[CMT_KEY_LEN];
    char tomtomKey[CMT_KEY_LEN];
    char googleKey[CMT_KEY_LEN];
};

/** @brief Requests made this month, kept in NVS. */
struct Cmt_Usage_t {
    uint32_t month;             /**< yyyymm (UTC) */
    uint32_t transit, route, geocode;
};

void cmt_Settings_Default(Cmt_Settings_t& s);
void cmt_Settings_Clamp(Cmt_Settings_t& s);
const char* cmt_Place_Name(int index);     /**< "Home"… */
int cmt_Board_Count(const Cmt_Settings_t& s);

// Active times ----------------------------------------------------------------------------------------------------
/** @brief The board is active at local weekday (0 Monday) and minute. */
bool cmt_Active(const Cmt_Board_t& b, int weekday, int minute);
/** @brief Seconds the board is active from a local moment to the end of its month (for the budget). */
uint32_t cmt_Active_Seconds_Left(const Cmt_Board_t& b, int year, int month, int day, int weekday, int minute);

// Budgets ---------------------------------------------------------------------------------------------------------
/** @brief Transitland requests a month for the plan (minus a reserve), the cap given for Pay As You Go. */
uint32_t cmt_Transit_Monthly(const Cmt_Settings_t& s);
uint32_t cmt_Route_Monthly(const Cmt_Settings_t& s);
/**
 * @brief Seconds between refreshes so @p left requests last @p activeSecondsLeft, shared by @p consumers boards, never
 *        under @p floorSec; 0 when nothing is left (stop asking until next month).
 */
uint32_t cmt_Interval(uint32_t left, uint32_t activeSecondsLeft, int consumers, uint32_t floorSec);
void cmt_Usage_Roll(Cmt_Usage_t& u, uint32_t yyyymm);

// Departures ---------------------------------------------------------------------------------------------------
struct Cmt_Departure_t {
    uint32_t time;              /**< Unix: estimated if known, else scheduled */
    int32_t delay;              /**< Seconds late (estimated - scheduled), 0 none */
    uint8_t realtime;           /**< An estimate from GTFS-RT */
    uint8_t cancelled;
    uint16_t color, textColor;  /**< RGB565 from route_color, route_text_color */
    char line[8];               /**< route_short_name (or a short long name) */
    char headsign[40];
};

/** @brief Reads GET /stops/{key}/departures: the departures of the lines asked for (@p lines "" all), soonest first,
 *         those already gone dropped. @return How many, or -1 if the JSON could not be read. */
int cmt_Parse_Departures(const char* json, size_t length, const char* lines, uint32_t now, Cmt_Departure_t* out, int max);
/** @brief The distinct lines in a departures answer ("12 → Ikeja"), for the phone's line picker. */
int cmt_Parse_Lines(const char* json, size_t length, char (*lines)[64], int max);

struct Cmt_Stop_t { char key[72]; char name[48]; char detail[64]; };
/** @brief Reads GET /stops (search): stops and stations. */
int cmt_Parse_Stops(const char* json, size_t length, Cmt_Stop_t* out, int max);

// Drives -----------------------------------------------------------------------------------------------------------
struct Cmt_Drive_Result_t {
    bool ok;
    uint32_t seconds;           /**< With traffic */
    uint32_t normalSeconds;     /**< Without traffic (or historic) */
    uint32_t meters;
    char via[48];               /**< Main roads, "" unknown */
};
/** @brief TomTom calculateRoute JSON. */
bool cmt_Parse_TomTom_Route(const char* json, size_t length, Cmt_Drive_Result_t& out);
/** @brief Google Routes computeRoutes JSON (duration "1234s", staticDuration, distanceMeters, description). */
bool cmt_Parse_Google_Route(const char* json, size_t length, Cmt_Drive_Result_t& out);

struct Cmt_Address_t { char text[72]; int32_t latE6, lonE6; };
/** @brief TomTom geocode/search JSON results. */
int cmt_Parse_TomTom_Geocode(const char* json, size_t length, Cmt_Address_t* out, int max);
/** @brief Google Geocoding JSON results ("status" must be OK). */
int cmt_Parse_Google_Geocode(const char* json, size_t length, Cmt_Address_t* out, int max, char* status, size_t statusSize);

// Labels ------------------------------------------------------------------------------------------------------------
/** @brief "NOW", "3 MIN", "1 H 05", or the clock time for later ones. */
void cmt_Minutes_Text(char* dest, size_t size, uint32_t time, uint32_t now, bool hour24);
/** @brief RGB565 from "0A7F3C" (GTFS colours, no '#'); @p fallback if empty or bad. */
uint16_t cmt_Color(const char* hex, uint16_t fallback);
/** @brief The minute (after midnight) to leave a board's place for an arrive-by time, or -1. */
int cmt_Leave_By(uint16_t arriveBy, uint32_t driveSeconds);

#endif
