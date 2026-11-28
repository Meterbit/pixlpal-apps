/**
 * @file iss_Mobile_Ui.h
 * @brief ISS Tracker's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (issTracker.cpp):
 *   0 {"query": "Lagos"}   a place search (Open-Meteo's free geocoding), answered as search "places"
 *   1 {"place": {"name": "Lagos, Nigeria", "value": "6.45407,3.39467"}}
 *   2 {"page": 0-2}        0 the map, 1 the passes, 2 the next pass in the sky
 *   3 {"cycle": 0-120}     seconds a page, 0 to stay on one
 *   4 {"visibleOnly": 0|1} list only the passes you can see
 *   5 {"chime": 0|1}       a chime 5 minutes before a pass you can see
 */
#ifndef ISS_MOBILE_UI_H
#define ISS_MOBILE_UI_H

static const char ISS_UI_MANIFEST[] = R"json({
  "name": "ISS Tracker",
  "about": "The International Space Station on a world map with day and night: where it is, where it goes next, and when it passes over your place, with the passes you can see with your own eyes.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char ISS_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.iss-tracker.png", "height": 64, "alt": "ISS Tracker" },
    {
      "type": "searchPicker",
      "title": "Your Place",
      "search": "places",
      "multi": false,
      "minChars": 2,
      "emptyHint": "Search a city, e.g. Lagos",
      "noResults": "No place of that name",
      "searchCommand": { "app_command": 0, "args": ["query"] },
      "pickCommand": { "app_command": 1, "args": ["place"] }
    },
    { "type": "statusPanel", "title": "The Station", "source": "now", "emptyText": "Open ISS Tracker on Pixlpal." },
    { "type": "statusPanel", "title": "Next Passes", "source": "passes", "emptyText": "Working them out." },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 0,
      "options": [
        { "label": "The World Map", "value": 0 },
        { "label": "The Passes Over Your Place", "value": 1 },
        { "label": "The Next Pass in Your Sky", "value": 2 }
      ],
      "command": { "app_command": 2, "args": ["page"] }
    },
    { "type": "slider", "title": "Change Page Every (0: Stay)", "min": 0, "max": 120, "step": 5, "default": 0, "suffix": " s", "command": { "app_command": 3, "args": ["cycle"] } },
    { "type": "switch", "title": "Only Passes You Can See", "default": 0, "command": { "app_command": 4, "args": ["visibleOnly"] } },
    { "type": "switch", "title": "Chime 5 Minutes Before a Pass You Can See", "default": 1, "command": { "app_command": 5, "args": ["chime"] } },
    {
      "type": "richTextBlock",
      "title": "How It Works",
      "blocks": [
        { "type": "bullet", "text": "Pixlpal fetches the station's orbit (its two-line elements) from wheretheiss.at twice a day, and works out the rest itself: its position every second, the track ahead and the passes for the next two days." },
        { "type": "bullet", "text": "A pass you can see: the station is in sunlight while your sky is dark, just after sunset or before sunrise. It looks like a bright star moving steadily, with no blinking lights." },
        { "type": "bullet", "text": "Passes lower than 10 degrees are left out: trees and buildings usually hide them." },
        { "type": "note", "text": "Turn the knob or click to change the page. Times are in Pixlpal's time zone." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Place saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Pixlpal could not search now" }
  ]
})json";

#endif
