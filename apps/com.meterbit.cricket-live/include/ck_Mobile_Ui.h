/**
 * @file ck_Mobile_Ui.h
 * @brief Cricket Live's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (cricketLive.cpp):
 *   0 {"apiKey": "..."}        a CricketData.org key (empty: forget it)
 *   1 {"team": "India"}        the team to follow (empty: none)
 *   2 {"minutes": 10-60}       3 {"seconds": 0|5-60}  seconds per match (0: knob only)
 *   4 {"sound": 0|1}           5 {}  check now
 */
#ifndef CK_MOBILE_UI_H
#define CK_MOBILE_UI_H

static const char CK_UI_MANIFEST[] = R"json({
  "name": "Cricket Live",
  "about": "Live cricket: runs, wickets and overs for every innings, what the chasing side needs, and today's matches and results, with your team first. A wicket in your team's match gets a call. From CricketData.org with your free key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CK_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.cricket-live.png", "height": 64, "alt": "Cricket Live" },
    { "type": "statusPanel", "title": "Matches", "source": "now", "emptyText": "Add your CricketData.org key below." },
    { "type": "textField", "title": "Your Team", "emptyHint": "India, Australia, Mumbai Indians...", "filledHint": "Change it (empty: none)", "command": { "app_command": 1, "args": ["team"] } },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "settings",
      "title": "Settings",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Check Every", "min": 10, "max": 60, "step": 5, "default": 20, "suffix": " min", "command": { "app_command": 2, "args": ["minutes"] } },
        { "type": "slider", "title": "Each Match For (0: Knob Only)", "min": 0, "max": 60, "step": 5, "default": 10, "suffix": " s", "command": { "app_command": 3, "args": ["seconds"] } },
        { "type": "switch", "title": "Sound for a Wicket in Your Team's Match", "default": true, "command": { "app_command": 4, "args": ["sound"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The free plan allows 100 checks a day: every 20 minutes uses 72. Check more often only on a paid plan." } ] }
      ]
    },
    {
      "type": "section",
      "id": "key",
      "title": "CricketData.org",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "Your CricketData.org key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 0, "args": ["apiKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Sign up free at cricketdata.org: the key is on your dashboard." },
          { "type": "note", "text": "The key is kept on Pixlpal only, and used only with CricketData.org." }
        ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "One match at a time: each innings' runs, wickets and overs, and the match's status. Turn the knob for the next match, click to check now." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Key saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That does not look like a CricketData.org key" },
    { "when": { "app_command": 1 }, "text": "Saved" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Cricket Live on Pixlpal first" }
  ]
})json";

#endif
