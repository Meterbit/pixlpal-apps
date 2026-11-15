#include "mtb_kit_oauth.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "mtb_engine.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"

#ifndef CONFIG_PXP_OAUTH_BROKER_URL
#define CONFIG_PXP_OAUTH_BROKER_URL ""
#endif

static const char TAG[] = "MTB_KIT_OAUTH";

#define OAUTH_MAGIC         0x4F41544Bu     // "OATK"
#define OAUTH_MARGIN_SEC    60
#define OAUTH_BODY_MAX      8192

// The stored form, an NVS blob of its own per account.
struct Mtb_Kit_OAuth_Record_t {
    uint32_t magic;
    uint16_t length;
    char token[MTB_KIT_OAUTH_REFRESH_MAX];
};

// Bumped by every Store/Forget, so an owner knows to reload.
static volatile uint32_t storeGeneration = 1;
static portMUX_TYPE storeLock = portMUX_INITIALIZER_UNLOCKED;

static void wipe_Free(char*& p, size_t size){
    if (p){ memset(p, 0, size); heap_caps_free(p); p = nullptr; }
}

uint32_t mtb_Kit_OAuth_Now(void){ return (uint32_t)(esp_timer_get_time() / 1000000LL); }

static bool read_Record(const char* key, Mtb_Kit_OAuth_Record_t& r){
    memset(&r, 0, sizeof(r));
    if (mtb_Read_Nvs_Struct(key, &r, sizeof(r)) != ESP_OK) return false;
    if (r.magic != OAUTH_MAGIC || r.length == 0 || r.length >= sizeof(r.token)) return false;
    r.token[r.length] = '\0';
    return strlen(r.token) == r.length;
}

bool mtb_Kit_OAuth_Store(const char* nvsKey, const char* refreshToken){
    if (nvsKey == nullptr || refreshToken == nullptr) return false;
    const size_t n = strlen(refreshToken);
    if (n < 8 || n >= MTB_KIT_OAUTH_REFRESH_MAX) return false;
    for (size_t i = 0; i < n; i++) if ((unsigned char)refreshToken[i] <= ' ' || (unsigned char)refreshToken[i] > '~') return false;
    Mtb_Kit_OAuth_Record_t* r = (Mtb_Kit_OAuth_Record_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_OAuth_Record_t), MALLOC_CAP_SPIRAM);
    if (r == nullptr) return false;
    r->magic = OAUTH_MAGIC;
    r->length = (uint16_t)n;
    memcpy(r->token, refreshToken, n);
    const bool ok = mtb_Write_Nvs_Struct(nvsKey, r, sizeof(*r)) == ESP_OK;
    memset(r, 0, sizeof(*r));
    heap_caps_free(r);
    portENTER_CRITICAL(&storeLock);
    storeGeneration = storeGeneration + 1;
    portEXIT_CRITICAL(&storeLock);
    return ok;
}

void mtb_Kit_OAuth_Forget(const char* nvsKey){
    Mtb_Kit_OAuth_Record_t* r = (Mtb_Kit_OAuth_Record_t*)heap_caps_calloc(1, sizeof(Mtb_Kit_OAuth_Record_t), MALLOC_CAP_SPIRAM);
    if (r){ mtb_Write_Nvs_Struct(nvsKey, r, sizeof(*r)); heap_caps_free(r); }
    portENTER_CRITICAL(&storeLock);
    storeGeneration = storeGeneration + 1;
    portEXIT_CRITICAL(&storeLock);
}

bool mtb_Kit_OAuth_Is_Stored(const char* nvsKey){
    Mtb_Kit_OAuth_Record_t* r = (Mtb_Kit_OAuth_Record_t*)heap_caps_malloc(sizeof(Mtb_Kit_OAuth_Record_t), MALLOC_CAP_SPIRAM);
    if (r == nullptr) return false;
    const bool ok = read_Record(nvsKey, *r);
    memset(r, 0, sizeof(*r));
    heap_caps_free(r);
    return ok;
}

static void load(Mtb_Kit_OAuth_t& o){
    portENTER_CRITICAL(&storeLock);
    o.generation = storeGeneration;
    portEXIT_CRITICAL(&storeLock);
    o.refresh[0] = '\0';
    o.access[0] = '\0';
    o.expiresAt = 0;
    o.retryAt = 0;
    o.failures = 0;
    o.error[0] = '\0';
    Mtb_Kit_OAuth_Record_t* r = (Mtb_Kit_OAuth_Record_t*)heap_caps_malloc(sizeof(Mtb_Kit_OAuth_Record_t), MALLOC_CAP_SPIRAM);
    if (r && read_Record(o.provider->nvsKey, *r)){
        memcpy(o.refresh, r->token, r->length + 1);
        o.state = MTB_OAUTH_RETRY;        // linked; the first Access refreshes
    } else o.state = MTB_OAUTH_UNLINKED;
    if (r){ memset(r, 0, sizeof(*r)); heap_caps_free(r); }
}

bool mtb_Kit_OAuth_Begin(Mtb_Kit_OAuth_t& o, const Mtb_Kit_OAuth_Provider_t& provider){
    memset(&o, 0, sizeof(o));
    o.provider = &provider;
    o.refresh = (char*)heap_caps_calloc(1, MTB_KIT_OAUTH_REFRESH_MAX, MALLOC_CAP_SPIRAM);
    o.access = (char*)heap_caps_calloc(1, MTB_KIT_OAUTH_ACCESS_MAX, MALLOC_CAP_SPIRAM);
    if (o.refresh == nullptr || o.access == nullptr){ mtb_Kit_OAuth_End(o); return false; }
    load(o);
    return true;
}

void mtb_Kit_OAuth_End(Mtb_Kit_OAuth_t& o){
    wipe_Free(o.refresh, MTB_KIT_OAUTH_REFRESH_MAX);
    wipe_Free(o.access, MTB_KIT_OAUTH_ACCESS_MAX);
    o.state = MTB_OAUTH_UNLINKED;
}

// "&name=value", the value percent-encoded.
static size_t add_Field(char* body, size_t at, size_t size, const char* name, const char* value){
    if (value == nullptr || at >= size) return at;
    at += snprintf(body + at, size - at, "%s%s=", at ? "&" : "", name);
    if (at >= size) return size;
    mtb_Kit_Url_Encode(body + at, size - at, value);
    return at + strlen(body + at);
}

const char* mtb_Kit_OAuth_Broker_Url(void){ return CONFIG_PXP_OAUTH_BROKER_URL; }

bool mtb_Kit_OAuth_Configured(const Mtb_Kit_OAuth_Provider_t& p){
    if (p.broker && p.broker[0]) return CONFIG_PXP_OAUTH_BROKER_URL[0] != '\0';
    return p.clientId && p.clientId[0];
}

static bool refresh_Now(Mtb_Kit_OAuth_t& o){
    const Mtb_Kit_OAuth_Provider_t& p = *o.provider;
    const bool viaBroker = p.broker && p.broker[0];
    if (!mtb_Kit_OAuth_Configured(p)){
        snprintf(o.error, sizeof(o.error), viaBroker ? "no account server" : "no client id");
        o.state = MTB_OAUTH_RELINK;
        return false;
    }
    // The account server adds the client ID and secret; the provider's own endpoint gets only public-client fields.
    char brokerUrl[192];
    const char* tokenUrl = p.tokenUrl;
    if (viaBroker){
        size_t base = strlen(CONFIG_PXP_OAUTH_BROKER_URL);
        while (base > 0 && CONFIG_PXP_OAUTH_BROKER_URL[base - 1] == '/') base--;
        snprintf(brokerUrl, sizeof(brokerUrl), "%.*s/v1/oauth/%s/refresh", (int)base, CONFIG_PXP_OAUTH_BROKER_URL, p.broker);
        tokenUrl = brokerUrl;
    }
    char* body = (char*)heap_caps_malloc(OAUTH_BODY_MAX, MALLOC_CAP_SPIRAM);
    char* rotated = (char*)heap_caps_malloc(MTB_KIT_OAUTH_REFRESH_MAX, MALLOC_CAP_SPIRAM);
    if (body == nullptr || rotated == nullptr){
        wipe_Free(body, OAUTH_BODY_MAX); wipe_Free(rotated, MTB_KIT_OAUTH_REFRESH_MAX);
        snprintf(o.error, sizeof(o.error), "out of memory");
        return false;
    }
    body[0] = '\0';
    size_t n = add_Field(body, 0, OAUTH_BODY_MAX, "grant_type", "refresh_token");
    n = add_Field(body, n, OAUTH_BODY_MAX, "refresh_token", o.refresh);
    if (!viaBroker) n = add_Field(body, n, OAUTH_BODY_MAX, "client_id", p.clientId);
    if (p.scope && p.scope[0]) n = add_Field(body, n, OAUTH_BODY_MAX, "scope", p.scope);
    // Provider quirks (Withings' "action=requesttoken") are the account server's job when it is used.
    if (!viaBroker && p.extraField && p.extraField[0] && n < OAUTH_BODY_MAX) n += snprintf(body + n, OAUTH_BODY_MAX - n, "&%s", p.extraField);
    if (n > OAUTH_BODY_MAX) n = OAUTH_BODY_MAX;
    bool ok = false;
    if (n < OAUTH_BODY_MAX - 1){
        static const char* const headers[] = {"Content-Type", "application/x-www-form-urlencoded", "Accept", "application/json", nullptr};
        Mtb_Kit_Http_t result;
        char* answerText = mtb_Kit_Http_Send("POST", tokenUrl, headers, body, n, nullptr, 0, 16 * 1024, result, p.userAgent);
        Mtb_Kit_OAuth_Answer_t answer;
        mtb_Kit_OAuth_Parse_Answer(result.code, answerText, answerText ? result.length : 0, o.access, MTB_KIT_OAUTH_ACCESS_MAX,
                                   rotated, MTB_KIT_OAUTH_REFRESH_MAX, answer);
        if (answerText){ memset(answerText, 0, result.length); heap_caps_free(answerText); }
        if (answer.ok){
            ok = true;
            o.expiresAt = mtb_Kit_OAuth_Now() + answer.expiresIn;
            o.state = MTB_OAUTH_OK;
            o.failures = 0;
            o.error[0] = '\0';
            if (answer.rotated && strcmp(rotated, o.refresh) != 0){
                // Keep the generation ours: our own save must not make us reload and drop the access token.
                const bool saved = mtb_Kit_OAuth_Store(p.nvsKey, rotated);
                portENTER_CRITICAL(&storeLock);
                o.generation = storeGeneration;
                portEXIT_CRITICAL(&storeLock);
                if (saved) snprintf(o.refresh, MTB_KIT_OAUTH_REFRESH_MAX, "%s", rotated);
                else ESP_LOGW(TAG, "%s: rotated refresh token could not be saved", p.nvsKey);
            }
        } else if (answer.refused){
            o.state = MTB_OAUTH_RELINK;
            snprintf(o.error, sizeof(o.error), "%s", answer.error);
            ESP_LOGW(TAG, "%s: link refused (%s)", p.nvsKey, answer.error);
        } else {
            o.state = MTB_OAUTH_RETRY;
            snprintf(o.error, sizeof(o.error), "%s", answer.error);
            ESP_LOGW(TAG, "%s: refresh failed (%s)", p.nvsKey, answer.error);
        }
    } else snprintf(o.error, sizeof(o.error), "token too long");
    memset(body, 0, OAUTH_BODY_MAX);
    heap_caps_free(body);
    wipe_Free(rotated, MTB_KIT_OAUTH_REFRESH_MAX);
    return ok;
}

const char* mtb_Kit_OAuth_Access(Mtb_Kit_OAuth_t& o){
    if (o.refresh == nullptr) return nullptr;
    uint32_t generation;
    portENTER_CRITICAL(&storeLock);
    generation = storeGeneration;
    portEXIT_CRITICAL(&storeLock);
    if (generation != o.generation) load(o);          // linked, relinked or unlinked from the phone
    if (o.state == MTB_OAUTH_UNLINKED || o.state == MTB_OAUTH_RELINK) return nullptr;
    const uint32_t now = mtb_Kit_OAuth_Now();
    if (o.state == MTB_OAUTH_OK && o.access[0] && now + OAUTH_MARGIN_SEC < o.expiresAt) return o.access;
    if (o.state == MTB_OAUTH_RETRY && o.retryAt && now < o.retryAt) return nullptr;
    if (refresh_Now(o)) return o.access;
    if (o.state == MTB_OAUTH_RETRY){
        static const uint16_t WAIT_S[] = {10, 30, 60, 120, 300, 600};
        const uint8_t i = o.failures < sizeof(WAIT_S) / sizeof(WAIT_S[0]) ? o.failures : sizeof(WAIT_S) / sizeof(WAIT_S[0]) - 1;
        o.retryAt = now + WAIT_S[i];
        if (o.failures < 250) o.failures++;
    }
    return nullptr;
}

void mtb_Kit_OAuth_Reject(Mtb_Kit_OAuth_t& o){
    if (o.access) o.access[0] = '\0';
    o.expiresAt = 0;
    if (o.state == MTB_OAUTH_OK){ o.state = MTB_OAUTH_RETRY; o.retryAt = 0; }
}

bool mtb_Kit_OAuth_Bearer(Mtb_Kit_OAuth_t& o, char* dest, size_t size){
    const char* token = mtb_Kit_OAuth_Access(o);
    if (token == nullptr || size < strlen(token) + 8) return false;
    snprintf(dest, size, "Bearer %s", token);
    return true;
}
