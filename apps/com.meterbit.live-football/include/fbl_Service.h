/**
 * @file fbl_Service.h
 * @brief Data service of the Live Football app: what to fetch and when so the day's quota lasts, the fetch task,
 *        back-off, the flash cache, and the copy of the data the screen draws from. It outlives the app.
 *
 * Free plan (checked 6 November 2026): live matches, and fixtures by date for yesterday, today and tomorrow (UTC),
 * all leagues at once. Paid plans add the table, top scorers and fixtures further ahead.
 */
#ifndef FBL_SERVICE_H
#define FBL_SERVICE_H

#include "fbl_Model.h"

#define FBL_SETTINGS_MAGIC  0x46424C54UL    // "FBLT": favourites by team ID, quiet hours

enum Fbl_Goal_Alerts_t : uint8_t { FBL_ALERTS_OFF = 0, FBL_ALERTS_FAVOURITE, FBL_ALERTS_ALL };

struct Fbl_Settings_t {
    uint32_t magic;
    uint8_t followCount;
    uint8_t goalAlerts;             /**< Fbl_Goal_Alerts_t */
    uint8_t liveMinutes;            /**< Fastest live refresh. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t quietHours;             /**< No celebrations from quietFrom to quietTo, local time. */
    uint16_t quietFrom;             /**< Minutes after midnight. */
    uint16_t quietTo;
    uint16_t follow[FBL_MAX_FOLLOW];
    uint16_t tableLeague;           /**< League of the table and scorers pages (paid plans). */
    Fbl_Favourites_t favourites;
    char apiKey[FBL_KEY_LEN];
};

struct Fbl_Status_t {
    bool busy;
    bool keyMissing;
    bool keyRejected;
    bool quotaUsed;
    bool rateLimited;
    bool failing;
    bool lowMemory;
    uint32_t liveInterval;          /**< Seconds between live reads at the current pace; 0 when nothing is live. */
    bool started;
    Fbl_Quota_t quota;
    uint32_t updated;               /**< When the newest scores arrived; 0 never. */
    uint8_t locked;                 /**< Bit per Fbl_Feature_t the plan refused. */
};

/** @brief Gets the answer to fbl_Service_Search(), on the fetch task: the teams found, best first, or an error text. */
using Fbl_Search_Fn = void (*)(const char* query, const Fbl_Team_Found_t* teams, uint8_t count, const char* error);

void fbl_Settings_Default(Fbl_Settings_t& settings);
/** @brief Unknown leagues and repeats out; values into range. */
void fbl_Settings_Clamp(Fbl_Settings_t& settings);

void fbl_Service_Begin(const Fbl_Settings_t& settings);
void fbl_Service_Set_Settings(const Fbl_Settings_t& settings);
/** @param nowUnix  0 until the clock is set. */
void fbl_Service_Poll(bool online, uint32_t nowUnix);
bool fbl_Service_Copy(Fbl_Data_t& out, uint32_t& version);
void fbl_Service_Get_Status(Fbl_Status_t& status);
/** @brief Reads the live feed (and today's list, if older than ten minutes) now. False if asked too recently. */
bool fbl_Service_Refresh_Now(uint32_t nowUnix);

void fbl_Service_Set_Search_Callback(Fbl_Search_Fn fn);
/**
 * @brief Searches API-Football's teams (one request, Free plan included) at the next poll. False, and nothing
 *        asked, without a key, with the day's quota used, or with a search already waiting.
 */
bool fbl_Service_Search(const char* query, uint32_t nowUnix);

#endif
