#include "mtb_kit_body_reader.h"
#include "opa_Api.h"
#include "opa_Config.h"
#include "opa_Parse.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char TAG[] = "OPA_API";

class Opa_Psram_Allocator_t : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void* pointer) override { heap_caps_free(pointer); }
    void* reallocate(void* pointer, size_t size) override { return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM); }
};

ArduinoJson::Allocator* opa_Json_Allocator(void){
    static Opa_Psram_Allocator_t allocator;
    return &allocator;
}

// The response reader is the kit's (mtb_kit_body_reader.h): one shared type, so one copy of the JSON parser.

static uint32_t opa_Header_Number(HTTPClient& http, const char* name, bool& present){
    const String value = http.header(name);
    present = value.length() > 0;
    return present ? (uint32_t)strtoul(value.c_str(), nullptr, 10) : 0;
}

// The quota headers. Only quota answers carry them: a per-minute 429 uses the same names for its 60-second window.
static bool opa_Read_Quota(HTTPClient& http, uint32_t now, Opa_Account_t& quota){
    bool hasLimit, hasRemaining, hasReset;
    memset(&quota, 0, sizeof(quota));
    quota.limit = opa_Header_Number(http, "X-RateLimit-Limit", hasLimit);
    quota.remaining = opa_Header_Number(http, "X-RateLimit-Remaining", hasRemaining);
    const uint32_t reset = opa_Header_Number(http, "X-RateLimit-Reset", hasReset);
    if (!hasLimit || !hasRemaining || quota.limit == 0) return false;
    quota.seen = now;
    // A Unix time on quota windows; a short count of seconds would be the per-minute window.
    quota.reset = (hasReset && reset > 1600000000UL) ? reset : 0;
    const String window = http.header("X-RateLimit-Window");
    if (http.header("X-Demo-Mode") == "true") quota.window = OPA_QUOTA_DEMO;
    else if (window.indexOf("month") >= 0) quota.window = OPA_QUOTA_MONTH;
    else if (window.indexOf("trial") >= 0) quota.window = OPA_QUOTA_TRIAL;
    else if (window.indexOf("day") >= 0 || window.indexOf("daily") >= 0) quota.window = OPA_QUOTA_DAY;
    else quota.window = quota.limit <= OPA_FREE_LIMIT ? OPA_QUOTA_DAY : OPA_QUOTA_MONTH;
    String tier = http.header("X-RateLimit-Tier");
    if (tier.length() == 0) tier = http.header("X-RateLimit-State");
    snprintf(quota.tier, sizeof(quota.tier), "%s", tier.c_str());
    return true;
}

Opa_Response_t opa_Api_Get(const char* path, const char* key, char* etag, size_t etagSize, JsonDocument& filter,
                           JsonDocument& doc, const char* label, uint32_t now){
    Opa_Response_t response;
    memset(&response, 0, sizeof(response));
    response.result = OPA_ERR_NETWORK;
    response.httpCode = -1;

    char url[320];
    snprintf(url, sizeof(url), "%s%s", OPA_HOST, path);
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);       // No chunk markers, so the body can be parsed straight from the stream.
    http.setReuse(false);
    http.setUserAgent(OPA_USER_AGENT);
    http.setConnectTimeout(OPA_CONNECT_TIMEOUT_MS);
    http.setTimeout(OPA_READ_TIMEOUT_MS);
    static const char* headers[] = {"Retry-After", "X-RateLimit-Limit", "X-RateLimit-Remaining", "X-RateLimit-Reset",
                                    "X-RateLimit-Window", "X-RateLimit-Tier", "X-RateLimit-State", "X-Demo-Mode", "ETag"};
    http.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
    if (!http.begin(client, url)){
        ESP_LOGW(TAG, "%s: bad URL", label);
        return response;
    }
    http.addHeader("Accept", "application/json");
    if (key != nullptr && key[0] != '\0') http.addHeader("Authorization", String("Token ") + key);
    if (etag != nullptr && etag[0] != '\0') http.addHeader("If-None-Match", etag);

    const int code = http.GET();
    response.httpCode = code;
    bool present;
    const uint32_t retryAfter = opa_Header_Number(http, "Retry-After", present);
    response.retryAfterSec = retryAfter;

    if (code == HTTP_CODE_OK || code == HTTP_CODE_NOT_MODIFIED){
        response.quotaSeen = opa_Read_Quota(http, now, response.quota);
    }
    if (code == HTTP_CODE_OK){
        Mtb_Kit_Body_Reader_t body(http.getStream(), OPA_READ_TIMEOUT_MS, OPA_MAX_RESPONSE_BYTES);
        const DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
        response.result = error ? OPA_ERR_PARSE : OPA_OK;
        if (error) ESP_LOGW(TAG, "%s: %s", label, error.c_str());
        if (!error && etag != nullptr) snprintf(etag, etagSize, "%s", http.header("ETag").c_str());
    } else if (code == HTTP_CODE_NOT_MODIFIED){
        response.result = OPA_NOT_MODIFIED;
    } else if (code > 0){
        // Error answers are small; their code says what to do.
        JsonDocument errorFilter(opa_Json_Allocator());
        opa_Error_Filter(errorFilter);
        doc.clear();
        Mtb_Kit_Body_Reader_t body(http.getStream(), OPA_READ_TIMEOUT_MS, OPA_MAX_RESPONSE_BYTES);
        if (deserializeJson(doc, body, DeserializationOption::Filter(errorFilter)) == DeserializationError::Ok){
            opa_Parse_Error(doc.as<JsonVariantConst>(), response.error, sizeof(response.error), response.invalid, sizeof(response.invalid));
        }
        const bool emailUnconfirmed = strstr(response.error, "EMAIL_CONFIRMATION") != nullptr;
        if (code == HTTP_CODE_UNAUTHORIZED) response.result = OPA_ERR_AUTH;
        else if (emailUnconfirmed) response.result = OPA_ERR_EMAIL;
        else if (code == HTTP_CODE_FORBIDDEN) response.result = OPA_ERR_PLAN;
        else if (code == HTTP_CODE_PAYMENT_REQUIRED) response.result = OPA_ERR_QUOTA;
        else if (code == HTTP_CODE_TOO_MANY_REQUESTS){
            // A used-up quota (or trial) is also a 429 on paid plans; its Retry-After runs to the reset.
            const bool quota = strstr(response.error, "QUOTA") != nullptr || strstr(response.error, "TRIAL") != nullptr || retryAfter > 600;
            response.result = quota ? OPA_ERR_QUOTA : OPA_ERR_RATE;
        }
        else if (code == HTTP_CODE_BAD_REQUEST && strstr(response.error, "invalid_code") != nullptr) response.result = OPA_ERR_BAD_CODE;
        else response.result = OPA_ERR_SERVER;
        if (response.result == OPA_ERR_QUOTA){
            response.quotaSeen = opa_Read_Quota(http, now, response.quota);
            if (response.quotaSeen){
                response.quota.remaining = 0;
                response.quota.exhausted = 1;
                if (response.quota.reset > now && response.retryAfterSec == 0) response.retryAfterSec = response.quota.reset - now;
            }
        }
        doc.clear();
    }
    http.end();
    ESP_LOGI(TAG, "%s: HTTP %d, result %d%s%s", label, code, (int)response.result, response.error[0] ? ", " : "", response.error);
    return response;
}
