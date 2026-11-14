/**
 * @file bcc_App.h
 * @brief Business & Creator Counters' parts talking to each other: the worker that asks the providers
 *        (bcc_Net.cpp), the sounds (bcc_Sound.cpp) and the app task (businessCounters.cpp).
 */
#ifndef BCC_APP_H
#define BCC_APP_H

#include "bcc_Model.h"

/** @brief Everything the providers said, copied out of the worker under its lock. */
struct Bcc_Status_t {
    Bcc_Sales_t shops[BCC_SHOPS];
    Bcc_Creator_t creator;
};

bool bcc_Net_Start(const Bcc_Settings_t& settings);
void bcc_Net_Stop(void);
void bcc_Net_Set_Settings(const Bcc_Settings_t& settings);
/** @brief Ask every source again now (the phone's "refresh"). */
void bcc_Net_Refresh(void);
/** @brief Copies the status if it changed since @p version (updated); false if not. */
bool bcc_Net_Copy(Bcc_Status_t& status, uint32_t& version);

void bcc_Sound_Sale(uint8_t volume, const char* speech);
void bcc_Sound_Milestone(uint8_t volume, const char* speech);
void bcc_Sound_Stop(void);

#endif
