/**
 * @file upt_Mobile_Ui.h
 * @brief Uptime Monitor's phone page {22,0} (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (uptimeMonitor.cpp):
 *   0 {"site0" ... "site7": "https://example.com"}   a website to check (empty: removed); one per call
 *   1 {"minutes": 1-60}   how often each is checked
 *   2 {"sound": 0|1}      a sound when one goes down or comes back
 *   3 {}                  check them all now
 *   4 {"slowMs": 500-10000}   slower than this is SLOW
 */
#ifndef UPT_MOBILE_UI_H
#define UPT_MOBILE_UI_H

static const char UPT_UI_MANIFEST[] = R"json({
  "appID": "22/0",
  "name": "Uptime Monitor",
  "about": "Pixlpal checks up to eight websites or web services every few minutes: up or down, how fast they answer, and how often they were up. When one goes down Pixlpal shows it in red and sounds an alarm, and again when it comes back.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char UPT_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:uptimeMonitor.png", "height": 64, "alt": "Uptime Monitor" },
    { "type": "statusPanel", "title": "Your Websites", "source": "sites", "emptyText": "No websites yet. Add them below." },
    { "type": "borderButton", "label": "CHECK THEM ALL NOW", "command": { "app_command": 3 } },
    {
      "type": "section",
      "id": "sites",
      "title": "Websites to Check",
      "widgets": [
        { "type": "textField", "title": "Website 1", "emptyHint": "https://example.com", "filledHint": "Change website 1 (empty removes it)", "command": { "app_command": 0, "args": ["site0"] } },
        { "type": "textField", "title": "Website 2", "emptyHint": "https://...", "filledHint": "Change website 2", "command": { "app_command": 0, "args": ["site1"] } },
        { "type": "textField", "title": "Website 3", "emptyHint": "https://...", "filledHint": "Change website 3", "command": { "app_command": 0, "args": ["site2"] } },
        { "type": "textField", "title": "Website 4", "emptyHint": "https://...", "filledHint": "Change website 4", "command": { "app_command": 0, "args": ["site3"] } },
        { "type": "textField", "title": "Website 5", "emptyHint": "https://...", "filledHint": "Change website 5", "command": { "app_command": 0, "args": ["site4"] } },
        { "type": "textField", "title": "Website 6", "emptyHint": "https://...", "filledHint": "Change website 6", "command": { "app_command": 0, "args": ["site5"] } },
        { "type": "textField", "title": "Website 7", "emptyHint": "https://...", "filledHint": "Change website 7", "command": { "app_command": 0, "args": ["site6"] } },
        { "type": "textField", "title": "Website 8", "emptyHint": "https://...", "filledHint": "Change website 8", "command": { "app_command": 0, "args": ["site7"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "A whole address, such as https://example.com/health. A name alone (example.com) is checked over https. http:// works for devices on your own network, such as a router or a home server." } ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Checking",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Check Every", "min": 1, "max": 60, "default": 5, "suffix": " min", "command": { "app_command": 1, "args": ["minutes"] } },
        { "type": "slider", "title": "Slow Above", "min": 500, "max": 10000, "step": 250, "default": 2000, "suffix": " ms", "command": { "app_command": 4, "args": ["slowMs"] } },
        { "type": "switch", "title": "Sound When One Goes Down or Comes Back", "default": true, "command": { "app_command": 2, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "How It Checks",
      "blocks": [
        { "type": "bullet", "text": "Up: the address answers with a page (HTTP 200 to 399). Down: an error page (400 and above), or no answer within 15 seconds." },
        { "type": "bullet", "text": "When nothing at all answers, Pixlpal's own internet is probably down: it shows OFFLINE instead of marking every website down." },
        { "type": "bullet", "text": "On Pixlpal, turn the knob to see one website at a time with its history, click to check them all now." },
        { "type": "note", "text": "The up times count since the monitor was opened." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Website saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is not a web address" },
    { "when": { "app_command": 3 }, "text": "Checking now" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Uptime Monitor on Pixlpal first" }
  ]
})json";

#endif
