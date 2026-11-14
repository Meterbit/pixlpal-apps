/**
 * @file trh_Model.h
 * @brief Treasure Hunt {19,3} (KIDS, ages 5-12): a hunt round the house or garden. A grown-up sets the clues on the
 *        phone (or picks a ready-made hunt) and hides a card with a 3-digit code where each clue leads. Pixlpal shows
 *        a clue; the child finds the card and turns the knob to enter its code, which opens the next clue, and the
 *        last one the treasure. Without codes, the grown-up moves the hunt on from the phone. No Arduino: tested on a PC.
 */
#ifndef TRH_MODEL_H
#define TRH_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define TRH_NVS_KEY         "trhSet"
#define TRH_SETTINGS_MAGIC  0x54524801u
#define TRH_CLUES           10
#define TRH_TEXT            80
#define TRH_HINT            48
#define TRH_PRESETS         3

struct Trh_Clue_t {
    char text[TRH_TEXT];
    char hint[TRH_HINT];
    uint16_t code;                      /**< 100-999: on the card where this clue leads */
};

struct Trh_Settings_t {
    uint32_t magic;
    uint8_t count;
    Trh_Clue_t clues[TRH_CLUES];
    uint8_t useCodes;                   /**< 0: the grown-up moves the hunt on from the phone */
    uint8_t showTimer;
    char prize[48];                     /**< the treasure screen's words: where the treasure is */
    uint16_t bestSeconds;
    uint32_t seed;
};

void trh_Settings_Default(Trh_Settings_t& s);
void trh_Settings_Clamp(Trh_Settings_t& s);
/** @brief "Look where we keep the cold food | It hums in the kitchen": a clue, with its hint after a '|'. */
bool trh_Add_Clue(Trh_Settings_t& s, const char* text);
/** @brief New codes for every clue, all different. */
void trh_New_Codes(Trh_Settings_t& s);
/** @brief A ready-made hunt: 0 at home, 1 in the garden, 2 for a party. */
bool trh_Preset(Trh_Settings_t& s, uint8_t which);
const char* trh_Preset_Name(uint8_t which);
/** @brief "4:05" */
void trh_Time_Text(char* dest, size_t size, uint32_t seconds);

#endif
