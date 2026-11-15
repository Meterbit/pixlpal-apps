/**
 * @file mtb_cal_model.h
 * @brief What the calendar apps (Google Calendar {2,0}, Outlook Calendar {2,1}) show, apart from where it comes from:
 *        events and tasks, the settings, times and labels, the agenda, the week and reminders. No device code: tested
 *        on a PC.
 *
 * Times are Unix seconds. All-day events start at local midnight of their first day and end at local midnight after
 * their last (the providers' exclusive end date). "Local" is the device's time zone (TZ, set from the phone).
 */
#ifndef MTB_CAL_MODEL_H
#define MTB_CAL_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define MTB_CAL_EVENTS      64
#define MTB_CAL_TASKS       32
#define MTB_CAL_PICKS       10      /**< Calendars the user can choose. */
#define MTB_CAL_ID          200     /**< Microsoft's calendar ids are about 150 characters. */
#define MTB_CAL_NAME        48
#define MTB_CAL_TITLE       80      /**< UTF-8, as the provider gives it. */
#define MTB_CAL_WHERE       48
#define MTB_CAL_ACCOUNT     64
#define MTB_CAL_SETTINGS_MAGIC  0x43414C31u   // "CAL1"

enum Mtb_Cal_View_t : uint8_t { MTB_CAL_VIEW_NEXT = 0, MTB_CAL_VIEW_AGENDA, MTB_CAL_VIEW_WEEK, MTB_CAL_VIEW_TASKS, MTB_CAL_VIEW_COUNT };

enum Mtb_Cal_Flag_t : uint8_t {
    MTB_CAL_TENTATIVE = 1,      /**< Answered "maybe", or not answered. */
    MTB_CAL_DECLINED  = 2,
    MTB_CAL_ONLINE    = 4,      /**< Has a meeting link. */
    MTB_CAL_FREE      = 8,      /**< Shown as free (not blocking time). */
    MTB_CAL_HOLIDAY   = 16,
    MTB_CAL_BIRTHDAY  = 32,
};

struct Mtb_Cal_Event_t {
    uint32_t start, end;
    uint32_t key;               /**< Title and start, hashed: the same event from two calendars, and reminders. */
    uint16_t color;             /**< RGB565, the calendar's colour. */
    uint8_t allDay;
    uint8_t flags;              /**< Mtb_Cal_Flag_t */
    char title[MTB_CAL_TITLE];
    char where[MTB_CAL_WHERE];
};

struct Mtb_Cal_Task_t {
    int32_t dueDay;             /**< Local days since 1970-01-01, or -1: no due date. */
    uint8_t important;
    char title[MTB_CAL_TITLE];
};

struct Mtb_Cal_Data_t {
    uint16_t eventCount, taskCount;
    int16_t unreadMail;         /**< -1 when not asked for. */
    uint8_t tasksAsked;         /**< Tasks were fetched (else none means "not shown", not "none due"). */
    uint32_t fetchedAt;         /**< Unix time of the last full sync. */
    char account[MTB_CAL_ACCOUNT];
    Mtb_Cal_Event_t events[MTB_CAL_EVENTS];
    Mtb_Cal_Task_t tasks[MTB_CAL_TASKS];
};

struct Mtb_Cal_Pick_t { char id[MTB_CAL_ID]; char name[MTB_CAL_NAME]; };

struct Mtb_Cal_Settings_t {
    uint32_t magic;
    uint8_t view;               /**< Mtb_Cal_View_t at start. */
    uint8_t daysAhead;          /**< 1-30 */
    uint8_t hour24;
    uint8_t showTasks;
    uint8_t showDeclined;
    uint8_t remindMinutes;      /**< 0 off, or minutes before an event. */
    uint8_t chime;
    uint8_t refreshMinutes;     /**< 5-60 */
    uint8_t showMail;           /**< Outlook: the unread count in the inbox. */
    uint8_t weekStart;          /**< 0 Monday, 1 Sunday (the week view starts today either way; for labels). */
    uint16_t accent;            /**< RGB565 */
    uint8_t calendarCount;      /**< 0: the calendars the account shows. */
    Mtb_Cal_Pick_t calendars[MTB_CAL_PICKS];
    Mtb_Cal_Pick_t taskList;    /**< id "" : the default list. */
    char holidays[4];           /**< Google: a country's public holidays ("ng"), "" none. */
    char holidayName[40];
};

void mtb_Cal_Settings_Default(Mtb_Cal_Settings_t& s, uint16_t accent);
void mtb_Cal_Settings_Clamp(Mtb_Cal_Settings_t& s);

// Time ----------------------------------------------------------------------------------------------------------
/** @brief Local days since 1970-01-01 of a Unix time. */
int32_t mtb_Cal_Local_Day(uint32_t t);
/** @brief Unix time of local midnight starting a local day. */
uint32_t mtb_Cal_Day_Start(int32_t day);
/**
 * @brief "2026-11-08" (an all-day date: local midnight), "2026-11-08T09:30:00+01:00" / "…Z", or Microsoft's
 *        "2026-11-08T09:30:00.0000000" (asked for in UTC). False if not a time.
 */
bool mtb_Cal_Parse_Time(const char* text, uint32_t& t, bool& dateOnly);
/** @brief "09:30" or "9:30 PM". */
void mtb_Cal_Clock(char* dest, size_t size, uint32_t t, bool hour24);
/** @brief "TODAY", "TOMORROW", "YESTERDAY", or "MON 9 NOV". */
void mtb_Cal_Day_Label(char* dest, size_t size, int32_t day, int32_t today);
/** @brief "1 H 25 MIN", "25 MIN", "2 D 3 H". */
void mtb_Cal_Span(char* dest, size_t size, uint32_t seconds);

// Events --------------------------------------------------------------------------------------------------------
uint32_t mtb_Cal_Key(const char* title, uint32_t start);
/** @brief Adds an event (keeping the list sorted by start, all-day first on their day); a duplicate key is dropped.
 *         When full, a later event gives way to an earlier one. False if it was not added. */
bool mtb_Cal_Add_Event(Mtb_Cal_Data_t& data, const Mtb_Cal_Event_t& event);
/** @brief Adds a task (due ones first, then by date, then undated in the order given). */
bool mtb_Cal_Add_Task(Mtb_Cal_Data_t& data, const Mtb_Cal_Task_t& task);
/** @brief The event is shown with these settings at @p now (not over, within the days ahead, not declined…). */
bool mtb_Cal_Shown(const Mtb_Cal_Event_t& e, const Mtb_Cal_Settings_t& s, uint32_t now);
/** @brief Indices of the events shown, in order. */
int mtb_Cal_Agenda(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now, uint8_t* order, int max);
/** @brief The timed event on now (latest started), else the next timed one, else the next all-day one; -1 if none. */
int mtb_Cal_Next(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now);
/**
 * @brief When @p e is, from @p now: "NOW · 20 MIN LEFT", "IN 25 MIN", "TODAY 14:00", "TOMORROW 9:30 AM",
 *        "MON 9 NOV 09:30", "TODAY · ALL DAY", "UNTIL FRI · ALL DAY".
 */
void mtb_Cal_When(char* dest, size_t size, const Mtb_Cal_Event_t& e, uint32_t now, bool hour24);
/** @brief "09:30 - 10:15" (or "ALL DAY", "ALL DAY · 3 DAYS"). */
void mtb_Cal_Range(char* dest, size_t size, const Mtb_Cal_Event_t& e, bool hour24);
/** @brief The fraction of a running event gone by, 0 if not running. */
float mtb_Cal_Progress(const Mtb_Cal_Event_t& e, uint32_t now);
/**
 * @brief The timed event whose reminder is due at @p now (starts within @p minutes, not yet started, not declined),
 *        not the one already reminded of (@p lastKey); -1 if none.
 */
int mtb_Cal_Reminder(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now, uint32_t lastKey);
/** @brief How many shown events fall on a local day. */
int mtb_Cal_Count_On(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, int32_t day, uint32_t now);

// Tasks ---------------------------------------------------------------------------------------------------------
/** @brief "OVERDUE 3 D", "TODAY", "TOMORROW", "FRI 13 NOV", or "" (no date). */
void mtb_Cal_Due_Label(char* dest, size_t size, const Mtb_Cal_Task_t& t, int32_t today);
int mtb_Cal_Overdue(const Mtb_Cal_Data_t& data, int32_t today);

// Colours -------------------------------------------------------------------------------------------------------
/** @brief A calendar's colour from "#RRGGBB" (too dark ones lifted to show on black), or the palette's @p index. */
uint16_t mtb_Cal_Color(const char* hex, int index);

#endif
