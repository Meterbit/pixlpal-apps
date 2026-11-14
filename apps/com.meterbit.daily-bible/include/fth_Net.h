/**
 * @file fth_Net.h
 * @brief Fetching for the FAITH apps (call from a worker task, never the app task: it waits on the network).
 */
#ifndef FTH_NET_H
#define FTH_NET_H

#include "fth_Kit.h"

/** @brief A verse from bible-api.com. @return true with @p out filled (day, edition and pick are the caller's). */
bool fth_Fetch_Bible(const char* reference, const char* translation, Fth_Verse_t& out, char* error, size_t errorSize);
/** @brief "Cannot reach X", "X is busy", "X answered HTTP 500". */
void fth_Http_Error(char* out, size_t size, const char* who, int code);

#endif
