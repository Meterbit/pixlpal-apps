/**
 * @file hse_Mobile_Ui.h
 * @brief HSE Board's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (hseBoard.cpp):
 *   0 {"event": 0-4}                       record an event today: 0 a lost-time injury (the days start again from 0),
 *                                          1 a medical treatment case, 2 a first aid case, 3 a near miss,
 *                                          4 a safety observation
 *   1 {}                                   undo the last event recorded here
 *   2 {"site"} | {"message"} | {"url"}     the site's name; the safety message of the week; the sheet (empty: none)
 *   3 {"sectors": [names]}                 one or more of the "sectors" dataset's names (general, manufacturing, oil
 *                                          and gas, mining, construction, facilities, warehouse): their reminders in
 *                                          turn, and one sector's slogan (several: the General one). 1.0.0's
 *                                          {"sector": 0-6} still works.
 *   4 {"hours": 0-10000000}                man-hours worked a day (0: no hours and rates shown)
 *   5 {"rateBase": 0|1}                    rates per 1,000,000 hours, or per 200,000 (OSHA)
 *   6 {"seconds": 5-60}                    seconds per page
 *   7 {"reminders": 0|1} | {"sound": 0|1}  the sector's safety reminders; a chime at milestones
 *   8 {"lastLti": "YYYY-MM-DD"}            the date of the last lost-time injury ("": none recorded)
 *   9 {"record"} | {"lti_m"} | {"lti_y"} | {"mtc_m"} | {"mtc_y"} | {"fac_m"} | {"fac_y"} | {"nm_m"} | {"nm_y"} |
 *     {"obs_m"} | {"obs_y"}                the best record (days) and this month's and year's counts, corrected
 *  10 {}                                   read the sheet now        11 {"minutes": 1-60}  read it every
 */
#ifndef HSE_MOBILE_UI_H
#define HSE_MOBILE_UI_H

static const char HSE_UI_MANIFEST[] = R"json({
  "name": "HSE Board",
  "about": "A safety board for the site: days without a lost-time injury in large figures with the best record, this month's and year's injuries, medical treatment and first aid cases, near misses and safety observations, man-hours and injury rates, the safety message of the week, and safety reminders for the sector. For manufacturing, oil and gas, mining, construction, facility management and warehouses. Record events from the phone, or keep every board on site in step from one sheet.",
  "author": "Meterbit Cyb",
  "version": "v1.0.1"
})json";

static const char HSE_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.hse-board.png", "height": 64, "alt": "HSE Board" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    { "type": "statusPanel", "title": "This Month and This Year", "source": "counts" },
    { "type": "statusPanel", "title": "Man-Hours and Rates", "source": "rates", "emptyText": "Set the man-hours worked a day (Site) to see them." },
    {
      "type": "section",
      "id": "record",
      "title": "Record an Event Today",
      "widgets": [
        { "type": "borderButton", "label": "LOST-TIME INJURY", "confirm": { "title": "Record a lost-time injury?", "text": "The days without a lost-time injury start again from 0 today, and it is added to this month and this year.", "ok": "Record", "cancel": "Cancel" }, "command": { "app_command": 0, "event": 0 } },
        { "type": "borderButton", "label": "MEDICAL TREATMENT CASE", "command": { "app_command": 0, "event": 1 } },
        { "type": "borderButton", "label": "FIRST AID CASE", "command": { "app_command": 0, "event": 2 } },
        { "type": "borderButton", "label": "NEAR MISS", "command": { "app_command": 0, "event": 3 } },
        { "type": "borderButton", "label": "SAFETY OBSERVATION", "command": { "app_command": 0, "event": 4 } },
        { "type": "borderButton", "label": "UNDO THE LAST EVENT", "confirm": { "title": "Undo the last event?", "text": "The last event recorded from a phone is taken back.", "ok": "Undo", "cancel": "Keep" }, "command": { "app_command": 1 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "With a sheet (below), the sheet decides the figures: record events there, and every board reading it shows them." } ], "visibleWhen": { "arg": "url", "notEquals": "" } }
      ]
    },
    { "type": "textField", "title": "Safety Message of the Week", "emptyHint": "Hands off moving parts: isolate first", "filledHint": "Change the message (empty: none)", "command": { "app_command": 2, "args": ["message"] } },
    {
      "type": "section",
      "id": "site",
      "title": "Site",
      "widgets": [
        { "type": "textField", "title": "Site Name", "emptyHint": "Ikeja Plant", "filledHint": "Change the name (empty: the sector's slogan)", "command": { "app_command": 2, "args": ["site"] } },
        { "type": "dropDownList", "title": "Sectors", "dataset": "sectors", "buttonLabel": "CHOOSE ONE OR MORE", "sendAs": "array", "command": { "app_command": 3, "args": ["sectors"] } },
        { "type": "textField", "title": "Man-Hours Worked a Day", "emptyHint": "1200 (an average for every day of the week)", "filledHint": "Change it (0: no hours and rates)", "command": { "app_command": 4, "args": ["hours"] } },
        {
          "type": "radioGroup",
          "title": "Injury Rates Per",
          "default": 0,
          "options": [ { "label": "1,000,000 hours (LTIFR, TRIFR)", "value": 0 }, { "label": "200,000 hours (OSHA)", "value": 1 } ],
          "command": { "app_command": 5, "args": ["rateBase"] }
        },
        { "type": "slider", "title": "Each Page For", "min": 5, "max": 60, "default": 10, "step": 5, "suffix": " s", "command": { "app_command": 6, "args": ["seconds"] } },
        { "type": "switch", "title": "Safety Reminders for the Sectors", "default": true, "command": { "app_command": 7, "args": ["reminders"] } },
        { "type": "switch", "title": "Chime at Milestones", "default": true, "command": { "app_command": 7, "args": ["sound"] } }
      ]
    },
    {
      "type": "section",
      "id": "figures",
      "title": "Correct the Figures",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Date of the Last Lost-Time Injury", "emptyHint": "2025-03-14", "filledHint": "YYYY-MM-DD (empty: none recorded)", "command": { "app_command": 8, "args": ["lastLti"] } },
        { "type": "textField", "title": "Best Record (Days)", "emptyHint": "512", "filledHint": "Change the record", "command": { "app_command": 9, "args": ["record"] } },
        { "type": "textField", "title": "Lost-Time Injuries This Month", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["lti_m"] } },
        { "type": "textField", "title": "Lost-Time Injuries This Year", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["lti_y"] } },
        { "type": "textField", "title": "Medical Treatment Cases This Month", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["mtc_m"] } },
        { "type": "textField", "title": "Medical Treatment Cases This Year", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["mtc_y"] } },
        { "type": "textField", "title": "First Aid Cases This Month", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["fac_m"] } },
        { "type": "textField", "title": "First Aid Cases This Year", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["fac_y"] } },
        { "type": "textField", "title": "Near Misses This Month", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["nm_m"] } },
        { "type": "textField", "title": "Near Misses This Year", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["nm_y"] } },
        { "type": "textField", "title": "Safety Observations This Month", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["obs_m"] } },
        { "type": "textField", "title": "Safety Observations This Year", "emptyHint": "0", "filledHint": "Change", "command": { "app_command": 9, "args": ["obs_y"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "This month's counts start again on the 1st, this year's on 1 January. The best record goes up by itself when the days pass it." } ] }
      ]
    },
    {
      "type": "section",
      "id": "sheet",
      "title": "One Sheet for Every Board on Site",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Sheet Link (CSV or JSON)", "emptyHint": "https://docs.google.com/spreadsheets/d/e/.../pub?output=csv", "filledHint": "Change the link (empty: figures recorded here)", "command": { "app_command": 2, "args": ["url"] } },
        { "type": "borderButton", "label": "READ THE SHEET NOW", "command": { "app_command": 10 } },
        { "type": "slider", "title": "Read It Every", "min": 1, "max": 60, "default": 10, "suffix": " min", "command": { "app_command": 11, "args": ["minutes"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Two columns: a name and its value, one a row. Names: last_lti (a date, YYYY-MM-DD), record, lti_month, lti_year, mtc_month, mtc_year, fac_month, fac_year, nm_month, nm_year, obs_month, obs_year, hours_per_day, message, site. Leave out any you don't keep." },
          { "type": "bullet", "text": "Google Sheets: File, Share, Publish to web, choose the sheet and Comma-separated values (.csv), then copy the link." },
          { "type": "bullet", "text": "JSON: an object with the same names, such as {\"last_lti\": \"2025-03-14\", \"nm_month\": 4}." },
          { "type": "note", "text": "Every Pixlpal reading the sheet shows the same figures: update the sheet once and the boards in the canteen, the gate and the control room follow." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Recorded" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Pixlpal needs the date first: connect it to Wi-Fi" },
    { "when": { "app_command": 1 }, "text": "Undone" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Nothing to undo" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 2, "response": 0 }, "text": "That doesn't fit: check the text or the web address" },
    { "when": { "app_command": 3 }, "text": "Sectors saved" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Choose at least one sector" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Write a whole number" },
    { "when": { "app_command": 8 }, "text": "Saved" },
    { "when": { "app_command": 8, "response": 0 }, "text": "Write the date as YYYY-MM-DD, not in the future" },
    { "when": { "app_command": 9 }, "text": "Saved" },
    { "when": { "app_command": 9, "response": 0 }, "text": "Write a whole number" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Open HSE Board on Pixlpal first, and give it a sheet" }
  ]
})json";

// The "sectors" list (hse_Sectors.h's names, in its order).
static const char HSE_UI_DATA[] = R"json({
  "datasets": {
    "sectors": [
      { "name": "General", "value": "General" },
      { "name": "Manufacturing", "value": "Manufacturing" },
      { "name": "Oil and gas", "value": "Oil and gas" },
      { "name": "Mining and minerals", "value": "Mining and minerals" },
      { "name": "Construction", "value": "Construction" },
      { "name": "Building and facility management", "value": "Building and facility management" },
      { "name": "Warehouse and logistics", "value": "Warehouse and logistics" }
    ]
  }
})json";

#endif
