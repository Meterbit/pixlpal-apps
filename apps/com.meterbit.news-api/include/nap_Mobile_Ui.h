/**
 * @file nap_Mobile_Ui.h
 * @brief Mobile UI of the News API app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in newsAPI.cpp:
 *   0 {"categories": ["general", ...]}   1 {"topics": "Nigeria, AI"}   2 {"language": "en"}   3 {"api_key"}
 *   4 {"floor": 5-180}   5 {"cycle"} / {"seconds"}   6 {"view": 0-1}   7 {} refresh
 * The argument names are also the keys of the values the app reports, with the status panel's plan, planState,
 * delay, keyText, keyState, left, limit, quotaState, pace, paceState, updated and paid.
 */
#ifndef NAP_MOBILE_UI_H
#define NAP_MOBILE_UI_H

static const char NAP_UI_MANIFEST[] = R"json({
  "appID": "1/2",
  "name": "News API",
  "about": "US top stories by category and stories on your own topics worldwide, one at a time as large as they fit or as a list, from NewsAPI.org.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char NAP_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Plan and Requests",
      "emptyText": "Launch News API on Pixlpal to see your plan and requests.",
      "rows": [
        {
          "label": "Plan",
          "value": "{plan}",
          "state": "{planState}"
        },
        {
          "label": "Stories",
          "value": "{delay}"
        },
        {
          "label": "API key",
          "value": "{keyText}",
          "state": "{keyState}"
        },
        {
          "label": "Requests",
          "value": "{left} of {limit} left today",
          "state": "{quotaState}",
          "meter": {
            "value": "left",
            "max": "limit"
          }
        },
        {
          "label": "Pace",
          "value": "{pace}",
          "state": "{paceState}"
        },
        {
          "label": "Last read",
          "ago": "updated"
        }
      ]
    },
    {
      "type": "richTextBlock",
      "blocks": [
        {
          "type": "note",
          "text": "NewsAPI's free Developer plan is for development and testing only: its terms ask for a paid plan (Business) for a display in everyday use."
        }
      ],
      "visibleWhen": {
        "arg": "paid",
        "notEquals": 1
      }
    },
    {
      "type": "section",
      "id": "feeds",
      "title": "Your Feeds",
      "widgets": [
        {
          "type": "dropDownList",
          "title": "US Headlines by Category",
          "dataset": "categories",
          "buttonLabel": "CHOOSE UP TO 4",
          "sendAs": "array",
          "command": {
            "app_command": 0,
            "args": [
              "categories"
            ]
          }
        },
        {
          "type": "textField",
          "title": "Your Topics, Worldwide",
          "emptyHint": "e.g. Nigeria, Arsenal, AI (up to 3)",
          "filledHint": "Change your topics",
          "command": {
            "app_command": 1,
            "args": [
              "topics"
            ]
          }
        },
        {
          "type": "dropDownTextField",
          "title": "Language of Topic Stories",
          "dataset": "languages",
          "dropDownItemCount": 8,
          "defaultValue": "en",
          "command": {
            "app_command": 2,
            "args": [
              "language"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "NewsAPI's headlines by category are for the United States; topics search news from everywhere. The day's requests are shared among the feeds."
            }
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
          "title": "Show",
          "default": 0,
          "options": [
            {
              "label": "One story at a time, large",
              "value": 0
            },
            {
              "label": "A list of headlines",
              "value": 1
            }
          ],
          "command": {
            "app_command": 6,
            "args": [
              "view"
            ]
          }
        },
        {
          "type": "switch",
          "title": "Move On by Itself",
          "default": true,
          "command": {
            "app_command": 5,
            "args": [
              "cycle"
            ]
          }
        },
        {
          "type": "slider",
          "title": "Time per Story",
          "min": 5,
          "max": 60,
          "default": 12,
          "suffix": "Secs",
          "command": {
            "app_command": 5,
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
              "text": "On Pixlpal: click for the next story, double click for its summary, triple click to refresh, long press to pause."
            }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "API Key and Plans",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "NewsAPI Key",
          "secret": true,
          "emptyHint": "Paste your NewsAPI key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": {
            "app_command": 3,
            "args": [
              "api_key"
            ]
          }
        },
        {
          "type": "slider",
          "title": "Read a Feed at Most Every",
          "min": 5,
          "max": 180,
          "step": 5,
          "default": 15,
          "suffix": "Mins",
          "command": {
            "app_command": 4,
            "args": [
              "floor"
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
            "text": "This reads every feed: one request each from today's quota.",
            "ok": "Refresh"
          }
        },
        {
          "type": "richTextBlock",
          "title": "Plans",
          "blocks": [
            {
              "type": "subheading",
              "text": "Developer (free): 100 requests a day"
            },
            {
              "type": "bullet",
              "text": "Stories 24 hours late, a month of search. For development and testing only."
            },
            {
              "type": "subheading",
              "text": "Business, $449 a month: 250,000 requests a month"
            },
            {
              "type": "bullet",
              "text": "Real-time stories, read every few minutes, 5 years of search, for production use."
            },
            {
              "type": "subheading",
              "text": "Advanced, $1,749 a month: 2,000,000 requests"
            },
            {
              "type": "note",
              "text": "Pixlpal checks the plan once a day and speeds up by itself after an upgrade. Prices as listed in November 2026."
            },
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Keys and plans: "
                },
                {
                  "text": "newsapi.org",
                  "link": "https://newsapi.org/pricing"
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
          "text": "Headlines from NewsAPI.org, with your own key; the stories belong to their publishers. Not affiliated with NewsAPI or the publishers."
        }
      ]
    }
  ],
  "toasts": [
    {
      "when": {
        "app_command": 0
      },
      "text": "Categories saved"
    },
    {
      "when": {
        "app_command": 1
      },
      "text": "Topics saved"
    },
    {
      "when": {
        "app_command": 2
      },
      "text": "Language saved"
    },
    {
      "when": {
        "app_command": 3
      },
      "text": "Key saved"
    },
    {
      "when": {
        "app_command": 4
      },
      "text": "Saved"
    },
    {
      "when": {
        "app_command": 5
      },
      "text": "Saved"
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
      "text": "Choose up to 4 categories (or keep a topic)"
    },
    {
      "when": {
        "app_command": 1,
        "response": 0
      },
      "text": "Up to 3 topics of letters and digits, separated by commas"
    },
    {
      "when": {
        "app_command": 3,
        "response": 0
      },
      "text": "NewsAPI keys are 32 letters and digits"
    },
    {
      "when": {
        "app_command": 7,
        "response": 0
      },
      "text": "Refreshed recently, or today's requests are used"
    }
  ]
})json";

static const char NAP_UI_DATA[] = R"json({"datasets": {"categories": [{"name": "Top stories", "value": "general"}, {"name": "Business", "value": "business"}, {"name": "Technology", "value": "technology"}, {"name": "Science", "value": "science"}, {"name": "Health", "value": "health"}, {"name": "Sports", "value": "sports"}, {"name": "Entertainment", "value": "entertainment"}], "languages": [{"name": "Any language", "value": "any"}, {"name": "English", "value": "en"}, {"name": "French", "value": "fr"}, {"name": "Arabic", "value": "ar"}, {"name": "German", "value": "de"}, {"name": "Spanish", "value": "es"}, {"name": "Italian", "value": "it"}, {"name": "Dutch", "value": "nl"}, {"name": "Norwegian", "value": "no"}, {"name": "Portuguese", "value": "pt"}, {"name": "Russian", "value": "ru"}, {"name": "Swedish", "value": "sv"}, {"name": "Hebrew", "value": "he"}, {"name": "Chinese", "value": "zh"}]}})json";

#endif
