/**
 * @file rss_Mobile_Ui.h
 * @brief Mobile UI of the RSS Reader app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in rss_News_Reader.cpp:
 *   0 {"query"} (answered as 248)   1 {"feeds": [{name, value: url}]}   2 {"view"}   3 {"cycle"} / {"seconds"}
 *   4 {"summaries"}   5 {"maxAge"}   6 {"muted"}   7 {"refresh"}   8 {} refresh now
 * The argument names are also the keys of the values the app reports, with the status panel's feedsText, feedsState,
 * problems, stories, hidden, fresh and updated.
 */
#ifndef RSS_MOBILE_UI_H
#define RSS_MOBILE_UI_H

static const char RSS_UI_MANIFEST[] = R"json({
  "appID": "1/1",
  "name": "RSS Reader",
  "about": "Headlines from up to seven news feeds you choose: BBC, Al Jazeera, NPR, the Guardian, tech and Nigerian news, or any RSS or Atom feed by its address.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char RSS_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Your Feeds",
      "emptyText": "Launch the RSS Reader on Pixlpal to see how your feeds are doing.",
      "rows": [
        { "label": "Feeds", "value": "{feedsText}", "state": "{feedsState}" },
        { "label": "Not reading", "value": "{problems}" },
        { "label": "Stories", "value": "{stories} on show, {fresh} new, {hidden} filtered out" },
        { "label": "Last read", "ago": "updated" }
      ]
    },
    {
      "type": "searchPicker",
      "title": "Feeds",
      "search": "feeds",
      "multi": true,
      "max": 7,
      "minChars": 2,
      "emptyHint": "Search: bbc, tech, sport, nigeria... or paste a feed address",
      "noResults": "No ready-made feed matches. Paste the feed's address (https://...)",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["feeds"] }
    },
    {
      "type": "section",
      "id": "reading",
      "title": "On the Screen",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Show",
          "default": 0,
          "options": [
            { "label": "One story at a time", "value": 0 },
            { "label": "A list of headlines", "value": 1 }
          ],
          "command": { "app_command": 2, "args": ["view"] }
        },
        {
          "type": "switch",
          "title": "Then the Story's Summary",
          "default": true,
          "command": { "app_command": 4, "args": ["summaries"] },
          "visibleWhen": { "arg": "view", "equals": 0 }
        },
        {
          "type": "switch",
          "title": "Move On by Itself",
          "default": true,
          "command": { "app_command": 3, "args": ["cycle"] }
        },
        {
          "type": "slider",
          "title": "Time per Story",
          "min": 5,
          "max": 60,
          "default": 12,
          "suffix": "Secs",
          "command": { "app_command": 3, "args": ["seconds"] },
          "visibleWhen": { "arg": "cycle", "equals": 1 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: turn the knob to go through the stories, click for the next one, double click to switch between one story and the list, triple click to read the feeds now, long press to pause." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "filters",
      "title": "Filters",
      "subtitle": "Keep old stories and topics you don't want off the screen",
      "collapsed": true,
      "widgets": [
        {
          "type": "slider",
          "title": "Hide Stories Older Than (0: never)",
          "min": 0,
          "max": 72,
          "default": 0,
          "suffix": "Hours",
          "command": { "app_command": 5, "args": ["maxAge"] }
        },
        {
          "type": "textField",
          "title": "Muted Words",
          "emptyHint": "e.g. crypto, celebrity",
          "filledHint": "Words separated by commas; send an empty field to clear",
          "command": { "app_command": 6, "args": ["muted"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "updates",
      "title": "Updates",
      "collapsed": true,
      "widgets": [
        {
          "type": "slider",
          "title": "Read Each Feed Every",
          "min": 10,
          "max": 180,
          "step": 5,
          "default": 15,
          "suffix": "Mins",
          "command": { "app_command": 7, "args": ["refresh"] }
        },
        {
          "type": "borderButton",
          "label": "READ THE FEEDS NOW",
          "command": { "app_command": 8 }
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "About Feeds",
      "blocks": [
        { "type": "paragraph", "text": "Feeds are free to read and need no key. Each story is shown with the feed it came from; the stories belong to their publishers, who offer their feeds for personal reading." },
        { "type": "note", "text": "Some sites stop or move their feeds. If a feed shows as not reading for a day, search for it again or paste its new address." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Searching" },
    { "when": { "app_command": 1 }, "text": "Feeds saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Saved" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 5 }, "text": "Filter saved" },
    { "when": { "app_command": 6 }, "text": "Muted words saved" },
    { "when": { "app_command": 7 }, "text": "Saved" },
    { "when": { "app_command": 8 }, "text": "Reading the feeds" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Type at least two letters" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick between one and seven feeds" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Too many words: keep it under 60 letters" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Read in the last two minutes" }
  ]
})json";

#endif
