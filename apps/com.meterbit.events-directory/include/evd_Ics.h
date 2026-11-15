/**
 * @file evd_Ics.h
 * @brief Events Directory's calendar reader: the events of an iCalendar file (RFC 5545) that fall in a window of time.
 *
 * Reads VEVENTs with DTSTART, DTEND or DURATION, SUMMARY, STATUS, TRANSP, RRULE (DAILY, WEEKLY with BYDAY, MONTHLY by
 * day of the month or "2TU"/"-1FR", YEARLY; INTERVAL, COUNT, UNTIL), EXDATE and RECURRENCE-ID (a changed or cancelled
 * occurrence). Cancelled and free ("TRANSP:TRANSPARENT") events are left out.
 *
 * Times: "...Z" is UTC. A time with a TZID, or none, is taken as the room's time zone (@p localOffset, Pixlpal's), as
 * a room's calendar is kept in the room's zone; a DATE (all day) runs from the room's midnight to the next.
 */
#ifndef EVD_ICS_H
#define EVD_ICS_H

#include <stdint.h>
#include <stddef.h>

#define EVD_TITLE_LEN 48
#define EVD_PLACE_LEN 32

struct Evd_Event_t {
    int64_t start, end;                 /**< Unix seconds. */
    bool allDay;
    char title[EVD_TITLE_LEN];          /**< As in the calendar (UTF-8), "" if the event has none. */
    char place[EVD_PLACE_LEN];          /**< Its LOCATION, "" if none. */
};

/** What the reader saw (for the owner, when nothing shows). */
struct Evd_Ics_Report_t {
    int vevents;                        /**< VEVENT blocks read. */
    bool calendar;                      /**< A BEGIN:VCALENDAR was there. */
};

/**
 * @brief The events of @p text overlapping [@p from, @p to), sorted by start, at most @p max (the earliest).
 * @return How many were written to @p out.
 */
int evd_Ics_Events(const char* text, size_t length, int64_t from, int64_t to, int32_t localOffset,
                   Evd_Event_t* out, int max, Evd_Ics_Report_t* report = nullptr);

#endif
