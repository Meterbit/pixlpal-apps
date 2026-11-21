/**
 * @file onc_Mobile_Ui.h
 * @brief On-Call Board's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (onCall.cpp):
 *   0 {"service": 0-2}            PagerDuty, PagerDuty (EU), incident.io
 *   1 {"apiKey": "..."}           the service's API key (empty: forget it)
 *   2 {"who": "..."}              PagerDuty: an escalation policy's ID; incident.io: a schedule's ID (empty: PagerDuty
 *                                 shows every policy's first level, incident.io no one)
 *   3 {"minutes": 1-15}           4 {"sound": 0|1} | {"nag": 0|1}   the alarm; and again every minute until answered
 *   5 {}                          check now
 */
#ifndef ONC_MOBILE_UI_H
#define ONC_MOBILE_UI_H

static const char ONC_UI_MANIFEST[] = R"json({
  "name": "On-Call Board",
  "about": "Your team's open incidents from PagerDuty or incident.io and who is on call, readable across the room. A new incident sounds an alarm and flashes red until someone takes it; all clear shows green.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char ONC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.on-call.png", "height": 64, "alt": "On-Call Board" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Choose the service and add its API key below." },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "access",
      "title": "Service",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Incidents From",
          "default": 0,
          "options": [
            { "label": "PagerDuty", "value": 0 },
            { "label": "PagerDuty (EU account)", "value": 1 },
            { "label": "incident.io", "value": 2 }
          ],
          "command": { "app_command": 0, "args": ["service"] }
        },
        { "type": "textField", "title": "API Key", "emptyHint": "The service's API key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 1, "args": ["apiKey"] } },
        { "type": "textField", "title": "Escalation Policy ID (Optional)", "emptyHint": "PXXXXXX (empty: every policy's first level)", "filledHint": "Change it (empty: every policy)", "command": { "app_command": 2, "args": ["who"] }, "visibleWhen": { "arg": "service", "in": [0, 1] } },
        { "type": "textField", "title": "On-Call Schedule ID", "emptyHint": "01G0J1EXE7AXZ2C93K61WBPYEH", "filledHint": "Change it (empty: no one shown)", "command": { "app_command": 2, "args": ["who"] }, "visibleWhen": { "arg": "service", "equals": 2 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "PagerDuty: Integrations, API Access Keys, Create New API Key, with Read-only API Key ticked. An escalation policy's ID is the last part of its page's address." },
          { "type": "bullet", "text": "incident.io: Settings, API keys, with View data access (incidents and schedules). A schedule's ID is the last part of its page's address." },
          { "type": "note", "text": "The key is kept on Pixlpal only, and used only with the service you choose." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Checking and Alarm",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Check Every", "min": 1, "max": 15, "default": 2, "suffix": " min", "command": { "app_command": 3, "args": ["minutes"] } },
        { "type": "switch", "title": "Alarm for a New Incident", "default": true, "command": { "app_command": 4, "args": ["sound"] } },
        { "type": "switch", "title": "Sound Again Every Minute Until It Is Acknowledged", "default": false, "command": { "app_command": 4, "args": ["nag"] }, "visibleWhen": { "arg": "sound", "equals": 1 } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Red: incidents no one has taken yet (PagerDuty's triggered, incident.io's triage). Amber: acknowledged or being worked on. Green: all clear." },
        { "type": "bullet", "text": "Click the knob to quiet the alarm until the next incident (or to check now). Turn it to see more incidents." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Key saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "That does not look like an API key" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Write the ID only: letters and digits" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open On-Call Board on Pixlpal first" }
  ]
})json";

#endif
