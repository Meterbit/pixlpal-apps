/**
 * @file bsy_App.h
 * @brief Busy Light's worker (bsy_Net.cpp), its screens (bsy_Ui.cpp) and the app task (busyLight.cpp).
 */
#ifndef BSY_APP_H
#define BSY_APP_H

#include "bsy_Model.h"

struct Bsy_Link_t {
    uint8_t state;                      /**< Mtb_Kit_OAuth_State_t */
    uint8_t calendars;                  /**< calendars read */
    char account[64];
    char error[96];
};

/** What the worker found, copied to the app. */
struct Bsy_Data_t {
    Bsy_Calendar_t cal;
    Bsy_Presence_t presence;
    int8_t presenceOk;                  /**< 1 read, 0 not available for this account, -1 failed, -2 not asked */
    char presenceWhy[80];
    Bsy_Link_t link[BSY_PROVIDERS];
    uint32_t syncedAt;
};

bool bsy_Net_Start(const Bsy_Settings_t& settings);
void bsy_Net_Stop(void);
void bsy_Net_Set_Settings(const Bsy_Settings_t& settings);
/** @brief Read the calendars and Teams again now (a new link, the phone's button). */
void bsy_Net_Sync(void);
bool bsy_Net_Copy(Bsy_Data_t& data, uint32_t& version);

// Screens --------------------------------------------------------------------------------------------------------
enum Bsy_Screen_t : uint8_t { BSY_SCREEN_SIGN = 0, BSY_SCREEN_KNOCKED, BSY_SCREEN_REPLY, BSY_SCREEN_TODAY };

struct Bsy_View_t {
    uint8_t screen;                     /**< Bsy_Screen_t */
    const Bsy_Settings_t* s;
    const Bsy_State_t* st;
    const Bsy_Calendar_t* cal;          /**< for the day's list */
    uint32_t now;
    int32_t utcOffset;
    int weekday, minute;                /**< local now */
    uint8_t glow;                       /**< 0-255, the ON AIR pulse */
    uint8_t reply;                      /**< Bsy_Reply_t, or BSY_REPLIES for replyText */
    const char* replyText;
    uint32_t knockedAt;
    const char* message;                /**< instead of the sign, when set */
};
void bsy_Draw(const Bsy_View_t& v);

#endif
