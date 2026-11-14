/**
 * @file opa_Api.h
 * @brief One HTTPS GET to OilPriceAPI: verified TLS, "Authorization: Token <key>", the app's User-Agent, ETags,
 *        the X-RateLimit-* quota headers, time-outs, a size cap, and the JSON body parsed through a filter.
 */
#ifndef OPA_API_H
#define OPA_API_H

#include "opa_Model.h"
#include <ArduinoJson.h>

enum Opa_Result_t : uint8_t {
    OPA_OK = 0,
    OPA_NOT_MODIFIED,       /**< 304: nothing new since the ETag sent. Does not count against the quota. */
    OPA_ERR_NO_KEY,         /**< Not sent: the request needs a key and none is set. */
    OPA_ERR_NETWORK,
    OPA_ERR_PARSE,
    OPA_ERR_AUTH,           /**< 401: missing or invalid key. Not retried until the key changes. */
    OPA_ERR_EMAIL,          /**< 403/429 EMAIL_CONFIRMATION_REQUIRED: the account's e-mail is not confirmed. */
    OPA_ERR_PLAN,           /**< 403: the plan does not include this endpoint or code. */
    OPA_ERR_QUOTA,          /**< 402, or 429 for a used-up quota: wait for the reset. */
    OPA_ERR_RATE,           /**< 429 per-minute limit: wait Retry-After. */
    OPA_ERR_BAD_CODE,       /**< 400 invalid_code: see Opa_Response_t::invalid. */
    OPA_ERR_SERVER,
};

struct Opa_Response_t {
    Opa_Result_t result;
    int httpCode;
    uint32_t retryAfterSec;         /**< From Retry-After, or the time to a quota reset; 0 if not given. */
    bool quotaSeen;                 /**< @p quota holds the X-RateLimit-* headers of this answer. */
    Opa_Account_t quota;
    char error[40];                 /**< The error code of an error answer. */
    char invalid[96];               /**< Codes a 400 invalid_code answer named, comma-separated. */
};

/**
 * @brief GET OPA_HOST + @p path and parse a 200 answer's body into @p doc through @p filter.
 * @param key       API key, or nullptr for the keyless demo endpoints.
 * @param etag      In: the ETag of the last answer to this path ("" for none). Out: the new one. May be nullptr.
 * @param label     What to call the request in the log. The key is never logged.
 */
Opa_Response_t opa_Api_Get(const char* path, const char* key, char* etag, size_t etagSize, JsonDocument& filter,
                           JsonDocument& doc, const char* label, uint32_t now);

/** @brief A JsonDocument allocator in PSRAM, so large answers leave internal RAM to TLS. */
ArduinoJson::Allocator* opa_Json_Allocator(void);

#endif
