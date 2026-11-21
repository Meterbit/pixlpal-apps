/**
 * @file brt_Mobile_Ui.h
 * @brief Breathe's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (breathe.cpp):
 *   0 {"start": 1|0}           start or stop a session (rejected when Breathe isn't open)
 *   1 {"pattern": 0-3}         box (4-4-4-4), 4-7-8, slow (5-5), longer out (4-6)
 *   2 {"minutes": 1-10}        3 {"sound": 0|1}        4 {"colour": 0-3}  teal, blue, violet, amber
 */
#ifndef BRT_MOBILE_UI_H
#define BRT_MOBILE_UI_H

static const char BRT_UI_MANIFEST[] = R"json({
  "name": "Breathe",
  "about": "Guided breathing: a shape on the panel grows as you breathe in and shrinks as you breathe out, for one to ten minutes. Box breathing, 4-7-8, slow breathing or a longer breath out, with soft tones if you like.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BRT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.breathe.png", "height": 64, "alt": "Breathe" },
    { "type": "statusPanel", "title": "Breathe", "source": "now" },
    { "type": "borderButton", "label": "START", "command": { "app_command": 0, "start": 1 } },
    { "type": "borderButton", "label": "STOP", "command": { "app_command": 0, "start": 0 } },
    {
      "type": "radioGroup",
      "title": "Pattern",
      "default": 2,
      "options": [
        { "label": "Box: in 4, hold 4, out 4, hold 4", "value": 0 },
        { "label": "4-7-8: in 4, hold 7, out 8", "value": 1 },
        { "label": "Slow: in 5, out 5 (six breaths a minute)", "value": 2 },
        { "label": "Longer out: in 4, out 6", "value": 3 }
      ],
      "command": { "app_command": 1, "args": ["pattern"] }
    },
    { "type": "slider", "title": "Session", "min": 1, "max": 10, "default": 3, "suffix": " min", "command": { "app_command": 2, "args": ["minutes"] } },
    {
      "type": "section",
      "id": "look",
      "title": "Look and Sound",
      "collapsed": true,
      "widgets": [
        { "type": "radioGroup", "title": "Colour", "default": 0, "options": [ { "label": "Teal", "value": 0 }, { "label": "Blue", "value": 1 }, { "label": "Violet", "value": 2 }, { "label": "Amber", "value": 3 } ], "command": { "app_command": 4, "args": ["colour"] } },
        { "type": "switch", "title": "Soft Tone at Each Change", "default": true, "command": { "app_command": 3, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "How To",
      "blocks": [
        { "type": "bullet", "text": "Breathe in as the shape grows, hold while it stays, breathe out as it shrinks. Gently, through the nose if you can." },
        { "type": "bullet", "text": "On Pixlpal: click to start or pause, turn to choose the pattern, long press to stop." },
        { "type": "note", "text": "If you feel dizzy or short of breath, stop and breathe normally. 4-7-8 holds the breath longest: try the slow pattern first." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Breathe on Pixlpal first" }
  ]
})json";

#endif
