/**
 * Release Countdown: a countdown to the team's next milestone (a release, a code freeze, a launch) in large figures,
 * the milestones after it, a CODE FREEZE sign the phone turns on and off, and, from a GitHub repository, its latest
 * release and its latest deployment to an environment with how it went.
 *
 * Up to five milestones, each a name and a time (entered on the phone in the Pixlpal's time zone, kept as Unix time).
 * The soonest one still ahead is the countdown; its last ten minutes flash gold; at zero, SHIP IT! with confetti and
 * a fanfare. A milestone that has passed shows as done for a day. A new deployment that succeeded gets a chime, and
 * one that failed an alarm.
 *
 * GitHub (optional, a token only for private repositories): GET /repos/{repo}/releases/latest, /deployments
 * (?environment, newest) and that deployment's newest status, every 5 minutes with a token and 10 without (GitHub
 * allows 60 requests an hour without one). NVS "relSave". Knob: click, check GitHub now.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "rel_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#define W           256
#define H           128
#define MILESTONES  5
#define NAME_LEN    28
#define REPO_LEN    72
#define ENV_LEN     24
#define TOKEN_LEN   100
#define NVS_KEY     "relSave"
#define SAVE_MAGIC  0x52454C01u
#define UA          "Pixlpal-ReleaseCountdown/1.0"
#define DONE_SHOWN  (24u * 3600u)      // a milestone that passed shows as done this long
#define FINAL_SECS  600                // the last ten minutes flash

EXT_RAM_BSS_ATTR TaskHandle_t releaseBoard_Task_H = NULL;
static void releaseBoard_App_Task(void* dApplication);
static void rel_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* releaseBoard_App;
Mtb_Applications* releaseBoard_App_GetInstance(){
    if (!releaseBoard_App){
        releaseBoard_App = new Mtb_Applications_FullScreen(releaseBoard_App_Task, &releaseBoard_Task_H, "Release Countdown", {0,0}, 8192);
        releaseBoard_App->mtb_App_Set_Mobile_UI(REL_UI_MANIFEST, REL_UI_API, nullptr, rel_Values);
    }
    return releaseBoard_App;
}
MTB_REGISTER_APP(releaseBoard_App, 0, 0)       // the catalogue gives the app its number

struct Rel_Milestone_t { char name[NAME_LEN]; uint32_t at; };

struct Rel_Save_t {
    uint32_t magic;
    Rel_Milestone_t m[MILESTONES];
    char repo[REPO_LEN], env[ENV_LEN], token[TOKEN_LEN];
    uint8_t freeze, sound;
};

enum : uint8_t { DP_NONE = 0, DP_OK, DP_FAIL, DP_RUN, DP_OTHER };

struct Rel_Shared_t {
    bool relKnown, depKnown;
    char tag[32], relBy[24];
    uint32_t relAt;
    char sha[8], ref[32], depBy[24], depState[16];
    uint8_t dep;
    uint32_t depId, depAt;
    char why[48];
    uint32_t roundAt, version;
    uint8_t depEvent;                   // a new deployment finished: DP_OK or DP_FAIL, for the app to sound
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Rel_Save_t save;
static EXT_RAM_BSS_ATTR Rel_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Rel_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.sound = 1; snprintf(s.env, ENV_LEN, "production"); }
    for (int i = 0; i < MILESTONES; i++) s.m[i].name[NAME_LEN - 1] = '\0';
    s.repo[REPO_LEN - 1] = s.env[ENV_LEN - 1] = s.token[TOKEN_LEN - 1] = '\0';
    if (!s.env[0]) snprintf(s.env, ENV_LEN, "production");
}

// The Pixlpal's offset from UTC now (seconds), from its local time.
static int32_t utc_Offset(time_t now){
    struct tm l;
    localtime_r(&now, &l);
    const int64_t local = (int64_t)mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday) * 86400 + l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
    return (int32_t)(local - (int64_t)now);
}

/** "2026-12-01 14:00 v2.4 release" or "2026-12-01 Beta": local time to Unix time, and the name. */
static bool parse_Milestone(const char* text, uint32_t& at, char* name, size_t n){
    int y, mo, d, h = 0, mi = 0, used = 0;
    while (*text == ' ') text++;
    if (sscanf(text, "%4d-%2d-%2d%n", &y, &mo, &d, &used) != 3) return false;
    const char* p = text + used;
    int used2 = 0;
    if (sscanf(p, " %2d:%2d%n", &h, &mi, &used2) == 2) p += used2;
    else { h = mi = 0; }
    while (*p == ' ' || *p == ',') p++;
    if (y < 2020 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > mtb_Kit_Days_In_Month(y, mo) || h > 23 || mi > 59) return false;
    if (!*p || strlen(p) >= n) return false;
    char clean[NAME_LEN];
    mtb_Kit_To_Ascii(clean, sizeof(clean), p);
    if (!clean[0]) return false;
    snprintf(name, n, "%s", clean);
    const int64_t local = (int64_t)mtb_Kit_Days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60;
    const time_t now = time(nullptr);
    at = (uint32_t)(local - utc_Offset(now));
    return true;
}

static void milestone_Text(const Rel_Milestone_t& m, char* out, size_t n){
    if (!m.name[0]){ out[0] = '\0'; return; }
    const time_t t = (time_t)m.at;
    struct tm l;
    localtime_r(&t, &l);
    if (l.tm_hour || l.tm_min) snprintf(out, n, "%04d-%02d-%02d %02d:%02d %s", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, m.name);
    else snprintf(out, n, "%04d-%02d-%02d %s", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, m.name);
}

static bool repo_Ok(const char* r){
    const char* slash = strchr(r, '/');
    if (!slash || slash == r || !slash[1] || strchr(slash + 1, '/')) return false;
    for (const char* k = r; *k; k++) if (!(isalnum((unsigned char)*k) || strchr("-_./", *k))) return false;
    return true;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Milestone(JsonDocument& c){
    for (int i = 0; i < MILESTONES; i++){
        char key[4];
        snprintf(key, sizeof(key), "m%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        Rel_Milestone_t m = {};
        if (*v && !parse_Milestone(v, m.at, m.name, sizeof(m.name))){ mtb_Ble_App_Cmd_Reject(); return; }
        portENTER_CRITICAL(&lock); save.m[i] = m; portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Repo(JsonDocument& c){
    const char* v = c["repo"] | (const char*)nullptr;
    if (!v) { mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    if (!strncasecmp(v, "https://github.com/", 19)) v += 19;
    char r[REPO_LEN];
    if (strlen(v) >= sizeof(r)){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(r, sizeof(r), "%s", v);
    size_t n = strlen(r);
    while (n && (r[n - 1] == ' ' || r[n - 1] == '/')) r[--n] = '\0';
    if (n && !repo_Ok(r)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.repo, REPO_LEN, "%s", r);
    sh.relKnown = sh.depKnown = false; sh.depId = 0; sh.why[0] = '\0'; sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Env(JsonDocument& c){
    const char* v = c["env"] | (const char*)nullptr;
    if (!v || strlen(v) >= ENV_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!(isalnum((unsigned char)*k) || strchr("-_.", *k))){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.env, ENV_LEN, "%s", v[0] ? v : "production");
    sh.depKnown = false; sh.depId = 0; sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Token(JsonDocument& c){
    const char* v = c["token"] | (const char*)nullptr;
    if (!v || strlen(v) >= TOKEN_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.token, TOKEN_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Freeze(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["freeze"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.freeze = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen || !save.repo[0]){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static void copy_Ascii(char* dest, size_t n, const char* src){
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s", src ? src : "");
    for (char* k = tmp; *k; k++) if (*k == '\n' || *k == '\r'){ *k = '\0'; break; }
    mtb_Kit_To_Ascii(dest, n, tmp);
}

static void error_Text(int code, bool tok, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "NOT ALLOWED");
    else if (code == -2) snprintf(why, n, "GITHUB'S ANSWER WAS TOO LONG");
    else if (code <= 0) snprintf(why, n, "NO ANSWER FROM GITHUB");
    else if (code == 401) snprintf(why, n, "GITHUB REFUSED THE TOKEN");
    else if (code == 403 || code == 429) snprintf(why, n, tok ? "GITHUB SAYS WAIT" : "RATE LIMITED: ADD A TOKEN");
    else if (code == 404) snprintf(why, n, tok ? "REPOSITORY NOT FOUND" : "NOT FOUND: PRIVATE? ADD A TOKEN");
    else snprintf(why, n, "GITHUB: HTTP %d", code);
}

static int get_Json(const char* url, const char* const* headers, JsonDocument& doc, JsonDocument& filter){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 60000, r, UA);
    if (r.code >= 200 && r.code < 300 && body){
        if (r.truncated || deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static uint8_t dep_State(const char* s){
    if (!s) return DP_NONE;
    if (!strcmp(s, "success")) return DP_OK;
    if (!strcmp(s, "failure") || !strcmp(s, "error")) return DP_FAIL;
    if (!strcmp(s, "in_progress") || !strcmp(s, "queued") || !strcmp(s, "pending")) return DP_RUN;
    return DP_OTHER;                                                    // inactive
}

static void worker(void*){
    uint32_t nextRound = 0;
    while (!sh.stop){
        if (!sh.checkNow && (int32_t)(millis() - nextRound) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Rel_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRound = millis() + (s.token[0] ? 5u : 10u) * 60000u;
        if (!s.repo[0]) continue;
        const bool tok = s.token[0] != 0;
        char auth[TOKEN_LEN + 16], url[260], why[48] = "";
        snprintf(auth, sizeof(auth), "Bearer %s", s.token);
        const char* headers[] = {"Accept", "application/vnd.github+json", "X-GitHub-Api-Version", "2022-11-28", tok ? "Authorization" : nullptr, auth, nullptr};
        Rel_Shared_t r;
        portENTER_CRITICAL(&lock); r = sh; portEXIT_CRITICAL(&lock);
        r.depEvent = 0;
        // The latest release (404: none yet).
        {
            JsonDocument doc, filter;
            filter["tag_name"] = true; filter["published_at"] = true; filter["author"]["login"] = true;
            snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", s.repo);
            const int code = get_Json(url, headers, doc, filter);
            if (code == 200){
                r.relKnown = true;
                copy_Ascii(r.tag, sizeof(r.tag), doc["tag_name"] | "");
                copy_Ascii(r.relBy, sizeof(r.relBy), doc["author"]["login"] | "");
                r.relAt = mtb_Kit_Parse_Iso_Time(doc["published_at"] | "");
            } else if (code == 404){ r.relKnown = true; r.tag[0] = '\0'; r.relAt = 0; }
            else error_Text(code, tok, why, sizeof(why));
        }
        // The latest deployment to the environment, and its newest status.
        if (!why[0]){
            JsonDocument doc, filter;
            JsonObject f = filter[0].to<JsonObject>();
            f["id"] = true; f["sha"] = true; f["ref"] = true; f["creator"]["login"] = true; f["created_at"] = true;
            char env[ENV_LEN * 3];
            mtb_Kit_Url_Encode(env, sizeof(env), s.env);
            snprintf(url, sizeof(url), "https://api.github.com/repos/%s/deployments?environment=%s&per_page=1", s.repo, env);
            const int code = get_Json(url, headers, doc, filter);
            if (code != 200) error_Text(code, tok, why, sizeof(why));
            else if (doc[0].isNull()){ r.depKnown = true; r.dep = DP_NONE; r.depId = 0; r.sha[0] = '\0'; }
            else {
                const uint32_t id = doc[0]["id"] | 0u;
                const bool newer = r.depKnown && id != r.depId;
                const uint8_t was = id == r.depId ? r.dep : DP_NONE;
                r.depId = id;
                const char* sha = doc[0]["sha"] | "";
                snprintf(r.sha, sizeof(r.sha), "%.7s", sha);
                copy_Ascii(r.ref, sizeof(r.ref), doc[0]["ref"] | "");
                copy_Ascii(r.depBy, sizeof(r.depBy), doc[0]["creator"]["login"] | "");
                r.depAt = mtb_Kit_Parse_Iso_Time(doc[0]["created_at"] | "");
                doc.clear(); filter.clear();
                JsonObject g = filter[0].to<JsonObject>();
                g["state"] = true; g["created_at"] = true;
                snprintf(url, sizeof(url), "https://api.github.com/repos/%s/deployments/%lu/statuses?per_page=1", s.repo, (unsigned long)id);
                const int c2 = get_Json(url, headers, doc, filter);
                if (c2 == 200){
                    r.dep = doc[0].isNull() ? DP_RUN : dep_State(doc[0]["state"]);
                    snprintf(r.depState, sizeof(r.depState), "%s", doc[0]["state"] | "pending");
                    const uint32_t at = mtb_Kit_Parse_Iso_Time(doc[0]["created_at"] | "");
                    if (at) r.depAt = at;
                    // A deployment that finished since the last round (a new one, or one that was running).
                    if (r.depKnown && (newer || was == DP_RUN) && (r.dep == DP_OK || r.dep == DP_FAIL)) r.depEvent = r.dep;
                    r.depKnown = true;
                } else error_Text(c2, tok, why, sizeof(why));
            }
        }
        memset(auth, 0, sizeof(auth));
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.repo, save.repo) && !strcmp(s.env, save.env)){       // not changed meanwhile
            sh.relKnown = r.relKnown; snprintf(sh.tag, sizeof(sh.tag), "%s", r.tag); snprintf(sh.relBy, sizeof(sh.relBy), "%s", r.relBy); sh.relAt = r.relAt;
            sh.depKnown = r.depKnown; sh.dep = r.dep; sh.depId = r.depId; sh.depAt = r.depAt;
            snprintf(sh.sha, sizeof(sh.sha), "%s", r.sha); snprintf(sh.ref, sizeof(sh.ref), "%s", r.ref);
            snprintf(sh.depBy, sizeof(sh.depBy), "%s", r.depBy); snprintf(sh.depState, sizeof(sh.depState), "%s", r.depState);
            if (r.depEvent) sh.depEvent = r.depEvent;
            snprintf(sh.why, sizeof(sh.why), "%s", why);
            sh.roundAt = (uint32_t)time(nullptr);
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 7, 14), C_HEAD = MTB_KIT_RGB(18, 22, 36), C_FOOT = MTB_KIT_RGB(14, 18, 28);
static const uint16_t C_FREEZE = MTB_KIT_RGB(40, 120, 230), C_DONE = MTB_KIT_RGB(40, 190, 90);
static const char* const DAY[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void ago_Text(char* t, size_t n, uint32_t at){
    const uint32_t now = (uint32_t)time(nullptr);
    if (!at || now < at){ t[0] = '\0'; return; }
    mtb_Kit_Ago(t, n, (int32_t)(now - at));
    mtb_Kit_Upper(t);
}

static void when_Text(char* t, size_t n, uint32_t at){
    const time_t tt = (time_t)at;
    struct tm l;
    localtime_r(&tt, &l);
    char clock[12] = "";
    if (l.tm_hour || l.tm_min){
        if (mtb_Clock_24h()) snprintf(clock, sizeof(clock), " %02d:%02d", l.tm_hour, l.tm_min);
        else snprintf(clock, sizeof(clock), " %d:%02d %s", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
    }
    snprintf(t, n, "%s %d %s%s", DAY[l.tm_wday % 7], l.tm_mday, MON[l.tm_mon % 12], clock);
}

/** "IN 12 DAYS", "IN 5 H", "IN 20 MIN" */
static void in_Text(char* t, size_t n, int32_t secs){
    if (secs >= 2 * 86400) snprintf(t, n, "IN %ld DAYS", (long)(secs / 86400));
    else if (secs >= 3600) snprintf(t, n, "IN %ld H", (long)(secs / 3600));
    else snprintf(t, n, "IN %ld MIN", (long)((secs + 59) / 60));
}

// The milestones in order of time, those still shown (ahead, or done within a day).
static int order(const Rel_Save_t& s, uint32_t now, int* idx){
    int n = 0;
    for (int i = 0; i < MILESTONES; i++) if (s.m[i].name[0] && s.m[i].at + DONE_SHOWN > now) idx[n++] = i;
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && s.m[idx[b]].at < s.m[idx[b - 1]].at; b--){ const int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    return n;
}

static void confetti(uint32_t ms, uint32_t since){
    static const uint16_t C[] = {KIT_GOLD, KIT_CYAN, KIT_GREEN, KIT_ORANGE, MTB_KIT_RGB(255, 80, 200), KIT_TEXT};
    const uint32_t t = ms - since;
    for (int k = 0; k < 70; k++){
        const uint32_t seed = (uint32_t)k * 2654435761u;
        const int x = (int)(seed % W), speed = 20 + (int)((seed >> 8) % 40);
        const int y = (int)((t * speed / 1000 + (seed >> 16) % H) % H);
        mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + 1), (int16_t)(y + 1), C[k % 6]);
    }
}

static void draw(const Rel_Save_t& s, const Rel_Shared_t& d, uint32_t ms, uint32_t shipSince, const char* shipName){
    mtb_Panel_Fill_Screen(C_BG);
    const uint32_t now = (uint32_t)time(nullptr);
    char t[120], fit[120];
    // The header: CODE FREEZE, or the date.
    if (s.freeze){
        mtb_Panel_Fill_Rect(0, 0, W - 1, 11, C_FREEZE);
        mtb_Kit_Text_Centre(W / 2, 2, Terminal6x8, KIT_TEXT, "CODE FREEZE: NO MERGES", C_FREEZE);
    } else {
        mtb_Panel_Fill_Rect(0, 0, W - 1, 11, C_HEAD);
        mtb_Kit_Text(4, 2, Terminal6x8, KIT_LABEL, "RELEASES", C_HEAD);
        when_Text(t, sizeof(t), now);
        char* clock = strchr(t + 4, ' ');
        if (clock){ clock = strchr(clock + 1, ' '); if (clock) *clock = '\0'; }     // the date only
        mtb_Kit_Text_Right(W - 4, 2, Terminal6x8, KIT_LABEL, t, C_HEAD);
    }
    int idx[MILESTONES];
    const int n = order(s, now, idx);
    int next = -1;
    for (int k = 0; k < n; k++) if (s.m[idx[k]].at > now){ next = idx[k]; break; }
    if (shipSince){
        confetti(ms, shipSince);
        const bool on = (ms / 300) % 2;
        mtb_Panel_Fill_Rect(10, 28, W - 11, 92, on ? KIT_GOLD : C_DONE);
        mtb_Kit_Text_Centre(W / 2, 34, PT_Sans_Narrow_Bold28x34, BLACK, "SHIP IT!", on ? KIT_GOLD : C_DONE);
        snprintf(t, sizeof(t), "%s", shipName); mtb_Kit_Upper(t);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - 30);
        mtb_Kit_Text_Centre(W / 2, 74, Terminal8x12, BLACK, fit, on ? KIT_GOLD : C_DONE);
    } else if (next < 0){
        mtb_Kit_Text_Centre(W / 2, 34, Terminal8x12, KIT_TEXT, n ? "ALL SHIPPED" : "NO MILESTONES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 52, Terminal4x6, KIT_LABEL, "ADD THE NEXT ONE IN THE PHONE APP", C_BG);
    } else {
        const Rel_Milestone_t& m = s.m[next];
        const int32_t left = (int32_t)(m.at - now);
        snprintf(t, sizeof(t), "%s", m.name); mtb_Kit_Upper(t);
        mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, t, W - 8);
        mtb_Kit_Text_Centre(W / 2, 13, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_BG);
        const long days = left / 86400, hh = (left % 86400) / 3600, mm = (left % 3600) / 60, ss = left % 60;
        uint16_t c = KIT_CYAN;
        if (left <= FINAL_SECS) c = (ms / 500) % 2 ? KIT_GOLD : KIT_ORANGE;
        else if (left < 86400) c = KIT_GOLD;
        if (days >= 1) snprintf(t, sizeof(t), "%ldD %02ld:%02ld:%02ld", days, hh, mm, ss);
        else snprintf(t, sizeof(t), "%02ld:%02ld:%02ld", hh, mm, ss);
        mtb_Kit_Text_Centre(W / 2, 36, PT_Sans_Narrow_Bold28x34, c, t, C_BG);
        when_Text(t, sizeof(t), m.at);
        mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_LABEL, t, C_BG);
    }
    // The others: done within a day, then the ones after the next.
    int y = 81, shown = 0;
    for (int k = 0; k < n && shown < 2; k++){
        const int i = idx[k];
        if (i == next) continue;
        const Rel_Milestone_t& m = s.m[i];
        char name[NAME_LEN];
        snprintf(name, sizeof(name), "%s", m.name); mtb_Kit_Upper(name);
        char right[24];
        uint16_t c = KIT_LABEL;
        if (m.at <= now){ char ago[20]; ago_Text(ago, sizeof(ago), m.at); snprintf(right, sizeof(right), "DONE %s", ago); c = C_DONE; }
        else in_Text(right, sizeof(right), (int32_t)(m.at - now));
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, name, W - 90);
        mtb_Kit_Text(4, y, Terminal6x8, m.at <= now ? C_DONE : KIT_TEXT, fit, C_BG);
        mtb_Kit_Text_Right(W - 4, y + 1, Terminal4x6, c, right, C_BG);
        y += 10;
        shown++;
    }
    // GitHub along the bottom.
    if (!s.repo[0]) return;
    mtb_Panel_Fill_Rect(0, 104, W - 1, H - 1, C_FOOT);
    if (d.why[0] && !d.relKnown && !d.depKnown){ mtb_Kit_Text(4, 110, Terminal6x8, KIT_ORANGE, d.why, C_FOOT); return; }
    char ago[20];
    if (d.relKnown){
        if (d.tag[0]){ ago_Text(ago, sizeof(ago), d.relAt); snprintf(t, sizeof(t), "LATEST %s%s%s", d.tag, ago[0] ? "  " : "", ago); }
        else snprintf(t, sizeof(t), "NO RELEASES YET");
        mtb_Kit_Upper(t);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - 8);
        mtb_Kit_Text(4, 107, Terminal4x6, KIT_TEXT, fit, C_FOOT);
    } else mtb_Kit_Text(4, 107, Terminal4x6, KIT_DIM, "CHECKING GITHUB...", C_FOOT);
    if (d.why[0]){ mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, d.why, 110); mtb_Kit_Text_Right(W - 4, 107, Terminal4x6, KIT_ORANGE, fit, C_FOOT); }
    if (!d.depKnown) return;
    char env[ENV_LEN];
    snprintf(env, sizeof(env), "%s", s.env); mtb_Kit_Upper(env);
    if (!d.depId){ snprintf(t, sizeof(t), "%s: NO DEPLOYMENTS", env); mtb_Kit_Text(4, 117, Terminal4x6, KIT_DIM, t, C_FOOT); return; }
    static const char* const WORD[] = {"-", "LIVE", "FAILED", "DEPLOYING", "INACTIVE"};
    const uint16_t sc = d.dep == DP_OK ? C_DONE : d.dep == DP_FAIL ? KIT_RED : d.dep == DP_RUN ? KIT_CYAN : KIT_DIM;
    ago_Text(ago, sizeof(ago), d.depAt);
    char by[24]; snprintf(by, sizeof(by), "%s", d.depBy); mtb_Kit_Upper(by);
    char sha[8]; snprintf(sha, sizeof(sha), "%s", d.sha); mtb_Kit_Upper(sha);
    snprintf(t, sizeof(t), "%s %s%s%s  %s", env, sha, by[0] ? " BY " : "", by, ago);
    const int16_t ww = mtb_Kit_Text_Width(Terminal6x8, WORD[d.dep < 5 ? d.dep : 0]);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - ww - 14);
    mtb_Kit_Text(4, 118, Terminal4x6, KIT_LABEL, fit, C_FOOT);
    mtb_Kit_Text_Right(W - 4, 116, Terminal6x8, sc, WORD[d.dep < 5 ? d.dep : 0], C_FOOT);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void play(uint8_t what){
    static const Snd_Note_t FANFARE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 260}, {0, 80}, {784, 120}, {1047, 420}};
    static const Snd_Note_t DEPLOYED[] = {{659, 90}, {988, 200}};
    static const Snd_Note_t FAILED[] = {{440, 180}, {0, 60}, {349, 180}, {0, 60}, {294, 420}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (what == 0) snd_Play(FANFARE, 7, 0.35f, false);
    else if (what == DP_OK) snd_Play(DEPLOYED, 2, 0.3f, false);
    else snd_Play(FAILED, 5, 0.35f, true);
}

static void releaseBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Milestone, cmd_Repo, cmd_Env, cmd_Token, cmd_Freeze, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "relNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Rel_Shared_t* d = (Rel_Shared_t*)heap_caps_calloc(1, sizeof(Rel_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, shipSince = 0, lastSecond = 0;
    uint32_t aheadMask = 0;                     // milestones ahead at the last look: one that reaches zero while open ships
    char shipName[NAME_LEN] = "";
    bool redraw = true, push = true, saveNow = false, first = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; first = true; }
        if (clickRequested){ clickRequested = false; if (shipSince) shipSince = 0; else sh.checkNow = true; redraw = true; }
        uint8_t event;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        event = sh.depEvent; sh.depEvent = 0;
        portEXIT_CRITICAL(&lock);
        if (event) play(event);
        // A milestone reaching zero while the app is open: SHIP IT! for 30 seconds.
        uint32_t ahead = 0;
        for (int i = 0; i < MILESTONES; i++) if (save.m[i].name[0] && save.m[i].at > now) ahead |= 1u << i;
        const uint32_t reached = first ? 0 : aheadMask & ~ahead;
        if (reached && now > 1700000000u){
            const int i = __builtin_ctz(reached);
            if (save.m[i].name[0] && now - save.m[i].at < 60){
                snprintf(shipName, sizeof(shipName), "%s", save.m[i].name);
                shipSince = ms ? ms : 1;
                play(0);
                push = true;
            }
        }
        aheadMask = ahead;
        first = false;
        if (shipSince && ms - shipSince >= 30000u){ shipSince = 0; redraw = true; }
        if (now != lastSecond){ lastSecond = now; redraw = true; }      // the countdown's seconds
        if (redraw || shipSince || ms - lastDraw >= 250u){
            redraw = false;
            lastDraw = ms;
            Rel_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, ms, shipSince, shipName);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(shipSince ? 40 : 100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void rel_Values(JsonObject values){
    Rel_Save_t* s = (Rel_Save_t*)heap_caps_malloc(sizeof(Rel_Save_t), MALLOC_CAP_SPIRAM);
    Rel_Shared_t* d = (Rel_Shared_t*)heap_caps_malloc(sizeof(Rel_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    const uint32_t now = (uint32_t)time(nullptr);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[140];
    int idx[MILESTONES];
    const int n = order(*s, now, idx);
    for (int k = 0; k < n; k++){
        const Rel_Milestone_t& m = s->m[idx[k]];
        JsonObject o = rows.add<JsonObject>();
        o["label"] = m.name;
        char when[40];
        when_Text(when, sizeof(when), m.at);
        if (m.at <= now){ snprintf(t, sizeof(t), "Done (%s)", when); o["state"] = "ok"; }
        else {
            const int32_t left = (int32_t)(m.at - now);
            if (left >= 86400) snprintf(t, sizeof(t), "In %ld days, %s", (long)(left / 86400), when);
            else snprintf(t, sizeof(t), "In %ld h %ld min, %s", (long)(left / 3600), (long)((left % 3600) / 60), when);
            o["state"] = left < 86400 ? "warn" : "info";
        }
        o["value"] = t;
    }
    if (s->repo[0] && appOpen){
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Latest release";
        if (!d->relKnown) { o["value"] = d->why[0] ? d->why : "Checking"; o["state"] = d->why[0] ? "warn" : "info"; }
        else { o["value"] = d->tag[0] ? d->tag : "None yet"; o["state"] = "info"; }
        if (d->depKnown){
            o = rows.add<JsonObject>();
            snprintf(t, sizeof(t), "Deploy to %s", s->env);
            o["label"] = t;
            if (!d->depId){ o["value"] = "None yet"; o["state"] = "off"; }
            else {
                snprintf(t, sizeof(t), "%s, %s%s%s", d->depState, d->sha, d->depBy[0] ? " by " : "", d->depBy);
                o["value"] = t;
                o["state"] = d->dep == DP_OK ? "ok" : d->dep == DP_FAIL ? "error" : "info";
            }
        }
    }
    for (int i = 0; i < MILESTONES; i++){
        char key[4];
        snprintf(key, sizeof(key), "m%d", i);
        milestone_Text(s->m[i], t, sizeof(t));
        values[key] = t;
    }
    values["repo"] = s->repo;
    values["env"] = s->env;
    values["token_set"] = s->token[0] ? 1 : 0;
    values["freeze"] = s->freeze;
    values["sound"] = s->sound;
    heap_caps_free(s);
    heap_caps_free(d);
}
