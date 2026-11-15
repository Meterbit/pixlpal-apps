/**
 * @file cth_Mobile_Ui.h
 * @brief Mobile UI of Catholic Daily {13,2} (FAITH), rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (catholicDaily.cpp): 0 acknowledged   1 refresh   2 {"verse"}   3 {"language"}   4 {"hour24"}   5 {"sound"}
 *   6 {"angelus"}   7 {"mercy"}   8 {"rosary"}   9 {"reminderSlot"}   10 {"reminderAt"}   11 {"reminderLabel"}
 *   12 {"reminderOn"}   13 {"reminderDays"}   14 another verse   15 ring the bell now (test)
 */
#ifndef CTH_MOBILE_UI_H
#define CTH_MOBILE_UI_H

static const char CTH_UI_MANIFEST[] = R"json({
  "appID": "13/2",
  "name": "Catholic Daily",
  "about": "Today's liturgical day from the General Roman Calendar in its colour, the Angelus (Regina Caeli in Eastertide) with a church bell, the Hour of Mercy, the Rosary's mysteries of the day, a Douay-Rheims verse each day, and reminders for Mass and the Rosary.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CTH_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:catholicDaily.png", "height": 64, "alt": "Catholic Daily" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    { "type": "statusPanel", "title": "Verse of the Day", "emptyText": "Open Catholic Daily on Pixlpal to read today's verse.", "source": "verses", "visibleWhen": { "arg": "verse", "equals": 1 } },
    { "type": "borderButton", "label": "ANOTHER VERSE TODAY", "command": { "app_command": 14 }, "visibleWhen": { "arg": "verse", "equals": 1 } },
    {
      "type": "section",
      "id": "devotions",
      "title": "Devotions",
      "widgets": [
        { "type": "switch", "title": "The Angelus at 6:00, 12:00 and 18:00 (Regina Caeli in Eastertide)", "default": true, "command": { "app_command": 6, "args": ["angelus"] } },
        { "type": "switch", "title": "The Hour of Mercy at 15:00", "default": false, "command": { "app_command": 7, "args": ["mercy"] } },
        {
          "type": "radioGroup",
          "title": "Sound",
          "default": 2,
          "options": [ { "label": "A church bell (3, 3, 3, then 9 for the Angelus)", "value": 2 }, { "label": "A gentle chime", "value": 1 }, { "label": "Silent (banner only)", "value": 0 } ],
          "command": { "app_command": 5, "args": ["sound"] }
        },
        { "type": "borderButton", "label": "RING THE BELL NOW (TEST)", "command": { "app_command": 15 } },
        { "type": "switch", "title": "The Rosary Page (the Mysteries of the Day)", "default": true, "command": { "app_command": 8, "args": ["rosary"] } }
      ]
    },
    {
      "type": "section",
      "id": "reminders",
      "title": "Reminders",
      "subtitle": "The Rosary, Mass, an examination of conscience",
      "collapsed": true,
      "widgets": [
        { "type": "statusPanel", "source": "reminders" },
        {
          "type": "radioGroup",
          "title": "Change Reminder",
          "default": 0,
          "options": [ { "label": "1", "value": 0 }, { "label": "2", "value": 1 }, { "label": "3", "value": 2 } ],
          "command": { "app_command": 9, "args": ["reminderSlot"] }
        },
        { "type": "switch", "title": "On", "default": false, "command": { "app_command": 12, "args": ["reminderOn"] } },
        { "type": "textField", "title": "Name", "emptyHint": "The Rosary, Sunday Mass, Adoration...", "command": { "app_command": 11, "args": ["reminderLabel"] } },
        { "type": "timeRange", "title": "At", "mode": "single", "defaultFrom": "20:00", "fromLabel": "At", "command": { "app_command": 10, "args": ["reminderAt"] } },
        {
          "type": "radioGroup",
          "title": "Days",
          "default": 0,
          "options": [ { "label": "Every day", "value": 0 }, { "label": "Weekdays", "value": 1 }, { "label": "Sundays", "value": 3 }, { "label": "Saturdays", "value": 4 }, { "label": "Fridays", "value": 5 } ],
          "command": { "app_command": 13, "args": ["reminderDays"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Calendar and Verse",
      "collapsed": true,
      "widgets": [
        {
          "type": "radioGroup",
          "title": "The Calendar's Language",
          "default": 0,
          "options": [ { "label": "English", "value": 0 }, { "label": "French", "value": 1 }, { "label": "Italian", "value": 2 }, { "label": "Latin", "value": 3 } ],
          "command": { "app_command": 3, "args": ["language"] }
        },
        { "type": "switch", "title": "A Verse Each Day (Douay-Rheims)", "default": true, "command": { "app_command": 2, "args": ["verse"] } },
        { "type": "switch", "title": "24-Hour Times", "default": true, "command": { "app_command": 4, "args": ["hour24"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The Douay-Rheims is the public domain Catholic Bible, with Tobit, Judith, Wisdom, Sirach, Baruch and Maccabees; its Psalms are numbered as in the Vulgate (Psalm 22: The Lord ruleth me). Newer approved translations are under copyright." } ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click: today, the Rosary, the verse (or stop the bell). Long press: another verse (on the verse page). Turn: scroll a long verse." },
        { "type": "note", "text": "Devotions sound while Catholic Daily is the app on Pixlpal's screen. The day follows the General Roman Calendar; national and diocesan feasts may differ." },
        { "type": "paragraph", "spans": [
          { "text": "Calendar: " }, { "text": "Church Calendar API", "link": "http://calapi.inadiutorium.cz" },
          { "text": ". Verses: " }, { "text": "bible-api.com", "link": "https://bible-api.com" }, { "text": "." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Reading again" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Catholic Daily is not open on Pixlpal" },
    { "when": { "app_command": 14 }, "text": "Another verse on its way" },
    { "when": { "app_command": 14, "response": 0 }, "text": "Open Catholic Daily on Pixlpal first" },
    { "when": { "app_command": 15, "response": 0 }, "text": "Open Catholic Daily on Pixlpal first" }
  ]
})json";

#endif
