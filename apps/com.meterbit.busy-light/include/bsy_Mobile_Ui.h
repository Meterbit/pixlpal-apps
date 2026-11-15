/**
 * @file bsy_Mobile_Ui.h
 * @brief Busy Light's phone UI {2,2} (docs/MOBILE_UI_FILES, format 2).
 *
 *   0 {"refreshToken"} link Google   1 {} unlink Google   2 {"refreshToken"} link Microsoft   3 {} unlink Microsoft
 *   4 {"sign", "minutes"?} set the sign (0 automatic)   5 {"manualMinutes"}   6 {"customText"} | {"customColor"}
 *   7 {"reply": 0-3} | {"replyText"} answer a knock
 *   8 {"useGoogle"} | {"useMicrosoft"} | {"usePresence"} | {"allCalendars"} | {"tentativeBusy"}
 *   9 {"showTitle"} | {"showNext"} | {"hour24"} | {"pulse"} | {"offHours"} | {"label"}
 *   10 {"from", "to"} | {"d0".."d6"}   11 {"knock"} | {"knockSound"} | {"phoneKnock"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
 *   12 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}   13 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}
 *   14 {} read the calendars again   15 {} a test knock
 */
#ifndef BSY_MOBILE_UI_H
#define BSY_MOBILE_UI_H

static const char BSY_UI_MANIFEST[] = R"json({
  "appID": "2/2",
  "name": "Busy Light",
  "about": "A sign for the door of your home office or studio: FREE, IN A MEETING, ON A CALL, ON AIR, DO NOT DISTURB. It follows your Google or Outlook calendar, Microsoft Teams and your working hours, or what you set here. Someone at the door can press the knob to knock; you get a message and answer from here, and your answer shows on the sign.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BSY_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "The Sign", "source": "status", "emptyText": "Open Busy Light on Pixlpal to show the sign." },
    {
      "type": "section", "id": "knock", "title": "Knocks", "widgets": [
        { "type": "statusPanel", "source": "knockRows", "emptyText": "Nobody has knocked" },
        { "type": "borderButton", "label": "COME IN", "command": { "app_command": 7, "reply": 0 }, "visibleWhen": { "arg": "knockPending", "equals": 1 } },
        { "type": "borderButton", "label": "ONE MINUTE", "command": { "app_command": 7, "reply": 1 }, "visibleWhen": { "arg": "knockPending", "equals": 1 } },
        { "type": "borderButton", "label": "FIVE MINUTES", "command": { "app_command": 7, "reply": 2 }, "visibleWhen": { "arg": "knockPending", "equals": 1 } },
        { "type": "borderButton", "label": "NOT NOW, SORRY", "command": { "app_command": 7, "reply": 3 }, "visibleWhen": { "arg": "knockPending", "equals": 1 } },
        { "type": "textField", "title": "Or Your Own Answer", "emptyHint": "Back at 3", "command": { "app_command": 7, "args": ["replyText"] } }
      ]
    },
    {
      "type": "section", "id": "set", "title": "Set the Sign", "widgets": [
        {
          "type": "radioGroup", "title": "Show", "default": 0,
          "options": [
            { "label": "Automatic: calendar, Teams and working hours", "value": 0 },
            { "label": "Free", "value": 1 },
            { "label": "Busy", "value": 2 },
            { "label": "In a meeting", "value": 3 },
            { "label": "On a call", "value": 4 },
            { "label": "On air", "value": 6 },
            { "label": "Do not disturb", "value": 7 },
            { "label": "Focusing", "value": 8 },
            { "label": "Away", "value": 9 },
            { "label": "Back soon", "value": 10 },
            { "label": "My own text (below)", "value": 13 }
          ],
          "command": { "app_command": 4, "args": ["sign"] }
        },
        {
          "type": "radioGroup", "title": "For", "default": 60,
          "options": [
            { "label": "15 minutes", "value": 15 }, { "label": "30 minutes", "value": 30 }, { "label": "1 hour", "value": 60 },
            { "label": "2 hours", "value": 120 }, { "label": "4 hours", "value": 240 }, { "label": "Until I change it", "value": 0 }
          ],
          "command": { "app_command": 5, "args": ["manualMinutes"] }
        },
        { "type": "textColorField", "title": "My Own Text", "defaultText": "RECORDING", "defaultColor": "0xFFFF2828", "command": { "app_command": 6, "args": ["customText", "customColor"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "A status set here wins over the calendar and Teams until its time runs out; then the sign goes back to automatic." }
        ] }
      ]
    },
    {
      "type": "section", "id": "calendars", "title": "Calendars and Teams", "collapsed": true, "widgets": [
        {
          "type": "accountLink", "provider": "google", "title": "Google Account",
          "about": "Sign in with Google so the sign follows your Google Calendar. Pixlpal only reads it. The same link serves the Google Calendar app.",
          "scopes": ["https://www.googleapis.com/auth/calendar.readonly", "https://www.googleapis.com/auth/tasks.readonly"],
          "linkedValue": "gLinked", "accountValue": "gAccount",
          "command": { "app_command": 0, "args": ["refreshToken"] },
          "unlinkCommand": { "app_command": 1 },
          "unlinkConfirm": "Unlink your Google account from Pixlpal? The Google Calendar app on Pixlpal is unlinked too."
        },
        { "type": "switch", "title": "Follow Google Calendar", "default": true, "command": { "app_command": 8, "args": ["useGoogle"] }, "visibleWhen": { "arg": "gLinked", "equals": 1 } },
        {
          "type": "accountLink", "provider": "microsoft", "title": "Microsoft Account",
          "about": "Sign in with Microsoft so the sign follows your Outlook calendar and, with a work or school account, your Teams status. Pixlpal only reads them. The same link serves the Outlook Calendar app.",
          "scopes": ["offline_access", "User.Read", "Calendars.Read", "Tasks.Read", "Mail.ReadBasic", "Presence.Read"],
          "linkedValue": "mLinked", "accountValue": "mAccount",
          "command": { "app_command": 2, "args": ["refreshToken"] },
          "unlinkCommand": { "app_command": 3 },
          "unlinkConfirm": "Unlink your Microsoft account from Pixlpal? The Outlook Calendar app on Pixlpal is unlinked too."
        },
        { "type": "switch", "title": "Follow Outlook Calendar", "default": true, "command": { "app_command": 8, "args": ["useMicrosoft"] }, "visibleWhen": { "arg": "mLinked", "equals": 1 } },
        { "type": "switch", "title": "Follow Teams Status", "default": true, "command": { "app_command": 8, "args": ["usePresence"] }, "visibleWhen": { "arg": "mLinked", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Teams status works with work and school accounts. If you linked Microsoft before, link it again here once to allow it." },
          { "type": "bullet", "text": "When Teams and the calendar disagree, the sign shows the busier of the two." }
        ], "visibleWhen": { "arg": "mLinked", "equals": 1 } },
        { "type": "radioGroup", "title": "Calendars", "default": 0,
          "options": [ { "label": "My own calendar only", "value": 0 }, { "label": "Every calendar I show in Google or Outlook", "value": 1 } ],
          "command": { "app_command": 8, "args": ["allCalendars"] } },
        { "type": "switch", "title": "Meetings Not Yet Accepted Count", "default": true, "command": { "app_command": 8, "args": ["tentativeBusy"] } },
        { "type": "borderButton", "label": "READ THE CALENDARS NOW", "command": { "app_command": 14 } }
      ]
    },
    {
      "type": "section", "id": "hours", "title": "Working Hours", "collapsed": true, "widgets": [
        { "type": "timeRange", "title": "Hours", "mode": "range", "defaultFrom": "09:00", "defaultTo": "17:00", "fromLabel": "From", "toLabel": "To",
          "command": { "app_command": 10, "args": ["from", "to"] } },
        { "type": "switch", "title": "Monday", "default": true, "command": { "app_command": 10, "args": ["d0"] } },
        { "type": "switch", "title": "Tuesday", "default": true, "command": { "app_command": 10, "args": ["d1"] } },
        { "type": "switch", "title": "Wednesday", "default": true, "command": { "app_command": 10, "args": ["d2"] } },
        { "type": "switch", "title": "Thursday", "default": true, "command": { "app_command": 10, "args": ["d3"] } },
        { "type": "switch", "title": "Friday", "default": true, "command": { "app_command": 10, "args": ["d4"] } },
        { "type": "switch", "title": "Saturday", "default": false, "command": { "app_command": 10, "args": ["d5"] } },
        { "type": "switch", "title": "Sunday", "default": false, "command": { "app_command": 10, "args": ["d6"] } },
        { "type": "radioGroup", "title": "Outside Working Hours", "default": 0,
          "options": [ { "label": "OFF WORK, and when you're back", "value": 0 }, { "label": "FREE", "value": 1 }, { "label": "OFF WORK, dimmed", "value": 2 } ],
          "command": { "app_command": 9, "args": ["offHours"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Meetings in your calendar show at any hour. With no day switched on, every hour is a working hour." } ] }
      ]
    },
    {
      "type": "section", "id": "sign", "title": "The Sign", "collapsed": true, "widgets": [
        { "type": "textField", "title": "Door Label", "emptyHint": "Ada's office", "command": { "app_command": 9, "args": ["label"] } },
        { "type": "switch", "title": "Show Meeting Titles", "default": false, "command": { "app_command": 9, "args": ["showTitle"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Off by default: anyone passing the door can read them." } ] },
        { "type": "switch", "title": "Show the Next Meeting When Free", "default": true, "command": { "app_command": 9, "args": ["showNext"] } },
        { "type": "switch", "title": "ON AIR Glows", "default": true, "command": { "app_command": 9, "args": ["pulse"] } },
        { "type": "switch", "title": "24-Hour Clock", "default": true, "command": { "app_command": 9, "args": ["hour24"] } }
      ]
    },
    {
      "type": "section", "id": "knocking", "title": "Knocking", "collapsed": true, "widgets": [
        { "type": "switch", "title": "Visitors Can Knock (Press the Knob)", "default": true, "command": { "app_command": 11, "args": ["knock"] } },
        { "type": "switch", "title": "Knock Sound on Pixlpal", "default": true, "command": { "app_command": 11, "args": ["knockSound"] } },
        { "type": "switch", "title": "Send Knocks to Phones", "default": true, "command": { "app_command": 11, "args": ["phoneKnock"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "A knock reaches you through ntfy, Telegram or WhatsApp, even when this app is closed. Answer here: the answer shows on the sign." },
          { "type": "bullet", "text": "With the Pixlpal app open on this page, knocks also appear above." }
        ], "visibleWhen": { "arg": "phoneKnock", "equals": 1 } },
        { "type": "switch", "title": "ntfy (Recommended)", "default": true, "command": { "app_command": 11, "args": ["ntfyOn"] }, "visibleWhen": { "arg": "phoneKnock", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install the free ntfy app (Android or iPhone), tap +, and subscribe to the topic shown under Knocks above. No account is needed." },
          { "type": "paragraph", "spans": [ { "text": "ntfy: " }, { "text": "ntfy.sh", "link": "https://ntfy.sh" } ] }
        ], "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Topic (Optional: Your Own)", "emptyHint": "A long, hard-to-guess name", "command": { "app_command": 12, "args": ["ntfyTopic"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "ntfy.sh", "command": { "app_command": 12, "args": ["ntfyServer"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Access Token (Protected Topics Only)", "emptyHint": "tk_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 13, "args": ["ntfyToken"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "switch", "title": "Telegram", "default": false, "command": { "app_command": 11, "args": ["telegramOn"] }, "visibleWhen": { "arg": "phoneKnock", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "In Telegram, talk to @BotFather: /newbot gives a bot token. Send your bot a message, then find your chat ID with @userinfobot." }
        ], "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Bot Token", "emptyHint": "123456:ABC...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 13, "args": ["telegramToken"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Chat ID", "emptyHint": "123456789", "command": { "app_command": 12, "args": ["telegramChat"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "switch", "title": "WhatsApp (CallMeBot)", "default": false, "command": { "app_command": 11, "args": ["whatsappOn"] }, "visibleWhen": { "arg": "phoneKnock", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "CallMeBot is free for personal use: from your phone, send \"I allow callmebot to send me messages\" to the number on callmebot.com, and it replies with your API key." },
          { "type": "paragraph", "spans": [ { "text": "Instructions: " }, { "text": "callmebot.com", "link": "https://www.callmebot.com/blog/free-api-whatsapp-messages/" } ] }
        ], "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "WhatsApp Number", "emptyHint": "+2348012345678", "command": { "app_command": 12, "args": ["whatsappPhone"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "CallMeBot API Key", "emptyHint": "123456", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "command": { "app_command": 13, "args": ["whatsappKey"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "borderButton", "label": "SEND A TEST KNOCK", "command": { "app_command": 15 }, "visibleWhen": { "arg": "phoneKnock", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "On Pixlpal: press the knob to knock. Turn it (or hold it) to see the day's meetings; double click to read the calendar again." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Google linked" },
    { "when": { "app_command": 2 }, "text": "Microsoft linked" },
    { "when": { "app_command": 4 }, "text": "The sign is updated" },
    { "when": { "app_command": 7 }, "text": "Your answer is on the sign" },
    { "when": { "app_command": 14 }, "text": "Reading the calendars" },
    { "when": { "app_command": 15 }, "text": "Test knock sent to your phones" },
    { "when": { "app_command": 0, "response": 0 }, "text": "The link did not work: try again" },
    { "when": { "app_command": 2, "response": 0 }, "text": "The link did not work: try again" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Type a few words" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Open Busy Light on Pixlpal first" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Pick two different times" },
    { "when": { "app_command": 12, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 14, "response": 0 }, "text": "Open Busy Light on Pixlpal first" },
    { "when": { "app_command": 15, "response": 0 }, "text": "Switch on ntfy, Telegram or WhatsApp first, and open the app on Pixlpal" }
  ]
})json";

#endif
