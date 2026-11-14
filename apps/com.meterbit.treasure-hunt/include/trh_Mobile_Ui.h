/**
 * @file trh_Mobile_Ui.h
 * @brief Treasure Hunt's phone UI {19,3}, for the grown-up running the hunt.
 *
 *   0 {"clue": "text | hint"}   1 {} remove the last   2 {} clear   3 {"preset": 0-2}   4 {"useCodes"} | {"showTimer"} | {"prize"}
 *   5 {"go": 1 start | 2 next clue | 3 hint}   6 {} new codes
 */
#ifndef TRH_MOBILE_UI_H
#define TRH_MOBILE_UI_H

static const char TRH_UI_MANIFEST[] = R"json({
  "appID": "19/3",
  "name": "Treasure Hunt",
  "about": "For children of 5 to 12: a treasure hunt round the house or garden. Write the clues here (or pick a ready-made hunt), hide a card with each clue's code where the clue leads, and Pixlpal does the rest: children find each card and enter its code with the knob to open the next clue, until the treasure.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TRH_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "Now", "source": "status", "emptyText": "Open Treasure Hunt on Pixlpal to play." },
    { "type": "borderButton", "label": "START THE HUNT", "command": { "app_command": 5, "go": 1 }, "confirm": "Start the hunt from the first clue?" },
    { "type": "borderButton", "label": "NEXT CLUE (THEY FOUND IT)", "command": { "app_command": 5, "go": 2 }, "visibleWhen": { "arg": "huntOn", "equals": 1 } },
    { "type": "borderButton", "label": "SHOW A HINT", "command": { "app_command": 5, "go": 3 }, "visibleWhen": { "arg": "huntOn", "equals": 1 } },
    {
      "type": "section", "id": "clues", "title": "The Clues", "widgets": [
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "Each clue says where to look next. Write each clue's card code on a piece of paper and hide it where that clue leads: finding it opens the next clue. The last card opens the treasure." }
        ], "visibleWhen": { "arg": "useCodes", "equals": 1 } },
        { "type": "statusPanel", "source": "clueRows", "emptyText": "No clues yet: add some, or load a ready-made hunt below." },
        { "type": "textField", "title": "Add a Clue (Optional Hint After |)", "emptyHint": "Look where the shoes sleep | By the door", "command": { "app_command": 0, "args": ["clue"] } },
        { "type": "borderButton", "label": "REMOVE THE LAST CLUE", "command": { "app_command": 1 } },
        { "type": "borderButton", "label": "NEW CODES", "command": { "app_command": 6 }, "confirm": "Give every clue a new code? Cards you already wrote won't match.", "visibleWhen": { "arg": "useCodes", "equals": 1 } },
        { "type": "borderButton", "label": "CLEAR ALL CLUES", "command": { "app_command": 2 }, "confirm": "Remove every clue?" }
      ]
    },
    {
      "type": "section", "id": "ready", "title": "Ready-Made Hunts", "collapsed": true, "widgets": [
        { "type": "borderButton", "label": "AT HOME (8 CLUES)", "command": { "app_command": 3, "preset": 0 }, "confirm": "Replace your clues with the home hunt?" },
        { "type": "borderButton", "label": "IN THE GARDEN (6 CLUES)", "command": { "app_command": 3, "preset": 1 }, "confirm": "Replace your clues with the garden hunt?" },
        { "type": "borderButton", "label": "A PARTY (6 CLUES)", "command": { "app_command": 3, "preset": 2 }, "confirm": "Replace your clues with the party hunt?" }
      ]
    },
    {
      "type": "section", "id": "options", "title": "Options", "collapsed": true, "widgets": [
        { "type": "switch", "title": "Cards with Codes", "default": true, "command": { "app_command": 4, "args": ["useCodes"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Off: no cards. When they find each place, press NEXT CLUE above (good for little ones)." } ], "visibleWhen": { "arg": "useCodes", "equals": 0 } },
        { "type": "switch", "title": "Show the Timer", "default": true, "command": { "app_command": 4, "args": ["showTimer"] } },
        { "type": "textField", "title": "Where the Treasure Is (Shown at the End)", "emptyHint": "Look under your pillow!", "command": { "app_command": 4, "args": ["prize"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "On Pixlpal: press to start, and to type a card's code (turn for each digit). Hold on a clue for its hint. The hunt doesn't count towards the daily time limit." } ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Clue added" },
    { "when": { "app_command": 3 }, "text": "Ready-made hunt loaded" },
    { "when": { "app_command": 5 }, "text": "Done" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Clues are 3 to 79 letters, 10 at most" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Treasure Hunt on Pixlpal first" }
  ]
})json";

#endif
