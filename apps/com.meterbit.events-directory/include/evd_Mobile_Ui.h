/**
 * @file evd_Mobile_Ui.h
 * @brief Events Directory's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (eventsDirectory.cpp):
 *   0 {"url0" | "url1": "https://....ics"}     an events calendar (webcal:// works); empty removes it
 *   1 {"event0" ... "event7": "18:00-23:00 Okafor Wedding @ Ballroom"}   an event typed here: today, or after "tomorrow"
 *                                                                        or a date (2026-11-20); empty removes it
 *   2 {"dir0" ... "dir7": "Ballroom right"}   the way to a room: its name and left, right, ahead, back, upstairs, downstairs
 *   3 {"venue": "Lagoon Hotel"}    4 {"minutes": 1-30}    5 {}  read the calendars now    6 {"titles": 0|1}
 */
#ifndef EVD_MOBILE_UI_H
#define EVD_MOBILE_UI_H

static const char EVD_UI_MANIFEST[] = R"json({
  "name": "Events Directory",
  "about": "Today's events, conferences and weddings for a hotel or venue lobby: each with its time, room and an arrow showing the way, what's on now marked. From the events calendar (an iCal link) or events typed on the phone.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char EVD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.events-directory.png", "height": 64, "alt": "Events Directory" },
    { "type": "statusPanel", "title": "Today", "source": "today", "emptyText": "No events today." },
    {
      "type": "section",
      "id": "events",
      "title": "Events Typed Here",
      "widgets": [
        { "type": "textField", "title": "Event 1", "emptyHint": "18:00-23:00 Okafor Wedding @ Ballroom", "filledHint": "Change event 1 (empty removes it)", "command": { "app_command": 1, "args": ["event0"] } },
        { "type": "textField", "title": "Event 2", "emptyHint": "09:00-17:00 Bank Leadership Summit @ Room 2", "filledHint": "Change event 2", "command": { "app_command": 1, "args": ["event1"] } },
        { "type": "textField", "title": "Event 3", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 3", "command": { "app_command": 1, "args": ["event2"] } },
        { "type": "textField", "title": "Event 4", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 4", "command": { "app_command": 1, "args": ["event3"] } },
        { "type": "textField", "title": "Event 5", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 5", "command": { "app_command": 1, "args": ["event4"] } },
        { "type": "textField", "title": "Event 6", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 6", "command": { "app_command": 1, "args": ["event5"] } },
        { "type": "textField", "title": "Event 7", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 7", "command": { "app_command": 1, "args": ["event6"] } },
        { "type": "textField", "title": "Event 8", "emptyHint": "Start-end Name @ Room", "filledHint": "Change event 8", "command": { "app_command": 1, "args": ["event7"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "An event is for today. Start it with tomorrow, or a date such as 2026-11-20, for another day." } ] }
      ]
    },
    {
      "type": "section",
      "id": "ways",
      "title": "The Way to Each Room",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Room 1", "emptyHint": "Ballroom right", "filledHint": "Change room 1 (empty removes it)", "command": { "app_command": 2, "args": ["dir0"] } },
        { "type": "textField", "title": "Room 2", "emptyHint": "Room 2 upstairs", "filledHint": "Change room 2", "command": { "app_command": 2, "args": ["dir1"] } },
        { "type": "textField", "title": "Room 3", "emptyHint": "Garden ahead", "filledHint": "Change room 3", "command": { "app_command": 2, "args": ["dir2"] } },
        { "type": "textField", "title": "Room 4", "emptyHint": "Room and way", "filledHint": "Change room 4", "command": { "app_command": 2, "args": ["dir3"] } },
        { "type": "textField", "title": "Room 5", "emptyHint": "Room and way", "filledHint": "Change room 5", "command": { "app_command": 2, "args": ["dir4"] } },
        { "type": "textField", "title": "Room 6", "emptyHint": "Room and way", "filledHint": "Change room 6", "command": { "app_command": 2, "args": ["dir5"] } },
        { "type": "textField", "title": "Room 7", "emptyHint": "Room and way", "filledHint": "Change room 7", "command": { "app_command": 2, "args": ["dir6"] } },
        { "type": "textField", "title": "Room 8", "emptyHint": "Room and way", "filledHint": "Change room 8", "command": { "app_command": 2, "args": ["dir7"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The room's name as events give it, then the way from where Pixlpal stands: left, right, ahead, back, upstairs or downstairs. An event's room matches when it starts with that name." } ] }
      ]
    },
    {
      "type": "section",
      "id": "calendars",
      "title": "Events Calendars",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Calendar 1 (iCal)", "emptyHint": "https://calendar.google.com/calendar/ical/.../basic.ics", "filledHint": "Change calendar 1 (empty removes it)", "secret": true, "command": { "app_command": 0, "args": ["url0"] } },
        { "type": "textField", "title": "Calendar 2 (iCal)", "emptyHint": "Another calendar, such as a second venue's", "filledHint": "Change calendar 2", "secret": true, "command": { "app_command": 0, "args": ["url1"] } },
        { "type": "borderButton", "label": "READ THE CALENDARS NOW", "command": { "app_command": 5 } },
        { "type": "slider", "title": "Read Them Every", "min": 1, "max": 30, "default": 10, "suffix": " min", "command": { "app_command": 4, "args": ["minutes"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "An event's room comes from its location. Pixlpal reads the first 64 KB of each calendar." } ] }
      ]
    },
    {
      "type": "section",
      "id": "sign",
      "title": "The Sign",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Venue Name", "emptyHint": "e.g. Lagoon Hotel", "filledHint": "Change the venue's name", "command": { "app_command": 3, "args": ["venue"] } },
        { "type": "switch", "title": "Show the Events' Names", "default": true, "command": { "app_command": 6, "args": ["titles"] } }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Event saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write it as 18:00-23:00 Name @ Room" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Write the room, then left, right, ahead, back, upstairs or downstairs" },
    { "when": { "app_command": 0 }, "text": "Calendar saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is not a calendar link (https:// or webcal://)" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Events Directory on Pixlpal first, and give it a calendar" }
  ]
})json";

#endif
