/**
 * @file gat_Mobile_Ui.h
 * @brief Gate Display's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (gateDisplay.cpp):
 *   0 {"airline": "Lagoon Air"}   1 {"flight": "LA 214"}   2 {"dest": "London Heathrow"}   3 {"time": "14:35"}   4 {"gate": "B12"}
 *   5 {"state": 0-8}   on time, check-in open, boarding soon, boarding, final call, gate closed, delayed, cancelled, departed
 *   6 {"groups": 0-9}  boarding groups (0: none)          7 {"newTime": "15:10"}  the time when delayed
 *   8 {"note": "..."}  a line along the bottom             9 {"chime": 0|1}        10 {}  the next step of boarding
 */
#ifndef GAT_MOBILE_UI_H
#define GAT_MOBILE_UI_H

static const char GAT_UI_MANIFEST[] = R"json({
  "name": "Gate Display",
  "about": "A gate or check-in sign for airline staff: the flight, destination, time and gate, and the call in large letters (boarding by group, final call, gate closed, delayed) with a chime at each change. One click moves boarding along. No internet needed.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char GAT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.gate-display.png", "height": 64, "alt": "Gate Display" },
    { "type": "statusPanel", "title": "The Gate", "source": "gateNow" },
    { "type": "borderButton", "label": "NEXT STEP OF BOARDING", "command": { "app_command": 10 } },
    {
      "type": "radioGroup", "title": "Call", "default": 0,
      "options": [
        { "label": "On time", "value": 0 }, { "label": "Check-in open", "value": 1 }, { "label": "Boarding soon", "value": 2 },
        { "label": "Boarding", "value": 3 }, { "label": "Final call", "value": 4 }, { "label": "Gate closed", "value": 5 },
        { "label": "Delayed", "value": 6 }, { "label": "Cancelled", "value": 7 }, { "label": "Departed", "value": 8 }
      ],
      "command": { "app_command": 5, "args": ["state"] }
    },
    { "type": "textField", "title": "New Time (When Delayed)", "emptyHint": "15:10", "filledHint": "Change the new time", "visibleWhen": { "arg": "state", "equals": 6 }, "command": { "app_command": 7, "args": ["newTime"] } },
    {
      "type": "section",
      "id": "flight",
      "title": "The Flight",
      "widgets": [
        { "type": "textField", "title": "Airline", "emptyHint": "e.g. Lagoon Air", "filledHint": "Change the airline", "command": { "app_command": 0, "args": ["airline"] } },
        { "type": "textField", "title": "Flight", "emptyHint": "e.g. LA 214", "filledHint": "Change the flight", "command": { "app_command": 1, "args": ["flight"] } },
        { "type": "textField", "title": "Destination", "emptyHint": "e.g. London Heathrow", "filledHint": "Change the destination", "command": { "app_command": 2, "args": ["dest"] } },
        { "type": "textField", "title": "Departure Time", "emptyHint": "14:35", "filledHint": "Change the time", "command": { "app_command": 3, "args": ["time"] } },
        { "type": "textField", "title": "Gate", "emptyHint": "e.g. B12", "filledHint": "Change the gate", "command": { "app_command": 4, "args": ["gate"] } },
        { "type": "slider", "title": "Boarding Groups", "min": 0, "max": 9, "default": 0, "command": { "app_command": 6, "args": ["groups"] } }
      ]
    },
    {
      "type": "section",
      "id": "sign",
      "title": "The Sign",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Note Along the Bottom", "emptyHint": "Please have your boarding pass and passport ready", "filledHint": "Change the note (empty: the call's own)", "command": { "app_command": 8, "args": ["note"] } },
        { "type": "switch", "title": "Chime at Each Change", "default": true, "command": { "app_command": 9, "args": ["chime"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click for the next step: boarding soon, boarding (group by group), final call, gate closed, departed." },
        { "type": "bullet", "text": "Turn the knob to choose any call. Hold it to go back to On time for the next flight." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3, "response": 0 }, "text": "Write the time as 14:35" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Write the time as 15:10" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Open Gate Display on Pixlpal first" }
  ]
})json";

#endif
