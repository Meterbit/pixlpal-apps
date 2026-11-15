/**
 * @file cc_Mobile_Ui.h
 * @brief Mobile UI of the CoinCap Crypto app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in crypto_Stats.cpp:
 *   0 {"coin": {name, value: "solana|SOL"}} show now (or the old {"coinID", "coinSymbol"})
 *   1 {"coins": [{name, value: "bitcoin|BTC"}]} the watchlist (or the old {"coinSymbols", "coinIDs"} texts)
 *   2 {"cycleCoins"} / {"dInterval": 10-600}   3 {"api_key"}
 *   6 {"credits": 500-10000000} a paid plan's credits a month
 * The coin searches run on the phone, over the 100 coins with CoinCap's ids it bundles (assets/coins_coincap.json,
 * "localList"; made by docs/MOBILE_UI_FILES/tools/make_search_lists.py). 4 and 5, the device-side searches, are gone.
 * The argument names are also the keys of the values the app reports, with the status panel's shownText, shownState,
 * plan, planState, paid, budget, banked, bankMax, pace, keyText, keyState, watchCount and updated.
 */
#ifndef CC_MOBILE_UI_H
#define CC_MOBILE_UI_H

static const char CC_UI_MANIFEST[] = R"json({
  "appID": "4/1",
  "name": "CoinCap Crypto",
  "about": "Your crypto watchlist from CoinCap: prices, 24-hour change, a 7-day chart, market numbers, supply, the top gainers and crypto news, paced to your CoinCap plan's credits.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char CC_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Crypto",
      "emptyText": "Launch CoinCap Crypto on Pixlpal to see prices here.",
      "rows": [
        { "label": "On screen", "value": "{shownText}", "state": "{shownState}" },
        { "label": "Watchlist", "value": "{watchCount} coins" },
        { "label": "API key", "value": "{keyText}", "state": "{keyState}" },
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "Credits banked", "value": "{banked} of {bankMax}, for {budget} a month", "meter": { "value": "banked", "max": "bankMax" } },
        { "label": "Pace", "value": "{pace}" },
        { "label": "Last answer", "ago": "updated" }
      ]
    },
    {
      "type": "searchPicker",
      "title": "Watchlist",
      "search": "coins",
      "multi": true,
      "max": 20,
      "minChars": 1,
      "emptyHint": "Search: btc, solana, shiba, or a CoinCap id",
      "noResults": "No coin of that name",
      "localList": { "asset": "coins_coincap.json", "maxResults": 12,
                     "custom": { "pattern": "^[A-Za-z0-9-]{1,39}$", "unless": "any", "name": "{Q:11}", "value": "{q}|{Q:11}", "detail": "CoinCap id {q}: not in the list" } },
      "pickCommand": { "app_command": 1, "args": ["coins"] }
    },
    {
      "type": "searchPicker",
      "title": "Show Now",
      "search": "show",
      "multi": false,
      "minChars": 1,
      "emptyHint": "Any coin, in the watchlist or not",
      "noResults": "No coin of that name",
      "localList": { "asset": "coins_coincap.json", "maxResults": 12,
                     "custom": { "pattern": "^[A-Za-z0-9-]{1,39}$", "unless": "any", "name": "{Q:11}", "value": "{q}|{Q:11}", "detail": "CoinCap id {q}: not in the list" } },
      "pickCommand": { "app_command": 0, "args": ["coin"] }
    },
    {
      "type": "section",
      "id": "cycle",
      "title": "Going Through the Watchlist",
      "widgets": [
        {
          "type": "switch",
          "title": "Move to the Next Coin by Itself",
          "default": true,
          "command": { "app_command": 2, "args": ["cycleCoins"] }
        },
        {
          "type": "slider",
          "title": "Time per Coin",
          "min": 10,
          "max": 600,
          "step": 5,
          "default": 30,
          "suffix": "Secs",
          "command": { "app_command": 2, "args": ["dInterval"] },
          "visibleWhen": { "arg": "cycleCoins", "equals": 1 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "On Pixlpal: press the knob for the next coin, long press to start or stop moving through them by itself. The pages (7-day chart, market numbers, supply, momentum, leaders and top gainers) turn while a coin is shown." }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "CoinCap API Key and Plan",
      "subtitle": "A free key from pro.coincap.io; a paid plan is recognised by itself",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "CoinCap API Key",
          "secret": true,
          "emptyHint": "Paste your key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 3, "args": ["api_key"] }
        },
        {
          "type": "slider",
          "title": "Credits a Month on Your Paid Plan",
          "min": 500,
          "max": 200000,
          "step": 500,
          "default": 500,
          "suffix": "",
          "command": { "app_command": 6, "args": ["credits"] },
          "visibleWhen": { "arg": "paid", "equals": 1 }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "Free plan: 500 credits a month and 5 calls a minute. A call costs a credit, plus one for each 2.5 KB it returns, so Pixlpal refreshes prices every few hours and keeps the month inside 500." },
            { "type": "bullet", "text": "Paid plans: CoinCap tells Pixlpal the plan's calls a minute with every answer. Set your plan's credits a month here, and Pixlpal refreshes faster to match: about every minute or two on large plans." },
            { "type": "paragraph", "spans": [ { "text": "Keys and plans: " }, { "text": "pro.coincap.io", "link": "https://pro.coincap.io" } ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Crypto data from CoinCap. Prices are for information, not for trading." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Showing it now" },
    { "when": { "app_command": 1 }, "text": "Watchlist saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Key saved" },
    { "when": { "app_command": 6 }, "text": "Credits saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is not a coin Pixlpal can show" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick between 1 and 100 coins" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Between 10 and 600 seconds" },
    { "when": { "app_command": 3, "response": 0 }, "text": "That does not look like a CoinCap key" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Between 500 and 10,000,000" }
  ]
})json";

#endif
