/**
 * @file cmt_App.h
 * @brief Commute's parts talking to each other: the network worker (cmt_Net.cpp) and the app task (commute.cpp).
 */
#ifndef CMT_APP_H
#define CMT_APP_H

#include "cmt_Model.h"

enum Cmt_Board_State_t : uint8_t { CMT_IDLE = 0, CMT_LOADING, CMT_OK, CMT_FAILED };

/** @brief A board's latest answer. */
struct Cmt_Result_t {
    uint8_t state;              /**< Cmt_Board_State_t */
    uint32_t fetchedAt;         /**< Unix */
    char error[72];
    // Departures.
    int count;
    Cmt_Departure_t departures[CMT_DEPARTURES];
    // A drive.
    Cmt_Drive_Result_t drive;
    uint8_t historyCount;
    uint16_t history[CMT_HISTORY];      /**< Minutes with traffic, oldest first */
};

struct Cmt_Status_t {
    Cmt_Usage_t usage;
    uint8_t transitSpent, routeSpent;   /**< The month's budget is used up (or the provider said so) */
    uint32_t nextTransit, nextRoute;    /**< Seconds between refreshes now */
};

bool cmt_Net_Start(const Cmt_Settings_t& s);
void cmt_Net_Stop(void);
void cmt_Net_Set_Settings(const Cmt_Settings_t& s);
/** @brief Fetch every board now (counts against the budgets). */
void cmt_Net_Refresh(void);
/** @brief A search for the phone: "stop", "lines" (of the board being edited), "address". */
bool cmt_Net_Search(const char* id, const char* query);
bool cmt_Net_Copy(Cmt_Result_t* results, Cmt_Status_t& status, uint32_t& version);
bool cmt_Net_Running(void);

#endif
