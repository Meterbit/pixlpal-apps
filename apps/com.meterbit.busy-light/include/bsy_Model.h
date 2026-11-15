/**
 * @file bsy_Model.h
 * @brief Busy Light / On Air {2,2}: a sign for the door of a home office. It says FREE, IN A MEETING, ON A CALL, ON
 *        AIR, DO NOT DISTURB... from the calendar (Google, Microsoft), Microsoft Teams presence, working hours, or
 *        the phone. People at the door can knock with the knob; the reply from inside shows on the sign.
 *        No Arduino, so it can be tested on a PC.
 *
 * Sources (checked 11 November 2026):
 *  - Google Calendar and Outlook calendars through meterbit_cal_kit's providers, with the account linked in Busy
 *    Light, Google Calendar {2,0} or Outlook Calendar {2,1} (one link serves all three).
 *  - Microsoft Teams presence: Graph GET /me/presence {"availability": "Busy", "activity": "InACall", ...}, scope
 *    Presence.Read, work or school accounts only (personal Microsoft accounts have no presence: 4xx).
 */
#ifndef BSY_MODEL_H
#define BSY_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ntf_Notify.h"

#define BSY_NVS_KEY         "bsySet"
#define BSY_SETTINGS_MAGIC  0x42535901u
#define BSY_EVENTS          48
#define BSY_TITLE           64
#define BSY_TEXT            24          /**< a custom status, as shown */
#define BSY_PRESENCE_FRESH  180         /**< seconds a Teams answer counts */

enum Bsy_Status_t : uint8_t {
    BSY_AUTO = 0,                       /**< no manual status: decided by the sources */
    BSY_FREE, BSY_BUSY, BSY_MEETING, BSY_CALL, BSY_PRESENTING, BSY_ON_AIR, BSY_DND, BSY_FOCUS, BSY_AWAY, BSY_BRB,
    BSY_OFF_WORK, BSY_OFFLINE, BSY_CUSTOM, BSY_STATUSES
};
enum Bsy_From_t : uint8_t { BSY_FROM_NONE = 0, BSY_FROM_PHONE, BSY_FROM_TEAMS, BSY_FROM_CALENDAR, BSY_FROM_HOURS };
enum Bsy_Provider_t : uint8_t { BSY_GOOGLE = 0, BSY_MICROSOFT, BSY_PROVIDERS };
enum Bsy_Off_Hours_t : uint8_t { BSY_OFF_SHOW = 0, BSY_OFF_FREE, BSY_OFF_DIM };
enum Bsy_Reply_t : uint8_t { BSY_REPLY_COME_IN = 0, BSY_REPLY_MINUTE, BSY_REPLY_FIVE, BSY_REPLY_NOT_NOW, BSY_REPLIES };
/** Event flags, as meterbit_cal_kit's (MTB_CAL_TENTATIVE 1, DECLINED 2, ONLINE 4, FREE 8, HOLIDAY 16, BIRTHDAY 32). */
enum Bsy_Event_Flag_t : uint8_t { BSY_EV_TENTATIVE = 1, BSY_EV_DECLINED = 2, BSY_EV_ONLINE = 4, BSY_EV_FREE = 8, BSY_EV_HOLIDAY = 16, BSY_EV_BIRTHDAY = 32 };

struct Bsy_Settings_t {
    uint32_t magic;
    char label[BSY_TEXT];               /**< on the sign's top line: "Ada's office", or "" */
    uint8_t useCalendar[BSY_PROVIDERS]; /**< read this account's calendar (when linked) */
    uint8_t usePresence;                /**< read Microsoft Teams presence (when linked) */
    uint8_t allCalendars;               /**< 0 the account's own calendar, 1 every calendar shown in the provider's app */
    uint8_t tentativeBusy;              /**< meetings not yet accepted count as busy */
    uint8_t showTitle;                  /**< meeting titles on the sign (off: private) */
    uint8_t showNext;                   /**< when free, the next meeting's time */
    uint8_t hour24;
    uint8_t pulse;                      /**< ON AIR and PRESENTING glow */
    uint8_t workDays;                   /**< bit 0 Monday ... bit 6 Sunday; 0: no working hours */
    uint16_t workFrom, workTo;          /**< minutes after local midnight */
    uint8_t offHours;                   /**< Bsy_Off_Hours_t */
    uint8_t knock, knockSound, phoneKnock;
    uint8_t manualMinutes;              /**< how long a status set from the phone lasts: 0 until changed */
    // A status set from the phone.
    uint8_t manual;                     /**< Bsy_Status_t, BSY_AUTO for none */
    uint32_t manualUntil;               /**< Unix, 0: until changed */
    char customText[BSY_TEXT];
    uint16_t customColor;               /**< RGB565 */
    Ntf_Config_t notify;
    char account[BSY_PROVIDERS][64];    /**< the linked accounts' addresses, for the phone */
};

/** A meeting, from either calendar. */
struct Bsy_Event_t {
    uint32_t start, end;
    uint8_t flags;                      /**< Bsy_Event_Flag_t */
    uint8_t allDay;
    uint8_t provider;
    char title[BSY_TITLE];
};

struct Bsy_Calendar_t {
    bool known;
    uint32_t fetchedAt;
    int count;
    Bsy_Event_t e[BSY_EVENTS];          /**< sorted by start */
};

struct Bsy_Presence_t {
    bool known;
    uint32_t at;
    char availability[24];
    char activity[32];
};

/** What the sign says. */
struct Bsy_State_t {
    uint8_t status;                     /**< Bsy_Status_t */
    uint8_t from;                       /**< Bsy_From_t */
    uint32_t start, until;              /**< of a meeting or a phone status; 0 unknown */
    char title[BSY_TITLE];              /**< the meeting's */
    bool calendarKnown;                 /**< a calendar was read (so "no more meetings" is true) */
    bool hasNext;
    uint32_t nextStart;
    char nextTitle[BSY_TITLE];
};

void bsy_Settings_Default(Bsy_Settings_t& s);
void bsy_Settings_Clamp(Bsy_Settings_t& s);

/** @brief How much a status keeps people out: OFF WORK and OFFLINE 0, FREE 1, AWAY 2, BUSY 3, MEETING 4, ON AIR 5. */
int bsy_Severity(uint8_t status);
/** @brief "IN A MEETING", for the sign. */
const char* bsy_Status_Text(uint8_t status);
/** @brief "In a meeting", for the phone. */
const char* bsy_Status_Name(uint8_t status);
/** @brief The sign's colour (RGB565). */
uint16_t bsy_Status_Color(uint8_t status, uint16_t customColor);
/** @brief Teams' availability and activity as a status; BSY_AUTO when it says nothing useful (offline, unknown). */
uint8_t bsy_Presence_Status(const char* availability, const char* activity);
/** @brief Graph's answer to /me/presence: 1 read, 0 no presence for this account (personal ones), -1 failed. */
int bsy_Parse_Presence(const char* body, size_t length, int httpCode, uint32_t now, Bsy_Presence_t& out, char* why, size_t whySize);

/** @brief The event keeps you busy (timed, not free, not declined; tentative only if @p tentativeBusy). */
bool bsy_Event_Busy(const Bsy_Event_t& e, bool tentativeBusy);
/** @brief MEETING, or FOCUS / AWAY for Google's focus time and out-of-office blocks. */
uint8_t bsy_Event_Status(const Bsy_Event_t& e);
/** @brief Adds an event in start order (a duplicate of title and start is dropped). False when full. */
bool bsy_Add_Event(Bsy_Calendar_t& c, const Bsy_Event_t& e);
/** @brief Inside the working hours? @p weekday 0 Monday, @p minute after local midnight. True when none are set. */
bool bsy_Working(const Bsy_Settings_t& s, int weekday, int minute);

/**
 * @brief What the sign says at @p now: a phone status while it lasts; else the busier of Teams presence (when fresh)
 *        and the calendar (back-to-back meetings joined); else FREE in working hours and the off-hours choice outside.
 */
void bsy_Decide(const Bsy_Settings_t& s, uint32_t now, int weekday, int minute, const Bsy_Calendar_t* cal,
                const Bsy_Presence_t* presence, Bsy_State_t& out);

/** @brief "13:40" or "1:40 PM" at a UTC offset. */
void bsy_Clock(char* dest, size_t size, uint32_t t, int32_t utcOffset, bool hour24);
/** @brief "25 MIN", "1 H 10". */
void bsy_Span(char* dest, size_t size, uint32_t seconds);
/**
 * @brief The line under the status: "UNTIL 14:30, 25 MIN LEFT", "NEXT MEETING AT 15:00", "FREE FOR THE REST OF THE
 *        DAY", "BACK MONDAY AT 09:00". @p weekday (0 Monday) and @p minute are local now.
 */
void bsy_Detail(char* dest, size_t size, const Bsy_State_t& st, const Bsy_Settings_t& s, uint32_t now, int32_t utcOffset,
                int weekday, int minute);
/** @brief A reply to a knock: "COME IN"... */
const char* bsy_Reply_Text(uint8_t reply);
uint16_t bsy_Reply_Color(uint8_t reply);

#endif
