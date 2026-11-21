/**
 * @file chs_Mobile_Ui.h
 * @brief Chess's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (chess.cpp):
 *   0 {"channel": 0-4}         Lichess TV: top rated, bullet, blitz, rapid, classical
 *   1 {"user": "name"}         a Lichess username for the ratings page (empty: none)
 *   2 {"page": 0|1}            show the game or the ratings now
 *   3 {}                       the next game
 */
#ifndef CHS_MOBILE_UI_H
#define CHS_MOBILE_UI_H

static const char CHS_UI_MANIFEST[] = R"json({
  "name": "Chess",
  "about": "A live game from Lichess TV on the board as it is played: the moves, the players, their ratings and clocks, and the result, then the next game. Add your Lichess name for a page of your ratings. No key needed.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CHS_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.chess.png", "height": 64, "alt": "Chess" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    {
      "type": "radioGroup",
      "title": "Lichess TV",
      "default": 0,
      "options": [
        { "label": "Top rated", "value": 0 },
        { "label": "Bullet", "value": 1 },
        { "label": "Blitz", "value": 2 },
        { "label": "Rapid", "value": 3 },
        { "label": "Classical", "value": 4 }
      ],
      "command": { "app_command": 0, "args": ["channel"] }
    },
    { "type": "borderButton", "label": "THE NEXT GAME", "command": { "app_command": 3 } },
    { "type": "textField", "title": "Your Lichess Name", "emptyHint": "For a page of your ratings", "filledHint": "Change it (empty: none)", "command": { "app_command": 1, "args": ["user"] } },
    { "type": "borderButton", "label": "SHOW THE GAME", "command": { "app_command": 2, "page": 0 } },
    { "type": "borderButton", "label": "SHOW MY RATINGS", "command": { "app_command": 2, "page": 1 }, "visibleWhen": { "arg": "user", "set": true } },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The board follows the game a few seconds behind, as Lichess shows games to watchers. White plays up the board." },
        { "type": "bullet", "text": "Turn the knob for your ratings (with your Lichess name) and back. Click for the next game." },
        { "type": "note", "text": "Games and ratings from lichess.org, the free and open chess server." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "A Lichess name has letters, digits, - and _" },
    { "when": { "app_command": 3 }, "text": "Finding the next game" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Chess on Pixlpal first" }
  ]
})json";

#endif
