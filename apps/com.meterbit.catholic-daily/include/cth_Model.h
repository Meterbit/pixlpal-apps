/**
 * @file cth_Model.h
 * @brief Catholic Daily {13,2} (FAITH): settings, the liturgical day (from the General Roman Calendar, or the season
 *        worked out here when offline), the Rosary's mysteries of the day, and the Angelus or Regina Caeli. No network,
 *        no panel: tested on a PC.
 */
#ifndef CTH_MODEL_H
#define CTH_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>
#include "fth_Kit.h"

#define CTH_SETTINGS_MAGIC  0x43544831u   // "CTH1"
#define CTH_NVS_KEY         "cthSettings"
#define CTH_VERSE_KEY       "cthVerse"
#define CTH_REMINDERS       3
#define CTH_CELEBRATIONS    3

enum Cth_Sound_t : uint8_t { CTH_SOUND_SILENT = 0, CTH_SOUND_CHIME, CTH_SOUND_BELL };
enum Cth_Rosary_t : uint8_t { CTH_JOYFUL = 0, CTH_SORROWFUL, CTH_GLORIOUS, CTH_LUMINOUS };

struct Cth_Settings_t {
    uint32_t magic;
    uint8_t translation;                /**< 0 Douay-Rheims (the only one for now) */
    uint8_t verse;                      /**< Show a verse each day */
    uint8_t language;                   /**< The calendar's titles: 0 English, 1 French, 2 Italian, 3 Latin */
    uint8_t hour24;
    uint8_t sound;                      /**< Cth_Sound_t, for the Angelus and reminders */
    uint8_t angelus;                    /**< The Angelus (Regina Caeli in Eastertide) at 6:00, 12:00 and 18:00 */
    uint8_t mercy;                      /**< The Hour of Mercy at 15:00 */
    uint8_t rosary;                     /**< The Rosary page */
    uint8_t reminderSlot;
    uint8_t reserved[3];
    Fth_Reminder_t reminders[CTH_REMINDERS];
};

struct Cth_Celebration_t { char title[72]; char colour[8]; char rank[28]; float rankNum; };

/** @brief The liturgical day. */
struct Cth_Day_t {
    uint32_t day;                       /**< Days since 1970 (local), 0 none */
    bool fromCalendar;                  /**< From the General Roman Calendar (else the season only, worked out here) */
    char season[12];                    /**< "ordinary", "advent", "christmas", "lent", "easter" */
    uint8_t week;
    uint8_t count;
    Cth_Celebration_t c[CTH_CELEBRATIONS];
};

void cth_Settings_Default(Cth_Settings_t& s);
void cth_Settings_Clamp(Cth_Settings_t& s);
int cth_Translations(void);
const Fth_Edition_t* cth_Translation(int i);
/** @brief The calendar's path for a day: "/api/v0/en/calendars/default/2026/11/8". */
size_t cth_Calendar_Path(uint8_t language, int y, int m, int d, char* out, size_t size);
/** @brief Reads the calendar's answer. */
bool cth_Parse_Day(JsonDocument& doc, uint32_t day, Cth_Day_t& out);
/** @brief The season (and the day's great feast) worked out here, for when the calendar cannot be reached. */
void cth_Fallback_Day(int y, int m, int d, uint32_t day, Cth_Day_t& out);
/** @brief "32nd week in Ordinary Time", "2nd week of Advent", "Christmas Time". */
void cth_Season_Text(const Cth_Day_t& d, char* out, size_t size);
/** @brief "SOLEMNITY", "FEAST", "MEMORIAL", "OPTIONAL MEMORIAL", "" (a weekday or a Sunday). */
void cth_Rank_Text(const Cth_Celebration_t& c, char* out, size_t size);
uint16_t cth_Colour(const Cth_Day_t& d);

/** @brief The day's mysteries: Joyful on Monday and Saturday, Sorrowful on Tuesday and Friday, Glorious on Wednesday and
 *         Sunday, Luminous on Thursday; on Sundays of Advent and Christmas the Joyful, of Lent the Sorrowful. */
uint8_t cth_Rosary(int weekday /* Monday 0 */, const char* season);
const char* cth_Rosary_Name(uint8_t set);               /**< "The Glorious Mysteries" */
const char* cth_Mystery(uint8_t set, int i);            /**< The i-th (0-4) mystery */
/** @brief "Regina Caeli" in Eastertide, else "The Angelus". */
const char* cth_Angelus_Name(const char* season);

#endif
