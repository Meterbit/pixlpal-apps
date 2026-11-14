/**
 * @file gnw_Mobile_Ui.h
 * @brief Mobile UI of the GNews Headlines app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in googleNews.cpp:
 *   0 {"categories": ["general", ...]}   1 {"topics": "AI, Arsenal"}   2 {"language": "en"}   3 {"country": "ng"}
 *   4 {"api_key"}   5 {"floor": 5-180}   6 {"cycle"} / {"seconds"}   7 {"view": 0-1}   8 {} refresh
 * The argument names are also the keys of the values the app reports, with the status panel's plan, planState,
 * delay, keyText, keyState, left, limit, quotaState, pace, paceState, updated and paid.
 */
#ifndef GNW_MOBILE_UI_H
#define GNW_MOBILE_UI_H

static const char GNW_UI_MANIFEST[] = R"json({
  "appID": "1/0",
  "name": "GNews Headlines",
  "about": "Top stories by category and on your own topics, one at a time as large as they fit or as a list, from GNews. New stories are marked and shown first.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char GNW_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Plan and Requests",
      "emptyText": "Launch GNews Headlines on Pixlpal to see your plan and requests.",
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
      "type": "section",
      "id": "feeds",
      "title": "Your Feeds",
      "widgets": [
        {
          "type": "dropDownList",
          "title": "Categories",
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
          "title": "Your Topics",
          "emptyHint": "e.g. Arsenal, AI, Lagos (up to 3)",
          "filledHint": "Change your topics",
          "command": {
            "app_command": 1,
            "args": [
              "topics"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "Each category and topic is a feed. Fewer feeds are read more often: the day's requests are shared among them."
            }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "region",
      "title": "Language and Country",
      "collapsed": true,
      "widgets": [
        {
          "type": "dropDownTextField",
          "title": "Language",
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
          "type": "dropDownTextField",
          "title": "Country",
          "dataset": "countries",
          "dropDownItemCount": 8,
          "defaultValue": "any",
          "command": {
            "app_command": 3,
            "args": [
              "country"
            ]
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
            "app_command": 7,
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
            "app_command": 6,
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
              "text": "On Pixlpal: click for the next story, double click for its summary, triple click to refresh, long press to pause. New stories are marked NEW and shown first."
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
          "title": "GNews API Key",
          "secret": true,
          "emptyHint": "Paste your GNews key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": {
            "app_command": 4,
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
            "app_command": 5,
            "args": [
              "floor"
            ]
          }
        },
        {
          "type": "borderButton",
          "label": "REFRESH NOW",
          "command": {
            "app_command": 8
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
              "text": "Free: 100 requests a day"
            },
            {
              "type": "bullet",
              "text": "10 stories a feed, 12 hours late. For personal, non-commercial use only."
            },
            {
              "type": "subheading",
              "text": "Essential, from €39.99 a month: 1,000 a day"
            },
            {
              "type": "bullet",
              "text": "Real-time stories, read every few minutes, commercial use."
            },
            {
              "type": "subheading",
              "text": "Business and Enterprise: 5,000 and 25,000 a day"
            },
            {
              "type": "bullet",
              "text": "Faster still, for many displays on one key."
            },
            {
              "type": "note",
              "text": "Upgrade on gnews.io and keep your key: Pixlpal reads the plan from GNews's answers and speeds up by itself. Prices as listed in November 2026."
            },
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Keys and plans: "
                },
                {
                  "text": "gnews.io",
                  "link": "https://gnews.io/pricing"
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
          "text": "Headlines from GNews (gnews.io), with your own key; the stories belong to their publishers. Not affiliated with GNews, Google or the publishers."
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
      "text": "Country saved"
    },
    {
      "when": {
        "app_command": 4
      },
      "text": "Key saved"
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
      "text": "Saved"
    },
    {
      "when": {
        "app_command": 8
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
        "app_command": 4,
        "response": 0
      },
      "text": "GNews keys are 32 letters and digits"
    },
    {
      "when": {
        "app_command": 8,
        "response": 0
      },
      "text": "Refreshed recently, or today's requests are used"
    }
  ]
})json";

static const char GNW_UI_DATA[] = R"json({"datasets": {"categories": [{"name": "Top stories", "value": "general"}, {"name": "World", "value": "world"}, {"name": "Nation", "value": "nation"}, {"name": "Business", "value": "business"}, {"name": "Technology", "value": "technology"}, {"name": "Entertainment", "value": "entertainment"}, {"name": "Sports", "value": "sports"}, {"name": "Science", "value": "science"}, {"name": "Health", "value": "health"}], "languages": [{"name": "Any language", "value": "any"}, {"name": "English", "value": "en"}, {"name": "French", "value": "fr"}, {"name": "Arabic", "value": "ar"}, {"name": "German", "value": "de"}, {"name": "Spanish", "value": "es"}, {"name": "Portuguese", "value": "pt"}, {"name": "Italian", "value": "it"}, {"name": "Dutch", "value": "nl"}, {"name": "Russian", "value": "ru"}, {"name": "Ukrainian", "value": "uk"}, {"name": "Chinese", "value": "zh"}, {"name": "Japanese", "value": "ja"}, {"name": "Hindi", "value": "hi"}, {"name": "Bengali", "value": "bn"}, {"name": "Tamil", "value": "ta"}, {"name": "Turkish", "value": "tr"}, {"name": "Indonesian", "value": "id"}, {"name": "Swedish", "value": "sv"}, {"name": "Norwegian", "value": "no"}, {"name": "Polish", "value": "pl"}, {"name": "Greek", "value": "el"}, {"name": "Hebrew", "value": "he"}, {"name": "Romanian", "value": "ro"}], "countries": [{"name": "Any country", "value": "any"}, {"name": "Nigeria", "value": "ng"}, {"name": "Ghana", "value": "gh"}, {"name": "Kenya", "value": "ke"}, {"name": "South Africa", "value": "za"}, {"name": "Egypt", "value": "eg"}, {"name": "Morocco", "value": "ma"}, {"name": "Ethiopia", "value": "et"}, {"name": "Tanzania", "value": "tz"}, {"name": "Uganda", "value": "ug"}, {"name": "Senegal", "value": "sn"}, {"name": "United States", "value": "us"}, {"name": "United Kingdom", "value": "gb"}, {"name": "Canada", "value": "ca"}, {"name": "Australia", "value": "au"}, {"name": "New Zealand", "value": "nz"}, {"name": "Ireland", "value": "ie"}, {"name": "India", "value": "in"}, {"name": "Pakistan", "value": "pk"}, {"name": "Bangladesh", "value": "bd"}, {"name": "Singapore", "value": "sg"}, {"name": "Malaysia", "value": "my"}, {"name": "Philippines", "value": "ph"}, {"name": "Indonesia", "value": "id"}, {"name": "Japan", "value": "jp"}, {"name": "China", "value": "cn"}, {"name": "Hong Kong", "value": "hk"}, {"name": "UAE", "value": "ae"}, {"name": "Saudi Arabia", "value": "sa"}, {"name": "Israel", "value": "il"}, {"name": "Turkey", "value": "tr"}, {"name": "France", "value": "fr"}, {"name": "Germany", "value": "de"}, {"name": "Spain", "value": "es"}, {"name": "Italy", "value": "it"}, {"name": "Netherlands", "value": "nl"}, {"name": "Portugal", "value": "pt"}, {"name": "Brazil", "value": "br"}, {"name": "Mexico", "value": "mx"}, {"name": "Argentina", "value": "ar"}]}})json";

#endif
