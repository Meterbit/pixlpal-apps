/**
 * @file pfx_Service.h
 * @brief FX Markets' data service: one request at a time, paced for the plan (Basic: one every 13 seconds, so 5 a
 *        minute), the plan check, and the copy the screens draw from. It outlives the app, so prices are kept while
 *        other apps show.
 */
#ifndef PFX_SERVICE_H
#define PFX_SERVICE_H

#include "pfx_Model.h"

struct Pfx_Status_t {
    bool started, busy, keyMissing, keyRejected, rateLimited, failing;
    uint32_t lastFetch;
};

void pfx_Service_Begin(const Pfx_Settings_t& settings);
void pfx_Service_Set_Settings(const Pfx_Settings_t& settings);
void pfx_Service_Poll(bool online, uint32_t now);
bool pfx_Service_Copy(Pfx_Data_t& out, uint32_t& version);
void pfx_Service_Get_Status(Pfx_Status_t& status);
/** @brief Reads every pair again now. False within two minutes of the last time. */
bool pfx_Service_Refresh(uint32_t now);

#endif
