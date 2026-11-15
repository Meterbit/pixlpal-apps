/**
 * @file fbl_Model.h
 * @brief Data model of the Live Football app, and the readers of API-Football's answers. Nothing here knows about
 *        the network or the panel, so all of it can be tested on a PC.
 *
 * Shapes (API-Football v3, checked against live answers on 6 November 2026):
 *   /fixtures   {"errors":[]|{...},"results":N,"response":[{"fixture":{id,timestamp,venue:{name},status:{short,
 *               elapsed,extra}},"league":{id,name,season,round},"teams":{"home":{id,name},"away":{...}},
 *               "goals":{home,away},"score":{halftime,penalty},"events":[{time:{elapsed,extra},team:{id},
 *               player:{name},type:"Goal"|"Card"|"subst"|"Var",detail}]}]}  (events only in live answers)
 *   /standings  "response":[{"league":{id,season,"standings":[[{rank,team:{id,name},points,goalsDiff,form,
 *               description,all:{played}}]]}}]
 *   /players/topscorers  "response":[{"player":{name},"statistics":[{team:{name},goals:{total}}]}]
 *   /status     "response":{"subscription":{plan},"requests":{current,limit_day}}
 *   /teams?search=  "response":[{"team":{id,name,code,country,founded,national,logo},"venue":{...}}]  (Free plan too)
 *   errors      {"plan":"Free plans do not have access to this season..."}, {"requests":...}, {"rateLimit":...},
 *               {"token":...}; the HTTP status is 200 even then.
 */
#ifndef FBL_MODEL_H
#define FBL_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define FBL_MAX_FOLLOW      8
#define FBL_MAX_MATCHES     40
#define FBL_MAX_EVENTS      14
#define FBL_TEAM_LEN        24
#define FBL_PLAYER_LEN      18
#define FBL_ROUND_LEN       22
#define FBL_VENUE_LEN       28
#define FBL_LEAGUE_LEN      22
#define FBL_MAX_FAVOURITES  8
#define FBL_SEARCH_RESULTS  20
#define FBL_TABLE_ROWS      24
#define FBL_SCORERS         10
#define FBL_KEY_LEN         72

#define FBL_DATA_MAGIC      0x46424C32UL    // "FBL2"
#define FBL_DATA_LAYOUT     2

enum Fbl_Event_Type_t : uint8_t {
    FBL_EV_GOAL = 0, FBL_EV_OWN_GOAL, FBL_EV_PENALTY, FBL_EV_MISSED_PENALTY, FBL_EV_YELLOW, FBL_EV_RED,
    FBL_EV_SUB, FBL_EV_VAR, FBL_EV_OTHER,
};

struct Fbl_Event_t {
    uint8_t minute;
    uint8_t extra;              /**< Stoppage-time minutes, 45+2 is minute 45, extra 2. */
    uint8_t type;               /**< Fbl_Event_Type_t */
    uint8_t away;               /**< 0: the home side's event, 1: the away side's. An own goal counts for the other side. */
    char player[FBL_PLAYER_LEN];
};

struct Fbl_Team_t {
    uint32_t id;
    char name[FBL_TEAM_LEN];
};

enum Fbl_Phase_t : uint8_t { FBL_PHASE_UPCOMING = 0, FBL_PHASE_LIVE, FBL_PHASE_BREAK, FBL_PHASE_FINISHED, FBL_PHASE_OFF };

struct Fbl_Match_t {
    uint32_t id;
    uint32_t kickoff;           /**< Unix time. */
    uint32_t updated;           /**< When this device last received it. */
    uint16_t leagueId;
    uint16_t season;
    char status[5];             /**< "NS", "1H", "HT", "2H", "ET", "P", "FT", "AET", "PEN", "PST"... */
    uint8_t elapsed;
    uint8_t extra;
    int8_t goals[2];            /**< -1 before kick-off. */
    int8_t halftime[2];
    int8_t penalties[2];
    uint8_t awaitingFinal;      /**< Left the live feed: the final result is still to be read. */
    uint8_t eventCount;
    Fbl_Team_t team[2];
    char round[FBL_ROUND_LEN];
    char venue[FBL_VENUE_LEN];
    char league[FBL_LEAGUE_LEN];   /**< The API's name, in capitals, for leagues not in fbl_Leagues(). */
    Fbl_Event_t events[FBL_MAX_EVENTS];
};

struct Fbl_Row_t {
    uint8_t rank;
    uint8_t played;
    int16_t goalDiff;
    uint16_t points;
    uint8_t zone;               /**< 0 none, 1 top (title, Champions League, promotion), 2 Europe or play-offs, 3 relegation. */
    uint32_t teamId;
    char team[FBL_TEAM_LEN];
    char form[6];               /**< Newest last: "WWDLW". */
};

struct Fbl_Table_t {
    uint16_t leagueId;
    uint16_t season;
    uint8_t count;
    uint32_t fetched;
    Fbl_Row_t rows[FBL_TABLE_ROWS];
};

struct Fbl_Scorer_t {
    char player[FBL_PLAYER_LEN];
    char team[FBL_TEAM_LEN];
    uint8_t goals;
};

struct Fbl_Quota_t {
    uint32_t seen;              /**< When last read; 0 never. */
    uint16_t limitDay;
    uint16_t remaining;
    uint8_t paid;               /**< The plan is not "Free". */
    uint8_t exhausted;          /**< The API said the day's requests are used up. */
    char plan[16];
};

enum Fbl_Feature_t : uint8_t { FBL_F_TABLE = 0, FBL_F_SCORERS, FBL_F_UPCOMING, FBL_F_COUNT };

struct Fbl_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    uint8_t matchCount;
    Fbl_Match_t matches[FBL_MAX_MATCHES];
    uint32_t dayFetched[3];         /**< Yesterday, today, tomorrow (UTC): when each list was read. */
    uint32_t dayKey[3];             /**< ...and for which UTC day (days since 1970). */
    uint32_t liveFetched;
    Fbl_Table_t table;
    uint8_t scorerCount;
    uint16_t scorersLeague;
    uint32_t scorersFetched;
    Fbl_Scorer_t scorers[FBL_SCORERS];
    Fbl_Quota_t quota;
    uint8_t locked[FBL_F_COUNT];    /**< The plan refused it. */
};

/** @brief Favourite teams, picked on the phone from a team search: matched by API-Football's team ID. */
struct Fbl_Favourite_t {
    uint32_t id;
    char name[FBL_TEAM_LEN];        /**< As API-Football writes it, UTF-8, for the phone. */
};

struct Fbl_Favourites_t {
    uint8_t count;
    Fbl_Favourite_t team[FBL_MAX_FAVOURITES];
};

/** @brief A team found by a search, for the phone's searchPicker. */
struct Fbl_Team_Found_t {
    uint32_t id;
    uint16_t founded;
    uint8_t national;
    char name[FBL_TEAM_LEN];
    char country[24];
};

/** @brief A league the phone offers. */
struct Fbl_League_t {
    uint16_t id;
    const char* name;       /**< As shown on the board: "PREMIER LEAGUE". */
    const char* country;
};

const Fbl_League_t* fbl_Leagues(size_t& count);
const Fbl_League_t* fbl_Find_League(uint16_t id);

/** @brief The phase a status code puts a match in. */
uint8_t fbl_Phase(const char* status);
/** @brief Whether a status means play is going on or paused within the match (HT, a break before extra time...). */
bool fbl_In_Play(const Fbl_Match_t& match);

// Reading answers ----------------------------------------------------------------------------------------------
/** @brief The filter for one /fixtures element; @p events adds the events. */
void fbl_Match_Filter(JsonDocument& filter, bool events);
/** @brief Reads one /fixtures element. Returns false if it has no id. */
bool fbl_Parse_Match(JsonVariantConst item, Fbl_Match_t& match, uint32_t now);

void fbl_Table_Filter(JsonDocument& filter);
bool fbl_Parse_Table(JsonVariantConst item, Fbl_Table_t& table);
void fbl_Scorer_Filter(JsonDocument& filter);
/** @brief Filter and reader of one /teams?search= element. */
void fbl_Team_Filter(JsonDocument& filter);
bool fbl_Parse_Team(JsonVariantConst item, Fbl_Team_Found_t& team);
/**
 * @brief Adds a found team to @p list, best first: the name equal to @p query, then names starting with it, then
 *        the rest, in the API's order; within each, first teams and women's teams before youth and reserve sides.
 */
void fbl_Add_Found_Team(Fbl_Team_Found_t* list, uint8_t& count, uint8_t max, const Fbl_Team_Found_t& team, const char* query);
/** @brief Whether a search query is acceptable: 3 to 30 letters, digits, spaces, and . - ' &. */
bool fbl_Search_Query_Valid(const char* query);
bool fbl_Parse_Scorer(JsonVariantConst item, Fbl_Scorer_t& scorer);

enum Fbl_Error_t : uint8_t { FBL_ERR_NONE = 0, FBL_ERR_PLAN, FBL_ERR_QUOTA, FBL_ERR_RATE, FBL_ERR_KEY, FBL_ERR_OTHER };
/** @brief Classifies an "errors" value ([] or {} when none); @p text receives its message. */
uint8_t fbl_Classify_Errors(JsonVariantConst errors, char* text, size_t size);

// Keeping the match list ----------------------------------------------------------------------------------------
int fbl_Find_Match(const Fbl_Data_t& data, uint32_t id);

/** @brief Whether a team (by API-Football ID) is a favourite. */
bool fbl_Is_Favourite(uint32_t teamId, const Fbl_Favourites_t& favourites);
bool fbl_Match_Is_Favourite(const Fbl_Match_t& match, const Fbl_Favourites_t& favourites);

/**
 * @brief Puts a match into the list: replaces the one with its id, keeping events and the live state the day lists
 *        do not carry; when full, makes room by dropping the match furthest in time from @p now, never a favourite's.
 * @return false if there was no room.
 */
bool fbl_Put_Match(Fbl_Data_t& data, const Fbl_Match_t& match, uint32_t now, const Fbl_Favourites_t& favourites);

/** @brief Drops matches that kicked off in [dayStart, dayStart + 1 day) and are not in @p ids (moved or cancelled). */
void fbl_Drop_Missing(Fbl_Data_t& data, uint32_t dayStart, const uint32_t* ids, size_t count);

/**
 * @brief Matches that were in play but are not in a new live answer: they have finished (or stopped). Marks them
 *        awaiting their final result. Returns how many.
 */
int fbl_Mark_Left_Live(Fbl_Data_t& data, const uint32_t* liveIds, size_t count, uint32_t now);

/** @brief Drops matches that ended more than @p ageSec ago. */
void fbl_Prune(Fbl_Data_t& data, uint32_t now, uint32_t ageSec);

/** @brief Sorts by kick-off. */
void fbl_Sort(Fbl_Data_t& data);

/**
 * @brief When the live feed needs reading until, from the matches in the list: 0 if no match is in play or due.
 */
uint32_t fbl_Live_Window_End(const Fbl_Data_t& data, uint32_t now);

/**
 * @brief Seconds between live reads so what is left of the day's quota lasts until @p windowEnd (or midnight UTC,
 *        whichever is first): never faster than @p floorSec, never slower than FBL_LIVE_SLOWEST_SEC.
 */
uint32_t fbl_Plan_Live_Interval(const Fbl_Quota_t& quota, uint32_t now, uint32_t windowEnd, uint32_t floorSec);

// Text ---------------------------------------------------------------------------------------------------------
/** @brief UTF-8 to the panel's ASCII: accents dropped ("Atlético" -> "Atletico"), anything else left out. */
void fbl_To_Ascii(char* dest, size_t size, const char* text);
/** @brief "Regular Season - 11" -> "MATCHDAY 11"; "1/128-finals" -> "ROUND OF 256"; others in capitals. */
void fbl_Round_Text(char* dest, size_t size, const char* round);
/** @brief The last name of "B. Saka" or "Bukayo Saka": "Saka". */
const char* fbl_Short_Player(const char* name);

int32_t fbl_Days_From_Civil(int y, int m, int d);

#endif
