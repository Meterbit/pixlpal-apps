/**
 * @file bgc_Mobile_Ui.h
 * @brief Mobile UI of the Big Clock app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in mtb_bigClock.cpp:
 *   0 {"face": 0-3}                 segment, bold, analog, words
 *   1 {"theme": 0-6}                classic, amber, ice, matrix, sunset, rainbow, custom
 *   2 {"color": "0xFFRRGGBB"}       the custom colour (selects the custom theme)
 *   3 {"hour24"}  4 {"seconds"}  5 {"date"}     switches
 *   6 {"autoFace": 0-60}            minutes per face when faces rotate; 0 off
 *   7 {"night"}                     night mode on or off
 *   8 {"nightFrom", "nightTo"}      night hours, "HH:MM"
 *   9 {"nightStyle": 0-1}           deep red, or the theme dimmed
 *  10 {}                            set the clock from the network now
 *  11 {"show": 0-2}                 the clock and date, the clock alone, the calendar alone
 * The argument names are also the keys of the values the app reports, with the status panel's clockText,
 * clockState, nightText, nightState and zone. Success toasts come before failure toasts.
 */
#ifndef BGC_MOBILE_UI_H
#define BGC_MOBILE_UI_H

static const char BGC_UI_MANIFEST[] = R"json({
  "appID": "0/3",
  "name": "Big Clock",
  "about": "The time across the whole display: giant segments, bold type, a dial or words, in seven colour themes, with a night mode for the bedside.",
  "author": "Meterbit Cyb",
  "version": "v2.1.0"
})json";

static const char BGC_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Clock",
      "rows": [
        { "label": "Time", "value": "{clockText}", "state": "{clockState}" },
        { "label": "Time zone", "value": "{zone}" },
        { "label": "Night mode", "value": "{nightText}", "state": "{nightState}" }
      ]
    },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 0,
      "options": [
        { "label": "Clock and date", "value": 0 },
        { "label": "Clock only, as large as it goes", "value": 1 },
        { "label": "Calendar only, as large as it goes", "value": 2 }
      ],
      "command": { "app_command": 11, "args": ["show"] }
    },
    {
      "type": "richTextBlock",
      "blocks": [ { "type": "note", "text": "Two Pixlpals side by side: set one to the clock and the other to the calendar, to read both from across the room." } ],
      "visibleWhen": { "arg": "show", "notEquals": 0 }
    },
    {
      "type": "radioGroup",
      "title": "Face",
      "default": 0,
      "options": [
        { "label": "Giant segments", "value": 0 },
        { "label": "Bold", "value": 1 },
        { "label": "Analog dial", "value": 2 },
        { "label": "Words (\"twenty past seven\")", "value": 3 }
      ],
      "command": { "app_command": 0, "args": ["face"] }
    },
    {
      "type": "radioGroup",
      "title": "Colours",
      "default": 0,
      "options": [
        { "label": "Classic white", "value": 0 },
        { "label": "Amber", "value": 1 },
        { "label": "Ice blue", "value": 2 },
        { "label": "Matrix green", "value": 3 },
        { "label": "Sunset gradient", "value": 4 },
        { "label": "Rainbow (turns through the hour)", "value": 5 },
        { "label": "My colour", "value": 6 }
      ],
      "command": { "app_command": 1, "args": ["theme"] }
    },
    {
      "type": "colorList",
      "id": "customColor",
      "title": "My Colour",
      "items": [ { "id": "digits", "label": "Digits", "default": "0xFFFF50A0" } ],
      "onChange": { "command": { "app_command": 2 }, "args": { "color": "{color.hexAARRGGBB}" } },
      "visibleWhen": { "arg": "theme", "equals": 6 }
    },
    {
      "type": "section",
      "id": "show",
      "title": "What to Show",
      "widgets": [
        { "type": "switch", "title": "24-Hour Clock", "default": false, "command": { "app_command": 3, "args": ["hour24"] } },
        { "type": "switch", "title": "Seconds", "default": true, "command": { "app_command": 4, "args": ["seconds"] }, "visibleWhen": { "arg": "show", "notEquals": 2 } },
        { "type": "switch", "title": "Date", "default": true, "command": { "app_command": 5, "args": ["date"] }, "visibleWhen": { "arg": "show", "equals": 0 } },
        {
          "type": "slider",
          "title": "Change Face Every",
          "min": 0, "max": 60, "default": 0, "step": 5, "suffix": "Mins",
          "command": { "app_command": 6, "args": ["autoFace"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [ { "type": "note", "text": "0 keeps one face. On Pixlpal: click the knob for the next face, double click for the next colours, triple click to switch between clock and date, clock only and calendar only, long press to start or stop faces changing. The calendar uses the Bold face's solid digits, or segment digits for the other faces." } ]
        }
      ]
    },
    {
      "type": "section",
      "id": "night",
      "title": "Night Mode",
      "subtitle": "Soft colours for the bedside",
      "widgets": [
        { "type": "switch", "title": "Night Mode", "default": false, "command": { "app_command": 7, "args": ["night"] } },
        {
          "type": "timeRange",
          "title": "Night Hours",
          "defaultFrom": "22:00",
          "defaultTo": "07:00",
          "command": { "app_command": 8, "args": ["nightFrom", "nightTo"] },
          "visibleWhen": { "arg": "night", "equals": 1 }
        },
        {
          "type": "radioGroup",
          "title": "At Night",
          "default": 0,
          "options": [
            { "label": "Deep red (kindest to sleepy eyes)", "value": 0 },
            { "label": "My colours, dimmed", "value": 1 }
          ],
          "command": { "app_command": 9, "args": ["nightStyle"] },
          "visibleWhen": { "arg": "night", "equals": 1 }
        }
      ]
    },
    { "type": "borderButton", "label": "SET THE CLOCK FROM THE NETWORK", "command": { "app_command": 10 } }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Face changed" },
    { "when": { "app_command": 1 }, "text": "Colours changed" },
    { "when": { "app_command": 2 }, "text": "Colour set" },
    { "when": { "app_command": 3 }, "text": "Saved" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 5 }, "text": "Saved" },
    { "when": { "app_command": 6 }, "text": "Saved" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 8 }, "text": "Night hours saved" },
    { "when": { "app_command": 9 }, "text": "Saved" },
    { "when": { "app_command": 10 }, "text": "Setting the clock" },
    { "when": { "app_command": 11 }, "text": "Saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Not a colour" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Start and end must differ" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Pixlpal is not online" }
  ]
})json";

#endif
