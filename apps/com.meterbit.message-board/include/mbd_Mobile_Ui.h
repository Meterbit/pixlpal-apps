/**
 * @file mbd_Mobile_Ui.h
 * @brief Message Board's phone page {22,1} (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (messageBoard.cpp):
 *   0 {"topic": "letters, digits, - and _"}   the ntfy topic Pixlpal follows
 *   1 {"server": "https://ntfy.sh"}           the ntfy server (a self-hosted one, too)
 *   2 {"token": "tk_..."}                     an access token for a protected topic ("" removes it); never sent back
 *                                             (the page shows only "token_set")
 *   3 {"pollSec": 10-300} | {"sound": 0|1}
 *   4 {}  a new random topic                  5 {}  clear the messages
 */
#ifndef MBD_MOBILE_UI_H
#define MBD_MOBILE_UI_H

static const char MBD_UI_MANIFEST[] = R"json({
  "appID": "22/1",
  "name": "Message Board",
  "about": "Send a message to Pixlpal from anywhere: a script, a server, a home automation, a shortcut on your phone or a friend. Pixlpal follows a topic on ntfy, a free and open source notification service, and shows each message with a chime. Urgent ones flash red.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char MBD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:messageBoard.png", "height": 64, "alt": "Message Board" },
    { "type": "statusPanel", "title": "On Pixlpal Now", "source": "now" },
    { "type": "statusPanel", "title": "How to Send a Message", "source": "howto" },
    { "type": "statusPanel", "title": "The Last Messages", "source": "messages", "emptyText": "No messages yet." },
    {
      "type": "section",
      "id": "topic",
      "title": "The Topic",
      "widgets": [
        { "type": "textField", "title": "Topic", "emptyHint": "e.g. pixlpal-kitchen-8kq2", "filledHint": "Change the topic", "command": { "app_command": 0, "args": ["topic"] } },
        { "type": "borderButton", "label": "MAKE A NEW RANDOM TOPIC", "confirm": { "title": "A new topic?", "text": "Messages sent to the old topic won't reach Pixlpal any more.", "ok": "New topic", "cancel": "Keep" }, "command": { "app_command": 4 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "On the public ntfy.sh server anyone who knows the topic can send to it, so keep it hard to guess, like the random one. For a private topic, use your own ntfy server or an ntfy account with an access token." } ] }
      ]
    },
    {
      "type": "section",
      "id": "server",
      "title": "Server and Settings",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "ntfy Server", "emptyHint": "https://ntfy.sh", "filledHint": "Change the server", "command": { "app_command": 1, "args": ["server"] } },
        { "type": "textField", "title": "Access Token (Optional)", "secret": true, "emptyHint": "tk_... for a protected topic", "filledHint": "Change the token", "command": { "app_command": 2, "args": ["token"] } },
        { "type": "borderButton", "label": "REMOVE THE ACCESS TOKEN", "visibleWhen": { "arg": "token_set", "equals": true }, "command": { "app_command": 2, "token": "" } },
        { "type": "slider", "title": "Look for New Messages Every", "min": 10, "max": 300, "step": 5, "default": 30, "suffix": " s", "command": { "app_command": 3, "args": ["pollSec"] } },
        { "type": "switch", "title": "Chime for a New Message", "default": true, "command": { "app_command": 3, "args": ["sound"] } },
        { "type": "borderButton", "label": "CLEAR THE MESSAGES", "confirm": { "title": "Clear them?", "text": "The messages on Pixlpal are deleted.", "ok": "Clear", "cancel": "Keep" }, "command": { "app_command": 5 } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "Sending",
      "blocks": [
        { "type": "bullet", "text": "From a computer: curl -d \"The backup is done\" https://ntfy.sh/your-topic" },
        { "type": "bullet", "text": "A title and how urgent: add -H \"Title: Server\" -H \"Priority: 5\". Priority 4 shows orange, 5 flashes red with an alarm." },
        { "type": "bullet", "text": "From a phone: the ntfy app, an iPhone Shortcut or an Android automation that opens the address with the text." },
        { "type": "bullet", "text": "On Pixlpal: turn the knob for older and newer messages, click to mark them read, hold to clear them." },
        { "type": "note", "text": "Pixlpal keeps the last 8 messages. ntfy.sh keeps messages for 12 hours, so ones sent while Pixlpal was off arrive when it is back." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Topic saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Use letters, digits, - and _ only (up to 64)" },
    { "when": { "app_command": 1, "response": 0 }, "text": "The server must start with https://" },
    { "when": { "app_command": 2 }, "text": "Token saved" },
    { "when": { "app_command": 4 }, "text": "New topic made" },
    { "when": { "app_command": 5 }, "text": "Messages cleared" }
  ]
})json";

#endif
