// Notifications to phones over the home Wi-Fi: ntfy, Telegram, WhatsApp (CallMeBot).
#include "ntf_Notify.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "NTF";

struct Message_t {
    char title[64];
    char text[320];
    uint8_t priority;
    uint8_t pending;                    // channels still to send (bits)
    uint8_t tries;
    uint32_t retryAt;                   // mono seconds
};

struct Notify_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    QueueHandle_t queue;
    volatile bool running, stop, changed;
    Ntf_Config_t* config;               // under lock
    Ntf_Status_t status;                // under lock
    uint32_t version;
    char userAgent[64], tags[48];
};
EXT_RAM_BSS_ATTR static Notify_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }
static uint32_t mono(void){ return millis() / 1000; }

const char* ntf_Channel_Name(uint8_t ch){
    static const char* const NAMES[NTF_CHANNELS] = {"ntfy", "Telegram", "WhatsApp"};
    return ch < NTF_CHANNELS ? NAMES[ch] : "";
}

bool ntf_Ready(const Ntf_Config_t& c, uint8_t ch){
    switch (ch){
        case NTF_NTFY: return c.on[ch] && c.ntfyTopic[0];
        case NTF_TELEGRAM: return c.on[ch] && c.telegramToken[0] && c.telegramChat[0];
        case NTF_WHATSAPP: return c.on[ch] && c.whatsappPhone[0] && c.whatsappKey[0];
        default: return false;
    }
}

static bool only(const char* t, const char* extra){
    for (; *t; t++) if (!isalnum((unsigned char)*t) && !strchr(extra, *t)) return false;
    return true;
}
bool ntf_Valid_Topic(const char* t){ return t && strlen(t) < 64 && only(t, "-_"); }
bool ntf_Valid_Server(const char* t){ return t && t[0] && strlen(t) < 64 && only(t, ".-:"); }
bool ntf_Valid_Chat(const char* t){
    if (!t || !t[0] || strlen(t) >= 32) return false;
    if (t[0] == '@') return only(t + 1, "_");                          // a public channel's name
    return only(t[0] == '-' ? t + 1 : t, "") && isdigit((unsigned char)t[t[0] == '-' ? 1 : 0]);
}
bool ntf_Valid_Phone(const char* t){
    if (!t || !t[0] || strlen(t) >= 24) return false;
    const char* p = t[0] == '+' ? t + 1 : t;
    if (strlen(p) < 6) return false;
    for (; *p; p++) if (!isdigit((unsigned char)*p)) return false;
    return true;
}

static void set_State(uint8_t ch, uint8_t state, const char* text){
    lock();
    N.status.state[ch] = state;
    snprintf(N.status.text[ch], sizeof(N.status.text[ch]), "%s", text ? text : "");
    N.version++;
    unlock();
}

static void http_Text(char* out, size_t size, const char* who, int code){
    if (code <= 0) snprintf(out, size, "Cannot reach %s", who);
    else if (code == 401 || code == 403) snprintf(out, size, "%s refused the key or token", who);
    else if (code == 429) snprintf(out, size, "%s: too many messages, trying later", who);
    else snprintf(out, size, "%s answered HTTP %d", who, code);
}

static bool send_Ntfy(const Ntf_Config_t& c, const Message_t& m, char* e, size_t es){
    char url[160], title[64], auth[80] = "";
    snprintf(url, sizeof(url), "https://%s/%s", c.ntfyServer[0] ? c.ntfyServer : "ntfy.sh", c.ntfyTopic);
    mtb_Kit_To_Ascii(title, sizeof(title), m.title);                // headers must be plain ASCII
    static const char* const PRIORITY[] = {"low", "default", "high", "urgent"};
    if (c.ntfyToken[0]) snprintf(auth, sizeof(auth), "Bearer %s", c.ntfyToken);
    const char* headers[] = {"Title", title, "Priority", PRIORITY[m.priority < 4 ? m.priority : 1], "Tags", N.tags,
                             "Content-Type", "text/plain; charset=utf-8", auth[0] ? "Authorization" : nullptr, auth, nullptr};
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Send("POST", url, headers, m.text, strlen(m.text), nullptr, 0, 2048, r, N.userAgent);
    heap_caps_free(body);
    memset(auth, 0, sizeof(auth));
    if (r.code == 200) return true;
    http_Text(e, es, "ntfy", r.code);
    return false;
}

static bool send_Telegram(const Ntf_Config_t& c, const Message_t& m, char* e, size_t es){
    char url[160];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", c.telegramToken);
    JsonDocument doc;
    doc["chat_id"] = c.telegramChat;
    char text[400];
    snprintf(text, sizeof(text), "%s\n%s", m.title, m.text);
    doc["text"] = text;
    doc["disable_notification"] = m.priority == NTF_LOW;
    char body[640];
    const size_t n = serializeJson(doc, body, sizeof(body));
    const char* headers[] = {"Content-Type", "application/json", nullptr};
    Mtb_Kit_Http_t r;
    char* answer = mtb_Kit_Http_Send("POST", url, headers, body, n, nullptr, 0, 4096, r, N.userAgent);
    memset(url, 0, sizeof(url));
    bool ok = false;
    JsonDocument a;
    if (answer && !deserializeJson(a, answer, r.length)) ok = a["ok"] | false;
    if (!ok){
        const char* d = a["description"] | "";
        if (r.code == 401 || r.code == 404) snprintf(e, es, "Telegram refused the bot token");
        else if (strstr(d, "chat not found")) snprintf(e, es, "Telegram: no such chat (send the bot a message first)");
        else if (d[0]) snprintf(e, es, "Telegram: %.60s", d);
        else http_Text(e, es, "Telegram", r.code);
    }
    heap_caps_free(answer);
    return ok;
}

static bool send_Whatsapp(const Ntf_Config_t& c, const Message_t& m, char* e, size_t es){
    char phone[48], text[1100], url[1300], both[400];
    mtb_Kit_Url_Encode(phone, sizeof(phone), c.whatsappPhone);
    snprintf(both, sizeof(both), "%s: %s", m.title, m.text);
    mtb_Kit_Url_Encode(text, sizeof(text), both);
    snprintf(url, sizeof(url), "https://api.callmebot.com/whatsapp.php?phone=%s&text=%s&apikey=%s", phone, text, c.whatsappKey);
    Mtb_Kit_Http_t r;
    char* answer = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 4096, r, N.userAgent);
    memset(url, 0, sizeof(url));
    // CallMeBot answers 200 with a page; its words tell whether it worked.
    const bool ok = r.code == 200 && answer && !strstr(answer, "APIKey is invalid") && !strstr(answer, "ERROR") && !strstr(answer, "Error");
    if (!ok){
        if (answer && strstr(answer, "APIKey")) snprintf(e, es, "CallMeBot refused the API key");
        else http_Text(e, es, "CallMeBot", r.code);
    }
    heap_caps_free(answer);
    return ok;
}

static bool send_On(uint8_t ch, const Ntf_Config_t& c, const Message_t& m, char* e, size_t es){
    switch (ch){
        case NTF_NTFY: return send_Ntfy(c, m, e, es);
        case NTF_TELEGRAM: return send_Telegram(c, m, e, es);
        case NTF_WHATSAPP: return send_Whatsapp(c, m, e, es);
        default: return false;
    }
}

static void worker(void*){
    Ntf_Config_t* c = (Ntf_Config_t*)heap_caps_malloc(sizeof(Ntf_Config_t), MALLOC_CAP_SPIRAM);
    Message_t* m = (Message_t*)heap_caps_malloc(sizeof(Message_t), MALLOC_CAP_SPIRAM);
    Message_t* retry = (Message_t*)heap_caps_calloc(1, sizeof(Message_t), MALLOC_CAP_SPIRAM);
    if (!c || !m || !retry) N.stop = true;
    while (!N.stop){
        if (N.changed){
            N.changed = false;
            lock();
            *c = *N.config;
            for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++){
                if (!ntf_Ready(*c, ch)) N.status.state[ch] = NTF_OFF;
                else if (N.status.state[ch] == NTF_OFF) N.status.state[ch] = NTF_READY;
            }
            N.version++;
            unlock();
        }
        bool have = false;
        if (retry->pending && (int32_t)(mono() - retry->retryAt) >= 0 && Mtb_Applications::internetConnectStatus){
            *m = *retry;
            retry->pending = 0;
            have = true;
        } else if (xQueueReceive(N.queue, m, pdMS_TO_TICKS(500)) == pdTRUE) have = true;
        if (!have) continue;
        lock();
        N.status.queued = (uint8_t)uxQueueMessagesWaiting(N.queue);
        unlock();
        uint8_t failed = 0;
        for (uint8_t ch = 0; ch < NTF_CHANNELS && !N.stop; ch++){
            if (!(m->pending & (1u << ch)) || !ntf_Ready(*c, ch)) continue;
            set_State(ch, NTF_SENDING, "Sending");
            char e[72] = "";
            if (send_On(ch, *c, *m, e, sizeof(e))){
                char t[72];
                const time_t now = time(nullptr);
                struct tm lt;
                localtime_r(&now, &lt);
                snprintf(t, sizeof(t), "Sent at %02d:%02d", lt.tm_hour, lt.tm_min);
                set_State(ch, NTF_OK, t);
                lock();
                N.status.sent++;
                unlock();
            } else {
                ESP_LOGW(TAG, "%s: %s", ntf_Channel_Name(ch), e);
                set_State(ch, NTF_FAILED, e);
                failed |= (uint8_t)(1u << ch);
            }
        }
        // Once more a minute later for the channels that failed (a newer failure replaces an older one).
        if (failed && m->tries == 0){
            *retry = *m;
            retry->pending = failed;
            retry->tries = 1;
            retry->retryAt = mono() + 60;
        }
    }
    if (c){ memset(c, 0, sizeof(*c)); heap_caps_free(c); }
    heap_caps_free(m);
    heap_caps_free(retry);
    N.running = false;
    vTaskDelete(NULL);
}

// An installed app's notification, sent now on each ready channel (the worker's job, without its minute-later retry).
static bool deliver_Now(Message_t* m){
    Ntf_Config_t* c = (Ntf_Config_t*)heap_caps_malloc(sizeof(Ntf_Config_t), MALLOC_CAP_SPIRAM);
    if (!c) return false;
    lock();
    *c = *N.config;
    unlock();
    bool any = false;
    for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++){
        if (!(m->pending & (1u << ch)) || !ntf_Ready(*c, ch)) continue;
        set_State(ch, NTF_SENDING, "Sending");
        char e[72] = "";
        if (send_On(ch, *c, *m, e, sizeof(e))){
            char t[72];
            const time_t now = time(nullptr);
            struct tm lt;
            localtime_r(&now, &lt);
            snprintf(t, sizeof(t), "Sent at %02d:%02d", lt.tm_hour, lt.tm_min);
            set_State(ch, NTF_OK, t);
            lock(); N.status.sent++; unlock();
            any = true;
        } else set_State(ch, NTF_FAILED, e);
    }
    memset(c, 0, sizeof(*c));
    heap_caps_free(c);
    return any;
}

bool ntf_Start(const Ntf_Config_t& config, const char* userAgent, const char* tags){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.queue = xQueueCreate(8, sizeof(Message_t));
        N.config = (Ntf_Config_t*)heap_caps_calloc(1, sizeof(Ntf_Config_t), MALLOC_CAP_SPIRAM);
        if (!N.lock || !N.queue || !N.config) return false;
        made = true;
    }
    for (int i = 0; i < 400 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (N.running) return false;
    xQueueReset(N.queue);
    lock();
    *N.config = config;
    memset(&N.status, 0, sizeof(N.status));
    snprintf(N.userAgent, sizeof(N.userAgent), "%s", userAgent ? userAgent : "PIXLPAL-L2/1.0");
    snprintf(N.tags, sizeof(N.tags), "%s", tags ? tags : "");
    N.version++;
    unlock();
    N.stop = false;
    N.changed = true;
    N.running = true;
    // An installed app has one thread: no worker; ntf_Send delivers at once (deliver_Now).
    return true;
}

void ntf_Stop(void){
    if (!N.running) return;
    N.stop = true;
    N.running = false;
    lock();
    memset(N.config, 0, sizeof(Ntf_Config_t));        // no tokens left in memory
    unlock();
}

void ntf_Set_Config(const Ntf_Config_t& config){
    if (!made) return;
    lock();
    *N.config = config;
    unlock();
    N.changed = true;
}

bool ntf_Send(const char* title, const char* text, uint8_t priority){
    if (!made || !N.running) return false;
    Message_t* m = (Message_t*)heap_caps_calloc(1, sizeof(Message_t), MALLOC_CAP_SPIRAM);
    if (!m) return false;
    snprintf(m->title, sizeof(m->title), "%s", title ? title : "");
    snprintf(m->text, sizeof(m->text), "%s", text ? text : "");
    m->priority = priority > NTF_URGENT ? NTF_NORMAL : priority;
    lock();
    for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++) if (ntf_Ready(*N.config, ch)) m->pending |= (uint8_t)(1u << ch);
    unlock();
    const bool ok = m->pending && deliver_Now(m);
    heap_caps_free(m);
    return ok;
}

bool ntf_Copy_Status(Ntf_Status_t& status, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ status = N.status; version = N.version; }
    unlock();
    return changed;
}
