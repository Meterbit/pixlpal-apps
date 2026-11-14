/**
 * @file cdq_Mobile_Ui.h
 * @brief Code Quest's phone UI {19,4}, for parents.
 *
 *   0 {"unlockAll"} | {"speed"}   1 {} every child starts again
 */
#ifndef CDQ_MOBILE_UI_H
#define CDQ_MOBILE_UI_H

static const char CDQ_UI_MANIFEST[] = R"json({
  "appID": "19/4",
  "name": "Code Quest",
  "about": "For children of 6 to 12: a first taste of programming. Children build a program from blocks (forward, turn, repeat, repeat until the star, if a wall is ahead) with the knob, then watch the robot run it. 16 levels, each needing a new idea, with stars for the fewest blocks and every gem.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CDQ_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "How They Are Doing", "source": "status", "emptyText": "Set up your children in Kids Learning." },
    {
      "type": "radioGroup", "title": "Robot Speed", "default": 1,
      "options": [ { "label": "Slow (to follow each block)", "value": 0 }, { "label": "Normal", "value": 1 }, { "label": "Fast", "value": 2 } ],
      "command": { "app_command": 0, "args": ["speed"] }
    },
    { "type": "switch", "title": "Open Every Level", "default": false, "command": { "app_command": 0, "args": ["unlockAll"] } },
    { "type": "richTextBlock", "blocks": [
      { "type": "heading", "text": "The four steps" },
      { "type": "bullet", "text": "Levels 1-6: moving and turning, one block at a time." },
      { "type": "bullet", "text": "Levels 7-10: REPEAT, for when there are too many steps for the blocks allowed." },
      { "type": "bullet", "text": "Levels 11-12: a repeat inside a repeat." },
      { "type": "bullet", "text": "Levels 13-16: REPEAT UNTIL THE STAR and IF WALL AHEAD: one program that follows any path." },
      { "type": "note", "text": "Each level opens when the one before has a star, unless you open them all above. Play time counts towards the daily limit set in Kids Learning." }
    ] },
    { "type": "borderButton", "label": "START AGAIN (EVERY CHILD)", "command": { "app_command": 1 }, "confirm": "Forget every child's stars and programs in Code Quest?" }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Everyone starts again" }
  ]
})json";

#endif
