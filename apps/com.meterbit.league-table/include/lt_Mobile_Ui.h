/**
 * @file lt_Mobile_Ui.h
 * @brief League Table's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (leagueTable.cpp):
 *   0 {"league": 0-11}         the competition (football-data.org's free ones)
 *   1 {"query": "ars"}         find a team in the loaded table, answered as 248 ("teams")
 *   2 {"team": {"name", "value": "57"}}   your team (value: its id; "0": none)
 *   3 {"apiKey": "..."}        a football-data.org key (empty: forget it)
 *   4 {"seconds": 0|5-60}      seconds per page (0: change pages with the knob only)
 *   5 {}                       check now
 */
#ifndef LT_MOBILE_UI_H
#define LT_MOBILE_UI_H

static const char LT_UI_MANIFEST[] = R"json({
  "name": "League Table",
  "about": "Your football league on the panel: the table with points, goal difference and form, the top scorers, and this round's results and fixtures, with your team highlighted. Premier League, La Liga, Serie A, Bundesliga, Ligue 1, the Champions League and more.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char LT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.league-table.png", "height": 64, "alt": "League Table" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Add your football-data.org key below." },
    {
      "type": "radioGroup",
      "title": "Competition",
      "default": 0,
      "options": [
        { "label": "Premier League (England)", "value": 0 },
        { "label": "Championship (England)", "value": 1 },
        { "label": "La Liga (Spain)", "value": 2 },
        { "label": "Serie A (Italy)", "value": 3 },
        { "label": "Bundesliga (Germany)", "value": 4 },
        { "label": "Ligue 1 (France)", "value": 5 },
        { "label": "Eredivisie (Netherlands)", "value": 6 },
        { "label": "Primeira Liga (Portugal)", "value": 7 },
        { "label": "Brasileirão (Brazil)", "value": 8 },
        { "label": "UEFA Champions League", "value": 9 },
        { "label": "European Championship", "value": 10 },
        { "label": "FIFA World Cup", "value": 11 }
      ],
      "command": { "app_command": 0, "args": ["league"] }
    },
    {
      "type": "searchPicker",
      "title": "Your Team",
      "search": "teams",
      "multi": false,
      "minChars": 2,
      "emptyHint": "Search the table, such as Arsenal",
      "noResults": "No team of that name in this competition",
      "searchCommand": { "app_command": 1, "args": ["query"] },
      "pickCommand": { "app_command": 2, "args": ["team"] }
    },
    { "type": "slider", "title": "Each Page For (0: Knob Only)", "min": 0, "max": 60, "step": 5, "default": 15, "suffix": " s", "command": { "app_command": 4, "args": ["seconds"] } },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "key",
      "title": "football-data.org",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "API Key", "emptyHint": "Your football-data.org key", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 3, "args": ["apiKey"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Register free at football-data.org: the key comes by email. The free plan covers these twelve competitions, with 10 requests a minute: plenty for one Pixlpal." },
          { "type": "note", "text": "The key is kept on Pixlpal only, and used only with football-data.org." }
        ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Three pages: the table, the top scorers, and this round's results and fixtures. Turn the knob to change page, click to check now." },
        { "type": "bullet", "text": "Your team is highlighted, and the table opens around it." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 2 }, "text": "Team saved" },
    { "when": { "app_command": 3 }, "text": "Key saved" },
    { "when": { "app_command": 3, "response": 0 }, "text": "That does not look like a football-data.org key" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open League Table on Pixlpal first" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Open League Table on Pixlpal, and let the table load first" }
  ]
})json";

#endif
