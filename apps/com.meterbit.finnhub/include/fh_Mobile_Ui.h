/**
 * @file fh_Mobile_Ui.h
 * @brief Mobile UI of the Finnhub Stats app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in finnhub_Stats.cpp:
 *   0 {"stock": {name, value: "AAPL"}} show now (or the old {"stkSymbol"})
 *   1 {"stocks": [{name, value}]} the watchlist (or the old {"stkList": "[AAPL, NVDA]"})
 *   2 {"cycleStocks"} / {"dInterval": 10-600}   3 {"api_key"}
 * The stock searches run on the phone, over the 5,541 US stocks it bundles (assets/stocks_us.json, "localList";
 * made by docs/MOBILE_UI_FILES/tools/make_search_lists.py). Commands 4 and 5, the device-side searches, are gone.
 * The argument names are also the keys of the values the app reports, with the status panel's shownText, shownState,
 * market, marketState, plan, planState, paid, keyText, keyState, watchCount and updated.
 */
#ifndef FH_MOBILE_UI_H
#define FH_MOBILE_UI_H

static const char FH_UI_MANIFEST[] = R"json({
  "appID": "4/0",
  "name": "Finnhub Stocks",
  "about": "A stock market terminal for your watchlist: live prices, the session chart, returns, fundamentals, the 52-week range, analysts, earnings and news, from Finnhub.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char FH_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Market",
      "emptyText": "Launch Finnhub Stocks on Pixlpal to see prices here.",
      "rows": [
        { "label": "On screen", "value": "{shownText}", "state": "{shownState}" },
        { "label": "US market", "value": "{market}", "state": "{marketState}" },
        { "label": "Watchlist", "value": "{watchCount} stocks" },
        { "label": "API key", "value": "{keyText}", "state": "{keyState}" },
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "Last answer", "ago": "updated" }
      ]
    },
    {
      "type": "searchPicker",
      "title": "Watchlist",
      "search": "stocks",
      "multi": true,
      "max": 20,
      "minChars": 1,
      "emptyHint": "Search by name or symbol: apple, NVDA, bank of america",
      "noResults": "No stock of that name. Try its symbol",
      "localList": { "asset": "stocks_us.json", "maxResults": 15,
                     "custom": { "pattern": "^[A-Za-z0-9.^-]{1,6}$", "unless": "exactOrWords", "name": "{Q}", "value": "{Q}", "detail": "Not in the list: check the symbol" } },
      "pickCommand": { "app_command": 1, "args": ["stocks"] }
    },
    {
      "type": "searchPicker",
      "title": "Show Now",
      "search": "show",
      "multi": false,
      "minChars": 1,
      "emptyHint": "Any stock, in the watchlist or not",
      "noResults": "No stock of that name. Try its symbol",
      "localList": { "asset": "stocks_us.json", "maxResults": 15,
                     "custom": { "pattern": "^[A-Za-z0-9.^-]{1,6}$", "unless": "exactOrWords", "name": "{Q}", "value": "{Q}", "detail": "Not in the list: check the symbol" } },
      "pickCommand": { "app_command": 0, "args": ["stock"] }
    },
    {
      "type": "section",
      "id": "cycle",
      "title": "Going Through the Watchlist",
      "widgets": [
        {
          "type": "switch",
          "title": "Move to the Next Stock by Itself",
          "default": true,
          "command": { "app_command": 2, "args": ["cycleStocks"] }
        },
        {
          "type": "slider",
          "title": "Time per Stock",
          "min": 10,
          "max": 600,
          "step": 5,
          "default": 30,
          "suffix": "Secs",
          "command": { "app_command": 2, "args": ["dInterval"] },
          "visibleWhen": { "arg": "cycleStocks", "equals": 1 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: press the knob for the next stock, long press to start or stop moving through them by itself. The pages (session chart, returns, fundamentals, 52 weeks, analysts) turn while a stock is shown." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "Finnhub API Key and Plan",
      "subtitle": "A free key from finnhub.io; a paid plan's extras show by themselves",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "Finnhub API Key",
          "secret": true,
          "emptyHint": "Paste your key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 3, "args": ["api_key"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Free plan: US stock prices, the US market's session, company profiles, fundamentals, analysts' ratings, earnings and company news. It allows 60 calls a minute; Pixlpal makes at most 30." },
            { "type": "bullet", "text": "Paid plans also give analysts' price targets. Pixlpal checks once a day and marks the target on the 52-week page when your key has it." },
            { "type": "paragraph", "spans": [ { "text": "Free key: " }, { "text": "finnhub.io/register", "link": "https://finnhub.io/register" }, { "text": "   Plans: " }, { "text": "finnhub.io/pricing", "link": "https://finnhub.io/pricing" } ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Market data from Finnhub. Prices are for information, not for trading." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Showing it now" },
    { "when": { "app_command": 1 }, "text": "Watchlist saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Key saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is not a stock symbol" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick between 1 and 100 stocks" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Between 10 and 600 seconds" },
    { "when": { "app_command": 3, "response": 0 }, "text": "That does not look like a Finnhub key" }
  ]
})json";

#endif
