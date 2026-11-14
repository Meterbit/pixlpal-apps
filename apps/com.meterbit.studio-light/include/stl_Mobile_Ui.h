/**
 * @file stl_Mobile_Ui.h
 * @brief Mobile UI of the Studio Light app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in studioLight.cpp:
 *   0 {"colorIndex": 0-4, "colorVal"}   1 {"value": 1-100} panel brightness   2 {"lightMode": 0-5}
 *   3 {"dInterval": 1-600}   4 {"kelvin"}   5 {"effect": 0-4}   6 {"intensity"}   7 {"power"}   8 {"fade"}
 *   9 {"sunrise": 1-60}
 * Commands 0-3 are the old page's, so it still works. The argument names are also the keys of the values the app
 * reports, with the status panel's status and statusState.
 */
#ifndef STL_MOBILE_UI_H
#define STL_MOBILE_UI_H

static const char STL_UI_MANIFEST[] = R"json({
  "appID": "6/0",
  "name": "Studio Light",
  "about": "The whole panel as a light: any colour, two colours, a colour cycle, white light at the colour temperature you choose, and effects such as candle, aurora and a sunrise wake light.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char STL_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Light",
      "emptyText": "Launch Studio Light on Pixlpal.",
      "rows": [ { "label": "Now", "value": "{status}", "state": "{statusState}" } ]
    },
    { "type": "switch", "title": "Light On", "default": true, "command": { "app_command": 7, "args": ["power"] } },
    {
      "type": "radioGroup",
      "title": "Light",
      "default": 4,
      "options": [
        { "label": "White, at a colour temperature", "value": 4 },
        { "label": "One colour", "value": 0 },
        { "label": "Two colours, side by side", "value": 1 },
        { "label": "Two colours, blended", "value": 3 },
        { "label": "Cycle through five colours", "value": 2 },
        { "label": "Effect", "value": 5 }
      ],
      "command": { "app_command": 2, "args": ["lightMode"] }
    },
    {
      "type": "slider",
      "title": "Colour Temperature",
      "min": 1800,
      "max": 10000,
      "step": 100,
      "default": 5600,
      "suffix": "K",
      "command": { "app_command": 4, "args": ["kelvin"] },
      "visibleWhen": { "arg": "lightMode", "equals": 4 }
    },
    {
      "type": "richTextBlock",
      "blocks": [ { "type": "note", "text": "2700 K is a warm lamp, 4000 K neutral, 5600 K daylight for photos and video, 6500 K cool daylight." } ],
      "visibleWhen": { "arg": "lightMode", "equals": 4 }
    },
    {
      "type": "radioGroup",
      "title": "Effect",
      "default": 0,
      "options": [
        { "label": "Breathing (the first colour)", "value": 0 },
        { "label": "Candle", "value": 1 },
        { "label": "Rainbow", "value": 2 },
        { "label": "Aurora", "value": 3 },
        { "label": "Sunrise wake light", "value": 4 }
      ],
      "command": { "app_command": 5, "args": ["effect"] },
      "visibleWhen": { "arg": "lightMode", "equals": 5 }
    },
    {
      "type": "slider",
      "title": "Sunrise Takes",
      "min": 1,
      "max": 60,
      "default": 15,
      "suffix": "Mins",
      "command": { "app_command": 9, "args": ["sunrise"] },
      "visibleWhen": { "arg": "effect", "equals": 4 }
    },
    {
      "type": "slider",
      "title": "Intensity",
      "min": 1,
      "max": 100,
      "default": 100,
      "suffix": "%",
      "command": { "app_command": 6, "args": ["intensity"] }
    },
    {
      "type": "section",
      "id": "colours",
      "title": "Colours",
      "widgets": [
        {
          "type": "colorList",
          "title": "Colours (the first is the one colour; the first two are split and blended)",
          "items": [
            { "id": "0", "label": "Colour 1", "default": "0xFFFFFFFF" },
            { "id": "1", "label": "Colour 2", "default": "0xFF8000FF" },
            { "id": "2", "label": "Colour 3", "default": "0xFF00FF40" },
            { "id": "3", "label": "Colour 4", "default": "0xFFFFD000" },
            { "id": "4", "label": "Colour 5", "default": "0xFF0040FF" }
          ],
          "onChange": { "command": { "app_command": 0 }, "args": { "colorIndex": "{item.id}", "colorVal": "{color.hexAARRGGBB}" } }
        },
        {
          "type": "slider",
          "title": "Time per Colour (cycle)",
          "min": 1,
          "max": 600,
          "default": 10,
          "suffix": "Secs",
          "command": { "app_command": 3, "args": ["dInterval"] },
          "visibleWhen": { "arg": "lightMode", "equals": 2 }
        },
        {
          "type": "switch",
          "title": "Fade Between Colours",
          "default": true,
          "command": { "app_command": 8, "args": ["fade"] },
          "visibleWhen": { "arg": "lightMode", "equals": 2 }
        }
      ]
    },
    {
      "type": "section",
      "id": "more",
      "title": "Panel and Knob",
      "collapsed": true,
      "widgets": [
        {
          "type": "slider",
          "title": "Panel Brightness (all apps)",
          "min": 1,
          "max": 100,
          "default": 60,
          "suffix": "%",
          "command": { "app_command": 1, "args": ["value"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Turn: intensity, or the colour temperature in white light." },
            { "type": "bullet", "text": "Click: the next colour, warmth or effect. Double click: the next kind of light. Long press: on or off." }
          ]
        }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Colour set" },
    { "when": { "app_command": 2 }, "text": "Light changed" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 9 }, "text": "Sunrise set" }
  ]
})json";

#endif
