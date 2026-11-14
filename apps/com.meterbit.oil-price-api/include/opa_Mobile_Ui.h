/**
 * @file opa_Mobile_Ui.h
 * @brief Mobile UI of the Oil Price API app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in oilPriceApi.cpp:
 *   0 {"code"}                    featured code (added to the watchlist if it is not on it)
 *   1 {"watch": [codes]}          watchlist, 1 to 12 codes
 *   2 {"range": 0-3}              chart range: 24 hours, 7 days, 30 days, 1 year
 *   3 {"api_key"}                 OilPriceAPI key
 *   4 {"share"} / {"floor"}       share of the quota, fastest refresh
 *   5 {"cycle"} / {"seconds"}     page rotation
 *   6 {}                          refresh now
 * The argument names are also the keys of the values oilPriceApi.cpp reports, which the phone shows on opening; the
 * status panel's keys (plan, keyText, left, limit, window, reset, pace, features, lockedFeatures, updated, paid) are
 * reported too, and pushed when new data arrives.
 * The "commodities" dataset lists the codes of opa_Model.cpp's catalogue. Slider limits match opa_Config.h.
 * Success toasts come before failure toasts: older phone apps match on the command number alone.
 */
#ifndef OPA_MOBILE_UI_H
#define OPA_MOBILE_UI_H

static const char OPA_UI_MANIFEST[] = R"json({
  "appID": "4/5",
  "name": "Oil Price API",
  "about": "Oil, gas, fuel, coal and carbon prices from OilPriceAPI. Works on the Free plan; paid plans add price history, longer change windows, the Brent futures curve, US rig counts and OPEC output.",
  "author": "Meterbit Cyb",
  "version": "v1.1.0"
})json";

static const char OPA_UI_API[] = R"json({
  "widgets": [
    {
      "type": "richTextBlock",
      "title": "Oil Price API",
      "blocks": [
        {
          "type": "paragraph",
          "text": "Prices of up to 12 commodities from OilPriceAPI, refreshed as often as your plan's quota allows and paced to last until it resets."
        },
        {
          "type": "note",
          "text": "Without a key, Pixlpal shows OilPriceAPI's public demo prices every 90 minutes."
        }
      ]
    },
    {
      "type": "statusPanel",
      "title": "Plan and Quota",
      "emptyText": "Launch Oil Price API on Pixlpal to see your plan and quota.",
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
          "value": "{left} of {limit} left {window}",
          "state": "{quotaState}",
          "meter": {
            "value": "left",
            "max": "limit"
          }
        },
        {
          "label": "Quota resets",
          "ago": "reset"
        },
        {
          "label": "Prices",
          "value": "{pace}",
          "state": "{paceState}"
        },
        {
          "label": "Includes",
          "value": "{features}",
          "state": "ok"
        },
        {
          "label": "Not in plan",
          "value": "{lockedFeatures}",
          "state": "off"
        },
        {
          "label": "Updated",
          "ago": "updated"
        }
      ]
    },
    {
      "type": "section",
      "id": "prices",
      "title": "Prices",
      "widgets": [
        {
          "type": "dropDownList",
          "title": "Watchlist",
          "dataset": "commodities",
          "buttonLabel": "CHOOSE UP TO 12",
          "sendAs": "array",
          "command": {
            "app_command": 1,
            "args": [
              "watch"
            ]
          },
          "itemTapCommand": {
            "app_command": 0,
            "args": [
              "code"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "Tap a code above to put it on the big display. All codes go in one request, so a longer list costs no more quota."
            }
          ]
        },
        {
          "type": "dropDownTextField",
          "title": "Featured on the Big Display",
          "dataset": "commodities",
          "dropDownItemCount": 6,
          "defaultValue": "BRENT_CRUDE_USD",
          "command": {
            "app_command": 0,
            "args": [
              "code"
            ]
          }
        },
        {
          "type": "radioGroup",
          "title": "Chart Range",
          "default": 2,
          "options": [
            {
              "label": "24 hours",
              "value": 0
            },
            {
              "label": "7 days",
              "value": 1
            },
            {
              "label": "30 days",
              "value": 2
            },
            {
              "label": "1 year",
              "value": 3
            }
          ],
          "command": {
            "app_command": 2,
            "args": [
              "range"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "note",
              "text": "History comes with paid plans. On the Free plan, Pixlpal charts the prices it has received itself."
            }
          ]
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
        }
      ]
    },
    {
      "type": "section",
      "id": "quota",
      "title": "Refresh and Quota",
      "collapsed": true,
      "widgets": [
        {
          "type": "slider",
          "title": "Share of the Quota for this Pixlpal",
          "min": 10,
          "max": 100,
          "step": 10,
          "default": 90,
          "suffix": "%",
          "command": {
            "app_command": 4,
            "args": [
              "share"
            ]
          }
        },
        {
          "type": "slider",
          "title": "Fastest Refresh",
          "min": 2,
          "max": 60,
          "default": 5,
          "suffix": "Mins",
          "command": {
            "app_command": 4,
            "args": [
              "floor"
            ]
          }
        },
        {
          "type": "borderButton",
          "label": "REFRESH NOW",
          "command": {
            "app_command": 6
          },
          "confirm": {
            "title": "Refresh now?",
            "text": "This uses one request from your OilPriceAPI quota.",
            "ok": "Refresh"
          }
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "API Key and Plans",
      "subtitle": "Your OilPriceAPI key and what each plan adds",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "OilPriceAPI Key",
          "secret": true,
          "emptyHint": "Paste your OilPriceAPI key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": {
            "app_command": 3,
            "args": [
              "api_key"
            ]
          }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Get a free key, no card needed: "
                },
                {
                  "text": "oilpriceapi.com/signup",
                  "link": "https://www.oilpriceapi.com/auth/signup"
                },
                {
                  "text": ". Confirm the e-mail OilPriceAPI sends you: until you do, the key stops after 50 requests."
                }
              ]
            }
          ]
        },
        {
          "type": "richTextBlock",
          "title": "Plans",
          "blocks": [
            {
              "type": "paragraph",
              "text": "Upgrade on OilPriceAPI's site and keep your key: Pixlpal reads the plan from it, and the extra pages appear by themselves. A new key just needs pasting above."
            },
            {
              "type": "subheading",
              "text": "Free: 50 requests a day"
            },
            {
              "type": "bullet",
              "text": "Prices about every 30 minutes, with the 24-hour change."
            },
            {
              "type": "subheading",
              "text": "Developer: $24 a month, 10,000 requests"
            },
            {
              "type": "bullet",
              "text": "Prices every 5 minutes, and a year of history for the chart."
            },
            {
              "type": "subheading",
              "text": "Starter: $49 a month, 50,000 requests"
            },
            {
              "type": "bullet",
              "text": "Adds the 7-day change and 5 years of history."
            },
            {
              "type": "subheading",
              "text": "Professional: $99 a month, 250,000 requests"
            },
            {
              "type": "bullet",
              "text": "Adds the 30-day change and the ICE Brent futures curve."
            },
            {
              "type": "subheading",
              "text": "Scale: $299 a month, 1,000,000 requests"
            },
            {
              "type": "bullet",
              "text": "Adds the 90-day change, US rig counts by basin, and OPEC output with Nigeria against its quota."
            },
            {
              "type": "note",
              "text": "Prices and limits as published by OilPriceAPI in November 2026. Check the current ones before you buy. Annual billing is 20% less."
            },
            {
              "type": "paragraph",
              "spans": [
                {
                  "text": "Compare plans: "
                },
                {
                  "text": "oilpriceapi.com/pricing",
                  "link": "https://www.oilpriceapi.com/pricing"
                }
              ]
            }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data and Terms",
      "blocks": [
        {
          "type": "paragraph",
          "spans": [
            {
              "text": "Data from OilPriceAPI ("
            },
            {
              "text": "oilpriceapi.com",
              "link": "https://www.oilpriceapi.com/"
            },
            {
              "text": "), under its terms: use your own key, for your own use, and keep it private. Series that come from the U.S. Energy Information Administration are credited to it on the display."
            }
          ]
        },
        {
          "type": "note",
          "text": "This product is not affiliated with or endorsed by OilPriceAPI. Prices are for information only, not for trading or settlement, and are not investment advice."
        }
      ]
    }
  ],
  "toasts": [
    {
      "when": {
        "app_command": 0
      },
      "text": "Featured"
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
      "text": "Chart range saved"
    },
    {
      "when": {
        "app_command": 3
      },
      "text": "API key saved"
    },
    {
      "when": {
        "app_command": 4
      },
      "text": "Saved"
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
      "text": "Refreshing"
    },
    {
      "when": {
        "app_command": 0,
        "response": 0
      },
      "text": "Unknown code, or the watchlist is full"
    },
    {
      "when": {
        "app_command": 1,
        "response": 0
      },
      "text": "Choose 1 to 12 codes from the list"
    },
    {
      "when": {
        "app_command": 2,
        "response": 0
      },
      "text": "Unknown chart range"
    },
    {
      "when": {
        "app_command": 3,
        "response": 0
      },
      "text": "That does not look like an API key"
    },
    {
      "when": {
        "app_command": 4,
        "response": 0
      },
      "text": "Value out of range"
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
        "app_command": 6,
        "response": 0
      },
      "text": "Refreshed recently, or the quota is used up"
    }
  ]
})json";

static const char OPA_UI_DATA[] = R"json({
  "datasets": {
    "commodities": [
      { "name": "Brent crude", "value": "BRENT_CRUDE_USD" },
      { "name": "WTI crude", "value": "WTI_USD" },
      { "name": "Dubai crude", "value": "DUBAI_CRUDE_USD" },
      { "name": "OPEC reference basket", "value": "OPEC_BASKET_USD" },
      { "name": "Urals crude", "value": "URALS_CRUDE_USD" },
      { "name": "Western Canadian Select", "value": "WCS_CRUDE_USD" },
      { "name": "Azeri Light", "value": "AZERI_LIGHT_USD" },
      { "name": "Basrah Medium", "value": "BASRAH_MEDIUM_USD" },
      { "name": "Bonny Light, Nigeria (paid plans)", "value": "BONNY_LIGHT_USD" },
      { "name": "Henry Hub natural gas", "value": "NATURAL_GAS_USD" },
      { "name": "UK natural gas (NBP)", "value": "NATURAL_GAS_GBP" },
      { "name": "Dutch TTF gas (paid plans)", "value": "DUTCH_TTF_EUR" },
      { "name": "JKM LNG, Asia (paid plans)", "value": "JKM_LNG_USD" },
      { "name": "RBOB gasoline", "value": "GASOLINE_RBOB_USD" },
      { "name": "Gasoline", "value": "GASOLINE_USD" },
      { "name": "Ultra low sulfur diesel", "value": "DIESEL_USD" },
      { "name": "Heating oil", "value": "HEATING_OIL_USD" },
      { "name": "US retail diesel", "value": "DIESEL_RETAIL_USD" },
      { "name": "Jet fuel (paid plans)", "value": "JET_FUEL_USD" },
      { "name": "Propane, Mont Belvieu", "value": "PROPANE_MONT_BELVIEU_USD" },
      { "name": "Thermal coal", "value": "COAL_USD" },
      { "name": "Newcastle coal", "value": "NEWCASTLE_COAL_USD" },
      { "name": "EU carbon allowance", "value": "EU_CARBON_EUR" },
      { "name": "UK carbon allowance", "value": "UK_CARBON_GBP" },
      { "name": "Gold", "value": "GOLD_USD" },
      { "name": "Silver", "value": "SILVER_USD" },
      { "name": "Euro in US dollars", "value": "EUR_USD" },
      { "name": "Pound in US dollars", "value": "GBP_USD" }
    ]
  }
})json";

#endif
