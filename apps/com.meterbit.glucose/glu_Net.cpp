// Glucose Display's worker: asks the chosen source just after each new reading is due (every 5 minutes for Nightscout
// and Dexcom, every minute for LibreLinkUp), keeps the last day of readings, and logs in again when a session ends.
// A refused login is not tried again until the phone changes it: Dexcom and Abbott lock accounts after repeated
// failures.
#include "glu_App.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "GLU_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Glucose Display)"
#define STATUS_EVERY    3600u
#define GRAPH_EVERY     900u
#define LLU_VERSION     "4.16.0"

ArduinoJson::Allocator* glu_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

static const char* const DEXCOM_BASE[3] = {
    "https://share2.dexcom.com/ShareWebServices/Services/",
    "https://shareous1.dexcom.com/ShareWebServices/Services/",
    "https://share.dexcom.jp/ShareWebServices/Services/"};
static const char* const DEXCOM_APP[3] = {"d89443d2-327c-4a6f-89e5-496bbb0317db", "d89443d2-327c-4a6f-89e5-496bbb0317db",
                                          "d8665ade-9673-4e27-9ff6-92db4ce13d13"};

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    uint32_t credGeneration;            /**< bumped when the source or its login changes */
    Glu_Settings_t* settings;
    Glu_Data_t* data;
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

/** The worker's own state between asks. */
struct Work_t {
    Glu_Settings_t s;
    Glu_Data_t d;
    uint32_t generation;
    uint32_t statusAt, propsAt, graphAt;
    bool propsMissing;
    char session[48];                   /**< Dexcom */
    Glu_Libre_Login_t login;            /**< LibreLinkUp: token and user */
    char accountId[65];
    bool loggedIn;
    bool blocked;                       /**< a refused login: wait for new details */
    char url[600];
    char auth[1100];
};

static void set_State(Work_t& w, uint8_t state, const char* error){
    w.d.state = state;
    snprintf(w.d.error, sizeof(w.d.error), "%s", error ? error : "");
}

// Nightscout ---------------------------------------------------------------------------------------------------------
static void ns_Base(const Glu_Settings_t& s, char* d, size_t n){
    snprintf(d, n, "%s", s.nsUrl);
    size_t k = strlen(d);
    while (k && d[k - 1] == '/') d[--k] = '\0';
}

/** GET a Nightscout path, with the token in the query or the hashed secret as API-SECRET. */
static char* ns_Get(Work_t& w, const char* pathAndQuery, size_t max, Mtb_Kit_Http_t& res){
    char base[100];
    ns_Base(w.s, base, sizeof(base));
    char tok[200] = "";
    if (w.s.nsToken[0]){
        char enc[190];
        mtb_Kit_Url_Encode(enc, sizeof(enc), w.s.nsToken);
        snprintf(tok, sizeof(tok), "%stoken=%s", strchr(pathAndQuery, '?') ? "&" : "?", enc);
    }
    snprintf(w.url, sizeof(w.url), "%s%s%s", base, pathAndQuery, tok);
    const char* withSecret[] = {"API-SECRET", w.s.nsSecretSha1, "Accept", "application/json", nullptr};
    const char* plain[] = {"Accept", "application/json", nullptr};
    char* body = mtb_Kit_Http_Get(w.url, w.s.nsSecretSha1[0] ? withSecret : plain, nullptr, 0, max, res, UA);
    memset(w.url, 0, sizeof(w.url));
    return body;
}

static void ns_Poll(Work_t& w, uint32_t now){
    if (!w.s.nsUrl[0]){ set_State(w, GLU_NET_NOT_SET, "Type your Nightscout address on the phone"); return; }
    Mtb_Kit_Http_t res;
    // The site's units and targets, now and then.
    if (!w.d.site.known || now - w.statusAt > STATUS_EVERY){
        char* body = ns_Get(w, "/api/v1/status.json", 32 * 1024, res);
        if (body && res.code == 200){
            Glu_Site_t site;
            if (glu_Parse_Ns_Status(body, res.length, site)){ w.d.site = site; w.statusAt = now; }
        }
        heap_caps_free(body);
        if (res.code == 401 || res.code == 403){
            set_State(w, GLU_NET_AUTH, "Nightscout refused: add a token with the \"readable\" role");
            return;
        }
        if (res.code == 404){ set_State(w, GLU_NET_FAILED, "No Nightscout site at that address"); return; }
    }
    // The readings since the last one (the whole day the first time).
    const Glu_Reading_t* last = glu_Latest(w.d.hist);
    char path[160];
    if (last) snprintf(path, sizeof(path), "/api/v1/entries/sgv.json?count=60&find%%5Bdate%%5D%%5B%%24gt%%5D=%llu",
                       (unsigned long long)last->t * 1000ull);
    else snprintf(path, sizeof(path), "/api/v1/entries/sgv.json?count=%d&find%%5Bdate%%5D%%5B%%24gte%%5D=%llu",
                  GLU_HISTORY, (unsigned long long)(now - 86400u) * 1000ull);
    char* body = ns_Get(w, path, 96 * 1024, res);
    int got = -1;
    if (body && res.code == 200) got = glu_Parse_Ns_Entries(body, res.length, now, w.d.hist);
    heap_caps_free(body);
    if (res.code == 401 || res.code == 403){ set_State(w, GLU_NET_AUTH, "Nightscout refused: check the token (role \"readable\")"); return; }
    if (got < 0){
        char e[64];
        snprintf(e, sizeof(e), res.code > 0 ? "Nightscout answered HTTP %d" : "No answer from Nightscout (%d)", res.code);
        set_State(w, GLU_NET_FAILED, e);
        return;
    }
    // Loop, AAPS and OpenAPS: insulin and carbs on board, the phone's battery.
    if (!w.propsMissing || now - w.propsAt > STATUS_EVERY){
        w.propsAt = now;
        char* p = ns_Get(w, "/api/v2/properties/iob,cob,upbat", 16 * 1024, res);
        Glu_Extra_t x;
        if (p && res.code == 200 && glu_Parse_Ns_Properties(p, res.length, x)){ w.d.extra = x; w.propsMissing = false; }
        else w.propsMissing = true;
        heap_caps_free(p);
    }
    set_State(w, GLU_NET_OK, nullptr);
}

// Dexcom Share -------------------------------------------------------------------------------------------------------
static bool looks_Like_Uuid(const char* t){
    if (strlen(t) != 36) return false;
    for (int i = 0; i < 36; i++){
        if (i == 8 || i == 13 || i == 18 || i == 23){ if (t[i] != '-') return false; }
        else if (!isxdigit((unsigned char)t[i])) return false;
    }
    return true;
}

/** "abc" as a JSON string (quotes and backslashes escaped). */
static void json_String(char* d, size_t n, const char* t){
    size_t k = 0;
    if (n < 3) return;
    d[k++] = '"';
    for (; *t && k + 3 < n; t++){
        if (*t == '"' || *t == '\\') d[k++] = '\\';
        if ((unsigned char)*t >= ' ') d[k++] = *t;
    }
    d[k++] = '"';
    d[k] = '\0';
}

static char* dx_Post(Work_t& w, const char* endpoint, const char* json, Mtb_Kit_Http_t& res, size_t max){
    snprintf(w.url, sizeof(w.url), "%s%s", DEXCOM_BASE[w.s.dxRegion], endpoint);
    static const char* const headers[] = {"Content-Type", "application/json", "Accept", "application/json", nullptr};
    return mtb_Kit_Http_Send("POST", w.url, headers, json, strlen(json), nullptr, 0, max, res, UA);
}

/** Why a Dexcom login failed, from its Code. */
static void dx_Refused(Work_t& w, const char* code, const char* body){
    if (!strcmp(code, "SSO_AuthenticateMaxAttemptsExceeded")){
        w.blocked = true;
        set_State(w, GLU_NET_LOCKED, "Dexcom locked the login after too many tries: wait 10 minutes, then send it again");
    } else if (!strcmp(code, "AccountPasswordInvalid") || (body && (strstr(body, "Cannot Authenticate") || strstr(body, "accountName")
                                                                    || strstr(body, "password")))){
        w.blocked = true;
        set_State(w, GLU_NET_AUTH, "Dexcom refused the user name or password (or the region)");
    } else {
        char e[96];
        snprintf(e, sizeof(e), "Dexcom: %s", code[0] ? code : "no answer");
        set_State(w, GLU_NET_FAILED, e);
    }
}

static bool dx_Login(Work_t& w){
    if (w.blocked) return false;
    char user[140], pass[140], app[48], body[400], code[48] = "";
    json_String(pass, sizeof(pass), w.s.dxPass);
    json_String(app, sizeof(app), DEXCOM_APP[w.s.dxRegion]);
    Mtb_Kit_Http_t res;
    char accountId[48];
    if (looks_Like_Uuid(w.s.dxUser)) snprintf(accountId, sizeof(accountId), "%s", w.s.dxUser);
    else {
        json_String(user, sizeof(user), w.s.dxUser);
        snprintf(body, sizeof(body), "{\"accountName\":%s,\"password\":%s,\"applicationId\":%s}", user, pass, app);
        char* a = dx_Post(w, "General/AuthenticatePublisherAccount", body, res, 4096);
        memset(body, 0, sizeof(body));
        const bool ok = a && res.code == 200 && glu_Parse_Dexcom_Id(a, res.length, accountId, sizeof(accountId));
        if (!ok){
            Glu_History_t scratch = {};
            if (a) glu_Parse_Dexcom(a, res.length, 0, scratch, code, sizeof(code));
            dx_Refused(w, a ? code : "", a);
        }
        heap_caps_free(a);
        if (!ok){ memset(pass, 0, sizeof(pass)); return false; }
    }
    char id[60];
    json_String(id, sizeof(id), accountId);
    snprintf(body, sizeof(body), "{\"accountId\":%s,\"password\":%s,\"applicationId\":%s}", id, pass, app);
    memset(pass, 0, sizeof(pass));
    char* s = dx_Post(w, "General/LoginPublisherAccountById", body, res, 4096);
    memset(body, 0, sizeof(body));
    const bool ok = s && res.code == 200 && glu_Parse_Dexcom_Id(s, res.length, w.session, sizeof(w.session));
    if (!ok){
        Glu_History_t scratch = {};
        if (s) glu_Parse_Dexcom(s, res.length, 0, scratch, code, sizeof(code));
        dx_Refused(w, s ? code : "", s);
    }
    heap_caps_free(s);
    w.loggedIn = ok;
    return ok;
}

static void dx_Poll(Work_t& w, uint32_t now){
    if (!w.s.dxUser[0] || !w.s.dxPass[0]){ set_State(w, GLU_NET_NOT_SET, "Type the Dexcom account on the phone"); return; }
    if (w.blocked) return;
    for (int attempt = 0; attempt < 2; attempt++){
        if (!w.loggedIn && !dx_Login(w)) return;
        const bool first = w.d.hist.count == 0;
        snprintf(w.url, sizeof(w.url), "%sPublisher/ReadPublisherLatestGlucoseValues?sessionId=%s&minutes=%d&maxCount=%d",
                 DEXCOM_BASE[w.s.dxRegion], w.session, first ? 1440 : 60, first ? 288 : 12);
        static const char* const headers[] = {"Content-Type", "application/json", "Accept", "application/json", nullptr};
        Mtb_Kit_Http_t res;
        char* body = mtb_Kit_Http_Send("POST", w.url, headers, "{}", 2, nullptr, 0, 64 * 1024, res, UA);
        memset(w.url, 0, sizeof(w.url));
        char code[48] = "";
        const int got = body ? glu_Parse_Dexcom(body, res.length, now, w.d.hist, code, sizeof(code)) : -1;
        heap_caps_free(body);
        if (got >= 0){ set_State(w, GLU_NET_OK, nullptr); return; }
        if (!strcmp(code, "SessionIdNotFound") || !strcmp(code, "SessionNotValid")){ w.loggedIn = false; continue; }
        char e[96];
        if (code[0]) snprintf(e, sizeof(e), "Dexcom: %s", code);
        else snprintf(e, sizeof(e), "No answer from Dexcom (%d)", res.code);
        set_State(w, GLU_NET_FAILED, e);
        return;
    }
}

// LibreLinkUp --------------------------------------------------------------------------------------------------------
static void llu_Url(Work_t& w, const char* path){
    if (w.d.region[0]) snprintf(w.url, sizeof(w.url), "https://api-%s.libreview.io%s", w.d.region, path);
    else snprintf(w.url, sizeof(w.url), "https://api.libreview.io%s", path);
}

static bool llu_Login(Work_t& w){
    if (w.blocked) return false;
    for (int hop = 0; hop < 2; hop++){
        char email[140], pass[140], body[320];
        json_String(email, sizeof(email), w.s.lluEmail);
        json_String(pass, sizeof(pass), w.s.lluPass);
        snprintf(body, sizeof(body), "{\"email\":%s,\"password\":%s}", email, pass);
        memset(pass, 0, sizeof(pass));
        llu_Url(w, "/llu/auth/login");
        static const char* const headers[] = {"Content-Type", "application/json", "Accept", "application/json", "product", "llu.android",
                                              "version", LLU_VERSION, "cache-control", "no-cache", nullptr};
        Mtb_Kit_Http_t res;
        char* a = mtb_Kit_Http_Send("POST", w.url, headers, body, strlen(body), nullptr, 0, 16 * 1024, res, UA);
        memset(body, 0, sizeof(body));
        const bool ok = a && glu_Parse_Libre_Login(a, res.length, w.login);
        const bool locked = res.code == 429 || (a && strstr(a, "locked"));
        const bool oldVersion = a && strstr(a, "\"status\":920");
        heap_caps_free(a);
        if (ok && w.login.redirect){
            snprintf(w.d.region, sizeof(w.d.region), "%s", w.login.region);
            continue;                                // log in again at the account's region
        }
        if (ok){
            glu_Sha256_Hex(w.login.userId, w.accountId);
            w.loggedIn = true;
            return true;
        }
        if (locked){ w.blocked = true; set_State(w, GLU_NET_LOCKED, "LibreLinkUp locked the login for a while: wait, then send it again"); }
        else if (w.login.status == 4){ w.blocked = true; set_State(w, GLU_NET_TERMS, "Open the LibreLinkUp app and accept its new terms, then send the login again"); }
        else if (w.login.status == 2 || res.code == 401){ w.blocked = true; set_State(w, GLU_NET_AUTH, "LibreLinkUp refused the email or password"); }
        else if (oldVersion) set_State(w, GLU_NET_FAILED, "LibreLinkUp asks for a newer app version: Pixlpal needs an update");
        else {
            char e[80];
            snprintf(e, sizeof(e), "LibreLinkUp login failed (HTTP %d)", res.code);
            set_State(w, GLU_NET_FAILED, e);
        }
        return false;
    }
    set_State(w, GLU_NET_FAILED, "LibreLinkUp kept redirecting the login");
    return false;
}

static char* llu_Get(Work_t& w, const char* path, size_t max, Mtb_Kit_Http_t& res){
    llu_Url(w, path);
    snprintf(w.auth, sizeof(w.auth), "Bearer %s", w.login.token);
    const char* headers[] = {"Accept", "application/json", "product", "llu.android", "version", LLU_VERSION, "Account-Id", w.accountId,
                             "Authorization", w.auth, "cache-control", "no-cache", nullptr};
    char* body = mtb_Kit_Http_Get(w.url, headers, nullptr, 0, max, res, UA);
    memset(w.auth, 0, sizeof(w.auth));
    return body;
}

static void llu_Poll(Work_t& w, uint32_t now){
    if (!w.s.lluEmail[0] || !w.s.lluPass[0]){ set_State(w, GLU_NET_NOT_SET, "Type the LibreLinkUp login on the phone"); return; }
    if (w.blocked) return;
    for (int attempt = 0; attempt < 2; attempt++){
        if (!w.loggedIn && !llu_Login(w)) return;
        Mtb_Kit_Http_t res;
        char* body = llu_Get(w, "/llu/connections", 64 * 1024, res);
        Glu_Libre_Patient_t people[GLU_PATIENTS] = {};
        const int n = body && res.code == 200 ? glu_Parse_Libre_Connections(body, res.length, now, w.s.lluPatient, people, GLU_PATIENTS, w.d.hist) : -1;
        heap_caps_free(body);
        if (res.code == 401){ w.loggedIn = false; continue; }
        if (n < 0){
            char e[80];
            snprintf(e, sizeof(e), "LibreLinkUp answered HTTP %d", res.code);
            set_State(w, GLU_NET_FAILED, e);
            return;
        }
        w.d.people = (uint8_t)(n < GLU_PATIENTS ? n : GLU_PATIENTS);
        memcpy(w.d.person, people, sizeof(people));
        if (n == 0){ set_State(w, GLU_NET_FAILED, "Nobody shares their readings with this LibreLinkUp account yet"); return; }
        // The last 12 hours, the first time and then every quarter hour.
        const uint8_t pick = w.s.lluPatient < w.d.people ? w.s.lluPatient : 0;
        if (now - w.graphAt > GRAPH_EVERY || w.d.hist.count < 3){
            char path[96];
            snprintf(path, sizeof(path), "/llu/connections/%s/graph", people[pick].id);
            bool safe = people[pick].id[0];
            for (const char* p = people[pick].id; *p; p++) if (!(isalnum((unsigned char)*p) || *p == '-')) safe = false;
            if (safe){
                char* g = llu_Get(w, path, 160 * 1024, res);
                if (g && res.code == 200 && glu_Parse_Libre_Graph(g, res.length, now, w.d.hist) >= 0) w.graphAt = now;
                heap_caps_free(g);
            }
        }
        set_State(w, GLU_NET_OK, nullptr);
        return;
    }
    set_State(w, GLU_NET_FAILED, "LibreLinkUp ended the session");
}

// The worker ---------------------------------------------------------------------------------------------------------
static void worker(void*){
    Work_t* w = (Work_t*)heap_caps_calloc(1, sizeof(Work_t), MALLOC_CAP_SPIRAM);
    if (!w) N.stop = true;
    uint32_t due = 0;
    uint8_t source = 0xFF;
    while (!N.stop){
        vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t now = (uint32_t)time(nullptr);
        lock();
        w->s = *N.settings;
        const uint32_t gen = N.credGeneration;
        const bool refresh = N.refresh;
        N.refresh = false;
        unlock();
        if (gen != w->generation || w->s.source != source){
            // A new source or login: start again, with none of the old readings.
            w->generation = gen;
            source = w->s.source;
            w->loggedIn = false;
            w->blocked = false;
            memset(w->session, 0, sizeof(w->session));
            memset(&w->login, 0, sizeof(w->login));
            memset(&w->d.hist, 0, sizeof(w->d.hist));
            memset(&w->d.site, 0, sizeof(w->d.site));
            memset(&w->d.extra, 0, sizeof(w->d.extra));
            w->d.people = 0;
            w->d.region[0] = '\0';
            w->statusAt = w->propsAt = w->graphAt = 0;
            w->propsMissing = false;
            set_State(*w, GLU_NET_CONNECTING, nullptr);
            due = 0;
        }
        if (refresh){ due = 0; w->blocked = false; }
        if (now < 1700000000u || !Mtb_Applications::internetConnectStatus || now < due) continue;
        if (w->d.state == GLU_NET_LOCKED) w->blocked = false;  // a lock lasts a while: one careful try after it
        w->d.triedAt = now;
        const uint32_t before = w->d.hist.count ? glu_Latest(w->d.hist)->t : 0;
        if (source == GLU_NIGHTSCOUT) ns_Poll(*w, now);
        else if (source == GLU_DEXCOM) dx_Poll(*w, now);
        else llu_Poll(*w, now);
        if (w->d.state == GLU_NET_OK) w->d.okAt = now;
        const Glu_Reading_t* last = glu_Latest(w->d.hist);
        const uint32_t every = source == GLU_LIBRE ? 60 : 300;
        if (w->d.state == GLU_NET_OK) due = glu_Next_Poll(last ? last->t : 0, now, every);
        else if (w->d.state == GLU_NET_FAILED) due = now + 60;
        else if (w->d.state == GLU_NET_LOCKED) due = now + 1800;
        else due = now + 600;                                   // not set or refused: the phone wakes us (refresh)
        w->d.nextAt = due;
        if (last && last->t != before) ESP_LOGI(TAG, "reading %u at %lu", last->mgdl, (unsigned long)last->t);
        else if (w->d.state != GLU_NET_OK) ESP_LOGW(TAG, "%s", w->d.error);
        lock();
        *N.data = w->d;
        N.version++;
        unlock();
    }
    if (w){ memset(w, 0, sizeof(*w)); heap_caps_free(w); }
    N.running = false;
    vTaskDelete(nullptr);
}

bool glu_Net_Start(const Glu_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Glu_Settings_t*)heap_caps_calloc(1, sizeof(Glu_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Glu_Data_t*)heap_caps_calloc(1, sizeof(Glu_Data_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Glu_Data_t));
    N.data->state = GLU_NET_CONNECTING;
    N.credGeneration++;
    N.version++;
    unlock();
    N.stop = false;
    N.refresh = false;
    N.running = true;
    if (xTaskCreatePinnedToCore(worker, "gluNet", 8192, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS){ N.running = false; return false; }
    return true;
}

void glu_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
}

void glu_Net_Set_Settings(const Glu_Settings_t& s){
    if (!made) return;
    lock();
    const Glu_Settings_t& o = *N.settings;
    const bool login = o.source != s.source || strcmp(o.nsUrl, s.nsUrl) || strcmp(o.nsToken, s.nsToken) || strcmp(o.nsSecretSha1, s.nsSecretSha1)
                    || strcmp(o.dxUser, s.dxUser) || strcmp(o.dxPass, s.dxPass) || o.dxRegion != s.dxRegion
                    || strcmp(o.lluEmail, s.lluEmail) || strcmp(o.lluPass, s.lluPass) || o.lluPatient != s.lluPatient;
    *N.settings = s;
    if (login) N.credGeneration++;
    unlock();
}

void glu_Net_Refresh(void){ N.refresh = true; }

bool glu_Net_Copy(Glu_Data_t& d, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ d = *N.data; version = N.version; }
    unlock();
    return changed;
}
