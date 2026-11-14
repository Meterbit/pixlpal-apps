/**
 * @file dbl_Mobile_Ui.h
 * @brief Mobile UI of Daily Bible {13,1} (FAITH), rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (dailyBible.cpp): 0 acknowledged   1 refresh   2 {"translation"}   3 {"verse"}   4 {"plan"}
 *   5 start the plan today   6 {"orthodox"}   7 {"hour24"}   8 {"chime"}   9 {"reminderSlot"}   10 {"reminderAt"}
 *   11 {"reminderLabel"}   12 {"reminderOn"}   13 {"reminderDays"}   14 another verse   15 {"read"} today's reading
 */
#ifndef DBL_MOBILE_UI_H
#define DBL_MOBILE_UI_H

static const char DBL_UI_MANIFEST[] = R"json({
  "appID": "13/1",
  "name": "Daily Bible",
  "about": "A Bible verse every day, a reading plan with a streak, the church year from Advent to Pentecost (Western or Orthodox), and reminders for your devotions and Sunday service.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char DBL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:dailyBible.png", "height": 64, "alt": "Daily Bible" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    { "type": "switch", "title": "I Have Read Today's Reading", "default": false, "command": { "app_command": 15, "args": ["read"] }, "visibleWhen": { "arg": "plan", "notEquals": 0 } },
    { "type": "statusPanel", "title": "Verse of the Day", "emptyText": "Open Daily Bible on Pixlpal to read today's verse.", "source": "verses", "visibleWhen": { "arg": "verse", "equals": 1 } },
    { "type": "borderButton", "label": "ANOTHER VERSE TODAY", "command": { "app_command": 14 }, "visibleWhen": { "arg": "verse", "equals": 1 } },
    {
      "type": "section",
      "id": "bible",
      "title": "Verse of the Day",
      "widgets": [
        { "type": "switch", "title": "Show a Verse Each Day", "default": true, "command": { "app_command": 3, "args": ["verse"] } },
        {
          "type": "radioGroup",
          "title": "Translation",
          "default": 0,
          "options": [
            { "label": "English, World English Bible", "value": 0 },
            { "label": "English, King James Version", "value": 1 },
            { "label": "English, Berean Standard Bible", "value": 2 },
            { "label": "English, American Standard Version", "value": 3 },
            { "label": "English, Bible in Basic English", "value": 4 },
            { "label": "English, World English Bible (British)", "value": 5 },
            { "label": "English, Open English Bible", "value": 6 },
            { "label": "English, Darby", "value": 7 },
            { "label": "Swahili Bible", "value": 8 },
            { "label": "Spanish, Reina Valera 1909", "value": 9 },
            { "label": "Spanish, La Biblia en Espanol Sencillo", "value": 10 },
            { "label": "German, Luther 1912", "value": 11 },
            { "label": "Italian, Riveduta", "value": 12 },
            { "label": "Tagalog, Ang Dating Biblia", "value": 13 },
            { "label": "Russian, Synodal", "value": 14 },
            { "label": "Romanian, Cornilescu", "value": 15 }
          ],
          "command": { "app_command": 2, "args": ["translation"] },
          "visibleWhen": { "arg": "verse", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "plan",
      "title": "Reading Plan",
      "collapsed": true,
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Plan",
          "default": 0,
          "options": [
            { "label": "No plan", "value": 0 },
            { "label": "The whole Bible in a year (3 to 4 chapters a day)", "value": 1 },
            { "label": "The New Testament, a chapter a day (260 days)", "value": 2 },
            { "label": "A Proverb and five Psalms a day, each month", "value": 3 }
          ],
          "command": { "app_command": 4, "args": ["plan"] }
        },
        {
          "type": "borderButton",
          "label": "START THE PLAN TODAY",
          "confirm": { "title": "Start again today?", "text": "Today becomes day 1 and the streak starts over.", "ok": "Start", "cancel": "Keep" },
          "command": { "app_command": 5 },
          "visibleWhen": { "arg": "plan", "in": [1, 2] }
        },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The plans follow the calendar from 1 January unless started today. On Pixlpal, long press the knob on today's page when you have read." } ] }
      ]
    },
    {
      "type": "section",
      "id": "reminders",
      "title": "Reminders",
      "subtitle": "Devotions, prayer, Sunday service",
      "collapsed": true,
      "widgets": [
        { "type": "statusPanel", "source": "reminders" },
        {
          "type": "radioGroup",
          "title": "Change Reminder",
          "default": 0,
          "options": [ { "label": "1", "value": 0 }, { "label": "2", "value": 1 }, { "label": "3", "value": 2 }, { "label": "4", "value": 3 } ],
          "command": { "app_command": 9, "args": ["reminderSlot"] }
        },
        { "type": "switch", "title": "On", "default": false, "command": { "app_command": 12, "args": ["reminderOn"] } },
        { "type": "textField", "title": "Name", "emptyHint": "Morning devotion, Sunday service...", "command": { "app_command": 11, "args": ["reminderLabel"] } },
        { "type": "timeRange", "title": "At", "mode": "single", "defaultFrom": "06:00", "fromLabel": "At", "command": { "app_command": 10, "args": ["reminderAt"] } },
        {
          "type": "radioGroup",
          "title": "Days",
          "default": 0,
          "options": [ { "label": "Every day", "value": 0 }, { "label": "Weekdays", "value": 1 }, { "label": "Weekends", "value": 2 }, { "label": "Sundays", "value": 3 }, { "label": "Saturdays", "value": 4 } ],
          "command": { "app_command": 13, "args": ["reminderDays"] }
        },
        { "type": "switch", "title": "Chime With Reminders", "default": true, "command": { "app_command": 8, "args": ["chime"] } }
      ]
    },
    {
      "type": "section",
      "id": "calendar",
      "title": "Church Year",
      "collapsed": true,
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Calendar",
          "default": 0,
          "options": [ { "label": "Western (Easter by the Gregorian calendar)", "value": 0 }, { "label": "Orthodox (Pascha, Christmas on 7 January)", "value": 1 } ],
          "command": { "app_command": 6, "args": ["orthodox"] }
        },
        { "type": "switch", "title": "24-Hour Times", "default": true, "command": { "app_command": 7, "args": ["hour24"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click: the verse or today's page. Long press: another verse (verse page), or mark today's reading read (today's page). Turn: scroll a long verse." },
        { "type": "note", "text": "Reminders sound while Daily Bible is the app on Pixlpal's screen." },
        { "type": "paragraph", "spans": [ { "text": "Verses: " }, { "text": "bible-api.com", "link": "https://bible-api.com" }, { "text": " (public domain translations)." } ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Reading again" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Daily Bible is not open on Pixlpal" },
    { "when": { "app_command": 5 }, "text": "Plan started: today is day 1" },
    { "when": { "app_command": 14 }, "text": "Another verse on its way" },
    { "when": { "app_command": 14, "response": 0 }, "text": "Open Daily Bible on Pixlpal first" }
  ]
})json";

#endif
