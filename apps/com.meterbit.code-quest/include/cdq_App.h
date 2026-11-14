/**
 * @file cdq_App.h
 * @brief Code Quest's screens (cdq_Ui.cpp) and what the app task (codeQuest.cpp) shows.
 */
#ifndef CDQ_APP_H
#define CDQ_APP_H

#include "cdq_Model.h"
#include "kk_Kids.h"

enum Cdq_Screen_t : uint8_t { CDQ_SCR_PICK = 0, CDQ_SCR_LEVELS, CDQ_SCR_EDIT, CDQ_SCR_RUN, CDQ_SCR_RESULT, CDQ_SCR_STOP };
#define CDQ_PAL_DELETE  'x'             /**< the palette's "delete the block before the caret" */
#define CDQ_PAL_CLOSE   'c'

struct Cdq_View_t {
    uint8_t screen;
    const Cdq_Settings_t* s;
    const Kk_Session_t* k;
    uint8_t pick;
    uint8_t stop;
    int level;                          /**< chosen (levels) or played */
    const char* program;
    int caret;                          /**< 0..length: the gap; length + 1: RUN */
    bool palette;                       /**< the block palette is open */
    const char* items;                  /**< its items (blocks, then delete and close) */
    int itemCount, item;
    const Cdq_Run_t* run;
    int step;                           /**< RUN: the step shown */
    uint32_t ms;                        /**< since the result showed */
    const char* message;                /**< a short note under the map ("NO MORE BLOCKS") */
};

void cdq_Draw(const Cdq_View_t& v);

#endif
