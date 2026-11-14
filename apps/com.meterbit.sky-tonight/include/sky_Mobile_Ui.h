/**
 * @file sky_Mobile_Ui.h
 * @brief Sky Tonight's phone page {3,5} (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (skyTonight.cpp):
 *   0 {"query": "Lagos"}   a place search (Open-Meteo's free geocoding), answered as command 248 ("places")
 *   1 {"place": {"name": "Lagos, Nigeria", "value": "6.45407,3.39467"}}
 *   2 {"page": 0-2}        0 sun and moon, 1 the space station, 2 aurora
 *   3 {"cycle": 0-120}     seconds a page, 0 to stay on one
 */
#ifndef SKY_MOBILE_UI_H
#define SKY_MOBILE_UI_H

static const char SKY_UI_MANIFEST[] = R"json({
  "appID": "3/5",
  "name": "Sky Tonight",
  "about": "The sky over your place: the sun's path today with sunrise and sunset, the moon's phase and when it rises, where the International Space Station is right now, and the chance of seeing the northern or southern lights.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SKY_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:skyTonight.png", "height": 64, "alt": "Sky Tonight" },
    {
      "type": "searchPicker",
      "title": "Place",
      "search": "places",
      "multi": false,
      "minChars": 2,
      "emptyHint": "Search a city, e.g. Lagos",
      "noResults": "No place of that name",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["place"] }
    },
    { "type": "statusPanel", "title": "The Sky Today", "source": "sky", "emptyText": "Choose a place." },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 0,
      "options": [
        { "label": "Sun and Moon", "value": 0 },
        { "label": "The Space Station Now", "value": 1 },
        { "label": "Aurora Chance", "value": 2 }
      ],
      "command": { "app_command": 2, "args": ["page"] }
    },
    { "type": "slider", "title": "Change Page Every (0: Stay)", "min": 0, "max": 120, "step": 5, "default": 20, "suffix": " s", "command": { "app_command": 3, "args": ["cycle"] } },
    {
      "type": "richTextBlock",
      "title": "Where It Comes From",
      "blocks": [
        { "type": "bullet", "text": "The sun and the moon are worked out on Pixlpal from the place and the time: no internet needed. Times are in Pixlpal's time zone." },
        { "type": "bullet", "text": "The space station's position: wheretheiss.at, every 10 seconds on its page." },
        { "type": "bullet", "text": "The aurora: the planetary K index from NOAA's Space Weather Prediction Center, every 10 minutes. The chance at your place comes from how far it is from the equator: a guide, not a forecast." },
        { "type": "note", "text": "Turn the knob to change the page." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Place saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Pixlpal could not search now" }
  ]
})json";

#endif
