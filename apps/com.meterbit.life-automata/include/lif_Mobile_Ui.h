/**
 * @file lif_Mobile_Ui.h
 * @brief Life & Automata's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (lifeAutomata.cpp):
 *   0 {"rule": 0-9}        the rule (life_Engine.cpp LIFE_RULES)
 *   1 {"cells": 0-2}       cells of 1, 2 or 4 pixels
 *   2 {"speed": 1-30}      generations a second
 *   3 {"theme": 0-3}       neon, fire, matrix, ice
 *   4 {"auto": 0-30}       minutes before the next rule by itself, 0 to stay
 *   5 {"labels": 0|1}      the rule's name in the corner
 *   6 {}                   seed it again
 *   7 {"paused": 0|1}
 */
#ifndef LIF_MOBILE_UI_H
#define LIF_MOBILE_UI_H

static const char LIF_UI_MANIFEST[] = R"json({
  "name": "Life & Automata",
  "about": "Living patterns that grow, move and die by simple rules: Conway's Game of Life, HighLife, Day & Night, Seeds, Brian's Brain, Maze and the one-line rules 30, 90 and 110. An ever-changing screen that seeds itself again when it settles.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char LIF_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.life-automata.png", "height": 64, "alt": "Life & Automata" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Open Life & Automata on Pixlpal." },
    {
      "type": "radioGroup",
      "title": "Rule",
      "default": 0,
      "options": [
        { "label": "Game of Life (B3/S23)", "value": 0 },
        { "label": "HighLife: with replicators (B36/S23)", "value": 1 },
        { "label": "Day & Night: islands (B3678/S34678)", "value": 2 },
        { "label": "Seeds: explosions (B2/S)", "value": 3 },
        { "label": "Maze: corridors (B3/S12345)", "value": 4 },
        { "label": "Anneal: blobs that melt together", "value": 5 },
        { "label": "Brian's Brain: sparks", "value": 6 },
        { "label": "Rule 30: chaos, one line at a time", "value": 7 },
        { "label": "Rule 110: gliding stripes", "value": 8 },
        { "label": "Rule 90: Sierpinski's triangle", "value": 9 }
      ],
      "command": { "app_command": 0, "args": ["rule"] }
    },
    { "type": "borderButton", "label": "SEED IT AGAIN", "command": { "app_command": 6 } },
    { "type": "switch", "title": "Paused", "default": 0, "command": { "app_command": 7, "args": ["paused"] } },
    {
      "type": "radioGroup",
      "title": "Cells",
      "default": 1,
      "options": [
        { "label": "Tiny (256 x 128 cells)", "value": 0 },
        { "label": "Small (128 x 64)", "value": 1 },
        { "label": "Big (64 x 32)", "value": 2 }
      ],
      "command": { "app_command": 1, "args": ["cells"] }
    },
    { "type": "slider", "title": "Speed", "min": 1, "max": 30, "step": 1, "default": 12, "suffix": " gen/s", "command": { "app_command": 2, "args": ["speed"] } },
    {
      "type": "radioGroup",
      "title": "Colours",
      "default": 0,
      "options": [
        { "label": "Neon", "value": 0 },
        { "label": "Fire", "value": 1 },
        { "label": "Matrix", "value": 2 },
        { "label": "Ice", "value": 3 }
      ],
      "command": { "app_command": 3, "args": ["theme"] }
    },
    { "type": "slider", "title": "Next Rule by Itself Every (0: Stay)", "min": 0, "max": 30, "step": 1, "default": 0, "suffix": " min", "command": { "app_command": 4, "args": ["auto"] } },
    { "type": "switch", "title": "The Rule's Name in the Corner", "default": 0, "command": { "app_command": 5, "args": ["labels"] } },
    {
      "type": "richTextBlock",
      "title": "How It Works",
      "blocks": [
        { "type": "paragraph", "text": "Each cell lives or dies by how many of its eight neighbours are alive. In the Game of Life a cell is born with exactly 3, and survives with 2 or 3: B3/S23. Other numbers give other worlds." },
        { "type": "paragraph", "text": "The one-line rules (30, 90, 110) work out each new line from the one before, and the picture scrolls up." },
        { "type": "bullet", "text": "New cells are bright, settled ones darker; cells that just died leave a faint trail." },
        { "type": "note", "text": "Turn the knob for the next rule, click to seed it again, double-click to pause." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 6 }, "text": "Seeded" }
  ]
})json";

#endif
