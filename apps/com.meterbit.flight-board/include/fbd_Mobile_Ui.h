/**
 * @file fbd_Mobile_Ui.h
 * @brief Flight Board's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (flightBoard.cpp):
 *   0 {"airport": "EGLL"}     ICAO (4 letters) or IATA (3) code
 *   1 {"dir": 0|1}            departures (0) or arrivals (1)
 *   2 {"key": "..."}          the owner's RapidAPI key for AeroDataBox (empty: forget it)
 *   3 {"minutes": 15-720}     how often the board is updated
 *   4 {"hours": 1-6}          how far ahead it shows
 *   5 {}                      update it now
 */
#ifndef FBD_MOBILE_UI_H
#define FBD_MOBILE_UI_H

static const char FBD_UI_MANIFEST[] = R"json({
  "name": "Flight Board",
  "about": "An airport's departures or arrivals on a split-flap board: time, flight, destination, gate and status (on time, delayed, boarding, departed, cancelled). From AeroDataBox, with your own RapidAPI key (its free plan updates the board a few times a day).",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char FBD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.flight-board.png", "height": 64, "alt": "Flight Board" },
    { "type": "statusPanel", "title": "The Board", "source": "board" },
    { "type": "borderButton", "label": "UPDATE THE BOARD NOW", "command": { "app_command": 5 } },
    { "type": "textField", "title": "Airport (ICAO or IATA Code)", "emptyHint": "e.g. EGLL or LHR, DNMM or LOS", "filledHint": "Change the airport", "command": { "app_command": 0, "args": ["airport"] } },
    { "type": "radioGroup", "title": "Show", "default": 0, "options": [ { "label": "Departures", "value": 0 }, { "label": "Arrivals", "value": 1 } ], "command": { "app_command": 1, "args": ["dir"] } },
    {
      "type": "section",
      "id": "data",
      "title": "Flight Data",
      "widgets": [
        { "type": "textField", "title": "RapidAPI Key", "emptyHint": "Your X-RapidAPI-Key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 2, "args": ["key"] } },
        { "type": "slider", "title": "Update Every", "min": 15, "max": 720, "step": 15, "default": 240, "suffix": " min", "command": { "app_command": 3, "args": ["minutes"] } },
        { "type": "slider", "title": "Show the Next", "min": 1, "max": 6, "default": 3, "suffix": " h", "command": { "app_command": 4, "args": ["hours"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Sign up at rapidapi.com, subscribe to AeroDataBox (its Basic plan is free), and copy your X-RapidAPI-Key here." },
          { "type": "bullet", "text": "The free plan allows about 200 board updates a month: every 4 hours stays within it. Paid plans allow more; 'Calls a month' above shows what your setting uses." },
          { "type": "note", "text": "Busy airports send a lot: if the board is cut short, Pixlpal shows fewer hours ahead. The key is kept on Pixlpal only." }
        ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The board turns its pages by itself; turn the knob to page, click to switch between departures and arrivals." },
        { "type": "note", "text": "Between updates the times and statuses are those of the last update: the board shows how old it is." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Airport saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write a 4-letter ICAO or 3-letter IATA code" },
    { "when": { "app_command": 2 }, "text": "Key saved" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Flight Board on Pixlpal first, with an airport and a key" }
  ]
})json";

#endif
