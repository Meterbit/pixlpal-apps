/**
 * @file kid_Ui.h
 * @brief Kids' Learning's 256x128 screen (from y = 10): who is playing, the menu, maths, the times-tables race,
 *        spelling, the reading timer, the games, the star chart, a round's result, and "that's all for today".
 */
#ifndef KID_UI_H
#define KID_UI_H

#include "kid_Model.h"
#include "kid_Games.h"

enum Kid_Screen_t : uint8_t { KID_SCR_PICK = 0, KID_SCR_MENU, KID_SCR_MATH, KID_SCR_RACE, KID_SCR_SPELL, KID_SCR_READ, KID_SCR_GAMES, KID_SCR_CATCH, KID_SCR_CAVE, KID_SCR_STARS,
                              KID_SCR_DONE, KID_SCR_LOCKED };
enum Kid_Menu_t : uint8_t { KID_M_MATH = 0, KID_M_RACE, KID_M_SPELL, KID_M_READ, KID_M_GAMES, KID_M_STARS, KID_M_COUNT };
#define KID_ROUND 10                    /**< Maths questions a round */
#define KID_SPELL_ROUND 5
#define KID_RACE_SECONDS 60

/** @brief What is on screen and the round under way (the app task's). */
struct Kid_Play_t {
    uint8_t screen;
    int index;                          /**< The menu's (or the child list's, the games list's) selection */
    bool typing;                        /**< A keyboard: answers are typed */
    // Maths and the race.
    Kid_Question_t q;
    char entry[8];                      /**< Typed so far */
    int choice;                         /**< The choice selected */
    uint8_t number, right, rightRun, wrongRun;
    uint8_t feedback;                   /**< 0 none, 1 right, 2 wrong */
    uint32_t feedbackUntil;             /**< ms */
    uint32_t raceEnd;                   /**< Unix */
    // Spelling.
    const char* word;
    char typed[32];
    uint8_t letter;                     /**< 0-25: the letter the knob shows */
    uint8_t lookPhase;                  /**< 1 the word is shown to remember */
    uint32_t lookUntil;                 /**< ms */
    // Reading.
    uint32_t readSeconds;
    bool readPaused;
    // The games.
    Kid_Catch_t catcher;
    Kid_Cave_t cave;
    // A result, or why it stopped.
    char title[32], line1[48], line2[48];
};

struct Kid_View_t {
    const Kid_Settings_t* settings;
    const Kid_Play_t* play;
    uint32_t now;                       /**< Unix */
    uint32_t ms;
};

void kid_Draw(const Kid_View_t& v);
uint16_t kid_Child_Color(uint8_t child);
const char* kid_Menu_Name(uint8_t item);

#endif
