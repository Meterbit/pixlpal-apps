/**
 * @file mfc_Mobile_Ui.h
 * @brief My First Computer's phone page {19,8}, for parents (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (myFirstComputer.cpp):
 *   0 {"childSlot": 0-2}   the child the page shows (Kids Learning's place)
 *   1 {"level": 0-3} | {"oneClick": 0-2}   that child's level (0: by age, moving up with badges) and how icons open
 *   2 {"progMouse" | "progKeys" | "progPaint" | "progWrite" | "progFiles" | "progCalc" | "progPiano" | "progWeb": 0|1}
 *   3 {"code": "4 digits"}   the parent code (holding the knob on Pixlpal asks for it)
 *   4 {"locked": 0|1}        lock the computer, or unlock it
 *   5 {}                     leave My First Computer (Pixlpal goes back to the app before it)
 *   6 {"file": "NAME.png"}   that child's picture to the phone (answered with 247 pieces)
 *   7 {"file": "NAME.png|NAME.txt"}   delete that child's file for good
 *   8 {}   15 more minutes today for that child      9 {}   sign that child in now
 *   10 {}  forget their secret pictures               11 {}  their licence starts again    12 {} delete all their files
 */
#ifndef MFC_MOBILE_UI_H
#define MFC_MOBILE_UI_H

static const char MFC_UI_MANIFEST[] = R"json({
  "appID": "19/8",
  "name": "My First Computer",
  "about": "A pretend computer for children of 4 to 12, to learn the mouse, the keyboard, windows, files and staying safe online, with nothing that can go wrong. Plug a USB mouse and keyboard into Pixlpal. It never goes online, it only keeps the child's own pictures and writing, and only a grown-up with the parent code can leave it.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char MFC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:myFirstComputer.png", "height": 64, "alt": "My First Computer" },
    { "type": "statusPanel", "title": "On Pixlpal Now", "source": "now" },
    { "type": "statusPanel", "title": "Your Children", "source": "children", "emptyText": "Set up your children in Kids Learning." },
    {
      "type": "section",
      "id": "child",
      "title": "A Child",
      "widgets": [
        {
          "type": "radioGroup",
          "title": "Child",
          "default": 0,
          "options": [ { "label": "1", "value": 0 }, { "label": "2", "value": 1 }, { "label": "3", "value": 2 } ],
          "command": { "app_command": 0, "args": ["childSlot"] }
        },
        { "type": "statusPanel", "title": "Their Computer Licence", "source": "licence" },
        {
          "type": "radioGroup",
          "title": "Level",
          "default": 0,
          "options": [
            { "label": "By Age (Moves Up as They Earn Badges)", "value": 0 },
            { "label": "1: First Steps (Mouse, Paint, Piano)", "value": 1 },
            { "label": "2: Explorer (Keyboard, Writing, Files)", "value": 2 },
            { "label": "3: Online Safety (Pretend Internet)", "value": 3 }
          ],
          "command": { "app_command": 1, "args": ["level"] }
        },
        {
          "type": "radioGroup",
          "title": "Opening Icons",
          "default": 0,
          "options": [
            { "label": "By Level (One Click at Level 1)", "value": 0 },
            { "label": "One Click", "value": 1 },
            { "label": "Two Clicks, as on a Real Computer", "value": 2 }
          ],
          "command": { "app_command": 1, "args": ["oneClick"] }
        },
        {
          "type": "fileList",
          "title": "Their Pictures and Writing",
          "source": "files",
          "emptyText": "Nothing saved yet",
          "saveKinds": ["png"],
          "saveCommand": { "app_command": 6, "args": ["file"] },
          "deleteCommand": { "app_command": 7, "args": ["file"] },
          "deleteConfirm": { "title": "Delete it?", "text": "It will be gone for good, not in the Recycle Bin.", "ok": "Delete", "cancel": "Keep" }
        },
        { "type": "borderButton", "label": "15 MORE MINUTES TODAY", "command": { "app_command": 8 } },
        { "type": "borderButton", "label": "SIGN THIS CHILD IN NOW", "command": { "app_command": 9 } },
        { "type": "borderButton", "label": "FORGET THEIR SECRET PICTURES", "confirm": { "title": "Forget them?", "text": "At level 3 they sign in with two secret pictures. They will choose new ones next time.", "ok": "Forget", "cancel": "Keep" }, "command": { "app_command": 10 } },
        { "type": "borderButton", "label": "START THEIR LICENCE AGAIN", "confirm": { "title": "Start again?", "text": "Their badges and level go back to the start. Their files stay.", "ok": "Start again", "cancel": "Keep" }, "command": { "app_command": 11 } },
        { "type": "borderButton", "label": "DELETE ALL THEIR FILES", "confirm": { "title": "Delete everything?", "text": "All their pictures and writing, and their Recycle Bin, will be gone for good.", "ok": "Delete all", "cancel": "Keep" }, "command": { "app_command": 12 } }
      ]
    },
    {
      "type": "section",
      "id": "programs",
      "title": "Programs on the Desktop",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Mouse School", "default": true, "command": { "app_command": 2, "args": ["progMouse"] } },
        { "type": "switch", "title": "Keyboard", "default": true, "command": { "app_command": 2, "args": ["progKeys"] } },
        { "type": "switch", "title": "Paint", "default": true, "command": { "app_command": 2, "args": ["progPaint"] } },
        { "type": "switch", "title": "My Writing", "default": true, "command": { "app_command": 2, "args": ["progWrite"] } },
        { "type": "switch", "title": "My Files and the Recycle Bin", "default": true, "command": { "app_command": 2, "args": ["progFiles"] } },
        { "type": "switch", "title": "Calculator", "default": true, "command": { "app_command": 2, "args": ["progCalc"] } },
        { "type": "switch", "title": "Piano", "default": true, "command": { "app_command": 2, "args": ["progPiano"] } },
        { "type": "switch", "title": "Pretend Internet", "default": true, "command": { "app_command": 2, "args": ["progWeb"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "A program also needs the child's level. Settings and the Computer Licence are always there." } ] }
      ]
    },
    {
      "type": "section",
      "id": "safety",
      "title": "Grown-Ups",
      "widgets": [
        { "type": "textField", "title": "Parent Code (4 Numbers)", "secret": true, "emptyHint": "Choose 4 numbers", "filledHint": "Change the parent code", "command": { "app_command": 3, "args": ["code"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Until you set your own, the code is 1234. Choose one your children don't know." } ], "visibleWhen": { "arg": "codeSet", "equals": 0 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "On Pixlpal, hold the knob and type the code to leave My First Computer, lock it or sign a child out." } ] },
        { "type": "switch", "title": "Lock the Computer", "default": false, "command": { "app_command": 4, "args": ["locked"] } },
        { "type": "borderButton", "label": "LEAVE MY FIRST COMPUTER", "confirm": { "title": "Leave it?", "text": "Pixlpal goes back to the app it showed before. Work that isn't saved is saved first.", "ok": "Leave", "cancel": "Stay" }, "command": { "app_command": 5 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The daily time limit and bedtime are set in Kids Learning, and count for all the kids apps." } ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "What They Learn",
      "blocks": [
        { "type": "bullet", "text": "Mouse School: pointing, clicking, double clicking, dragging, scrolling and right clicking, as games." },
        { "type": "bullet", "text": "Keyboard: finding the keys, typing their name with a capital letter, and the special keys." },
        { "type": "bullet", "text": "Paint and My Writing: menus, Save and Save As, naming a file, opening it again." },
        { "type": "bullet", "text": "My Files: folders, renaming, deleting to the Recycle Bin and bringing things back." },
        { "type": "bullet", "text": "The Pretend Internet: links, Back and Home, searching, and what to do about prize pop-ups and forms asking for an address or a password. It never goes online." },
        { "type": "bullet", "text": "Signing in, the Start menu, windows (minimise and close) and shutting down properly. Each skill is a badge on their Computer Licence." },
        { "type": "note", "text": "No mouse? The knob works too: turn to move from thing to thing, click to open, double click to go back." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3 }, "text": "Parent code saved" },
    { "when": { "app_command": 3, "response": 0 }, "text": "The code must be 4 numbers" },
    { "when": { "app_command": 5 }, "text": "Leaving My First Computer" },
    { "when": { "app_command": 7 }, "text": "Deleted" },
    { "when": { "app_command": 8 }, "text": "15 more minutes today" },
    { "when": { "app_command": 9 }, "text": "Signed in on Pixlpal" },
    { "when": { "app_command": 9, "response": 0 }, "text": "Set this child up in Kids Learning first" },
    { "when": { "app_command": 10 }, "text": "Their secret pictures are forgotten" },
    { "when": { "app_command": 11 }, "text": "Their licence starts again" },
    { "when": { "app_command": 12 }, "text": "Their files are deleted" }
  ]
})json";

#endif
