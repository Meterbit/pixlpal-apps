/**
 * @file bc_Mobile_Ui.h
 * @brief Baby Countdown's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (babyCountdown.cpp):
 *   0 {"due": "YYYY-MM-DD"} | {"lmp": "YYYY-MM-DD"}   the due date, or the first day of the last period (+280 days)
 *   1 {"name": "..."}          the baby's name or nickname (empty: Baby)
 *   2 {"display": 0|1}         a plain countdown, or the full board (weeks, size, appointments)
 *   3 {"showFrom": 0-20}       nothing on the panel before this week (0: from the start)
 *   4 {"appt0" ... "appt3": "YYYY-MM-DD text"}   appointments, one per call (empty removes)
 *   5 {}                       the baby has arrived (today)
 *   6 {}                       stop, and forget everything
 */
#ifndef BC_MOBILE_UI_H
#define BC_MOBILE_UI_H

static const char BC_UI_MANIFEST[] = R"json({
  "name": "Baby Countdown",
  "about": "A countdown to your baby's due date for the whole family: the days to go, and if you like the week, the baby's size and your appointments. Shows as little as you choose, and stops with one tap.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.baby-countdown.png", "height": 64, "alt": "Baby Countdown" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Add the due date below." },
    {
      "type": "section",
      "id": "date",
      "title": "The Due Date",
      "widgets": [
        { "type": "textField", "title": "Due Date", "emptyHint": "YYYY-MM-DD, as your midwife or doctor gave it", "filledHint": "Change it (YYYY-MM-DD)", "command": { "app_command": 0, "args": ["due"] } },
        { "type": "textField", "title": "Or: First Day of the Last Period", "emptyHint": "YYYY-MM-DD (Pixlpal adds 280 days)", "filledHint": "YYYY-MM-DD", "command": { "app_command": 0, "args": ["lmp"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "A date worked out from the last period is an estimate: use the due date your midwife or doctor gives you once you have it." } ] }
      ]
    },
    {
      "type": "section",
      "id": "panel",
      "title": "On the Panel",
      "widgets": [
        { "type": "radioGroup", "title": "Show", "default": 0, "options": [ { "label": "A plain countdown (days to go only)", "value": 0 }, { "label": "Everything: the name, the week, the size and appointments", "value": 1 } ], "command": { "app_command": 2, "args": ["display"] } },
        { "type": "slider", "title": "Show Nothing Before Week (0: From Now)", "min": 0, "max": 20, "default": 0, "command": { "app_command": 3, "args": ["showFrom"] } },
        { "type": "textField", "title": "Baby's Name or Nickname", "emptyHint": "Baby", "filledHint": "Change it (empty: Baby)", "command": { "app_command": 1, "args": ["name"] }, "visibleWhen": { "arg": "display", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Before the week you choose, the panel shows only the time and date, so visitors see nothing." } ] }
      ]
    },
    {
      "type": "section",
      "id": "appointments",
      "title": "Appointments",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Appointment 1", "emptyHint": "2027-01-12 Scan, 10:30", "filledHint": "Change it (empty removes it)", "command": { "app_command": 4, "args": ["appt0"] } },
        { "type": "textField", "title": "Appointment 2", "emptyHint": "YYYY-MM-DD what", "filledHint": "Change it (empty removes it)", "command": { "app_command": 4, "args": ["appt1"] } },
        { "type": "textField", "title": "Appointment 3", "emptyHint": "YYYY-MM-DD what", "filledHint": "Change it (empty removes it)", "command": { "app_command": 4, "args": ["appt2"] } },
        { "type": "textField", "title": "Appointment 4", "emptyHint": "YYYY-MM-DD what", "filledHint": "Change it (empty removes it)", "command": { "app_command": 4, "args": ["appt3"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "bullet", "text": "The next one shows on the full board. Past ones go by themselves." } ] }
      ]
    },
    { "type": "borderButton", "label": "OUR BABY HAS ARRIVED", "confirm": { "title": "Congratulations!", "text": "The panel welcomes your baby from today.", "ok": "Yes", "cancel": "Not yet" }, "command": { "app_command": 5 } },
    { "type": "borderButton", "label": "STOP THE COUNTDOWN", "confirm": { "title": "Stop the countdown?", "text": "Baby Countdown clears the panel and forgets the dates, the name and the appointments.", "ok": "Stop", "cancel": "Keep it" }, "command": { "app_command": 6 } },
    {
      "type": "richTextBlock",
      "title": "About",
      "blocks": [
        { "type": "bullet", "text": "The weeks count from the first day of the last period, as midwives and doctors count them. The sizes are rough, friendly comparisons." },
        { "type": "note", "text": "Baby Countdown gives no medical advice. Your midwife or doctor is the one to ask about anything that worries you." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write the date as YYYY-MM-DD" },
    { "when": { "app_command": 4 }, "text": "Saved" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Write it as 2027-01-12 Scan, 10:30" },
    { "when": { "app_command": 5 }, "text": "Congratulations!" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Add the due date first" },
    { "when": { "app_command": 6 }, "text": "Stopped" }
  ]
})json";

#endif
