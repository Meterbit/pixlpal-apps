/**
 * @file fh_Symbol.h
 * @brief Stock symbols as the phone sends them ("aapl", "BRK.B"). No Arduino, so it can be tested on a PC.
 *        The list of stocks to search is on the phone (assets/stocks_us.json), not in the firmware.
 */
#ifndef FH_SYMBOL_H
#define FH_SYMBOL_H

#include <stddef.h>

/** @brief 1-11 characters of A-Z, 0-9, '.', '-', '^' (upper case after conversion). */
bool fh_Symbol_Valid(const char* symbol);
/** @brief Copies @p text upper-cased into @p dest; false if it is not a valid symbol. */
bool fh_Symbol_Normalise(const char* text, char* dest, size_t size);

#endif
