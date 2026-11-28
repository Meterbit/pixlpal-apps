/**
 * @file spl_Mobile_Ui.h
 * @brief Space Launches' phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (spaceLaunches.cpp):
 *   0 {"provider": 0-8}    whose launches: all, or one launch company
 *   1 {"page": 0|1}        the next launch, or the list
 *   2 {"sound": 0|1}       the last ten seconds and the liftoff, out loud
 *   3 {}                   check again now
 */
#ifndef SPL_MOBILE_UI_H
#define SPL_MOBILE_UI_H

static const char SPL_UI_MANIFEST[] = R"json({
  "name": "Space Launches",
  "about": "A countdown to the next rocket launch anywhere in the world: the rocket, the mission, the pad and whether it is go, with the launches after it. From The Space Devs' Launch Library.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SPL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.space-launches.png", "height": 64, "alt": "Space Launches" },
    { "type": "statusPanel", "title": "Next Launch", "source": "next", "emptyText": "Open Space Launches on Pixlpal." },
    { "type": "statusPanel", "title": "After It", "source": "later" },
    {
      "type": "radioGroup",
      "title": "Launches By",
      "default": 0,
      "options": [
        { "label": "Everyone", "value": 0 },
        { "label": "SpaceX", "value": 1 },
        { "label": "Rocket Lab", "value": 2 },
        { "label": "United Launch Alliance", "value": 3 },
        { "label": "Blue Origin", "value": 4 },
        { "label": "Arianespace", "value": 5 },
        { "label": "ISRO (India)", "value": 6 },
        { "label": "CASC (China)", "value": 7 },
        { "label": "Mitsubishi Heavy Industries (Japan)", "value": 8 }
      ],
      "command": { "app_command": 0, "args": ["provider"] }
    },
    {
      "type": "radioGroup",
      "title": "Show",
      "default": 0,
      "options": [
        { "label": "The Next Launch", "value": 0 },
        { "label": "The List", "value": 1 }
      ],
      "command": { "app_command": 1, "args": ["page"] }
    },
    { "type": "switch", "title": "Count Down Out Loud", "default": 1, "command": { "app_command": 2, "args": ["sound"] } },
    { "type": "borderButton", "label": "CHECK AGAIN NOW", "command": { "app_command": 3 } },
    {
      "type": "richTextBlock",
      "title": "Good to Know",
      "blocks": [
        { "type": "bullet", "text": "GO: the time is confirmed. TBC: it is expected but not yet confirmed. TBD: the date is not yet known, so Pixlpal shows the earliest it can be (NET, no earlier than)." },
        { "type": "bullet", "text": "Launches move often, even in the last minutes. Pixlpal checks every 15 minutes, and every 5 in the last hour before a launch." },
        { "type": "note", "text": "Data from The Space Devs' Launch Library 2 (thespacedevs.com), free for everyone. Turn the knob for the list." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3 }, "text": "Checking" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Open Space Launches on Pixlpal first" }
  ]
})json";

#endif
