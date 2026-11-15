// Pixlpal compat: the firmware's HTTP helper on pxl_http_get / pxl_http_post. It waits for the answer (the app's
// loop pauses; input and commands keep arriving). Only the hosts in app.json's "net"; GET and POST only; response
// headers are not collected (result.header stays empty). A request that didn't start has its reason as the code
// (PXL_HTTP_NOT_ALLOWED: a host the app may not reach, such as a site its owner hasn't allowed yet).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>
#define MTB_KIT_HTTP_HEADERS    4
struct Mtb_Kit_Http_t {
    int code;
    size_t length;
    bool truncated;
    char header[MTB_KIT_HTTP_HEADERS][64];
};
char* mtb_Kit_Http_Get(const char* url, const char* const* requestHeaders, const char* const* collect, size_t collectCount,
                       size_t maxBytes, Mtb_Kit_Http_t& result, const char* userAgent);
char* mtb_Kit_Http_Send(const char* method, const char* url, const char* const* requestHeaders, const void* requestBody,
                        size_t requestLength, const char* const* collect, size_t collectCount,
                        size_t maxBytes, Mtb_Kit_Http_t& result, const char* userAgent);
ArduinoJson::Allocator* mtb_Kit_Json_Allocator(void);
