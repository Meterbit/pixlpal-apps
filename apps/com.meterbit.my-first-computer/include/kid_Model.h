/**
 * @file kid_Model.h
 * @brief Kids' Learning & Pixel Games {19,0} (KIDS): children (up to three, by age), maths questions that follow how a child
 *        is doing, the times-tables race, spelling (built-in lists by age, or the school's own words), the reading
 *        timer, and what parents set: a daily time limit, bedtime, game time earned by learning. No network, no
 *        panel: tested on a PC.
 */
#ifndef KID_MODEL_H
#define KID_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define KID_SETTINGS_MAGIC  0x4B494431u   // "KID1"
#define KID_NVS_KEY         "kidSettings"
#define KID_CHILDREN        3
#define KID_MATH_LEVELS     10
#define KID_SPELL_LEVELS    5
#define KID_WORDS_LEN       200

enum Kid_Skill_t : uint8_t { KID_ADD = 0, KID_SUB, KID_MUL, KID_DIV, KID_SPELL, KID_SKILLS };

struct Kid_Child_t {
    uint8_t used;
    char name[16];
    uint8_t age;                        /**< 4-12 */
    uint8_t mathLevel, spellLevel;      /**< They move with how the child does */
    uint16_t stars;
    uint16_t bestRace;                  /**< Most right in a 60 s race */
    uint16_t gameSeconds;               /**< Game time earned and not yet played */
    // Today (reset each day).
    uint32_t day;                       /**< Days since 1970 (local) of the counts below */
    uint16_t todaySeconds;              /**< Learning and games together (the daily limit) */
    uint16_t todayGameSeconds, todayReadSeconds, extraSeconds;   /**< extra: given by a parent today */
    uint16_t todayRight, todayAsked;
    // All time.
    uint16_t right[KID_SKILLS], asked[KID_SKILLS];
    uint16_t streak;                    /**< Days in a row with some learning */
    uint32_t lastLearnDay;
    uint16_t readWeek[7];               /**< Minutes read, [0] today */
};

struct Kid_Settings_t {
    uint32_t magic;
    Kid_Child_t children[KID_CHILDREN];
    uint8_t active;                     /**< The child playing */
    uint8_t editing;                    /**< The child the phone edits */
    uint8_t limitMinutes;               /**< A day, learning and games; 0 no limit */
    uint8_t bedtimeOn;
    uint16_t bedFrom, bedTo;            /**< Minutes after midnight */
    uint8_t rewardMinutes;              /**< Game minutes for a good round (7 or more right of 10) */
    uint8_t maxGameMinutes;             /**< Game minutes a day at most */
    uint8_t readGoal;                   /**< Minutes a day */
    uint8_t speak, language, sound, easyAnswers;
    uint16_t tables;                    /**< Bits 2-12: the tables the race asks */
    char words[KID_WORDS_LEN];          /**< The school's spelling words, comma separated ("" the built-in lists) */
};

void kid_Settings_Default(Kid_Settings_t& s);
void kid_Settings_Clamp(Kid_Settings_t& s);
void kid_Child_Default(Kid_Child_t& c, const char* name, uint8_t age);
/** @brief The levels to start at for an age. */
uint8_t kid_Math_Level_For_Age(uint8_t age);
uint8_t kid_Spell_Level_For_Age(uint8_t age);
const char* kid_Language_Code(uint8_t i);

// A small random number generator (so questions can be tested).
uint32_t kid_Random(uint32_t& state);
int kid_Random_Range(uint32_t& state, int lo, int hi);      /**< lo..hi inclusive */

// Maths -----------------------------------------------------------------------------------------------------------------
struct Kid_Question_t {
    int a, b, answer;
    char op;                            /**< '+', '-', 'x', '/' */
    uint8_t skill;                      /**< Kid_Skill_t */
    bool choice;                        /**< Must be answered by choosing (the youngest, or a parent's choice) */
    int choices[3];                     /**< Always made: the knob chooses; a keyboard types */
    char text[24];                      /**< "7 x 8 =" */
};
/** @brief A question for a level (0 the youngest ... 9). @p easy: choices even at higher levels. */
void kid_Math_Question(uint8_t level, bool easy, uint32_t& rng, Kid_Question_t& q);
/** @brief A race question from the tables in @p tables (bits 2-12). */
void kid_Race_Question(uint16_t tables, uint32_t& rng, Kid_Question_t& q);
/** @brief Moves the level: up after 5 right in a row, down after 2 wrong in a row (the runs reset when it moves). */
uint8_t kid_Adapt(uint8_t level, uint8_t maxLevel, uint8_t& rightRun, uint8_t& wrongRun, bool right);

// Spelling -----------------------------------------------------------------------------------------------------------------
/** @brief The words to spell: the school's (if any), else the built-in list for the level. @return how many. */
int kid_Words(const Kid_Settings_t& s, uint8_t level, const char** out, int max, char* buffer, size_t bufferSize);
/** @brief Spelt right: letters only, any case. */
bool kid_Spelling_Right(const char* word, const char* typed);

// Time -----------------------------------------------------------------------------------------------------------------------
/** @brief A new day: today's counts start again (the streak and reading week move on). */
void kid_New_Day(Kid_Child_t& c, uint32_t today);
/** @brief Seconds left today (0 when the limit is reached; 0xFFFF no limit). */
uint16_t kid_Seconds_Left(const Kid_Settings_t& s, const Kid_Child_t& c);
bool kid_Bedtime(const Kid_Settings_t& s, uint16_t minute);
/** @brief One second spent: learning, reading or a game. */
void kid_Spend_Second(Kid_Child_t& c, bool game, bool reading);
/** @brief A round finished: stars and, for a good round, game time (within the day's games limit). @return seconds of game time given. */
uint16_t kid_Round_Done(const Kid_Settings_t& s, Kid_Child_t& c, uint8_t skill, int right, int asked, uint32_t today);
/** @brief Reading done: stars for each 5 minutes, the week's minutes. */
uint16_t kid_Reading_Done(Kid_Child_t& c, uint16_t seconds, uint32_t today);

#endif
