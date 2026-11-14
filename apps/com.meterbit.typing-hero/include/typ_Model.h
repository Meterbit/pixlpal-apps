/**
 * @file typ_Model.h
 * @brief Typing Hero {19,5} (KIDS, ages 7-12): touch typing on a USB keyboard plugged into Pixlpal. 13 lessons from
 *        the home row to sentences, each a line to type with the next key and its finger shown on a keyboard; a
 *        falling-letters game; a one-minute word race (words a minute and accuracy). No Arduino: tested on a PC.
 */
#ifndef TYP_MODEL_H
#define TYP_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define TYP_NVS_KEY         "typSet"
#define TYP_SETTINGS_MAGIC  0x54595001u
#define TYP_LESSONS         13
#define TYP_LINE            30
#define TYP_DROPS           8

enum Typ_Finger_t : uint8_t { TYP_L_PINKY = 0, TYP_L_RING, TYP_L_MIDDLE, TYP_L_INDEX, TYP_THUMB, TYP_R_INDEX, TYP_R_MIDDLE, TYP_R_RING, TYP_R_PINKY, TYP_FINGERS };

struct Typ_Settings_t {
    uint32_t magic;
    uint8_t lesson[3];                  /**< each child's (Kids Learning's place): the furthest open */
    uint8_t bestWpm[3];
    uint16_t bestFall[3];
    uint8_t accuracy[3];                /**< the last lesson's, percent */
    uint16_t lessonsDone[3];
    uint8_t keyboardMap;                /**< show the keyboard and fingers */
    uint8_t unlockAll;
    uint8_t passAccuracy;               /**< percent needed to open the next lesson */
};

struct Typ_Lesson_t { const char* name; const char* keys; const char* tip; };

/** A falling letter. */
struct Typ_Drop_t { char c; float x, y; bool live; };
struct Typ_Fall_t {
    Typ_Drop_t d[TYP_DROPS];
    float speed;                        /**< pixels a second */
    float spawnIn;                      /**< seconds to the next letter */
    uint16_t score;
    uint8_t lives;
    uint8_t zapped;                     /**< the last letter hit, for a flash (index + 1) */
    bool over;
};

void typ_Settings_Default(Typ_Settings_t& s);
void typ_Settings_Clamp(Typ_Settings_t& s);
const Typ_Lesson_t& typ_Lesson(int i);
/** @brief The finger for a character (shift's finger is not shown). */
uint8_t typ_Finger(char c);
const char* typ_Finger_Name(uint8_t f);
uint16_t typ_Finger_Color(uint8_t f);
/** @brief Where a key is on the drawn keyboard: row 0-3 (numbers, top, home, bottom), column. False if not drawn. */
bool typ_Key_Place(char c, int& row, int& col);
/** @brief A line of about TYP_LINE characters for a lesson: real words made of its keys, else made-up ones. */
void typ_Line(int lesson, uint32_t& rng, char* out, size_t size);
/** @brief A word for the race (lowercase, common). */
const char* typ_Race_Word(uint32_t& rng);
/** @brief Words a minute: (characters / 5) / minutes. */
int typ_Wpm(uint32_t chars, uint32_t ms);
void typ_Fall_Start(Typ_Fall_t& f);
/** @brief Moves the letters on by @p dt seconds; new ones from @p keys. @p height: where they land. */
void typ_Fall_Step(Typ_Fall_t& f, float dt, const char* keys, int width, int height, uint32_t& rng);
/** @brief A key pressed: zaps the lowest matching letter. True if one was hit. */
bool typ_Fall_Key(Typ_Fall_t& f, char c);

#endif
