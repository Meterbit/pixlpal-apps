#include "oil_Http.h"
#include "oil_Config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_log.h"

static const char TAG[] = "OIL_HTTP";

// A TLS client's read() returns at once when nothing has arrived yet, which a parser would take for the end of the
// body, so this waits for data. It also stops at OIL_MAX_RESPONSE_BYTES.
class Oil_Stream_Source_t : public Oil_Byte_Source_t {
public:
    explicit Oil_Stream_Source_t(Client& stream) : stream(stream) {}

    int read(void) override {
        if (position >= length && !fill()) return -1;
        return buffer[position++];
    }

private:
    bool fill(void){
        if (total >= OIL_MAX_RESPONSE_BYTES) return false;
        const uint32_t start = millis();
        while (millis() - start < OIL_READ_TIMEOUT_MS){
            const int waiting = stream.available();
            if (waiting > 0){
                const int received = stream.read(buffer, waiting < (int)sizeof(buffer) ? waiting : (int)sizeof(buffer));
                if (received > 0){
                    position = 0;
                    length = (size_t)received;
                    total += (size_t)received;
                    return true;
                }
            } else if (!stream.connected()) return false;
            delay(5);
        }
        return false;
    }

    Client& stream;
    uint8_t buffer[512];
    size_t position = 0;
    size_t length = 0;
    size_t total = 0;
};

Oil_Fetch_Result_t oil_Http_Get(const char* url, const char* label, Oil_Body_Fn onBody, void* context){
    Oil_Fetch_Result_t outcome = {OIL_ERR_NETWORK, -1, 0};
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);       // No chunk markers, so the body can be parsed straight from the stream.
    http.setReuse(false);
    http.setUserAgent(OIL_USER_AGENT);
    http.setConnectTimeout(OIL_CONNECT_TIMEOUT_MS);
    http.setTimeout(OIL_READ_TIMEOUT_MS);
    static const char* headers[] = {"Retry-After"};
    http.collectHeaders(headers, 1);
    if (!http.begin(client, url)){
        ESP_LOGW(TAG, "%s: bad URL", label);
        return outcome;
    }
    const int code = http.GET();
    outcome.httpCode = code;
    if (code == HTTP_CODE_OK){
        Oil_Stream_Source_t body(http.getStream());
        outcome.result = onBody(body, context) ? OIL_OK : OIL_ERR_PARSE;
    } else if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN){
        outcome.result = OIL_ERR_AUTH;
    } else if (code == HTTP_CODE_TOO_MANY_REQUESTS || code == HTTP_CODE_SERVICE_UNAVAILABLE){
        outcome.result = OIL_ERR_RATE_LIMIT;
        const long retryAfter = http.header("Retry-After").toInt();     // Seconds; an HTTP date reads as 0.
        outcome.retryAfterSec = retryAfter > 0 ? (uint32_t)retryAfter : 0;
    } else if (code > 0){
        outcome.result = OIL_ERR_SERVER;
    }
    http.end();
    ESP_LOGI(TAG, "%s: HTTP %d, result %d", label, code, (int)outcome.result);
    return outcome;
}
