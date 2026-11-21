/**
 * @file wb_Mobile_Ui.h
 * @brief Water & Breaks' phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (waterBreaks.cpp):
 *   0 {"glass": 1|-1}          a glass of water, or take the last one back
 *   1 {"goal": 4-16}           glasses a day           8 {"ml": 100-500}  millilitres a glass
 *   2 {"from": "09:00", "to": "17:00"}   working hours  3 {"days": 0-2}  every day, Monday-Friday, Monday-Saturday
 *   4 {"stand": 0|15-120}      minutes between stand-up breaks (0: none)
 *   5 {"eyes": 0|1}            20-20-20 eye breaks     6 {"water": 0|30-240}  remind after this long without a glass
 *   7 {"sound": 0|1}
 */
#ifndef WB_MOBILE_UI_H
#define WB_MOBILE_UI_H

static const char WB_UI_MANIFEST[] = R"json({
  "name": "Water & Breaks",
  "about": "For your desk: glasses of water against a daily goal (click the knob for each), reminders to stand up and stretch, and 20-20-20 eye breaks for screen work, all within your working hours.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char WB_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.water-breaks.png", "height": 64, "alt": "Water & Breaks" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    { "type": "borderButton", "label": "I HAD A GLASS OF WATER", "command": { "app_command": 0, "glass": 1 } },
    { "type": "borderButton", "label": "TAKE THE LAST GLASS BACK", "command": { "app_command": 0, "glass": -1 } },
    {
      "type": "section",
      "id": "water",
      "title": "Water",
      "widgets": [
        { "type": "slider", "title": "Glasses a Day", "min": 4, "max": 16, "default": 8, "command": { "app_command": 1, "args": ["goal"] } },
        { "type": "slider", "title": "A Glass Holds", "min": 100, "max": 500, "step": 50, "default": 250, "suffix": " ml", "command": { "app_command": 8, "args": ["ml"] } },
        { "type": "slider", "title": "Remind Me After (0: Never)", "min": 0, "max": 240, "step": 15, "default": 90, "suffix": " min", "command": { "app_command": 6, "args": ["water"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Eight glasses is a common rule of thumb, not a prescription: how much you need depends on you, the weather and what you do. Follow your doctor's advice if you have been given any." } ] }
      ]
    },
    {
      "type": "section",
      "id": "breaks",
      "title": "Breaks",
      "widgets": [
        { "type": "slider", "title": "Stand Up and Stretch Every (0: Never)", "min": 0, "max": 120, "step": 15, "default": 45, "suffix": " min", "command": { "app_command": 4, "args": ["stand"] } },
        { "type": "switch", "title": "Eye Breaks (20-20-20)", "default": true, "command": { "app_command": 5, "args": ["eyes"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "bullet", "text": "20-20-20: every 20 minutes of screen work, look at something about 20 feet (6 metres) away for 20 seconds. Pixlpal counts the 20 seconds." } ] }
      ]
    },
    {
      "type": "section",
      "id": "hours",
      "title": "Working Hours",
      "collapsed": true,
      "widgets": [
        { "type": "timeRange", "title": "Remind Me Between", "mode": "range", "defaultFrom": "09:00", "defaultTo": "17:00", "command": { "app_command": 2, "args": ["from", "to"] } },
        { "type": "radioGroup", "title": "On", "default": 1, "options": [ { "label": "Every day", "value": 0 }, { "label": "Monday to Friday", "value": 1 }, { "label": "Monday to Saturday", "value": 2 } ], "command": { "app_command": 3, "args": ["days"] } },
        { "type": "switch", "title": "Soft Sound With Each Reminder", "default": true, "command": { "app_command": 7, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click the knob: a glass of water (or, during a reminder, done). Double click: take the last glass back." },
        { "type": "bullet", "text": "Reminders come only within your working hours, and go by themselves after a minute." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Nothing to take back" },
    { "when": { "app_command": 2 }, "text": "Working hours saved" }
  ]
})json";

#endif
