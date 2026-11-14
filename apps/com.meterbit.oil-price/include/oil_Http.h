/**
 * @file oil_Http.h
 * @brief One HTTPS GET for the Oil price providers: verified TLS, the app's User-Agent, time-outs, a size cap,
 *        and the body handed to a streaming parser.
 */
#ifndef OIL_HTTP_H
#define OIL_HTTP_H

#include "oil_Provider.h"
#include "oil_Parse.h"

/** @brief Reads the body. Returns false if it was not in the expected format. */
typedef bool (*Oil_Body_Fn)(Oil_Byte_Source_t& body, void* context);

/**
 * @brief GET @p url and pass a 200 answer's body to @p onBody. The server must present a certificate that
 *        chains to the firmware's CA bundle.
 * @param label  What to call the request in the log. The URL itself is never logged, since it may hold a key.
 */
Oil_Fetch_Result_t oil_Http_Get(const char* url, const char* label, Oil_Body_Fn onBody, void* context);

#endif
