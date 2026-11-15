/**
 * @file wfl_Ui.h
 * @brief World Flags' screen, the whole 256x128 panel: a band (what plays, and where in it), the flag (drawn by the
 *        app into the box this returns), and beside it the country's name, capital and continent; in the quiz, the
 *        question and a countdown until the answer. Without the details, the flag takes the whole panel.
 */
#ifndef WFL_UI_H
#define WFL_UI_H

#include "wfl_Model.h"
#include "mtb_kit_image.h"

struct Wfl_View_t {
    const Wfl_Settings_t* settings;
    const Wfl_Country_t* country;
    int position, total;            /**< Where in the playlist (1-based), for the band. */
    bool revealed;                  /**< The quiz's answer is shown. */
    float progress;                 /**< 0-1 of the flag's (or the question's) time. */
    uint8_t flagState;              /**< 0 loading, 1 ready, 2 could not be fetched. */
};

/** @brief Draws everything but the flag; returns the flag's box. */
Mtb_Kit_Box_t wfl_Draw(const Wfl_View_t& view);
/** @brief Only what moves with time: the band's progress bar and the quiz's countdown (the flag is left alone). */
void wfl_Draw_Progress(const Wfl_View_t& view);
/** @brief The words in the flag's box while it loads or when it cannot be fetched. */
void wfl_Draw_Flag_Placeholder(const Wfl_View_t& view, Mtb_Kit_Box_t box);

#endif
