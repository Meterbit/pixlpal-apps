/**
 * @file clm_App.h
 * @brief Calm Corner's screens (clm_Ui.cpp) and what the app task (calmCorner.cpp) shows.
 */
#ifndef CLM_APP_H
#define CLM_APP_H

#include "clm_Model.h"
#include "kk_Kids.h"

enum Clm_Screen_t : uint8_t { CLM_SCR_PICK = 0, CLM_SCR_FEEL, CLM_SCR_ANSWER, CLM_SCR_BREATHE, CLM_SCR_GROUND, CLM_SCR_STRETCH, CLM_SCR_LIGHTS,
                              CLM_SCR_AFTER, CLM_SCR_THANKS };
#define CLM_CHOICES 5                   /**< the four activities and "I'm OK" */

struct Clm_View_t {
    uint8_t screen;
    const Clm_Settings_t* s;
    const Kk_Session_t* k;
    uint8_t pick;
    uint8_t feeling;                    /**< the one chosen or being chosen */
    uint8_t choice;                     /**< ANSWER: 0-4 */
    uint32_t ms;                        /**< since the activity began */
    int groundStep;
    uint8_t groundFound;
    int stretch;
};

void clm_Draw(const Clm_View_t& v);
/** @brief A face for a feeling, centred on (cx, cy). */
void clm_Face(int16_t cx, int16_t cy, int16_t r, uint8_t feeling, uint8_t level);

#endif
