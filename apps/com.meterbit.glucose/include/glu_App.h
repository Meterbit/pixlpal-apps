/**
 * @file glu_App.h
 * @brief Glucose Display's worker (glu_Net.cpp), its screens (glu_Ui.cpp) and the app task (glucoseDisplay.cpp).
 */
#ifndef GLU_APP_H
#define GLU_APP_H

#include "glu_Model.h"

enum Glu_Net_State_t : uint8_t {
    GLU_NET_NOT_SET = 0,                /**< the source's account or site is not set up */
    GLU_NET_CONNECTING,
    GLU_NET_OK,
    GLU_NET_AUTH,                       /**< the login was refused: fix it on the phone (not tried again until then) */
    GLU_NET_LOCKED,                     /**< too many logins: the service locked it for a while */
    GLU_NET_TERMS,                      /**< LibreLinkUp: new terms to accept in its app */
    GLU_NET_FAILED,                     /**< the last try failed for a passing reason */
};

/** What the worker found, copied to the app. */
struct Glu_Data_t {
    Glu_History_t hist;
    Glu_Site_t site;
    Glu_Extra_t extra;
    uint8_t state;                      /**< Glu_Net_State_t */
    char error[96];
    uint32_t triedAt, okAt, nextAt;
    uint8_t people;                     /**< LibreLinkUp: the people followed */
    Glu_Libre_Patient_t person[GLU_PATIENTS];
    char region[8];                     /**< LibreLinkUp's, found at login */
};

bool glu_Net_Start(const Glu_Settings_t& settings);
void glu_Net_Stop(void);
void glu_Net_Set_Settings(const Glu_Settings_t& settings);
/** @brief Ask again now (and log in again if the login had been refused). */
void glu_Net_Refresh(void);
bool glu_Net_Copy(Glu_Data_t& data, uint32_t& version);

// Screens --------------------------------------------------------------------------------------------------------
enum Glu_Screen_t : uint8_t { GLU_SCREEN_NOW = 0, GLU_SCREEN_GRAPH, GLU_SCREEN_STATS, GLU_SCREENS };

struct Glu_View_t {
    uint8_t screen;                     /**< Glu_Screen_t */
    const Glu_Settings_t* s;
    const Glu_Data_t* d;
    uint32_t now;
    int32_t utcOffset;
    uint8_t alarm;                      /**< Glu_Alarm_t sounding, NONE */
    bool flash;                         /**< the alarm's blink phase */
    bool snoozed;                       /**< an alarm is held back by a snooze */
    bool dim;                           /**< the quiet hours' night screen */
    uint8_t graphHours;
    const char* message;                /**< instead of the screen, when set */
};
void glu_Draw(const Glu_View_t& v);

#endif
