/**
 * @file exm_Mobile_Ui.h
 * @brief Exam Clock's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (examClock.cpp):
 *   0 {"name": "Year 9 Maths"}      1 {"minutes": 5-300}      2 {"reading": 0-30}      3 {"extra": 0-100}  extra time, %
 *   4 {"rule0" ... "rule3": "Phones off"}   the room's rules, shown in turn (empty: none)
 *   5 {"warn": 0|1}  warnings at 30, 15 and 5 minutes       6 {"chime": 0|1}
 *   7 {} start      8 {} pause or go on      9 {} end the exam (back to the start)      10 {} five minutes more
 */
#ifndef EXM_MOBILE_UI_H
#define EXM_MOBILE_UI_H

static const char EXM_UI_MANIFEST[] = R"json({
  "name": "Exam Clock",
  "about": "For tests and exams: the time left in large figures, the start and finish times, reading time, warnings at 30, 15 and 5 minutes, extra time for those who have it, and the room's rules in turn. Pause it if the exam is interrupted.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char EXM_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.exam-clock.png", "height": 64, "alt": "Exam Clock" },
    { "type": "statusPanel", "title": "The Exam", "source": "exam" },
    { "type": "borderButton", "label": "START THE EXAM", "command": { "app_command": 7 } },
    { "type": "borderButton", "label": "PAUSE / GO ON", "command": { "app_command": 8 } },
    { "type": "borderButton", "label": "FIVE MINUTES MORE", "command": { "app_command": 10 } },
    { "type": "borderButton", "label": "END THE EXAM", "confirm": { "title": "End the exam?", "text": "The clock stops and goes back to the start.", "ok": "End it", "cancel": "Go on" }, "command": { "app_command": 9 } },
    {
      "type": "section",
      "id": "setup",
      "title": "The Exam",
      "widgets": [
        { "type": "textField", "title": "Exam Name", "emptyHint": "e.g. Year 9 Maths, Paper 1", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["name"] } },
        { "type": "slider", "title": "Writing Time", "min": 5, "max": 300, "step": 5, "default": 60, "suffix": " min", "command": { "app_command": 1, "args": ["minutes"] } },
        { "type": "slider", "title": "Reading Time", "min": 0, "max": 30, "default": 0, "suffix": " min", "command": { "app_command": 2, "args": ["reading"] } },
        { "type": "slider", "title": "Extra Time", "min": 0, "max": 100, "step": 5, "default": 25, "suffix": " %", "command": { "app_command": 3, "args": ["extra"] } }
      ]
    },
    {
      "type": "section",
      "id": "rules",
      "title": "The Room's Rules",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Rule 1", "emptyHint": "Phones off and away", "filledHint": "Change rule 1 (empty removes it)", "command": { "app_command": 4, "args": ["rule0"] } },
        { "type": "textField", "title": "Rule 2", "emptyHint": "No talking", "filledHint": "Change rule 2", "command": { "app_command": 4, "args": ["rule1"] } },
        { "type": "textField", "title": "Rule 3", "emptyHint": "Write your name on every page", "filledHint": "Change rule 3", "command": { "app_command": 4, "args": ["rule2"] } },
        { "type": "textField", "title": "Rule 4", "emptyHint": "Raise your hand for help", "filledHint": "Change rule 4", "command": { "app_command": 4, "args": ["rule3"] } },
        { "type": "switch", "title": "Warnings at 30, 15 and 5 Minutes", "default": true, "command": { "app_command": 5, "args": ["warn"] } },
        { "type": "switch", "title": "Chime with the Warnings", "default": true, "command": { "app_command": 6, "args": ["chime"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click to start. Hold the knob to pause (a fire drill) and again to go on. Before the start, turn the knob to change the writing time." },
        { "type": "bullet", "text": "Extra time shows its own finish time; after the main time ends, the clock goes on for those who have it." },
        { "type": "note", "text": "The exam keeps its time if Exam Clock is closed and opened again." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 7 }, "text": "The exam has started" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Open Exam Clock on Pixlpal first (or the exam is already running)" },
    { "when": { "app_command": 8, "response": 0 }, "text": "No exam is running" },
    { "when": { "app_command": 10 }, "text": "Five minutes added" },
    { "when": { "app_command": 10, "response": 0 }, "text": "No exam is running" }
  ]
})json";

#endif
