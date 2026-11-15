/**
 * @file fbl_Ui.h
 * @brief Drawing of the Live Football screens (256x128). Draws from an Fbl_Data_t only.
 *
 * The 10 rows above y = 10 belong to the status bar. Every screen has a title band (y 10-19) and a footer (y 119-127).
 *   Live       a match in play on a mown pitch: crests, the score in big digits, the minute, a 90-minute timeline
 *              with goals and cards, the scorers
 *   Board      a day's matches by league: kick-off, minute or result, favourites marked
 *   Countdown  the favourite team's next match, counting down to kick-off
 *   Table      the league table with form (paid plans); Scorers: the top scorers (paid plans)
 *   Goal       a goal celebration; Banner: red card, kick-off, full time
 *   Message    what to do: no key, key rejected, quota used, no matches
 */
#ifndef FBL_UI_H
#define FBL_UI_H

#include "fbl_Model.h"
#include "fbl_Service.h"
#include <time.h>

enum Fbl_Screen_t : uint8_t {
    FBL_SCREEN_LIVE = 0, FBL_SCREEN_BOARD, FBL_SCREEN_COUNTDOWN, FBL_SCREEN_TABLE, FBL_SCREEN_SCORERS,
    FBL_SCREEN_GOAL, FBL_SCREEN_BANNER, FBL_SCREEN_MESSAGE,
};

enum Fbl_Board_t : uint8_t { FBL_BOARD_YESTERDAY = 0, FBL_BOARD_TODAY, FBL_BOARD_TOMORROW, FBL_BOARD_AHEAD };
enum Fbl_Banner_t : uint8_t { FBL_BANNER_RED = 0, FBL_BANNER_KICKOFF, FBL_BANNER_FULLTIME };

/** @brief The matches of one board, in the order drawn (indexes into data->matches). */
struct Fbl_Board_List_t {
    uint8_t count;
    uint8_t index[FBL_MAX_MATCHES];
};

struct Fbl_View_t {
    const Fbl_Data_t* data;
    const Fbl_Settings_t* settings;
    const Fbl_Status_t* status;
    uint8_t screen;             /**< Fbl_Screen_t */
    int match;                  /**< Live, countdown, goal, banner: index into data->matches. */
    uint8_t board;              /**< Fbl_Board_t */
    uint8_t page;               /**< Board, table and scorer pages. */
    uint8_t liveIndex, liveCount;   /**< "2/3 LIVE". */
    uint8_t goalSide;           /**< Goal: 0 home, 1 away. */
    uint8_t banner;             /**< Fbl_Banner_t */
    const char* message;        /**< Message screen: title. */
    const char* detail;         /**< ...and text. */
    uint32_t now;
    uint32_t frame;             /**< Animation frames since the screen came up. */
    bool clockValid;
};

/** @brief The matches of a board: those whose local kick-off day is that board's day (or later, for "ahead"). */
void fbl_Ui_Board_List(const Fbl_View_t& view, uint8_t board, Fbl_Board_List_t& list);
/** @brief Pages a board takes. */
uint8_t fbl_Ui_Board_Pages(const Fbl_View_t& view, uint8_t board);
uint8_t fbl_Ui_Table_Pages(const Fbl_View_t& view);

void fbl_Ui_Draw(const Fbl_View_t& view);
/** @brief What moves: the live dot, the goal celebration, the countdown. About ten times a second. */
void fbl_Ui_Animate(const Fbl_View_t& view);

#endif
