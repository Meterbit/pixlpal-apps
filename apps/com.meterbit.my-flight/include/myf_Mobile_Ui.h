/**
 * @file myf_Mobile_Ui.h
 * @brief My Flight's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (myFlight.cpp):
 *   0 {"flight": "BA 75"}        the flight number (IATA or ICAO)
 *   1 {"date": "2026-11-20"}     the day it leaves (local); "today" or "tomorrow" work too
 *   2 {"key": "..."}             the owner's RapidAPI key for AeroDataBox (empty: forget it)
 *   3 {"travel": 15-300}         minutes to get to the airport
 *   4 {"early": 30-240}          minutes to be at the airport before departure
 *   5 {}                         check the flight now
 */
#ifndef MYF_MOBILE_UI_H
#define MYF_MOBILE_UI_H

static const char MYF_UI_MANIFEST[] = R"json({
  "name": "My Flight",
  "about": "Follows your flight from home or the hotel: status, terminal and gate, any delay, when to leave for the airport, the countdown to boarding, the flight's progress in the air, and the baggage belt when it lands. From AeroDataBox, with your own RapidAPI key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char MYF_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.my-flight.png", "height": 64, "alt": "My Flight" },
    { "type": "statusPanel", "title": "The Flight", "source": "status" },
    { "type": "borderButton", "label": "CHECK IT NOW", "command": { "app_command": 5 } },
    { "type": "textField", "title": "Flight Number", "emptyHint": "e.g. BA 75", "filledHint": "Change the flight", "command": { "app_command": 0, "args": ["flight"] } },
    { "type": "textField", "title": "Date It Leaves", "emptyHint": "2026-11-20, today or tomorrow", "filledHint": "Change the date", "command": { "app_command": 1, "args": ["date"] } },
    {
      "type": "section",
      "id": "trip",
      "title": "Getting There",
      "widgets": [
        { "type": "slider", "title": "Time to Get to the Airport", "min": 15, "max": 300, "step": 5, "default": 60, "suffix": " min", "command": { "app_command": 3, "args": ["travel"] } },
        { "type": "slider", "title": "Be at the Airport Before Departure", "min": 30, "max": 240, "step": 15, "default": 120, "suffix": " min", "command": { "app_command": 4, "args": ["early"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Pixlpal shows when to leave: departure, less the time to be there early, less the journey. It follows the flight's latest time." } ] }
      ]
    },
    {
      "type": "section",
      "id": "data",
      "title": "Flight Data",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "RapidAPI Key", "emptyHint": "Your X-RapidAPI-Key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 2, "args": ["key"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Sign up at rapidapi.com, subscribe to AeroDataBox (its Basic plan is free), and copy your X-RapidAPI-Key here." },
          { "type": "bullet", "text": "Pixlpal checks every 6 hours until the day before, every hour on the day, and every 15 minutes from 3 hours before departure until landing: a flight takes about 30 checks, well within the free plan." },
          { "type": "note", "text": "The key is kept on Pixlpal only. A Flight Board on the same Pixlpal can use the same key." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Flight saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write a flight number such as BA 75" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write the date as 2026-11-20, or today, or tomorrow" },
    { "when": { "app_command": 2 }, "text": "Key saved" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open My Flight on Pixlpal first, with a flight and a key" }
  ]
})json";

#endif
