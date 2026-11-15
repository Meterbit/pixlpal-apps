/**
 * @file tmw_Mobile_Ui.h
 * @brief Team Around the World's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (teamWorld.cpp):
 *   0 {"person0" ... "person7": "Ada, Lagos"}   a teammate and their city (empty: removed); one per call
 *   1 {"from": "09:00", "to": "17:00"}           working hours, in each person's own time
 *   2 {}                                          fetch the public holidays again
 */
#ifndef TMW_MOBILE_UI_H
#define TMW_MOBILE_UI_H

static const char TMW_UI_MANIFEST[] = R"json({
  "name": "Team Around the World",
  "about": "For teams across time zones: each teammate's local time and whether they are working, off for the evening, asleep, on a weekend or a public holiday, the hours when everyone is at work, and each person's next holiday.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TMW_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.team-world.png", "height": 64, "alt": "Team Around the World" },
    { "type": "statusPanel", "title": "Together", "source": "together" },
    { "type": "statusPanel", "title": "The Team Now", "source": "team", "emptyText": "No teammates yet. Add them below." },
    {
      "type": "section",
      "id": "people",
      "title": "Teammates",
      "widgets": [
        { "type": "textField", "title": "Person 1", "emptyHint": "Name, city: Ada, Lagos", "filledHint": "Change person 1 (empty removes them)", "command": { "app_command": 0, "args": ["person0"] } },
        { "type": "textField", "title": "Person 2", "emptyHint": "Name, city", "filledHint": "Change person 2", "command": { "app_command": 0, "args": ["person1"] } },
        { "type": "textField", "title": "Person 3", "emptyHint": "Name, city", "filledHint": "Change person 3", "command": { "app_command": 0, "args": ["person2"] } },
        { "type": "textField", "title": "Person 4", "emptyHint": "Name, city", "filledHint": "Change person 4", "command": { "app_command": 0, "args": ["person3"] } },
        { "type": "textField", "title": "Person 5", "emptyHint": "Name, city", "filledHint": "Change person 5", "command": { "app_command": 0, "args": ["person4"] } },
        { "type": "textField", "title": "Person 6", "emptyHint": "Name, city", "filledHint": "Change person 6", "command": { "app_command": 0, "args": ["person5"] } },
        { "type": "textField", "title": "Person 7", "emptyHint": "Name, city", "filledHint": "Change person 7", "command": { "app_command": 0, "args": ["person6"] } },
        { "type": "textField", "title": "Person 8", "emptyHint": "Name, city", "filledHint": "Change person 8", "command": { "app_command": 0, "args": ["person7"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "note", "text": "The city sets the person's time zone and country: write a large city near them if theirs isn't found (Pixlpal knows 172). Add the country when a name is in two: Ada, Portland, United States." }
        ] }
      ]
    },
    { "type": "timeRange", "title": "Working Hours (Each in Their Own Time)", "mode": "range", "defaultFrom": "09:00", "defaultTo": "17:00", "fromLabel": "From", "toLabel": "To", "command": { "app_command": 1, "args": ["from", "to"] } },
    { "type": "borderButton", "label": "FETCH THE HOLIDAYS AGAIN", "command": { "app_command": 2 } },
    {
      "type": "richTextBlock",
      "title": "How It Reads",
      "blocks": [
        { "type": "bullet", "text": "Green: working. Amber: the morning before work or the evening after. Dark blue: asleep (10 pm to 7 am). Grey: their weekend. Purple: a public holiday where they are." },
        { "type": "bullet", "text": "Weekends follow each country: Saturday and Sunday, or Friday and Saturday where that is the custom." },
        { "type": "bullet", "text": "On Pixlpal, turn the knob to see one person with their next holidays." },
        { "type": "note", "text": "National holidays come from Nager.Date, which covers about 120 countries." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Teammate saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write a name and a city: Ada, Lagos. That city wasn't found: try a larger one near them" },
    { "when": { "app_command": 1 }, "text": "Working hours saved" },
    { "when": { "app_command": 2 }, "text": "Fetching the holidays" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Open Team Around the World on Pixlpal first" }
  ]
})json";

#endif
