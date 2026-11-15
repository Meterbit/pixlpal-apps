/**
 * @file sth_Mobile_Ui.h
 * @brief Stand-up Host's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (standupHost.cpp):
 *   0 {"names": "Ada, Ben, Chi"}   the team, up to 12, separated by commas
 *   1 {"seconds": 30-300}           each person's time
 *   2 {"warn": 5-60}                the warning, seconds before the end of a turn
 *   3 {"sound": 0|1}                4 {"shuffle": 0|1}   a new order for every stand-up
 *   5 {}  start, or the next person    6 {}  skip the person speaking (away today)    7 {}  end the stand-up
 */
#ifndef STH_MOBILE_UI_H
#define STH_MOBILE_UI_H

static const char STH_UI_MANIFEST[] = R"json({
  "name": "Stand-up Host",
  "about": "Runs the team's daily stand-up: it shuffles who speaks when, gives each person the same time with a warning near the end, and moves on at a click. At the end it shows how long the meeting took and who went over.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char STH_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.standup-host.png", "height": 64, "alt": "Stand-up Host" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    { "type": "borderButton", "label": "START / NEXT PERSON", "command": { "app_command": 5 } },
    { "type": "borderButton", "label": "SKIP: AWAY TODAY", "command": { "app_command": 6 } },
    { "type": "borderButton", "label": "END THE STAND-UP", "confirm": { "title": "End the stand-up?", "text": "Pixlpal shows how long it took and who went over.", "ok": "End", "cancel": "Go on" }, "command": { "app_command": 7 } },
    { "type": "statusPanel", "title": "Speaking Order", "source": "order", "emptyText": "No team yet. Write the names below." },
    {
      "type": "section",
      "id": "team",
      "title": "The Team",
      "widgets": [
        { "type": "textField", "title": "Names, Separated by Commas", "emptyHint": "Ada, Ben, Chi, Dayo", "filledHint": "Change the team (up to 12)", "command": { "app_command": 0, "args": ["names"] } },
        { "type": "switch", "title": "A New Order for Every Stand-up", "default": true, "command": { "app_command": 4, "args": ["shuffle"] } }
      ]
    },
    {
      "type": "section",
      "id": "timing",
      "title": "Timing",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Each Person Speaks For", "min": 30, "max": 300, "step": 15, "default": 120, "suffix": " s", "command": { "app_command": 1, "args": ["seconds"] } },
        { "type": "slider", "title": "Warn Before the End", "min": 5, "max": 60, "step": 5, "default": 15, "suffix": " s", "command": { "app_command": 2, "args": ["warn"] } },
        { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 3, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click to start, and again for the next person. Before the start, turn the knob for a new order." },
        { "type": "bullet", "text": "While someone speaks, turn the knob to give them 15 seconds more or less. Double click in their first 10 seconds to mark someone away today." },
        { "type": "bullet", "text": "Hold the knob to end the stand-up early. The summary shows each person's time; those who went over are in red." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Team saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write between 1 and 12 names, separated by commas" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Stand-up Host on Pixlpal first, and write the team's names" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Nobody is speaking" },
    { "when": { "app_command": 7, "response": 0 }, "text": "No stand-up is running" }
  ]
})json";

#endif
