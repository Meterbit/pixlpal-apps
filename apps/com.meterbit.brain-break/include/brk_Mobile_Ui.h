/**
 * @file brk_Mobile_Ui.h
 * @brief Brain Break's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (brainBreak.cpp):
 *   0 {"minutes": 1-3}    the break's length          1 {"seated": 0|1}   only activities done sitting down
 *   2 {"sound": 0|1}      3 {} start a break           4 {} stop it        5 {} skip to the next activity
 */
#ifndef BRK_MOBILE_UI_H
#define BRK_MOBILE_UI_H

static const char BRK_UI_MANIFEST[] = R"json({
  "name": "Brain Break",
  "about": "A short movement break when the class needs one: a moving figure shows each activity (star jumps, stretches, dance and freeze) with a countdown, and the break ends with slow breathing. Seated activities for small rooms.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BRK_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.brain-break.png", "height": 64, "alt": "Brain Break" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    { "type": "borderButton", "label": "START A BRAIN BREAK", "command": { "app_command": 3 } },
    { "type": "borderButton", "label": "NEXT ACTIVITY", "command": { "app_command": 5 } },
    { "type": "borderButton", "label": "STOP", "command": { "app_command": 4 } },
    { "type": "slider", "title": "Length", "min": 1, "max": 3, "default": 2, "suffix": " min", "command": { "app_command": 0, "args": ["minutes"] } },
    { "type": "switch", "title": "Seated Activities Only (Small Rooms)", "default": false, "command": { "app_command": 1, "args": ["seated"] } },
    { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 2, "args": ["sound"] } },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click to start a break, and to skip an activity. Hold the knob to stop. Before the start, turn it to change the length." },
        { "type": "bullet", "text": "Each activity lasts about 20 seconds. Dance and Freeze: dance until Pixlpal shows FREEZE, then stand still!" },
        { "type": "note", "text": "Every break ends with slow breathing, so the class is calm and ready to learn again." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3 }, "text": "Brain break!" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Brain Break on Pixlpal first" }
  ]
})json";

#endif
