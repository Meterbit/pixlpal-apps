/**
 * @file mts_Mobile_Ui.h
 * @brief Motorsport Live's phone UI {5,1} (docs/MOBILE_UI_FILES, format 2).
 *
 *   0 {"user"} | {"pass"} (secret) the OpenF1 sponsor account   1 {"favourite": number | "HAM" | 0}
 *   2 {"p0".."p4"} | {"cycle"} | {"seconds"} | {"hour24"} | {"liveFirst"}
 *   3 {"alertMinutes"} | {"alertRacesOnly"} | {"alertFlags"} | {"sound"} | {"phoneAlerts"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
 *   4 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}   5 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}
 *   6 {} read again now   7 {} a test alert
 */
#ifndef MTS_MOBILE_UI_H
#define MTS_MOBILE_UI_H

static const char MTS_UI_MANIFEST[] = R"json({
  "appID": "5/1",
  "name": "Motorsport Live",
  "about": "Formula 1 race weekends on Pixlpal: every session in your time with a countdown and the circuit, live timing while a session runs (positions, gaps, tyres, pit stops, safety cars and flags), the latest result, and both championships. Data from OpenF1, an unofficial community project; not associated with Formula 1.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char MTS_UI_API[] = R"json({
  "widgets": [
    { "type": "statusPanel", "title": "The Weekend", "source": "status", "emptyText": "Open Motorsport Live on Pixlpal to see the weekend here." },
    {
      "type": "section", "id": "live", "title": "Live Timing", "subtitle": "Needs an OpenF1 sponsor account", "collapsed": true, "widgets": [
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "Without an account, Pixlpal shows the schedule, results and championships for free. OpenF1 keeps data back while a session is live (from 30 minutes before it to 30 minutes after it), so results appear about 30 minutes after the flag." },
          { "type": "paragraph", "text": "With an OpenF1 sponsor account (€9.90 a month, for personal use), live timing shows during every session: positions, gaps, tyres, pit stops, safety cars and flags, a few seconds behind the track." },
          { "type": "paragraph", "spans": [ { "text": "Sponsor OpenF1: " }, { "text": "openf1.org", "link": "https://openf1.org/#sponsorship" } ] }
        ] },
        { "type": "textField", "title": "OpenF1 Username", "emptyHint": "Your sponsor account's username", "command": { "app_command": 0, "args": ["user"] } },
        { "type": "textField", "title": "OpenF1 Password", "emptyHint": "Kept on Pixlpal only", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "obscureText": true, "command": { "app_command": 0, "args": ["pass"] } },
        { "type": "switch", "title": "Show Live Timing First During Sessions", "default": true, "command": { "app_command": 2, "args": ["liveFirst"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "To remove the account, send an empty username." } ] }
      ]
    },
    {
      "type": "section", "id": "fav", "title": "Your Driver", "collapsed": true, "widgets": [
        { "type": "textField", "title": "Driver Number or Code", "emptyHint": "44, or HAM (0: none)", "command": { "app_command": 1, "args": ["favourite"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Your driver is highlighted in timing, results and the championship. A code works while the app runs on Pixlpal." } ] }
      ]
    },
    {
      "type": "section", "id": "alerts", "title": "Alerts", "collapsed": true, "widgets": [
        { "type": "statusPanel", "title": "Alerts", "source": "alertRows", "emptyText": "On Pixlpal's status bar" },
        {
          "type": "radioGroup", "title": "Before a Session Starts", "default": 15,
          "options": [ { "label": "Off", "value": 0 }, { "label": "5 minutes", "value": 5 }, { "label": "15 minutes", "value": 15 }, { "label": "30 minutes", "value": 30 }, { "label": "1 hour", "value": 60 } ],
          "command": { "app_command": 3, "args": ["alertMinutes"] }
        },
        { "type": "switch", "title": "Only Qualifying, Sprints and Races", "default": true, "command": { "app_command": 3, "args": ["alertRacesOnly"] } },
        { "type": "switch", "title": "Safety Cars and Red Flags (Live Timing)", "default": true, "command": { "app_command": 3, "args": ["alertFlags"] } },
        { "type": "switch", "title": "Sounds", "default": true, "command": { "app_command": 3, "args": ["sound"] } },
        { "type": "switch", "title": "Send Alerts to Phones", "default": false, "command": { "app_command": 3, "args": ["phoneAlerts"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Session starts, red flags and race results go to your phones through ntfy, Telegram or WhatsApp while Motorsport Live is on Pixlpal's display." }
        ], "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "switch", "title": "ntfy (Recommended)", "default": false, "command": { "app_command": 3, "args": ["ntfyOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "Install the free ntfy app (Android or iPhone), tap +, and subscribe to the topic shown under Alerts above. No account is needed." },
          { "type": "paragraph", "spans": [ { "text": "ntfy: " }, { "text": "ntfy.sh", "link": "https://ntfy.sh" } ] }
        ], "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Topic (Optional: Your Own)", "emptyHint": "A long, hard-to-guess name", "command": { "app_command": 4, "args": ["ntfyTopic"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "ntfy.sh", "command": { "app_command": 4, "args": ["ntfyServer"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "textField", "title": "Access Token (Protected Topics Only)", "emptyHint": "tk_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 5, "args": ["ntfyToken"] }, "visibleWhen": { "arg": "ntfyOn", "equals": 1 } },
        { "type": "switch", "title": "Telegram", "default": false, "command": { "app_command": 3, "args": ["telegramOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "In Telegram, talk to @BotFather: /newbot gives a bot token. Send your bot a message (or add it to a group), then find the chat ID with @userinfobot." }
        ], "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Bot Token", "emptyHint": "123456:ABC...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 5, "args": ["telegramToken"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "textField", "title": "Chat or Group ID", "emptyHint": "123456789", "command": { "app_command": 4, "args": ["telegramChat"] }, "visibleWhen": { "arg": "telegramOn", "equals": 1 } },
        { "type": "switch", "title": "WhatsApp (CallMeBot)", "default": false, "command": { "app_command": 3, "args": ["whatsappOn"] }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "CallMeBot is free for personal use: from your phone, send \"I allow callmebot to send me messages\" to the number on callmebot.com, and it replies with your API key." },
          { "type": "paragraph", "spans": [ { "text": "Instructions: " }, { "text": "callmebot.com", "link": "https://www.callmebot.com/blog/free-api-whatsapp-messages/" } ] }
        ], "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "WhatsApp Number", "emptyHint": "+2348012345678", "command": { "app_command": 4, "args": ["whatsappPhone"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "textField", "title": "CallMeBot API Key", "emptyHint": "123456", "filledHint": "Saved on Pixlpal: type again to change it", "secret": true, "command": { "app_command": 5, "args": ["whatsappKey"] }, "visibleWhen": { "arg": "whatsappOn", "equals": 1 } },
        { "type": "borderButton", "label": "SEND A TEST ALERT", "command": { "app_command": 7 }, "visibleWhen": { "arg": "phoneAlerts", "equals": 1 } }
      ]
    },
    {
      "type": "section", "id": "display", "title": "Pages", "collapsed": true, "widgets": [
        { "type": "switch", "title": "Page: The Weekend", "default": true, "command": { "app_command": 2, "args": ["p0"] } },
        { "type": "switch", "title": "Page: Live Timing", "default": true, "command": { "app_command": 2, "args": ["p1"] } },
        { "type": "switch", "title": "Page: Latest Result", "default": true, "command": { "app_command": 2, "args": ["p2"] } },
        { "type": "switch", "title": "Page: Drivers' Championship", "default": true, "command": { "app_command": 2, "args": ["p3"] } },
        { "type": "switch", "title": "Page: Teams' Championship", "default": true, "command": { "app_command": 2, "args": ["p4"] } },
        { "type": "switch", "title": "Turn Pages by Themselves", "default": true, "command": { "app_command": 2, "args": ["cycle"] } },
        { "type": "slider", "title": "Time per Page", "min": 5, "max": 120, "default": 15, "suffix": " s", "command": { "app_command": 2, "args": ["seconds"] }, "visibleWhen": { "arg": "cycle", "equals": 1 } },
        { "type": "switch", "title": "24-Hour Clock", "default": true, "command": { "app_command": 2, "args": ["hour24"] } },
        { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 6 } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "On Pixlpal: click for the next page; on live timing, turn the knob for the other ten cars. Double click to refresh, hold to start or stop turning pages." }
        ] }
      ]
    },
    {
      "type": "richTextBlock", "title": "Data", "blocks": [
        { "type": "paragraph", "spans": [ { "text": "Data from " }, { "text": "OpenF1", "link": "https://openf1.org" }, { "text": " (CC BY-NC-SA 4.0), an unofficial community project. Not associated with Formula 1, the FIA or Formula One Management. F1 and FORMULA 1 are trademarks of Formula One Licensing B.V." } ] }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Saved: signing in to OpenF1" },
    { "when": { "app_command": 1 }, "text": "Your driver is saved" },
    { "when": { "app_command": 6 }, "text": "Refreshing" },
    { "when": { "app_command": 7 }, "text": "Test alert sent to your phones" },
    { "when": { "app_command": 0, "response": 0 }, "text": "That is too long" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Use a driver number (1-99), or a three-letter code while the app runs on Pixlpal" },
    { "when": { "app_command": 4, "response": 0 }, "text": "Check what you typed" },
    { "when": { "app_command": 6, "response": 0 }, "text": "Open Motorsport Live on Pixlpal first" },
    { "when": { "app_command": 7, "response": 0 }, "text": "Switch on ntfy, Telegram or WhatsApp first, and open the app on Pixlpal" }
  ]
})json";

#endif
