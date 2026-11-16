/**
 * @file cmc_Mobile_Ui.h
 * @brief CoinMarketCap Crypto's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (coinMarketCap.cpp):
 *   0 {"api_key": "..."}                 the owner's CoinMarketCap API key (empty removes it); never sent back
 *   1 {"watch": "BTC ETH SOL"}           the watchlist: up to 10 symbols
 *   2 {"convert": "USD"}                 the currency prices are in
 *   3 {"share": 10-100}                  the share of the plan's credits left this month the app may use
 *   4 {"seconds": 5-60}                  each page's time on screen
 *   5 {"market": 0|1}  6 {"movers": 0|1} the market page, the top-50 movers page
 *   7 {}                                 read the plan and the prices now
 */
#ifndef CMC_MOBILE_UI_H
#define CMC_MOBILE_UI_H

static const char CMC_UI_MANIFEST[] = R"json({
  "name": "CoinMarketCap Crypto",
  "about": "Your crypto watchlist from CoinMarketCap with your own API key: prices, changes from an hour to 90 days, a 7-day chart where your plan has history, the whole market, Fear & Greed and the day's biggest movers. It reads your plan's credits and paces itself to them.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CMC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.coinmarketcap.png", "height": 64, "alt": "CoinMarketCap Crypto" },
    { "type": "statusPanel", "title": "Your Plan and Pixlpal", "source": "cmc" },
    { "type": "textField", "title": "Watchlist", "emptyHint": "Symbols, up to 10: BTC ETH SOL", "filledHint": "Change the watchlist", "command": { "app_command": 1, "args": ["watch"] } },
    { "type": "borderButton", "label": "READ NOW", "command": { "app_command": 7 } },
    {
      "type": "section",
      "id": "key",
      "title": "Your CoinMarketCap API Key",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "From pro.coinmarketcap.com, your account's API Keys page", "filledHint": "Change the key (empty removes it)", "secret": true, "command": { "app_command": 0, "args": ["api_key"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "The key is kept on Pixlpal and used only to ask CoinMarketCap for data; it is never shown or sent back to the phone. What you see depends on your plan: every plan has prices, the market and Fear & Greed; the 7-day chart needs a plan with price history, and otherwise the screen shows the changes as bars." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "setup",
      "title": "Credits and the Screen",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Share of Your Credits Pixlpal May Use (%)", "min": 10, "max": 100, "default": 80, "command": { "app_command": 3, "args": ["share"] } },
        { "type": "textField", "title": "Prices In", "emptyHint": "USD, EUR, NGN, KES...", "filledHint": "Change the currency", "command": { "app_command": 2, "args": ["convert"] } },
        { "type": "slider", "title": "Seconds Each Page", "min": 5, "max": 60, "default": 10, "command": { "app_command": 4, "args": ["seconds"] } },
        { "type": "switch", "title": "Market Page (Total Value, Dominance, Fear & Greed)", "default": true, "command": { "app_command": 5, "args": ["market"] } },
        { "type": "switch", "title": "Biggest Movers Page (Top 50)", "default": true, "command": { "app_command": 6, "args": ["movers"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "Pixlpal asks CoinMarketCap how many credits your plan has left this month and when they renew, and spreads its share of them over the time left: on the free plan prices refresh every few minutes, on paid plans as often as every 30 seconds. Lower the share if you use the same key elsewhere." }
        ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The pages go round by themselves: each coin, the market, the movers. Turn the knob to go through them yourself; click to read now." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Key saved on Pixlpal" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write up to 10 symbols: BTC ETH SOL" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Write a currency code: USD" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Open the app on Pixlpal and give your key first" }
  ]
})json";

#endif
