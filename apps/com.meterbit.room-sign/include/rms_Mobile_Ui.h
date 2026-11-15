/**
 * @file rms_Mobile_Ui.h
 * @brief Room Sign's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (roomSign.cpp):
 *   0 {"room": "Boardroom"}           the room's name on the sign
 *   1 {"url": "https://....ics"}       the room's calendar (iCal link; webcal:// works); empty removes it
 *   2 {"minutes": 1-30}                how often the calendar is read
 *   3 {"titles": 0|1}                  show the meetings' titles (0: only "BUSY")
 *   4 {}                               read the calendar now
 */
#ifndef RMS_MOBILE_UI_H
#define RMS_MOBILE_UI_H

static const char RMS_UI_MANIFEST[] = R"json({
  "name": "Room Sign",
  "about": "Turns Pixlpal into a meeting-room sign: free or in use, until when, what is on now and next, from the room's calendar. Works with Google, Outlook and any calendar that shares an iCal (.ics) link.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char RMS_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.room-sign.png", "height": 64, "alt": "Room Sign" },
    { "type": "statusPanel", "title": "The Room", "source": "status" },
    { "type": "borderButton", "label": "READ THE CALENDAR NOW", "command": { "app_command": 4 } },
    {
      "type": "section",
      "id": "room",
      "title": "The Room and Its Calendar",
      "widgets": [
        { "type": "textField", "title": "Room Name", "emptyHint": "e.g. Boardroom", "filledHint": "Change the room's name", "command": { "app_command": 0, "args": ["room"] } },
        { "type": "textField", "title": "Calendar Link (iCal)", "emptyHint": "https://calendar.google.com/calendar/ical/.../basic.ics", "filledHint": "Change the calendar link (empty removes it)", "secret": true, "command": { "app_command": 1, "args": ["url"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Google Calendar: the room's calendar, Settings and sharing, Integrate calendar, Secret address in iCal format." },
          { "type": "bullet", "text": "Outlook: Settings, Calendar, Shared calendars, Publish a calendar, choose the room's calendar and Can view all details, then copy the ICS link." },
          { "type": "note", "text": "The link is kept on Pixlpal only. Anyone with it can read the calendar, so it isn't shown here again." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "The Sign",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Show the Meetings' Titles", "default": true, "command": { "app_command": 3, "args": ["titles"] } },
        { "type": "slider", "title": "Read the Calendar Every", "min": 1, "max": 30, "default": 5, "suffix": " min", "command": { "app_command": 2, "args": ["minutes"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "How It Reads",
      "blocks": [
        { "type": "bullet", "text": "Green: free. Red: in use. Amber: the meeting ends in five minutes, or the next one starts in five." },
        { "type": "bullet", "text": "On Pixlpal, turn the knob to see the rest of the week, click to read the calendar now." },
        { "type": "note", "text": "Times are in Pixlpal's time zone. Pixlpal reads the first 64 KB of the calendar: a calendar with years of past meetings may be cut short." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Room name saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write a name of up to 31 letters" },
    { "when": { "app_command": 1 }, "text": "Calendar saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "That is not a calendar link (https:// or webcal://)" },
    { "when": { "app_command": 4 }, "text": "Reading the calendar" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Open Room Sign on Pixlpal first, and give it the calendar link" }
  ]
})json";

#endif
