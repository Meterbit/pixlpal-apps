/**
 * @file otd_Mobile_Ui.h
 * @brief On This Day's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (onThisDay.cpp):
 *   0 {"seconds": 5-120}   seconds an event stays
 *   1 {"ticker": 0|1}      births, deaths and observances along the bottom
 *   2 {"speak": 0|1}       read each event aloud
 *   3 {}                   the next event now
 */
#ifndef OTD_MOBILE_UI_H
#define OTD_MOBILE_UI_H

static const char OTD_UI_MANIFEST[] = R"json({
  "name": "On This Day",
  "about": "What happened on today's date in history, from Wikipedia's front page: the events with their years, and who was born and who died, with the day's observances. A new day at midnight.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char OTD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.on-this-day.png", "height": 64, "alt": "On This Day" },
    { "type": "statusPanel", "title": "Today in History", "source": "events", "emptyText": "Open On This Day on Pixlpal." },
    { "type": "borderButton", "label": "THE NEXT EVENT", "command": { "app_command": 3 } },
    { "type": "slider", "title": "Each Event For", "min": 5, "max": 120, "step": 5, "default": 20, "suffix": " s", "command": { "app_command": 0, "args": ["seconds"] } },
    { "type": "switch", "title": "Births and Deaths Along the Bottom", "default": 1, "command": { "app_command": 1, "args": ["ticker"] } },
    { "type": "switch", "title": "Read Each Event Aloud", "default": 0, "command": { "app_command": 2, "args": ["speak"] } },
    {
      "type": "richTextBlock",
      "title": "Where It Comes From",
      "blocks": [
        { "type": "bullet", "text": "Wikipedia's \"On this day\": the anniversaries its editors choose for the front page each date, checked against the articles. Pixlpal fetches them once a day, at midnight in its time zone, and keeps them." },
        { "type": "bullet", "text": "Reading aloud uses Google's text to speech, and stays quiet in Silent Mode." },
        { "type": "note", "text": "Turn the knob to go through the events. Text from Wikipedia, under CC BY-SA." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3, "response": 0 }, "text": "Open On This Day on Pixlpal first" }
  ]
})json";

#endif
