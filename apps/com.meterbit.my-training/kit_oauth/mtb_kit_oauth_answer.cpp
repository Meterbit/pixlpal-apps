// The token endpoint's answer, apart from the device code so it can be tested on a PC.
#include "mtb_kit_oauth.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

static bool copy_Token(char* dest, size_t size, const char* text){
    if (dest == nullptr || size == 0) return false;
    dest[0] = '\0';
    if (text == nullptr) return false;
    const size_t n = strlen(text);
    if (n == 0 || n >= size) return false;
    memcpy(dest, text, n + 1);
    return true;
}

bool mtb_Kit_OAuth_Parse_Answer(int code, const char* body, size_t length, char* access, size_t accessSize,
                                char* rotated, size_t rotatedSize, Mtb_Kit_OAuth_Answer_t& answer){
    memset(&answer, 0, sizeof(answer));
    if (access && accessSize) access[0] = '\0';
    if (rotated && rotatedSize) rotated[0] = '\0';
    if (code <= 0){ snprintf(answer.error, sizeof(answer.error), "no answer (%d)", code); return false; }

    JsonDocument filter;
    filter["access_token"] = true;
    filter["refresh_token"] = true;
    filter["expires_in"] = true;
    filter["error"] = true;
    // Withings wraps its answer: {"status": 0, "body": {"access_token"...}} with HTTP 200 even for a refusal.
    filter["status"] = true;
    filter["body"]["access_token"] = true;
    filter["body"]["refresh_token"] = true;
    filter["body"]["expires_in"] = true;
    JsonDocument doc;
    const bool parsed = body && length && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    const bool wrapped = parsed && doc["status"].is<long>() && (doc["body"].is<JsonObjectConst>() || doc["access_token"].isNull());
    const long status = wrapped ? doc["status"].as<long>() : 0;
    JsonVariantConst tokens = wrapped ? doc["body"].as<JsonVariantConst>() : doc.as<JsonVariantConst>();
    const char* error = parsed ? doc["error"].as<const char*>() : nullptr;

    if (code == 200 && parsed && status == 0 && copy_Token(access, accessSize, tokens["access_token"].as<const char*>())){
        answer.ok = true;
        const long expires = tokens["expires_in"] | 3600L;
        answer.expiresIn = expires > 60 && expires < 86400L * 30 ? (uint32_t)expires : 3600;
        answer.rotated = copy_Token(rotated, rotatedSize, tokens["refresh_token"].as<const char*>());
        return true;
    }
    if (wrapped && status != 0){
        // Withings: 100-102, 200, 401 authentication failed, 214 unauthorized, 342/343 bad credentials or token,
        // 503 invalid parameters (a refresh token it no longer knows). 601 is its rate limit, 2554 a server error.
        const bool refused = (status >= 100 && status <= 102) || status == 200 || status == 214 || status == 342 || status == 343
                          || status == 401 || (status == 503 && error && strstr(error, "refresh"));
        answer.refused = refused;
        snprintf(answer.error, sizeof(answer.error), "%s (status %ld)", error ? error : "refused", status);
        return false;
    }
    // RFC 6749 5.2: a refresh token that is invalid, expired or revoked is "invalid_grant". A client the provider no
    // longer accepts cannot refresh either: both need a new link (and the second a fixed app registration).
    if (error && (strcmp(error, "invalid_grant") == 0 || strcmp(error, "invalid_client") == 0 ||
                  strcmp(error, "unauthorized_client") == 0 || strcmp(error, "interaction_required") == 0)){
        answer.refused = true;
        snprintf(answer.error, sizeof(answer.error), "%s", error);
        return false;
    }
    if (error) snprintf(answer.error, sizeof(answer.error), "%s (HTTP %d)", error, code);
    else snprintf(answer.error, sizeof(answer.error), "HTTP %d", code);
    return false;
}
