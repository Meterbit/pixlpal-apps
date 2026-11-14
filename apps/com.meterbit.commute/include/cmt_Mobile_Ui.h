/**
 * @file cmt_Mobile_Ui.h
 * @brief Mobile UI of Commute & Departures {10,0}, rendered by the Pixlpal phone app (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (commute.cpp): 0 acknowledged   1 refresh now   2 {"transitKey"} (secret)   3 {"plan"}   4 {"paygCap"}
 *   5 {"drive"}   6 {"tomtomKey"} (secret)   7 {"googleKey"} (secret)   8 {"googleCap"}   9 {"placeSlot"}
 *   10 {"query"} addresses, 248 "address"   11 {"address": {name, value: "lat,lon"}}   12 {"editing"}   13 {"kind"}
 *   14 {"label"}   15 {"query"} stops, 248 "stop"   16 {"stop": {name, value}}   17 {"query"} lines, 248 "lines"
 *   18 {"lines": [...]}   19 {"walk"}   20 {"from"}   21 {"to"}   22 {"miles"}   23 {"arriveBy"}   24 {"days"}
 *   25 {"windowFrom", "windowTo"}   26 {"alert"}   27 remove the board   28 {"hour24"}   29 {"chime"}
 *   30 {"alertMinutes"}   31 {"avoid"}
 */
#ifndef CMT_MOBILE_UI_H
#define CMT_MOBILE_UI_H

static const char CMT_UI_MANIFEST[] = R"json({
  "appID": "10/0",
  "name": "Commute",
  "about": "When to leave, at a glance: live departures from your bus, tram, train and metro stops (thousands of transit agencies through Transitland) and drive times with today's traffic between your places (TomTom or Google), with a chime when it is time to go.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char CMT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:commute.png", "height": 64, "alt": "Commute" },
    { "type": "statusPanel", "title": "Your Boards", "emptyText": "No board yet: set one up below.", "source": "boards" },
    { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 1 } },
    {
      "type": "section",
      "id": "providers",
      "title": "Providers and Keys",
      "subtitle": "Free plans are enough for most commutes",
      "widgets": [
        { "type": "statusPanel", "title": "This Month", "source": "usage" },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "paragraph", "spans": [
              { "text": "Departures come from Transitland. Get a free API key at " },
              { "text": "transit.land", "link": "https://www.transit.land/documentation/rest-api/" },
              { "text": " (sign in, then the API key page). The free Explorer plan is for personal, non-commercial use." }
            ] }
          ]
        },
        {
          "type": "textField",
          "title": "Transitland API Key",
          "emptyHint": "Paste your Transitland key",
          "filledHint": "Saved on Pixlpal: paste again to change it",
          "secret": true,
          "obscureText": true,
          "command": { "app_command": 2, "args": ["transitKey"] }
        },
        {
          "type": "radioGroup",
          "title": "Transitland Plan",
          "default": 0,
          "options": [
            { "label": "Explorer (free, 5,000 a month)", "value": 0 },
            { "label": "Hobbyist (50,000 a month)", "value": 1 },
            { "label": "Pay As You Go (10,000 free, then billed)", "value": 2 }
          ],
          "command": { "app_command": 3, "args": ["plan"] }
        },
        {
          "type": "slider",
          "title": "Pay As You Go: Stop After",
          "min": 1000, "max": 60000, "default": 10000, "step": 1000, "suffix": "Requests",
          "command": { "app_command": 4, "args": ["paygCap"] },
          "visibleWhen": { "arg": "plan", "equals": 2 }
        },
        {
          "type": "radioGroup",
          "title": "Drive Times From",
          "default": 0,
          "options": [
            { "label": "TomTom (free tier, no card needed)", "value": 0 },
            { "label": "Google Routes (5,000 a month free, card needed)", "value": 1 }
          ],
          "command": { "app_command": 5, "args": ["drive"] }
        },
        {
          "type": "richTextBlock",
          "blocks": [ { "type": "paragraph", "spans": [
            { "text": "Get a free TomTom key at " }, { "text": "developer.tomtom.com", "link": "https://developer.tomtom.com/user/register" },
            { "text": ". It also looks up your addresses." }
          ] } ],
          "visibleWhen": { "arg": "drive", "equals": 0 }
        },
        {
          "type": "textField",
          "title": "TomTom API Key",
          "emptyHint": "Paste your TomTom key",
          "filledHint": "Saved on Pixlpal: paste again to change it",
          "secret": true,
          "obscureText": true,
          "command": { "app_command": 6, "args": ["tomtomKey"] },
          "visibleWhen": { "arg": "drive", "equals": 0 }
        },
        {
          "type": "richTextBlock",
          "blocks": [ { "type": "paragraph", "spans": [
            { "text": "In the " }, { "text": "Google Cloud console", "link": "https://console.cloud.google.com/google/maps-apis/" },
            { "text": ", enable the Routes API and the Geocoding API and create a key. Traffic-aware routes are billed beyond the free 5,000 a month; Pixlpal stops at the cap below." }
          ] } ],
          "visibleWhen": { "arg": "drive", "equals": 1 }
        },
        {
          "type": "textField",
          "title": "Google Maps API Key",
          "emptyHint": "Paste your Google key",
          "filledHint": "Saved on Pixlpal: paste again to change it",
          "secret": true,
          "obscureText": true,
          "command": { "app_command": 7, "args": ["googleKey"] },
          "visibleWhen": { "arg": "drive", "equals": 1 }
        },
        {
          "type": "slider",
          "title": "Google: Stop After",
          "min": 0, "max": 60000, "default": 5000, "step": 500, "suffix": "Routes",
          "command": { "app_command": 8, "args": ["googleCap"] },
          "visibleWhen": { "arg": "drive", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "places",
      "title": "Your Places",
      "subtitle": "For drive times, and stops near you",
      "widgets": [
        { "type": "statusPanel", "title": "Places", "source": "places" },
        {
          "type": "radioGroup",
          "title": "Set the Address Of",
          "default": 0,
          "options": [ { "label": "Home", "value": 0 }, { "label": "Work", "value": 1 }, { "label": "School", "value": 2 }, { "label": "Other", "value": 3 } ],
          "command": { "app_command": 9, "args": ["placeSlot"] }
        },
        {
          "type": "searchPicker",
          "title": "Address",
          "search": "address",
          "multi": false,
          "minChars": 3,
          "emptyHint": "Street and town, e.g. 12 Allen Avenue, Ikeja",
          "noResults": "Not found: add the town or the country",
          "searchCommand": { "app_command": 10, "args": ["query"] },
          "pickCommand": { "app_command": 11, "args": ["address"] }
        }
      ]
    },
    {
      "type": "section",
      "id": "board",
      "title": "Set Up a Board",
      "subtitle": "Up to six: departures from a stop, or a drive",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Board",
          "default": 0,
          "options": [
            { "label": "Board 1", "value": 0 }, { "label": "Board 2", "value": 1 }, { "label": "Board 3", "value": 2 },
            { "label": "Board 4", "value": 3 }, { "label": "Board 5", "value": 4 }, { "label": "Board 6", "value": 5 }
          ],
          "command": { "app_command": 12, "args": ["editing"] }
        },
        { "type": "statusPanel", "rows": [ { "label": "Editing", "value": "{editingNote}", "state": "info" } ] },
        {
          "type": "radioGroup",
          "title": "What It Shows",
          "default": 0,
          "options": [ { "label": "Departures from a stop", "value": 0 }, { "label": "A drive between two places", "value": 1 } ],
          "command": { "app_command": 13, "args": ["kind"] }
        },
        { "type": "textField", "title": "Name on Pixlpal (Optional)", "emptyHint": "To work, School run…", "command": { "app_command": 14, "args": ["label"] } },
        {
          "type": "searchPicker",
          "title": "Stop or Station",
          "search": "stop",
          "multi": false,
          "minChars": 2,
          "emptyHint": "A stop's name: stops within 10 km of your first place come first",
          "noResults": "No stop of that name nearby (set Home first, or try part of the name)",
          "searchCommand": { "app_command": 15, "args": ["query"] },
          "pickCommand": { "app_command": 16, "args": ["stop"] },
          "visibleWhen": { "arg": "kind", "equals": 0 }
        },
        {
          "type": "searchPicker",
          "title": "Only These Lines (None: All)",
          "search": "lines",
          "multi": true,
          "max": 8,
          "minChars": 0,
          "emptyHint": "Search with nothing typed to see the lines at this stop",
          "noResults": "No line of that name leaves this stop in the next hours",
          "searchCommand": { "app_command": 17, "args": ["query"] },
          "pickCommand": { "app_command": 18, "args": ["lines"] },
          "visibleWhen": { "arg": "kind", "equals": 0 }
        },
        {
          "type": "slider",
          "title": "Walk to the Stop",
          "min": 0, "max": 60, "default": 0, "step": 1, "suffix": "Mins",
          "command": { "app_command": 19, "args": ["walk"] },
          "visibleWhen": { "arg": "kind", "equals": 0 }
        },
        {
          "type": "radioGroup",
          "title": "From",
          "default": 0,
          "options": [ { "label": "Home", "value": 0 }, { "label": "Work", "value": 1 }, { "label": "School", "value": 2 }, { "label": "Other", "value": 3 } ],
          "command": { "app_command": 20, "args": ["from"] },
          "visibleWhen": { "arg": "kind", "equals": 1 }
        },
        {
          "type": "radioGroup",
          "title": "To",
          "default": 1,
          "options": [ { "label": "Home", "value": 0 }, { "label": "Work", "value": 1 }, { "label": "School", "value": 2 }, { "label": "Other", "value": 3 } ],
          "command": { "app_command": 21, "args": ["to"] },
          "visibleWhen": { "arg": "kind", "equals": 1 }
        },
        {
          "type": "timeRange",
          "title": "Arrive By (for a Leave-By Time)",
          "mode": "single",
          "defaultFrom": "08:30",
          "fromLabel": "Arrive by",
          "command": { "app_command": 23, "args": ["arriveBy"] },
          "visibleWhen": { "arg": "kind", "equals": 1 }
        },
        {
          "type": "radioGroup",
          "title": "Days",
          "default": 0,
          "options": [ { "label": "Every day", "value": 0 }, { "label": "Weekdays", "value": 1 }, { "label": "Weekends", "value": 2 } ],
          "command": { "app_command": 24, "args": ["days"] }
        },
        {
          "type": "timeRange",
          "title": "Hours It Refreshes (Same Time Twice: All Day)",
          "mode": "range",
          "defaultFrom": "06:30",
          "defaultTo": "09:30",
          "fromLabel": "From",
          "toLabel": "Until",
          "command": { "app_command": 25, "args": ["windowFrom", "windowTo"] }
        },
        { "type": "switch", "title": "Leave Alert (Banner and Chime)", "default": false, "command": { "app_command": 26, "args": ["alert"] } },
        {
          "type": "borderButton",
          "label": "REMOVE THIS BOARD",
          "confirm": { "title": "Remove this board?", "text": "Its stop, lines and hours are deleted from Pixlpal.", "ok": "Remove", "cancel": "Keep" },
          "command": { "app_command": 27 }
        }
      ]
    },
    {
      "type": "section",
      "id": "display",
      "title": "Display and Alerts",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "24-Hour Times", "default": true, "command": { "app_command": 28, "args": ["hour24"] } },
        { "type": "switch", "title": "Chime With Leave Alerts", "default": true, "command": { "app_command": 29, "args": ["chime"] } },
        {
          "type": "slider",
          "title": "Alert Before It Is Time to Leave",
          "min": 0, "max": 30, "default": 5, "step": 1, "suffix": "Mins",
          "command": { "app_command": 30, "args": ["alertMinutes"] }
        },
        { "type": "switch", "title": "Distances in Miles", "default": false, "command": { "app_command": 22, "args": ["miles"] } },
        {
          "type": "radioGroup",
          "title": "Drives Avoid",
          "default": 0,
          "options": [ { "label": "Nothing", "value": 0 }, { "label": "Tolls", "value": 1 }, { "label": "Highways", "value": 2 }, { "label": "Tolls and highways", "value": 3 } ],
          "command": { "app_command": 31, "args": ["avoid"] }
        }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click: the next board (or dismiss a leave alert). Double click: refresh now. Long press: all boards at once. Turn: brightness." },
        { "type": "bullet", "text": "Green times are live from the agency; white ones are timetabled. Each board refreshes only in its days and hours, spaced so the free month lasts; in between, Pixlpal counts the next two hours down by itself." },
        { "type": "note", "text": "Your keys stay on Pixlpal and are never sent back to the phone. Live times depend on each agency publishing them." },
        { "type": "paragraph", "spans": [
          { "text": "Transit data from " }, { "text": "Transitland", "link": "https://www.transit.land/terms" },
          { "text": " and the transit agencies who publish it." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Refreshing" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Commute is not open on Pixlpal" },
    { "when": { "app_command": 2 }, "text": "Transitland key saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "That does not look like a key" },
    { "when": { "app_command": 6 }, "text": "TomTom key saved" },
    { "when": { "app_command": 7 }, "text": "Google key saved" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Open Commute on Pixlpal and add the drive key first" },
    { "when": { "app_command": 11 }, "text": "Address saved" },
    { "when": { "app_command": 15, "response": 0 }, "text": "Open Commute on Pixlpal and add the Transitland key first" },
    { "when": { "app_command": 16 }, "text": "Stop saved" },
    { "when": { "app_command": 17, "response": 0 }, "text": "Pick a stop first (with Commute open on Pixlpal)" },
    { "when": { "app_command": 18 }, "text": "Lines saved" },
    { "when": { "app_command": 27 }, "text": "Board removed" }
  ]
})json";

#endif
