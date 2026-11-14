/**
 * @file ngm_App.h
 * @brief NGX Market's worker (ngm_Net.cpp: asks NGN Market, paced to the plan's calls) and the app task (ngxMarket.cpp).
 */
#ifndef NGM_APP_H
#define NGM_APP_H

#include "ngm_Model.h"

/** @brief What NGN Market said, copied out of the worker under its lock. */
struct Ngm_Data_t {
    Ngx_Board_t board;                  /**< The caller gives board.stocks (NGX_STOCKS_MAX) */
    Ngx_Table_t indices, forex, dividends;
    float chart[NGX_CHART_MAX];
    int chartCount;
    char chartSymbol[16];
    Ngm_Meta_t meta;
    Ngm_Status_t status;
    uint8_t state;                      /**< Ngm_State_t of the last price refresh */
    char text[96];
    uint32_t nextRefresh;               /**< Unix */
    bool dividendsAllowed, chartAllowed;
};

bool ngm_Net_Start(const Ngm_Settings_t& settings);
void ngm_Net_Stop(void);
void ngm_Net_Set_Settings(const Ngm_Settings_t& settings);
void ngm_Net_Refresh(void);
/** @brief Copies everything (the stocks into @p data.board.stocks) if it changed since @p version. */
bool ngm_Net_Copy(Ngm_Data_t& data, uint32_t& version);

#endif
