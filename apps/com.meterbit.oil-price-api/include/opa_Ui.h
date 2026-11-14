/**
 * @file opa_Ui.h
 * @brief Drawing of the Oil Price API screen (256x128). Draws from an Opa_Data_t only; knows nothing of the network.
 *
 * Screen layout (the 10 rows above y = 10 belong to the status bar):
 *   y  10-40   The featured code: label, 24-hour change, price, unit, name, source and time of the price
 *   y  43-106  Rotating page: chart, changes, board, heat map, futures curve, rigs and OPEC, plan and quota
 *   y 110-117  Watchlist strip, the featured code highlighted
 *   y 120-127  Ticker (a Mtb_ScrollText_t owned by the app task)
 */
#ifndef OPA_UI_H
#define OPA_UI_H

#include "opa_Model.h"
#include "opa_Service.h"

#define OPA_TICKER_Y        120

enum Opa_Page_t : uint8_t {
    OPA_PAGE_CHART = 0,
    OPA_PAGE_CHANGES,
    OPA_PAGE_BOARD,
    OPA_PAGE_HEAT,
    OPA_PAGE_CURVE,         /**< Professional plans and up. */
    OPA_PAGE_ENERGY,        /**< Scale plan: rig counts and OPEC output. */
    OPA_PAGE_PLAN,
    OPA_PAGE_COUNT
};

struct Opa_View_t {
    const Opa_Data_t* data;
    int selected;               /**< Index into data->quotes of the featured code. */
    uint8_t page;               /**< Opa_Page_t */
    uint8_t range;              /**< Opa_Range_t the user chose for the chart. */
    uint8_t share;              /**< Per cent of the quota this device may use. */
    bool stale;                 /**< The prices on screen are older than they should be: their time is drawn in amber. */
    const char* notice;         /**< Shown instead of a page that has no data yet, or nullptr. */
    const Opa_Status_t* status;
    uint32_t now;
};

/** @brief Whether a page has anything to show; pages without are skipped. */
bool opa_Ui_Page_Available(const Opa_View_t& view, uint8_t page);

void opa_Ui_Clear(void);
void opa_Ui_Draw_Hero(const Opa_View_t& view);
void opa_Ui_Draw_Page(const Opa_View_t& view);
void opa_Ui_Draw_Strip(const Opa_View_t& view);

/** @brief The plan's name for the screen: "FREE", "DEVELOPER", "TRIAL", "DEMO"... */
void opa_Ui_Plan_Name(char* dest, size_t size, const Opa_Account_t& account, bool demo);

#endif
