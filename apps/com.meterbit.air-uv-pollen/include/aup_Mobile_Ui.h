/**
 * @file aup_Mobile_Ui.h
 * @brief Air, UV & Pollen's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (airUvPollen.cpp):
 *   0 {"query": "Lagos"}       a place search (Open-Meteo's free geocoding), answered as 248 ("places")
 *   1 {"place": {"name", "value": "lat,lon"}}   the place
 *   2 {"scale": 0|1}           the US AQI or the European AQI
 *   3 {}                       check now
 */
#ifndef AUP_MOBILE_UI_H
#define AUP_MOBILE_UI_H

static const char AUP_UI_MANIFEST[] = R"json({
  "name": "Air, UV & Pollen",
  "about": "Today's outdoor conditions where you live: the air quality index, the UV index with the hours to wear sunscreen, and grass, tree and weed pollen (in Europe). Free Open-Meteo data, no key needed.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char AUP_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.air-uv-pollen.png", "height": 64, "alt": "Air, UV & Pollen" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Choose your place below." },
    {
      "type": "searchPicker",
      "title": "Place",
      "search": "places",
      "multi": false,
      "minChars": 2,
      "emptyHint": "Search a city, such as Lagos",
      "noResults": "No place of that name",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["place"] }
    },
    { "type": "radioGroup", "title": "Air Quality Index", "default": 0, "options": [ { "label": "US AQI (0-500)", "value": 0 }, { "label": "European AQI (0-100+)", "value": 1 } ], "command": { "app_command": 2, "args": ["scale"] } },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 3 } },
    {
      "type": "richTextBlock",
      "title": "What It Shows",
      "blocks": [
        { "type": "bullet", "text": "Air: the index now, in its scale's words (good, moderate...), and fine particles (PM2.5)." },
        { "type": "bullet", "text": "UV: the index now and today's highest. Sunscreen hours are when it is 3 or more, as the World Health Organization advises." },
        { "type": "bullet", "text": "Pollen: tree (alder, birch, olive), grass and weed (mugwort, ragweed), as low, moderate, high or very high. Open-Meteo has pollen for Europe only." },
        { "type": "note", "text": "Forecast data from Open-Meteo (CAMS), checked every 30 minutes. Your place is sent to Open-Meteo only." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Place saved" },
    { "when": { "app_command": 3 }, "text": "Checking now" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Air, UV & Pollen on Pixlpal first" }
  ]
})json";

#endif
