/**
 * @file wfl_Mobile_Ui.h
 * @brief Mobile UI of the World Flags app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in worldFlags.cpp:
 *   0 {"country": {name, value: "ng"}} show (or the old {"countryName"})   1 {"countries": [...]} favourites (or the
 *   old {"flagIDs"})   2 {"cycleFlags", "showData", "dInterval"} (the old page)   3 {"showData"}   4 {"dInterval"}
 *   5 {"query"} favourites' search, 6 {"query"} show-now search: answered as 248 ("countries", "show")
 *   7 {"mode"}   8 {"source"}   9 {"continent"}   10 {"quizSeconds"}
 * The search runs on the device over the phone app's former list of 270 countries (wfl_Country_List.cpp).
 */
#ifndef WFL_MOBILE_UI_H
#define WFL_MOBILE_UI_H

static const char WFL_UI_MANIFEST[] = R"json({
  "appID": "6/1",
  "name": "World Flags",
  "about": "The flags of 270 countries and territories with their capitals and continents: one flag, a slideshow (the world, a continent or your favourites), or a flag quiz.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char WFL_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "On Pixlpal",
      "emptyText": "Launch World Flags on Pixlpal.",
      "rows": [ { "label": "Showing", "value": "{status}" } ]
    },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 1,
      "options": [
        { "label": "One flag", "value": 0 },
        { "label": "A slideshow", "value": 1 },
        { "label": "A quiz: the flag, then its name", "value": 2 }
      ],
      "command": { "app_command": 7, "args": ["mode"] }
    },
    {
      "type": "searchPicker",
      "title": "The Flag",
      "search": "show",
      "multi": false,
      "minChars": 1,
      "emptyHint": "Search a country, a capital or a code: ghana, tokyo, br",
      "noResults": "No country of that name",
      "searchCommand": { "app_command": 6, "args": ["query"] },
      "pickCommand": { "app_command": 0, "args": ["country"] },
      "visibleWhen": { "arg": "mode", "equals": 0 }
    },
    {
      "type": "section",
      "id": "slideshow",
      "title": "Which Flags",
      "visibleWhen": { "arg": "mode", "in": [1, 2] },
      "widgets": [
        {
          "type": "radioGroup",
          "title": "From",
          "default": 0,
          "options": [
            { "label": "The whole world", "value": 0 },
            { "label": "One continent", "value": 1 },
            { "label": "My favourites", "value": 2 }
          ],
          "command": { "app_command": 8, "args": ["source"] }
        },
        {
          "type": "radioGroup",
          "title": "Continent",
          "default": 0,
          "options": [
            { "label": "Africa", "value": 0 },
            { "label": "Asia", "value": 2 },
            { "label": "Europe", "value": 3 },
            { "label": "North America", "value": 4 },
            { "label": "South America", "value": 6 },
            { "label": "Oceania", "value": 5 },
            { "label": "Antarctica", "value": 1 }
          ],
          "command": { "app_command": 9, "args": ["continent"] },
          "visibleWhen": { "arg": "source", "equals": 1 }
        },
        {
          "type": "searchPicker",
          "title": "Favourites",
          "search": "countries",
          "multi": true,
          "max": 40,
          "minChars": 1,
          "emptyHint": "Add countries: nigeria, japan...",
          "noResults": "No country of that name",
          "searchCommand": { "app_command": 5, "args": ["query"] },
          "pickCommand": { "app_command": 1, "args": ["countries"] }
        },
        {
          "type": "slider",
          "title": "Time per Flag",
          "min": 3,
          "max": 600,
          "default": 20,
          "suffix": "Secs",
          "command": { "app_command": 4, "args": ["dInterval"] },
          "visibleWhen": { "arg": "mode", "equals": 1 }
        },
        {
          "type": "slider",
          "title": "Quiz: Time to Guess",
          "min": 3,
          "max": 30,
          "default": 8,
          "suffix": "Secs",
          "command": { "app_command": 10, "args": ["quizSeconds"] },
          "visibleWhen": { "arg": "mode", "equals": 2 }
        }
      ]
    },
    {
      "type": "switch",
      "title": "Name, Capital and Continent Beside the Flag",
      "default": true,
      "command": { "app_command": 3, "args": ["showData"] }
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click: the next flag (in the quiz: the answer, then the next). Double click: the details on or off." },
        { "type": "bullet", "text": "Long press: one flag, slideshow, quiz. Turn: brightness." },
        { "type": "note", "text": "Flags from the woble/flags collection on GitHub, fetched as they are shown." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Showing it now" },
    { "when": { "app_command": 1 }, "text": "Favourites saved" },
    { "when": { "app_command": 3 }, "text": "Saved" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 8 }, "text": "Saved" },
    { "when": { "app_command": 9 }, "text": "Saved" },
    { "when": { "app_command": 10 }, "text": "Saved" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Add some favourites first" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Up to 40 countries from the list" }
  ]
})json";

#endif
