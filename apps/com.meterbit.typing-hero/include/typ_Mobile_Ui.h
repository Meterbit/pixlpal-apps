/**
 * @file typ_Mobile_Ui.h
 * @brief Typing Hero's phone UI {19,5}, for parents.
 *
 *   0 {"keyboardMap"} | {"unlockAll"} | {"passAccuracy"}   1 {} every child starts again
 */
#ifndef TYP_MOBILE_UI_H
#define TYP_MOBILE_UI_H

static const char TYP_UI_MANIFEST[] = R"json({
  "appID": "19/5",
  "name": "Typing Hero",
  "about": "For children of 7 to 12: learn to type with all ten fingers on a real keyboard. Plug any USB keyboard into Pixlpal. 13 lessons from the home row to sentences show the next key and the finger for it, a falling-letters game makes practice fun, and a one-minute word race measures speed.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TYP_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "How They Are Doing", "source": "status", "emptyText": "Set up your children in Kids Learning." },
    { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Plug a USB keyboard into Pixlpal (a hub works too)." } ], "visibleWhen": { "arg": "keyboard", "equals": 0 } },
    { "type": "switch", "title": "Show the Keyboard and Fingers", "default": true, "command": { "app_command": 0, "args": ["keyboardMap"] } },
    { "type": "slider", "title": "Accuracy Needed for the Next Lesson", "min": 50, "max": 100, "step": 5, "default": 90, "suffix": " %", "command": { "app_command": 0, "args": ["passAccuracy"] } },
    { "type": "switch", "title": "Open Every Lesson", "default": false, "command": { "app_command": 0, "args": ["unlockAll"] } },
    { "type": "richTextBlock", "blocks": [
      { "type": "bullet", "text": "Each lesson is three lines. A wrong key turns red and must be typed right: accuracy matters more than speed at first." },
      { "type": "bullet", "text": "Falling letters uses the keys learnt so far." },
      { "type": "note", "text": "Arrows and Enter choose, Esc goes back. Play time counts towards the daily limit set in Kids Learning." }
    ] },
    { "type": "borderButton", "label": "START AGAIN (EVERY CHILD)", "command": { "app_command": 1 }, "confirm": "Forget every child's lessons and best scores in Typing Hero?" }
  ],
  "toasts": [ { "when": { "app_command": 1 }, "text": "Everyone starts again" } ]
})json";

#endif
