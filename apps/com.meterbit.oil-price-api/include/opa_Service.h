/**
 * @file opa_Service.h
 * @brief Data service of the Oil Price API app: what to fetch and when, so the plan's quota lasts; the fetch task;
 *        back-off; the flash cache; and the copy of the data the screen draws from.
 *
 * The service outlives the app: its data and any fetch in progress survive the app being closed, so reopening it
 * shows the data at once and never starts a second download.
 */
#ifndef OPA_SERVICE_H
#define OPA_SERVICE_H

#include "opa_Model.h"

#define OPA_SETTINGS_MAGIC  0x4F504153UL    // "OPAS"

/** @brief The user's settings. Kept in NVS by the app. */
struct Opa_Settings_t {
    uint32_t magic;
    uint8_t watchCount;
    uint8_t range;                      /**< Opa_Range_t of the chart. */
    uint8_t share;                      /**< Per cent of the quota this device may use. */
    uint8_t floorMin;                   /**< Fastest quote refresh, minutes. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    char watch[OPA_MAX_WATCH][OPA_CODE_LEN];
    char featured[OPA_CODE_LEN];        /**< The code in the large display and the chart. */
    char apiKey[OPA_KEY_LEN];
};

/** @brief What the screen needs to know about the downloads. */
struct Opa_Status_t {
    bool busy;
    bool demo;                  /**< No key: the keyless demo feed is used. */
    bool keyRejected;
    bool emailUnconfirmed;
    bool quotaUsed;             /**< The quota is used up until it resets. */
    bool rateLimited;
    bool failing;               /**< The last quote request failed for another reason. */
    bool lowMemory;
    uint32_t quoteInterval;     /**< Seconds between quote requests at the current pace. */
    uint32_t nextQuote;         /**< Unix time of the next quote request. */
    bool started;
    uint32_t quotesFetched;     /**< When the prices last arrived; 0 never. */
    Opa_Account_t account;      /**< Plan and quota, from the last answer's headers. */
    uint8_t access[OPA_F_COUNT];    /**< Opa_Access_State_t of each paid feature. */
};

void opa_Settings_Default(Opa_Settings_t& settings);
/** @brief Brings out-of-range values back into range, and drops unknown or repeated codes from the watchlist. */
void opa_Settings_Clamp(Opa_Settings_t& settings);

/** @brief Starts the service, or updates its settings if it is already running. The first call loads the cache. */
void opa_Service_Begin(const Opa_Settings_t& settings);
/** @brief Applies new settings: a new watchlist, chart or key is fetched at once. */
void opa_Service_Set_Settings(const Opa_Settings_t& settings);

/**
 * @brief Starts a download if one is due. Call often from the app task; it returns at once.
 * @param nowUnix  The current time, or 0 if the clock has not been set yet.
 */
void opa_Service_Poll(bool online, uint32_t nowUnix);

/** @brief Copies the data into @p out if it has changed since the copy whose version is @p version. */
bool opa_Service_Copy(Opa_Data_t& out, uint32_t& version);

void opa_Service_Get_Status(Opa_Status_t& status);

/** @brief Asks for prices now. Returns false if asked too recently, or the quota is used up. */
bool opa_Service_Refresh_Now(uint32_t nowUnix);

#endif
