/**
 * @file kid_Mobile_Ui.h
 * @brief Mobile UI of Kids' Learning & Pixel Games {19,0} (KIDS), for parents, rendered by the Pixlpal phone app
 *        (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (kidsLearning.cpp): 0 acknowledged   1 {"childSlot"}   2 {"childName"} | {"childAge"} | {"childOn"}
 *   3 {"limitMinutes"} | {"bedtimeOn"} | {"rewardMinutes"} | {"maxGameMinutes"} | {"readGoal"} | {"speak"} | {"language"} |
 *   {"sound"} | {"easyAnswers"}   4 {"bedFrom", "bedTo"}   5 {"query"} tables, 248 "tables"   6 {"tables": [...]}
 *   7 {"words"}   8 15 more minutes today   9 start the child's progress again   10 this child plays now
 */
#ifndef KID_MOBILE_UI_H
#define KID_MOBILE_UI_H

static const char KID_UI_MANIFEST[] = R"json({
  "appID": "19/0",
  "name": "Kids Learning",
  "about": "Maths that follows how your child is doing, a times-tables race, spelling they hear and spell, a reading timer, and two original games they earn by learning. For ages 4 to 12. You set the time limit, bedtime and the school's spelling words, and see progress here.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char KID_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:kidsLearning.png", "height": 64, "alt": "Kids Learning" },
    { "type": "statusPanel", "title": "Your Children", "source": "children" },
    {
      "type": "section",
      "id": "child",
      "title": "A Child",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Child",
          "default": 0,
          "options": [ { "label": "1", "value": 0 }, { "label": "2", "value": 1 }, { "label": "3", "value": 2 } ],
          "command": { "app_command": 1, "args": ["childSlot"] }
        },
        { "type": "statusPanel", "title": "Progress", "source": "progress" },
        { "type": "switch", "title": "Plays on This Pixlpal", "default": true, "command": { "app_command": 2, "args": ["childOn"] } },
        { "type": "textField", "title": "Name", "emptyHint": "Tobi", "command": { "app_command": 2, "args": ["childName"] } },
        { "type": "slider", "title": "Age", "min": 4, "max": 12, "default": 7, "step": 1, "suffix": "Years", "command": { "app_command": 2, "args": ["childAge"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The age sets where maths and spelling start; then they follow how the child does (up after five right in a row, down after two wrong)." } ] },
        { "type": "borderButton", "label": "15 MORE MINUTES TODAY", "command": { "app_command": 8 } },
        { "type": "borderButton", "label": "PLAY AS THIS CHILD NOW", "command": { "app_command": 10 } },
        { "type": "borderButton", "label": "START THEIR PROGRESS AGAIN", "confirm": { "title": "Start again?", "text": "Stars, levels and records go back to the start for this child.", "ok": "Start again", "cancel": "Keep" }, "command": { "app_command": 9 } }
      ]
    },
    {
      "type": "section",
      "id": "limits",
      "title": "Time and Bedtime",
      "widgets": [
        { "type": "slider", "title": "Time on Pixlpal a Day (0: No Limit)", "min": 0, "max": 240, "default": 60, "step": 5, "suffix": "Mins", "command": { "app_command": 3, "args": ["limitMinutes"] } },
        { "type": "switch", "title": "Bedtime (Nothing to Play)", "default": false, "command": { "app_command": 3, "args": ["bedtimeOn"] } },
        { "type": "timeRange", "title": "From and Until", "mode": "range", "defaultFrom": "20:00", "defaultTo": "07:00", "fromLabel": "From", "toLabel": "Until", "command": { "app_command": 4, "args": ["bedFrom", "bedTo"] }, "visibleWhen": { "arg": "bedtimeOn", "equals": 1 } },
        { "type": "slider", "title": "Game Time for a Good Round (7 of 10 Right)", "min": 0, "max": 15, "default": 3, "step": 1, "suffix": "Mins", "command": { "app_command": 3, "args": ["rewardMinutes"] } },
        { "type": "slider", "title": "Game Time a Day at Most", "min": 0, "max": 120, "default": 20, "step": 5, "suffix": "Mins", "command": { "app_command": 3, "args": ["maxGameMinutes"] } }
      ]
    },
    {
      "type": "section",
      "id": "learning",
      "title": "Learning",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "The School's Spelling Words (Optional)", "emptyHint": "mosquito, umbrella, calendar...", "command": { "app_command": 7, "args": ["words"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Separate the words with commas. Leave empty for the built-in lists by age." } ] },
        {
          "type": "searchPicker",
          "title": "Times Tables in the Race",
          "search": "tables",
          "multi": true,
          "max": 11,
          "minChars": 0,
          "emptyHint": "Search with nothing typed to see them all",
          "noResults": "2 to 12",
          "searchCommand": { "app_command": 5, "args": ["query"] },
          "pickCommand": { "app_command": 6, "args": ["tables"] }
        },
        { "type": "slider", "title": "Reading Goal a Day", "min": 5, "max": 120, "default": 20, "step": 5, "suffix": "Mins", "command": { "app_command": 3, "args": ["readGoal"] } },
        { "type": "switch", "title": "Always Choose the Answer (Even With a Keyboard)", "default": false, "command": { "app_command": 3, "args": ["easyAnswers"] } }
      ]
    },
    {
      "type": "section",
      "id": "sound",
      "title": "Sound and Voice",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 3, "args": ["sound"] } },
        { "type": "switch", "title": "Speak the Words and Questions (Needs the Internet)", "default": true, "command": { "app_command": 3, "args": ["speak"] } },
        {
          "type": "radioGroup",
          "title": "Voice",
          "default": 0,
          "options": [ { "label": "English", "value": 0 }, { "label": "French", "value": 1 }, { "label": "Spanish", "value": 2 }, { "label": "Portuguese", "value": 3 },
                       { "label": "German", "value": 4 }, { "label": "Italian", "value": 5 }, { "label": "Dutch", "value": 6 }, { "label": "Swahili", "value": 7 } ],
          "command": { "app_command": 3, "args": ["language"] },
          "visibleWhen": { "arg": "speak", "equals": 1 }
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose, click to pick or answer, hold to go back. In spelling: click adds the letter, two clicks undo it (or say the word again), hold checks the word." },
        { "type": "bullet", "text": "With a USB keyboard plugged in, answers and words are typed (Enter to check, Esc to go back)." },
        { "type": "bullet", "text": "Games are earned: a good round of maths, spelling or the race adds game time, up to the day's limit." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 2, "response": 0 }, "text": "One child at least must play" },
    { "when": { "app_command": 8 }, "text": "15 more minutes today" },
    { "when": { "app_command": 9 }, "text": "Progress started again" },
    { "when": { "app_command": 10 }, "text": "Switched on Pixlpal" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Switch this child on first" }
  ]
})json";

#endif
