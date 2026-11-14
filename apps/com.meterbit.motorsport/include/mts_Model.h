/**
 * @file mts_Model.h
 * @brief Motorsport Live {5,1}: Formula 1 race weekends on Pixlpal. The weekend's sessions with countdowns and the
 *        circuit's shape, live timing during sessions (positions, gaps, tyres, pit stops, flags and safety cars),
 *        results, and both championships. No Arduino, so it can be tested on a PC.
 *
 * Source (checked 11 November 2026): OpenF1, api.openf1.org/v1 (unofficial; not associated with Formula 1).
 *  - Free, no account: all data from 2023 on, except while a session is live (from 30 minutes before it starts to
 *    30 minutes after it ends). 3 requests a second, 30 a minute.
 *  - Sponsor (€9.90 a month, the user's own account): live data too, 6 a second, 60 a minute. POST /token with
 *    username and password (form) gives {"access_token", "expires_in": "3600", "token_type": "bearer"}.
 *  - Answers are JSON arrays of rows. A query with no rows answers 404 {"detail": "No results found."}.
 *  - Licence: CC BY-NC-SA 4.0, "for non-commercial analysis and fan engagement" (see the README).
 * Endpoints used: sessions, meetings, drivers, session_result, championship_drivers, championship_teams, position,
 * intervals, laps, stints, pit, race_control, weather, location (the circuit's shape, from one lap of last year's race).
 */
#ifndef MTS_MODEL_H
#define MTS_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>
#include "ntf_Notify.h"

#define MTS_NVS_KEY         "mtsSet"
#define MTS_SETTINGS_MAGIC  0x4D545301u
#define MTS_SESSIONS        160
#define MTS_MEETINGS        32
#define MTS_DRIVERS         24
#define MTS_TEAMS           12
#define MTS_TRACK_POINTS    160
#define MTS_LIVE_BEFORE     1800u       /**< OpenF1 calls a session live from 30 minutes before it starts... */
#define MTS_LIVE_AFTER      1800u       /**< ...to 30 minutes after it ends */

enum Mts_Type_t : uint8_t { MTS_PRACTICE = 0, MTS_QUALIFYING, MTS_SPRINT_QUALI, MTS_SPRINT, MTS_RACE, MTS_TESTING };
enum Mts_Page_t : uint8_t { MTS_P_WEEKEND = 0, MTS_P_LIVE, MTS_P_RESULT, MTS_P_DRIVERS, MTS_P_TEAMS, MTS_PAGES };
enum Mts_Plan_t : uint8_t { MTS_PLAN_FREE = 0, MTS_PLAN_SPONSOR, MTS_PLAN_REFUSED };
enum Mts_Flag_t : uint8_t { MTS_FLAG_NONE = 0, MTS_FLAG_GREEN, MTS_FLAG_YELLOW, MTS_FLAG_VSC, MTS_FLAG_SC, MTS_FLAG_SC_ENDING, MTS_FLAG_RED, MTS_FLAG_CHEQUERED };
enum Mts_Compound_t : uint8_t { MTS_TYRE_UNKNOWN = 0, MTS_TYRE_SOFT, MTS_TYRE_MEDIUM, MTS_TYRE_HARD, MTS_TYRE_INTER, MTS_TYRE_WET };

struct Mts_Settings_t {
    uint32_t magic;
    char user[64];                      /**< the OpenF1 sponsor account (secret), "" for free */
    char pass[64];                      /**< secret */
    uint8_t favourite;                  /**< a driver's number, 0 none */
    uint8_t pages;                      /**< a bit per Mts_Page_t */
    uint8_t cycle, seconds;
    uint8_t hour24;
    uint8_t liveFirst;                  /**< during a session, stay on live timing */
    uint8_t alertMinutes;               /**< tell this long before a session (0 off) */
    uint8_t alertRacesOnly;             /**< only qualifying, sprints and races */
    uint8_t alertFlags;                 /**< safety cars and red flags on the status bar */
    uint8_t sound;
    uint8_t phoneAlerts;
    Ntf_Config_t notify;
};

struct Mts_Session_t {
    uint32_t key, meeting;
    uint32_t start, end;
    int32_t gmtOffset;                  /**< the circuit's, seconds */
    uint16_t circuitKey;
    uint8_t type;                       /**< Mts_Type_t */
    uint8_t cancelled;
    char name[20];                      /**< "Practice 1", "Sprint Qualifying" */
    char circuit[24];                   /**< "Interlagos" */
    char location[24];                  /**< "São Paulo" (UTF-8) */
    char country[24];
};

struct Mts_Meeting_t { uint32_t key; char name[40]; };

struct Mts_Schedule_t {
    int count, meetings;
    uint16_t year;
    Mts_Session_t s[MTS_SESSIONS];      /**< by start */
    Mts_Meeting_t m[MTS_MEETINGS];
};

struct Mts_Driver_t {
    uint8_t number;
    char code[4];                       /**< "NOR" */
    char name[20];                      /**< "Norris" */
    char team[24];
    uint16_t color;                     /**< RGB565 */
};

struct Mts_Drivers_t { uint32_t session; int count; Mts_Driver_t d[MTS_DRIVERS]; };

struct Mts_Result_Row_t {
    uint8_t number, position;           /**< position 0: not classified */
    uint8_t dnf, dns, dsq;
    uint16_t laps;
    float points;
    float gap;                          /**< seconds behind (qualifying: in the last part reached), NAN unknown */
    int8_t lapsDown;
    float best;                         /**< qualifying: the best of the parts, seconds */
};

struct Mts_Result_t { uint32_t session; uint8_t type; int count; Mts_Result_Row_t r[MTS_DRIVERS]; };

struct Mts_Standings_t {
    uint32_t session;                   /**< after this race */
    int drivers, teams;
    struct { uint8_t number, position, before; float points, gained; } d[MTS_DRIVERS];
    struct { char team[24]; uint8_t position, before; float points, gained; } t[MTS_TEAMS];
};

/** One car in live timing. */
struct Mts_Car_t {
    uint8_t number;
    uint8_t position;                   /**< 0 unknown */
    uint32_t positionAt;                /**< when the position was set (ms in the feed, for order) */
    float gap, interval;                /**< seconds, NAN unknown */
    int8_t lapsDown;                    /**< > 0: laps behind the leader */
    uint8_t compound, tyreLaps;
    uint8_t pits;
    float best;                         /**< best lap, seconds, NAN none */
    float last;
    uint16_t lap;                       /**< laps started */
    uint8_t inPit;
};

struct Mts_Live_t {
    uint32_t session;
    uint8_t type;
    int count;
    Mts_Car_t car[MTS_DRIVERS];
    uint16_t lap, laps;                 /**< the leader's lap; the race's length (from last year), 0 unknown */
    uint8_t flag;                       /**< Mts_Flag_t */
    uint8_t finished;
    char message[96];                   /**< race control's newest */
    uint32_t messageAt;
    uint8_t fastest;                    /**< the number of the car with the fastest lap */
    float fastestTime;
    float air, track;
    uint8_t rain;
    // How far each feed has been read ("2026-11-08T18:00:00.123" style marks in Unix ms).
    uint64_t markPosition, markInterval, markLap, markPit, markControl, markWeather;
};

struct Mts_Track_t { uint16_t circuitKey; int n; int16_t x[MTS_TRACK_POINTS], y[MTS_TRACK_POINTS]; uint16_t laps; };

// Settings ---------------------------------------------------------------------------------------------------------
void mts_Settings_Default(Mts_Settings_t& s);
void mts_Settings_Clamp(Mts_Settings_t& s);

// Times and words ---------------------------------------------------------------------------------------------------
/** @brief Unix ms of "2026-11-08T17:03:31.020000+00:00" (offsets honoured). 0 if not a time. */
uint64_t mts_Parse_Time_Ms(const char* text);
/** @brief "2026-11-08T17:03:31.020" for a date> filter. */
void mts_Iso_Ms(char* dest, size_t size, uint64_t ms);
/** @brief "-08:00:00" into seconds. */
int32_t mts_Parse_Offset(const char* text);
uint8_t mts_Type_Of(const char* sessionType, const char* sessionName);
const char* mts_Type_Short(uint8_t type);       /**< "FP", "QUALI", "SPRINT", "RACE" */
uint8_t mts_Compound_Of(const char* text);
char mts_Compound_Letter(uint8_t compound);     /**< S M H I W ? */
uint16_t mts_Compound_Color(uint8_t compound);
uint16_t mts_Team_Color(const char* hex);       /**< "F47600" into RGB565, lifted when too dark */
void mts_Clock(char* dest, size_t size, uint32_t t, int32_t utcOffset, bool hour24);
/** @brief "2 D 04:12:30", "04:12:30", "12:30". */
void mts_Countdown(char* dest, size_t size, uint32_t seconds);
/** @brief "1:27.470" */
void mts_Lap_Time(char* dest, size_t size, float seconds);
/** @brief "+1.234", "+1 LAP", "LEADER". */
void mts_Gap_Text(char* dest, size_t size, float gap, int8_t lapsDown, bool leader);
const char* mts_Flag_Text(uint8_t flag);
uint16_t mts_Flag_Color(uint8_t flag);

// The schedule ------------------------------------------------------------------------------------------------------
void mts_Filter_Sessions(JsonDocument& filter);
/** @brief sessions?year=: into the schedule (sorted by start). @return sessions read, -1 not an answer. */
int mts_Parse_Sessions(JsonVariantConst rows, Mts_Schedule_t& out);
void mts_Filter_Meetings(JsonDocument& filter);
int mts_Parse_Meetings(JsonVariantConst rows, Mts_Schedule_t& out);
const char* mts_Meeting_Name(const Mts_Schedule_t& s, uint32_t meeting);
/** @brief The session live now (OpenF1's window: 30 min either side), -1 none. */
int mts_Live_Session(const Mts_Schedule_t& s, uint32_t now);
/** @brief The next session not yet started (cancelled ones skipped), -1 none. */
int mts_Next_Session(const Mts_Schedule_t& s, uint32_t now);
/** @brief The last finished session (of @p type, or any when 0xFF), -1 none. */
int mts_Last_Session(const Mts_Schedule_t& s, uint32_t now, uint8_t type);
/** @brief The weekend to show: the live or next session's meeting, or the last one's when the season is over. */
uint32_t mts_Weekend(const Mts_Schedule_t& s, uint32_t now);

// Drivers, results, standings --------------------------------------------------------------------------------------
void mts_Filter_Drivers(JsonDocument& filter);
int mts_Parse_Drivers(JsonVariantConst rows, uint32_t session, Mts_Drivers_t& out);
const Mts_Driver_t* mts_Driver(const Mts_Drivers_t& d, uint8_t number);
void mts_Filter_Result(JsonDocument& filter);
int mts_Parse_Result(JsonVariantConst rows, uint32_t session, uint8_t type, Mts_Result_t& out);
int mts_Parse_Standings_Drivers(JsonVariantConst rows, Mts_Standings_t& out);
int mts_Parse_Standings_Teams(JsonVariantConst rows, Mts_Standings_t& out);
/** @brief A team's colour from the drivers (the first driver of that team). */
uint16_t mts_Color_Of_Team(const Mts_Drivers_t& d, const char* team);

// Live timing ------------------------------------------------------------------------------------------------------
void mts_Live_Reset(Mts_Live_t& l, uint32_t session, uint8_t type, const Mts_Drivers_t& drivers);
void mts_Filter_Position(JsonDocument& filter);
void mts_Filter_Interval(JsonDocument& filter);
void mts_Filter_Laps(JsonDocument& filter);
void mts_Filter_Stints(JsonDocument& filter);
void mts_Filter_Pit(JsonDocument& filter);
void mts_Filter_Control(JsonDocument& filter);
void mts_Filter_Weather(JsonDocument& filter);
/** @brief Rows of each feed, newer than the feed's mark (which moves on). @return rows used. */
int mts_Live_Positions(JsonVariantConst rows, Mts_Live_t& l);
int mts_Live_Intervals(JsonVariantConst rows, Mts_Live_t& l);
int mts_Live_Laps(JsonVariantConst rows, Mts_Live_t& l);
int mts_Live_Stints(JsonVariantConst rows, Mts_Live_t& l);
int mts_Live_Pits(JsonVariantConst rows, Mts_Live_t& l);
/** @return true when the flag changed (for an alert). */
bool mts_Live_Control(JsonVariantConst rows, Mts_Live_t& l);
int mts_Live_Weather(JsonVariantConst rows, Mts_Live_t& l);
/** @brief The cars in running order (by position; qualifying and practice by best lap when no position). */
int mts_Live_Order(const Mts_Live_t& l, uint8_t* order, int max);

// The circuit -------------------------------------------------------------------------------------------------------
/** @brief One lap of location rows into an outline of up to MTS_TRACK_POINTS points, scaled to 0-1000. */
int mts_Parse_Track(JsonVariantConst rows, Mts_Track_t& out);

/** @brief An OpenF1 answer that is not rows: "No results found." (404) is no rows, not an error. */
bool mts_No_Rows(int httpCode, JsonVariantConst doc);

#endif
