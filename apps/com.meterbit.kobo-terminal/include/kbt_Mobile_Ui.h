/**
 * @file kbt_Mobile_Ui.h
 * @brief Mobile UI of Kobo Terminal {4,7} (FINANCE), rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (koboTerminal.cpp): 0 ask again now   1 {"stocks": [{name, value}]} the watchlist   2 {"stock": {name, value}} the
 *   stock shown on its own   3 / 4 {"query"}: the searches (answered as 248 "stocks", "focus")   5 {"p0".."p7"} pages,
 *   {"cycle"}, {"seconds"}   6 {"apiKey"}
 * Status rows: "market".
 */
#ifndef KBT_MOBILE_UI_H
#define KBT_MOBILE_UI_H

static const char KBT_UI_MANIFEST[] = R"json({
  "appID": "4/7",
  "name": "Kobo Terminal",
  "about": "The Nigerian Exchange and fixed income on Pixlpal: the All-Share Index and breadth, a heatmap of every stock, gainers and losers, your watchlist, one stock with its fundamentals, the NGX indices, the CBN's T-bill and bond auction stop rates, and the bonds on NGX. Data from Kobo Terminal with your own key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char KBT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:koboTerminal.png", "height": 64, "alt": "Kobo Terminal" },
    { "type": "statusPanel", "title": "Market", "source": "market" },
    { "type": "borderButton", "label": "ASK AGAIN NOW", "command": { "app_command": 0 } },
    {
      "type": "searchPicker", "title": "Watchlist", "search": "stocks", "multi": true, "max": 12, "minChars": 1,
      "emptyHint": "Symbol or name: dangcem, zenith, mtn", "noResults": "No listed company of that name",
      "searchCommand": { "app_command": 3, "args": ["query"] }, "pickCommand": { "app_command": 1, "args": ["stocks"] }
    },
    {
      "type": "searchPicker", "title": "The Stock on Its Own", "search": "focus", "multi": false, "minChars": 1,
      "emptyHint": "Any listed company", "noResults": "No listed company of that name",
      "searchCommand": { "app_command": 4, "args": ["query"] }, "pickCommand": { "app_command": 2, "args": ["stock"] }
    },
    {
      "type": "section", "id": "pages", "title": "Pages on Pixlpal", "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "The Market's Day", "default": true, "command": { "app_command": 5, "args": ["p0"] } },
        { "type": "switch", "title": "Heatmap of Every Stock", "default": true, "command": { "app_command": 5, "args": ["p1"] } },
        { "type": "switch", "title": "Gainers and Losers", "default": true, "command": { "app_command": 5, "args": ["p2"] } },
        { "type": "switch", "title": "Watchlist", "default": true, "command": { "app_command": 5, "args": ["p3"] } },
        { "type": "switch", "title": "The Stock on Its Own (Fundamentals on Starter)", "default": true, "command": { "app_command": 5, "args": ["p4"] } },
        { "type": "switch", "title": "NGX Indices", "default": true, "command": { "app_command": 5, "args": ["p5"] } },
        { "type": "switch", "title": "CBN Auctions: T-Bills and Bonds", "default": true, "command": { "app_command": 5, "args": ["p6"] } },
        { "type": "switch", "title": "Bonds on NGX", "default": true, "command": { "app_command": 5, "args": ["p7"] } },
        { "type": "switch", "title": "Turn Pages by Themselves", "default": true, "command": { "app_command": 5, "args": ["cycle"] } },
        { "type": "slider", "title": "Each Page For", "min": 5, "max": 120, "default": 15, "step": 5, "suffix": "Secs", "command": { "app_command": 5, "args": ["seconds"] }, "visibleWhen": { "arg": "cycle", "equals": 1 } }
      ]
    },
    {
      "type": "section", "id": "key", "title": "Kobo Terminal API Key and Plan", "subtitle": "Personal: free, 100 calls a day", "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "Your Kobo Terminal key", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 6, "args": ["apiKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Get a key at koboterminal.com/api (Personal is free). It never leaves Pixlpal." },
          { "type": "bullet", "text": "Pixlpal asks every 20 minutes while NGX trades, once after the close, and the auctions and bonds once a day: about 70 calls on a trading day, inside the Personal plan's 100." },
          { "type": "bullet", "text": "Starter adds the chosen stock's fundamentals (P/E, EPS, yield, ROE, margin). Pixlpal finds your plan by itself each day." },
          { "type": "paragraph", "spans": [ { "text": "Plans: " }, { "text": "koboterminal.com/api", "link": "https://koboterminal.com/api" } ] }
        ] }
      ]
    },
    {
      "type": "richTextBlock", "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click or turn the knob for the next page (turn left to go back), two clicks to ask again now, hold to stop or start the pages turning." },
        { "type": "paragraph", "text": "Data from Kobo Terminal, delayed by at least 30 minutes, for your own information: not investment advice." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Asking now" },
    { "when": { "app_command": 1 }, "text": "Watchlist saved" },
    { "when": { "app_command": 6 }, "text": "Key saved on Pixlpal" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Kobo Terminal on Pixlpal first" },
    { "when": { "app_command": 1, "response": 0 }, "text": "At most 12 stocks" },
    { "when": { "app_command": 6, "response": 0 }, "text": "That does not look like a Kobo Terminal key" }
  ]
})json";

#endif
