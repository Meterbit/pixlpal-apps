/**
 * @file sb_Mobile_Ui.h
 * @brief Scoreboard's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (scoreboard.cpp):
 *   0 {"point": 0|1, "n": 1-3}     a point (goal, basket of n) to home (0) or away (1)
 *   1 {}  undo                      2 {"clock": 1|0}  start or stop the clock      3 {}  a new match
 *   4 {"sport": 0-6}                football, basketball, volleyball, table tennis, badminton, tennis, points only
 *   5 {"home", "homeColor"} | {"away", "awayColor"}   names and colours ("0xAARRGGBB")
 *   6 {"minutes": 1-60}             a period's length        7 {"periods": 1-4}
 *   8 {"bestOf": 1|3|5|7}           sets (or games) to play  9 {"sound": 0|1}
 */
#ifndef SB_MOBILE_UI_H
#define SB_MOBILE_UI_H

static const char SB_UI_MANIFEST[] = R"json({
  "name": "Scoreboard",
  "about": "A scoreboard for the games you play: names and colours, the score from the knob or your phone, a game clock with periods and a buzzer, and the scoring rules of volleyball, table tennis, badminton and tennis built in.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SB_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.scoreboard.png", "height": 64, "alt": "Scoreboard" },
    { "type": "statusPanel", "title": "Score", "source": "score" },
    { "type": "borderButton", "label": "+1 HOME", "command": { "app_command": 0, "point": 0, "n": 1 } },
    { "type": "borderButton", "label": "+1 AWAY", "command": { "app_command": 0, "point": 1, "n": 1 } },
    { "type": "borderButton", "label": "+2 HOME", "command": { "app_command": 0, "point": 0, "n": 2 }, "visibleWhen": { "arg": "sport", "equals": 1 } },
    { "type": "borderButton", "label": "+2 AWAY", "command": { "app_command": 0, "point": 1, "n": 2 }, "visibleWhen": { "arg": "sport", "equals": 1 } },
    { "type": "borderButton", "label": "+3 HOME", "command": { "app_command": 0, "point": 0, "n": 3 }, "visibleWhen": { "arg": "sport", "equals": 1 } },
    { "type": "borderButton", "label": "+3 AWAY", "command": { "app_command": 0, "point": 1, "n": 3 }, "visibleWhen": { "arg": "sport", "equals": 1 } },
    { "type": "borderButton", "label": "UNDO", "command": { "app_command": 1 } },
    { "type": "borderButton", "label": "START THE CLOCK", "command": { "app_command": 2, "clock": 1 }, "visibleWhen": { "arg": "sport", "in": [0, 1] } },
    { "type": "borderButton", "label": "STOP THE CLOCK", "command": { "app_command": 2, "clock": 0 }, "visibleWhen": { "arg": "sport", "in": [0, 1] } },
    { "type": "borderButton", "label": "NEW MATCH", "confirm": { "title": "Start a new match?", "text": "The score and the clock go back to zero.", "ok": "New match", "cancel": "Keep playing" }, "command": { "app_command": 3 } },
    {
      "type": "section",
      "id": "game",
      "title": "The Game",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Sport",
          "default": 0,
          "options": [
            { "label": "Football (goals, a clock counting up)", "value": 0 },
            { "label": "Basketball (1, 2 or 3 points, a clock counting down)", "value": 1 },
            { "label": "Volleyball (sets to 25, the last to 15)", "value": 2 },
            { "label": "Table tennis (games to 11)", "value": 3 },
            { "label": "Badminton (games to 21)", "value": 4 },
            { "label": "Tennis (15, 30, 40, games and sets)", "value": 5 },
            { "label": "Points only", "value": 6 }
          ],
          "command": { "app_command": 4, "args": ["sport"] }
        },
        { "type": "textColorField", "title": "Home", "defaultText": "HOME", "defaultColor": "0xFFE53935", "command": { "app_command": 5, "args": ["home", "homeColor"] } },
        { "type": "textColorField", "title": "Away", "defaultText": "AWAY", "defaultColor": "0xFF1E88E5", "command": { "app_command": 5, "args": ["away", "awayColor"] } },
        { "type": "slider", "title": "Each Period", "min": 1, "max": 60, "default": 45, "suffix": " min", "command": { "app_command": 6, "args": ["minutes"] }, "visibleWhen": { "arg": "sport", "in": [0, 1] } },
        { "type": "slider", "title": "Periods", "min": 1, "max": 4, "default": 2, "command": { "app_command": 7, "args": ["periods"] }, "visibleWhen": { "arg": "sport", "in": [0, 1] } },
        { "type": "radioGroup", "title": "Best Of", "default": 5, "options": [ { "label": "1", "value": 1 }, { "label": "3", "value": 3 }, { "label": "5", "value": 5 }, { "label": "7", "value": 7 } ], "command": { "app_command": 8, "args": ["bestOf"] }, "visibleWhen": { "arg": "sport", "in": [2, 3, 4, 5] } },
        { "type": "switch", "title": "Buzzer and Sounds", "default": true, "command": { "app_command": 9, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose a side (it's underlined), click for a point, double click to undo, long press to start or stop the clock." },
        { "type": "bullet", "text": "For volleyball, table tennis, badminton and tennis, Pixlpal keeps the sets and games and shows who serves." },
        { "type": "bullet", "text": "The match is kept if Scoreboard is closed: open it again to go on." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1, "response": 0 }, "text": "Nothing to undo" },
    { "when": { "app_command": 3 }, "text": "New match" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Scoreboard on Pixlpal first" }
  ]
})json";

#endif
