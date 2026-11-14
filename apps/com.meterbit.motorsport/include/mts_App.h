/**
 * @file mts_App.h
 * @brief Motorsport Live's worker (mts_Net.cpp), its screens (mts_Ui.cpp) and the app task (motorsport.cpp).
 */
#ifndef MTS_APP_H
#define MTS_APP_H

#include "mts_Model.h"

/** What the worker found, copied to the app. */
struct Mts_Data_t {
    uint8_t plan;                       /**< Mts_Plan_t */
    char planWhy[80];
    bool schedOk;
    Mts_Schedule_t sched;
    uint32_t weekend;                   /**< the meeting shown */
    Mts_Drivers_t drivers;
    Mts_Track_t track;
    Mts_Result_t result;                /**< the last finished session's */
    Mts_Standings_t standings;
    Mts_Live_t live;
    bool liveOk;                        /**< live timing is coming in */
    bool liveBlocked;                   /**< a session is live but the plan has no live data */
    char why[96];
    uint32_t calls;                     /**< requests today */
};

bool mts_Net_Start(const Mts_Settings_t& settings);
void mts_Net_Stop(void);
void mts_Net_Set_Settings(const Mts_Settings_t& settings);
void mts_Net_Refresh(void);
bool mts_Net_Copy(Mts_Data_t& data, uint32_t& version);

// Screens --------------------------------------------------------------------------------------------------------
struct Mts_View_t {
    uint8_t page;                       /**< Mts_Page_t */
    const Mts_Settings_t* s;
    const Mts_Data_t* d;
    uint32_t now;
    int32_t utcOffset;                  /**< the device's, for session times */
    uint8_t half;                       /**< live timing: 0 the first ten, 1 the rest */
    uint16_t tick;                      /**< for the dot running round the circuit */
    const char* message;
};
void mts_Draw(const Mts_View_t& v);
/** @brief The page has something to show. */
bool mts_Page_Ready(const Mts_Data_t& d, uint8_t page, uint32_t now);

#endif
