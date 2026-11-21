/**
 * @file prb_Mobile_Ui.h
 * @brief PR Board's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (prBoard.cpp):
 *   0 {"token": "..."}            a GitHub token (empty: forget it)
 *   1 {"scope": "org:acme"}       GitHub search words added to every list, such as org:acme or repo:acme/web (empty: all)
 *   2 {"minutes": 2-30}           3 {"sound": 0|1}   a chime for a new review request
 *   4 {}                          check now
 */
#ifndef PRB_MOBILE_UI_H
#define PRB_MOBILE_UI_H

static const char PRB_UI_MANIFEST[] = R"json({
  "name": "PR Board",
  "about": "Your GitHub pull requests and issues at a glance: reviews waiting on you, your own pull requests with their reviews and checks, and the issues assigned to you. A new review request gets a chime.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char PRB_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.pr-board.png", "height": 64, "alt": "PR Board" },
    { "type": "statusPanel", "title": "Now", "source": "now", "emptyText": "Add a GitHub token below." },
    { "type": "borderButton", "label": "CHECK NOW", "command": { "app_command": 4 } },
    {
      "type": "section",
      "id": "access",
      "title": "GitHub",
      "widgets": [
        { "type": "textField", "title": "GitHub Token", "emptyHint": "github_pat_... or ghp_...", "filledHint": "A token is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 0, "args": ["token"] } },
        { "type": "textField", "title": "Only From", "emptyHint": "org:acme, or repo:acme/web (empty: everywhere)", "filledHint": "Change it (empty: everywhere)", "command": { "app_command": 1, "args": ["scope"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "A classic token (Settings, Developer settings, Personal access tokens, Tokens (classic)) with the repo scope sees every repository you can. A fine-grained token sees only the repositories you give it: give it Pull requests and Issues, Read-only." },
          { "type": "bullet", "text": "Only From takes GitHub search words: org:acme, repo:acme/web, or two of them, such as org:acme org:acme-labs." },
          { "type": "note", "text": "The token is kept on Pixlpal only, and used only with GitHub." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Checking",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Check Every", "min": 2, "max": 30, "default": 5, "suffix": " min", "command": { "app_command": 2, "args": ["minutes"] } },
        { "type": "switch", "title": "Chime for a New Review Request", "default": true, "command": { "app_command": 3, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "The three counts: pull requests waiting for your review, your own open pull requests, and open issues assigned to you." },
        { "type": "bullet", "text": "Turn the knob to list each of them. Click to check now." },
        { "type": "bullet", "text": "Your pull requests show APPROVED, CHANGES or REVIEW, and a red CHECKS when their checks failed." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Token saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That does not look like a GitHub token" },
    { "when": { "app_command": 1 }, "text": "Saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write search words such as org:acme or repo:acme/web" },
    { "when": { "app_command": 4 }, "text": "Checking now" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Open PR Board on Pixlpal first" }
  ]
})json";

#endif
