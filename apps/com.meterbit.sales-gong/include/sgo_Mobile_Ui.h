/**
 * @file sgo_Mobile_Ui.h
 * @brief Sales Gong's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (salesGong.cpp):
 *   0 {"url": "https://...csv"}       the sheet (CSV or JSON); empty: deals added here instead
 *   1 {"target": "120000"}            this month's target (0: none)
 *   2 {"unit": "$"}                   written before amounts (up to 4 characters)
 *   3 {"minutes": 1-30}               4 {"sound": 0|1}
 *   5 {"deal": "Ada 4200"}            a deal: rings the gong, and adds it to the board when there is no sheet
 *   6 {}                              ring the gong (no deal)
 *   7 {}                              read the sheet now          8 {}  clear the deals added here
 */
#ifndef SGO_MOBILE_UI_H
#define SGO_MOBILE_UI_H

static const char SGO_UI_MANIFEST[] = R"json({
  "name": "Sales Gong",
  "about": "The team's goal board: this month's total against the target, whether you're ahead of the pace, and who is leading. Every new deal rings the gong. Reads a Google Sheet published as CSV, any CSV or JSON link, or deals you add from the phone.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SGO_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.sales-gong.png", "height": 64, "alt": "Sales Gong" },
    { "type": "statusPanel", "title": "This Month", "source": "month" },
    { "type": "statusPanel", "title": "Leaderboard", "source": "board", "emptyText": "No deals yet." },
    { "type": "textField", "title": "A Deal: Name and Amount", "emptyHint": "Ada 4200", "filledHint": "Another deal", "command": { "app_command": 5, "args": ["deal"] } },
    { "type": "borderButton", "label": "RING THE GONG", "command": { "app_command": 6 } },
    {
      "type": "section",
      "id": "goal",
      "title": "The Goal",
      "widgets": [
        { "type": "textField", "title": "This Month's Target", "emptyHint": "120000", "filledHint": "Change the target (0: none)", "command": { "app_command": 1, "args": ["target"] } },
        { "type": "textField", "title": "Before Amounts", "emptyHint": "$", "filledHint": "Change it (up to 4 characters: $, EUR, N)", "command": { "app_command": 2, "args": ["unit"] } }
      ]
    },
    {
      "type": "section",
      "id": "sheet",
      "title": "The Sheet",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Sheet Link (CSV or JSON)", "emptyHint": "https://docs.google.com/spreadsheets/d/e/.../pub?output=csv", "filledHint": "Change the link (empty: deals added here)", "command": { "app_command": 0, "args": ["url"] } },
        { "type": "borderButton", "label": "READ THE SHEET NOW", "command": { "app_command": 7 } },
        { "type": "slider", "title": "Read It Every", "min": 1, "max": 30, "default": 5, "suffix": " min", "command": { "app_command": 3, "args": ["minutes"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Google Sheets: File, Share, Publish to web, choose the sheet and Comma-separated values (.csv), then copy the link." },
          { "type": "bullet", "text": "One row a deal or a person: the name in the first column, the amount in the second. Rows with the same name are added up; a heading row is skipped." },
          { "type": "bullet", "text": "JSON: a list of {\"name\": ..., \"amount\": ...}." },
          { "type": "note", "text": "Without a sheet, the deals you add here make the board. They start again each month." }
        ] },
        { "type": "borderButton", "label": "CLEAR THE DEALS ADDED HERE", "confirm": { "title": "Clear the deals?", "text": "The deals added from the phone this month are removed.", "ok": "Clear", "cancel": "Keep" }, "command": { "app_command": 8 } }
      ]
    },
    { "type": "switch", "title": "Gong Sound", "default": true, "command": { "app_command": 4, "args": ["sound"] } }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Sheet saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is not a web address" },
    { "when": { "app_command": 1 }, "text": "Target saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Write a whole number" },
    { "when": { "app_command": 5 }, "text": "Gong!" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Write a name and an amount: Ada 4200" },
    { "when": { "app_command": 6 }, "text": "Gong!" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Open Sales Gong on Pixlpal first" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Open Sales Gong on Pixlpal first, and give it a sheet" }
  ]
})json";

#endif
