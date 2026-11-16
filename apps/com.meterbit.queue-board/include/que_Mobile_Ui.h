/**
 * @file que_Mobile_Ui.h
 * @brief Queue Board's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (queueBoard.cpp):
 *   0 {"place": "Lagoon Bank, Marina"}   1 {"line0" ... "line2": "A Cash"}  a line's letter and name (empty: removed)
 *   2 {"counters": 1-12}                  3 {"counter": 1-12}   4 {"line": 0-2}   the counter and line the buttons call for
 *   5 {}  call the next ticket            6 {}  call it again    7 {"issued": "A 57"}  tickets taken so far on a line
 *   8 {"voice": 0|1}                      9 {}  start a new day (numbers back to 0)   10 {"serve": "A 40"}  set the number now serving
 */
#ifndef QUE_MOBILE_UI_H
#define QUE_MOBILE_UI_H

static const char QUE_UI_MANIFEST[] = R"json({
  "name": "Queue Board",
  "about": "A queue display for a bank branch or any service counter: the ticket called and its counter in large letters, a chime and a spoken call, each service line's number now serving, how many wait and about how long. Staff call the next ticket with a click or from the phone.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char QUE_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.queue-board.png", "height": 64, "alt": "Queue Board" },
    { "type": "statusPanel", "title": "The Queue", "source": "queue" },
    { "type": "radioGroup", "title": "Line", "default": 0, "options": [ { "label": "Line 1", "value": 0 }, { "label": "Line 2", "value": 1 }, { "label": "Line 3", "value": 2 } ], "command": { "app_command": 4, "args": ["line"] } },
    { "type": "slider", "title": "Counter", "min": 1, "max": 12, "default": 1, "command": { "app_command": 3, "args": ["counter"] } },
    { "type": "borderButton", "label": "CALL THE NEXT TICKET", "command": { "app_command": 5 } },
    { "type": "borderButton", "label": "CALL IT AGAIN", "command": { "app_command": 6 } },
    { "type": "textField", "title": "Tickets Taken So Far (For the Waiting Count)", "emptyHint": "A 57", "filledHint": "Another line's count", "command": { "app_command": 7, "args": ["issued"] } },
    {
      "type": "section",
      "id": "setup",
      "title": "The Lines and Counters",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Branch or Place", "emptyHint": "e.g. Lagoon Bank, Marina", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["place"] } },
        { "type": "textField", "title": "Line 1", "emptyHint": "Letter and name: A Cash", "filledHint": "Change line 1 (empty removes it)", "command": { "app_command": 1, "args": ["line0"] } },
        { "type": "textField", "title": "Line 2", "emptyHint": "B Customer service", "filledHint": "Change line 2", "command": { "app_command": 1, "args": ["line1"] } },
        { "type": "textField", "title": "Line 3", "emptyHint": "C Loans", "filledHint": "Change line 3", "command": { "app_command": 1, "args": ["line2"] } },
        { "type": "slider", "title": "Counters", "min": 1, "max": 12, "default": 4, "command": { "app_command": 2, "args": ["counters"] } },
        { "type": "switch", "title": "Speak Each Call", "default": true, "command": { "app_command": 8, "args": ["voice"] } },
        { "type": "textField", "title": "Set the Number Now Serving", "emptyHint": "A 40", "filledHint": "Set another", "command": { "app_command": 10, "args": ["serve"] } },
        { "type": "borderButton", "label": "START A NEW DAY", "confirm": { "title": "Start a new day?", "text": "Every line's numbers go back to 0.", "ok": "New day", "cancel": "Keep" }, "command": { "app_command": 9 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The numbers start again by themselves each morning. The spoken call uses Google's voice: only the ticket and counter are sent, and only while Pixlpal is online." } ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose this desk's counter, double click to change its line, click to call the next ticket, hold to call it again." },
        { "type": "bullet", "text": "The waiting count and the time come from the tickets taken so far (typed above) and how quickly the line has been moving." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1, "response": 0 }, "text": "Write a letter and a name: A Cash" },
    { "when": { "app_command": 5 }, "text": "Called" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Queue Board on Pixlpal first, and set the lines" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Write the line's letter and the number: A 57" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Write the line's letter and the number: A 40" },
    { "when": { "app_command": 9 }, "text": "New day: numbers back to 0" }
  ]
})json";

#endif
