/**
 * @file cfx_Service.h
 * @brief Currency Exchange's data service: what to fetch and when, the fetch task, the flash cache, and the copy
 *        of the data the screens draw from. It outlives the app.
 *
 * Rates are fetched only after the provider's next update time, so a Free key spends about two requests a day
 * (the rates and the quota), Pro about thirty-five, and the keyless feed one.
 */
#ifndef CFX_SERVICE_H
#define CFX_SERVICE_H

#include "cfx_Model.h"

#define CFX_SETTINGS_MAGIC  0x43465853UL    // "CFXS"
#define CFX_NVS_KEY         "currencyCfg"

struct Cfx_Settings_t {
    uint32_t magic;
    char base[4];
    uint8_t watchCount;
    char watch[CFX_MAX_WATCH][4];
    uint8_t featured;               /**< Index in watch of the big pair. */
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t inverse;                /**< The big pair as 1 target = x base. */
    double amount;                  /**< The converter's amount, in the base currency. */
    char apiKey[CFX_KEY_LEN];
};

struct Cfx_Status_t {
    bool started;
    bool busy;
    bool keyless;
    bool keyRejected;
    bool inactive;                  /**< The account's email is not confirmed. */
    bool quotaUsed;
    bool rateLimited;
    bool failing;
    uint32_t nextFetch;             /**< Unix time of the next rates request. */
};

void cfx_Settings_Default(Cfx_Settings_t& settings);
/** @brief Codes made valid, repeats and the base out of the watchlist, values into range. */
void cfx_Settings_Clamp(Cfx_Settings_t& settings);

void cfx_Service_Begin(const Cfx_Settings_t& settings);
void cfx_Service_Set_Settings(const Cfx_Settings_t& settings);
void cfx_Service_Poll(bool online, uint32_t nowUnix);
bool cfx_Service_Copy(Cfx_Data_t& out, uint32_t& version);
void cfx_Service_Get_Status(Cfx_Status_t& status);
/** @brief The plan, quota and update times only (for the phone, without copying the rate table). */
void cfx_Service_Get_Plan(uint8_t& plan, Cfx_Quota_t& quota, uint32_t& lastUpdate, uint32_t& nextUpdate, uint8_t& historyLocked);
/** @brief Fetches the rates now. False if asked within two minutes, or with nothing new to fetch. */
bool cfx_Service_Refresh_Now(uint32_t nowUnix);

#endif
