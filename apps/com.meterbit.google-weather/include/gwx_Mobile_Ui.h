/**
 * @file gwx_Mobile_Ui.h
 * @brief Mobile UI of the Google Weather app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in googleWeather.cpp:
 *   0 {"query": "Lagos"} (answered as 248)   1 {"place": {"name", "value": "lat,lng"}}   2 {"units": 0-1}
 *   3 {"alerts"}   4 {"budget": 1000-200000}   5 {"api_key"}   6 {"cycle"} / {"seconds"}   7 {} refresh
 * The argument names are also the keys of the values the app reports, with the status panel's conditions, alert,
 * alertState, keyText, keyState, used, usedLeft, usedState, pace and updated.
 */
#ifndef GWX_MOBILE_UI_H
#define GWX_MOBILE_UI_H

static const char GWX_UI_MANIFEST[] = R"json({
  "appID": "3/2",
  "name": "Google Weather",
  "about": "The weather from Google's Weather API for your place: now, the next hours, seven days, sun and moon, and weather alerts. Uses your own Google Cloud key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char GWX_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Weather and Usage",
      "emptyText": "Launch Google Weather on Pixlpal to see the weather and your usage.",
      "rows": [
        {
          "label": "Now",
          "value": "{conditions}"
        },
        {
          "label": "Alerts",
          "value": "{alert}",
          "state": "{alertState}"
        },
        {
          "label": "API key",
          "value": "{keyText}",
          "state": "{keyState}"
        },
        {
          "label": "This month",
          "value": "{used} calls, {usedLeft} left of your budget",
          "state": "{usedState}",
          "meter": {
            "value": "usedLeft",
            "max": "budget"
          }
        },
        {
          "label": "Pace",
          "value": "{pace}"
        },
        {
          "label": "Last read",
          "ago": "updated"
        }
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
      "searchCommand": {
        "app_command": 0,
        "args": [
          "query"
        ]
      },
      "pickCommand": {
        "app_command": 1,
        "args": [
          "place"
        ]
      }
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
            {
              "label": "°C, km/h",
              "value": 0
            },
            {
              "label": "°F, mph",
              "value": 1
            }
          ],
          "command": {
            "app_command": 2,
            "args": [
              "units"
            ]
          }
        },
        {
          "type": "switch",
          "title": "Weather Alerts",
          "default": true,
          "command": {
            "app_command": 3,
            "args": [
              "alerts"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "Alerts come from the national weather services Google lists for about 60 countries; elsewhere there are none to show."
            }
          ],
          "visibleWhen": {
            "arg": "alerts",
            "equals": 1
          }
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
          "command": {
            "app_command": 6,
            "args": [
              "cycle"
            ]
          }
        },
        {
          "type": "slider",
          "title": "Time per Page",
          "min": 4,
          "max": 60,
          "default": 10,
          "suffix": "Secs",
          "command": {
            "app_command": 6,
            "args": [
              "seconds"
            ]
          },
          "visibleWhen": {
            "arg": "cycle",
            "equals": 1
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "On Pixlpal: click for the next page (now, hours, days, sun and moon, alerts, usage), double click for now, triple click to refresh, long press to start or stop rotation."
            }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "Google API Key and Budget",
      "subtitle": "Your Google Cloud key, and the calls a month you allow",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "Google API Key",
          "secret": true,
          "emptyHint": "Paste your key (AIza...)",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": {
            "app_command": 5,
            "args": [
              "api_key"
            ]
          }
        },
        {
          "type": "slider",
          "title": "Calls a Month Pixlpal May Make",
          "min": 1000,
          "max": 50000,
          "step": 1000,
          "default": 9000,
          "suffix": "",
          "command": {
            "app_command": 4,
            "args": [
              "budget"
            ]
          }
        },
        {
          "type": "borderButton",
          "label": "REFRESH NOW",
          "command": {
            "app_command": 7
          },
          "confirm": {
            "title": "Refresh now?",
            "text": "This uses two calls of your budget.",
            "ok": "Refresh"
          }
        },
        {
          "type": "richTextBlock",
          "title": "Setting Up",
          "blocks": [
            {
              "type": "bullet",
              "text": "In the Google Cloud console, create a project, turn on billing, and enable the Weather API and the Geocoding API."
            },
            {
              "type": "bullet",
              "text": "Create an API key, restrict it to those two APIs, and paste it above."
            },
            {
              "type": "note",
              "text": "Google gives each project free calls every month and bills use beyond them. Pixlpal spreads your budget over the month: 9,000 reads the conditions about every 10 minutes. Check the current free allowance and prices on Google's pricing page; the console has the exact count."
            },
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Pricing: "
                },
                {
                  "text": "mapsplatform.google.com/pricing",
                  "link": "https://mapsplatform.google.com/pricing/"
                }
              ]
            }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        {
          "type": "paragraph",
          "text": "Weather data from Google (Google Maps Platform Weather API), with your own key. Alerts from the issuing weather services. Not affiliated with or endorsed by Google."
        }
      ]
    }
  ],
  "toasts": [
    {
      "when": {
        "app_command": 0
      },
      "text": "Searching"
    },
    {
      "when": {
        "app_command": 1
      },
      "text": "Place saved"
    },
    {
      "when": {
        "app_command": 2
      },
      "text": "Saved"
    },
    {
      "when": {
        "app_command": 3
      },
      "text": "Saved"
    },
    {
      "when": {
        "app_command": 4
      },
      "text": "Budget saved"
    },
    {
      "when": {
        "app_command": 5
      },
      "text": "Key saved"
    },
    {
      "when": {
        "app_command": 6
      },
      "text": "Saved"
    },
    {
      "when": {
        "app_command": 7
      },
      "text": "Refreshing"
    },
    {
      "when": {
        "app_command": 0,
        "response": 0
      },
      "text": "Add your key first, or wait for the last search"
    },
    {
      "when": {
        "app_command": 1,
        "response": 0
      },
      "text": "Pick a place from the search"
    },
    {
      "when": {
        "app_command": 4,
        "response": 0
      },
      "text": "Between 1,000 and 200,000"
    },
    {
      "when": {
        "app_command": 5,
        "response": 0
      },
      "text": "Google API keys start with AIza"
    },
    {
      "when": {
        "app_command": 7,
        "response": 0
      },
      "text": "Refreshed recently, or the budget is spent"
    }
  ]
})json";

#endif
