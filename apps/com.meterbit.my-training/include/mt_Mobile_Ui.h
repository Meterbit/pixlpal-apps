/**
 * @file mt_Mobile_Ui.h
 * @brief My Training's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (myTraining.cpp):
 *   0 {"refreshToken": "..."}  link Strava (the phone signs in, through the account server)
 *   1 {}                       unlink Strava
 *   2 {"sport": 0-4}           count: everything, runs, rides, walks and hikes, swims
 *   3 {"goal": 0-1000}         a weekly distance goal in the units below (0: none)
 *   4 {"miles": 0|1}           kilometres or miles
 *   5 {}                       check now
 */
#ifndef MT_MOBILE_UI_H
#define MT_MOBILE_UI_H

static const char MT_UI_MANIFEST[] = R"json({
  "name": "My Training",
  "about": "Your week from Strava: distance against a weekly goal, time and climbing, a bar for each day, and your latest run or ride with its pace. Runs, rides, walks and swims, in kilometres or miles.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char MT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.my-training.png", "height": 64, "alt": "My Training" },
    { "type": "statusPanel", "title": "This Week", "source": "now", "emptyText": "Link your Strava account below." },
    {
      "type": "accountLink", "provider": "strava", "title": "Strava Account",
      "about": "Sign in with Strava so Pixlpal can show your week. Pixlpal only reads your activities, and shows them only on your Pixlpal.",
      "scopes": ["read", "activity:read"],
      "linkedValue": "linked", "accountValue": "account",
      "command": { "app_command": 0, "args": ["refreshToken"] },
      "unlinkCommand": { "app_command": 1 },
      "unlinkConfirm": "Unlink your Strava account from Pixlpal?"
    },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 5 }, "visibleWhen": { "arg": "linked", "equals": 1 } },
    {
      "type": "section",
      "id": "settings",
      "title": "Your Week",
      "widgets": [
        { "type": "radioGroup", "title": "Count", "default": 0, "options": [ { "label": "Everything", "value": 0 }, { "label": "Runs", "value": 1 }, { "label": "Rides", "value": 2 }, { "label": "Walks and hikes", "value": 3 }, { "label": "Swims", "value": 4 } ], "command": { "app_command": 2, "args": ["sport"] } },
        { "type": "slider", "title": "Weekly Goal (0: None)", "min": 0, "max": 300, "step": 5, "default": 30, "command": { "app_command": 3, "args": ["goal"] } },
        { "type": "radioGroup", "title": "Units", "default": 0, "options": [ { "label": "Kilometres", "value": 0 }, { "label": "Miles", "value": 1 } ], "command": { "app_command": 4, "args": ["miles"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The week runs from Monday. Pixlpal checks Strava every hour, and when you click the knob." },
        { "type": "note", "text": "Activities come from Strava, and stay between Strava and your Pixlpal." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Strava linked" },
    { "when": { "app_command": 1 }, "text": "Strava unlinked" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open My Training on Pixlpal first" }
  ]
})json";

#endif
