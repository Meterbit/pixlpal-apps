/**
 * @file tdw_Mobile_Ui.h
 * @brief Tides & Water's phone UI {3,3} (docs/MOBILE_UI_FILES, format 2).
 *
 *   0 {"query"} places (answered as 248 "places")   1 {"place": {name, value: "lat,lng"}}
 *   2 {"query"} rivers (248 "rivers")                3 {"riverPlace": {name, value}} or {} (the place itself)
 *   4 {"source"}   5 {"datum"} | {"imperial"} | {"windUnit"}   6 {"p0".."p5"} | {"cycle"} | {"seconds"}
 *   7 {"alertOn"} | {"highWaterM"} | {"highWaterFt"} | {"waveAlert"} | {"gustAlert"} | {"riverAlert"} | {"stormAlert"}
 *     | {"leadHours"} | {"phoneAlerts"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
 *   8 {"worldTidesKey"} | {"stormglassKey"} (secrets)   9 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
 *   10 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"} (secrets)   11 {} refresh now   12 {} a test alert
 */
#ifndef TDW_MOBILE_UI_H
#define TDW_MOBILE_UI_H

static const char TDW_UI_MANIFEST[] = R"json({
  "appID": "3/3",
  "name": "Tides & Water",
  "about": "For homes by the sea, a lagoon or a river: the tide now and the next highs and lows, the sea (waves, swell, water temperature, currents), wind, rain and storms, the river's flow against its last 10 years, sun, moon and spring tides. Alerts on Pixlpal and on your phones for high water, rough sea, strong wind, thunder and a rising river. Free sources everywhere; official NOAA tides in US waters; WorldTides or Stormglass with your own key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TDW_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "The Water", "source": "status", "emptyText": "Open Tides & Water on Pixlpal to see the water here." },
    { "type": "statusPanel", "title": "Alerts", "source": "alertRows", "emptyText": "No alerts", "visibleWhen": { "arg": "alertOn", "equals": 1 } },
    {
      "type": "searchPicker", "title": "Place", "search": "places", "multi": false, "minChars": 2,
      "emptyHint": "A town or beach, or coordinates: 6.45, 3.39",
      "noResults": "No place of that name",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["place"] }
    },
    {
      "type": "section", "id": "tides", "title": "Tides", "widgets": [
        {
          "type": "radioGroup", "title": "Tides From", "default": 0,
          "options": [
            { "label": "Auto: the best source here (Recommended)", "value": 0 },
            { "label": "NOAA tide stations (US waters, free)", "value": 1 },
            { "label": "WorldTides (your key, worldwide)", "value": 2 },
            { "label": "Stormglass (your key, worldwide)", "value": 3 },
            { "label": "Open-Meteo ocean model (free, everywhere)", "value": 4 }
          ],
          "command": { "app_command": 4, "args": ["source"] }
        },
        {
          "type": "radioGroup", "title": "Heights Above", "default": 0,
          "options": [
            { "label": "Chart datum: lowest tides, as on charts and tide tables", "value": 0 },
            { "label": "Mean sea level", "value": 1 }
          ],
          "command": { "app_command": 5, "args": ["datum"] }
        },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Auto takes the nearest NOAA station within 40 km in US waters, then WorldTides or Stormglass if you added a key, then Open-Meteo's ocean model." },
          { "type": "bullet", "text": "The ocean model works everywhere for free, from 8 km cells and above mean sea level. In lagoons, creeks and harbours it can be off by an hour or more: a key gives tide-gauge predictions there." },
          { "type": "note", "text": "Not for navigation. Use official tide tables at sea." }
        ] },
        { "type": "textField", "title": "WorldTides API Key", "secret": true, "emptyHint": "From worldtides.info/developer", "filledHint": "Saved on Pixlpal: paste a new one to replace it", "command": { "app_command": 8, "args": ["worldTidesKey"] } },
        { "type": "textField", "title": "Stormglass API Key", "secret": true, "emptyHint": "From stormglass.io", "filledHint": "Saved on Pixlpal: paste a new one to replace it", "command": { "app_command": 8, "args": ["stormglassKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "WorldTides: Pixlpal spends 2 credits a day (about 730 a year; the $3.99 yearly plan has 1,000). Stormglass: 2 of the free plan's 10 requests a day." },
          { "type": "paragraph", "spans": [ { "text": "Keys: " }, { "text": "worldtides.info", "link": "https://www.worldtides.info/developer" }, { "text": " and " }, { "text": "stormglass.io", "link": "https://stormglass.io" } ] }
        ] }
      ]
    },
    {
      "type": "section", "id": "river", "title": "River", "collapsed": true, "widgets": [
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "Pixlpal follows the biggest river within 5 km of the place. If your river is elsewhere (upstream, or the town by it), pick it here." }
        ] },
        {
          "type": "searchPicker", "title": "River Place (Empty: the Place Above)", "search": "rivers", "multi": false, "minChars": 2,
          "emptyHint": "A town on the river, or coordinates",
          "noResults": "No place of that name",
          "searchCommand": { "app_command": 2, "args": ["query"] },
          "pickCommand": { "app_command": 3, "args": ["riverPlace"] }
        },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "River flow is from the GloFAS flood model (5 km), judged against the same spot's last 5 years: High is the top 10% of days, Very high the top 1%." }
        ] }
      ]
    },
    {
      "type": "section", "id": "alerts", "title": "Alerts", "subtitle": "On Pixlpal, and on your phones if you like", "collapsed": true, "widgets": [
        { "type": "switch", "title": "Alerts", "default": true, "command": { "app_command": 7, "args": ["alertOn"] } },
        { "type": "slider", "title": "High Water From (0: Off)", "min": 0, "max": 10, "step": 0.1, "decimals": 1, "default": 0, "suffix": " m",
          "command": { "app_command": 7, "args": ["highWaterM"] }, "visibleWhen": { "arg": "imperial", "notEquals": 1 } },
        { "type": "slider", "title": "High Water From (0: Off)", "min": 0, "max": 32, "step": 0.5, "decimals": 1, "default": 0, "suffix": " ft",
          "command": { "app_command": 7, "args": ["highWaterFt"] }, "visibleWhen": { "arg": "imperial", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "Use the height where water reaches your road, jetty or garden, above the datum chosen under Tides. The tide page shows it as a red line." }
        ] },
        {
          "type": "radioGroup", "title": "Waves From", "default": 25,
          "options": [
            { "label": "Off", "value": 0 }, { "label": "1 m (3 ft)", "value": 10 }, { "label": "1.5 m (5 ft)", "value": 15 },
            { "label": "2 m (6.5 ft)", "value": 20 }, { "label": "2.5 m (8 ft): rough sea", "value": 25 }, { "label": "4 m (13 ft): very rough", "value": 40 }
          ],
          "command": { "app_command": 7, "args": ["waveAlert"] }
        },
        {
          "type": "radioGroup", "title": "Wind Gusts From", "default": 60,
          "options": [
            { "label": "Off", "value": 0 }, { "label": "40 km/h (22 kn, 25 mph)", "value": 40 }, { "label": "60 km/h (32 kn, 37 mph)", "value": 60 },
            { "label": "75 km/h (40 kn, 47 mph): gale", "value": 75 }, { "label": "90 km/h (49 kn, 56 mph): strong gale", "value": 90 }
          ],
          "command": { "app_command": 7, "args": ["gustAlert"] }
        },
        {
          "type": "radioGroup", "title": "River From", "default": 4,
          "options": [
            { "label": "Off", "value": 0 }, { "label": "High (top 10% of days)", "value": 3 },
            { "label": "Very high (top 1%)", "value": 4 }, { "label": "Higher than any day in 10 years", "value": 5 }
          ],
          "command": { "app_command": 7, "args": ["riverAlert"] }
        },
        { "type": "switch", "title": "Thunderstorms", "default": true, "command": { "app_command": 7, "args": ["stormAlert"] } },
        {
          "type": "radioGroup", "title": "Warn This Long Before", "default": 6,
          "options": [ { "label": "3 hours", "value": 3 }, { "label": "6 hours", "value": 6 }, { "label": "12 hours", "value": 12 }, { "label": "24 hours", "value": 24 } ],
          "command": { "app_command": 7, "args": ["leadHours"] }
        },
        { "type": "switch", "title": "Send Alerts to Phones", "default": false, "command": { "app_command": 7, "args": ["phoneAlerts"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Free, over the home Wi-Fi, through ntfy, Telegram or WhatsApp (set up below). Each alert is sent once." },
          { "type": "bullet", "text": "Pixlpal watches while Tides & Water is on its display (also when it comes round in app cycling)." }
        ], "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "ntfy (Recommended)", "default": false, "command": { "app_command": 7, "args": ["ntfyOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install the free ntfy app (Android or iPhone), tap +, and subscribe to the topic shown under Alerts above. No account is needed." },
          { "type": "paragraph", "spans": [ { "text": "ntfy: " }, { "text": "ntfy.sh", "link": "https://ntfy.sh" } ] }
        ], "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Topic (Optional: Your Own)", "emptyHint": "A long, hard-to-guess name", "command": { "app_command": 9, "args": ["ntfyTopic"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "ntfy.sh", "command": { "app_command": 9, "args": ["ntfyServer"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Access Token (Protected Topics Only)", "emptyHint": "tk_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 10, "args": ["ntfyToken"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "switch", "title": "Telegram", "default": false, "command": { "app_command": 7, "args": ["telegramOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "In Telegram, talk to @BotFather: /newbot gives a bot token. Send your bot a message (or add it to a family group), then find the chat ID with @userinfobot (a group's starts with -100)." }
        ], "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Bot Token", "emptyHint": "123456:ABC...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 10, "args": ["telegramToken"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Chat or Group ID", "emptyHint": "123456789", "command": { "app_command": 9, "args": ["telegramChat"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "switch", "title": "WhatsApp (CallMeBot)", "default": false, "command": { "app_command": 7, "args": ["whatsappOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "CallMeBot is free for personal use: from your phone, send \"I allow callmebot to send me messages\" to the number on callmebot.com, and it replies with your API key." },
          { "type": "paragraph", "spans": [ { "text": "Instructions: " }, { "text": "callmebot.com", "link": "https://www.callmebot.com/blog/free-api-whatsapp-messages/" } ] }
        ], "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "WhatsApp Number", "emptyHint": "+2348012345678", "command": { "app_command": 9, "args": ["whatsappPhone"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "CallMeBot API Key", "emptyHint": "123456", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "command": { "app_command": 10, "args": ["whatsappKey"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "borderButton", "label": "SEND A TEST ALERT", "command": { "app_command": 12 }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } }
      ]
    },
    {
      "type": "section", "id": "display", "title": "Units and Display", "collapsed": true, "widgets": [
        { "type": "radioGroup", "title": "Heights and Temperature", "default": 0,
          "options": [ { "label": "Metres, °C", "value": 0 }, { "label": "Feet, °F", "value": 1 } ],
          "command": { "app_command": 5, "args": ["imperial"] } },
        { "type": "radioGroup", "title": "Wind", "default": 0,
          "options": [ { "label": "km/h", "value": 0 }, { "label": "Knots", "value": 1 }, { "label": "mph", "value": 2 }, { "label": "m/s", "value": 3 } ],
          "command": { "app_command": 5, "args": ["windUnit"] } },
        { "type": "switch", "title": "Page: Tide Now", "default": true, "command": { "app_command": 6, "args": ["p0"] } },
        { "type": "switch", "title": "Page: Tide Times", "default": true, "command": { "app_command": 6, "args": ["p1"] } },
        { "type": "switch", "title": "Page: Sea", "default": true, "command": { "app_command": 6, "args": ["p2"] } },
        { "type": "switch", "title": "Page: Wind & Weather", "default": true, "command": { "app_command": 6, "args": ["p3"] } },
        { "type": "switch", "title": "Page: River", "default": true, "command": { "app_command": 6, "args": ["p4"] } },
        { "type": "switch", "title": "Page: Sun & Moon", "default": true, "command": { "app_command": 6, "args": ["p5"] } },
        { "type": "switch", "title": "Turn Pages by Themselves", "default": true, "command": { "app_command": 6, "args": ["cycle"] } },
        { "type": "slider", "title": "Time per Page", "min": 4, "max": 60, "default": 12, "suffix": " s", "command": { "app_command": 6, "args": ["seconds"] }, "visibleWhen": { "arg": "cycle", "equals": 1 } },
        { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 11 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "On Pixlpal: click or turn for the next page, double click to refresh, hold to start or stop turning pages. Pages without data here (no sea, no river) are skipped." }
        ] }
      ]
    },
    {
      "type": "richTextBlock", "title": "Data", "blocks": [
        { "type": "paragraph", "text": "Sea, weather and river: Open-Meteo.com (CC BY 4.0), with Météo-France, ECMWF, DWD and NOAA wave models and the Copernicus GloFAS flood model. Tides: NOAA CO-OPS, WorldTides or Stormglass when used. Place search: GeoNames." },
        { "type": "note", "text": "Not for navigation or safety decisions on its own: follow official warnings." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Searching" },
    { "when": { "app_command": 1 }, "text": "Place saved" },
    { "when": { "app_command": 2 }, "text": "Searching" },
    { "when": { "app_command": 3 }, "text": "River place saved" },
    { "when": { "app_command": 8 }, "text": "Key saved" },
    { "when": { "app_command": 11 }, "text": "Refreshing" },
    { "when": { "app_command": 12 }, "text": "Test alert sent to your phones" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Tides & Water on Pixlpal to search by name (coordinates work any time)" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Open Tides & Water on Pixlpal to search by name (coordinates work any time)" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Pick a place from the search" },
    { "when": { "app_command": 8, "response": 0 }, "text": "That does not look like a key" },
    { "when": { "app_command": 9, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 11, "response": 0 }, "text": "Open Tides & Water on Pixlpal first" },
    { "when": { "app_command": 12, "response": 0 }, "text": "Switch on ntfy, Telegram or WhatsApp first, and open the app on Pixlpal" }
  ]
})json";

#endif
