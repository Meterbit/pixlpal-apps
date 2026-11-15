/**
 * @file bel_Mobile_Ui.h
 * @brief Bell Schedule's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (bellSchedule.cpp):
 *   0 {"day0" ... "day6": "08:00-08:45 Maths; 08:45-09:30 English"}   a weekday's timetable (0 Monday); empty: no school
 *   1 {}                 copy Monday's timetable to Tuesday to Friday
 *   2 {"chime": 0|1}     a chime at each bell
 *   3 {"off": 0|1}       no school today (a holiday, a closure): the sign shows the next school day
 */
#ifndef BEL_MOBILE_UI_H
#define BEL_MOBILE_UI_H

static const char BEL_UI_MANIFEST[] = R"json({
  "name": "Bell Schedule",
  "about": "The school day at a glance: the lesson now with the time left, what comes next, and how long until break, lunch or home time. A timetable for each weekday, with a chime at each bell.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BEL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.bell-schedule.png", "height": 64, "alt": "Bell Schedule" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    {
      "type": "section",
      "id": "week",
      "title": "The Timetable",
      "widgets": [
        { "type": "textField", "title": "Monday", "emptyHint": "08:00-08:45 Maths; 08:45-09:30 English; 10:15-10:35 Break", "filledHint": "Change Monday", "command": { "app_command": 0, "args": ["day0"] } },
        { "type": "borderButton", "label": "COPY MONDAY TO TUESDAY-FRIDAY", "confirm": { "title": "Copy Monday?", "text": "Tuesday to Friday get Monday's timetable.", "ok": "Copy", "cancel": "Cancel" }, "command": { "app_command": 1 } },
        { "type": "textField", "title": "Tuesday", "emptyHint": "Lessons, separated by ;", "filledHint": "Change Tuesday", "command": { "app_command": 0, "args": ["day1"] } },
        { "type": "textField", "title": "Wednesday", "emptyHint": "Lessons, separated by ;", "filledHint": "Change Wednesday", "command": { "app_command": 0, "args": ["day2"] } },
        { "type": "textField", "title": "Thursday", "emptyHint": "Lessons, separated by ;", "filledHint": "Change Thursday", "command": { "app_command": 0, "args": ["day3"] } },
        { "type": "textField", "title": "Friday", "emptyHint": "Lessons, separated by ;", "filledHint": "Change Friday", "command": { "app_command": 0, "args": ["day4"] } },
        { "type": "textField", "title": "Saturday", "emptyHint": "Empty: no school", "filledHint": "Change Saturday", "command": { "app_command": 0, "args": ["day5"] } },
        { "type": "textField", "title": "Sunday", "emptyHint": "Empty: no school", "filledHint": "Change Sunday", "command": { "app_command": 0, "args": ["day6"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Each lesson: its start and end, then its name. Separate lessons with a semicolon: 08:00-08:45 Maths; 08:45-09:30 English." },
          { "type": "bullet", "text": "Names with Break, Lunch, Recess or Snack count as breaks: they show in green, and the sign counts down to them." },
          { "type": "note", "text": "Up to 14 lessons a day." }
        ] }
      ]
    },
    { "type": "switch", "title": "No School Today", "default": false, "command": { "app_command": 3, "args": ["off"] } },
    { "type": "switch", "title": "Chime at Each Bell", "default": true, "command": { "app_command": 2, "args": ["chime"] } },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The bar along the bottom is the whole day: lessons in blue, breaks in green, the white line now." },
        { "type": "bullet", "text": "Turn the knob to see the timetable of each day, click to come back to now." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Timetable saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write each lesson as 08:00-08:45 Maths, separated by ;" },
    { "when": { "app_command": 1 }, "text": "Monday copied to Tuesday-Friday" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write Monday's timetable first" }
  ]
})json";

#endif
