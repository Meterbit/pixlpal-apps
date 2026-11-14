/**
 * @file clm_Model.h
 * @brief Calm Corner {19,2} (KIDS, ages 4-10): naming a feeling, then something that helps. Breathing with a growing
 *        and shrinking shape, "5 things I can see" grounding, a few slow stretches, or calm lights and soft notes.
 *        Afterwards: "how do you feel now?". The feelings are kept for parents (only on Pixlpal and their phone). It
 *        never counts towards the daily time limit, and works at bedtime. No Arduino: tested on a PC.
 */
#ifndef CLM_MODEL_H
#define CLM_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ntf_Notify.h"

#define CLM_NVS_KEY         "clmSet"
#define CLM_SETTINGS_MAGIC  0x434C4D01u
#define CLM_LOG             40

enum Clm_Feeling_t : uint8_t { CLM_HAPPY = 0, CLM_CALM, CLM_EXCITED, CLM_TIRED, CLM_SAD, CLM_WORRIED, CLM_ANGRY, CLM_FEELINGS };
enum Clm_Activity_t : uint8_t { CLM_BREATHE = 0, CLM_GROUND, CLM_STRETCH, CLM_LIGHTS, CLM_ACTIVITIES };
enum Clm_Breath_Style_t : uint8_t { CLM_BREATH_AUTO = 0, CLM_BREATH_BALLOON, CLM_BREATH_BOX, CLM_BREATH_STYLES };
enum Clm_Phase_t : uint8_t { CLM_IN = 0, CLM_HOLD_IN, CLM_OUT, CLM_HOLD_OUT, CLM_DONE };

struct Clm_Entry_t {
    uint32_t at;                        /**< Unix */
    uint8_t child;                      /**< Kids Learning's place of the child */
    uint8_t feeling;                    /**< Clm_Feeling_t */
    uint8_t after;                      /**< after the activity, 0xFF not asked */
    uint8_t activity;                   /**< Clm_Activity_t, 0xFF none */
};

struct Clm_Settings_t {
    uint32_t magic;
    uint8_t breathStyle;                /**< Clm_Breath_Style_t */
    uint8_t breaths;                    /**< 3-10 */
    uint8_t tones;                      /**< soft notes with the breathing and the lights */
    uint8_t lightsMinutes;              /**< 1-10 */
    uint8_t keepLog;
    uint8_t alertFeelings;              /**< a bit per Clm_Feeling_t: tell the parents */
    uint8_t phoneAlerts;
    Ntf_Config_t notify;
    uint8_t logCount, logHead;
    Clm_Entry_t log[CLM_LOG];           /**< a ring, logHead the next to write */
};

/** A breathing moment: what to do now, how far through it (0-1), which breath (from 1). */
struct Clm_Breath_t { uint8_t phase; float progress; uint8_t breath; uint8_t secondsLeft; float size; };

void clm_Settings_Default(Clm_Settings_t& s);
void clm_Settings_Clamp(Clm_Settings_t& s);
const char* clm_Feeling_Name(uint8_t f);        /**< "WORRIED" */
const char* clm_Feeling_Word(uint8_t f);        /**< "worried", for the phone */
uint16_t clm_Feeling_Color(uint8_t f);
/** @brief What to say to a feeling ("IT'S OK TO FEEL ANGRY. LET'S BREATHE."), and the activity to suggest. */
const char* clm_Answer(uint8_t f);
uint8_t clm_Suggest(uint8_t f);
const char* clm_Activity_Name(uint8_t a);
/** @brief Breathing seconds: in, hold, out, hold. Auto: a balloon (3 in, 4 out) to 7, box breathing (4 each) from 8. */
void clm_Breath_Pattern(uint8_t style, uint8_t age, uint8_t seconds[4]);
/** @brief Where the breathing is @p ms after it started. */
void clm_Breath_At(uint8_t style, uint8_t age, uint8_t breaths, uint32_t ms, Clm_Breath_t& out);
/** @brief The grounding steps for an age: "THINGS YOU CAN SEE" 5, HEAR 4, TOUCH 3, (from 7) SMELL 2, TASTE 1. */
int clm_Ground_Steps(uint8_t age);
const char* clm_Ground_Text(int step);
uint8_t clm_Ground_Count(int step);
/** @brief The stretches: their words, and how long each is held. */
#define CLM_STRETCHES 6
const char* clm_Stretch_Text(int i);
#define CLM_STRETCH_SECONDS 12
void clm_Log(Clm_Settings_t& s, const Clm_Entry_t& e);
/** @brief The newest @p i-th entry (0 the newest), false when there is none. */
bool clm_Log_Get(const Clm_Settings_t& s, int i, Clm_Entry_t& e);
/** @brief The pick of feelings a child had in the last @p days (counts per feeling). */
void clm_Log_Counts(const Clm_Settings_t& s, uint8_t child, uint32_t now, uint32_t days, uint16_t counts[CLM_FEELINGS]);

#endif
