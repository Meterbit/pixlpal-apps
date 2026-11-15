/**
 * @file cc_Coin_Id.h
 * @brief CoinCap ids and the phone's coin values ("bitcoin|BTC"). No Arduino, so it can be tested on a PC.
 *        The list of coins to search is on the phone (assets/coins_coincap.json), not in the firmware.
 */
#ifndef CC_COIN_ID_H
#define CC_COIN_ID_H

#include <stddef.h>

/** @brief A coin as the phone picks it: CoinCap's id and the symbol shown. */
struct Cc_Coin_Ref_t {
    char id[40];                    /**< "binance-coin" */
    char symbol[12];                /**< "BNB" */
};

/** @brief CoinCap ids: 1-39 of a-z, 0-9 and '-'. */
bool cc_Id_Valid(const char* id);
/** @brief "bitcoin|BTC" into its id and symbol; false if not one. */
bool cc_Parse_Value(const char* value, char* id, size_t idSize, char* symbol, size_t symbolSize);

#endif
