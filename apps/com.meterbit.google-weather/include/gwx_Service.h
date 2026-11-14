/**
 * @file gwx_Service.h
 * @brief Google Weather's data service: what to read and when so the month's call budget lasts, the fetch task,
 *        place searches for the phone, the flash cache, and the copy the screens draw from. It outlives the app.
 */
#ifndef GWX_SERVICE_H
#define GWX_SERVICE_H

#include "gwx_Model.h"

struct Gwx_Status_t {
    bool started;
    bool busy;
    bool keyMissing;
    bool denied;                    /**< Key refused, API not enabled, or no billing: errorText says which. */
    bool quotaUsed;                 /**< Google's quota, or this month's budget. */
    bool failing;
    uint32_t interval;              /**< Seconds between current-conditions reads. */
    uint32_t usedCalls;
    uint32_t lastFetch;
};

/** @brief The answer to gwx_Service_Search(), on the fetch task. */
using Gwx_Search_Fn = void (*)(const char* query, const Gwx_Place_t* places, int count, const char* error);

void gwx_Service_Begin(const Gwx_Settings_t& settings);
void gwx_Service_Set_Settings(const Gwx_Settings_t& settings);
void gwx_Service_Poll(bool online, uint32_t nowUnix);
bool gwx_Service_Copy(Gwx_Data_t& out, uint32_t& version);
void gwx_Service_Get_Status(Gwx_Status_t& status, char* errorText = nullptr, size_t errorSize = 0);
bool gwx_Service_Refresh_Now(uint32_t nowUnix);
void gwx_Service_Set_Search_Callback(Gwx_Search_Fn fn);
/** @brief Searches places (Geocoding, one call) at the next poll. False without a key or with one waiting. */
bool gwx_Service_Search(const char* query);

#endif
