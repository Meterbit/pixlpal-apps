// Busy Light's worker: the linked calendars every 2 minutes (the list of calendars every 6 hours) and Teams presence
// every 30 seconds, through meterbit_cal_kit's Google and Microsoft providers and accounts.
#include "bsy_App.h"
#include "mtb_engine.h"
#include "mtb_cal_engine.h"
#include "mtb_kit_oauth.h"
#include "gcl_Provider.h"
#include "olk_Provider.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "BSY_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Busy Light)"
#define CAL_EVERY       120u
#define LIST_EVERY      (6u * 3600u)
#define PRESENCE_EVERY  30u

ArduinoJson::Allocator* bsy_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

static const Mtb_Cal_Provider_t* const PROVIDERS[BSY_PROVIDERS] = {&GCL_PROVIDER, &OLK_PROVIDER};

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, sync;
    bool changed;
    Bsy_Settings_t* settings;
    Bsy_Data_t* data;
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

static void to_Event(const Mtb_Cal_Event_t& in, uint8_t provider, Bsy_Event_t& out){
    out.start = in.start;
    out.end = in.end;
    out.flags = in.flags;
    out.allDay = in.allDay;
    out.provider = provider;
    snprintf(out.title, sizeof(out.title), "%s", in.title);
}

static void worker(void*){
    Bsy_Settings_t* s = (Bsy_Settings_t*)heap_caps_malloc(sizeof(Bsy_Settings_t), MALLOC_CAP_SPIRAM);
    Bsy_Calendar_t* cal = (Bsy_Calendar_t*)heap_caps_calloc(1, sizeof(Bsy_Calendar_t), MALLOC_CAP_SPIRAM);
    Mtb_Cal_Data_t* tmp = (Mtb_Cal_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Cal_Data_t), MALLOC_CAP_SPIRAM);
    Mtb_Cal_Calendar_t* lists[BSY_PROVIDERS] = {};
    Mtb_Kit_OAuth_t* oauth = (Mtb_Kit_OAuth_t*)heap_caps_calloc(BSY_PROVIDERS, sizeof(Mtb_Kit_OAuth_t), MALLOC_CAP_SPIRAM);
    bool ok = s && cal && tmp && oauth;
    for (int p = 0; ok && p < BSY_PROVIDERS; p++){
        lists[p] = (Mtb_Cal_Calendar_t*)heap_caps_calloc(MTB_CAL_CALENDARS, sizeof(Mtb_Cal_Calendar_t), MALLOC_CAP_SPIRAM);
        ok = lists[p] && mtb_Kit_OAuth_Begin(oauth[p], *PROVIDERS[p]->oauth);
    }
    if (!ok) N.stop = true;
    int listCount[BSY_PROVIDERS] = {-1, -1};
    uint32_t listedAt[BSY_PROVIDERS] = {0, 0}, calDue = 0, presenceDue = 0;
    char account[BSY_PROVIDERS][64] = {};
    while (!N.stop){
        vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u || !Mtb_Applications::internetConnectStatus) continue;
        lock();
        *s = *N.settings;
        const bool sync = N.sync, changed = N.changed;
        N.sync = false;
        N.changed = false;
        unlock();
        if (sync || changed){ calDue = presenceDue = 0; }
        if (sync){ listCount[0] = listCount[1] = -1; account[0][0] = account[1][0] = '\0'; }

        // The calendars.
        if (now >= calDue){
            calDue = now + CAL_EVERY;
            memset(cal, 0, sizeof(*cal));
            bool anyOk = false, anyLinked = false;
            Bsy_Link_t links[BSY_PROVIDERS] = {};
            for (int p = 0; p < BSY_PROVIDERS; p++){
                const Mtb_Cal_Provider_t& pv = *PROVIDERS[p];
                Bsy_Link_t& link = links[p];
                if (!mtb_Kit_OAuth_Is_Stored(pv.oauth->nvsKey)){
                    link.state = MTB_OAUTH_UNLINKED;
                    listCount[p] = -1;
                    account[p][0] = '\0';
                    continue;
                }
                const bool wanted = s->useCalendar[p] || (p == BSY_MICROSOFT && s->usePresence);
                if (!wanted){ link.state = MTB_OAUTH_OK; snprintf(link.account, sizeof(link.account), "%s", account[p]); continue; }
                anyLinked = true;
                Mtb_Cal_Api_t api = {&oauth[p], UA, 0, ""};
                if (!account[p][0] && pv.account) pv.account(api, account[p], sizeof(account[p]));
                if (s->useCalendar[p]){
                    if (listCount[p] < 0 || now - listedAt[p] > LIST_EVERY){
                        listCount[p] = pv.calendars(api, lists[p], MTB_CAL_CALENDARS);
                        listedAt[p] = now;
                    }
                    if (listCount[p] >= 0){
                        memset(tmp, 0, sizeof(*tmp));
                        bool failed = false;
                        int used = 0;
                        for (int c = 0; c < listCount[p]; c++){
                            const Mtb_Cal_Calendar_t& k = lists[p][c];
                            if (k.holiday || !(k.primary || (s->allCalendars && k.shown))) continue;
                            if (pv.events(api, k, now - 6 * 3600, now + 30 * 3600, *tmp) < 0) failed = true;
                            else used++;
                        }
                        for (int i = 0; i < tmp->eventCount; i++){
                            Bsy_Event_t e;
                            to_Event(tmp->events[i], (uint8_t)p, e);
                            bsy_Add_Event(*cal, e);
                        }
                        link.calendars = (uint8_t)used;
                        if (used) anyOk = true;
                        if (failed && !api.error[0]) snprintf(api.error, sizeof(api.error), "A calendar could not be read");
                    }
                }
                link.state = oauth[p].state;
                snprintf(link.account, sizeof(link.account), "%s", account[p]);
                snprintf(link.error, sizeof(link.error), "%s", api.error[0] ? api.error : oauth[p].error);
                if (link.state == MTB_OAUTH_OK && !api.error[0]) link.error[0] = '\0';
            }
            cal->known = anyOk;
            cal->fetchedAt = now;
            lock();
            // A failed read keeps the last good calendar for half an hour.
            if (anyOk || !anyLinked || !N.data->cal.known || now - N.data->cal.fetchedAt > 1800) N.data->cal = *cal;
            memcpy(N.data->link, links, sizeof(links));
            N.data->syncedAt = now;
            N.version++;
            unlock();
        }

        // Teams presence.
        if (now >= presenceDue){
            presenceDue = now + PRESENCE_EVERY;
            const bool linked = mtb_Kit_OAuth_Is_Stored(OLK_PROVIDER.oauth->nvsKey);
            if (!s->usePresence || !linked){
                lock();
                N.data->presenceOk = -2;
                N.data->presence.known = false;
                N.data->presenceWhy[0] = '\0';
                N.version++;
                unlock();
                continue;
            }
            Mtb_Cal_Api_t api = {&oauth[BSY_MICROSOFT], UA, 0, ""};
            size_t length = 0;
            char* body = mtb_Cal_Api_Get(api, "https://graph.microsoft.com/v1.0/me/presence", 8192, length);
            Bsy_Presence_t pr = {};
            char why[80] = "";
            int result;
            if (body) result = bsy_Parse_Presence(body, length, api.lastCode, now, pr, why, sizeof(why));
            else if (api.lastCode == 400 || api.lastCode == 403 || api.lastCode == 404){
                result = 0;
                snprintf(why, sizeof(why), "Teams status needs a work or school account, linked with Presence.Read");
            } else {
                result = -1;
                snprintf(why, sizeof(why), "Teams status: %s", api.error[0] ? api.error : "no answer");
            }
            heap_caps_free(body);
            if (result == 0) presenceDue = now + 1800;      // not for this account: look again in half an hour
            else if (result < 0) presenceDue = now + 120;
            lock();
            N.data->presenceOk = (int8_t)result;
            snprintf(N.data->presenceWhy, sizeof(N.data->presenceWhy), "%s", why);
            if (result == 1) N.data->presence = pr;
            else if (now - N.data->presence.at > BSY_PRESENCE_FRESH) N.data->presence.known = false;
            N.version++;
            unlock();
            if (result != 1) ESP_LOGW(TAG, "%s", why);
        }
    }
    for (int p = 0; p < BSY_PROVIDERS; p++){
        if (oauth) mtb_Kit_OAuth_End(oauth[p]);
        heap_caps_free(lists[p]);
    }
    heap_caps_free(oauth);
    heap_caps_free(s);
    heap_caps_free(cal);
    heap_caps_free(tmp);
    N.running = false;
    vTaskDelete(nullptr);
}

bool bsy_Net_Start(const Bsy_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Bsy_Settings_t*)heap_caps_calloc(1, sizeof(Bsy_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Bsy_Data_t*)heap_caps_calloc(1, sizeof(Bsy_Data_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Bsy_Data_t));
    N.data->presenceOk = -2;
    N.version++;
    unlock();
    N.stop = false;
    N.sync = false;
    N.changed = false;
    N.running = true;
    if (xTaskCreatePinnedToCore(worker, "bsyNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS){ N.running = false; return false; }
    return true;
}

void bsy_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
}

void bsy_Net_Set_Settings(const Bsy_Settings_t& settings){
    if (!made) return;
    lock();
    Bsy_Settings_t& o = *N.settings;
    const bool changed = memcmp(o.useCalendar, settings.useCalendar, sizeof(o.useCalendar)) != 0 || o.usePresence != settings.usePresence
                      || o.allCalendars != settings.allCalendars;
    o = settings;
    if (changed) N.changed = true;
    unlock();
}

void bsy_Net_Sync(void){ N.sync = true; }

bool bsy_Net_Copy(Bsy_Data_t& d, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ d = *N.data; version = N.version; }
    unlock();
    return changed;
}
