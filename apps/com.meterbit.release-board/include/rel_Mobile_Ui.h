/**
 * @file rel_Mobile_Ui.h
 * @brief Release Countdown's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (releaseBoard.cpp):
 *   0 {"m0" ... "m4": "YYYY-MM-DD [HH:MM] name"}   one per call, local time; empty removes it
 *   1 {"repo": "owner/repo"}   2 {"env": "production"}   3 {"token": "..."}   (empty: none)
 *   4 {"freeze": 0|1}          5 {"sound": 0|1}           6 {}  check GitHub now
 */
#ifndef REL_MOBILE_UI_H
#define REL_MOBILE_UI_H

static const char REL_UI_MANIFEST[] = R"json({
  "name": "Release Countdown",
  "about": "A countdown to your next release, code freeze or launch, with the milestones after it, a code freeze sign, and your repository's latest GitHub release and production deploy. Ship day gets a fanfare.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char REL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.release-board.png", "height": 64, "alt": "Release Countdown" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Add a milestone below." },
    { "type": "switch", "title": "Code Freeze", "default": false, "command": { "app_command": 4, "args": ["freeze"] } },
    {
      "type": "section",
      "id": "milestones",
      "title": "Milestones",
      "widgets": [
        { "type": "textField", "title": "Milestone 1", "emptyHint": "2026-12-01 14:00 v2.4 release", "filledHint": "Change it (empty removes it)", "command": { "app_command": 0, "args": ["m0"] } },
        { "type": "textField", "title": "Milestone 2", "emptyHint": "2026-11-27 Code freeze", "filledHint": "Change it (empty removes it)", "command": { "app_command": 0, "args": ["m1"] } },
        { "type": "textField", "title": "Milestone 3", "emptyHint": "2027-01-15 Beta", "filledHint": "Change it (empty removes it)", "command": { "app_command": 0, "args": ["m2"] } },
        { "type": "textField", "title": "Milestone 4", "emptyHint": "2027-02-01 Launch", "filledHint": "Change it (empty removes it)", "command": { "app_command": 0, "args": ["m3"] } },
        { "type": "textField", "title": "Milestone 5", "emptyHint": "2027-03-01 v3.0", "filledHint": "Change it (empty removes it)", "command": { "app_command": 0, "args": ["m4"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Write the date, the time if it has one (24-hour, your Pixlpal's time zone), then the name: 2026-12-01 14:00 v2.4 release. Without a time, it is midnight at the start of that day." },
          { "type": "bullet", "text": "Pixlpal counts down to the soonest one. A milestone stays shown as done for a day, then goes." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "github",
      "title": "GitHub (Optional)",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Repository", "emptyHint": "owner/repo", "filledHint": "Change it (empty: none)", "command": { "app_command": 1, "args": ["repo"] } },
        { "type": "textField", "title": "Deploy Environment", "emptyHint": "production", "filledHint": "Change it", "command": { "app_command": 2, "args": ["env"] } },
        { "type": "textField", "title": "GitHub Token (Private Repositories)", "emptyHint": "github_pat_...", "filledHint": "A token is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 3, "args": ["token"] } },
        { "type": "borderButton", "label": "CHECK GITHUB NOW", "command": { "app_command": 6 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Pixlpal shows the repository's latest release, and its latest deployment to the environment (from GitHub Actions or any tool that reports deployments to GitHub) with how it went." },
          { "type": "bullet", "text": "A public repository needs no token. For a private one: a fine-grained token for it with Contents and Deployments, Read-only." },
          { "type": "note", "text": "The token is kept on Pixlpal only, and used only with GitHub." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Sound",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Fanfare at Zero, and a Sound for Each Deploy", "default": true, "command": { "app_command": 5, "args": ["sound"] } }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Milestone saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write it as 2026-12-01 14:00 v2.4 release" },
    { "when": { "app_command": 1 }, "text": "Saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write owner/repo" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Letters, digits, - _ . only" },
    { "when": { "app_command": 3 }, "text": "Token saved" },
    { "when": { "app_command": 6 }, "text": "Checking now" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Open Release Countdown on Pixlpal, and add a repository" }
  ]
})json";

#endif
