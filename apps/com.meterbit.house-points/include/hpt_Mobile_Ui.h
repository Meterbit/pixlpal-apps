/**
 * @file hpt_Mobile_Ui.h
 * @brief House Points's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (housePoints.cpp):
 *   0 {"name0": "Red Dragons", "color0": "0xFFE53935"} ... (name5, color5)   a house's name and colour; an empty name removes it
 *   1 {"house": 0-5}      the house the buttons give to       2 {"amount": 1-50}   how many points
 *   3 {}  give them       4 {}  take them away                 5 {"award": "Red 5"}  a house (its name's start) and points (-5 takes)
 *   6 {}  start a new term (all points to 0)                    7 {"sound": 0|1}
 */
#ifndef HPT_MOBILE_UI_H
#define HPT_MOBILE_UI_H

static const char HPT_UI_MANIFEST[] = R"json({
  "name": "House Points",
  "about": "A live scoreboard for houses or teams: each house's colour bar and total, the leader crowned, and a cheer whenever points are given. Give points from the phone or with the knob; this week's points and the term's are kept.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char HPT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.house-points.png", "height": 64, "alt": "House Points" },
    { "type": "statusPanel", "title": "The Scores", "source": "scores" },
    { "type": "textField", "title": "Quick Award: House and Points", "emptyHint": "Red 5  (or Blue -2)", "filledHint": "Another award", "command": { "app_command": 5, "args": ["award"] } },
    { "type": "radioGroup", "title": "House", "default": 0, "options": [ { "label": "House 1", "value": 0 }, { "label": "House 2", "value": 1 }, { "label": "House 3", "value": 2 }, { "label": "House 4", "value": 3 }, { "label": "House 5", "value": 4 }, { "label": "House 6", "value": 5 } ], "command": { "app_command": 1, "args": ["house"] } },
    { "type": "slider", "title": "Points", "min": 1, "max": 50, "default": 1, "command": { "app_command": 2, "args": ["amount"] } },
    { "type": "borderButton", "label": "GIVE POINTS", "command": { "app_command": 3 } },
    { "type": "borderButton", "label": "TAKE POINTS AWAY", "command": { "app_command": 4 } },
    { "type": "statusPanel", "title": "Recent Awards", "source": "recent", "emptyText": "No points given yet." },
    {
      "type": "section",
      "id": "houses",
      "title": "The Houses",
      "collapsed": true,
      "widgets": [
        { "type": "textColorField", "title": "House 1", "defaultText": "Red", "defaultColor": "0xFFE53935", "command": { "app_command": 0, "args": ["name0", "color0"] } },
        { "type": "textColorField", "title": "House 2", "defaultText": "Blue", "defaultColor": "0xFF1E88E5", "command": { "app_command": 0, "args": ["name1", "color1"] } },
        { "type": "textColorField", "title": "House 3", "defaultText": "Green", "defaultColor": "0xFF43A047", "command": { "app_command": 0, "args": ["name2", "color2"] } },
        { "type": "textColorField", "title": "House 4", "defaultText": "Yellow", "defaultColor": "0xFFFDD835", "command": { "app_command": 0, "args": ["name3", "color3"] } },
        { "type": "textColorField", "title": "House 5", "defaultText": "", "defaultColor": "0xFF8E24AA", "command": { "app_command": 0, "args": ["name4", "color4"] } },
        { "type": "textColorField", "title": "House 6", "defaultText": "", "defaultColor": "0xFFFB8C00", "command": { "app_command": 0, "args": ["name5", "color5"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Up to six houses or teams. Send an empty name to remove one." } ] }
      ]
    },
    {
      "type": "section",
      "id": "term",
      "title": "The Term",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 7, "args": ["sound"] } },
        { "type": "borderButton", "label": "START A NEW TERM", "confirm": { "title": "Start a new term?", "text": "Every house goes back to 0 points.", "ok": "New term", "cancel": "Keep" }, "command": { "app_command": 6 } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose a house, click to give it a point, double click to take one away." },
        { "type": "note", "text": "This week's points start again each Monday; the term's stay until you start a new term." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "House saved" },
    { "when": { "app_command": 3 }, "text": "Points given" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Choose a house that exists" },
    { "when": { "app_command": 4 }, "text": "Points taken away" },
    { "when": { "app_command": 5 }, "text": "Points given" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Write a house and points: Red 5" },
    { "when": { "app_command": 6 }, "text": "New term: every house back to 0" }
  ]
})json";

#endif
