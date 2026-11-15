/**
 * @file fth_Worker.h
 * @brief The verse-of-the-day worker of the Bible-based FAITH apps (Daily Bible, Catholic Daily): one task that reads
 *        the day's verse from bible-api.com (kept in NVS, so a day offline still shows it), retries with a growing
 *        pause, and runs an app's own daily job (Catholic Daily: the liturgical day). One FAITH app runs at a time.
 */
#ifndef FTH_WORKER_H
#define FTH_WORKER_H

#include "fth_Kit.h"

struct Fth_Worker_Config_t {
    bool verse;                                         /**< Read a verse each day */
    const char* (*reference)(uint32_t day, uint8_t pick);   /**< The day's reference */
    char translation[16];                               /**< bible-api.com's id ("web", "dra") */
    uint8_t edition;                                    /**< Its index in the app's list (kept with the verse) */
    char nvsKey[16];                                    /**< Where the verse is kept */
    /** @brief The app's daily job, on the worker (for the day @p day, days since 1970). True when done for the day. */
    bool (*daily)(uint32_t day, int y, int m, int d);
};

bool fth_Worker_Start(const Fth_Worker_Config_t& config);
void fth_Worker_Stop(void);
void fth_Worker_Configure(const Fth_Worker_Config_t& config);
/** @brief Read the verse (and the daily job) again now. */
void fth_Worker_Refresh(void);
/** @brief Another verse today. */
void fth_Worker_Another(void);
/** @brief The verse and its error, if changed since @p version (the daily job's news counts as a change). */
bool fth_Worker_Copy(Fth_Verse_t* verse, char* error, size_t errorSize, uint32_t& version);
/** @brief From the daily job: something new to show. */
void fth_Worker_Changed(void);

#endif
