/**
 * @file arc_Mobile_Ui.h
 * @brief Pixel Arcade's phone page {20,0} (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (pixelArcade.cpp):
 *   0 {"pad": "left|right|up|down|ok|back"}   the phone as a game pad
 *   1 {"game": 0-2}       the game the menu shows (0 Worm, 1 Brick Breaker, 2 Comet Dodge); starts it from the menu
 *   2 {"speed": 0-2}      0 relaxed, 1 normal, 2 fast
 *   3 {"sound": 0|1}
 *   4 {}                  forget the best scores
 */
#ifndef ARC_MOBILE_UI_H
#define ARC_MOBILE_UI_H

static const char ARC_UI_MANIFEST[] = R"json({
  "appID": "20/0",
  "name": "Pixel Arcade",
  "about": "Three quick arcade games on Pixlpal: Worm (eat, grow, don't bite yourself), Brick Breaker (bounce the ball through the wall) and Comet Dodge (fly through the comets and catch the stars). Play with the knob, a USB keyboard or mouse, or this phone as a game pad. Pixlpal keeps each game's best score.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char ARC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:pixelArcade.png", "height": 64, "alt": "Pixel Arcade" },
    { "type": "statusPanel", "title": "On Pixlpal Now", "source": "now" },
    { "type": "statusPanel", "title": "Best Scores", "source": "scores", "emptyText": "No games played yet." },
    {
      "type": "radioGroup",
      "title": "Game",
      "default": 0,
      "options": [
        { "label": "Worm: Eat the Fruit and Grow", "value": 0 },
        { "label": "Brick Breaker: Clear the Wall", "value": 1 },
        { "label": "Comet Dodge: Fly Through the Comets", "value": 2 }
      ],
      "command": { "app_command": 1, "args": ["game"] }
    },
    {
      "type": "section",
      "id": "pad",
      "title": "Game Pad",
      "widgets": [
        { "type": "borderButton", "label": "UP", "command": { "app_command": 0, "pad": "up" } },
        { "type": "borderButton", "label": "LEFT", "command": { "app_command": 0, "pad": "left" } },
        { "type": "borderButton", "label": "RIGHT", "command": { "app_command": 0, "pad": "right" } },
        { "type": "borderButton", "label": "DOWN", "command": { "app_command": 0, "pad": "down" } },
        { "type": "borderButton", "label": "START / PAUSE", "command": { "app_command": 0, "pad": "ok" } },
        { "type": "borderButton", "label": "BACK TO THE MENU", "command": { "app_command": 0, "pad": "back" } }
      ]
    },
    {
      "type": "radioGroup",
      "title": "Speed",
      "default": 1,
      "options": [ { "label": "Relaxed", "value": 0 }, { "label": "Normal", "value": 1 }, { "label": "Fast", "value": 2 } ],
      "command": { "app_command": 2, "args": ["speed"] }
    },
    { "type": "switch", "title": "Sound", "default": true, "command": { "app_command": 3, "args": ["sound"] } },
    { "type": "borderButton", "label": "FORGET THE BEST SCORES", "confirm": { "title": "Forget them?", "text": "Every game's best score goes back to 0.", "ok": "Forget", "cancel": "Keep" }, "command": { "app_command": 4 } },
    {
      "type": "richTextBlock",
      "title": "How to Play",
      "blocks": [
        { "type": "bullet", "text": "Knob: turn to choose a game, click to start. In a game, click to pause, hold to go back to the menu." },
        { "type": "bullet", "text": "Worm: each turn of the knob turns the worm left or right. On a keyboard, the arrows point it. Relaxed speed lets the worm go through the walls." },
        { "type": "bullet", "text": "Brick Breaker: turn the knob, move the mouse or hold the arrows to move the bat. Three balls a game; each cleared wall is a faster one." },
        { "type": "bullet", "text": "Comet Dodge: turn the knob, move the mouse or hold up and down to fly. Stars are 10 points; one comet ends the flight." },
        { "type": "note", "text": "Keyboard: Enter or Space starts and pauses, Esc goes back to the menu." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 4 }, "text": "Best scores forgotten" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Pixel Arcade on Pixlpal first" }
  ]
})json";

#endif
