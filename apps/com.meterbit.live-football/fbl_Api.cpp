#include "mtb_kit_body_reader.h"
#include "fbl_Api.h"
#include "fbl_Config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char TAG[] = "FBL_API";

class Fbl_Psram_Allocator_t : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void* pointer) override { heap_caps_free(pointer); }
    void* reallocate(void* pointer, size_t size) override { return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM); }
};

ArduinoJson::Allocator* fbl_Json_Allocator(void){
    static Fbl_Psram_Allocator_t allocator;
    return &allocator;
}

// The response reader is the kit's (mtb_kit_body_reader.h): one shared type, so one copy of the JSON parser.

Fbl_Response_t fbl_Api_Get(const char* path, const char* key, JsonDocument& filter, Fbl_Item_Fn onItem, void* context, const char* label){
    Fbl_Response_t response;
    memset(&response, 0, sizeof(response));
    response.result = FBL_NET;
    response.httpCode = -1;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", FBL_HOST, path);
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);
    http.setReuse(false);
    http.setUserAgent(FBL_USER_AGENT);
    http.setConnectTimeout(FBL_CONNECT_TIMEOUT_MS);
    http.setTimeout(FBL_READ_TIMEOUT_MS);
    static const char* headers[] = {"x-ratelimit-requests-limit", "x-ratelimit-requests-remaining"};
    http.collectHeaders(headers, 2);
    if (!http.begin(client, url)){
        ESP_LOGW(TAG, "%s: bad URL", label);
        return response;
    }
    http.addHeader("x-apisports-key", key);
    const int code = http.GET();
    response.httpCode = code;
    const String limit = http.header("x-ratelimit-requests-limit"), remaining = http.header("x-ratelimit-requests-remaining");
    if (limit.length() && remaining.length()){
        response.quotaSeen = true;
        response.limitDay = (uint16_t)limit.toInt();
        response.remaining = (uint16_t)remaining.toInt();
    }
    if (code != HTTP_CODE_OK){
        response.result = code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN ? FBL_KEY : code == HTTP_CODE_TOO_MANY_REQUESTS ? FBL_RATE : code > 0 ? FBL_SERVER : FBL_NET;
        http.end();
        ESP_LOGW(TAG, "%s: HTTP %d", label, code);
        return response;
    }

    Mtb_Kit_Body_Reader_t reader(http.getStream(), FBL_READ_TIMEOUT_MS, FBL_MAX_RESPONSE_BYTES);
    JsonDocument doc(fbl_Json_Allocator());
    uint8_t kind;
    const uint8_t walk = fbl_Walk(reader, doc, filter, onItem, context, kind, response.error, sizeof(response.error), response.items);
    static const uint8_t RESULT[] = {FBL_OK, FBL_PLAN, FBL_QUOTA, FBL_RATE, FBL_KEY, FBL_SERVER};
    response.result = walk == FBL_WALK_OK ? (uint8_t)FBL_OK : walk == FBL_WALK_API_ERROR ? RESULT[kind] : (uint8_t)FBL_PARSE;
    http.end();
    ESP_LOGI(TAG, "%s: %u items, result %d, %u of %u requests left%s%s", label, (unsigned)response.items, (int)response.result,
             (unsigned)response.remaining, (unsigned)response.limitDay, response.error[0] ? ": " : "", response.error);
    return response;
}

bool fbl_Api_Get_Image(const char* url, uint8_t** buffer, size_t* length, size_t maxLength){
    *buffer = nullptr;
    *length = 0;
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);
    http.setUserAgent(FBL_USER_AGENT);
    http.setConnectTimeout(FBL_CONNECT_TIMEOUT_MS);
    http.setTimeout(FBL_READ_TIMEOUT_MS);
    if (!http.begin(client, url)) return false;
    if (http.GET() != HTTP_CODE_OK){ http.end(); return false; }
    uint8_t* data = (uint8_t*)heap_caps_malloc(maxLength, MALLOC_CAP_SPIRAM);
    if (data == nullptr){ http.end(); return false; }
    Mtb_Kit_Body_Reader_t reader(http.getStream(), FBL_READ_TIMEOUT_MS, FBL_MAX_RESPONSE_BYTES);
    size_t used = 0;
    for (int c; used < maxLength && (c = reader.read()) >= 0; ) data[used++] = (uint8_t)c;
    http.end();
    if (used == 0 || used >= maxLength){ heap_caps_free(data); return false; }
    *buffer = data;
    *length = used;
    return true;
}
