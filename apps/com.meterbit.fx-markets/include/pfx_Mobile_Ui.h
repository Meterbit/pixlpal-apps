/**
 * @file pfx_Mobile_Ui.h
 * @brief Mobile UI of the FX Markets app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in polygonFX.cpp:
 *   0 {"query"} (answered as 248)   1 {"pairs": [{name, value: "EUR/USD"}]}   2 {"intraday"}
 *   3 {"cycle"} / {"seconds"}   4 {"api_key"}   5 {} refresh now
 * The argument names are also the keys of the values the app reports, with the status panel's board, market,
 * marketState, plan, planState, paid, keyText, keyState, callsToday and updated.
 */
#ifndef PFX_MOBILE_UI_H
#define PFX_MOBILE_UI_H

static const char PFX_UI_MANIFEST[] = R"json({
  "appID": "4/3",
  "name": "FX Markets",
  "about": "Forex pairs from Massive (formerly Polygon.io): a board of up to eight pairs, candle charts, and the world's trading sessions. Free plan: end-of-day prices; paid plans: live.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char PFX_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Markets",
      "emptyText": "Launch FX Markets on Pixlpal to see prices here.",
      "rows": [
        { "label": "Prices", "value": "{board}" },
        { "label": "Market", "value": "{market}", "state": "{marketState}" },
        { "label": "API key", "value": "{keyText}", "state": "{keyState}" },
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "Today", "value": "{callsToday} calls" },
        { "label": "Last read", "ago": "updated" }
      ]
    },
    {
      "type": "searchPicker",
      "title": "Pairs (the first is charted first)",
      "search": "pairs",
      "multi": true,
      "max": 8,
      "minChars": 2,
      "emptyHint": "Search: naira, yen, gbp, eur chf...",
      "noResults": "No currency of that name. Try its code, e.g. KES",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["pairs"] }
    },
    {
      "type": "section",
      "id": "display",
      "title": "Display",
      "collapsed": true,
      "widgets": [
        {
          "type": "switch",
          "title": "Today's 15-Minute Chart (paid plans)",
          "default": true,
          "command": { "app_command": 2, "args": ["intraday"] },
          "visibleWhen": { "arg": "paid", "equals": 1 }
        },
        {
          "type": "switch",
          "title": "Rotate Pages",
          "default": true,
          "command": { "app_command": 3, "args": ["cycle"] }
        },
        {
          "type": "slider",
          "title": "Time per Page",
          "min": 4,
          "max": 60,
          "default": 12,
          "suffix": "Secs",
          "command": { "app_command": 3, "args": ["seconds"] },
          "visibleWhen": { "arg": "cycle", "equals": 1 }
        },
        { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 5 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: click for the next page (board, chart, sessions, plan), double click to chart the next pair, triple click to refresh, long press to start or stop rotation." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "Massive API Key and Plan",
      "subtitle": "Free key at massive.com; upgrades are picked up by themselves",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "Massive API Key",
          "secret": true,
          "emptyHint": "Paste your key (Polygon.io keys work too)",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 4, "args": ["api_key"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Currencies Basic (free): end-of-day prices and 30-day charts. It allows 5 calls a minute, so Pixlpal reads one pair every 13 seconds and each pair about once an hour." },
            { "type": "bullet", "text": "Currencies Starter and up: live prices every 15 seconds and today's 15-minute chart. Pixlpal checks once a day, so an upgrade shows within a day." },
            { "type": "note", "text": "Massive's individual plans are for personal, non-commercial use, and their data may not be shown to others. Prices are for information, not trading." },
            { "type": "paragraph", "spans": [ { "text": "Plans: " }, { "text": "massive.com/pricing", "link": "https://massive.com/pricing" } ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Forex data from Massive (formerly Polygon.io), with your own key. Trading sessions are the usual 08:00 to 17:00 local hours of each city (Tokyo 09:00 to 18:00), with daylight saving." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Searching" },
    { "when": { "app_command": 1 }, "text": "Pairs saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Saved" },
    { "when": { "app_command": 4 }, "text": "Key saved" },
    { "when": { "app_command": 5 }, "text": "Refreshing" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick between one and eight pairs" },
    { "when": { "app_command": 4, "response": 0 }, "text": "That does not look like a Massive key" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Refreshed in the last two minutes, or no key yet" }
  ]
})json";

#endif
