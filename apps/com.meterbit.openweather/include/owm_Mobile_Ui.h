/**
 * @file owm_Mobile_Ui.h
 * @brief Mobile UI of the OpenWeather app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in openWeather.cpp:
 *   0 {"query"} (answered as 248)   1 {"place": {name, value: "lat,lng"}}   2 {"units"}   3 {"air"}   4 {"alerts"}
 *   5 {"oneCall"}   6 {"api_key"}   7 {"cycle"} / {"seconds"}   8 {} refresh now
 * The argument names are also the keys of the values the app reports, with the status panel's conditions, airText,
 * airState, alert, alertState, plan, planState, paid, hasOneCall, oneCallText, keyText, keyState, usedToday and updated.
 */
#ifndef OWM_MOBILE_UI_H
#define OWM_MOBILE_UI_H

static const char OWM_UI_MANIFEST[] = R"json({
  "appID": "3/0",
  "name": "OpenWeather",
  "about": "The weather from OpenWeather for any place: now, the hours and days ahead, sun and moon, air quality, and alerts. Works on the free plan and uses more of OpenWeather as your key gets it.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char OWM_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Weather and Plan",
      "emptyText": "Launch OpenWeather on Pixlpal to see the weather and your plan.",
      "rows": [
        { "label": "Now", "value": "{conditions}" },
        { "label": "Air", "value": "{airText}", "state": "{airState}" },
        { "label": "Alerts", "value": "{alert}", "state": "{alertState}" },
        { "label": "API key", "value": "{keyText}", "state": "{keyState}" },
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "One Call 3.0", "value": "{oneCallText}" },
        { "label": "Today", "value": "{usedToday} calls" },
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
          "type": "switch",
          "title": "Weather Alerts (One Call 3.0)",
          "default": true,
          "command": { "app_command": 4, "args": ["alerts"] }
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
            { "type": "note", "text": "On Pixlpal: click for the next page (now, hours, days, sun and moon, air, alerts, your plan), double click for now, triple click to refresh, long press to start or stop rotation." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "API Key and Plan",
      "subtitle": "Free key from openweathermap.org; upgrades are picked up by themselves",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "OpenWeather API Key",
          "secret": true,
          "emptyHint": "Paste your key (32 letters and digits)",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 6, "args": ["api_key"] }
        },
        {
          "type": "switch",
          "title": "Use One Call 3.0 When Subscribed",
          "default": true,
          "command": { "app_command": 5, "args": ["oneCall"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Free plan: conditions every 10 minutes, a forecast in 3-hour steps for 5 days, and air quality." },
            { "type": "bullet", "text": "Startup plan: adds the 16-day forecast. Developer plan and up: adds an hour-by-hour forecast." },
            { "type": "bullet", "text": "One Call by Call: 48 hours hour by hour, 8 days, and government weather alerts. Its first 1,000 calls a day are free; Pixlpal makes at most 96, so it costs nothing." },
            { "type": "note", "text": "Pixlpal checks once a day which of these your key has, so an upgrade on openweathermap.org shows up within a day. New keys can take up to two hours to start working." },
            { "type": "paragraph", "spans": [ { "text": "Plans: " }, { "text": "openweathermap.org/price", "link": "https://openweathermap.org/price" } ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Weather data © OpenWeather, under the Open Database License. Alerts from the national weather services OpenWeather passes on." }
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
    { "when": { "app_command": 0, "response": 0 }, "text": "Add your key first, or wait for the last search" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick a place from the search" },
    { "when": { "app_command": 6, "response": 0 }, "text": "OpenWeather keys are 32 letters and digits" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Refreshed in the last five minutes, or no key yet" }
  ]
})json";

#endif
