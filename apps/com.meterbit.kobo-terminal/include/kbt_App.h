/**
 * @file kbt_App.h
 * @brief Kobo Terminal's worker (kbt_Net.cpp) and the app task (koboTerminal.cpp).
 */
#ifndef KBT_APP_H
#define KBT_APP_H

#include "kbt_Model.h"

struct Kbt_Data_t {
    Ngx_Board_t board;                  /**< The caller gives board.stocks (NGX_STOCKS_MAX) */
    Ngx_Table_t indices, tbills, bonds;
    char facts[6][40];
    int factsCount;
    char factsSymbol[16];
    uint8_t plan;                       /**< Kbt_Plan_t */
    uint8_t state;
    char text[96];
    uint32_t callsToday, dailyLimit;
    uint32_t nextRefresh;
    bool statusKnown, open;
    char statusText[48];
};

bool kbt_Net_Start(const Kbt_Settings_t& settings);
void kbt_Net_Stop(void);
void kbt_Net_Set_Settings(const Kbt_Settings_t& settings);
void kbt_Net_Refresh(void);
bool kbt_Net_Copy(Kbt_Data_t& data, uint32_t& version);

#endif
