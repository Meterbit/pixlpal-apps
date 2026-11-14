/**
 * @file cfx_Mobile_Ui.h
 * @brief Mobile UI of the Currency Exchange app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in currencyExchange.cpp:
 *   0 {"base": "USD"}            1 {"watch": ["NGN", ...]}      2 {"featured": "NGN"}
 *   3 {"api_key"} ("" removes)   4 {"amount": "100"}            5 {"cycle"} / {"seconds"}
 *   6 {"inverse"}                7 {} refresh
 * The argument names are also the keys of the values the app reports, with the status panel's plan, planState,
 * keyText, keyState, left, limit, quotaState, renews, cadence, updated, next, history and paid. The "currencies"
 * dataset is generated from cfx_Model.cpp's catalogue. Success toasts come before failure toasts.
 */
#ifndef CFX_MOBILE_UI_H
#define CFX_MOBILE_UI_H

static const char CFX_UI_MANIFEST[] = R"json({
  "appID": "4/2",
  "name": "Currency Exchange",
  "about": "Exchange rates for up to 12 currencies against your base: the featured pair in big digits with its changes and a month's chart, a board, and a converter. Works without a key.",
  "author": "Meterbit Cyb",
  "version": "v2.0.0"
})json";

static const char CFX_UI_API[] = R"json({
  "widgets": [
    {
      "type": "statusPanel",
      "title": "Plan and Rates",
      "emptyText": "Launch Currency Exchange on Pixlpal to see your plan and rates.",
      "rows": [
        {
          "label": "Plan",
          "value": "{plan}",
          "state": "{planState}"
        },
        {
          "label": "API key",
          "value": "{keyText}",
          "state": "{keyState}"
        },
        {
          "label": "Requests",
          "value": "{left} of {limit} left this month",
          "state": "{quotaState}",
          "meter": {
            "value": "left",
            "max": "limit"
          }
        },
        {
          "label": "Quota renews",
          "value": "{renews}"
        },
        {
          "label": "Rates update",
          "value": "{cadence}"
        },
        {
          "label": "Rates of",
          "ago": "updated"
        },
        {
          "label": "Next rates",
          "ago": "next"
        },
        {
          "label": "History",
          "value": "{history}"
        }
      ]
    },
    {
      "type": "section",
      "id": "currencies",
      "title": "Currencies",
      "widgets": [
        {
          "type": "dropDownTextField",
          "title": "Base Currency",
          "dataset": "currencies",
          "dropDownItemCount": 8,
          "defaultValue": "USD",
          "command": {
            "app_command": 0,
            "args": [
              "base"
            ]
          }
        },
        {
          "type": "dropDownList",
          "title": "Currencies to Watch",
          "dataset": "currencies",
          "buttonLabel": "CHOOSE UP TO 12",
          "sendAs": "array",
          "command": {
            "app_command": 1,
            "args": [
              "watch"
            ]
          },
          "itemTapCommand": {
            "app_command": 2,
            "args": [
              "featured"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "Tap a currency above to put it on the big display. One request brings all the rates, so a longer list costs nothing more."
            }
          ]
        },
        {
          "type": "switch",
          "title": "Show the Other Way Round (1 NGN = 0.00074 USD)",
          "default": false,
          "command": {
            "app_command": 6,
            "args": [
              "inverse"
            ]
          }
        }
      ]
    },
    {
      "type": "section",
      "id": "convert",
      "title": "Converter",
      "widgets": [
        {
          "type": "textField",
          "title": "Amount in the Base Currency",
          "emptyHint": "e.g. 100",
          "filledHint": "Change the amount",
          "command": {
            "app_command": 4,
            "args": [
              "amount"
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
          "type": "switch",
          "title": "Rotate Pages",
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
          "title": "Time per Page",
          "min": 4,
          "max": 60,
          "default": 10,
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
              "text": "On Pixlpal: click the knob for the next page (pair, board, converter, plan), double click for the next pair, triple click to refresh, long press to start or stop rotation."
            }
          ]
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "API Key and Plans",
      "subtitle": "Works without a key; a key unlocks more",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "ExchangeRate-API Key",
          "secret": true,
          "emptyHint": "Paste your key (optional)",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": {
            "app_command": 3,
            "args": [
              "api_key"
            ]
          }
        },
        {
          "type": "borderButton",
          "label": "USE THE FREE FEED WITHOUT A KEY",
          "command": {
            "app_command": 3,
            "api_key": ""
          },
          "visibleWhen": {
            "arg": "api_key_set",
            "equals": 1
          },
          "confirm": {
            "title": "Remove the key?",
            "text": "Pixlpal goes back to the free daily feed without a key.",
            "ok": "Remove"
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
            "text": "This uses one request. Rates only change when the provider updates them.",
            "ok": "Refresh"
          }
        },
        {
          "type": "richTextBlock",
          "title": "Plans",
          "blocks": [
            {
              "type": "subheading",
              "text": "No key"
            },
            {
              "type": "bullet",
              "text": "Daily rates for 160+ currencies. The display credits ExchangeRate-API, as the free feed requires."
            },
            {
              "type": "subheading",
              "text": "Free key: 1,500 requests a month"
            },
            {
              "type": "bullet",
              "text": "Daily rates, no credit line. Pixlpal uses about 60 requests a month."
            },
            {
              "type": "subheading",
              "text": "Pro, $10 a month: 30,000 requests"
            },
            {
              "type": "bullet",
              "text": "Hourly rates, and history: 7- and 30-day changes and a month's chart from day one."
            },
            {
              "type": "subheading",
              "text": "Business, $30 a month: 125,000 requests"
            },
            {
              "type": "bullet",
              "text": "Rates every 5 minutes."
            },
            {
              "type": "note",
              "text": "Upgrade on ExchangeRate-API's site and keep your key: Pixlpal reads the plan from it. Prices as listed in November 2026; check the current ones."
            },
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Keys and plans: "
                },
                {
                  "text": "exchangerate-api.com",
                  "link": "https://www.exchangerate-api.com"
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
          "text": "Rates by ExchangeRate-API: indicative mid-market rates, for information only, not for trading. Not affiliated with or endorsed by ExchangeRate-API."
        }
      ]
    }
  ],
  "toasts": [
    {
      "when": {
        "app_command": 0
      },
      "text": "Base currency saved"
    },
    {
      "when": {
        "app_command": 1
      },
      "text": "Watchlist saved"
    },
    {
      "when": {
        "app_command": 2
      },
      "text": "On the big display"
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
      "text": "Amount saved"
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
      "text": "Not a currency code"
    },
    {
      "when": {
        "app_command": 1,
        "response": 0
      },
      "text": "Choose 1 to 12 currencies"
    },
    {
      "when": {
        "app_command": 2,
        "response": 0
      },
      "text": "The list is full, or that is the base currency"
    },
    {
      "when": {
        "app_command": 3,
        "response": 0
      },
      "text": "That does not look like an ExchangeRate-API key"
    },
    {
      "when": {
        "app_command": 4,
        "response": 0
      },
      "text": "Type a number above 0"
    },
    {
      "when": {
        "app_command": 5,
        "response": 0
      },
      "text": "Value out of range"
    },
    {
      "when": {
        "app_command": 7,
        "response": 0
      },
      "text": "Refreshed recently"
    }
  ]
})json";

static const char CFX_UI_DATA[] = R"json({"datasets": {"currencies": [{"name": "USD · US Dollar", "value": "USD"}, {"name": "EUR · Euro", "value": "EUR"}, {"name": "GBP · British Pound", "value": "GBP"}, {"name": "JPY · Japanese Yen", "value": "JPY"}, {"name": "CNY · Chinese Yuan", "value": "CNY"}, {"name": "CHF · Swiss Franc", "value": "CHF"}, {"name": "CAD · Canadian Dollar", "value": "CAD"}, {"name": "AUD · Australian Dollar", "value": "AUD"}, {"name": "NZD · New Zealand Dollar", "value": "NZD"}, {"name": "NGN · Nigerian Naira", "value": "NGN"}, {"name": "GHS · Ghanaian Cedi", "value": "GHS"}, {"name": "KES · Kenyan Shilling", "value": "KES"}, {"name": "ZAR · South African Rand", "value": "ZAR"}, {"name": "EGP · Egyptian Pound", "value": "EGP"}, {"name": "MAD · Moroccan Dirham", "value": "MAD"}, {"name": "XOF · West African CFA", "value": "XOF"}, {"name": "XAF · Central African CFA", "value": "XAF"}, {"name": "ETB · Ethiopian Birr", "value": "ETB"}, {"name": "TZS · Tanzanian Shilling", "value": "TZS"}, {"name": "UGX · Ugandan Shilling", "value": "UGX"}, {"name": "RWF · Rwandan Franc", "value": "RWF"}, {"name": "ZMW · Zambian Kwacha", "value": "ZMW"}, {"name": "BWP · Botswana Pula", "value": "BWP"}, {"name": "MUR · Mauritian Rupee", "value": "MUR"}, {"name": "DZD · Algerian Dinar", "value": "DZD"}, {"name": "TND · Tunisian Dinar", "value": "TND"}, {"name": "INR · Indian Rupee", "value": "INR"}, {"name": "PKR · Pakistani Rupee", "value": "PKR"}, {"name": "BDT · Bangladeshi Taka", "value": "BDT"}, {"name": "LKR · Sri Lankan Rupee", "value": "LKR"}, {"name": "HKD · Hong Kong Dollar", "value": "HKD"}, {"name": "SGD · Singapore Dollar", "value": "SGD"}, {"name": "KRW · South Korean Won", "value": "KRW"}, {"name": "TWD · Taiwan Dollar", "value": "TWD"}, {"name": "THB · Thai Baht", "value": "THB"}, {"name": "MYR · Malaysian Ringgit", "value": "MYR"}, {"name": "IDR · Indonesian Rupiah", "value": "IDR"}, {"name": "PHP · Philippine Peso", "value": "PHP"}, {"name": "VND · Vietnamese Dong", "value": "VND"}, {"name": "AED · UAE Dirham", "value": "AED"}, {"name": "SAR · Saudi Riyal", "value": "SAR"}, {"name": "QAR · Qatari Riyal", "value": "QAR"}, {"name": "KWD · Kuwaiti Dinar", "value": "KWD"}, {"name": "BHD · Bahraini Dinar", "value": "BHD"}, {"name": "OMR · Omani Rial", "value": "OMR"}, {"name": "JOD · Jordanian Dinar", "value": "JOD"}, {"name": "ILS · Israeli Shekel", "value": "ILS"}, {"name": "TRY · Turkish Lira", "value": "TRY"}, {"name": "RUB · Russian Ruble", "value": "RUB"}, {"name": "UAH · Ukrainian Hryvnia", "value": "UAH"}, {"name": "PLN · Polish Zloty", "value": "PLN"}, {"name": "CZK · Czech Koruna", "value": "CZK"}, {"name": "HUF · Hungarian Forint", "value": "HUF"}, {"name": "RON · Romanian Leu", "value": "RON"}, {"name": "SEK · Swedish Krona", "value": "SEK"}, {"name": "NOK · Norwegian Krone", "value": "NOK"}, {"name": "DKK · Danish Krone", "value": "DKK"}, {"name": "ISK · Icelandic Krona", "value": "ISK"}, {"name": "MXN · Mexican Peso", "value": "MXN"}, {"name": "BRL · Brazilian Real", "value": "BRL"}, {"name": "ARS · Argentine Peso", "value": "ARS"}, {"name": "CLP · Chilean Peso", "value": "CLP"}, {"name": "COP · Colombian Peso", "value": "COP"}, {"name": "PEN · Peruvian Sol", "value": "PEN"}, {"name": "JMD · Jamaican Dollar", "value": "JMD"}, {"name": "TTD · Trinidad Dollar", "value": "TTD"}]}})json";

#endif
