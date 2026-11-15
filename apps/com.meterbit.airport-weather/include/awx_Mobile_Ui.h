/**
 * @file awx_Mobile_Ui.h
 * @brief Airport Weather's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (airportWeather.cpp):
 *   0 {"station0" ... "station2": "EGLL"}   an airport's ICAO code (empty: removed)
 *   1 {"vis": 0|1}       visibility in statute miles (0) or kilometres (1)
 *   2 {"press": 0|1}     pressure in hPa (0) or inches of mercury (1)
 *   3 {}  fetch the reports now
 */
#ifndef AWX_MOBILE_UI_H
#define AWX_MOBILE_UI_H

static const char AWX_UI_MANIFEST[] = R"json({
  "name": "Airport Weather",
  "about": "An airport's official weather, decoded: the flight category (VFR, MVFR, IFR, LIFR), wind on a compass with gusts, visibility, clouds, temperature and pressure, the forecast (TAF) ahead, and the raw reports. From aviationweather.gov, free, for up to three airports.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char AWX_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.airport-weather.png", "height": 64, "alt": "Airport Weather" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "No airports yet. Add them below." },
    { "type": "borderButton", "label": "FETCH THE REPORTS NOW", "command": { "app_command": 3 } },
    {
      "type": "section",
      "id": "stations",
      "title": "Airports",
      "widgets": [
        { "type": "textField", "title": "Airport 1 (ICAO Code)", "emptyHint": "e.g. EGLL, KJFK, DNMM", "filledHint": "Change airport 1 (empty removes it)", "command": { "app_command": 0, "args": ["station0"] } },
        { "type": "textField", "title": "Airport 2", "emptyHint": "ICAO code", "filledHint": "Change airport 2", "command": { "app_command": 0, "args": ["station1"] } },
        { "type": "textField", "title": "Airport 3", "emptyHint": "ICAO code", "filledHint": "Change airport 3", "command": { "app_command": 0, "args": ["station2"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The four-letter ICAO code: EGLL is London Heathrow, KJFK New York JFK, DNMM Lagos. Any airport that issues METAR reports works." } ] }
      ]
    },
    {
      "type": "section",
      "id": "units",
      "title": "Units",
      "collapsed": true,
      "widgets": [
        { "type": "radioGroup", "title": "Visibility", "default": 0, "options": [ { "label": "Statute miles", "value": 0 }, { "label": "Kilometres", "value": 1 } ], "command": { "app_command": 1, "args": ["vis"] } },
        { "type": "radioGroup", "title": "Pressure", "default": 0, "options": [ { "label": "hPa (QNH)", "value": 0 }, { "label": "Inches of mercury", "value": 1 } ], "command": { "app_command": 2, "args": ["press"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "How It Reads",
      "blocks": [
        { "type": "bullet", "text": "VFR (green): ceiling above 3000 ft and visibility over 5 miles. MVFR (blue), IFR (red), LIFR (magenta) as they fall." },
        { "type": "bullet", "text": "On Pixlpal, turn the knob for the forecast and the raw reports; click for the next airport." },
        { "type": "note", "text": "Reports from the US National Weather Service's Aviation Weather Center (aviationweather.gov), every 10 minutes. Not for navigation: check official sources before flying." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Airport saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write a 4-letter ICAO code, such as EGLL" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Airport Weather on Pixlpal first" }
  ]
})json";

#endif
