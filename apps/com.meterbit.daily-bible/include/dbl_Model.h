/**
 * @file dbl_Model.h
 * @brief Daily Bible {13,1} (FAITH, for Protestant, Evangelical, Pentecostal, Orthodox and other Christians): settings,
 *        the translations, the reading plans and the reading streak. No network, no panel: tested on a PC.
 */
#ifndef DBL_MODEL_H
#define DBL_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "fth_Kit.h"

#define DBL_SETTINGS_MAGIC  0x44424C31u   // "DBL1"
#define DBL_NVS_KEY         "dblSettings"
#define DBL_VERSE_KEY       "dblVerse"
#define DBL_REMINDERS       4

enum Dbl_Plan_t : uint8_t { DBL_PLAN_OFF = 0, DBL_PLAN_BIBLE_YEAR, DBL_PLAN_NT, DBL_PLAN_PSALMS_PROVERBS, DBL_PLANS };

struct Dbl_Settings_t {
    uint32_t magic;
    uint8_t translation;                /**< Index in dbl_Translation */
    uint8_t verse;                      /**< Show the verse of the day */
    uint8_t plan;                       /**< Dbl_Plan_t */
    uint8_t orthodox;                   /**< The church year by the Orthodox Easter and Christmas (7 January) */
    uint8_t hour24, chime;              /**< chime: a sound with reminders */
    uint8_t reminderSlot, reserved;
    uint32_t planStart;                 /**< Days since 1970 the plan began, 0 the 1st of January */
    uint32_t lastRead;                  /**< The plan day (days since 1970) last read, 0 none */
    uint16_t streak;                    /**< Days in a row read */
    uint16_t reserved2;
    Fth_Reminder_t reminders[DBL_REMINDERS];
};

void dbl_Settings_Default(Dbl_Settings_t& s);
void dbl_Settings_Clamp(Dbl_Settings_t& s);
int dbl_Translations(void);
const Fth_Edition_t* dbl_Translation(int i);
const char* dbl_Plan_Name(uint8_t plan);

/** @brief Today's reading for a plan ("Genesis 1-3", "Genesis 50; Exodus 1-2", "Proverbs 8; Psalms 8, 38, 68, 98, 128"). */
void dbl_Reading(const Dbl_Settings_t& s, uint32_t today, int y, int m, int d, char* out, size_t size);
/** @brief Marks today read (or not): the streak counts days in a row. */
void dbl_Mark_Read(Dbl_Settings_t& s, uint32_t today, bool read);
bool dbl_Read_Today(const Dbl_Settings_t& s, uint32_t today);
/** @brief The streak as it stands today (broken if yesterday was missed). */
uint16_t dbl_Streak(const Dbl_Settings_t& s, uint32_t today);

#endif
