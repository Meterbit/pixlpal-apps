/**
 * @file ttm_Model.h
 * @brief Tell the Time {19,1} (KIDS, ages 5-8): learning to read a clock. A big clock with the time in words and the
 *        family's day; "set the clock to half past 3" with the knob; "what time is it?" with three answers; "what
 *        time will it be in 20 minutes?". Six levels, from o'clock to any minute and time passing, moved by how the
 *        child does. No Arduino: tested on a PC.
 */
#ifndef TTM_MODEL_H
#define TTM_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define TTM_NVS_KEY         "ttmSet"
#define TTM_SETTINGS_MAGIC  0x54544D01u
#define TTM_EVENTS          6
#define TTM_LEVELS          6
#define TTM_ROUND           8

enum Ttm_Level_t : uint8_t { TTM_OCLOCK = 0, TTM_HALF, TTM_QUARTER, TTM_FIVE, TTM_MINUTE, TTM_LATER };
enum Ttm_Kind_t : uint8_t { TTM_SET = 0, TTM_READ, TTM_PASS, TTM_KINDS };

struct Ttm_Event_t { uint16_t minute; char label[20]; };

struct Ttm_Settings_t {
    uint32_t magic;
    uint8_t startLevel;                 /**< 0xFF: from the child's age */
    uint8_t kinds;                      /**< a bit per Ttm_Kind_t */
    uint8_t hour24;                     /**< the clock page's digital time */
    uint8_t words;                      /**< the time in words on the clock page */
    uint8_t level[3];                   /**< each child's, 0xFF not started */
    uint16_t rounds[3], right[3], asked[3];
    uint8_t eventCount;
    Ttm_Event_t events[TTM_EVENTS];     /**< the family's day, by time */
};

struct Ttm_Question_t {
    uint8_t kind;                       /**< Ttm_Kind_t */
    uint16_t target;                    /**< minutes after midnight, 0-719 (a 12-hour clock) */
    uint16_t shown;                     /**< READ and PASS: the clock shown */
    uint16_t later;                     /**< PASS: minutes added */
    uint16_t choices[3];
    char prompt[48];
    char choiceText[3][24];
};

void ttm_Settings_Default(Ttm_Settings_t& s);
void ttm_Settings_Clamp(Ttm_Settings_t& s);
uint8_t ttm_Level_For_Age(uint8_t age);
/** @brief "3 O'CLOCK", "HALF PAST 3", "QUARTER TO 4", "10 PAST 3", "25 TO 4", "17 MINUTES PAST 3", "12 MINUTES TO 4". */
void ttm_Words(char* dest, size_t size, uint16_t minute);
/** @brief "3:05" (12-hour, no AM/PM) or "15:05". */
void ttm_Digital(char* dest, size_t size, uint16_t minute, bool hour24);
/** @brief How far one knob step moves the minute hand at a level. */
uint16_t ttm_Step(uint8_t level);
void ttm_Question(uint8_t level, uint8_t kinds, uint32_t& rng, Ttm_Question_t& q);
/** @brief The minute hand at a time: set questions accept the right time on a 12-hour clock. */
bool ttm_Set_Right(const Ttm_Question_t& q, uint16_t set);
/** @brief "Look at the short hand: it shows the hour." */
const char* ttm_Hint(const Ttm_Question_t& q, uint16_t set);
/** @brief Adds an event "07:30 School bus" (sorted). False if it is not one or the day is full. */
bool ttm_Add_Event(Ttm_Settings_t& s, const char* text);
/** @brief The next event after @p minute today, -1 none. */
int ttm_Next_Event(const Ttm_Settings_t& s, uint16_t minute);

#endif
