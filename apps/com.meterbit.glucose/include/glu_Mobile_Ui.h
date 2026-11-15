/**
 * @file glu_Mobile_Ui.h
 * @brief Glucose Display's phone UI {14,2} (docs/MOBILE_UI_FILES, format 2).
 *
 *   0 {"source"}   1 {"nsUrl"} | {"dxUser"} | {"dxRegion"} | {"lluEmail"} | {"lluPatient"} | {"name"}
 *   2 {"nsToken"} | {"nsSecret"} | {"dxPass"} | {"lluPass"} (secret)   3 {"units"} | {"useSiteTargets"} | {"graphHours"} | {"hour24"}
 *   4 {"<line>Mg"} | {"<line>Mmol"} for urgentLow, low, high, urgentHigh, targetLow, targetHigh
 *   5 {"soonLowOn"} | {"lowOn"} | {"fallFastOn"} | {"staleOn"} | {"urgentHighOn"} | {"highOn"} | {"riseFastOn"} | {"sound"}
 *     | {"phoneAlerts"} | {"quietHighs"} | {"nightDim"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
 *   6 {"staleMinutes"} | {"lowSnooze"} | {"highSnooze"}   7 {"from", "to"} quiet hours   8 {} snooze   9 {} read again now
 *   10 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}   11 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}
 *   12 {} a test alert
 */
#ifndef GLU_MOBILE_UI_H
#define GLU_MOBILE_UI_H

static const char GLU_UI_MANIFEST[] = R"json({
  "appID": "14/2",
  "name": "Glucose Display",
  "about": "A continuous glucose monitor's reading, its trend arrow and the last hours, large enough to read across the room, from Nightscout, Dexcom Share or LibreLinkUp. Alarms for lows, highs, fast changes and missing readings sound on Pixlpal and reach the family's phones. Not a medical device: keep your CGM app and its alarms on.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char GLU_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "Now", "source": "status", "emptyText": "Open Glucose Display on Pixlpal to see the readings here." },
    { "type": "borderButton", "label": "SNOOZE THE ALARM", "command": { "app_command": 8 }, "visibleWhen": { "arg": "alarmOn", "equals": 1 } },
    { "type": "richTextBlock", "blocks": [
      { "type": "note", "text": "Not a medical device. Pixlpal shows what your CGM shares online, a few minutes late at times. Keep the CGM app and its own alarms on, and make treatment decisions from them." }
    ] },
    {
      "type": "section", "id": "source", "title": "Where the Readings Come From", "widgets": [
        {
          "type": "radioGroup", "title": "Source", "default": 0,
          "options": [
            { "label": "Nightscout (recommended)", "value": 0 },
            { "label": "Dexcom Share (Dexcom G6, G7, ONE+)", "value": 1 },
            { "label": "LibreLinkUp (FreeStyle Libre 2, 3)", "value": 2 }
          ],
          "command": { "app_command": 0, "args": ["source"] }
        },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Your own Nightscout site works with almost every CGM and pump loop, and adds insulin and carbs on board from Loop, AAPS or OpenAPS." },
          { "type": "bullet", "text": "In Nightscout, open Admin Tools, add a subject with the role \"readable\", and paste its token below. An API secret works too, but a token can only read." }
        ], "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "textField", "title": "Nightscout Address", "emptyHint": "https://yoursite.example.com", "command": { "app_command": 1, "args": ["nsUrl"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "textField", "title": "Access Token (Recommended)", "emptyHint": "pixlpal-1a2b3c4d5e6f7a8b", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 2, "args": ["nsToken"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "textField", "title": "Or the API Secret", "emptyHint": "12 characters or more", "filledHint": "Saved on Pixlpal (hashed): type again to change it", "secret": true, "obscureText": true, "command": { "app_command": 2, "args": ["nsSecret"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "switch", "title": "Use My Site's Target Range for the Alarms", "default": true, "command": { "app_command": 3, "args": ["useSiteTargets"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Share must be on in the Dexcom app, with at least one follower invited. Type the Dexcom account of the person wearing the sensor (the publisher), not a follower's." },
          { "type": "bullet", "text": "Dexcom Share is not an official interface. Pixlpal uses it as other open-source tools do; Dexcom can change it. After a wrong password, Pixlpal stops trying until you type it again, as Dexcom locks accounts after repeated failures." }
        ], "visibleWhen": { "arg": "source", "equals": 1 } },
        { "type": "textField", "title": "Dexcom User Name, Email or Phone", "emptyHint": "+11234567890 or name@example.com", "command": { "app_command": 1, "args": ["dxUser"] }, "visibleWhen": { "arg": "source", "equals": 1 } },
        { "type": "textField", "title": "Dexcom Password", "emptyHint": "Password", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "obscureText": true, "command": { "app_command": 2, "args": ["dxPass"] }, "visibleWhen": { "arg": "source", "equals": 1 } },
        {
          "type": "radioGroup", "title": "Dexcom Account Region", "default": 0,
          "options": [ { "label": "United States", "value": 0 }, { "label": "Outside the United States", "value": 1 }, { "label": "Japan", "value": 2 } ],
          "command": { "app_command": 1, "args": ["dxRegion"] }, "visibleWhen": { "arg": "source", "equals": 1 }
        },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install LibreLinkUp, create an account, and have the sensor wearer invite it from their Libre app (Connected Apps, LibreLinkUp). Type that LibreLinkUp account here." },
          { "type": "bullet", "text": "LibreLinkUp is not an official interface for other apps; Abbott can change it. If Abbott shows new terms, accept them in the LibreLinkUp app, then send the password again." }
        ], "visibleWhen": { "arg": "source", "equals": 2 } },
        { "type": "textField", "title": "LibreLinkUp Email", "emptyHint": "name@example.com", "command": { "app_command": 1, "args": ["lluEmail"] }, "visibleWhen": { "arg": "source", "equals": 2 } },
        { "type": "textField", "title": "LibreLinkUp Password", "emptyHint": "Password", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "obscureText": true, "command": { "app_command": 2, "args": ["lluPass"] }, "visibleWhen": { "arg": "source", "equals": 2 } },
        {
          "type": "radioGroup", "title": "Whose Readings (If You Follow Several)", "default": 0,
          "options": [ { "label": "Person 1", "value": 0 }, { "label": "Person 2", "value": 1 }, { "label": "Person 3", "value": 2 }, { "label": "Person 4", "value": 3 } ],
          "command": { "app_command": 1, "args": ["lluPatient"] }, "visibleWhen": { "arg": "source", "equals": 2 }
        },
        { "type": "borderButton", "label": "CONNECT AGAIN NOW", "command": { "app_command": 9 } }
      ]
    },
    {
      "type": "section", "id": "alarms", "title": "Alarms", "collapsed": true, "widgets": [
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "URGENT LOW always sounds. A click on Pixlpal's knob, or the button above, snoozes an alarm; a new low or high starts again unsnoozed." }
        ] },
        { "type": "switch", "title": "Urgent Low Soon (Falling Toward It)", "default": true, "command": { "app_command": 5, "args": ["soonLowOn"] } },
        { "type": "switch", "title": "Low", "default": true, "command": { "app_command": 5, "args": ["lowOn"] } },
        { "type": "switch", "title": "Falling Fast", "default": false, "command": { "app_command": 5, "args": ["fallFastOn"] } },
        { "type": "switch", "title": "Urgent High", "default": true, "command": { "app_command": 5, "args": ["urgentHighOn"] } },
        { "type": "switch", "title": "High", "default": false, "command": { "app_command": 5, "args": ["highOn"] } },
        { "type": "switch", "title": "Rising Fast", "default": false, "command": { "app_command": 5, "args": ["riseFastOn"] } },
        { "type": "switch", "title": "No Readings", "default": true, "command": { "app_command": 5, "args": ["staleOn"] } },
        { "type": "slider", "title": "No Readings For", "min": 10, "max": 120, "step": 5, "default": 20, "suffix": " min", "command": { "app_command": 6, "args": ["staleMinutes"] }, "visibleWhen": { "arg": "staleOn", "equals": 1 } },
        { "type": "slider", "title": "Urgent Low Below", "min": 40, "max": 70, "default": 55, "suffix": " mg/dL", "command": { "app_command": 4, "args": ["urgentLowMg"] }, "visibleWhen": { "arg": "mmol", "equals": 0 } },
        { "type": "slider", "title": "Low Below", "min": 60, "max": 120, "default": 70, "suffix": " mg/dL", "command": { "app_command": 4, "args": ["lowMg"] }, "visibleWhen": { "arg": "mmol", "equals": 0 } },
        { "type": "slider", "title": "High Above", "min": 120, "max": 300, "step": 5, "default": 180, "suffix": " mg/dL", "command": { "app_command": 4, "args": ["highMg"] }, "visibleWhen": { "arg": "mmol", "equals": 0 } },
        { "type": "slider", "title": "Urgent High From", "min": 180, "max": 400, "step": 10, "default": 250, "suffix": " mg/dL", "command": { "app_command": 4, "args": ["urgentHighMg"] }, "visibleWhen": { "arg": "mmol", "equals": 0 } },
        { "type": "slider", "title": "Urgent Low Below", "min": 2.2, "max": 3.9, "step": 0.1, "decimals": 1, "default": 3.1, "suffix": " mmol", "command": { "app_command": 4, "args": ["urgentLowMmol"] }, "visibleWhen": { "arg": "mmol", "equals": 1 } },
        { "type": "slider", "title": "Low Below", "min": 3.3, "max": 6.7, "step": 0.1, "decimals": 1, "default": 3.9, "suffix": " mmol", "command": { "app_command": 4, "args": ["lowMmol"] }, "visibleWhen": { "arg": "mmol", "equals": 1 } },
        { "type": "slider", "title": "High Above", "min": 6.7, "max": 16.7, "step": 0.1, "decimals": 1, "default": 10.0, "suffix": " mmol", "command": { "app_command": 4, "args": ["highMmol"] }, "visibleWhen": { "arg": "mmol", "equals": 1 } },
        { "type": "slider", "title": "Urgent High From", "min": 10.0, "max": 22.2, "step": 0.1, "decimals": 1, "default": 13.9, "suffix": " mmol", "command": { "app_command": 4, "args": ["urgentHighMmol"] }, "visibleWhen": { "arg": "mmol", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "With a Nightscout site and its target range switched on above, the site's lines are used instead of these." }
        ], "visibleWhen": { "arg": "source", "equals": 0 } },
        { "type": "slider", "title": "Snooze Lows For", "min": 5, "max": 60, "step": 5, "default": 15, "suffix": " min", "command": { "app_command": 6, "args": ["lowSnooze"] } },
        { "type": "slider", "title": "Snooze Highs For", "min": 15, "max": 240, "step": 15, "default": 60, "suffix": " min", "command": { "app_command": 6, "args": ["highSnooze"] } },
        { "type": "switch", "title": "Alarm Sound on Pixlpal", "default": true, "command": { "app_command": 5, "args": ["sound"] } },
        { "type": "timeRange", "title": "Quiet Hours", "mode": "range", "defaultFrom": "22:00", "defaultTo": "07:00", "fromLabel": "From", "toLabel": "To", "command": { "app_command": 7, "args": ["from", "to"] } },
        { "type": "switch", "title": "No High Alarms in the Quiet Hours", "default": true, "command": { "app_command": 5, "args": ["quietHighs"] } },
        { "type": "switch", "title": "Dim Night Screen in the Quiet Hours", "default": true, "command": { "app_command": 5, "args": ["nightDim"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "Lows, urgent highs and missing readings sound at any hour. Alarms work while Glucose Display is on Pixlpal's screen: in Carousel mode, pin it (Fixed mode) at night." }
        ] }
      ]
    },
    {
      "type": "section", "id": "phones", "title": "Alarms on the Family's Phones", "collapsed": true, "widgets": [
        { "type": "switch", "title": "Send Alarms to Phones", "default": true, "command": { "app_command": 5, "args": ["phoneAlerts"] } },
        { "type": "statusPanel", "source": "alertRows", "emptyText": "" , "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "An urgent low is sent again every 5 minutes until it passes or someone snoozes it on Pixlpal; other alarms every 15 minutes." }
        ], "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "ntfy (Recommended)", "default": true, "command": { "app_command": 5, "args": ["ntfyOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install the free ntfy app (Android or iPhone), tap +, and subscribe to the topic shown above. No account is needed. Urgent lows arrive at ntfy's highest priority." },
          { "type": "paragraph", "spans": [ { "text": "ntfy: " }, { "text": "ntfy.sh", "link": "https://ntfy.sh" } ] }
        ], "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Topic (Optional: Your Own)", "emptyHint": "A long, hard-to-guess name", "command": { "app_command": 10, "args": ["ntfyTopic"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "ntfy.sh", "command": { "app_command": 10, "args": ["ntfyServer"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Access Token (Protected Topics Only)", "emptyHint": "tk_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 11, "args": ["ntfyToken"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "switch", "title": "Telegram", "default": false, "command": { "app_command": 5, "args": ["telegramOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "In Telegram, talk to @BotFather: /newbot gives a bot token. Send your bot a message, then find your chat ID with @userinfobot. A group's ID reaches everyone in it." }
        ], "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Bot Token", "emptyHint": "123456:ABC...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 11, "args": ["telegramToken"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Chat ID", "emptyHint": "123456789", "command": { "app_command": 10, "args": ["telegramChat"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "switch", "title": "WhatsApp (CallMeBot)", "default": false, "command": { "app_command": 5, "args": ["whatsappOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "CallMeBot is free for personal use: from your phone, send \"I allow callmebot to send me messages\" to the number on callmebot.com, and it replies with your API key." },
          { "type": "paragraph", "spans": [ { "text": "Instructions: " }, { "text": "callmebot.com", "link": "https://www.callmebot.com/blog/free-api-whatsapp-messages/" } ] }
        ], "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "WhatsApp Number", "emptyHint": "+2348012345678", "command": { "app_command": 10, "args": ["whatsappPhone"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "CallMeBot API Key", "emptyHint": "123456", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "command": { "app_command": 11, "args": ["whatsappKey"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "borderButton", "label": "SEND A TEST ALERT", "command": { "app_command": 12 }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } }
      ]
    },
    {
      "type": "section", "id": "display", "title": "The Screen", "collapsed": true, "widgets": [
        { "type": "textField", "title": "Whose Readings (Shown on Top)", "emptyHint": "Ada", "command": { "app_command": 1, "args": ["name"] } },
        {
          "type": "radioGroup", "title": "Units", "default": 2,
          "options": [ { "label": "mg/dL", "value": 0 }, { "label": "mmol/L", "value": 1 }, { "label": "As my Nightscout site", "value": 2 } ],
          "command": { "app_command": 3, "args": ["units"] }
        },
        {
          "type": "radioGroup", "title": "Graph Under the Reading", "default": 3,
          "options": [ { "label": "3 hours", "value": 3 }, { "label": "6 hours", "value": 6 }, { "label": "12 hours", "value": 12 }, { "label": "24 hours", "value": 24 } ],
          "command": { "app_command": 3, "args": ["graphHours"] }
        },
        { "type": "switch", "title": "24-Hour Clock", "default": true, "command": { "app_command": 3, "args": ["hour24"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "On Pixlpal: click to snooze an alarm (or, with none, to change the graph's hours); turn for a longer graph and the last 24 hours in numbers (time in range, average, GMI, variability); click twice to ask the source again." }
        ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Source changed" },
    { "when": { "app_command": 2 }, "text": "Saved on Pixlpal" },
    { "when": { "app_command": 8 }, "text": "Alarm snoozed" },
    { "when": { "app_command": 9 }, "text": "Connecting again" },
    { "when": { "app_command": 12 }, "text": "Test alert sent to your phones" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Check what you typed (an API secret has 12 characters or more)" },
    { "when": { "app_command": 4, "response": 0 }, "text": "That line is out of range" },
    { "when": { "app_command": 8, "response": 0 }, "text": "No alarm is sounding" },
    { "when": { "app_command": 9, "response": 0 }, "text": "Open Glucose Display on Pixlpal first" },
    { "when": { "app_command": 10, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 12, "response": 0 }, "text": "Switch on ntfy, Telegram or WhatsApp first, and open the app on Pixlpal" }
  ]
})json";

#endif
