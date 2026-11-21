/**
 * Team Tracker: up to four teams in API-Sports' other sports (basketball, American football, rugby, ice hockey,
 * baseball, handball, volleyball): for each, the game being played (live score and stage), else the next game (day,
 * time, opponent, home or away) and the last result (won, lost or drawn, and the score).
 *
 * API-Sports (the owner's key, one for every sport; the free plan allows 100 requests a day per sport): a team's
 * games come from GET /games?team={id}&season={season}. The season's form differs by sport and league ("2025",
 * "2025-2026"), so the likely ones are tried in turn and the first with games is kept. A season's list can pass the
 * 64 KB an answer may have: the games are read one by one from the text, so a list cut short still gives the games
 * before the cut. API-Sports' own error messages (a plan's limits, a wrong key) are shown as they are. Each team is
 * checked every 30 minutes, every 5 while one of its games is on. NVS "ttSave". Knob: click, check now.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "tt_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

static const char TAG[] = "TEAM_TRACKER";
#define W           256
#define H           128
#define NVS_KEY     "ttSave"
#define SAVE_MAGIC  0x54544B01u
#define UA          "Pixlpal-TeamTracker/1.0"
#define KEY_LEN     48
#define TEAMS       4
#define SPORTS      7
#define RESULTS     10

static const char* const HOST[SPORTS] = {"v1.basketball.api-sports.io", "v1.american-football.api-sports.io", "v1.rugby.api-sports.io",
                                         "v1.hockey.api-sports.io", "v1.baseball.api-sports.io", "v1.handball.api-sports.io", "v1.volleyball.api-sports.io"};
static const char* const SPORT_NAME[SPORTS] = {"BASKETBALL", "AMERICAN FOOTBALL", "RUGBY", "ICE HOCKEY", "BASEBALL", "HANDBALL", "VOLLEYBALL"};

EXT_RAM_BSS_ATTR TaskHandle_t teamTracker_Task_H = NULL;
static void teamTracker_App_Task(void* dApplication);
static void tt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* teamTracker_App;
Mtb_Applications* teamTracker_App_GetInstance(){
    if (!teamTracker_App){
        teamTracker_App = new Mtb_Applications_FullScreen(teamTracker_App_Task, &teamTracker_Task_H, "Team Tracker", {0,0}, 8192);
        teamTracker_App->mtb_App_Set_Mobile_UI(TT_UI_MANIFEST, TT_UI_API, nullptr, tt_Values);
    }
    return teamTracker_App;
}
MTB_REGISTER_APP(teamTracker_App, 0, 0)       // the catalogue gives the app its number

struct Tt_Team_t { uint8_t sport; uint32_t id; char name[28]; char season[12]; };
struct Tt_Save_t { uint32_t magic; char key[KEY_LEN]; uint8_t searchSport; Tt_Team_t team[TEAMS]; };

enum : uint8_t { G_NONE = 0, G_NEXT, G_LIVE, G_DONE };
struct Tt_Game_t { uint8_t kind; uint32_t at; char opp[24], stage[8]; bool home; int us, them; };
struct Tt_State_t { Tt_Game_t live, next, last; char why[48]; bool known; uint32_t checkedMs; };

struct Tt_Shared_t {
    Tt_State_t st[TEAMS];
    uint32_t version;
    char query[40];
    uint8_t querySport;
    volatile bool searchNow, checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Tt_Save_t save;
static EXT_RAM_BSS_ATTR Tt_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Tt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; }
    s.key[KEY_LEN - 1] = '\0';
    if (s.searchSport >= SPORTS) s.searchSport = 0;
    for (int i = 0; i < TEAMS; i++){ if (s.team[i].sport >= SPORTS) s.team[i].id = 0; s.team[i].name[sizeof(s.team[i].name) - 1] = s.team[i].season[sizeof(s.team[i].season) - 1] = '\0'; }
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!isalnum((unsigned char)*k)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); memset(sh.st, 0, sizeof(sh.st)); sh.version++; portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Sport(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sport"], 0, SPORTS - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.searchSport = (uint8_t)v; phoneChanged = true; }
static void cmd_Search(JsonDocument& c){
    const char* q = c["query"] | (const char*)nullptr;
    if (!appOpen || !save.key[0] || !q || strlen(q) < 3 || strlen(q) >= sizeof(sh.query)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(sh.query, sizeof(sh.query), "%s", q); sh.querySport = save.searchSport; portEXIT_CRITICAL(&lock);
    sh.searchNow = true;
}
static void cmd_Team(JsonDocument& c){
    const char* name = c["team"]["name"] | (const char*)nullptr;
    const char* value = c["team"]["value"] | (const char*)nullptr;
    char* end = nullptr;
    const long sport = value ? strtol(value, &end, 10) : -1;
    const unsigned long id = end && *end == ':' ? strtoul(end + 1, nullptr, 10) : 0;
    if (!name || sport < 0 || sport >= SPORTS || !id){ mtb_Ble_App_Cmd_Reject(); return; }
    int slot = -1;
    for (int i = 0; i < TEAMS; i++){ if (save.team[i].id == id && save.team[i].sport == sport) return; if (!save.team[i].id && slot < 0) slot = i; }
    if (slot < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    Tt_Team_t& t = save.team[slot];
    memset(&t, 0, sizeof(t));
    t.sport = (uint8_t)sport; t.id = (uint32_t)id;
    mtb_Kit_To_Ascii(t.name, sizeof(t.name), name);
    memset(&sh.st[slot], 0, sizeof(sh.st[slot]));
    sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Remove(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["remove"], 0, TEAMS - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    memmove(&save.team[v], &save.team[v + 1], sizeof(save.team[0]) * (TEAMS - 1 - v));
    memset(&save.team[TEAMS - 1], 0, sizeof(save.team[0]));
    memmove(&sh.st[v], &sh.st[v + 1], sizeof(sh.st[0]) * (TEAMS - 1 - v));
    memset(&sh.st[TEAMS - 1], 0, sizeof(sh.st[0]));
    sh.version++;
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }

// The worker ------------------------------------------------------------------------------------------------------------------------
/** GET from a sport's host; the answer's text (freed by the caller) and its length. */
static char* get_Raw(uint8_t sport, const char* path, const char* key, Mtb_Kit_Http_t& r){
    char url[200];
    snprintf(url, sizeof(url), "https://%s%s", HOST[sport], path);
    const char* headers[] = {"x-apisports-key", key, nullptr};
    r = {};
    return mtb_Kit_Http_Get(url, headers, nullptr, 0, 64000, r, UA);
}

// API-Sports answers 200 with {"errors": {"plan": "...", "token": "..."}} (an object, or [] when there are none).
static bool api_Error(const char* body, size_t n, char* why, size_t whyN){
    JsonDocument doc, filter;
    filter["errors"] = true;
    if (deserializeJson(doc, body, n, DeserializationOption::Filter(filter))) {
        // A list cut short can still begin with errors: [] , so look at the text itself.
        const char* e = strstr(body, "\"errors\"");
        if (!e || e - body > 200) return false;
        const char* colon = strchr(e, ':');
        if (colon && colon[1] == '[') return false;
        snprintf(why, whyN, "API-SPORTS REFUSED THE REQUEST");
        return true;
    }
    JsonObjectConst errors = doc["errors"].as<JsonObjectConst>();
    if (errors.isNull() || errors.size() == 0) return false;
    for (JsonPairConst p : errors){
        char m[96];
        mtb_Kit_To_Ascii(m, sizeof(m), p.value().as<const char*>() ? p.value().as<const char*>() : p.key().c_str());
        mtb_Kit_Upper(m);
        snprintf(why, whyN, "%.47s", m);
        ESP_LOGW(TAG, "API-Sports: %s: %s", p.key().c_str(), m);
        return true;
    }
    return false;
}

static void search(uint8_t sport, const char* query, const char* key){
    char q[96], path[140];
    mtb_Kit_Url_Encode(q, sizeof(q), query);
    snprintf(path, sizeof(path), "/teams?search=%s", q);
    Mtb_Kit_Http_t r;
    char* body = get_Raw(sport, path, key, r);
    JsonDocument out;
    JsonArray list = out.to<JsonArray>();
    char why[48] = "";
    if (r.code == 200 && body && !api_Error(body, r.length, why, sizeof(why))){
        JsonDocument doc, filter;
        JsonObject f = filter["response"][0].to<JsonObject>();
        f["id"] = true; f["name"] = true; f["country"]["name"] = true; f["country"] = true;
        if (!deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter)))
            for (JsonVariantConst t : doc["response"].as<JsonArrayConst>()){
                if (list.size() >= RESULTS) break;
                JsonObject item = list.add<JsonObject>();
                item["name"] = t["name"] | "?";
                char v[24];
                snprintf(v, sizeof(v), "%u:%lu", (unsigned)sport, (unsigned long)(t["id"] | 0u));
                item["value"] = v;
                item["detail"] = t["country"]["name"].is<const char*>() ? t["country"]["name"].as<const char*>() : (t["country"].is<const char*>() ? t["country"].as<const char*>() : "");
            }
    } else if (!why[0]) snprintf(why, sizeof(why), "API-Sports: HTTP %d", r.code);
    ESP_LOGI(TAG, "Search %s '%s': HTTP %d, %u found%s%s", SPORT_NAME[sport], query, r.code, (unsigned)list.size(), why[0] ? ", " : "", why);
    heap_caps_free(body);
    if (teamTracker_App) teamTracker_App->mtb_App_Send_Search_Results("teams", query, list, why[0] ? why : nullptr);
}

/** The seasons to try for a sport now, most likely first. */
static int seasons(uint8_t sport, char out[3][12]){
    const time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    const int y = u.tm_year + 1900, m = u.tm_mon + 1;
    int n = 0;
    switch (sport){
    case 0: { const int s = m >= 8 ? y : y - 1; snprintf(out[n++], 12, "%d-%d", s, s + 1); snprintf(out[n++], 12, "%d", s); break; }   // basketball
    case 1: { const int s = m >= 3 ? y : y - 1; snprintf(out[n++], 12, "%d", s); break; }                                              // NFL: Sept-Feb
    case 4: snprintf(out[n++], 12, "%d", y); snprintf(out[n++], 12, "%d", y - 1); break;                                               // baseball
    default: { const int s = m >= 8 ? y : y - 1; snprintf(out[n++], 12, "%d", s); if (s != y) snprintf(out[n++], 12, "%d", y); break; }
    }
    return n;
}

// A game's score for a side: a number, or {"total": n}.
static int score_Of(JsonVariantConst v){
    if (v.is<int>()) return v.as<int>();
    if (v["total"].is<int>()) return v["total"].as<int>();
    return -1;
}

static bool finished(const char* s){
    static const char* const DONE[] = {"FT", "AOT", "AET", "AP", "PEN", "AW", "AWD", "WO", "POST_OT", "ET_END"};
    for (const char* d : DONE) if (!strcmp(s, d)) return true;
    return false;
}
static bool not_Started(const char* s){ return !strcmp(s, "NS") || !strcmp(s, "TBD") || !strcmp(s, "SCH") || !s[0]; }
static bool off(const char* s){ return !strcmp(s, "CANC") || !strcmp(s, "PST") || !strcmp(s, "ABD") || !strcmp(s, "SUSP") || !strcmp(s, "INTR") || !strcmp(s, "AWD"); }

/** Reads the games in "response": [...] one at a time, so a list cut short still gives the games before the cut. */
static int scan_Games(const char* body, size_t n, const Tt_Team_t& t, Tt_State_t& st){
    const char* p = strstr(body, "\"response\"");
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    p++;
    const char* end = body + n;
    const uint32_t now = (uint32_t)time(nullptr);
    int games = 0;
    st.live.kind = st.next.kind = st.last.kind = G_NONE;
    while (p < end){
        while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p != '{') break;
        // The element's end, by its braces (outside strings).
        int depth = 0; bool str = false; const char* q = p;
        for (; q < end; q++){
            if (str){ if (*q == '\\') q++; else if (*q == '"') str = false; continue; }
            if (*q == '"') str = true;
            else if (*q == '{') depth++;
            else if (*q == '}' && --depth == 0) break;
        }
        if (q >= end) break;                                    // cut short here
        JsonDocument doc, filter;
        filter["timestamp"] = true; filter["status"]["short"] = true; filter["game"]["date"]["timestamp"] = true; filter["game"]["status"]["short"] = true;
        filter["teams"]["home"]["id"] = true; filter["teams"]["home"]["name"] = true; filter["teams"]["away"]["id"] = true; filter["teams"]["away"]["name"] = true;
        filter["scores"]["home"] = true; filter["scores"]["away"] = true;
        if (!deserializeJson(doc, p, (size_t)(q - p + 1), DeserializationOption::Filter(filter))){
            games++;
            const uint32_t at = doc["timestamp"].is<uint32_t>() ? doc["timestamp"].as<uint32_t>() : (doc["game"]["date"]["timestamp"] | 0u);
            const char* status = doc["status"]["short"].is<const char*>() ? doc["status"]["short"].as<const char*>() : (doc["game"]["status"]["short"] | "");
            const bool home = (doc["teams"]["home"]["id"] | 0u) == t.id;
            Tt_Game_t g = {};
            g.at = at; g.home = home;
            mtb_Kit_To_Ascii(g.opp, sizeof(g.opp), home ? (doc["teams"]["away"]["name"] | "?") : (doc["teams"]["home"]["name"] | "?"));
            g.us = score_Of(home ? doc["scores"]["home"] : doc["scores"]["away"]);
            g.them = score_Of(home ? doc["scores"]["away"] : doc["scores"]["home"]);
            snprintf(g.stage, sizeof(g.stage), "%s", status);
            if (off(status)) {}
            else if (finished(status)){ if (!st.last.kind || at > st.last.at){ g.kind = G_DONE; st.last = g; } }
            else if (not_Started(status)){ if (at + 3 * 3600 > now && (!st.next.kind || at < st.next.at)){ g.kind = G_NEXT; st.next = g; } }
            else { g.kind = G_LIVE; st.live = g; }
        }
        p = q + 1;
    }
    return games;
}

static void check_Team(Tt_Team_t& t, const char* key, Tt_State_t& st){
    char tried[3][12];
    int n = 0;
    if (t.season[0]){ snprintf(tried[0], 12, "%s", t.season); n = 1; }
    else n = seasons(t.sport, tried);
    st.why[0] = '\0';
    for (int k = 0; k < n; k++){
        char path[96];
        snprintf(path, sizeof(path), "/games?team=%lu&season=%s", (unsigned long)t.id, tried[k]);
        Mtb_Kit_Http_t r;
        char* body = get_Raw(t.sport, path, key, r);
        int games = -1;
        if ((r.code == 200 || r.code == PXL_HTTP_TOO_BIG) && body){
            if (!api_Error(body, r.length, st.why, sizeof(st.why))) games = scan_Games(body, r.length, t, st);
        } else if (r.code == PXL_HTTP_NOT_ALLOWED) snprintf(st.why, sizeof(st.why), "NOT ALLOWED");
        else if (r.code <= 0) snprintf(st.why, sizeof(st.why), "NO ANSWER FROM API-SPORTS");
        else snprintf(st.why, sizeof(st.why), "API-SPORTS: HTTP %d", r.code);
        ESP_LOGI(TAG, "%s %s season %s: HTTP %d, %u bytes%s, %d games%s%s", SPORT_NAME[t.sport], t.name, tried[k], r.code, (unsigned)r.length, r.truncated ? " (cut short)" : "",
                 games, st.why[0] ? ", " : "", st.why);
        heap_caps_free(body);
        if (st.why[0]) return;
        if (games > 0){ snprintf(t.season, sizeof(t.season), "%s", tried[k]); st.known = true; return; }
    }
    if (t.season[0]){ t.season[0] = '\0'; }                    // none this season: look again next time
    st.known = true;
    snprintf(st.why, sizeof(st.why), "NO GAMES THIS SEASON");
}

static void worker(void*){
    while (!sh.stop){
        if (sh.searchNow){
            sh.searchNow = false;
            char q[40], key[KEY_LEN]; uint8_t sport;
            portENTER_CRITICAL(&lock); snprintf(q, sizeof(q), "%s", sh.query); sport = sh.querySport; snprintf(key, sizeof(key), "%s", save.key); portEXIT_CRITICAL(&lock);
            search(sport, q, key);
            memset(key, 0, sizeof(key));
            continue;
        }
        const bool all = sh.checkNow;
        sh.checkNow = false;
        Tt_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        bool any = false;
        for (int i = 0; i < TEAMS && s.key[0] && !sh.stop; i++){
            Tt_Team_t t = s.team[i];
            if (!t.id) continue;
            Tt_State_t st;
            portENTER_CRITICAL(&lock); st = sh.st[i]; portEXIT_CRITICAL(&lock);
            const uint32_t every = st.live.kind == G_LIVE ? 5u * 60000u : 30u * 60000u;
            if (!all && st.checkedMs && millis() - st.checkedMs < every) continue;
            any = true;
            check_Team(t, s.key, st);
            st.checkedMs = millis() ? millis() : 1;
            portENTER_CRITICAL(&lock);
            if (save.team[i].id == t.id && save.team[i].sport == t.sport){ sh.st[i] = st; snprintf(save.team[i].season, sizeof(save.team[i].season), "%s", t.season); sh.version++; }
            portEXIT_CRITICAL(&lock);
        }
        if (!any) vTaskDelay(pdMS_TO_TICKS(300));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 6, 12), C_ROW = MTB_KIT_RGB(14, 16, 28), C_LIVE = MTB_KIT_RGB(235, 40, 40);

static void when_Text(char* t, size_t n, uint32_t at){
    const time_t tt = (time_t)at;
    struct tm l, nowL;
    const time_t now = time(nullptr);
    localtime_r(&tt, &l);
    localtime_r(&now, &nowL);
    static const char* const D[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    char clock[12];
    if (mtb_Clock_24h()) snprintf(clock, sizeof(clock), "%02d:%02d", l.tm_hour, l.tm_min);
    else snprintf(clock, sizeof(clock), "%d:%02d%s", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
    if (l.tm_yday == nowL.tm_yday && l.tm_year == nowL.tm_year) snprintf(t, n, "TODAY %s", clock);
    else if (at > (uint32_t)now && at - (uint32_t)now < 6 * 86400u) snprintf(t, n, "%s %s", D[l.tm_wday], clock);
    else snprintf(t, n, "%d/%d %s", l.tm_mday, l.tm_mon + 1, clock);
}

static void draw(const Tt_Save_t& s, const Tt_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    int used[TEAMS], n = 0;
    for (int i = 0; i < TEAMS; i++) if (s.team[i].id) used[n++] = i;
    if (!s.key[0]){ mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, "ADD AN API-SPORTS KEY", C_BG); mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "FREE AT DASHBOARD.API-FOOTBALL.COM", C_BG); return; }
    if (!n){ mtb_Kit_Text_Centre(W / 2, 54, Terminal8x12, KIT_TEXT, "ADD YOUR TEAMS", C_BG); mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_LABEL, "IN THE PHONE APP, UNDER TEAM TRACKER", C_BG); return; }
    const int rowH = (H - (n - 1) * 2) / n;
    char t[64], f[64];
    for (int k = 0; k < n; k++){
        const Tt_Team_t& team = s.team[used[k]];
        const Tt_State_t& st = d.st[used[k]];
        const int y = k * (rowH + 2);
        const bool live = st.live.kind == G_LIVE;
        const uint16_t bg = live ? MTB_KIT_RGB(40, 10, 12) : C_ROW;
        mtb_Panel_Fill_Rect(0, y, W - 1, y + rowH - 1, bg);
        snprintf(t, sizeof(t), "%s", team.name); mtb_Kit_Upper(t);
        mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, 140);
        mtb_Kit_Text(4, y + 2, Terminal6x8, KIT_TEXT, f, bg);
        mtb_Kit_Text_Right(W - 4, y + 3, Terminal4x6, KIT_DIM, SPORT_NAME[team.sport], bg);
        const bool tall = rowH >= 40;
        const int y2 = y + (tall ? 14 : 11);
        if (!st.known && !st.why[0]){ mtb_Kit_Text(4, y2, Terminal4x6, KIT_DIM, "CHECKING...", bg); continue; }
        if (st.why[0] && !st.live.kind && !st.next.kind && !st.last.kind){ mtb_Kit_Fit(f, sizeof(f), Terminal4x6, st.why, W - 8); mtb_Kit_Text(4, y2, Terminal4x6, KIT_ORANGE, f, bg); continue; }
        char opp[24];
        if (live){
            snprintf(opp, sizeof(opp), "%s", st.live.opp); mtb_Kit_Upper(opp);
            snprintf(t, sizeof(t), "%d - %d", st.live.us < 0 ? 0 : st.live.us, st.live.them < 0 ? 0 : st.live.them);
            mtb_Kit_Text(4, y2, tall ? PT_Sans_Narrow_Bold18x22 : Terminal8x12, KIT_GOLD, t, bg);
            snprintf(t, sizeof(t), "%s %s  %s", st.live.home ? "V" : "AT", opp, st.live.stage);
            mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, 140);
            mtb_Kit_Text(tall ? 90 : 70, y2 + (tall ? 8 : 3), Terminal4x6, KIT_LABEL, f, bg);
            if ((ms / 600) % 2) mtb_Panel_Fill_Circle(W - 8, y2 + 6, 3, C_LIVE);
            continue;
        }
        if (st.next.kind){
            char w[24]; when_Text(w, sizeof(w), st.next.at);
            snprintf(opp, sizeof(opp), "%s", st.next.opp); mtb_Kit_Upper(opp);
            snprintf(t, sizeof(t), "NEXT %s %s %s", w, st.next.home ? "V" : "AT", opp);
            mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, W - 8);
            mtb_Kit_Text(4, y2, Terminal4x6, KIT_CYAN, f, bg);
        }
        if (st.last.kind && (tall || !st.next.kind)){
            snprintf(opp, sizeof(opp), "%s", st.last.opp); mtb_Kit_Upper(opp);
            const char* res = st.last.us > st.last.them ? "W" : st.last.us < st.last.them ? "L" : "D";
            snprintf(t, sizeof(t), "LAST %s %d-%d %s %s", res, st.last.us, st.last.them, st.last.home ? "V" : "AT", opp);
            mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, W - 8);
            mtb_Kit_Text(4, y2 + (st.next.kind ? 9 : 0), Terminal4x6, *res == 'W' ? KIT_GREEN : *res == 'L' ? KIT_RED : KIT_LABEL, f, bg);
        }
        if (!st.next.kind && !st.last.kind) mtb_Kit_Text(4, y2, Terminal4x6, KIT_DIM, "NO GAMES THIS SEASON", bg);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void teamTracker_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Key, cmd_Sport, cmd_Search, cmd_Team, cmd_Remove, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "ttNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Tt_Shared_t* d = (Tt_Shared_t*)heap_caps_calloc(1, sizeof(Tt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = saveNow = true; }
        portEXIT_CRITICAL(&lock);
        bool live = false;
        for (int i = 0; i < TEAMS; i++) if (d->st[i].live.kind == G_LIVE) live = true;
        if (redraw || (live && ms - lastDraw >= 600u) || ms - lastDraw >= 30000u){
            redraw = false;
            lastDraw = ms;
            Tt_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void tt_Values(JsonObject values){
    Tt_Save_t s;
    Tt_Shared_t* d = (Tt_Shared_t*)heap_caps_calloc(1, sizeof(Tt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[120];
    for (int i = 0; i < TEAMS; i++){
        char key[8];
        snprintf(key, sizeof(key), "team%d", i);
        values[key] = s.team[i].id ? s.team[i].name : "";
        if (!s.team[i].id) continue;
        const Tt_State_t& st = d->st[i];
        JsonObject o = rows.add<JsonObject>();
        snprintf(t, sizeof(t), "%d. %s", i + 1, s.team[i].name);
        o["label"] = t;
        if (!appOpen){ o["value"] = "Checked while Team Tracker is open"; o["state"] = "off"; continue; }
        if (st.live.kind == G_LIVE){ snprintf(t, sizeof(t), "Live: %d-%d %s %s (%s)", st.live.us, st.live.them, st.live.home ? "v" : "at", st.live.opp, st.live.stage); o["state"] = "warn"; }
        else if (st.why[0] && !st.next.kind && !st.last.kind){ snprintf(t, sizeof(t), "%s", st.why); o["state"] = "warn"; }
        else if (st.next.kind){ char w[24]; when_Text(w, sizeof(w), st.next.at); snprintf(t, sizeof(t), "Next: %s %s %s", w, st.next.home ? "v" : "at", st.next.opp); o["state"] = "info"; }
        else if (st.last.kind){ snprintf(t, sizeof(t), "Last: %d-%d %s %s", st.last.us, st.last.them, st.last.home ? "v" : "at", st.last.opp); o["state"] = "info"; }
        else { snprintf(t, sizeof(t), "Checking"); o["state"] = "info"; }
        o["value"] = t;
    }
    values["sport"] = s.searchSport;
    values["apiKey_set"] = s.key[0] ? 1 : 0;
    heap_caps_free(d);
}
