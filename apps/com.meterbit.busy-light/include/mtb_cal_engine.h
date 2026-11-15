/**
 * @file mtb_cal_engine.h
 * @brief The calendar apps' network side: one worker task that keeps the account's events (and tasks, and the unread
 *        mail count) fresh through a provider (Google, Microsoft), and answers the phone's searches of calendars and
 *        task lists. The app task reads copies; nothing here draws.
 */
#ifndef MTB_CAL_ENGINE_H
#define MTB_CAL_ENGINE_H

#include "mtb_cal_model.h"
#include "mtb_kit_oauth.h"
#include "mtb_kit_http.h"

#define MTB_CAL_CALENDARS   40      /**< Calendars of an account kept for choosing. */

struct Mtb_Cal_Calendar_t {
    char id[MTB_CAL_ID];
    char name[MTB_CAL_NAME];
    uint16_t color;
    uint8_t primary;            /**< The account's own calendar. */
    uint8_t shown;              /**< Shown by default (Google: ticked in Calendar; Microsoft: every calendar). */
    uint8_t holiday;
};

/** @brief An authorised API call's context, for the provider. */
struct Mtb_Cal_Api_t {
    Mtb_Kit_OAuth_t* oauth;
    const char* userAgent;
    int lastCode;               /**< HTTP status of the last call. */
    char error[80];             /**< What went wrong, for the phone ("Calendar API not enabled", "HTTP 503"). */
};

/**
 * @brief GET with the account's access token. A 401 refreshes the token and tries once more. On an error status the
 *        API's message is put in api.error. @return The body (PSRAM, heap_caps_free) or nullptr.
 * @param extraHeaders  Name, value, …, nullptr; or nullptr.
 */
char* mtb_Cal_Api_Get(Mtb_Cal_Api_t& api, const char* url, size_t maxBytes, size_t& length, const char* const* extraHeaders = nullptr);

/** @brief Where calendars come from. A function returns a count, or -1 when it failed (api.error says why). */
struct Mtb_Cal_Provider_t {
    const char* name;                                   /**< "Google" */
    const Mtb_Kit_OAuth_Provider_t* oauth;
    bool (*account)(Mtb_Cal_Api_t& api, char* dest, size_t size);
    int (*calendars)(Mtb_Cal_Api_t& api, Mtb_Cal_Calendar_t* out, int max);
    /** @brief Adds the calendar's events between @p from and @p to (Unix) to @p into with mtb_Cal_Add_Event. */
    int (*events)(Mtb_Cal_Api_t& api, const Mtb_Cal_Calendar_t& calendar, uint32_t from, uint32_t to, Mtb_Cal_Data_t& into);
    int (*taskLists)(Mtb_Cal_Api_t& api, Mtb_Cal_Pick_t* out, int max);     /**< nullptr: no tasks */
    /** @brief Adds the list's open tasks (@p listId "" : the default list). */
    int (*tasks)(Mtb_Cal_Api_t& api, const char* listId, Mtb_Cal_Data_t& into);
    int (*unreadMail)(Mtb_Cal_Api_t& api);              /**< nullptr: no mail */
    /** @brief A country's public holidays as a calendar ("ng"); nullptr: not offered. */
    bool (*holidayCalendar)(const char* country, const char* name, Mtb_Cal_Calendar_t& out);
};

enum Mtb_Cal_Sync_t : uint8_t { MTB_CAL_SYNC_IDLE = 0, MTB_CAL_SYNC_BUSY, MTB_CAL_SYNC_OK, MTB_CAL_SYNC_PARTIAL, MTB_CAL_SYNC_FAILED };

struct Mtb_Cal_Status_t {
    uint8_t link;               /**< Mtb_Kit_OAuth_State_t */
    uint8_t sync;               /**< Mtb_Cal_Sync_t of the last sync (BUSY while one runs). */
    uint32_t lastOk;            /**< Unix time of the last good sync, 0 never. */
    uint32_t nextAt;            /**< Unix time of the next. */
    uint8_t calendarsUsed;
    char error[96];
    char account[MTB_CAL_ACCOUNT];
};

/** @brief What a search found (on the worker task): the app sends it to the phone. */
typedef void (*Mtb_Cal_Search_Fn)(const char* searchId, const char* query, const Mtb_Cal_Pick_t* picks, const uint16_t* colors,
                                  int count, const char* error);

bool mtb_Cal_Engine_Start(const Mtb_Cal_Provider_t& provider, const Mtb_Cal_Settings_t& settings, Mtb_Cal_Search_Fn searchFn);
void mtb_Cal_Engine_Stop(void);
/** @brief New settings: synced again at once when what is fetched changed. */
void mtb_Cal_Engine_Set_Settings(const Mtb_Cal_Settings_t& settings);
/** @brief Sync now (the phone's button, the knob, a new link). */
void mtb_Cal_Engine_Sync(void);
/** @brief The data, if newer than @p version (then updated). */
bool mtb_Cal_Engine_Copy(Mtb_Cal_Data_t& out, uint32_t& version);
void mtb_Cal_Engine_Get_Status(Mtb_Cal_Status_t& status);
/** @brief The engine is running for this provider (its data and status are this app's). */
bool mtb_Cal_Engine_Running_For(const Mtb_Cal_Provider_t& provider);
/** @brief Searches the account's calendars ("calendars") or task lists ("taskList") by name; "" lists all. */
bool mtb_Cal_Engine_Search(const char* searchId, const char* query);

#endif
