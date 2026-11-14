/**
 * @file clm_Mobile_Ui.h
 * @brief Calm Corner's phone UI {19,2}, for parents.
 *
 *   0 {"breathStyle"} | {"breaths"} | {"tones"} | {"lightsMinutes"} | {"keepLog"}
 *   1 {"a0".."a6"} | {"phoneAlerts"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
 *   2 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}   3 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}
 *   4 {} forget the feelings   5 {} a test message
 */
#ifndef CLM_MOBILE_UI_H
#define CLM_MOBILE_UI_H

static const char CLM_UI_MANIFEST[] = R"json({
  "appID": "19/2",
  "name": "Calm Corner",
  "about": "For children of 4 to 10: a quiet place to say how they feel, with seven faces, and to calm down: breathing with a growing balloon, finding things to see and hear, slow stretches, or calm lights with soft notes. You can see their feelings here and be told of hard ones. It never counts towards the daily time limit, and works at bedtime.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CLM_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "This Week", "source": "status", "emptyText": "Set up your children in Kids Learning." },
    {
      "type": "section", "id": "log", "title": "Feelings", "collapsed": true, "widgets": [
        { "type": "statusPanel", "source": "logRows", "emptyText": "Nothing yet" },
        { "type": "switch", "title": "Keep the Feelings", "default": true, "command": { "app_command": 0, "args": ["keepLog"] } },
        { "type": "borderButton", "label": "FORGET THEM ALL", "command": { "app_command": 4 }, "confirm": "Forget every feeling kept?" },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The last 40 are kept on Pixlpal only, and shown here. Nothing goes anywhere else unless you switch on messages below." } ] }
      ]
    },
    {
      "type": "section", "id": "calm", "title": "Calming Down", "collapsed": true, "widgets": [
        {
          "type": "radioGroup", "title": "Breathing", "default": 0,
          "options": [
            { "label": "Right for their age (Recommended)", "value": 0 },
            { "label": "Balloon: 3 in, 4 out (young children)", "value": 1 },
            { "label": "Box: 4 in, hold 4, 4 out, hold 4", "value": 2 }
          ],
          "command": { "app_command": 0, "args": ["breathStyle"] }
        },
        { "type": "slider", "title": "Breaths", "min": 3, "max": 10, "default": 5, "command": { "app_command": 0, "args": ["breaths"] } },
        { "type": "slider", "title": "Calm Lights For", "min": 1, "max": 10, "default": 3, "suffix": " min", "command": { "app_command": 0, "args": ["lightsMinutes"] } },
        { "type": "switch", "title": "Soft Notes", "default": true, "command": { "app_command": 0, "args": ["tones"] } }
      ]
    },
    {
      "type": "section", "id": "alerts", "title": "Tell Me", "subtitle": "When a child picks a hard feeling", "collapsed": true, "widgets": [
        { "type": "statusPanel", "source": "alertRows", "emptyText": "Off" },
        { "type": "switch", "title": "Send Messages to Phones", "default": false, "command": { "app_command": 1, "args": ["phoneAlerts"] } },
        { "type": "switch", "title": "Sad", "default": false, "command": { "app_command": 1, "args": ["a4"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "Worried", "default": false, "command": { "app_command": 1, "args": ["a5"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "Angry", "default": false, "command": { "app_command": 1, "args": ["a6"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "Tired", "default": false, "command": { "app_command": 1, "args": ["a3"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "ntfy (Recommended)", "default": false, "command": { "app_command": 1, "args": ["ntfyOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install the free ntfy app (Android or iPhone), tap +, and subscribe to the topic shown above. No account is needed." },
          { "type": "paragraph", "spans": [ { "text": "ntfy: " }, { "text": "ntfy.sh", "link": "https://ntfy.sh" } ] }
        ], "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Topic (Optional: Your Own)", "emptyHint": "A long, hard-to-guess name", "command": { "app_command": 2, "args": ["ntfyTopic"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "ntfy.sh", "command": { "app_command": 2, "args": ["ntfyServer"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Access Token (Protected Topics Only)", "emptyHint": "tk_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["ntfyToken"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "switch", "title": "Telegram", "default": false, "command": { "app_command": 1, "args": ["telegramOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "textField", "title": "Bot Token", "emptyHint": "123456:ABC...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["telegramToken"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Chat ID", "emptyHint": "123456789", "command": { "app_command": 2, "args": ["telegramChat"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "switch", "title": "WhatsApp (CallMeBot)", "default": false, "command": { "app_command": 1, "args": ["whatsappOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "textField", "title": "WhatsApp Number", "emptyHint": "+2348012345678", "command": { "app_command": 2, "args": ["whatsappPhone"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "CallMeBot API Key", "emptyHint": "123456", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "command": { "app_command": 3, "args": ["whatsappKey"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "borderButton", "label": "SEND A TEST MESSAGE", "command": { "app_command": 5 }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } }
      ]
    },
    {
      "type": "richTextBlock", "title": "About", "blocks": [
        { "type": "paragraph", "text": "Calm Corner helps children name feelings and calm their bodies. It is not a replacement for talking with them, or for professional help when a child is often upset." },
        { "type": "note", "text": "On Pixlpal: turn the knob to choose a face, press to choose; hold to go back. Children are set up in Kids Learning." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 4 }, "text": "Forgotten" },
    { "when": { "app_command": 5 }, "text": "Test message sent" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Switch on ntfy, Telegram or WhatsApp first, and open the app on Pixlpal" }
  ]
})json";

#endif
