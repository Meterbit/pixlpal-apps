/**
 * @file fbl_Api.h
 * @brief One HTTPS GET to API-Football: verified TLS, the x-apisports-key header, the daily quota headers, and the
 *        answer streamed: "errors" is read, then each element of "response" is parsed on its own through a filter,
 *        so a 3 MB day list needs a few kilobytes of RAM.
 */
#ifndef FBL_API_H
#define FBL_API_H

#include "fbl_Stream.h"

enum Fbl_Result_t : uint8_t {
    FBL_OK = 0, FBL_NET, FBL_PARSE, FBL_PLAN, FBL_QUOTA, FBL_RATE, FBL_KEY, FBL_SERVER,
};

struct Fbl_Response_t {
    uint8_t result;                 /**< Fbl_Result_t */
    int httpCode;
    bool quotaSeen;
    uint16_t limitDay;
    uint16_t remaining;
    uint16_t items;                 /**< Elements of "response" handed to the callback. */
    char error[96];
};

Fbl_Response_t fbl_Api_Get(const char* path, const char* key, JsonDocument& filter, Fbl_Item_Fn onItem, void* context, const char* label);

/** @brief GET a crest PNG into a PSRAM buffer (caller frees with heap_caps_free). Returns false on failure. */
bool fbl_Api_Get_Image(const char* url, uint8_t** buffer, size_t* length, size_t maxLength);

/** @brief A JsonDocument allocator in PSRAM. */
ArduinoJson::Allocator* fbl_Json_Allocator(void);

#endif
