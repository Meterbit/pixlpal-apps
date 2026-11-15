/**
 * @file npk_Mobile_Ui.h
 * @brief Name Picker's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (namePicker.cpp):
 *   0 {"names": "Ada, Ben, Chi"}   the class, up to 40, separated by commas
 *   1 {"size": 2-8}                people a group           2 {"repeat": 0|1}   1: anyone may be picked again at once
 *   3 {"sound": 0|1}               4 {"mode": 0|1}          0 pick a name, 1 groups
 *   5 {}  pick a name     6 {}  make new groups     7 {}  start a new round (everyone can be picked again)
 */
#ifndef NPK_MOBILE_UI_H
#define NPK_MOBILE_UI_H

static const char NPK_UI_MANIFEST[] = R"json({
  "name": "Name Picker",
  "about": "Picks who answers next: the class's names spin and land on one, and nobody is picked twice until everyone has had a turn. It also splits the class into random groups or pairs.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char NPK_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.name-picker.png", "height": 64, "alt": "Name Picker" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    { "type": "borderButton", "label": "PICK A NAME", "command": { "app_command": 5 } },
    { "type": "borderButton", "label": "MAKE GROUPS", "command": { "app_command": 6 } },
    { "type": "radioGroup", "title": "On Pixlpal", "default": 0, "options": [ { "label": "Pick a name", "value": 0 }, { "label": "Groups", "value": 1 } ], "command": { "app_command": 4, "args": ["mode"] } },
    { "type": "statusPanel", "title": "Groups", "source": "groups", "emptyText": "No groups yet: press Make Groups." },
    {
      "type": "section",
      "id": "class",
      "title": "The Class",
      "widgets": [
        { "type": "textField", "title": "Names, Separated by Commas", "emptyHint": "Ada, Ben, Chi, Dayo, Emeka", "filledHint": "Change the class (up to 40)", "command": { "app_command": 0, "args": ["names"] } },
        { "type": "slider", "title": "People in a Group", "min": 2, "max": 8, "default": 4, "command": { "app_command": 1, "args": ["size"] } },
        { "type": "switch", "title": "Anyone May Be Picked Again at Once", "default": false, "command": { "app_command": 2, "args": ["repeat"] } },
        { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 3, "args": ["sound"] } },
        { "type": "borderButton", "label": "START A NEW ROUND", "command": { "app_command": 7 } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click to pick a name (or to make new groups). Double click to switch between picking and groups." },
        { "type": "bullet", "text": "Turn the knob to page through the groups. Hold it to start a new round of picking." },
        { "type": "note", "text": "Unless you allow repeats, nobody is picked twice until everyone has been picked once." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Class saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write between 2 and 40 names, separated by commas" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Name Picker on Pixlpal first, and write the names" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Open Name Picker on Pixlpal first, and write the names" },
    { "when": { "app_command": 7 }, "text": "New round: everyone can be picked again" }
  ]
})json";

#endif
