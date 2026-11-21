/**
 * @file tt_Mobile_Ui.h
 * @brief Team Tracker's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (teamTracker.cpp):
 *   0 {"apiKey": "..."}        an API-Sports key (empty: forget it)
 *   1 {"sport": 0-6}           the sport to search in: basketball, American football, rugby, ice hockey, baseball,
 *                              handball, volleyball
 *   2 {"query": "lakers"}      a team search in that sport, answered as 248 ("teams")
 *   3 {"team": {"name", "value": "sport:id"}}   add the team (up to four)
 *   4 {"remove": 0-3}          remove a team
 *   5 {}                       check now
 */
#ifndef TT_MOBILE_UI_H
#define TT_MOBILE_UI_H

static const char TT_UI_MANIFEST[] = R"json({
  "name": "Team Tracker",
  "about": "Your teams beyond football: the live score, the next game and the last result for up to four teams in basketball, American football, rugby, ice hockey, baseball, handball or volleyball. From API-Sports with your free key.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.team-tracker.png", "height": 64, "alt": "Team Tracker" },
    { "type": "statusPanel", "title": "Your Teams", "source": "now", "emptyText": "Add your API-Sports key, then your teams." },
    {
      "type": "section",
      "id": "add",
      "title": "Add a Team",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Sport",
          "default": 0,
          "options": [
            { "label": "Basketball", "value": 0 },
            { "label": "American football", "value": 1 },
            { "label": "Rugby", "value": 2 },
            { "label": "Ice hockey", "value": 3 },
            { "label": "Baseball", "value": 4 },
            { "label": "Handball", "value": 5 },
            { "label": "Volleyball", "value": 6 }
          ],
          "command": { "app_command": 1, "args": ["sport"] }
        },
        {
          "type": "searchPicker",
          "title": "Team",
          "search": "teams",
          "multi": false,
          "minChars": 3,
          "emptyHint": "Search, such as Lakers",
          "noResults": "No team of that name in this sport",
          "searchCommand": { "app_command": 2, "args": ["query"] },
          "pickCommand": { "app_command": 3, "args": ["team"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "remove",
      "title": "Remove a Team",
      "collapsed": true,
      "widgets": [
        { "type": "borderButton", "label": "REMOVE TEAM 1", "command": { "app_command": 4, "remove": 0 }, "visibleWhen": { "arg": "team0", "set": true } },
        { "type": "borderButton", "label": "REMOVE TEAM 2", "command": { "app_command": 4, "remove": 1 }, "visibleWhen": { "arg": "team1", "set": true } },
        { "type": "borderButton", "label": "REMOVE TEAM 3", "command": { "app_command": 4, "remove": 2 }, "visibleWhen": { "arg": "team2", "set": true } },
        { "type": "borderButton", "label": "REMOVE TEAM 4", "command": { "app_command": 4, "remove": 3 }, "visibleWhen": { "arg": "team3", "set": true } }
      ]
    },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "key",
      "title": "API-Sports",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "Your API-Sports key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 0, "args": ["apiKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Sign up free at dashboard.api-football.com: one key works for every API-Sports sport, each with its own 100 requests a day on the free plan. The key you use for Live Football works here too." },
          { "type": "bullet", "text": "Pixlpal checks each team every 30 minutes, and every 5 while one of its games is being played." },
          { "type": "note", "text": "The key is kept on Pixlpal only, and used only with API-Sports." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Key saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That does not look like an API-Sports key" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Open Team Tracker on Pixlpal, and add your key first" },
    { "when": { "app_command": 3 }, "text": "Team added" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Four teams already: remove one first" },
    { "when": { "app_command": 4 }, "text": "Removed" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Team Tracker on Pixlpal first" }
  ]
})json";

#endif
