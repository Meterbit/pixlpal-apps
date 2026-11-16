/**
 * @file sca_Mobile_Ui.h
 * @brief Scam Alerts' phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (scamAlerts.cpp):
 *   0 {"bank": "Lagoon Bank"}            1 {"tip0" ... "tip5": "Headline | what to know"}  the bank's own tips (empty removes)
 *   2 {"builtin": 0|1}                   Scam Alerts' own ten tips shown too
 *   3 {"alert": "..."}                   an urgent alert, shown between the tips in red (empty clears it)
 *   4 {"seconds": 6-60}                  each tip's time on screen
 *   5 {"contact": "Report fraud: 0800 123 456"}  the line along the bottom
 */
#ifndef SCA_MOBILE_UI_H
#define SCA_MOBILE_UI_H

static const char SCA_UI_MANIFEST[] = R"json({
  "name": "Scam Alerts",
  "about": "Fraud-awareness tips for a bank branch, in turn on the screen: never share your PIN or one-time code, there is no safe account, fake calls and links. Add the bank's own tips, and put up an urgent alert about a scam going round now.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SCA_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.scam-alerts.png", "height": 64, "alt": "Scam Alerts" },
    { "type": "statusPanel", "title": "On the Screen", "source": "shown" },
    { "type": "textField", "title": "Urgent Alert", "emptyHint": "e.g. Fake texts about blocked accounts are going round. Don't click the link.", "filledHint": "Change the alert (empty clears it)", "command": { "app_command": 3, "args": ["alert"] } },
    {
      "type": "section",
      "id": "tips",
      "title": "Your Own Tips",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Tip 1", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip0"] } },
        { "type": "textField", "title": "Tip 2", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip1"] } },
        { "type": "textField", "title": "Tip 3", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip2"] } },
        { "type": "textField", "title": "Tip 4", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip3"] } },
        { "type": "textField", "title": "Tip 5", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip4"] } },
        { "type": "textField", "title": "Tip 6", "emptyHint": "Headline | what to know", "filledHint": "Change it (empty removes it)", "command": { "app_command": 1, "args": ["tip5"] } },
        { "type": "switch", "title": "Show Scam Alerts' Own Ten Tips Too", "default": true, "command": { "app_command": 2, "args": ["builtin"] } }
      ]
    },
    {
      "type": "section",
      "id": "setup",
      "title": "The Screen",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Bank or Branch", "emptyHint": "e.g. Lagoon Bank", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["bank"] } },
        { "type": "textField", "title": "Line Along the Bottom", "emptyHint": "Report fraud: 0800 123 456", "filledHint": "Change it (empty removes it)", "command": { "app_command": 5, "args": ["contact"] } },
        { "type": "slider", "title": "Seconds Each Tip", "min": 6, "max": 60, "default": 12, "command": { "app_command": 4, "args": ["seconds"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The tips show in turn; an urgent alert comes up in red after each one. Turn the knob to go through them yourself." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1, "response": 0 }, "text": "Write a headline, a | and what to know" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Keep at least one tip: add your own first" },
    { "when": { "app_command": 3 }, "text": "Alert updated" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Keep the alert under 240 characters" }
  ]
})json";

#endif
