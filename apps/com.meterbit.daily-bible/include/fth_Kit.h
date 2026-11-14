/**
 * @file fth_Kit.h
 * @brief What the FAITH apps share (Salah Times {13,0}, Daily Bible {13,1}, Catholic Daily {13,2}): a verse of the
 *        day, reminders at chosen times, the church year (Western or Orthodox Easter), clocks and countdowns. No
 *        network and no panel here: tested on a PC. The screens are in fth_Ui.h, the sounds in fth_Sound.h, the
 *        Bible fetch in fth_Net.h.
 */
#ifndef FTH_KIT_H
#define FTH_KIT_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define FTH_VERSE_LEN   1000

/** @brief A verse of the day, as fetched (text in UTF-8; the screens make it ASCII). */
struct Fth_Verse_t {
    uint32_t day;                       /**< Days since 1970 (local), 0 none */
    uint8_t edition;                    /**< The translation's index in the app's list */
    uint8_t pick;                       /**< 0 the day's verse, more after "another verse" */
    uint8_t source, reserved;
    char reference[48];                 /**< "John 3:16 (WEB)" */
    char text[FTH_VERSE_LEN];
    char extra[FTH_VERSE_LEN / 2];      /**< Shown first, dimmer: a transliteration, "" none */
};

struct Fth_Reminder_t {
    uint8_t on;
    uint8_t days;                       /**< 0 every day, 1 weekdays, 2 weekends, 3 Sundays, 4 Saturdays, 5 Fridays */
    uint16_t minute;                    /**< After midnight */
    char label[24];
};
#define FTH_DAYS_COUNT 6

struct Fth_Edition_t { const char* id; const char* name; };

void fth_Copy(char* dest, size_t size, const char* src);
/** @brief Whitespace made single spaces; cut at a whole character with "..." if too long. */
void fth_Clean_Text(char* out, size_t size, const char* in);

// Bible verses (bible-api.com: public domain translations) ------------------------------------------------------------
/** @brief "https://bible-api.com/John%203:16?translation=web" */
size_t fth_Bible_Url(const char* reference, const char* translation, char* out, size_t size);
bool fth_Parse_Bible(JsonDocument& doc, const char* translation, Fth_Verse_t& v);
/** @brief A day's verse from verses of encouragement (Protestant canon, Hebrew Psalm numbers). */
const char* fth_Bible_Reference(uint32_t day, uint8_t pick);
/** @brief A day's verse for the Douay-Rheims and the Vulgate: Vulgate Psalm numbers, Tobit, Wisdom, Sirach... */
const char* fth_Catholic_Reference(uint32_t day, uint8_t pick);
uint32_t fth_Pick(uint32_t day, uint8_t pick, uint32_t count, uint32_t stride);

// Reminders -------------------------------------------------------------------------------------------------------------
bool fth_Reminder_Today(const Fth_Reminder_t& r, int weekday /* Monday 0 */);
void fth_Reminder_Clamp(Fth_Reminder_t& r);
const char* fth_Days_Name(uint8_t days);                    /**< "every day"... */
/** @brief The next reminder after @p now: its index, and seconds to it (-1 none in two days). */
int fth_Next_Reminder(const Fth_Reminder_t* r, int count, uint32_t now, int32_t& secondsTo);

// Time ------------------------------------------------------------------------------------------------------------------
/** @brief "1:23:45", "23:45". */
void fth_Countdown(char* out, size_t size, int32_t seconds);
void fth_Clock(char* out, size_t size, uint32_t t, bool hour24);
void fth_Minute_Clock(char* out, size_t size, uint16_t minute, bool hour24);
/** @brief Days since 1970 of the local date of @p now, and its parts (weekday: Monday 0). */
uint32_t fth_Local_Day(uint32_t now, int* y, int* m, int* d, int* weekday);
void fth_Civil_From_Days(int32_t days, int& y, int& m, int& d);

// The church year -------------------------------------------------------------------------------------------------------
/** @brief Easter Sunday: Western (Gregorian computus), or Orthodox (Julian computus, as a Gregorian date, 1900-2099). */
void fth_Easter(int year, bool orthodox, int& month, int& day);

enum Fth_Season_t : uint8_t { FTH_ADVENT = 0, FTH_CHRISTMAS, FTH_ORDINARY, FTH_LENT, FTH_HOLY_WEEK, FTH_EASTER, FTH_SEASONS };
struct Fth_Church_Day_t {
    uint8_t season;                     /**< Fth_Season_t */
    char seasonName[24];                /**< "Advent", "Lent", "Easter", "Ordinary Time" ("Nativity Fast" for the Orthodox) */
    uint16_t color;                     /**< RGB565 of the season or the day */
    char colorName[8];                  /**< "violet", "white", "green", "red" */
    char feast[40];                     /**< Today's great day ("Good Friday"), "" none */
    char next[32];                      /**< The next great day */
    int nextDays;                       /**< Days to it */
};
void fth_Church_Day(int y, int m, int d, bool orthodox, Fth_Church_Day_t& out);
uint16_t fth_Liturgical_Color(const char* name);           /**< "violet", "rose", "white", "red", "green", "black" */

#endif
