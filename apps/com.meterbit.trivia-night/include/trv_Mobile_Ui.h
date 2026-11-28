/**
 * @file trv_Mobile_Ui.h
 * @brief Trivia Night's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (triviaNight.cpp):
 *   0 {"category": 0-24}     0 any, else Open Trivia DB's category number (9-32)
 *   1 {"difficulty": 0-3}    any, easy, medium, hard
 *   2 {"questions": 5-20}    questions a round
 *   3 {"names": "Ada, Ben"}  the players (1-4), comma separated; empty: one player
 *   4 {"timer": 0-60}        seconds to answer, 0 for no limit
 *   5 {"sound": 0|1}
 *   6 {}                     a new game now
 *   7 {"kind": 0-2}          any, four answers, true or false
 */
#ifndef TRV_MOBILE_UI_H
#define TRV_MOBILE_UI_H

static const char TRV_UI_MANIFEST[] = R"json({
  "name": "Trivia Night",
  "about": "A quiz on Pixlpal for one to four players: questions from the Open Trivia Database on films, science, history, sport and more. Turn the knob to choose an answer, click to lock it in, and see who wins the round.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char TRV_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.trivia-night.png", "height": 64, "alt": "Trivia Night" },
    { "type": "statusPanel", "title": "The Scores", "source": "scores", "emptyText": "Open Trivia Night on Pixlpal." },
    { "type": "borderButton", "label": "NEW GAME", "command": { "app_command": 6 }, "confirm": "Start a new game? The scores go back to zero." },
    { "type": "textField", "title": "Players", "emptyHint": "Names, comma separated: Ada, Ben, Chi", "filledHint": "Change the players (empty: one player)", "command": { "app_command": 3, "args": ["names"] } },
    {
      "type": "radioGroup",
      "title": "Questions About",
      "default": 0,
      "options": [
        { "label": "Anything", "value": 0 },
        { "label": "General knowledge", "value": 9 },
        { "label": "Books", "value": 10 },
        { "label": "Film", "value": 11 },
        { "label": "Music", "value": 12 },
        { "label": "Television", "value": 14 },
        { "label": "Video games", "value": 15 },
        { "label": "Science and nature", "value": 17 },
        { "label": "Computers", "value": 18 },
        { "label": "Mathematics", "value": 19 },
        { "label": "Mythology", "value": 20 },
        { "label": "Sports", "value": 21 },
        { "label": "Geography", "value": 22 },
        { "label": "History", "value": 23 },
        { "label": "Politics", "value": 24 },
        { "label": "Art", "value": 25 },
        { "label": "Animals", "value": 27 },
        { "label": "Vehicles", "value": 28 },
        { "label": "Comics", "value": 29 },
        { "label": "Gadgets", "value": 30 },
        { "label": "Anime and manga", "value": 31 },
        { "label": "Cartoons", "value": 32 }
      ],
      "command": { "app_command": 0, "args": ["category"] }
    },
    {
      "type": "radioGroup",
      "title": "How Hard",
      "default": 0,
      "options": [
        { "label": "A mix", "value": 0 },
        { "label": "Easy (1 point)", "value": 1 },
        { "label": "Medium (2 points)", "value": 2 },
        { "label": "Hard (3 points)", "value": 3 }
      ],
      "command": { "app_command": 1, "args": ["difficulty"] }
    },
    {
      "type": "radioGroup",
      "title": "Answers",
      "default": 0,
      "options": [
        { "label": "Both kinds", "value": 0 },
        { "label": "Four to choose from", "value": 1 },
        { "label": "True or false", "value": 2 }
      ],
      "command": { "app_command": 7, "args": ["kind"] }
    },
    { "type": "slider", "title": "Questions a Round", "min": 5, "max": 20, "step": 1, "default": 10, "command": { "app_command": 2, "args": ["questions"] } },
    { "type": "slider", "title": "Time to Answer (0: No Limit)", "min": 0, "max": 60, "step": 5, "default": 20, "suffix": " s", "command": { "app_command": 4, "args": ["timer"] } },
    { "type": "switch", "title": "Sounds", "default": 1, "command": { "app_command": 5, "args": ["sound"] } },
    {
      "type": "richTextBlock",
      "title": "How to Play",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to choose an answer, click to lock it in. With more than one player, each question is one player's, in turn: the name is at the top." },
        { "type": "bullet", "text": "Easy questions are worth 1 point, medium 2, hard 3. Three right in a row earns a bonus point." },
        { "type": "bullet", "text": "Changes to the questions start with the next round. A round's questions don't come back until all of them in that category have been asked." },
        { "type": "note", "text": "Questions from the Open Trivia Database (opentdb.com), under CC BY-SA 4.0." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 3 }, "text": "Players saved: a new game starts" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Up to four names, comma separated" },
    { "when": { "app_command": 6 }, "text": "New game" }
  ]
})json";

#endif
