/**
 * @file kk_Kids.h
 * @brief What the KIDS apps share. The children are Kids Learning's: names, ages and stars, set from its phone page.
 *        So are the parent's rules: the daily time limit, bedtime, sounds on or off, and speaking. One set of rules
 *        covers every kids app.
 *
 *  - A session reads the children when an app opens. Time spent and stars earned are added to the child playing,
 *    about once a minute and when the app closes. Each save reads the stored settings first and adds to them, so a
 *    change a parent makes from the phone meanwhile is kept.
 *  - Screens shared by the kids apps: who is playing, "that's all for today", bedtime, a child's chip and stars, a
 *    shower of confetti.
 *  - Sounds: well done, try again, a click, a fanfare. They are silent when the parent switched sounds off.
 */
#ifndef KK_KIDS_H
#define KK_KIDS_H

#include <stdint.h>
#include <stddef.h>

#define KK_CHILDREN 3

enum Kk_Stop_t : uint8_t { KK_GO = 0, KK_TIME_UP, KK_BEDTIME };
enum Kk_Sound_t : uint8_t { KK_YAY = 0, KK_OOPS, KK_CLICK, KK_FANFARE, KK_LEVEL_UP, KK_SOFT };

struct Kk_Session_t {
    bool loaded;
    uint8_t count;                      /**< children set up */
    uint8_t child;                      /**< the one playing (0..count-1) */
    uint8_t real[KK_CHILDREN];          /**< each one's place in Kids Learning's list */
    char names[KK_CHILDREN][16];
    uint8_t ages[KK_CHILDREN];
    uint16_t stars[KK_CHILDREN];
    uint8_t sound, speak, language;     /**< the parent's choices (Kids Learning) */
    uint16_t left;                      /**< seconds left today, 0xFFFF no limit */
    uint16_t bedFrom, bedTo;
    uint8_t bedtimeOn;
    // Not yet saved.
    uint16_t pendingSeconds, pendingStars;
    uint32_t savedMs;
};

/** @brief Reads the children and rules. False if memory ran out (a session with one child, no limits, is set up). */
bool kk_Begin(Kk_Session_t& k);
/** @brief Saves what is pending (each app calls it when it closes). */
void kk_End(Kk_Session_t& k);
/** @brief Another child plays (what is pending goes to the one before). */
void kk_Select(Kk_Session_t& k, uint8_t child);
/** @brief One more second played, counted against the daily limit when @p counts; saves every minute. */
void kk_Tick(Kk_Session_t& k, bool counts);
void kk_Add_Stars(Kk_Session_t& k, uint16_t stars);
/** @brief Why play must stop now (the limit, bedtime), or KK_GO. @p minute: local minutes after midnight. */
uint8_t kk_Stop(const Kk_Session_t& k, uint16_t minute);
const char* kk_Name(const Kk_Session_t& k);
uint8_t kk_Age(const Kk_Session_t& k);
uint16_t kk_Stars(const Kk_Session_t& k);
/** @brief Local minutes after midnight, and days since 1970 (local). */
uint16_t kk_Local_Minute(uint32_t now);
uint32_t kk_Local_Day(uint32_t now);
/** @brief A sound, unless the parent turned sounds off. */
void kk_Sound(const Kk_Session_t& k, uint8_t sound);
/** @brief Words spoken (when the parent allows it), after an optional sound. */
void kk_Say(const Kk_Session_t& k, const char* text);

// Screens (256 x 118 from MTB_KIT_TOP, or the whole panel with @p top 0) -------------------------------------------
uint16_t kk_Child_Color(uint8_t child);
/** @brief The child's chip (top left) and stars (top right), with @p title between. */
void kk_Header(const Kk_Session_t& k, int16_t top, const char* title, uint16_t titleColor);
/** @brief A five-pointed star centred on (cx, cy). */
void kk_Star(int16_t cx, int16_t cy, int16_t r, uint16_t color);
/** @brief Confetti falling, @p ms since it began (draw it over a screen for about 2 s). */
void kk_Confetti(uint32_t ms, uint32_t seed, int16_t top);
/** @brief Who is playing: the children, @p selected highlighted. */
void kk_Pick_Screen(const Kk_Session_t& k, int16_t top, uint8_t selected);
/** @brief "That's all for today" or bedtime. */
void kk_Stop_Screen(const Kk_Session_t& k, int16_t top, uint8_t why);
/** @brief Big text, cut into lines that fit @p width, centred on @p cx from @p y. @return the y after it. */
int16_t kk_Words(const char* text, int16_t cx, int16_t y, int16_t width, const uint8_t* font, uint16_t color, int maxLines,
                 uint16_t background = 0);

#endif
