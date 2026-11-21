/**
 * @file swd_Mobile_Ui.h
 * @brief Sleep Wind-Down's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (sleepWindDown.cpp):
 *   0 {"bed": "22:30", "wake": "06:30"}     every night (or Sunday to Thursday nights, with a weekend time)
 *   1 {"wbed": "23:30", "wwake": "08:00"}   Friday and Saturday nights
 *   2 {"weekend": 0|1}                      3 {"windDown": 15-120}  minutes     4 {"sunrise": 0-45}  minutes (0: none)
 *   5 {"night": 0|1}                        a dim red clock, or dark            6 {"chime": 0|1} | {"windChime": 0|1}
 */
#ifndef SWD_MOBILE_UI_H
#define SWD_MOBILE_UI_H

static const char SWD_UI_MANIFEST[] = R"json({
  "name": "Sleep Wind-Down",
  "about": "A calmer bedtime: a countdown to the bedtime you choose, a reminder to wind down before it as the panel dims to a warm glow, a dim red clock or darkness at night, and a gentle sunrise light to wake to.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SWD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.sleep-wind-down.png", "height": 64, "alt": "Sleep Wind-Down" },
    { "type": "statusPanel", "title": "Tonight", "source": "now" },
    { "type": "timeRange", "title": "Bedtime and Wake-Up", "mode": "range", "defaultFrom": "22:30", "defaultTo": "06:30", "command": { "app_command": 0, "args": ["bed", "wake"] } },
    { "type": "switch", "title": "Different Times on Friday and Saturday Nights", "default": false, "command": { "app_command": 2, "args": ["weekend"] } },
    { "type": "timeRange", "title": "Friday and Saturday Nights", "mode": "range", "defaultFrom": "23:30", "defaultTo": "08:00", "command": { "app_command": 1, "args": ["wbed", "wwake"] }, "visibleWhen": { "arg": "weekend", "equals": 1 } },
    {
      "type": "section",
      "id": "evening",
      "title": "Evening",
      "widgets": [
        { "type": "slider", "title": "Wind Down For", "min": 15, "max": 120, "step": 15, "default": 60, "suffix": " min", "command": { "app_command": 3, "args": ["windDown"] } },
        { "type": "switch", "title": "Soft Chime When It's Time to Wind Down", "default": true, "command": { "app_command": 6, "args": ["windChime"] } },
        { "type": "radioGroup", "title": "At Night", "default": 0, "options": [ { "label": "A dim red clock", "value": 0 }, { "label": "Dark", "value": 1 } ], "command": { "app_command": 5, "args": ["night"] } }
      ]
    },
    {
      "type": "section",
      "id": "morning",
      "title": "Morning",
      "widgets": [
        { "type": "slider", "title": "Sunrise Light Before Wake-Up (0: None)", "min": 0, "max": 45, "step": 5, "default": 20, "suffix": " min", "command": { "app_command": 4, "args": ["sunrise"] } },
        { "type": "switch", "title": "Gentle Chime at Wake-Up", "default": false, "command": { "app_command": 6, "args": ["chime"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "The chime is soft: keep your usual alarm if you must not oversleep." } ] }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Leave Sleep Wind-Down open in the bedroom. During the wind-down the panel grows dimmer and warmer, with an idea for winding down now and then." },
        { "type": "bullet", "text": "At night, click the knob to see the time for a few seconds. In the day, turn it for brightness." },
        { "type": "bullet", "text": "Pixlpal's own brightness is never changed: the app only draws dimmer." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Bedtime and wake-up must differ" },
    { "when": { "app_command": 1 }, "text": "Saved" }
  ]
})json";

#endif
