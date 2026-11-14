/**
 * @file ttm_Mobile_Ui.h
 * @brief Tell the Time's phone UI {19,1}, for parents.
 *
 *   0 {"startLevel": 0-5 | 255}   1 {"kSet"} | {"kRead"} | {"kPass"} | {"hour24"} | {"words"}
 *   2 {"event": "07:30 School bus"}   3 {} remove the last event   4 {} clear the day
 */
#ifndef TTM_MOBILE_UI_H
#define TTM_MOBILE_UI_H

static const char TTM_UI_MANIFEST[] = R"json({
  "appID": "19/1",
  "name": "Tell the Time",
  "about": "For children of 5 to 8: a big clock with the time in words and your family's day, and games to learn the clock: set the hands with the knob, read the clock, and work out what time it will be later. Six levels, from o'clock to any minute, that follow how your child does.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TTM_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "How They Are Doing", "source": "status", "emptyText": "Set up your children in Kids Learning." },
    {
      "type": "section", "id": "level", "title": "Levels", "widgets": [
        {
          "type": "radioGroup", "title": "Start At", "default": 255,
          "options": [
            { "label": "Right for their age (Recommended)", "value": 255 },
            { "label": "1. O'clock", "value": 0 },
            { "label": "2. Half past", "value": 1 },
            { "label": "3. Quarter past and quarter to", "value": 2 },
            { "label": "4. Five minutes", "value": 3 },
            { "label": "5. Any minute", "value": 4 },
            { "label": "6. Time passing", "value": 5 }
          ],
          "command": { "app_command": 0, "args": ["startLevel"] }
        },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Choosing a level starts every child again from it. After that, 7 or more right of 8 moves a child up, 3 or fewer moves them down." } ] },
        { "type": "switch", "title": "Set the Clock (Knob Moves the Hands)", "default": true, "command": { "app_command": 1, "args": ["kSet"] } },
        { "type": "switch", "title": "What Time Is It? (Choose the Answer)", "default": true, "command": { "app_command": 1, "args": ["kRead"] } },
        { "type": "switch", "title": "Time Passing (From Level 6)", "default": true, "command": { "app_command": 1, "args": ["kPass"] } }
      ]
    },
    {
      "type": "section", "id": "day", "title": "Our Day", "subtitle": "Shown on the clock page", "collapsed": true, "widgets": [
        { "type": "statusPanel", "source": "dayRows", "emptyText": "Nothing added yet" },
        { "type": "textField", "title": "Add (Time and What)", "emptyHint": "07:30 School bus", "command": { "app_command": 2, "args": ["event"] } },
        { "type": "borderButton", "label": "REMOVE THE LAST ONE", "command": { "app_command": 3 } },
        { "type": "borderButton", "label": "CLEAR THE DAY", "command": { "app_command": 4 }, "confirm": "Remove everything from the day?" },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Up to 6 things, with a 24-hour time. The clock page shows what comes next (\"LUNCH IN 25 MIN\")." } ] }
      ]
    },
    {
      "type": "section", "id": "clock", "title": "The Clock Page", "collapsed": true, "widgets": [
        { "type": "switch", "title": "The Time in Words", "default": true, "command": { "app_command": 1, "args": ["words"] } },
        { "type": "switch", "title": "24-Hour Digital Time", "default": false, "command": { "app_command": 1, "args": ["hour24"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "Children, stars, the daily time limit, bedtime and sounds are set in Kids Learning, and count for every kids app." },
          { "type": "note", "text": "On Pixlpal: turn the knob to choose or to move the hands, press to choose, hold to go back. The clock page doesn't count towards the time limit." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Saved" },
    { "when": { "app_command": 2 }, "text": "Added to the day" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Type a time and a name, like 07:30 School bus (6 at most)" },
    { "when": { "app_command": 3, "response": 0 }, "text": "The day is empty" }
  ]
})json";

#endif
