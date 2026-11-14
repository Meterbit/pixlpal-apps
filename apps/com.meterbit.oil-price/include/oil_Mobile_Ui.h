/**
 * @file oil_Mobile_Ui.h
 * @brief Mobile UI of the Oil price app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES).
 *
 * Each widget's command reaches the handler with the same number in oil_Price.cpp:
 *   0 {"code"}                          benchmark on screen
 *   1 {"priceMinutes"} / {"newsMinutes"} refresh intervals
 *   2 {"cycle"} / {"seconds"}           page rotation
 *   3 {"api_key"}                       EIA API key
 *   4 {}                                refresh now
 * The argument names are also the keys of the values oil_Price.cpp reports, which the phone shows on opening.
 * Slider limits match oil_Config.h. Success toasts come before failure toasts: older phone apps match on the
 * command number alone and stop at the first rule.
 */
#ifndef OIL_MOBILE_UI_H
#define OIL_MOBILE_UI_H

static const char OIL_UI_MANIFEST[] = R"json({
  "appID": "4/4",
  "name": "Oil Price",
  "about": "Crude oil, fuel and natural gas benchmarks, Nigerian and OPEC crude output, and energy headlines from the U.S. Energy Information Administration (EIA).",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char OIL_UI_API[] = R"json({
  "widgets": [
    {
      "type": "richTextBlock",
      "title": "Oil Price",
      "blocks": [
        { "type": "paragraph", "text": "Brent, WTI, Henry Hub gas and U.S. fuel prices, with 3-month charts, Nigerian and OPEC crude output, and EIA energy headlines." },
        { "type": "note", "text": "Prices are EIA daily spot prices, published one to two business days late. They are not live quotes and not investment advice." }
      ]
    },
    {
      "type": "dropDownTextField",
      "title": "Benchmark on Screen",
      "dataset": "benchmarks",
      "dropDownItemCount": 6,
      "defaultValue": "BRENT",
      "command": { "app_command": 0, "args": ["code"] }
    },
    {
      "type": "switch",
      "title": "Rotate Pages",
      "default": true,
      "command": { "app_command": 2, "args": ["cycle"] }
    },
    {
      "type": "slider",
      "title": "Time per Page",
      "min": 4, "max": 60, "default": 10, "suffix": "Secs",
      "command": { "app_command": 2, "args": ["seconds"] }
    },
    {
      "type": "slider",
      "title": "Price Refresh",
      "min": 60, "max": 1440, "step": 30, "default": 180, "suffix": "Mins",
      "command": { "app_command": 1, "args": ["priceMinutes"] }
    },
    {
      "type": "slider",
      "title": "Headline Refresh",
      "min": 120, "max": 2880, "step": 60, "default": 360, "suffix": "Mins",
      "command": { "app_command": 1, "args": ["newsMinutes"] }
    },
    {
      "type": "textField",
      "title": "EIA API Key",
      "secret": true,
      "emptyHint": "Paste your free EIA API key",
      "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
      "command": { "app_command": 3, "args": ["api_key"] }
    },
    {
      "type": "richTextBlock",
      "blocks": [
        { "type": "paragraph", "spans": [
          { "text": "Prices need a free key from " },
          { "text": "eia.gov/opendata/register.php", "link": "https://www.eia.gov/opendata/register.php" },
          { "text": ". Headlines work without one." }
        ] }
      ]
    },
    { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 4 } },
    {
      "type": "richTextBlock",
      "title": "Data Sources",
      "blocks": [
        { "type": "paragraph", "spans": [
          { "text": "Data: U.S. Energy Information Administration (" },
          { "text": "eia.gov", "link": "https://www.eia.gov/" },
          { "text": "), open data API and Today in Energy. EIA data is in the public domain. EIA does not endorse this product." }
        ] },
        { "type": "note", "text": "Bonny Light is shown as EIA's monthly cost of U.S. imports of that crude, when EIA publishes it. EIA has no Bonny Light spot price." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Benchmark changed" },
    { "when": { "app_command": 1 }, "text": "Refresh interval saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "API key saved" },
    { "when": { "app_command": 4 }, "text": "Refreshing" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Unknown benchmark" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Interval out of range" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Value out of range" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Not a valid EIA key: letters and digits only" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Refreshed recently. Try again in a few minutes" }
  ]
})json";

static const char OIL_UI_DATA[] = R"json({
  "datasets": {
    "benchmarks": [
      { "name": "Brent (Europe spot)", "value": "BRENT" },
      { "name": "WTI (Cushing spot)", "value": "WTI" },
      { "name": "Henry Hub natural gas", "value": "HH GAS" },
      { "name": "Gasoline (NY Harbor)", "value": "GASOLINE" },
      { "name": "Diesel ULSD (NY Harbor)", "value": "DIESEL" },
      { "name": "Heating oil (NY Harbor)", "value": "HEAT OIL" },
      { "name": "Jet fuel (US Gulf Coast)", "value": "JET" },
      { "name": "Propane (Mont Belvieu)", "value": "PROPANE" },
      { "name": "Bonny Light (monthly, US import)", "value": "BONNY LT" },
      { "name": "Nigeria crude output", "value": "NIGERIA" },
      { "name": "OPEC crude output", "value": "OPEC" }
    ]
  }
})json";

#endif
