/**
 * @file nxt_Mobile_Ui.h
 * @brief Now & Next's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (nowNext.cpp):
 *   0 {"step0" ... "step9": "Reading 15"}   a step: its name and minutes (empty: removed); the picture follows the name
 *   1 {"auto": 0|1}    move on by itself when the time is up (0: wait for a click)
 *   2 {"sound": 0|1}
 *   3 {} start, or the next step      4 {} back a step      5 {} start the routine again
 */
#ifndef NXT_MOBILE_UI_H
#define NXT_MOBILE_UI_H

static const char NXT_UI_MANIFEST[] = R"json({
  "name": "Now & Next",
  "about": "A picture routine for young pupils and special-needs classrooms: what we are doing now, in a large picture with a bar of the time left, and what comes next. Calm, clear changes from one activity to the next.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char NXT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.now-next.png", "height": 64, "alt": "Now & Next" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    { "type": "borderButton", "label": "START / NEXT STEP", "command": { "app_command": 3 } },
    { "type": "borderButton", "label": "BACK A STEP", "command": { "app_command": 4 } },
    { "type": "borderButton", "label": "START THE ROUTINE AGAIN", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "routine",
      "title": "The Routine",
      "widgets": [
        { "type": "textField", "title": "Step 1", "emptyHint": "Name and minutes: Circle time 10", "filledHint": "Change step 1 (empty removes it)", "command": { "app_command": 0, "args": ["step0"] } },
        { "type": "textField", "title": "Step 2", "emptyHint": "Reading 15", "filledHint": "Change step 2", "command": { "app_command": 0, "args": ["step1"] } },
        { "type": "textField", "title": "Step 3", "emptyHint": "Snack 10", "filledHint": "Change step 3", "command": { "app_command": 0, "args": ["step2"] } },
        { "type": "textField", "title": "Step 4", "emptyHint": "Play outside 20", "filledHint": "Change step 4", "command": { "app_command": 0, "args": ["step3"] } },
        { "type": "textField", "title": "Step 5", "emptyHint": "Maths 15", "filledHint": "Change step 5", "command": { "app_command": 0, "args": ["step4"] } },
        { "type": "textField", "title": "Step 6", "emptyHint": "Name and minutes", "filledHint": "Change step 6", "command": { "app_command": 0, "args": ["step5"] } },
        { "type": "textField", "title": "Step 7", "emptyHint": "Name and minutes", "filledHint": "Change step 7", "command": { "app_command": 0, "args": ["step6"] } },
        { "type": "textField", "title": "Step 8", "emptyHint": "Name and minutes", "filledHint": "Change step 8", "command": { "app_command": 0, "args": ["step7"] } },
        { "type": "textField", "title": "Step 9", "emptyHint": "Name and minutes", "filledHint": "Change step 9", "command": { "app_command": 0, "args": ["step8"] } },
        { "type": "textField", "title": "Step 10", "emptyHint": "Name and minutes", "filledHint": "Change step 10", "command": { "app_command": 0, "args": ["step9"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "The picture follows the step's name: reading, writing, maths, science, art, music, PE or play, snack or lunch, circle time, computers, outside, rest or quiet, wash hands, tidy up, home time, trip, topic or world, languages. Anything else gets a star." },
          { "type": "note", "text": "Minutes from 1 to 120. Without minutes a step lasts until you move on." }
        ] }
      ]
    },
    { "type": "switch", "title": "Move On by Itself When the Time Is Up", "default": false, "command": { "app_command": 1, "args": ["auto"] } },
    { "type": "switch", "title": "Gentle Chime at Each Change", "default": true, "command": { "app_command": 2, "args": ["sound"] } },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click to start, and again to move to the next step. Turn the knob back or forward. Hold it to start the routine again." },
        { "type": "bullet", "text": "When a step's time is up, Pixlpal shows what is next and waits for you (unless it moves on by itself)." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Step saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write the step's name, then its minutes: Reading 15" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Now & Next on Pixlpal first, and write the routine" }
  ]
})json";

#endif
