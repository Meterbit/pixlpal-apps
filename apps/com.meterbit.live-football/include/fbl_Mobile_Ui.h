/**
 * @file fbl_Mobile_Ui.h
 * @brief Mobile UI of the Live Football app, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Each widget's command reaches the handler with the same number in liveFootball.cpp:
 *   0 {"leagues": ["39", "140"]}       leagues followed, 1 to 8
 *   1 {"query": "arsenal"}             team search; the teams come back as command 248
 *   2 {"table": "39"}                  league of the table and scorers pages (paid plans)
 *   3 {"api_key"}                      API-Football key
 *   4 {"goals": 0-2}                   goal celebrations: off, favourite teams, every followed match
 *   5 {"liveMinutes": 1-15}            fastest live refresh
 *   6 {"cycle"} / {"seconds"}          page rotation
 *   7 {}                               refresh now
 *   8 {}                               show a goal celebration
 *   9 {"teams": [{"name","value"}]}    favourite teams, up to 8, by API-Football team ID
 *  10 {"quiet": 0|1}                   quiet hours on or off
 *  11 {"quietFrom", "quietTo"}         quiet hours, "HH:MM"
 * The argument names are also the keys of the values liveFootball.cpp reports; the status panel's keys (plan,
 * left, limit, pace, keyText, updated...) are reported too, and pushed when they change. The "leagues" dataset lists
 * fbl_Model.cpp's leagues. Success toasts come before failure toasts.
 */
#ifndef FBL_MOBILE_UI_H
#define FBL_MOBILE_UI_H

static const char FBL_UI_MANIFEST[] = R"json({
  "appID": "5/0",
  "name": "Live Football",
  "about": "Live scores with goal celebrations, today's matches, yesterday's results and your team's next kick-off, from API-Football.",
  "author": "Meterbit Cyb",
  "version": "v2.1.0"
})json";

static const char FBL_UI_API[] = R"json({
  "widgets": [
    {
      "type": "richTextBlock",
      "title": "Live Football",
      "blocks": [
        { "type": "paragraph", "text": "Live scores on a pitch, with crests, the minute, a timeline of goals and cards, and a celebration on the display when your team scores. Between matches: today's fixtures, yesterday's results and a countdown to your team's next kick-off." }
      ]
    },
    {
      "type": "statusPanel",
      "title": "Plan and Requests",
      "emptyText": "Launch Live Football on Pixlpal to see your plan and requests.",
      "rows": [
        { "label": "Plan", "value": "{plan}", "state": "{planState}" },
        { "label": "Includes", "value": "{features}" },
        { "label": "Requests", "value": "{left} of {limit} left today", "state": "{quotaState}", "meter": { "value": "left", "max": "limit" } },
        { "label": "Live updates", "value": "{pace}", "state": "{paceState}" },
        { "label": "API key", "value": "{keyText}", "state": "{keyState}" },
        { "label": "Scores", "ago": "updated" }
      ]
    },
    {
      "type": "section",
      "id": "teams",
      "title": "Teams and Leagues",
      "widgets": [
        {
          "type": "searchPicker",
          "title": "Favourite Teams",
          "search": "teams",
          "multi": true,
          "max": 8,
          "minChars": 3,
          "emptyHint": "Search a team, e.g. Arsenal",
          "noResults": "No team of that name. Try part of the name.",
          "searchCommand": { "app_command": 1, "args": ["query"] },
          "pickCommand": { "app_command": 9, "args": ["teams"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "note", "text": "Your teams' matches are shown in gold, from any league, with a countdown to the next one. Each search uses one of the day's requests." }
          ]
        },
        {
          "type": "dropDownList",
          "title": "Leagues to Follow",
          "dataset": "leagues",
          "buttonLabel": "CHOOSE UP TO 8",
          "sendAs": "array",
          "command": { "app_command": 0, "args": ["leagues"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "celebrations",
      "title": "Celebrations",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Goal Celebrations",
          "default": 2,
          "options": [
            { "label": "Off", "value": 0 },
            { "label": "My teams only", "value": 1 },
            { "label": "Every followed match", "value": 2 }
          ],
          "command": { "app_command": 4, "args": ["goals"] }
        },
        {
          "type": "switch",
          "title": "Quiet Hours",
          "default": false,
          "command": { "app_command": 10, "args": ["quiet"] },
          "visibleWhen": { "arg": "goals", "notEquals": 0 }
        },
        {
          "type": "timeRange",
          "title": "No Celebrations Between",
          "defaultFrom": "22:00",
          "defaultTo": "07:00",
          "command": { "app_command": 11, "args": ["quietFrom", "quietTo"] },
          "visibleWhen": { "arg": "quiet", "equals": 1 }
        },
        { "type": "borderButton", "label": "SHOW A GOAL CELEBRATION", "command": { "app_command": 8 } }
      ]
    },
    {
      "type": "section",
      "id": "display",
      "title": "Display",
      "collapsed": true,
      "widgets": [
        {
          "type": "switch",
          "title": "Rotate Pages",
          "default": true,
          "command": { "app_command": 6, "args": ["cycle"] }
        },
        {
          "type": "slider",
          "title": "Time per Page",
          "min": 4, "max": 60, "default": 10, "suffix": "Secs",
          "command": { "app_command": 6, "args": ["seconds"] },
          "visibleWhen": { "arg": "cycle", "equals": 1 }
        },
        {
          "type": "dropDownTextField",
          "title": "League for the Table",
          "dataset": "leagues",
          "dropDownItemCount": 6,
          "defaultValue": "39",
          "command": { "app_command": 2, "args": ["table"] },
          "visibleWhen": { "arg": "paid", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "api",
      "title": "API Key and Plan",
      "subtitle": "Your API-Football key, refresh and plans",
      "collapsed": true,
      "widgets": [
        {
          "type": "textField",
          "title": "API-Football Key",
          "secret": true,
          "emptyHint": "Paste your API-Football key",
          "filledHint": "Key saved on Pixlpal. Paste a new one to replace it",
          "command": { "app_command": 3, "args": ["api_key"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "paragraph", "spans": [
              { "text": "Get a free key (100 requests a day) at " },
              { "text": "dashboard.api-football.com", "link": "https://dashboard.api-football.com/register" },
              { "text": ". Pixlpal paces its requests so the day's quota lasts through the day's matches." }
            ] }
          ]
        },
        {
          "type": "slider",
          "title": "Fastest Live Refresh",
          "min": 1, "max": 15, "default": 2, "suffix": "Mins",
          "command": { "app_command": 5, "args": ["liveMinutes"] }
        },
        { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 7 }, "confirm": { "title": "Refresh now?", "text": "This uses one or two of today's API-Football requests.", "ok": "Refresh" } },
        {
          "type": "richTextBlock",
          "title": "Free and Paid Plans",
          "blocks": [
            { "type": "subheading", "text": "Free plan" },
            { "type": "bullet", "text": "Live scores, goals and cards for your leagues and teams." },
            { "type": "bullet", "text": "Yesterday's results, today's and tomorrow's fixtures." },
            { "type": "subheading", "text": "Paid plans" },
            { "type": "bullet", "text": "League table with form, and top scorers, for the league you choose under Display." },
            { "type": "bullet", "text": "Fixtures further ahead, and live updates as often as every minute." },
            { "type": "note", "text": "Upgrade on API-Football's site and keep your key: Pixlpal reads the plan from it and the extra pages appear by themselves. A new key just needs pasting above." },
            { "type": "paragraph", "spans": [
              { "text": "Plans: " },
              { "text": "api-football.com/pricing", "link": "https://www.api-football.com/pricing" }
            ] }
          ]
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Data",
      "blocks": [
        { "type": "paragraph", "text": "Data and crests from API-Football (api-sports.io), with your own key. Not affiliated with or endorsed by API-Football, the leagues or the clubs." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Leagues saved" },
    { "when": { "app_command": 1 }, "text": "Searching API-Football" },
    { "when": { "app_command": 2 }, "text": "Table league saved" },
    { "when": { "app_command": 3 }, "text": "API key saved" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 5 }, "text": "Saved" },
    { "when": { "app_command": 6 }, "text": "Saved" },
    { "when": { "app_command": 7 }, "text": "Refreshing" },
    { "when": { "app_command": 8 }, "text": "GOAL! on Pixlpal" },
    { "when": { "app_command": 9 }, "text": "Favourite teams saved" },
    { "when": { "app_command": 10 }, "text": "Saved" },
    { "when": { "app_command": 11 }, "text": "Quiet hours saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Choose 1 to 8 leagues from the list" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Can't search now: 3 to 30 letters, a key, and requests left today" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Unknown league" },
    { "when": { "app_command": 3, "response": 0 }, "text": "That does not look like an API-Football key" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Value out of range" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Value out of range" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Refreshed recently, or the day's quota is used up" },
    { "when": { "app_command": 8, "response": 0 }, "text": "No match to celebrate yet" },
    { "when": { "app_command": 9, "response": 0 }, "text": "Up to 8 teams" },
    { "when": { "app_command": 11, "response": 0 }, "text": "Start and end must differ" }
  ]
})json";

static const char FBL_UI_DATA[] = R"json({
  "datasets": {
    "leagues": [
      { "name": "Premier League (England)", "value": "39" },
      { "name": "Championship (England)", "value": "40" },
      { "name": "FA Cup", "value": "45" },
      { "name": "EFL Cup", "value": "48" },
      { "name": "La Liga (Spain)", "value": "140" },
      { "name": "Copa del Rey", "value": "143" },
      { "name": "Serie A (Italy)", "value": "135" },
      { "name": "Coppa Italia", "value": "137" },
      { "name": "Bundesliga (Germany)", "value": "78" },
      { "name": "DFB Pokal", "value": "81" },
      { "name": "Ligue 1 (France)", "value": "61" },
      { "name": "Coupe de France", "value": "66" },
      { "name": "Eredivisie (Netherlands)", "value": "88" },
      { "name": "Primeira Liga (Portugal)", "value": "94" },
      { "name": "Pro League (Belgium)", "value": "144" },
      { "name": "Premiership (Scotland)", "value": "179" },
      { "name": "Super Lig (Turkey)", "value": "203" },
      { "name": "Saudi Pro League", "value": "307" },
      { "name": "MLS (USA)", "value": "253" },
      { "name": "Liga MX (Mexico)", "value": "262" },
      { "name": "Brasileirao (Brazil)", "value": "71" },
      { "name": "Liga Profesional (Argentina)", "value": "128" },
      { "name": "NPFL (Nigeria)", "value": "399" },
      { "name": "Botola Pro (Morocco)", "value": "200" },
      { "name": "UEFA Champions League", "value": "2" },
      { "name": "UEFA Europa League", "value": "3" },
      { "name": "UEFA Conference League", "value": "848" },
      { "name": "CAF Champions League", "value": "12" },
      { "name": "Copa Libertadores", "value": "13" },
      { "name": "FIFA World Cup", "value": "1" },
      { "name": "UEFA Euro", "value": "4" },
      { "name": "Africa Cup of Nations", "value": "6" },
      { "name": "Copa America", "value": "9" },
      { "name": "UEFA Nations League", "value": "5" }
    ]
  }
})json";

#endif
