#include "mtb_cal_engine.h"
#include "mtb_engine.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char TAG[] = "MTB_CAL";

#define CAL_STACK           12288
#define CAL_LIST_MAX_AGE    (6 * 3600)      // the account's calendar list, kept this long (monotonic seconds)
#define CAL_SEARCH_MAX_AGE  300
#define CAL_TIME_VALID      1700000000u

// Everything here lives as long as the device: a worker that is slow to stop never finds its memory gone.
struct Cal_Engine_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    TaskHandle_t task;
    volatile bool running;          // a worker task exists
    volatile bool stop;
    const Mtb_Cal_Provider_t* provider;
    Mtb_Cal_Search_Fn searchFn;

    // Under lock.
    Mtb_Cal_Settings_t settings;
    bool settingsChanged, syncRequested, searchPending;
    char searchId[16], searchQuery[64];
    uint32_t version;
    Mtb_Cal_Status_t status;

    // The worker's.
    Mtb_Cal_Data_t* published;      // under lock
    Mtb_Cal_Data_t* work;
    Mtb_Cal_Calendar_t* calendars;
    int calendarCount;
    uint32_t calendarsAt;           // monotonic
    uint32_t linkGeneration;
    uint8_t failures;
    Mtb_Kit_OAuth_t oauth;
};

EXT_RAM_BSS_ATTR static Cal_Engine_t E;
static bool engineMade = false;

static void lock(void){ xSemaphoreTake(E.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(E.lock); }
static uint32_t unix_Now(void){ return (uint32_t)time(nullptr); }

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

// API ------------------------------------------------------------------------------------------------------------
// The provider's own words for a refusal: {"error": {"message": "…"}} (Google and Microsoft), or {"error": "…"}.
static void api_Error(Mtb_Cal_Api_t& api, int code, const char* body, size_t length){
    char message[64] = "";
    if (body && length){
        JsonDocument filter;
        filter["error"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, body, length, DeserializationOption::Filter(filter))){
            const char* m = doc["error"]["message"] | (const char*)nullptr;
            if (m == nullptr) m = doc["error"].as<const char*>();
            copy(message, sizeof(message), m);
        }
    }
    if (code <= 0) snprintf(api.error, sizeof(api.error), "No connection to %s", E.provider ? E.provider->name : "the server");
    else if (code == 429) snprintf(api.error, sizeof(api.error), "Too many requests: slowing down");
    else if (message[0]) snprintf(api.error, sizeof(api.error), "%d: %s", code, message);
    else snprintf(api.error, sizeof(api.error), "HTTP %d", code);
}

char* mtb_Cal_Api_Get(Mtb_Cal_Api_t& api, const char* url, size_t maxBytes, size_t& length, const char* const* extraHeaders){
    length = 0;
    api.lastCode = -1;
    char* auth = (char*)heap_caps_malloc(MTB_KIT_OAUTH_ACCESS_MAX + 16, MALLOC_CAP_SPIRAM);
    if (auth == nullptr){ copy(api.error, sizeof(api.error), "Out of memory"); return nullptr; }
    char* body = nullptr;
    for (int attempt = 0; attempt < 2; attempt++){
        if (!mtb_Kit_OAuth_Bearer(*api.oauth, auth, MTB_KIT_OAUTH_ACCESS_MAX + 16)){
            copy(api.error, sizeof(api.error), api.oauth->state == MTB_OAUTH_RELINK ? "Link the account again" : "Not signed in");
            break;
        }
        const char* headers[16];
        int n = 0;
        headers[n++] = "Authorization"; headers[n++] = auth;
        headers[n++] = "Accept"; headers[n++] = "application/json";
        for (const char* const* h = extraHeaders; h && h[0] && h[1] && n < 14; h += 2){ headers[n++] = h[0]; headers[n++] = h[1]; }
        headers[n] = nullptr;
        Mtb_Kit_Http_t r;
        body = mtb_Kit_Http_Get(url, headers, nullptr, 0, maxBytes, r, api.userAgent);
        api.lastCode = r.code;
        length = body ? r.length : 0;
        if (r.code == 401 && attempt == 0){                // the token went stale early (revoked session…)
            mtb_Kit_OAuth_Reject(*api.oauth);
            heap_caps_free(body);
            body = nullptr;
            continue;
        }
        if (r.code != 200){
            api_Error(api, r.code, body, length);
            heap_caps_free(body);
            body = nullptr;
            length = 0;
        } else if (r.truncated){
            snprintf(api.error, sizeof(api.error), "Answer too long");
            heap_caps_free(body);
            body = nullptr;
            length = 0;
        }
        break;
    }
    memset(auth, 0, MTB_KIT_OAUTH_ACCESS_MAX + 16);
    heap_caps_free(auth);
    return body;
}

// Sync -----------------------------------------------------------------------------------------------------------
static bool calendars_Fresh(void){
    return E.calendarCount > 0 && mtb_Kit_OAuth_Now() - E.calendarsAt < CAL_LIST_MAX_AGE;
}

static bool load_Calendars(Mtb_Cal_Api_t& api){
    const int n = E.provider->calendars(api, E.calendars, MTB_CAL_CALENDARS);
    if (n < 0) return false;
    E.calendarCount = n;
    E.calendarsAt = mtb_Kit_OAuth_Now();
    return true;
}

static void set_Status(uint8_t sync, const char* error){
    lock();
    E.status.sync = sync;
    E.status.link = E.oauth.state;
    if (error) copy(E.status.error, sizeof(E.status.error), error);
    unlock();
}

// Which calendars to fetch: the user's choice, else what the account shows, plus a country's holidays.
static int choose(const Mtb_Cal_Settings_t& s, Mtb_Cal_Calendar_t* out, int max){
    int n = 0;
    if (s.calendarCount){
        for (int i = 0; i < s.calendarCount && n < max; i++){
            Mtb_Cal_Calendar_t c = {};
            copy(c.id, sizeof(c.id), s.calendars[i].id);
            copy(c.name, sizeof(c.name), s.calendars[i].name);
            c.color = mtb_Cal_Color(nullptr, i);
            for (int k = 0; k < E.calendarCount; k++) if (strcmp(E.calendars[k].id, c.id) == 0){ c = E.calendars[k]; break; }
            out[n++] = c;
        }
    } else {
        for (int k = 0; k < E.calendarCount && n < max; k++) if (E.calendars[k].shown) out[n++] = E.calendars[k];
        if (n == 0) for (int k = 0; k < E.calendarCount && n < max; k++) if (E.calendars[k].primary) out[n++] = E.calendars[k];
    }
    if (s.holidays[0] && E.provider->holidayCalendar && n < max){
        Mtb_Cal_Calendar_t h = {};
        if (E.provider->holidayCalendar(s.holidays, s.holidayName, h)){
            bool already = false;
            for (int i = 0; i < n; i++) if (strcmp(out[i].id, h.id) == 0) already = true;
            if (!already) out[n++] = h;
        }
    }
    return n;
}

static void sync(Mtb_Cal_Settings_t& s){
    Mtb_Cal_Api_t api = {};
    api.oauth = &E.oauth;
    api.userAgent = E.provider->oauth->userAgent;
    set_Status(MTB_CAL_SYNC_BUSY, nullptr);
    const uint32_t now = unix_Now();
    uint32_t wait = (uint32_t)s.refreshMinutes * 60;

    if (mtb_Kit_OAuth_Access(E.oauth) == nullptr){
        const uint8_t state = E.oauth.state;
        lock();
        E.status.link = state;
        E.status.sync = MTB_CAL_SYNC_FAILED;
        if (state == MTB_OAUTH_UNLINKED){ copy(E.status.error, sizeof(E.status.error), "Link your account in the phone app"); E.status.account[0] = '\0'; }
        else if (state == MTB_OAUTH_RELINK) snprintf(E.status.error, sizeof(E.status.error), "Link your account again (%.64s)", E.oauth.error);
        else snprintf(E.status.error, sizeof(E.status.error), "Sign-in failed: %.78s", E.oauth.error);
        E.status.nextAt = now + (state == MTB_OAUTH_RETRY ? 30 : 15);
        unlock();
        if (state != MTB_OAUTH_OK && state != MTB_OAUTH_RETRY){
            E.calendarCount = 0;      // another account may come
            lock();
            if (E.published->eventCount || E.published->taskCount){ memset(E.published, 0, sizeof(Mtb_Cal_Data_t)); E.published->unreadMail = -1; E.version++; }
            unlock();
        }
        return;
    }
    // A new link: the account's name and calendars again.
    if (E.linkGeneration != E.oauth.generation){
        E.linkGeneration = E.oauth.generation;
        E.calendarCount = 0;
        lock();
        E.status.account[0] = '\0';
        unlock();
    }
    char account[MTB_CAL_ACCOUNT] = "";
    lock();
    copy(account, sizeof(account), E.status.account);
    unlock();
    if (account[0] == '\0' && E.provider->account && E.provider->account(api, account, sizeof(account))){
        lock();
        copy(E.status.account, sizeof(E.status.account), account);
        unlock();
    }

    if (!calendars_Fresh() && !load_Calendars(api) && E.calendarCount == 0 && s.calendarCount == 0){
        ESP_LOGW(TAG, "calendar list: %s", api.error);
        lock();
        E.status.sync = MTB_CAL_SYNC_FAILED;
        E.status.link = E.oauth.state;
        copy(E.status.error, sizeof(E.status.error), api.error);
        const uint32_t backoff = 60u << (E.failures < 4 ? E.failures : 4);
        E.status.nextAt = now + (backoff < wait ? backoff : wait);
        unlock();
        if (E.failures < 250) E.failures++;
        return;
    }

    Mtb_Cal_Calendar_t* chosen = (Mtb_Cal_Calendar_t*)heap_caps_malloc(sizeof(Mtb_Cal_Calendar_t) * (MTB_CAL_PICKS + 1), MALLOC_CAP_SPIRAM);
    if (chosen == nullptr){ set_Status(MTB_CAL_SYNC_FAILED, "Out of memory"); return; }
    const int count = choose(s, chosen, MTB_CAL_PICKS + 1);

    Mtb_Cal_Data_t& w = *E.work;
    memset(&w, 0, sizeof(w));
    w.unreadMail = -1;
    copy(w.account, sizeof(w.account), account);
    const int32_t today = mtb_Cal_Local_Day(now);
    const uint32_t from = mtb_Cal_Day_Start(today), to = mtb_Cal_Day_Start(today + s.daysAhead);
    int failed = 0;
    char firstError[80] = "";
    for (int i = 0; i < count && !E.stop; i++){
        if (E.provider->events(api, chosen[i], from, to, w) < 0){
            failed++;
            if (firstError[0] == '\0') snprintf(firstError, sizeof(firstError), "%.20s: %.56s", chosen[i].name, api.error);
            ESP_LOGW(TAG, "events of %s: %s", chosen[i].name, api.error);
        }
    }
    if (s.showTasks && E.provider->tasks && !E.stop){
        if (E.provider->tasks(api, s.taskList.id, w) >= 0) w.tasksAsked = 1;
        else {
            if (firstError[0] == '\0') snprintf(firstError, sizeof(firstError), "Tasks: %.70s", api.error);
            ESP_LOGW(TAG, "tasks: %s", api.error);
        }
    }
    if (s.showMail && E.provider->unreadMail && !E.stop){
        const int unread = E.provider->unreadMail(api);
        w.unreadMail = (int16_t)(unread < 0 ? -1 : unread > 9999 ? 9999 : unread);
    }
    heap_caps_free(chosen);

    const bool allFailed = count > 0 && failed == count;
    lock();
    E.status.link = E.oauth.state;
    E.status.calendarsUsed = (uint8_t)count;
    if (allFailed){
        E.status.sync = MTB_CAL_SYNC_FAILED;
        copy(E.status.error, sizeof(E.status.error), firstError);
    } else {
        w.fetchedAt = now;
        memcpy(E.published, &w, sizeof(w));
        E.version++;
        E.status.lastOk = now;
        E.status.sync = failed || firstError[0] ? MTB_CAL_SYNC_PARTIAL : MTB_CAL_SYNC_OK;
        copy(E.status.error, sizeof(E.status.error), firstError);
        if (count == 0 && !firstError[0]) copy(E.status.error, sizeof(E.status.error), "No calendar chosen or shown");
    }
    if (allFailed){
        const uint32_t backoff = 60u << (E.failures < 4 ? E.failures : 4);
        E.status.nextAt = now + (backoff < wait ? backoff : wait);
    } else E.status.nextAt = now + wait;
    unlock();
    if (allFailed){ if (E.failures < 250) E.failures++; } else E.failures = 0;
}

// Searches -------------------------------------------------------------------------------------------------------
static bool name_Matches(const char* name, const char* query){
    if (query == nullptr || query[0] == '\0' || strcmp(query, "*") == 0) return true;
    const size_t q = strlen(query);
    for (const char* p = name; *p; p++) if (strncasecmp(p, query, q) == 0) return true;
    return false;
}

static void search(const char* id, const char* query){
    Mtb_Cal_Api_t api = {};
    api.oauth = &E.oauth;
    api.userAgent = E.provider->oauth->userAgent;
    Mtb_Cal_Pick_t* picks = (Mtb_Cal_Pick_t*)heap_caps_calloc(MTB_CAL_CALENDARS, sizeof(Mtb_Cal_Pick_t), MALLOC_CAP_SPIRAM);
    uint16_t colors[MTB_CAL_CALENDARS] = {};
    if (picks == nullptr){ E.searchFn(id, query, nullptr, nullptr, 0, "Out of memory"); return; }
    int n = 0;
    const char* error = nullptr;
    if (strcmp(id, "calendars") == 0){
        if ((E.calendarCount == 0 || mtb_Kit_OAuth_Now() - E.calendarsAt > CAL_SEARCH_MAX_AGE) && !load_Calendars(api) && E.calendarCount == 0)
            error = api.error;
        for (int k = 0; k < E.calendarCount && n < MTB_CAL_CALENDARS; k++){
            if (!name_Matches(E.calendars[k].name, query)) continue;
            copy(picks[n].id, sizeof(picks[n].id), E.calendars[k].id);
            copy(picks[n].name, sizeof(picks[n].name), E.calendars[k].name);
            colors[n++] = E.calendars[k].color;
        }
    } else if (strcmp(id, "taskList") == 0 && E.provider->taskLists){
        Mtb_Cal_Pick_t* all = (Mtb_Cal_Pick_t*)heap_caps_calloc(MTB_CAL_CALENDARS, sizeof(Mtb_Cal_Pick_t), MALLOC_CAP_SPIRAM);
        const int count = all ? E.provider->taskLists(api, all, MTB_CAL_CALENDARS) : -1;
        if (count < 0) error = all ? api.error : "Out of memory";
        for (int k = 0; k < count && n < MTB_CAL_CALENDARS; k++) if (name_Matches(all[k].name, query)) picks[n++] = all[k];
        heap_caps_free(all);
    } else error = "Not offered";
    E.searchFn(id, query, picks, colors, n, error);
    heap_caps_free(picks);
}

// The worker -----------------------------------------------------------------------------------------------------
static void cal_Worker(void*){
    mtb_Kit_OAuth_Begin(E.oauth, *E.provider->oauth);
    E.linkGeneration = 0;
    E.failures = 0;
    E.calendarCount = 0;
    Mtb_Cal_Settings_t* s = (Mtb_Cal_Settings_t*)heap_caps_malloc(sizeof(Mtb_Cal_Settings_t), MALLOC_CAP_SPIRAM);
    while (!E.stop && s){
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (E.stop) break;
        const bool online = Mtb_Applications::internetConnectStatus;
        const uint32_t now = unix_Now();
        bool doSync = false, doSearch = false;
        char id[16], query[64];
        lock();
        if (E.searchPending && online){ E.searchPending = false; doSearch = true; copy(id, sizeof(id), E.searchId); copy(query, sizeof(query), E.searchQuery); }
        if (online && now > CAL_TIME_VALID && (E.syncRequested || E.settingsChanged || (int32_t)(now - E.status.nextAt) >= 0)){
            doSync = true;
            E.syncRequested = E.settingsChanged = false;
            *s = E.settings;
        }
        unlock();
        if (doSearch) search(id, query);
        if (doSync && !E.stop) sync(*s);
    }
    heap_caps_free(s);
    mtb_Kit_OAuth_End(E.oauth);
    E.task = NULL;
    E.running = false;
    vTaskDelete(NULL);
}

bool mtb_Cal_Engine_Start(const Mtb_Cal_Provider_t& provider, const Mtb_Cal_Settings_t& settings, Mtb_Cal_Search_Fn searchFn){
    if (!engineMade){
        E.lock = xSemaphoreCreateMutexStatic(&E.lockBuffer);
        E.published = (Mtb_Cal_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Cal_Data_t), MALLOC_CAP_SPIRAM);
        E.work = (Mtb_Cal_Data_t*)heap_caps_calloc(1, sizeof(Mtb_Cal_Data_t), MALLOC_CAP_SPIRAM);
        E.calendars = (Mtb_Cal_Calendar_t*)heap_caps_calloc(MTB_CAL_CALENDARS, sizeof(Mtb_Cal_Calendar_t), MALLOC_CAP_SPIRAM);
        if (E.lock == NULL || E.published == nullptr || E.work == nullptr || E.calendars == nullptr) return false;
        engineMade = true;
    }
    // A worker of the app before may still be finishing a request.
    for (int i = 0; i < 400 && E.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (E.running) return false;
    lock();
    E.provider = &provider;
    E.searchFn = searchFn;
    E.settings = settings;
    E.settingsChanged = false;
    E.syncRequested = true;
    E.searchPending = false;
    E.version++;
    memset(E.published, 0, sizeof(Mtb_Cal_Data_t));
    E.published->unreadMail = -1;
    memset(&E.status, 0, sizeof(E.status));
    E.status.link = mtb_Kit_OAuth_Is_Stored(provider.oauth->nvsKey) ? MTB_OAUTH_RETRY : MTB_OAUTH_UNLINKED;
    unlock();
    E.stop = false;
    E.running = true;
    if (xTaskCreatePinnedToCore(cal_Worker, "calSync", CAL_STACK, nullptr, 1, &E.task, tskNO_AFFINITY) != pdPASS){
        E.running = false;
        return false;
    }
    return true;
}

void mtb_Cal_Engine_Stop(void){
    if (!E.running) return;
    E.stop = true;
    if (E.task) xTaskNotifyGive(E.task);
    for (int i = 0; i < 40 && E.running; i++) vTaskDelay(pdMS_TO_TICKS(50));   // not longer: the app is closing
}

// What makes a sync necessary at once (the view, the clock format and the like do not).
static bool fetch_Changed(const Mtb_Cal_Settings_t& a, const Mtb_Cal_Settings_t& b){
    return a.daysAhead != b.daysAhead || a.showTasks != b.showTasks || a.showMail != b.showMail || a.calendarCount != b.calendarCount ||
           memcmp(a.calendars, b.calendars, sizeof(a.calendars)) != 0 || strcmp(a.taskList.id, b.taskList.id) != 0 ||
           strcmp(a.holidays, b.holidays) != 0 || a.refreshMinutes != b.refreshMinutes;
}

void mtb_Cal_Engine_Set_Settings(const Mtb_Cal_Settings_t& settings){
    if (!engineMade) return;
    lock();
    if (fetch_Changed(E.settings, settings)) E.settingsChanged = true;
    E.settings = settings;
    unlock();
    if (E.task) xTaskNotifyGive(E.task);
}

void mtb_Cal_Engine_Sync(void){
    if (!engineMade) return;
    lock();
    E.syncRequested = true;
    unlock();
    if (E.task) xTaskNotifyGive(E.task);
}

bool mtb_Cal_Engine_Copy(Mtb_Cal_Data_t& out, uint32_t& version){
    if (!engineMade) return false;
    lock();
    const bool changed = version != E.version;
    if (changed){ memcpy(&out, E.published, sizeof(out)); version = E.version; }
    unlock();
    return changed;
}

void mtb_Cal_Engine_Get_Status(Mtb_Cal_Status_t& status){
    if (!engineMade){ memset(&status, 0, sizeof(status)); return; }
    lock();
    status = E.status;
    unlock();
}

bool mtb_Cal_Engine_Search(const char* searchId, const char* query){
    if (!engineMade || !E.running) return false;
    lock();
    copy(E.searchId, sizeof(E.searchId), searchId);
    copy(E.searchQuery, sizeof(E.searchQuery), query);
    E.searchPending = true;
    unlock();
    if (E.task) xTaskNotifyGive(E.task);
    return true;
}

bool mtb_Cal_Engine_Running_For(const Mtb_Cal_Provider_t& provider){
    return engineMade && E.running && E.provider == &provider;
}
