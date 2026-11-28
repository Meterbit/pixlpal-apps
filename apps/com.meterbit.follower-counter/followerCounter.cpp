/**
 * Follower Counter: up to four counts, big, rolling up when they change: YouTube subscribers (the owner's YouTube Data
 * API key: channels?part=statistics,snippet), GitHub followers (/users/{u}) and stars (/repos/{o}/{r}), Bluesky
 * followers (the public AppView's getProfile) and Mastodon followers (/api/v1/accounts/lookup on the larger servers,
 * listed in app.json). With the gain since midnight, the next milestone (100, 250, 500, 1,000, 2,500...) and confetti
 * with a fanfare when one is passed. One at a time, big, or all together. Each count every 10 minutes (GitHub allows
 * 60 checks an hour without a key). Knob: turn for the counter. NVS "folSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "fol_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define SLOTS       4
#define NVS_KEY     "folSave"
#define SAVE_MAGIC  0x464F4C01u
#define UA          "Pixlpal-FollowerCounter/1.0 (https://github.com/Meterbit/pixlpal-apps)"
#define EVERY_MS    600000u
#define ROLL_MS     700u
#define CONFETTI_MS 4500u
#define CONFETTI_N  48

enum { K_NONE = 0, K_YOUTUBE, K_GH_FOLLOWERS, K_GH_STARS, K_BLUESKY, K_MASTODON, KINDS };
static const char* const KIND_NAME[KINDS] = {"", "YOUTUBE", "GITHUB", "GITHUB", "BLUESKY", "MASTODON"};
static const char* const KIND_UNIT[KINDS] = {"", "SUBSCRIBERS", "FOLLOWERS", "STARS", "FOLLOWERS", "FOLLOWERS"};
static const uint16_t KIND_COLOR[KINDS] = {0, MTB_KIT_RGB(255, 40, 40), MTB_KIT_RGB(200, 200, 210), MTB_KIT_RGB(255, 200, 60),
                                           MTB_KIT_RGB(40, 140, 255), MTB_KIT_RGB(140, 100, 255)};
// The Mastodon servers app.json lists (the app reaches only those).
static const char* const MASTODON[] = {"mastodon.social", "mastodon.online", "mstdn.social", "mas.to", "mastodon.world", "fosstodon.org",
    "hachyderm.io", "infosec.exchange", "techhub.social", "mastodon.art", "mstdn.jp", "universeodon.com", "social.vivaldi.net",
    "mastodon.ie", "aus.social", "mastodon.nz", "toot.community", "sfba.social", "piaille.fr", "mastodon.uno", "chaos.social"};

EXT_RAM_BSS_ATTR TaskHandle_t followerCounter_Task_H = NULL;
static void followerCounter_App_Task(void* dApplication);
static void fol_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* followerCounter_App;
Mtb_Applications* followerCounter_App_GetInstance(){
    if (!followerCounter_App){
        followerCounter_App = new Mtb_Applications_FullScreen(followerCounter_App_Task, &followerCounter_Task_H, "Follower Counter", {0,0}, 8192);
        followerCounter_App->mtb_App_Set_Mobile_UI(FOL_UI_MANIFEST, FOL_UI_API, nullptr, fol_Values);
    }
    return followerCounter_App;
}
MTB_REGISTER_APP(followerCounter_App, 0, 0)     // the catalogue gives the app its number

struct Fol_Slot_t {
    uint8_t kind, pad[3];
    char account[72];
    char label[28];
    int64_t last;                       // the last count (-1 none)
    int64_t baseline;                   // the count at the day's first check
    int32_t baseDay;                    // that day (local days since 1970)
};
struct Fol_Save_t {
    uint32_t magic;
    uint8_t layout, seconds, celebrate, pad;
    char ytKey[48];
    Fol_Slot_t slot[SLOTS];
};

struct Fol_Live_t { int64_t value; char name[40]; char why[40]; uint32_t atMs; };
struct Fol_Shared_t {
    Fol_Live_t live[SLOTS];
    uint32_t version;
    volatile bool stop, running, checkNow;
    volatile bool redo[SLOTS];
};

static EXT_RAM_BSS_ATTR Fol_Save_t save;
static EXT_RAM_BSS_ATTR Fol_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false;

static void read_Save(Fol_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC; s.seconds = 10; s.celebrate = 1;
        for (auto& k : s.slot){ k.last = -1; k.baseline = -1; }
    }
    s.layout = s.layout ? 1 : 0;
    if (s.seconds < 5 || s.seconds > 60) s.seconds = 10;
    s.celebrate = s.celebrate ? 1 : 0;
    s.ytKey[sizeof(s.ytKey) - 1] = '\0';
    for (auto& k : s.slot){
        if (k.kind >= KINDS) k.kind = K_NONE;
        k.account[sizeof(k.account) - 1] = k.label[sizeof(k.label) - 1] = '\0';
    }
}

// Accounts -----------------------------------------------------------------------------------------------------------------------
static bool all_Of(const char* s, const char* extra){
    if (!*s) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr(extra, *s)) return false;
    return true;
}

/** The account as the counter's service wants it, from what the owner typed (a link works too); false if it isn't one. */
static bool normal_Account(uint8_t kind, const char* in, char* out, size_t n){
    char t[96];
    while (*in == ' ') in++;
    snprintf(t, sizeof(t), "%s", in);
    size_t len = strlen(t);
    while (len && (t[len - 1] == ' ' || t[len - 1] == '/')) t[--len] = '\0';
    const char* p = t;
    if (!strncasecmp(p, "https://", 8)) p += 8; else if (!strncasecmp(p, "http://", 7)) p += 7;
    if (!strncasecmp(p, "www.", 4)) p += 4;
    switch (kind){
        case K_YOUTUBE: {
            if (!strncasecmp(p, "youtube.com/", 12)){ p += 12; if (!strncasecmp(p, "channel/", 8)) p += 8; }
            if (*p == '@'){ if (!all_Of(p + 1, "._-") || strlen(p) > 40) return false; }
            else if (!(strlen(p) == 24 && !strncmp(p, "UC", 2) && all_Of(p, "_-"))){
                if (!all_Of(p, "._-") || strlen(p) > 39) return false;
                snprintf(out, n, "@%s", p);
                return true;
            }
            snprintf(out, n, "%s", p);
            return true;
        }
        case K_GH_FOLLOWERS:
            if (!strncasecmp(p, "github.com/", 11)) p += 11;
            if (*p == '@') p++;
            if (!all_Of(p, "-") || strlen(p) > 39) return false;
            snprintf(out, n, "%s", p);
            return true;
        case K_GH_STARS: {
            if (!strncasecmp(p, "github.com/", 11)) p += 11;
            const char* slash = strchr(p, '/');
            if (!slash || strchr(slash + 1, '/') || slash == p || !slash[1]) return false;
            char owner[48], repo[104];
            snprintf(owner, sizeof(owner), "%.*s", (int)(slash - p), p);
            snprintf(repo, sizeof(repo), "%s", slash + 1);
            if (!all_Of(owner, "-") || !all_Of(repo, "._-") || strlen(p) > 70) return false;
            snprintf(out, n, "%s/%s", owner, repo);
            return true;
        }
        case K_BLUESKY:
            if (!strncasecmp(p, "bsky.app/profile/", 17)) p += 17;
            if (*p == '@') p++;
            if (!all_Of(p, ".-") || !strchr(p, '.') || strlen(p) > 64) return false;
            snprintf(out, n, "%s", p);
            for (char* q = out; *q; q++) *q = (char)tolower((unsigned char)*q);
            return true;
        case K_MASTODON: {
            // "@you@server", "you@server", or "server/@you".
            char user[48] = "", server[64] = "";
            const char* at = strstr(p, "/@");
            if (at){ snprintf(server, sizeof(server), "%.*s", (int)(at - p), p); snprintf(user, sizeof(user), "%s", at + 2); }
            else {
                if (*p == '@') p++;
                const char* a = strchr(p, '@');
                if (!a) return false;
                snprintf(user, sizeof(user), "%.*s", (int)(a - p), p);
                snprintf(server, sizeof(server), "%s", a + 1);
            }
            for (char* q = server; *q; q++) *q = (char)tolower((unsigned char)*q);
            if (!all_Of(user, "_.") ) return false;
            bool known = false;
            for (const char* m : MASTODON) if (!strcmp(m, server)) known = true;
            if (!known) return false;
            snprintf(out, n, "@%s@%s", user, server);
            return true;
        }
    }
    return false;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
/** The slot named in a command's key ("kind2": 2), or -1. */
static int slot_Of(JsonDocument& c, const char* base, JsonVariant& v){
    char key[16];
    for (int k = 0; k < SLOTS; k++){
        snprintf(key, sizeof(key), "%s%d", base, k);
        if (!c[key].isNull()){ v = c[key]; return k; }
    }
    return -1;
}
static void slot_Changed(int k){
    save.slot[k].last = -1; save.slot[k].baseline = -1; save.slot[k].baseDay = 0;
    sh.live[k].value = -1; sh.live[k].name[0] = sh.live[k].why[0] = '\0';
    sh.redo[k] = true;
    phoneChanged = saveWanted = true;
}
static void cmd_Kind(JsonDocument& c){
    JsonVariant v;
    const int k = slot_Of(c, "kind", v);
    long x;
    if (k < 0 || !mtb_Kit_Json_Range(v, 0, KINDS - 1, x)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    if (save.slot[k].kind != (uint8_t)x){
        save.slot[k].kind = (uint8_t)x;
        char a[72];
        if (save.slot[k].account[0] && normal_Account((uint8_t)x, save.slot[k].account, a, sizeof(a))) snprintf(save.slot[k].account, sizeof(save.slot[k].account), "%s", a);
        else save.slot[k].account[0] = '\0';
        slot_Changed(k);
    }
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Account(JsonDocument& c){
    JsonVariant v;
    const int k = slot_Of(c, "account", v);
    const char* a = v.as<const char*>();
    if (k < 0 || !a){ mtb_Ble_App_Cmd_Reject(); return; }
    char out[72] = "";
    if (a[0] && (save.slot[k].kind == K_NONE || !normal_Account(save.slot[k].kind, a, out, sizeof(out)))){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.slot[k].account, sizeof(save.slot[k].account), "%s", out);
    slot_Changed(k);
    portEXIT_CRITICAL(&lock);
}
static void cmd_Label(JsonDocument& c){
    JsonVariant v;
    const int k = slot_Of(c, "label", v);
    const char* a = v.as<const char*>();
    if (k < 0 || !a){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); mtb_Kit_To_Ascii(save.slot[k].label, sizeof(save.slot[k].label), a); portEXIT_CRITICAL(&lock);
    phoneChanged = saveWanted = true;
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["ytKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.ytKey) || (v[0] && !all_Of(v, "_-"))){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.ytKey, sizeof(save.ytKey), "%s", v); portEXIT_CRITICAL(&lock);
    for (int k = 0; k < SLOTS; k++) if (save.slot[k].kind == K_YOUTUBE) sh.redo[k] = true;
    phoneChanged = saveWanted = true;
}
static void cmd_Layout(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["layout"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.layout = (uint8_t)v; phoneChanged = saveWanted = true; }
static void cmd_Seconds(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seconds"], 5, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.seconds = (uint8_t)v; phoneChanged = saveWanted = true; }
static void cmd_Celebrate(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["celebrate"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.celebrate = (uint8_t)v; phoneChanged = saveWanted = true; }
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) turns += 1; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, JsonDocument& doc, JsonDocument& filter, bool github){
    const char* gh[] = {"Accept", "application/vnd.github+json", "X-GitHub-Api-Version", "2022-11-28", nullptr};
    const char* plain[] = {"Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, github ? gh : plain, nullptr, 0, 24000, r, UA);
    int code = r.code;
    if (code == 200 && body && deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) code = -100;
    heap_caps_free(body);
    return code;
}

static int64_t big_Number(JsonVariantConst v){
    if (v.is<const char*>()) return strtoll(v.as<const char*>(), nullptr, 10);
    if (v.is<int64_t>() || v.is<long>() || v.is<int>()) return v.as<int64_t>();
    return -1;
}

static int fetch(const Fol_Slot_t& s, const char* ytKey, Fol_Live_t& out){
    char url[256], enc[160];
    JsonDocument doc, filter;
    int code = -1;
    switch (s.kind){
        case K_YOUTUBE: {
            if (!ytKey[0]) return -50;
            mtb_Kit_Url_Encode(enc, sizeof(enc), s.account);
            snprintf(url, sizeof(url), "https://www.googleapis.com/youtube/v3/channels?part=statistics,snippet&%s=%s&key=%s",
                     s.account[0] == '@' ? "forHandle" : "id", enc, ytKey);
            JsonObject f = filter["items"].add<JsonObject>();
            f["snippet"]["title"] = true; f["statistics"]["subscriberCount"] = true; f["statistics"]["hiddenSubscriberCount"] = true;
            code = get_Json(url, doc, filter, false);
            if (code != 200) return code;
            JsonVariantConst it = doc["items"][0];
            if (it.isNull()) return -51;
            if (it["statistics"]["hiddenSubscriberCount"] | false) return -52;
            out.value = big_Number(it["statistics"]["subscriberCount"]);
            mtb_Kit_To_Ascii(out.name, sizeof(out.name), it["snippet"]["title"] | "");
            break;
        }
        case K_GH_FOLLOWERS:
            snprintf(url, sizeof(url), "https://api.github.com/users/%s", s.account);
            filter["followers"] = true; filter["name"] = true; filter["login"] = true;
            code = get_Json(url, doc, filter, true);
            if (code != 200) return code;
            out.value = big_Number(doc["followers"]);
            mtb_Kit_To_Ascii(out.name, sizeof(out.name), doc["name"].is<const char*>() ? doc["name"].as<const char*>() : (doc["login"] | s.account));
            break;
        case K_GH_STARS:
            snprintf(url, sizeof(url), "https://api.github.com/repos/%s", s.account);
            filter["stargazers_count"] = true; filter["full_name"] = true;
            code = get_Json(url, doc, filter, true);
            if (code != 200) return code;
            out.value = big_Number(doc["stargazers_count"]);
            mtb_Kit_To_Ascii(out.name, sizeof(out.name), doc["full_name"] | s.account);
            break;
        case K_BLUESKY:
            mtb_Kit_Url_Encode(enc, sizeof(enc), s.account);
            snprintf(url, sizeof(url), "https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=%s", enc);
            filter["followersCount"] = true; filter["displayName"] = true; filter["handle"] = true;
            code = get_Json(url, doc, filter, false);
            if (code != 200) return code;
            out.value = big_Number(doc["followersCount"]);
            mtb_Kit_To_Ascii(out.name, sizeof(out.name), doc["displayName"].is<const char*>() && doc["displayName"].as<const char*>()[0] ? doc["displayName"].as<const char*>() : (doc["handle"] | s.account));
            break;
        case K_MASTODON: {
            const char* at = strchr(s.account + 1, '@');
            if (!at) return -51;
            char user[48];
            snprintf(user, sizeof(user), "%.*s", (int)(at - s.account - 1), s.account + 1);
            snprintf(url, sizeof(url), "https://%s/api/v1/accounts/lookup?acct=%s", at + 1, user);
            filter["followers_count"] = true; filter["display_name"] = true; filter["username"] = true;
            code = get_Json(url, doc, filter, false);
            if (code != 200) return code;
            out.value = big_Number(doc["followers_count"]);
            mtb_Kit_To_Ascii(out.name, sizeof(out.name), doc["display_name"].is<const char*>() && doc["display_name"].as<const char*>()[0] ? doc["display_name"].as<const char*>() : (doc["username"] | user));
            break;
        }
        default: return -1;
    }
    return out.value >= 0 ? 200 : -100;
}

static void why_Of(char* t, size_t n, uint8_t kind, int code){
    if (code == -50) snprintf(t, n, "ADD A YOUTUBE KEY IN THE PHONE APP");
    else if (code == -51 || code == 404) snprintf(t, n, "NO SUCH ACCOUNT");
    else if (code == -52) snprintf(t, n, "THE CHANNEL HIDES ITS COUNT");
    else if (code == 400 && kind == K_BLUESKY) snprintf(t, n, "NO SUCH ACCOUNT");
    else if ((code == 400 || code == 403) && kind == K_YOUTUBE) snprintf(t, n, "YOUTUBE REFUSED THE KEY");
    else if ((code == 403 || code == 429) && (kind == K_GH_FOLLOWERS || kind == K_GH_STARS)) snprintf(t, n, "GITHUB SAYS WAIT A WHILE");
    else if (code == 429) snprintf(t, n, "TOO MANY CHECKS: WAITING");
    else if (code <= 0) snprintf(t, n, "NO ANSWER");
    else snprintf(t, n, "HTTP %d", code);
}

static int32_t today(void){
    const time_t now = time(nullptr);
    if (now < 1700000000) return 0;
    struct tm l;
    localtime_r(&now, &l);
    return mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

static void worker(void*){
    uint32_t nextMs[SLOTS] = {0};
    while (!sh.stop){
        if (sh.checkNow){ sh.checkNow = false; for (auto& n : nextMs) n = 0; }
        bool did = false;
        for (int k = 0; k < SLOTS && !sh.stop; k++){
            if (sh.redo[k]){ sh.redo[k] = false; nextMs[k] = 0; }
            if (nextMs[k] && (int32_t)(millis() - nextMs[k]) < 0) continue;
            Fol_Slot_t s;
            char key[48];
            portENTER_CRITICAL(&lock); s = save.slot[k]; memcpy(key, save.ytKey, sizeof(key)); portEXIT_CRITICAL(&lock);
            if (s.kind == K_NONE || !s.account[0]){ nextMs[k] = millis() + 2000; continue; }
            Fol_Live_t o = {};
            o.value = -1;
            const int code = fetch(s, key, o);
            portENTER_CRITICAL(&lock);
            // Only if the slot still holds that account.
            if (save.slot[k].kind == s.kind && !strcmp(save.slot[k].account, s.account)){
                Fol_Live_t& l = sh.live[k];
                if (code == 200){
                    l.value = o.value; snprintf(l.name, sizeof(l.name), "%s", o.name); l.why[0] = '\0'; l.atMs = millis();
                    Fol_Slot_t& ss = save.slot[k];
                    const int32_t day = today();
                    if (day && (ss.baseDay != day || ss.baseline < 0)){ ss.baseDay = day; ss.baseline = o.value; }
                    if (ss.last != o.value){ ss.last = o.value; saveWanted = true; }
                } else why_Of(l.why, sizeof(l.why), s.kind, code);
                sh.version++;
            }
            portEXIT_CRITICAL(&lock);
            nextMs[k] = millis() + (code == 200 ? EVERY_MS : code == -50 ? 5000u : code == 403 || code == 429 ? 1800000u : 120000u);
            did = true;
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        if (!did) vTaskDelay(pdMS_TO_TICKS(300));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Numbers and milestones ---------------------------------------------------------------------------------------------------------------
static void thousands(char* t, size_t n, int64_t v){
    char raw[24];
    snprintf(raw, sizeof(raw), "%lld", (long long)(v < 0 ? -v : v));
    const int len = (int)strlen(raw);
    size_t o = 0;
    if (v < 0 && o + 1 < n) t[o++] = '-';
    for (int i = 0; i < len && o + 2 < n; i++){
        t[o++] = raw[i];
        if ((len - 1 - i) % 3 == 0 && i != len - 1) t[o++] = ',';
    }
    t[o] = '\0';
}

/** 1.2K, 34.5K, 1.23M: for the small cells. */
static void short_Number(char* t, size_t n, int64_t v){
    if (v < 10000) thousands(t, n, v);
    else if (v < 999950) snprintf(t, n, "%.1fK", v / 1000.0);                // not "1000.0K"
    else if (v < 999995000) snprintf(t, n, "%.2fM", v / 1000000.0);
    else snprintf(t, n, "%.2fB", v / 1000000000.0);
}

/** The first milestone above @p v: 100, 250, 500, 1,000, 2,500, 5,000 ... */
static int64_t next_Milestone(int64_t v){
    int64_t base = 100;
    for (;;){
        if (v < base) return base;
        if (v < base * 5 / 2) return base * 5 / 2;
        if (v < base * 5) return base * 5;
        base *= 10;
        if (base > 1000000000000LL) return -1;
    }
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 6, 10);

struct Fol_Piece_t { float x, y, vx, vy; uint16_t c; };
static EXT_RAM_BSS_ATTR Fol_Piece_t confetti[CONFETTI_N];
static uint32_t confettiAt = 0;
static int64_t confettiFor = 0;

static void start_Confetti(int64_t milestone){
    static const uint16_t C[6] = {MTB_KIT_RGB(255, 60, 60), MTB_KIT_RGB(255, 200, 40), MTB_KIT_RGB(60, 220, 100), MTB_KIT_RGB(60, 160, 255),
                                  MTB_KIT_RGB(220, 80, 255), MTB_KIT_RGB(255, 255, 255)};
    for (int i = 0; i < CONFETTI_N; i++){
        Fol_Piece_t& p = confetti[i];
        p.x = (float)(pxl_random() % W); p.y = -(float)(pxl_random() % 60);
        p.vx = ((int)(pxl_random() % 200) - 100) / 100.0f; p.vy = 0.5f + (pxl_random() % 100) / 60.0f;
        p.c = C[pxl_random() % 6];
    }
    confettiAt = millis() ? millis() : 1;
    confettiFor = milestone;
}

static void draw_Confetti(uint32_t ms, uint32_t dtMs){
    if (!confettiAt) return;
    if (ms - confettiAt > CONFETTI_MS){ confettiAt = 0; return; }
    const float k = dtMs / 40.0f;
    for (auto& p : confetti){
        p.vy += 0.06f * k; p.x += p.vx * k; p.y += p.vy * k;
        if (p.y >= 0) mtb_Panel_Fill_Rect((int16_t)p.x, (int16_t)p.y, (int16_t)(p.x + 1), (int16_t)(p.y + 1), p.c);
    }
}

static const char* title_Of(const Fol_Slot_t& s, const Fol_Live_t& l){ return s.label[0] ? s.label : l.name[0] ? l.name : s.account; }

/** The number of a big counter, rolling from @p from to @p to as @p t goes 0 to 1. */
static void big_Number_Roll(int y, int64_t from, int64_t to, float t, uint16_t c){
    char a[32], b[32];
    thousands(b, sizeof(b), to);
    const uint8_t* font = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, b) <= W - 8 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
    const int h = font == PT_Sans_Narrow_Bold28x34 ? 34 : 22;
    if (t >= 1 || from < 0 || from == to){ mtb_Kit_Text_Centre(W / 2, y, font, c, b, C_BG); return; }
    thousands(a, sizeof(a), from);
    const float e = 1 - (1 - t) * (1 - t);                  // eases out
    const int off = (int)lroundf(e * (h + 2));
    const int dir = to > from ? 1 : -1;                     // rising numbers roll up
    mtb_Kit_Text_Centre(W / 2, y - dir * off, font, c, a, C_BG);
    mtb_Kit_Text_Centre(W / 2, y + dir * (h + 2 - off), font, c, b, C_BG);
    // Only the number's own band shows.
    mtb_Panel_Fill_Rect(0, (int16_t)(y - h - 4), W - 1, (int16_t)(y - 1), C_BG);
    mtb_Panel_Fill_Rect(0, (int16_t)(y + h), W - 1, (int16_t)(y + 2 * h + 4), C_BG);
}

static void draw_One(const Fol_Save_t& s, const Fol_Shared_t& d, int k, int64_t rollFrom, float rollT){
    const Fol_Slot_t& sl = s.slot[k];
    const Fol_Live_t& l = d.live[k];
    const uint16_t kc = KIND_COLOR[sl.kind];
    const int64_t v = l.value >= 0 ? l.value : sl.last;
    // The number first: the rest is drawn over its edges.
    if (v >= 0) big_Number_Roll(40, rollFrom, v, rollT, KIT_TEXT);
    else mtb_Kit_Text_Centre(W / 2, 52, Terminal6x8, l.why[0] ? KIT_ORANGE : KIT_LABEL, l.why[0] ? l.why : "COUNTING...", C_BG);
    mtb_Panel_Fill_Rect(0, 0, W - 1, 35, C_BG);
    // The service and the account.
    const int bw = mtb_Kit_Text_Width(Terminal4x6, KIND_NAME[sl.kind]) + 6;
    mtb_Panel_Fill_Round_Rect(4, 4, (int16_t)(4 + bw), 12, 2, kc);
    mtb_Kit_Text(7, 6, Terminal4x6, C_BG, KIND_NAME[sl.kind], kc);
    char t[64], f[64];
    snprintf(t, sizeof(t), "%s", title_Of(sl, l));
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal8x12, t, W - 16 - bw);
    mtb_Kit_Text(10 + bw, 3, Terminal8x12, kc, f, C_BG);
    mtb_Kit_Text_Centre(W / 2, 22, Terminal6x8, KIT_LABEL, KIND_UNIT[sl.kind], C_BG);
    if (v < 0) return;
    // Today, and the next milestone.
    if (sl.baseline >= 0 && sl.baseDay == today()){
        const int64_t gain = v - sl.baseline;
        char g[24];
        thousands(g, sizeof(g), gain < 0 ? -gain : gain);
        snprintf(t, sizeof(t), "%s%s TODAY", gain > 0 ? "+" : gain < 0 ? "-" : "", g);
        mtb_Kit_Text_Centre(W / 2, 82, Terminal8x12, gain > 0 ? KIT_GREEN : gain < 0 ? KIT_RED : KIT_LABEL, t, C_BG);
    }
    const int64_t m = next_Milestone(v);
    if (m > 0){
        char a[24], b[24];
        thousands(a, sizeof(a), m);
        thousands(b, sizeof(b), m - v);
        snprintf(t, sizeof(t), "NEXT: %s, %s TO GO", a, b);
        mtb_Kit_Text_Centre(W / 2, 100, Terminal6x8, KIT_DIM, t, C_BG);
        // How far from the last milestone to the next.
        int64_t prev = 0;
        for (int64_t q = 100; q < m; q = next_Milestone(q)) prev = q;
        const float frac = m > prev ? (float)(v - prev) / (float)(m - prev) : 0;
        mtb_Kit_Bar(40, 111, W - 80, 3, frac, kc);
    }
    if (l.why[0]) mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_ORANGE, l.why, C_BG);
}

static void draw_All(const Fol_Save_t& s, const Fol_Shared_t& d, const int* used, int n){
    char t[64], f[64];
    const int cols = n <= 2 ? 1 : 2, rows = n <= 2 ? n : 2;
    const int cw = W / cols, ch = H / rows;
    for (int i = 0; i < n; i++){
        const int k = used[i];
        const Fol_Slot_t& sl = s.slot[k];
        const Fol_Live_t& l = d.live[k];
        const int x = (i % cols) * cw, y = (i / cols) * ch;
        const uint16_t kc = KIND_COLOR[sl.kind];
        mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + 2), (int16_t)(y + ch - 2), kc);
        snprintf(t, sizeof(t), "%s", title_Of(sl, l));
        mtb_Kit_Upper(t);
        mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, cw - 10);
        mtb_Kit_Text(x + 6, y + 3, Terminal6x8, kc, f, C_BG);
        const int64_t v = l.value >= 0 ? l.value : sl.last;
        if (v >= 0){
            if (cols == 1) thousands(t, sizeof(t), v); else short_Number(t, sizeof(t), v);
            const uint8_t* font = cols == 1 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
            if (mtb_Kit_Text_Width(font, t) > cw - 12) font = Terminal10x17;
            mtb_Kit_Text(x + 6, y + 13, font, KIT_TEXT, t, C_BG);
            if (sl.baseline >= 0 && sl.baseDay == today() && v != sl.baseline){
                char g[24];
                thousands(g, sizeof(g), v - sl.baseline > 0 ? v - sl.baseline : sl.baseline - v);
                snprintf(t, sizeof(t), "%s%s", v > sl.baseline ? "+" : "-", g);
                mtb_Kit_Text_Right(x + cw - 4, y + 4, Terminal4x6, v > sl.baseline ? KIT_GREEN : KIT_RED, t, C_BG);
            }
            mtb_Kit_Text(x + 6, y + ch - (cols == 1 ? 12 : 9), Terminal4x6, KIT_DIM, KIND_UNIT[sl.kind], C_BG);
        } else mtb_Kit_Text(x + 6, y + 20, Terminal4x6, l.why[0] ? KIT_ORANGE : KIT_LABEL, l.why[0] ? l.why : "COUNTING...", C_BG);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void followerCounter_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Kind, cmd_Account, cmd_Label, cmd_Key, cmd_Layout, cmd_Seconds, cmd_Celebrate, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = saveWanted = false;
    memset(&sh, 0, sizeof(sh));
    for (auto& l : sh.live) l.value = -1;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "folNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Fol_Shared_t* d = (Fol_Shared_t*)heap_caps_calloc(1, sizeof(Fol_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    for (auto& l : d->live) l.value = -1;
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, shownMs = millis(), lastMs = millis(), rollAt = 0;
    int64_t shownValue[SLOTS], rollFrom = -1;
    for (int k = 0; k < SLOTS; k++) shownValue[k] = save.slot[k].last;
    int index = 0;
    bool push = true;
    confettiAt = 0;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis(), dt = ms - lastMs;
        lastMs = ms;
        if (phoneChanged){ phoneChanged = false; push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; push = true; }
        portEXIT_CRITICAL(&lock);
        Fol_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        int used[SLOTS], n = 0;
        for (int k = 0; k < SLOTS; k++) if (s.slot[k].kind != K_NONE && s.slot[k].account[0]) used[n++] = k;
        // A count that changed: roll it in (show that counter), and celebrate a milestone passed.
        for (int i = 0; i < n; i++){
            const int k = used[i];
            const int64_t v = d->live[k].value;
            if (v < 0 || v == shownValue[k]) continue;
            const int64_t before = shownValue[k];
            if (before >= 0){
                const int64_t m = next_Milestone(before);
                if (s.celebrate && m > 0 && v >= m && confettiFor != m){
                    start_Confetti(m);
                    static const Snd_Note_t FANFARE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 240}, {784, 120}, {1047, 400}};
                    snd_Play(FANFARE, 6, 0.3f, false);
                }
                if (!s.layout){ index = i; shownMs = ms; rollFrom = before; rollAt = ms ? ms : 1; }
            }
            shownValue[k] = v;
        }
        const int t = turns;
        if (t && n){ turns -= t; index = ((index + t) % n + n) % n; shownMs = ms; rollAt = 0; push = true; }
        else if (t) turns -= t;
        if (n && !s.layout && ms - shownMs >= s.seconds * 1000u && !confettiAt){ index = (index + 1) % n; shownMs = ms; rollAt = 0; }
        if (index >= n) index = 0;
        mtb_Panel_Fill_Screen(C_BG);
        if (!n){
            mtb_Kit_Text_Centre(W / 2, 48, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "FOLLOWER COUNTER", C_BG);
            mtb_Kit_Text_Centre(W / 2, 78, Terminal6x8, KIT_LABEL, "ADD AN ACCOUNT IN THE PHONE APP", C_BG);
        } else if (s.layout) draw_All(s, *d, used, n);
        else {
            const float rt = rollAt ? (float)(ms - rollAt) / ROLL_MS : 1.0f;
            if (rt >= 1) rollAt = 0;
            draw_One(s, *d, used[index], rollAt ? rollFrom : -1, rt);
        }
        draw_Confetti(ms, dt);
        if (saveWanted){ saveWanted = false; Fol_Save_t w; portENTER_CRITICAL(&lock); w = save; portEXIT_CRITICAL(&lock); mtb_Write_Nvs_Struct(NVS_KEY, &w, sizeof(w)); }
        if ((push && ms - pushedMs >= 3000) || ms - pushedMs >= 60000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(confettiAt || rollAt ? 20 : 100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void fol_Values(JsonObject values){
    Fol_Save_t s;
    Fol_Shared_t* d = (Fol_Shared_t*)heap_caps_calloc(1, sizeof(Fol_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(d, &sh, sizeof(sh)); portEXIT_CRITICAL(&lock); }
    else { read_Save(s); for (auto& l : d->live) l.value = -1; }
    JsonArray rows = values["counts"].to<JsonArray>();
    char key[16], t[96], n[32];
    for (int k = 0; k < SLOTS; k++){
        const Fol_Slot_t& sl = s.slot[k];
        snprintf(key, sizeof(key), "kind%d", k); values[key] = sl.kind;
        snprintf(key, sizeof(key), "account%d", k); values[key] = sl.account;
        snprintf(key, sizeof(key), "label%d", k); values[key] = sl.label;
        if (sl.kind == K_NONE || !sl.account[0]) continue;
        const Fol_Live_t& l = d->live[k];
        const int64_t v = l.value >= 0 ? l.value : sl.last;
        JsonObject o = rows.add<JsonObject>();
        o["label"] = title_Of(sl, l);
        if (v >= 0){
            thousands(n, sizeof(n), v);
            if (sl.baseline >= 0 && sl.baseDay == today() && v != sl.baseline){
                char g[24];
                thousands(g, sizeof(g), v > sl.baseline ? v - sl.baseline : sl.baseline - v);
                snprintf(t, sizeof(t), "%s %s (%s%s today)", n, KIND_UNIT[sl.kind], v > sl.baseline ? "+" : "-", g);
            } else snprintf(t, sizeof(t), "%s %s", n, KIND_UNIT[sl.kind]);
            for (char* p = t + strlen(n); *p; p++) *p = (char)tolower((unsigned char)*p);
            o["value"] = t; o["state"] = l.why[0] ? "warn" : "ok";
        } else { o["value"] = l.why[0] ? l.why : (appOpen ? "Counting" : "While Follower Counter is open"); o["state"] = l.why[0] ? "warn" : "info"; }
    }
    values["ytKey_set"] = s.ytKey[0] ? 1 : 0;
    values["layout"] = s.layout;
    values["seconds"] = s.seconds;
    values["celebrate"] = s.celebrate;
    heap_caps_free(d);
}
