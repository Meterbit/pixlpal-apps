/**
 * @file ngm_Mobile_Ui.h
 * @brief Mobile UI of NGX Market {4,6} (FINANCE), rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (ngxMarket.cpp): 0 ask again now   1 {"stocks": [{name, value}]} the watchlist   2 {"stock": {name, value}} the
 *   stock shown on its own   3 / 4 {"query"}: the searches (answered as 248 "stocks", "focus")   5 {"p0".."p7"} pages,
 *   {"cycle"}, {"seconds"}, {"chartPeriod"}   6 {"apiKey"}
 * Status rows: "market".
 */
#ifndef NGM_MOBILE_UI_H
#define NGM_MOBILE_UI_H

static const char NGM_UI_MANIFEST[] = R"json({
  "appID": "4/6",
  "name": "NGX Market",
  "about": "The Nigerian Exchange on Pixlpal: the All-Share Index and the day's breadth, a heatmap of every stock, gainers and losers, your watchlist, one stock with its chart, the NGX indices, the naira's exchange rates and dividends coming. Data from NGN Market with your own free key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char NGM_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:ngxMarket.png", "height": 64, "alt": "NGX Market" },
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
      "type": "radioGroup", "title": "Its Chart (Hobby Plan and Above)", "default": 1,
      "options": [ { "label": "7 Days", "value": 0 }, { "label": "30 Days", "value": 1 }, { "label": "90 Days", "value": 2 }, { "label": "A Year", "value": 3 } ],
      "command": { "app_command": 5, "args": ["chartPeriod"] }
    },
    {
      "type": "section", "id": "pages", "title": "Pages on Pixlpal", "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "The Market's Day", "default": true, "command": { "app_command": 5, "args": ["p0"] } },
        { "type": "switch", "title": "Heatmap of Every Stock", "default": true, "command": { "app_command": 5, "args": ["p1"] } },
        { "type": "switch", "title": "Gainers and Losers", "default": true, "command": { "app_command": 5, "args": ["p2"] } },
        { "type": "switch", "title": "Watchlist", "default": true, "command": { "app_command": 5, "args": ["p3"] } },
        { "type": "switch", "title": "The Stock on Its Own", "default": true, "command": { "app_command": 5, "args": ["p4"] } },
        { "type": "switch", "title": "NGX Indices", "default": true, "command": { "app_command": 5, "args": ["p5"] } },
        { "type": "switch", "title": "Naira Exchange Rates", "default": true, "command": { "app_command": 5, "args": ["p6"] } },
        { "type": "switch", "title": "Dividends Coming (Starter Plan)", "default": true, "command": { "app_command": 5, "args": ["p7"] } },
        { "type": "switch", "title": "Turn Pages by Themselves", "default": true, "command": { "app_command": 5, "args": ["cycle"] } },
        { "type": "slider", "title": "Each Page For", "min": 5, "max": 120, "default": 15, "step": 5, "suffix": "Secs", "command": { "app_command": 5, "args": ["seconds"] }, "visibleWhen": { "arg": "cycle", "equals": 1 } }
      ]
    },
    {
      "type": "section", "id": "key", "title": "NGN Market API Key and Plan", "subtitle": "Free: 3,000 calls a month", "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "ngm_live_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 6, "args": ["apiKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Make a free key at ngnmarket.com: sign in, then Developer. It never leaves Pixlpal." },
          { "type": "bullet", "text": "NGN Market tells Pixlpal the plan and the calls left with every answer. Pixlpal asks about every 20 minutes while NGX trades (09:00 to 16:00, Monday to Friday), once after the close, and spreads the month's calls so they last: about 1,500 of the free plan's 3,000." },
          { "type": "bullet", "text": "Hobby adds the stock's price chart; Starter adds dividends coming. Pixlpal finds out by itself when you upgrade." },
          { "type": "paragraph", "spans": [ { "text": "Plans: " }, { "text": "ngnmarket.com/pricing/api", "link": "https://ngnmarket.com/pricing/api" } ] }
        ] }
      ]
    },
    {
      "type": "richTextBlock", "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click or turn the knob for the next page (turn left to go back), two clicks to ask again now, hold to stop or start the pages turning." },
        { "type": "paragraph", "text": "Data from NGN Market, refreshed every 20 minutes during trading, for your own information: not investment advice." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Asking now" },
    { "when": { "app_command": 1 }, "text": "Watchlist saved" },
    { "when": { "app_command": 6 }, "text": "Key saved on Pixlpal" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open NGX Market on Pixlpal first" },
    { "when": { "app_command": 1, "response": 0 }, "text": "At most 12 stocks" },
    { "when": { "app_command": 6, "response": 0 }, "text": "That does not look like an NGN Market key (ngm_...)" }
  ]
})json";

#endif
