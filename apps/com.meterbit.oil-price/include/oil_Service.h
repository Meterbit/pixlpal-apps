/**
 * @file oil_Service.h
 * @brief Data service of the Oil price app: when to fetch, the fetch task, back-off, the flash cache and the
 *        copy of the data the screen draws from.
 *
 * The service outlives the app: its data and any fetch in progress survive the app being closed, so reopening
 * it shows the data at once and never starts a second download.
 */
#ifndef OIL_SERVICE_H
#define OIL_SERVICE_H

#include "oil_Provider.h"

#define OIL_KEY_LEN         64
#define OIL_SETTINGS_MAGIC  0x4F494C53UL    // "OILS"

/** @brief The user's settings. Kept in NVS by the app. */
struct Oil_Settings_t {
    uint32_t magic;
    uint16_t priceRefreshMin;
    uint16_t newsRefreshMin;
    uint8_t pageSeconds;
    uint8_t autoCycle;                  /**< Non-zero: pages change by themselves. */
    char selected[OIL_CODE_LEN];        /**< Code of the series in the large display. */
    char apiKey[OIL_KEY_LEN];
};

/** @brief What the screen needs to know about the downloads. */
struct Oil_Status_t {
    bool busy;              /**< A download is running. */
    bool keyMissing;        /**< Prices need a key and none is set. */
    bool keyRejected;
    bool rateLimited;
    bool failing;           /**< The last attempt at some feed failed for another reason. */
    bool lowMemory;         /**< The last download was put off for lack of RAM. */
};

/** @brief Fills in the defaults, from oil_Config.h. The API key is the user's, sent from the phone. */
void oil_Settings_Default(Oil_Settings_t& settings);
/** @brief Brings out-of-range values back into range. */
void oil_Settings_Clamp(Oil_Settings_t& settings);

/**
 * @brief Starts the service, or updates its settings if it is already running. The first call loads the cache.
 *        Call from the app task.
 */
void oil_Service_Begin(Oil_Provider_t& provider, const Oil_Settings_t& settings);

/** @brief Applies new settings. A new key retries the feeds that need it at once. */
void oil_Service_Set_Settings(const Oil_Settings_t& settings);

/**
 * @brief Starts a download if one is due. Call often from the app task; it returns at once.
 * @param online   Whether there is an internet connection.
 * @param nowUnix  The current time, or 0 if the clock has not been set yet.
 */
void oil_Service_Poll(bool online, uint32_t nowUnix);

/**
 * @brief Copies the data into @p out if it has changed since the copy whose version is @p version.
 * @return true if @p out was updated.
 */
bool oil_Service_Copy(Oil_Data_t& out, uint32_t& version);

void oil_Service_Get_Status(Oil_Status_t& status);

/** @brief true if the provider offers a series with this code. Safe from any task. */
bool oil_Service_Has_Series(const char* code);

/** @brief Asks for every feed now. Returns false if the last request was too recent, or the source said wait. */
bool oil_Service_Refresh_Now(uint32_t nowUnix);

#endif
