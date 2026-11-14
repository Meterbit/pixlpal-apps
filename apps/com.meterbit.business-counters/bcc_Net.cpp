// Business & Creator Counters: the worker that asks each provider on its own schedule, straight from Pixlpal (no
// server of ours). Shops every 30 seconds (a sale shows within half a minute), YouTube every 5 minutes (288 of the
// free 10,000 quota units a day), GitHub as often as its hourly limit allows, Plausible's live count every minute and
// the day's numbers every 5 (about 72 of its 600 requests an hour). A failure waits longer each time, up to 15 minutes;
// a rate limit waits as long as the provider says.
#include "bcc_App.h"
#include "bcc_Parse.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char TAG[] = "BCC_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Business Counters)"
#define SHOP_PAGES      10              // 1,000 sales a day per shop are counted; more shows as "1,000+"
#define SHOP_SECONDS    30
#define BODY_MAX        (1536 * 1024)   // a Stripe page of 100 charges is about 350 KB

ArduinoJson::Allocator* bcc_Json_Allocator(void){ return mtb_Kit_Json_Allocator(); }

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    uint8_t askNow;                     // under lock: sources to ask at once (their settings changed)
    Bcc_Settings_t* settings;           // under lock; the worker works on its own copy
    Bcc_Status_t status;                // under lock
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }
static uint32_t mono(void){ return millis() / 1000; }

// The worker's own state.
struct Plan_t {
    uint32_t due[BCC_SOURCES];          // mono seconds
    uint8_t fails[BCC_SOURCES];
    uint32_t liveDue, todayDue;         // Plausible's two questions
    char shopToken[96];                 // Shopify's 24-hour token from the client credentials
    uint32_t shopTokenUntil;            // mono seconds
    char shopTokenFor[160];             // the store and client ID it is for
};
EXT_RAM_BSS_ATTR static Plan_t P;

// Today, from Pixlpal's clock: the day number and local midnight as Unix seconds.
static uint32_t local_Midnight(uint32_t now, uint32_t* day){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (day) *day = (uint32_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return now - (uint32_t)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
}

static void schedule(uint8_t source, uint8_t state, uint32_t okSeconds, uint32_t waitSeconds = 0){
    if (state == BCC_OK){ P.fails[source] = 0; P.due[source] = mono() + okSeconds; return; }
    if (P.fails[source] < 6) P.fails[source]++;
    uint32_t wait = okSeconds << P.fails[source];
    if (state == BCC_LIMIT) wait = waitSeconds ? waitSeconds : 300;
    if (state == BCC_KEY || state == BCC_PLAN) wait = 900;          // until the user changes it (a change asks at once)
    if (wait > 900) wait = 900;
    if (wait < 20) wait = 20;
    P.due[source] = mono() + wait;
}

// Shops -------------------------------------------------------------------------------------------------------------
// One day's pass over a shop: all its pages into @p work. Returns the state.
static uint8_t pass_Stripe(const Bcc_Settings_t& s, uint32_t midnight, Bcc_Sales_t& work){
    char auth[150];
    snprintf(auth, sizeof(auth), "Bearer %s", s.stripeKey);
    const char* headers[] = {"Authorization", auth, nullptr};
    Bcc_Page_t page = {};
    uint8_t state = BCC_OK;
    for (int p = 0; p < SHOP_PAGES; p++){
        char url[200];
        snprintf(url, sizeof(url), "https://api.stripe.com/v1/charges?created%%5Bgte%%5D=%lu&limit=100%s%s", (unsigned long)midnight,
                 p ? "&starting_after=" : "", p ? page.cursor : "");
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, BODY_MAX, r, UA);
        state = bcc_Parse_Stripe(body, r.length, r.code, work, page, work.text, sizeof(work.text));
        heap_caps_free(body);
        if (state != BCC_OK || !page.more) break;
        if (p == SHOP_PAGES - 1) work.more = 1;
    }
    memset(auth, 0, sizeof(auth));
    const bool test = !strncmp(s.stripeKey, "sk_test_", 8) || !strncmp(s.stripeKey, "rk_test_", 8);
    if (test) work.test = 1;
    snprintf(work.plan, sizeof(work.plan), "%s", work.test ? "Test mode" : "Live mode");
    return state;
}

static uint8_t pass_Paystack(const Bcc_Settings_t& s, uint32_t midnight, Bcc_Sales_t& work){
    char auth[110], since[24];
    snprintf(auth, sizeof(auth), "Bearer %s", s.paystackKey);
    bcc_Iso(since, sizeof(since), midnight);
    const char* headers[] = {"Authorization", auth, nullptr};
    uint8_t state = BCC_OK;
    for (int p = 1; p <= SHOP_PAGES; p++){
        char url[200];
        snprintf(url, sizeof(url), "https://api.paystack.co/transaction?from=%s&perPage=100&page=%d&status=success", since, p);
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, BODY_MAX, r, UA);
        Bcc_Page_t page;
        state = bcc_Parse_Paystack(body, r.length, r.code, p, work, page, work.text, sizeof(work.text));
        heap_caps_free(body);
        if (state != BCC_OK || !page.more) break;
        if (p == SHOP_PAGES) work.more = 1;
    }
    memset(auth, 0, sizeof(auth));
    work.test = !strncmp(s.paystackKey, "sk_test_", 8) ? 1 : 0;
    snprintf(work.plan, sizeof(work.plan), "%s", work.test ? "Test mode" : "Live mode");
    return state;
}

// Shopify's access token: the client credentials grant (a Dev Dashboard app in the store's organization), renewed
// before its 24 hours run out; or, with no client ID, a legacy custom app's Admin API token as it is.
static bool shopify_Token(const Bcc_Settings_t& s, const char* domain, char* token, size_t size, Bcc_Sales_t& work, uint8_t& state){
    if (!s.shopClientId[0]){ snprintf(token, size, "%s", s.shopSecret); return true; }
    char forWhom[160];
    snprintf(forWhom, sizeof(forWhom), "%s|%s", domain, s.shopClientId);
    if (P.shopToken[0] && !strcmp(P.shopTokenFor, forWhom) && (int32_t)(P.shopTokenUntil - mono()) > 300){
        snprintf(token, size, "%s", P.shopToken);
        return true;
    }
    char url[160], id[100], secret[170], form[320];
    snprintf(url, sizeof(url), "https://%s/admin/oauth/access_token", domain);
    mtb_Kit_Url_Encode(id, sizeof(id), s.shopClientId);
    mtb_Kit_Url_Encode(secret, sizeof(secret), s.shopSecret);
    const int n = snprintf(form, sizeof(form), "grant_type=client_credentials&client_id=%s&client_secret=%s", id, secret);
    const char* headers[] = {"Content-Type", "application/x-www-form-urlencoded", "Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Send("POST", url, headers, form, (size_t)n, nullptr, 0, 8192, r, UA);
    memset(secret, 0, sizeof(secret));
    memset(form, 0, sizeof(form));
    uint32_t expires = 0;
    state = bcc_Parse_Shopify_Token(body, r.length, r.code, P.shopToken, sizeof(P.shopToken), expires, work.text, sizeof(work.text));
    heap_caps_free(body);
    if (state != BCC_OK){ P.shopToken[0] = '\0'; return false; }
    P.shopTokenUntil = mono() + expires;
    snprintf(P.shopTokenFor, sizeof(P.shopTokenFor), "%s", forWhom);
    snprintf(token, size, "%s", P.shopToken);
    return true;
}

static uint8_t pass_Shopify(const Bcc_Settings_t& s, uint32_t midnight, Bcc_Sales_t& work){
    char domain[80];
    if (!bcc_Shop_Domain(s.shopStore, domain, sizeof(domain))){ snprintf(work.text, sizeof(work.text), "Not a store name: use name.myshopify.com"); return BCC_KEY; }
    char token[96];
    uint8_t state = BCC_OK;
    if (!shopify_Token(s, domain, token, sizeof(token), work, state)) return state;
    char url[140], since[24], query[900];
    snprintf(url, sizeof(url), "https://%s/admin/api/2026-10/graphql.json", domain);
    bcc_Iso(since, sizeof(since), midnight);
    const char* headers[] = {"Content-Type", "application/json", "X-Shopify-Access-Token", token, nullptr};
    Bcc_Page_t page = {};
    char plan[32] = "";
    uint8_t dev = 0;
    for (int p = 0; p < SHOP_PAGES; p++){
        const size_t n = bcc_Shopify_Query(query, sizeof(query), since, p ? page.cursor : "");
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Send("POST", url, headers, query, n, nullptr, 0, BODY_MAX, r, UA);
        state = bcc_Parse_Shopify(body, r.length, r.code, work, page, work.text, sizeof(work.text));
        heap_caps_free(body);
        if (p == 0){ memcpy(plan, work.plan, sizeof(plan)); dev = work.test; }
        if (r.code == 401) P.shopToken[0] = '\0';                   // ask for a new one next time
        if (state != BCC_OK || !page.more) break;
        if (p == SHOP_PAGES - 1) work.more = 1;
    }
    memset(token, 0, sizeof(token));
    memcpy(work.plan, plan, sizeof(plan));
    work.test = dev;
    return state;
}

static void run_Shop(const Bcc_Settings_t& s, uint8_t shop, uint32_t now){
    uint32_t day;
    const uint32_t midnight = local_Midnight(now, &day);
    Bcc_Sales_t* work = (Bcc_Sales_t*)heap_caps_calloc(1, sizeof(Bcc_Sales_t), MALLOC_CAP_SPIRAM);
    if (!work){ schedule(shop, BCC_ERROR, SHOP_SECONDS); return; }
    uint8_t state = shop == BCC_STRIPE ? pass_Stripe(s, midnight, *work) : shop == BCC_PAYSTACK ? pass_Paystack(s, midnight, *work)
                                                                                                 : pass_Shopify(s, midnight, *work);
    lock();
    Bcc_Sales_t& out = N.status.shops[shop];
    if (state == BCC_OK){
        work->state = BCC_OK;
        work->day = day;
        work->updated = now;
        work->text[0] = '\0';
        out = *work;
    } else {
        // The last good numbers stay (on the same day), with why they are not new.
        out.state = state;
        memcpy(out.text, work->text, sizeof(out.text));
        if (work->plan[0]) memcpy(out.plan, work->plan, sizeof(out.plan));
        if (out.day != day){ out.count = 0; out.moneyCount = 0; out.lastAt = 0; out.updated = 0; out.day = day; }
    }
    N.version++;
    unlock();
    if (state != BCC_OK) ESP_LOGW(TAG, "%s: %s", bcc_Source_Name(shop), work->text);
    heap_caps_free(work);
    schedule(shop, state, SHOP_SECONDS);
}

// Creators ----------------------------------------------------------------------------------------------------------
static void set_Creator(uint8_t source, uint8_t state, const char* why, const Bcc_Creator_t* values){
    lock();
    Bcc_Creator_t& c = N.status.creator;
    if (values && state == BCC_OK){
        switch (source){
            case BCC_YOUTUBE: c.subs = values->subs; c.views = values->views; c.videos = values->videos; c.subsHidden = values->subsHidden;
                              memcpy(c.channel, values->channel, sizeof(c.channel)); break;
            case BCC_GITHUB: c.stars = values->stars; c.forks = values->forks; c.followers = values->followers;
                             memcpy(c.repoStars, values->repoStars, sizeof(c.repoStars)); memcpy(c.repoName, values->repoName, sizeof(c.repoName));
                             c.repoCount = values->repoCount; c.ghLimit = values->ghLimit; break;
            default: break;
        }
        c.updated[source] = (uint32_t)time(nullptr);
    }
    c.state[source] = state;
    snprintf(c.text[source], sizeof(c.text[source]), "%s", state == BCC_OK ? "" : (why ? why : ""));
    N.version++;
    unlock();
}

static void run_Youtube(const Bcc_Settings_t& s){
    char channel[128], url[320];
    const char* ch = s.ytChannel;
    const bool id = strlen(ch) == 24 && ch[0] == 'U' && ch[1] == 'C';
    if (id) snprintf(channel, sizeof(channel), "id=%s", ch);
    else {
        char handle[70], encoded[100];
        snprintf(handle, sizeof(handle), "%s%s", ch[0] == '@' ? "" : "@", ch);
        mtb_Kit_Url_Encode(encoded, sizeof(encoded), handle);
        snprintf(channel, sizeof(channel), "forHandle=%s", encoded);
    }
    snprintf(url, sizeof(url), "https://www.googleapis.com/youtube/v3/channels?part=snippet,statistics&%s&key=%s", channel, s.ytKey);
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 64 * 1024, r, UA);
    memset(url, 0, sizeof(url));
    Bcc_Creator_t* c = (Bcc_Creator_t*)heap_caps_calloc(1, sizeof(Bcc_Creator_t), MALLOC_CAP_SPIRAM);
    char why[96] = "";
    uint8_t state = c ? bcc_Parse_Youtube(body, r.length, r.code, *c, why, sizeof(why)) : BCC_ERROR;
    heap_caps_free(body);
    set_Creator(BCC_YOUTUBE, state, why, c);
    heap_caps_free(c);
    // The quota starts again at midnight Pacific time; an hour's wait is plenty either way.
    schedule(BCC_YOUTUBE, state, 300, 3600);
}

static void run_Github(const Bcc_Settings_t& s){
    char auth[120] = "";
    if (s.ghToken[0]) snprintf(auth, sizeof(auth), "Bearer %s", s.ghToken);
    const char* headers[] = {"Accept", "application/vnd.github+json", "X-GitHub-Api-Version", "2022-11-28",
                             s.ghToken[0] ? "Authorization" : nullptr, auth, nullptr};
    const char* collect[] = {"X-RateLimit-Limit", "X-RateLimit-Remaining", "X-RateLimit-Reset"};
    Bcc_Creator_t* c = (Bcc_Creator_t*)heap_caps_calloc(1, sizeof(Bcc_Creator_t), MALLOC_CAP_SPIRAM);
    if (!c){ schedule(BCC_GITHUB, BCC_ERROR, 600); return; }
    uint8_t state = BCC_OK;
    char why[96] = "";
    int requests = 0;
    uint32_t resetAt = 0;
    long limit = 0, remaining = -1;
    for (int i = -1; i < BCC_REPOS && state != BCC_LIMIT; i++){
        char url[160];
        if (i < 0){ if (!s.ghUser[0]) continue; snprintf(url, sizeof(url), "https://api.github.com/users/%s", s.ghUser); }
        else { if (!s.ghRepos[i][0]) continue; snprintf(url, sizeof(url), "https://api.github.com/repos/%s", s.ghRepos[i]); }
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Get(url, headers, collect, 3, 128 * 1024, r, UA);
        requests++;
        char w[96] = "";
        const uint8_t st = bcc_Parse_Github(body, r.length, r.code, i, *c, w, sizeof(w));
        heap_caps_free(body);
        if (r.header[0][0]) limit = atol(r.header[0]);
        if (r.header[1][0]) remaining = atol(r.header[1]);
        if (r.header[2][0]) resetAt = (uint32_t)atol(r.header[2]);
        if (st == BCC_OK){ if (i >= 0){ c->stars += c->repoStars[i]; c->repoCount++; } }
        else if (state == BCC_OK || st == BCC_LIMIT){ state = st; snprintf(why, sizeof(why), "%.40s: %.50s", i >= 0 ? s.ghRepos[i] : s.ghUser, w); }
    }
    memset(auth, 0, sizeof(auth));
    c->ghLimit = (uint16_t)(limit > 0 && limit < 65535 ? limit : 0);
    // Some repos failing still shows the rest, with what went wrong.
    const bool partial = state != BCC_OK && state != BCC_LIMIT && (c->repoCount || c->followers);
    set_Creator(BCC_GITHUB, partial ? (uint8_t)BCC_OK : state, why, c);
    if (partial){ lock(); snprintf(N.status.creator.text[BCC_GITHUB], sizeof(N.status.creator.text[0]), "%.63s", why); N.version++; unlock(); state = BCC_OK; }
    heap_caps_free(c);
    // Use at most half of the hour's allowance: every 2 minutes with a token, every 10 or more without.
    const uint32_t every = limit >= 1000 ? 120 : (uint32_t)(requests * 120 > 600 ? requests * 120 : 600);
    const uint32_t now = (uint32_t)time(nullptr);
    const uint32_t wait = (remaining == 0 && resetAt > now) ? resetAt - now + 5 : 0;
    schedule(BCC_GITHUB, wait ? (uint8_t)BCC_LIMIT : state, every, wait ? wait : 900);
}

static void run_Plausible(const Bcc_Settings_t& s, bool live, bool today){
    char auth[100];
    snprintf(auth, sizeof(auth), "Bearer %s", s.plKey);
    char site[100];
    mtb_Kit_Url_Encode(site, sizeof(site), s.plSite);
    Bcc_Creator_t* c = (Bcc_Creator_t*)heap_caps_calloc(1, sizeof(Bcc_Creator_t), MALLOC_CAP_SPIRAM);
    if (!c){ schedule(BCC_PLAUSIBLE, BCC_ERROR, 60); return; }
    uint8_t state = BCC_OK;
    char why[96] = "";
    if (live){
        char url[220];
        snprintf(url, sizeof(url), "https://%s/api/v1/stats/realtime/visitors?site_id=%s", s.plHost, site);
        const char* headers[] = {"Authorization", auth, nullptr};
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 4096, r, UA);
        state = bcc_Parse_Plausible_Live(body, r.length, r.code, *c, why, sizeof(why));
        heap_caps_free(body);
        if (state == BCC_OK){ lock(); N.status.creator.live = c->live; unlock(); }
    }
    if (today && state == BCC_OK){
        char url[160], q[200];
        snprintf(url, sizeof(url), "https://%s/api/v2/query", s.plHost);
        JsonDocument doc;
        doc["site_id"] = s.plSite;
        doc["metrics"].add("visitors");
        doc["metrics"].add("pageviews");
        doc["date_range"] = "day";
        const size_t n = serializeJson(doc, q, sizeof(q));
        const char* headers[] = {"Authorization", auth, "Content-Type", "application/json", nullptr};
        Mtb_Kit_Http_t r;
        char* body = mtb_Kit_Http_Send("POST", url, headers, q, n, nullptr, 0, 16384, r, UA);
        state = bcc_Parse_Plausible_Today(body, r.length, r.code, *c, why, sizeof(why));
        heap_caps_free(body);
        if (state == BCC_OK){ lock(); N.status.creator.visitors = c->visitors; N.status.creator.pageviews = c->pageviews; unlock(); }
        if (state == BCC_OK) P.todayDue = mono() + 300;
    }
    memset(auth, 0, sizeof(auth));
    set_Creator(BCC_PLAUSIBLE, state, why, nullptr);
    if (state == BCC_OK){ lock(); N.status.creator.updated[BCC_PLAUSIBLE] = (uint32_t)time(nullptr); unlock(); }
    heap_caps_free(c);
    schedule(BCC_PLAUSIBLE, state, 60);
}

// The worker --------------------------------------------------------------------------------------------------------
// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static Bcc_Settings_t* s;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Bcc_Settings_t*)heap_caps_malloc(sizeof(Bcc_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s){ N.running = false; return; }
}

static void worker_End(void){
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    N.running = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!N.running) return;
    if (N.stop){ worker_End(); return; }
    if (millis() - wk_StepAt < 500) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {
        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u || !Mtb_Applications::internetConnectStatus){ wk_WakeAt = millis() + 1000; continue; }
        // The latest settings, and a refresh request.
        lock();
        *s = *N.settings;
        const bool refresh = N.refresh;
        const uint8_t ask = N.askNow;
        N.refresh = false;
        N.askNow = 0;
        unlock();
        if (refresh){ memset(P.due, 0, sizeof(P.due)); memset(P.fails, 0, sizeof(P.fails)); P.liveDue = P.todayDue = 0; }
        for (int i = 0; i < BCC_SOURCES; i++) if (ask & (1u << i)){ P.due[i] = 0; P.fails[i] = 0; if (i == BCC_PLAUSIBLE) P.todayDue = 0; }
        for (uint8_t src = 0; src < BCC_SOURCES && !N.stop; src++){
            if (!bcc_Source_Ready(*s, src)){
                lock();
                if (src < BCC_SHOPS){ if (N.status.shops[src].state != BCC_OFF){ memset(&N.status.shops[src], 0, sizeof(Bcc_Sales_t)); N.version++; } }
                else if (N.status.creator.state[src] != BCC_OFF){ N.status.creator.state[src] = BCC_OFF; N.status.creator.text[src][0] = '\0'; N.version++; }
                unlock();
                P.due[src] = 0;
                continue;
            }
            if ((int32_t)(mono() - P.due[src]) < 0) continue;
            lock();
            if (src < BCC_SHOPS){ if (N.status.shops[src].state == BCC_OFF){ N.status.shops[src].state = BCC_WAIT; N.version++; } }
            else if (N.status.creator.state[src] == BCC_OFF){ N.status.creator.state[src] = BCC_WAIT; N.version++; }
            unlock();
            switch (src){
                case BCC_STRIPE: case BCC_PAYSTACK: case BCC_SHOPIFY: run_Shop(*s, src, (uint32_t)time(nullptr)); break;
                case BCC_YOUTUBE: run_Youtube(*s); break;
                case BCC_GITHUB: run_Github(*s); break;
                case BCC_PLAUSIBLE: run_Plausible(*s, true, (int32_t)(mono() - P.todayDue) >= 0); break;
            }
        }
        wk_WakeAt = millis() + 1000;
    } while (0);
}

bool bcc_Net_Start(const Bcc_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Bcc_Settings_t*)heap_caps_calloc(1, sizeof(Bcc_Settings_t), MALLOC_CAP_SPIRAM);
        if (!N.settings) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(&N.status, 0, sizeof(N.status));
    N.status.creator.live = N.status.creator.visitors = N.status.creator.pageviews = -1;
    N.version++;
    unlock();
    memset(&P, 0, sizeof(P));
    N.stop = false;
    N.refresh = false;
    N.askNow = 0;
    N.running = true;
    worker_Begin();
    if (N.running) pxl_compat_add_background(worker_Step);
    return true;
}

void bcc_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    lock();
    memset(N.settings, 0, sizeof(Bcc_Settings_t));      // no keys left in memory
    unlock();
    memset(P.shopToken, 0, sizeof(P.shopToken));
}

void bcc_Net_Set_Settings(const Bcc_Settings_t& settings){
    if (!made) return;
    lock();
    // A source whose settings changed is asked at once.
    const Bcc_Settings_t& old = *N.settings;
    const bool shops[BCC_SOURCES] = {
        old.on[0] != settings.on[0] || strcmp(old.stripeKey, settings.stripeKey) != 0,
        old.on[1] != settings.on[1] || strcmp(old.paystackKey, settings.paystackKey) != 0,
        old.on[2] != settings.on[2] || strcmp(old.shopStore, settings.shopStore) || strcmp(old.shopClientId, settings.shopClientId)
            || strcmp(old.shopSecret, settings.shopSecret),
        old.on[3] != settings.on[3] || strcmp(old.ytKey, settings.ytKey) || strcmp(old.ytChannel, settings.ytChannel),
        old.on[4] != settings.on[4] || strcmp(old.ghToken, settings.ghToken) || memcmp(old.ghRepos, settings.ghRepos, sizeof(old.ghRepos))
            || strcmp(old.ghUser, settings.ghUser),
        old.on[5] != settings.on[5] || strcmp(old.plKey, settings.plKey) || strcmp(old.plSite, settings.plSite) || strcmp(old.plHost, settings.plHost)};
    *N.settings = settings;
    for (int i = 0; i < BCC_SOURCES; i++) if (shops[i]) N.askNow = (uint8_t)(N.askNow | (1u << i));
    unlock();
}

void bcc_Net_Refresh(void){ N.refresh = true; }

bool bcc_Net_Copy(Bcc_Status_t& status, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = N.version != version;
    if (changed){ status = N.status; version = N.version; }
    unlock();
    return changed;
}
