/**
 * @file wck_Mobile_Ui.h
 * @brief Mobile UI of the World Clock app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in worldClock.cpp:
 *   0 {"dCityIndex", "dCityName", "dTimeZone"} (the old page)   1 {"dCityIndex", "value": colour}
 *   2 {"ClockMode": 0 one city, 1 board, 2 planner}   3 {} set the clock   4 {"cities": [{name, value}]}
 *   5 {"query"} (answered as 248, "cities")   6 {"hour24"}   7 {"cycle"} / {"seconds"}   8 {"from", "to"}
 * The search runs on the device over the phone app's former list of 172 cities (wck_City_List.cpp). The status
 * panel's rows come from the values' "rows": each city's time now.
 */
#ifndef WCK_MOBILE_UI_H
#define WCK_MOBILE_UI_H

static const char WCK_UI_MANIFEST[] = R"json({
  "appID": "0/2",
  "name": "World Clock",
  "about": "Up to six cities' times: one large with its flag, all on a board, or side by side hour by hour to plan a call across time zones.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char WCK_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Now",
      "emptyText": "Launch World Clock on Pixlpal to see your cities' times.",
      "source": "rows"
    },
    {
      "type": "searchPicker",
      "title": "Cities (in order)",
      "search": "cities",
      "multi": true,
      "max": 6,
      "minChars": 2,
      "emptyHint": "Search a city or a country: tokyo, brazil",
      "noResults": "No city of that name in the list",
      "searchCommand": { "app_command": 5, "args": ["query"] },
      "pickCommand": { "app_command": 4, "args": ["cities"] }
    },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 1,
      "options": [
        { "label": "One city, large, with its flag", "value": 0 },
        { "label": "All cities on a board", "value": 1 },
        { "label": "Meeting planner, hour by hour", "value": 2 }
      ],
      "command": { "app_command": 2, "args": ["ClockMode"] }
    },
    {
      "type": "section",
      "id": "display",
      "title": "Display",
      "widgets": [
        { "type": "switch", "title": "24-Hour Clock", "default": false, "command": { "app_command": 6, "args": ["hour24"] } },
        { "type": "switch", "title": "Change View by Itself", "default": true, "command": { "app_command": 7, "args": ["cycle"] } },
        {
          "type": "slider",
          "title": "Time per View (and per City)",
          "min": 5,
          "max": 120,
          "default": 15,
          "suffix": "Secs",
          "command": { "app_command": 7, "args": ["seconds"] },
          "visibleWhen": { "arg": "cycle", "equals": 1 }
        },
        {
          "type": "colorList",
          "title": "City Colours",
          "items": [
            { "id": "0", "label": "City 1", "default": "0xFF50DC78" },
            { "id": "1", "label": "City 2", "default": "0xFFFFFFFF" },
            { "id": "2", "label": "City 3", "default": "0xFFFFD23C" },
            { "id": "3", "label": "City 4", "default": "0xFF50C8FF" },
            { "id": "4", "label": "City 5", "default": "0xFFFF78C8" },
            { "id": "5", "label": "City 6", "default": "0xFFFF963C" }
          ],
          "onChange": { "command": { "app_command": 1 }, "args": { "dCityIndex": "{item.id}", "value": "{color.hexAARRGGBB}" } }
        }
      ]
    },
    {
      "type": "section",
      "id": "planner",
      "title": "Meeting Planner",
      "collapsed": true,
      "widgets": [
        {
          "type": "timeRange",
          "title": "Working Hours",
          "mode": "range",
          "defaultFrom": "09:00",
          "defaultTo": "17:00",
          "fromLabel": "From",
          "toLabel": "To",
          "command": { "app_command": 8, "args": ["from", "to"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "The planner shows every city's hours in columns, green in working hours, amber in the morning and evening, blue at night. Turn Pixlpal's knob to move the time and find an hour that suits everyone; it goes back to now after a minute." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "help",
      "title": "On Pixlpal",
      "collapsed": true,
      "widgets": [
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Click: the next view. Double click: the next city, large." },
            { "type": "bullet", "text": "Turn: brightness, or the planner's time. Long press: views change by themselves, or stay." }
          ]
        },
        { "type": "borderButton", "label": "SET THE CLOCK FROM THE INTERNET", "command": { "app_command": 3 } }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Colour saved" },
    { "when": { "app_command": 2 }, "text": "Showing it now" },
    { "when": { "app_command": 3 }, "text": "Setting the clock" },
    { "when": { "app_command": 4 }, "text": "Cities saved" },
    { "when": { "app_command": 6 }, "text": "Saved" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 8 }, "text": "Working hours saved" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Pick between one and six cities" },
    { "when": { "app_command": 8, "response": 0 }, "text": "The hours must differ" }
  ]
})json";

#endif
