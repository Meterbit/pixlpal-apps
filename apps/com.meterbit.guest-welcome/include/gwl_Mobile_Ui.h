/**
 * @file gwl_Mobile_Ui.h
 * @brief Guest Welcome's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (guestWelcome.cpp):
 *   0 {"guest": "Ms Okafor"}    1 {"room": "412"}    2 {"hotel": "Lagoon Hotel"}
 *   3 {"ssid": "Lagoon-Guest"}  4 {"password": "..."}  5 {"security": 0 WPA/WPA2/WPA3, 1 WEP, 2 none}
 *   6 {"showPassword": 0|1}     7 {"checkout": "11:00"}   8 {"breakfast": "06:30-10:30"}   9 {"message": "..."}
 *   10 {}  the guest has left: name, room and message cleared       11 {"night": 0|1}  a quiet clock at night
 */
#ifndef GWL_MOBILE_UI_H
#define GWL_MOBILE_UI_H

static const char GWL_UI_MANIFEST[] = R"json({
  "name": "Guest Welcome",
  "about": "A personal welcome in the guest's room: their name and room number, a Wi-Fi code their phone can scan to join, checkout time, breakfast hours and the hotel's message of the day. It dims to a quiet clock at night.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char GWL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.guest-welcome.png", "height": 64, "alt": "Guest Welcome" },
    { "type": "statusPanel", "title": "On the Screen", "source": "screen" },
    {
      "type": "section",
      "id": "guest",
      "title": "The Guest",
      "widgets": [
        { "type": "textField", "title": "Guest's Name", "emptyHint": "e.g. Ms Okafor, or the Adeyemi family", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["guest"] } },
        { "type": "textField", "title": "Room", "emptyHint": "e.g. 412", "filledHint": "Change the room", "command": { "app_command": 1, "args": ["room"] } },
        { "type": "textField", "title": "Message of the Day", "emptyHint": "e.g. Pool party tonight at 7, by the bar", "filledHint": "Change the message (empty: none)", "command": { "app_command": 9, "args": ["message"] } },
        { "type": "borderButton", "label": "THE GUEST HAS LEFT", "confirm": { "title": "Clear the guest?", "text": "The name, room and message are cleared; the hotel's details stay.", "ok": "Clear", "cancel": "Keep" }, "command": { "app_command": 10 } }
      ]
    },
    {
      "type": "section",
      "id": "wifi",
      "title": "Wi-Fi",
      "widgets": [
        { "type": "textField", "title": "Network Name", "emptyHint": "e.g. Lagoon-Guest", "filledHint": "Change the network name", "command": { "app_command": 3, "args": ["ssid"] } },
        { "type": "textField", "title": "Password", "emptyHint": "The guests' Wi-Fi password", "filledHint": "A password is saved (send a new one to change it)", "secret": true, "command": { "app_command": 4, "args": ["password"] } },
        { "type": "radioGroup", "title": "Security", "default": 0, "options": [ { "label": "WPA, WPA2 or WPA3", "value": 0 }, { "label": "WEP", "value": 1 }, { "label": "None (open network)", "value": 2 } ], "command": { "app_command": 5, "args": ["security"] } },
        { "type": "switch", "title": "Show the Password Beside the Code", "default": true, "command": { "app_command": 6, "args": ["showPassword"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Guests point their phone's camera at the code on Pixlpal to join the network, with no typing." } ] }
      ]
    },
    {
      "type": "section",
      "id": "hotel",
      "title": "The Hotel",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Hotel Name", "emptyHint": "e.g. Lagoon Hotel", "filledHint": "Change the hotel's name", "command": { "app_command": 2, "args": ["hotel"] } },
        { "type": "textField", "title": "Checkout", "emptyHint": "11:00", "filledHint": "Change the checkout time", "command": { "app_command": 7, "args": ["checkout"] } },
        { "type": "textField", "title": "Breakfast", "emptyHint": "06:30-10:30, Garden Restaurant", "filledHint": "Change the breakfast hours", "command": { "app_command": 8, "args": ["breakfast"] } },
        { "type": "switch", "title": "Quiet Clock at Night (10 pm to 6 am)", "default": true, "command": { "app_command": 11, "args": ["night"] } }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Name saved" },
    { "when": { "app_command": 3 }, "text": "Network saved" },
    { "when": { "app_command": 4 }, "text": "Password saved" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Up to 63 characters" },
    { "when": { "app_command": 10 }, "text": "Guest cleared" }
  ]
})json";

#endif
