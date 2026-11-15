/**
 * @file fth_Sound.h
 * @brief The FAITH apps' sounds, played straight to the speakers (no player needed): a calm call of five notes, one
 *        soft note, and a church bell (the Angelus: three strokes, three, three, then nine).
 */
#ifndef FTH_SOUND_H
#define FTH_SOUND_H

#include <stdbool.h>

/** @brief Five rising and falling notes (@p soft: a single note). Blocks while it plays (2 s or less). */
void fth_Chime(bool soft);
/** @brief A bell: @p groups strokes groups ({3, 3, 3, 9} for the Angelus), a pause between groups. */
void fth_Bell(const int* groups, int count);
/** @brief The Angelus bell: 3, 3, 3, then 9. */
void fth_Angelus_Bell(void);

enum Fth_Sound_t { FTH_SOUND_SOFT = 1, FTH_SOUND_CHIME, FTH_SOUND_ANGELUS };
/** @brief Plays a sound on its own task (the app keeps drawing). False if one is already playing. */
bool fth_Sound_Play(int kind);
/** @brief Stops it (within a few milliseconds) and waits for it to end. Call before the app closes. */
void fth_Sound_Stop(void);
bool fth_Sound_Busy(void);

#endif
