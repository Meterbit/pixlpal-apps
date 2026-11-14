/**
 * @file fcl_Mobile_Ui.h
 * @brief Focus List's phone page {21,0} (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (focusList.cpp):
 *   0 {"task0" ... "task7": "text"}   a task (empty: removed); one field per call
 *   1 {"focusMin": 10-90} | {"breakMin": 1-30} | {"longMin": 5-45}
 *   2 {"sound": 0|1}
 *   3 {}  start or pause the timer      4 {}  skip to the next part (focus or break)      5 {}  stop the timer
 *   6 {}  remove the tasks that are done
 *   7 {"pick": 0-7}  the task the timer counts for
 */
#ifndef FCL_MOBILE_UI_H
#define FCL_MOBILE_UI_H

static const char FCL_UI_MANIFEST[] = R"json({
  "appID": "21/0",
  "name": "Focus List",
  "about": "Today's to-do list on Pixlpal, with a focus timer: work on one task for a set time, then take a short break, with a longer break after every four. Pixlpal counts the minutes spent on each task. Write the tasks here; tick them off with the knob.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char FCL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:focusList.png", "height": 64, "alt": "Focus List" },
    { "type": "statusPanel", "title": "Now", "source": "now" },
    { "type": "statusPanel", "title": "Today's Tasks", "source": "tasks", "emptyText": "No tasks yet. Write some below." },
    { "type": "borderButton", "label": "START / PAUSE THE TIMER", "command": { "app_command": 3 } },
    { "type": "borderButton", "label": "SKIP TO THE NEXT PART", "command": { "app_command": 4 } },
    { "type": "borderButton", "label": "STOP THE TIMER", "command": { "app_command": 5 } },
    {
      "type": "radioGroup",
      "title": "The Timer Counts For",
      "default": 0,
      "options": [
        { "label": "Task 1", "value": 0 }, { "label": "Task 2", "value": 1 }, { "label": "Task 3", "value": 2 }, { "label": "Task 4", "value": 3 },
        { "label": "Task 5", "value": 4 }, { "label": "Task 6", "value": 5 }, { "label": "Task 7", "value": 6 }, { "label": "Task 8", "value": 7 }
      ],
      "command": { "app_command": 7, "args": ["pick"] }
    },
    {
      "type": "section",
      "id": "tasks",
      "title": "Write the Tasks",
      "widgets": [
        { "type": "textField", "title": "Task 1", "emptyHint": "e.g. Reply to the invoices", "filledHint": "Change task 1 (empty removes it)", "command": { "app_command": 0, "args": ["task0"] } },
        { "type": "textField", "title": "Task 2", "emptyHint": "A task", "filledHint": "Change task 2", "command": { "app_command": 0, "args": ["task1"] } },
        { "type": "textField", "title": "Task 3", "emptyHint": "A task", "filledHint": "Change task 3", "command": { "app_command": 0, "args": ["task2"] } },
        { "type": "textField", "title": "Task 4", "emptyHint": "A task", "filledHint": "Change task 4", "command": { "app_command": 0, "args": ["task3"] } },
        { "type": "textField", "title": "Task 5", "emptyHint": "A task", "filledHint": "Change task 5", "command": { "app_command": 0, "args": ["task4"] } },
        { "type": "textField", "title": "Task 6", "emptyHint": "A task", "filledHint": "Change task 6", "command": { "app_command": 0, "args": ["task5"] } },
        { "type": "textField", "title": "Task 7", "emptyHint": "A task", "filledHint": "Change task 7", "command": { "app_command": 0, "args": ["task6"] } },
        { "type": "textField", "title": "Task 8", "emptyHint": "A task", "filledHint": "Change task 8", "command": { "app_command": 0, "args": ["task7"] } },
        { "type": "borderButton", "label": "REMOVE THE TASKS THAT ARE DONE", "confirm": { "title": "Remove them?", "text": "The ticked tasks are taken off the list.", "ok": "Remove", "cancel": "Keep" }, "command": { "app_command": 6 } }
      ]
    },
    {
      "type": "section",
      "id": "timer",
      "title": "The Timer",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Focus", "min": 10, "max": 90, "step": 5, "default": 25, "suffix": " min", "command": { "app_command": 1, "args": ["focusMin"] } },
        { "type": "slider", "title": "Short Break", "min": 1, "max": 30, "default": 5, "suffix": " min", "command": { "app_command": 1, "args": ["breakMin"] } },
        { "type": "slider", "title": "Long Break (After 4 Focuses)", "min": 5, "max": 45, "step": 5, "default": 15, "suffix": " min", "command": { "app_command": 1, "args": ["longMin"] } },
        { "type": "switch", "title": "Chime at the End of Each Part", "default": true, "command": { "app_command": 2, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose a task. Click to start or pause the timer on it. Double click to tick it off (or back on)." },
        { "type": "bullet", "text": "Hold the knob to skip to the next part: from focus to a break, or from a break back to focus." },
        { "type": "note", "text": "The minutes on each task and the focuses done start again each day. The tasks stay until you remove them." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Task saved" },
    { "when": { "app_command": 6 }, "text": "Done tasks removed" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Focus List on Pixlpal first" },
    { "when": { "app_command": 7, "response": 0 }, "text": "That task is empty" }
  ]
})json";

#endif
