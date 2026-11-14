/**
 * @file omt_Mobile_Ui.h
 * @brief Mobile UI of the Open-Meteo app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in openMeteo.cpp:
 *   0 {"query"} (answered as 248)   1 {"place": {name, value: "lat,lng"}}   2 {"units"}   3 {"air"}   4 {"aqi"}
 *   5 {"refresh"}   6 {"api_key"}   7 {"cycle"} / {"seconds"}   8 {} refresh now   9 {} back to the free API
 * The argument names are also the keys of the values the app reports, with the status panel's conditions, airText,
 * airState, plan, planState, paid, keyText, keyState, usedToday, pace and updated.
 */
#ifndef OMT_MOBILE_UI_H
#define OMT_MOBILE_UI_H

static const char OMT_UI_MANIFEST[] = R"json({
  "appID": "3/1",
  "name": "Open-Meteo",
  "about": "Free weather from Open-Meteo for any place: now, 48 hours, 16 days, sun and moon, and air quality with pollen in Europe. No key needed; a paid Open-Meteo key is used when you add one.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char OMT_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Weather",
      "emptyText": "Launch Open-Meteo on Pixlpal to see the weather here.",
      "rows": [
        { "label": "Now", "value": "{conditions}" },
        { "label": "Air", "value": "{airText}", "state": "{airState}" },
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "Today", "value": "{usedToday} requests. {pace}" },
        { "label": "Last read", "ago": "updated" }
      ]
    },
    {
      "type": "searchPicker",
      "title": "Place",
      "search": "places",
      "multi": false,
      "minChars": 2,
      "emptyHint": "Search a city, e.g. Lagos",
      "noResults": "No place of that name",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["place"] }
    },
    {
      "type": "section",
      "id": "weather",
      "title": "Weather",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Units",
          "default": 0,
          "options": [
            { "label": "°C, km/h", "value": 0 },
            { "label": "°F, mph", "value": 1 }
          ],
          "command": { "app_command": 2, "args": ["units"] }
        },
        {
          "type": "switch",
          "title": "Air Quality",
          "default": true,
          "command": { "app_command": 3, "args": ["air"] }
        },
        {
          "type": "radioGroup",
          "title": "Air Quality Index",
          "default": 0,
          "options": [
            { "label": "US AQI (0-500)", "value": 0 },
            { "label": "European AQI (0-100+)", "value": 1 }
          ],
          "command": { "app_command": 4, "args": ["aqi"] },
          "visibleWhen": { "arg": "air", "equals": 1 }
        },
        {
          "type": "slider",
          "title": "Read the Conditions Every",
          "min": 5,
          "max": 60,
          "step": 5,
          "default": 15,
          "suffix": "Mins",
          "command": { "app_command": 5, "args": ["refresh"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On the free API the fastest is every 10 minutes; a paid key allows 5. Pollen is measured in Europe only." }
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
          "type": "switch",
          "title": "Rotate Pages",
          "default": true,
          "command": { "app_command": 7, "args": ["cycle"] }
        },
        {
          "type": "slider",
          "title": "Time per Page",
          "min": 4,
          "max": 60,
          "default": 10,
          "suffix": "Secs",
          "command": { "app_command": 7, "args": ["seconds"] },
          "visibleWhen": { "arg": "cycle", "equals": 1 }
        },
        {
          "type": "borderButton",
          "label": "REFRESH NOW",
          "command": { "app_command": 8 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: click for the next page (now, hours, days, the week after, sun and moon, air, your plan), double click for now, triple click to refresh, long press to start or stop rotation." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "plan",
      "title": "Plan and API Key",
      "subtitle": "Free for personal use; a key for commercial use",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "Open-Meteo API Key (optional)",
          "secret": true,
          "emptyHint": "Paste a key from a paid plan",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 6, "args": ["api_key"] }
        },
        {
          "type": "borderButton",
          "label": "USE THE FREE API",
          "command": { "app_command": 9 },
          "confirm": { "title": "Forget the key?", "text": "Pixlpal will use Open-Meteo's free API, for non-commercial use only.", "ok": "Forget it" },
          "visibleWhen": { "arg": "api_key_set", "equals": 1 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "paragraph", "text": "The free API is for non-commercial use: a display at home is fine. It allows 10,000 requests a day per network; Pixlpal makes about 200." },
            { "type": "paragraph", "text": "For use in a business, take an Open-Meteo plan (Standard, Professional or Enterprise) and paste its key. Pixlpal then uses Open-Meteo's reserved servers and can read the conditions every 5 minutes." },
            { "type": "paragraph", "spans": [ { "text": "Plans: " }, { "text": "open-meteo.com/en/pricing", "link": "https://open-meteo.com/en/pricing" } ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Weather data by Open-Meteo.com, under CC BY 4.0. Air quality from the Copernicus Atmosphere Monitoring Service (CAMS) via Open-Meteo. Place search based on GeoNames." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Searching" },
    { "when": { "app_command": 1 }, "text": "Place saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Saved" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 5 }, "text": "Saved" },
    { "when": { "app_command": 6 }, "text": "Key saved" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 8 }, "text": "Refreshing" },
    { "when": { "app_command": 9 }, "text": "Using the free API" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Wait for the last search to finish" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick a place from the search" },
    { "when": { "app_command": 6, "response": 0 }, "text": "That does not look like an Open-Meteo key" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Refreshed in the last five minutes" }
  ]
})json";

#endif
