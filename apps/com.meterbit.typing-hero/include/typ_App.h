/**
 * @file typ_App.h
 * @brief Typing Hero's screens (typ_Ui.cpp) and what the app task (typingHero.cpp) shows.
 */
#ifndef TYP_APP_H
#define TYP_APP_H

#include "typ_Model.h"
#include "kk_Kids.h"

enum Typ_Screen_t : uint8_t { TYP_SCR_PICK = 0, TYP_SCR_NO_KEYBOARD, TYP_SCR_MENU, TYP_SCR_LESSONS, TYP_SCR_LESSON, TYP_SCR_DONE,
                              TYP_SCR_FALL, TYP_SCR_RACE, TYP_SCR_STOP };
enum Typ_Menu_t : uint8_t { TYP_M_LESSONS = 0, TYP_M_FALL, TYP_M_RACE, TYP_MENU };

struct Typ_View_t {
    uint8_t screen;
    const Typ_Settings_t* s;
    const Kk_Session_t* k;
    uint8_t pick, stop, menu;
    int lesson;                         /**< chosen or played */
    uint8_t open;                       /**< lessons open to this child */
    // A lesson.
    const char* line;
    int at;                             /**< characters typed right */
    uint8_t lineNo, lines;
    bool missed;                        /**< the last key was wrong (a red flash) */
    int wpm, accuracy;
    // The result.
    const char* title;
    uint8_t stars;
    const char* note;
    uint32_t ms;
    // The games.
    const Typ_Fall_t* fall;
    const char* word;                   /**< the race: the word to type */
    const char* nextWords;
    const char* typed;
    uint32_t secondsLeft;
    uint16_t words;
};

void typ_Draw(const Typ_View_t& v);

#endif
