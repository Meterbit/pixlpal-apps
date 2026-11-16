/**
 * @file brt_Mobile_Ui.h
 * @brief Branch Rates' phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (branchRates.cpp):
 *   0 {"bank": "Lagoon Bank"}            1 {"base": "NGN"}  the currency the rates are in
 *   2 {"fx0" ... "fx7": "USD 1520.00 1560.00"}   a currency: its code, what the bank buys at, what it sells at (empty removes it)
 *   3 {"dep0" ... "dep3": "Savings account 5.5%"}  a deposit product and its rate (empty removes it)
 *   4 {"note": "Rates may change without notice."}  5 {"url": "https://..."}  a JSON link the rates are read from
 *   6 {}  read the link now              7 {"seconds": 5-60}  each page's time on screen
 */
#ifndef BRT_MOBILE_UI_H
#define BRT_MOBILE_UI_H

static const char BRT_UI_MANIFEST[] = R"json({
  "name": "Branch Rates",
  "about": "A rates board for a bank branch or bureau de change: what the bank buys and sells each currency at, and its savings and deposit rates, in turn on the screen. Type the rates on the phone, or give a link the bank's system keeps up to date.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BRT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.branch-rates.png", "height": 64, "alt": "Branch Rates" },
    { "type": "statusPanel", "title": "The Board", "source": "rates" },
    {
      "type": "section",
      "id": "fx",
      "title": "Exchange Rates",
      "widgets": [
        { "type": "textField", "title": "Rates In (Your Currency)", "emptyHint": "NGN", "filledHint": "Change the currency", "command": { "app_command": 1, "args": ["base"] } },
        { "type": "textField", "title": "Currency 1", "emptyHint": "Code, we buy, we sell: USD 1520.00 1560.00", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx0"] } },
        { "type": "textField", "title": "Currency 2", "emptyHint": "GBP 1910.00 1975.00", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx1"] } },
        { "type": "textField", "title": "Currency 3", "emptyHint": "EUR 1640.00 1700.00", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx2"] } },
        { "type": "textField", "title": "Currency 4", "emptyHint": "Code, we buy, we sell", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx3"] } },
        { "type": "textField", "title": "Currency 5", "emptyHint": "Code, we buy, we sell", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx4"] } },
        { "type": "textField", "title": "Currency 6", "emptyHint": "Code, we buy, we sell", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx5"] } },
        { "type": "textField", "title": "Currency 7", "emptyHint": "Code, we buy, we sell", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx6"] } },
        { "type": "textField", "title": "Currency 8", "emptyHint": "Code, we buy, we sell", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["fx7"] } }
      ]
    },
    {
      "type": "section",
      "id": "dep",
      "title": "Savings and Deposit Rates",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Product 1", "emptyHint": "Name and rate: Savings account 5.5%", "filledHint": "Change it (empty removes it)", "command": { "app_command": 3, "args": ["dep0"] } },
        { "type": "textField", "title": "Product 2", "emptyHint": "Fixed deposit 90 days 12%", "filledHint": "Change it (empty removes it)", "command": { "app_command": 3, "args": ["dep1"] } },
        { "type": "textField", "title": "Product 3", "emptyHint": "Name and rate", "filledHint": "Change it (empty removes it)", "command": { "app_command": 3, "args": ["dep2"] } },
        { "type": "textField", "title": "Product 4", "emptyHint": "Name and rate", "filledHint": "Change it (empty removes it)", "command": { "app_command": 3, "args": ["dep3"] } }
      ]
    },
    {
      "type": "section",
      "id": "setup",
      "title": "The Board",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Bank or Branch", "emptyHint": "e.g. Lagoon Bank, Marina", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["bank"] } },
        { "type": "textField", "title": "Note Along the Bottom", "emptyHint": "Rates are indicative and may change without notice.", "filledHint": "Change the note (empty removes it)", "command": { "app_command": 4, "args": ["note"] } },
        { "type": "slider", "title": "Seconds Each Page", "min": 5, "max": 60, "default": 12, "command": { "app_command": 7, "args": ["seconds"] } }
      ]
    },
    {
      "type": "section",
      "id": "link",
      "title": "Read the Rates From a Link",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Rates Link (JSON)", "emptyHint": "https://intranet.example.com/rates.json", "filledHint": "Change the link (empty removes it)", "secret": true, "command": { "app_command": 5, "args": ["url"] } },
        { "type": "borderButton", "label": "READ IT NOW", "command": { "app_command": 6 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "Pixlpal reads the link every 10 minutes, and what it reads replaces the rates typed above. It takes a JSON answer like this:" },
          { "type": "paragraph", "text": "{\"base\": \"NGN\", \"fx\": [{\"code\": \"USD\", \"buy\": 1520, \"sell\": 1560}], \"deposit\": [{\"name\": \"Savings account\", \"rate\": \"5.5%\"}], \"note\": \"Rates may change without notice.\"}" }
        ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The exchange rates and the deposit rates show in turn. Turn the knob to go through them yourself; click to read the link now." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1, "response": 0 }, "text": "Write a currency code: NGN" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Write the code and two numbers: USD 1520.00 1560.00" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Write the name and the rate: Savings account 5.5%" },
    { "when": { "app_command": 5, "response": 0 }, "text": "That isn't a web link" },
    { "when": { "app_command": 6 }, "text": "Reading the link" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Open Branch Rates on Pixlpal first, and give a link" }
  ]
})json";

#endif
