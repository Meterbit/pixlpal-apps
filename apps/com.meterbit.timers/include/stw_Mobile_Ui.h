/**
 * @file stw_Mobile_Ui.h
 * @brief Mobile UI of the Timers app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in realStopwatch.cpp:
 *   0 {"mode"}   1 {} stopwatch start/stop   2 {} lap   3 {} stopwatch reset   4 {"timer"} / {"timerMinutes"}
 *   5 {} timer start/pause   6 {} timer reset   7 {"preset"} / {"work"} / {"rest"} / {"rounds"}
 *   8 {} intervals start/pause   9 {} intervals reset   10 {"hundredths"} / {"flash"} / {"theme"}
 * The argument names are also the keys of the values the app reports, with the status panel's swText, swState,
 * laps, timerText, timerState, ivText, ivState, and running, timerRunning, ivRunning.
 */
#ifndef STW_MOBILE_UI_H
#define STW_MOBILE_UI_H

static const char STW_UI_MANIFEST[] = R"json({
  "appID": "0/4",
  "name": "Timers",
  "about": "A stopwatch with laps, a countdown timer, and an interval timer for Tabata, HIIT, EMOM or Pomodoro. Work them from Pixlpal's knob or from here; they keep running while other apps show.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char STW_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Now",
      "emptyText": "Launch Timers on Pixlpal.",
      "rows": [
        { "label": "Stopwatch", "value": "{swText}", "state": "{swState}" },
        { "label": "Laps", "value": "{laps}" },
        { "label": "Timer", "value": "{timerText}", "state": "{timerState}" },
        { "label": "Intervals", "value": "{ivText}", "state": "{ivState}" }
      ]
    },
    {
      "type": "radioGroup",
      "title": "Show on Pixlpal",
      "default": 0,
      "options": [
        { "label": "Stopwatch", "value": 0 },
        { "label": "Timer", "value": 1 },
        { "label": "Intervals", "value": 2 }
      ],
      "command": { "app_command": 0, "args": ["mode"] }
    },
    {
      "type": "section",
      "id": "stopwatch",
      "title": "Stopwatch",
      "visibleWhen": { "arg": "mode", "equals": 0 },
      "widgets": [
        { "type": "borderButton", "label": "START / STOP", "command": { "app_command": 1 } },
        { "type": "borderButton", "label": "LAP", "command": { "app_command": 2 }, "visibleWhen": { "arg": "running", "equals": 1 } },
        { "type": "borderButton", "label": "RESET", "command": { "app_command": 3 }, "confirm": { "title": "Reset the stopwatch?", "text": "The time and the laps are cleared.", "ok": "Reset" } }
      ]
    },
    {
      "type": "section",
      "id": "timer",
      "title": "Timer",
      "visibleWhen": { "arg": "mode", "equals": 1 },
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Length",
          "default": 300,
          "options": [
            { "label": "1 minute", "value": 60 },
            { "label": "3 minutes (tea)", "value": 180 },
            { "label": "5 minutes", "value": 300 },
            { "label": "10 minutes", "value": 600 },
            { "label": "15 minutes", "value": 900 },
            { "label": "30 minutes", "value": 1800 },
            { "label": "1 hour", "value": 3600 }
          ],
          "command": { "app_command": 4, "args": ["timer"] }
        },
        {
          "type": "slider",
          "title": "Or Your Own Length",
          "min": 1,
          "max": 180,
          "default": 5,
          "suffix": "Mins",
          "command": { "app_command": 4, "args": ["timerMinutes"] }
        },
        { "type": "borderButton", "label": "START / PAUSE", "command": { "app_command": 5 } },
        { "type": "borderButton", "label": "RESET", "command": { "app_command": 6 } },
        {
          "type": "switch",
          "title": "Flash the Panel at Zero",
          "default": true,
          "command": { "app_command": 10, "args": ["flash"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "intervals",
      "title": "Intervals",
      "visibleWhen": { "arg": "mode", "equals": 2 },
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Workout",
          "default": 0,
          "options": [
            { "label": "Tabata: 20 s work, 10 s rest, 8 rounds", "value": 0 },
            { "label": "HIIT: 40 s work, 20 s rest, 10 rounds", "value": 1 },
            { "label": "EMOM: every minute for 10 minutes", "value": 2 },
            { "label": "Pomodoro: 25 min focus, 5 min break, 4 rounds", "value": 3 },
            { "label": "Plank: 1 min hold, 30 s rest, 3 rounds", "value": 4 }
          ],
          "command": { "app_command": 7, "args": ["preset"] }
        },
        {
          "type": "slider",
          "title": "Work",
          "min": 5,
          "max": 600,
          "step": 5,
          "default": 20,
          "suffix": "Secs",
          "command": { "app_command": 7, "args": ["work"] }
        },
        {
          "type": "slider",
          "title": "Rest",
          "min": 0,
          "max": 300,
          "step": 5,
          "default": 10,
          "suffix": "Secs",
          "command": { "app_command": 7, "args": ["rest"] }
        },
        {
          "type": "slider",
          "title": "Rounds",
          "min": 1,
          "max": 50,
          "default": 8,
          "suffix": "",
          "command": { "app_command": 7, "args": ["rounds"] }
        },
        { "type": "borderButton", "label": "START / PAUSE", "command": { "app_command": 8 } },
        { "type": "borderButton", "label": "RESET", "command": { "app_command": 9 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "Each workout starts with 5 seconds to get ready. The last three seconds of every phase turn gold, and the band flashes as a new phase begins." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "display",
      "title": "Display",
      "collapsed": true,
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Digits",
          "default": 0,
          "options": [
            { "label": "Cyan", "value": 0 },
            { "label": "Green", "value": 1 },
            { "label": "Amber", "value": 2 },
            { "label": "White", "value": 3 },
            { "label": "Red", "value": 4 }
          ],
          "command": { "app_command": 10, "args": ["theme"] }
        },
        {
          "type": "switch",
          "title": "Stopwatch Hundredths",
          "default": true,
          "command": { "app_command": 10, "args": ["hundredths"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: click to start and stop; double click for a lap (stopwatch) or to reset; long press for the next timer; turn the knob to set the timer's length or the rounds before you start." }
          ]
        }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Shown on Pixlpal" },
    { "when": { "app_command": 2 }, "text": "Lap" },
    { "when": { "app_command": 3 }, "text": "Stopwatch reset" },
    { "when": { "app_command": 4 }, "text": "Timer length set" },
    { "when": { "app_command": 6 }, "text": "Timer reset" },
    { "when": { "app_command": 7 }, "text": "Workout set" },
    { "when": { "app_command": 9 }, "text": "Intervals reset" },
    { "when": { "app_command": 10 }, "text": "Saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Start the stopwatch first" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Reset the timer before changing its length" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Reset the workout before changing it" }
  ]
})json";

#endif
