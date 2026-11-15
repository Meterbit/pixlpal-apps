/**
 * @file mtb_cal_app.h
 * @brief The calendar app itself, shared by Google Calendar {2,0} and Outlook Calendar {2,1}: the app task, the knob,
 *        reminders with a chime, the phone's commands and the values the phone shows. Each app gives its provider,
 *        its look and its phone UI (which uses the command numbers below).
 *
 * Phone commands:
 *   0 (acknowledged; the old page's "link" request)   1 {"refreshToken"} link the account (the old page sends this too)
 *   2 (old "show events", acknowledged)   3 {"showTasks"}   4 (old "show holidays", acknowledged)   5 {"themeColor"} accent
 *   6 unlink   7 sync now   8 {"query"} search calendars, 248 "calendars"   9 {"calendars": [{name, value}]}
 *   10 {"query"} search task lists, 248 "taskList"   11 {"taskList": {name, value}}   12 {"view"} 0-3   13 {"days"} 1-30
 *   14 {"hour24"}   15 {"showDeclined"}   16 {"remind"} minutes, 0 off   17 {"chime"}   18 {"refresh"} minutes
 *   19 {"query"} search countries, 248 "holidays"   20 {"holidays": {name, value}} ("none" off)   21 {"showMail"}
 *   22 next view
 */
#ifndef MTB_CAL_APP_H
#define MTB_CAL_APP_H

#include "mtb_engine.h"
#include "mtb_cal_engine.h"

struct Mtb_Cal_Country_t { const char* code; const char* name; };

struct Mtb_Cal_App_Def_t {
    const char* brand;                      /**< "GOOGLE CALENDAR", on the panel */
    const char* settingsKey;                /**< NVS key of the settings (up to 15 characters) */
    uint16_t accent;                        /**< The default accent colour (RGB565) */
    const Mtb_Cal_Provider_t* provider;
    const Mtb_Cal_Country_t* countries;     /**< For public holidays, or nullptr */
    int countryCount;
};

/** @brief The app task's body: returns when the app closes. */
void mtb_Cal_App_Run(Mtb_Applications* app, const Mtb_Cal_App_Def_t& def);
/** @brief The values the phone shows (the BLE task), whether or not the app is open. */
void mtb_Cal_App_Values(const Mtb_Cal_App_Def_t& def, JsonObject values);

#endif
