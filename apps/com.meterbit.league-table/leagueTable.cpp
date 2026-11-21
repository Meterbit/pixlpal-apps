/**
 * League Table: one football competition on the panel, in three pages the knob turns (or that change by themselves):
 *   - The table: position, team, played, goal difference, points and the last five results, opening around the
 *     owner's team (highlighted); a competition with groups shows the team's group.
 *   - The top scorers: goals and assists.
 *   - This round: its results (live ones marked) and fixtures with their local kick-off times.
 * From football-data.org (v4, the owner's free key): /competitions/{code}/standings, /scorers?limit=8 and
 * /matches?matchday={current}, every 10 minutes (every 2 while a match of the round is being played). NVS "ltSave".
 * Knob: turn, the next page; click, check now.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "lt_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "ltSave"
#define SAVE_MAGIC  0x4C544401u
#define UA          "Pixlpal-LeagueTable/1.0"
#define KEY_LEN     48
#define ROWS        40
#define SCORERS     8
#define MATCHES     12
#define LEAGUES     12

static const char* const CODE[LEAGUES] = {"PL", "ELC", "PD", "SA", "BL1", "FL1", "DED", "PPL", "BSA", "CL", "EC", "WC"};
static const char* const LEAGUE_NAME[LEAGUES] = {"PREMIER LEAGUE", "CHAMPIONSHIP", "LA LIGA", "SERIE A", "BUNDESLIGA", "LIGUE 1",
                                                 "EREDIVISIE", "PRIMEIRA LIGA", "BRASILEIRAO", "CHAMPIONS LEAGUE", "EUROS", "WORLD CUP"};

EXT_RAM_BSS_ATTR TaskHandle_t leagueTable_Task_H = NULL;
static void leagueTable_App_Task(void* dApplication);
static void lt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* leagueTable_App;
Mtb_Applications* leagueTable_App_GetInstance(){
    if (!leagueTable_App){
        leagueTable_App = new Mtb_Applications_FullScreen(leagueTable_App_Task, &leagueTable_Task_H, "League Table", {0,0}, 8192);
        leagueTable_App->mtb_App_Set_Mobile_UI(LT_UI_MANIFEST, LT_UI_API, nullptr, lt_Values);
    }
    return leagueTable_App;
}
MTB_REGISTER_APP(leagueTable_App, 0, 0)       // the catalogue gives the app its number

struct Lt_Save_t {
    uint32_t magic;
    uint8_t league, seconds;
    uint32_t teamId;
    char teamName[32];
    char key[KEY_LEN];
};

struct Lt_Row_t { uint8_t pos, played; int16_t gd, points; char tla[5], name[24], form[6]; uint32_t id; };
struct Lt_Scorer_t { char name[24], team[5]; uint8_t goals, assists; };
struct Lt_Match_t { char home[5], away[5]; int8_t hs, as; uint8_t state; uint32_t at; };     // state: 0 to come, 1 live, 2 done, 3 off

struct Lt_Shared_t {
    Lt_Row_t row[ROWS];
    uint8_t rows;
    char group[16];
    Lt_Scorer_t scorer[SCORERS];
    uint8_t scorers;
    Lt_Match_t match[MATCHES];
    uint8_t matches, matchday;
    bool known, live;
    char why[44];
    uint32_t at, version;
    char query[40];
    volatile bool searchNow, checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Lt_Save_t save;
static EXT_RAM_BSS_ATTR Lt_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Lt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.seconds = 15; }
    if (s.league >= LEAGUES) s.league = 0;
    if (s.seconds && (s.seconds < 5 || s.seconds > 60)) s.seconds = 15;
    s.teamName[sizeof(s.teamName) - 1] = s.key[KEY_LEN - 1] = '\0';
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void changed(void){ portENTER_CRITICAL(&lock); sh.known = false; sh.rows = sh.scorers = sh.matches = 0; sh.why[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock); sh.checkNow = true; phoneChanged = true; }
static void cmd_League(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["league"], 0, LEAGUES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (save.league == (uint8_t)v) return;
    save.league = (uint8_t)v;
    save.teamId = 0; save.teamName[0] = '\0';               // a team in another competition
    changed();
}
static void cmd_Search(JsonDocument& c){
    const char* q = c["query"] | (const char*)nullptr;
    if (!appOpen || !q || strlen(q) < 2 || strlen(q) >= sizeof(sh.query) || !sh.rows){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(sh.query, sizeof(sh.query), "%s", q); portEXIT_CRITICAL(&lock);
    sh.searchNow = true;
}
static void cmd_Team(JsonDocument& c){
    const char* name = c["team"]["name"] | (const char*)nullptr;
    const char* id = c["team"]["value"] | (const char*)nullptr;
    if (!name || !id){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    save.teamId = (uint32_t)strtoul(id, nullptr, 10);
    mtb_Kit_To_Ascii(save.teamName, sizeof(save.teamName), save.teamId ? name : "");
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!isalnum((unsigned char)*k)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Seconds(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["seconds"], 0, 60, v) || (v && v < 5)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.seconds = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* path, const char* key, JsonDocument& doc, JsonDocument& filter){
    char url[200];
    snprintf(url, sizeof(url), "https://api.football-data.org/v4%s", path);
    const char* headers[] = {"X-Auth-Token", key, nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 60000, r, UA);
    if (r.code == 200 && body){
        if (r.truncated || deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static void error_Text(int code, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "NOT ALLOWED");
    else if (code == -2) snprintf(why, n, "AN ANSWER PIXLPAL CAN'T READ");
    else if (code <= 0) snprintf(why, n, "NO ANSWER FROM FOOTBALL-DATA.ORG");
    else if (code == 400 || code == 401) snprintf(why, n, "FOOTBALL-DATA.ORG REFUSED THE KEY");
    else if (code == 403) snprintf(why, n, "NOT IN YOUR FOOTBALL-DATA.ORG PLAN");
    else if (code == 429) snprintf(why, n, "TOO MANY REQUESTS: WAIT A MINUTE");
    else snprintf(why, n, "FOOTBALL-DATA.ORG: HTTP %d", code);
}

static void copy_Upper(char* d, size_t n, const char* s){ mtb_Kit_To_Ascii(d, n, s ? s : ""); mtb_Kit_Upper(d); }

static int fetch_Table(const Lt_Save_t& s, Lt_Shared_t& r){
    JsonDocument doc, filter;
    filter["season"]["currentMatchday"] = true;
    JsonObject st = filter["standings"][0].to<JsonObject>();
    st["type"] = true; st["group"] = true;
    JsonObject t = st["table"][0].to<JsonObject>();
    t["position"] = true; t["playedGames"] = true; t["goalDifference"] = true; t["points"] = true; t["form"] = true;
    t["team"]["id"] = true; t["team"]["tla"] = true; t["team"]["shortName"] = true;
    char path[64];
    snprintf(path, sizeof(path), "/competitions/%s/standings", CODE[s.league]);
    const int code = get_Json(path, s.key, doc, filter);
    if (code != 200) return code;
    r.matchday = doc["season"]["currentMatchday"] | 0;
    // The standings to show: the group with the owner's team, else the first "TOTAL" one.
    JsonArrayConst all = doc["standings"].as<JsonArrayConst>();
    JsonVariantConst chosen;
    for (JsonVariantConst g : all){
        if (strcmp(g["type"] | "TOTAL", "TOTAL")) continue;
        if (chosen.isNull()) chosen = g;
        if (s.teamId) for (JsonVariantConst row : g["table"].as<JsonArrayConst>()) if ((row["team"]["id"] | 0u) == s.teamId){ chosen = g; break; }
    }
    r.rows = 0;
    const char* group = chosen["group"] | "";
    copy_Upper(r.group, sizeof(r.group), group);
    for (char* k = r.group; *k; k++) if (*k == '_') *k = ' ';
    for (JsonVariantConst row : chosen["table"].as<JsonArrayConst>()){
        if (r.rows >= ROWS) break;
        Lt_Row_t& o = r.row[r.rows++];
        memset(&o, 0, sizeof(o));
        o.pos = row["position"] | 0; o.played = row["playedGames"] | 0; o.gd = row["goalDifference"] | 0; o.points = row["points"] | 0;
        o.id = row["team"]["id"] | 0u;
        copy_Upper(o.tla, sizeof(o.tla), row["team"]["tla"] | "");
        mtb_Kit_To_Ascii(o.name, sizeof(o.name), row["team"]["shortName"] | "");
        // Form: "W,D,L,W,W" (newest last, as football-data.org gives it) into "WDLWW".
        const char* f = row["form"] | "";
        int n = 0;
        for (const char* k = f; *k && n < 5; k++) if (*k == 'W' || *k == 'D' || *k == 'L') o.form[n++] = *k;
        o.form[n] = '\0';
    }
    return 200;
}

static int fetch_Scorers(const Lt_Save_t& s, Lt_Shared_t& r){
    JsonDocument doc, filter;
    JsonObject f = filter["scorers"][0].to<JsonObject>();
    f["player"]["name"] = true; f["team"]["tla"] = true; f["goals"] = true; f["assists"] = true;
    char path[64];
    snprintf(path, sizeof(path), "/competitions/%s/scorers?limit=%d", CODE[s.league], SCORERS);
    const int code = get_Json(path, s.key, doc, filter);
    if (code != 200) return code;
    r.scorers = 0;
    for (JsonVariantConst p : doc["scorers"].as<JsonArrayConst>()){
        if (r.scorers >= SCORERS) break;
        Lt_Scorer_t& o = r.scorer[r.scorers++];
        copy_Upper(o.name, sizeof(o.name), p["player"]["name"] | "");
        copy_Upper(o.team, sizeof(o.team), p["team"]["tla"] | "");
        o.goals = p["goals"] | 0;
        o.assists = p["assists"].is<int>() ? p["assists"].as<int>() : 0;
    }
    return 200;
}

static int fetch_Matches(const Lt_Save_t& s, Lt_Shared_t& r){
    if (!r.matchday){ r.matches = 0; return 200; }
    JsonDocument doc, filter;
    JsonObject f = filter["matches"][0].to<JsonObject>();
    f["utcDate"] = true; f["status"] = true; f["homeTeam"]["tla"] = true; f["awayTeam"]["tla"] = true;
    f["score"]["fullTime"]["home"] = true; f["score"]["fullTime"]["away"] = true;
    char path[80];
    snprintf(path, sizeof(path), "/competitions/%s/matches?matchday=%u", CODE[s.league], (unsigned)r.matchday);
    const int code = get_Json(path, s.key, doc, filter);
    if (code != 200) return code;
    r.matches = 0; r.live = false;
    for (JsonVariantConst m : doc["matches"].as<JsonArrayConst>()){
        if (r.matches >= MATCHES) break;
        Lt_Match_t& o = r.match[r.matches++];
        copy_Upper(o.home, sizeof(o.home), m["homeTeam"]["tla"] | "?");
        copy_Upper(o.away, sizeof(o.away), m["awayTeam"]["tla"] | "?");
        o.hs = m["score"]["fullTime"]["home"].is<int>() ? m["score"]["fullTime"]["home"].as<int>() : -1;
        o.as = m["score"]["fullTime"]["away"].is<int>() ? m["score"]["fullTime"]["away"].as<int>() : -1;
        o.at = mtb_Kit_Parse_Iso_Time(m["utcDate"] | "");
        const char* st = m["status"] | "";
        if (!strcmp(st, "IN_PLAY") || !strcmp(st, "PAUSED") || !strcmp(st, "LIVE")){ o.state = 1; r.live = true; }
        else if (!strcmp(st, "FINISHED") || !strcmp(st, "AWARDED")) o.state = 2;
        else if (!strcmp(st, "POSTPONED") || !strcmp(st, "SUSPENDED") || !strcmp(st, "CANCELLED")) o.state = 3;
        else o.state = 0;
    }
    return 200;
}

static void search(const char* query){
    JsonDocument out;
    JsonArray list = out.to<JsonArray>();
    char q[40];
    copy_Upper(q, sizeof(q), query);
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < sh.rows && list.size() < 10; i++){
        char name[24];
        copy_Upper(name, sizeof(name), sh.row[i].name);
        if (!strstr(name, q) && strcmp(sh.row[i].tla, q)) continue;
        JsonObject item = list.add<JsonObject>();
        item["name"] = sh.row[i].name;
        char id[12];
        snprintf(id, sizeof(id), "%lu", (unsigned long)sh.row[i].id);
        item["value"] = id;
        item["detail"] = sh.row[i].tla;
    }
    portEXIT_CRITICAL(&lock);
    if (leagueTable_App) leagueTable_App->mtb_App_Send_Search_Results("teams", query, list, nullptr);
}

static void worker(void*){
    uint32_t next = 0;
    while (!sh.stop){
        if (sh.searchNow){ sh.searchNow = false; char q[40]; portENTER_CRITICAL(&lock); snprintf(q, sizeof(q), "%s", sh.query); portEXIT_CRITICAL(&lock); search(q); continue; }
        if (!sh.checkNow && (int32_t)(millis() - next) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Lt_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        next = millis() + 10u * 60000u;
        if (!s.key[0]) continue;
        static Lt_Shared_t r;
        portENTER_CRITICAL(&lock); r = sh; portEXIT_CRITICAL(&lock);
        char why[44] = "";
        int code = fetch_Table(s, r);
        if (code == 200) code = fetch_Scorers(s, r);
        if (code == 200) code = fetch_Matches(s, r);
        if (code != 200) error_Text(code, why, sizeof(why));
        if (code == 200 && r.live) next = millis() + 2u * 60000u;
        if (code != 200 && code != 400 && code != 401) next = millis() + 2u * 60000u;
        portENTER_CRITICAL(&lock);
        if (s.league == save.league && !strcmp(s.key, save.key)){
            if (code == 200){
                memcpy(sh.row, r.row, sizeof(r.row)); sh.rows = r.rows; snprintf(sh.group, sizeof(sh.group), "%s", r.group);
                memcpy(sh.scorer, r.scorer, sizeof(r.scorer)); sh.scorers = r.scorers;
                memcpy(sh.match, r.match, sizeof(r.match)); sh.matches = r.matches; sh.matchday = r.matchday; sh.live = r.live;
                sh.known = true; sh.at = (uint32_t)time(nullptr);
            }
            snprintf(sh.why, sizeof(sh.why), "%s", why);
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 8, 6), C_HEAD = MTB_KIT_RGB(10, 40, 24), C_MINE = MTB_KIT_RGB(30, 70, 140), C_PITCH = MTB_KIT_RGB(60, 190, 100);
enum { PAGE_TABLE = 0, PAGE_SCORERS, PAGE_ROUND, PAGES };

static void header(const Lt_Save_t& s, const Lt_Shared_t& d, const char* what){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    char t[64];
    snprintf(t, sizeof(t), "%s%s%s", LEAGUE_NAME[s.league], d.group[0] ? "  " : "", d.group);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 3, Terminal4x6, C_PITCH, what, C_HEAD);
}

static uint16_t form_Color(char f){ return f == 'W' ? KIT_GREEN : f == 'D' ? KIT_LABEL : KIT_RED; }

static void draw_Table(const Lt_Save_t& s, const Lt_Shared_t& d){
    header(s, d, "TABLE");
    char t[32];
    const int rowH = 9, top = 21, fit = (H - top) / rowH;      // 11 rows
    mtb_Kit_Text(4, 13, Terminal4x6, KIT_DIM, "#  TEAM", C_BG);
    mtb_Kit_Text_Right(150, 13, Terminal4x6, KIT_DIM, "P", C_BG);
    mtb_Kit_Text_Right(178, 13, Terminal4x6, KIT_DIM, "GD", C_BG);
    mtb_Kit_Text_Right(204, 13, Terminal4x6, KIT_DIM, "PTS", C_BG);
    mtb_Kit_Text(212, 13, Terminal4x6, KIT_DIM, "FORM", C_BG);
    // The window: from the top, unless the owner's team is further down.
    int mine = -1;
    for (int i = 0; i < d.rows; i++) if (d.row[i].id == s.teamId && s.teamId) mine = i;
    int first = 0;
    if (mine >= fit - 1) first = mine - fit / 2;
    if (first > d.rows - fit) first = d.rows - fit;
    if (first < 0) first = 0;
    for (int k = 0; k < fit && first + k < d.rows; k++){
        const Lt_Row_t& r = d.row[first + k];
        const int y = top + k * rowH;
        const bool me = first + k == mine;
        const uint16_t bg = me ? C_MINE : C_BG;
        if (me) mtb_Panel_Fill_Rect(0, y - 1, W - 1, y + rowH - 2, bg);
        snprintf(t, sizeof(t), "%2u", (unsigned)r.pos);
        mtb_Kit_Text(2, y, Terminal6x8, KIT_LABEL, t, bg);
        char name[24];
        snprintf(name, sizeof(name), "%s", r.name); mtb_Kit_Upper(name);
        char f2[24];
        mtb_Kit_Fit(f2, sizeof(f2), Terminal6x8, name, 112);
        mtb_Kit_Text(20, y, Terminal6x8, KIT_TEXT, f2, bg);
        snprintf(t, sizeof(t), "%u", (unsigned)r.played); mtb_Kit_Text_Right(150, y, Terminal6x8, KIT_LABEL, t, bg);
        snprintf(t, sizeof(t), "%+d", r.gd); mtb_Kit_Text_Right(178, y, Terminal6x8, r.gd > 0 ? KIT_GREEN : r.gd < 0 ? KIT_RED : KIT_LABEL, t, bg);
        snprintf(t, sizeof(t), "%d", r.points); mtb_Kit_Text_Right(204, y, Terminal6x8, KIT_GOLD, t, bg);
        for (int j = 0; r.form[j] && j < 5; j++) mtb_Panel_Fill_Rect(212 + j * 8, y + 1, 212 + j * 8 + 5, y + 6, form_Color(r.form[j]));
    }
}

static void draw_Scorers(const Lt_Save_t& s, const Lt_Shared_t& d){
    header(s, d, "TOP SCORERS");
    char t[48];
    mtb_Kit_Text_Right(214, 13, Terminal4x6, KIT_DIM, "GOALS", C_BG);
    mtb_Kit_Text_Right(W - 4, 13, Terminal4x6, KIT_DIM, "AST", C_BG);
    for (int k = 0; k < d.scorers && k < 8; k++){
        const Lt_Scorer_t& p = d.scorer[k];
        const int y = 21 + k * 13;
        snprintf(t, sizeof(t), "%d", k + 1);
        mtb_Kit_Text(3, y, Terminal6x8, KIT_LABEL, t, C_BG);
        char f[32];
        mtb_Kit_Fit(f, sizeof(f), Terminal6x8, p.name, 130);
        mtb_Kit_Text(16, y, Terminal6x8, KIT_TEXT, f, C_BG);
        mtb_Kit_Text(150, y + 1, Terminal4x6, KIT_LABEL, p.team, C_BG);
        snprintf(t, sizeof(t), "%u", (unsigned)p.goals); mtb_Kit_Text_Right(214, y, Terminal6x8, KIT_GOLD, t, C_BG);
        snprintf(t, sizeof(t), "%u", (unsigned)p.assists); mtb_Kit_Text_Right(W - 4, y, Terminal6x8, KIT_LABEL, t, C_BG);
    }
    if (!d.scorers) mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_DIM, "NO SCORERS YET", C_BG);
}

static void draw_Round(const Lt_Save_t& s, const Lt_Shared_t& d){
    char what[24];
    snprintf(what, sizeof(what), "MATCHDAY %u", (unsigned)d.matchday);
    header(s, d, what);
    char t[40];
    const char* mineTla = "";
    for (int i = 0; i < d.rows; i++) if (d.row[i].id == s.teamId && s.teamId) mineTla = d.row[i].tla;
    for (int k = 0; k < d.matches && k < 12; k++){
        const Lt_Match_t& m = d.match[k];
        const int col = k / 6, row = k % 6, x = col * 128 + 4, y = 15 + row * 18;
        const bool me = mineTla[0] && (!strcmp(m.home, mineTla) || !strcmp(m.away, mineTla));
        const uint16_t ink = me ? KIT_CYAN : KIT_TEXT;
        if (m.state == 1 || m.state == 2) snprintf(t, sizeof(t), "%s %d-%d %s", m.home, m.hs < 0 ? 0 : m.hs, m.as < 0 ? 0 : m.as, m.away);
        else snprintf(t, sizeof(t), "%s V %s", m.home, m.away);
        mtb_Kit_Text(x, y, Terminal6x8, ink, t, C_BG);
        char sub[24];
        if (m.state == 1) snprintf(sub, sizeof(sub), "LIVE");
        else if (m.state == 2) snprintf(sub, sizeof(sub), "FULL TIME");
        else if (m.state == 3) snprintf(sub, sizeof(sub), "POSTPONED");
        else {
            const time_t at = (time_t)m.at;
            struct tm l;
            localtime_r(&at, &l);
            static const char* const D[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            if (mtb_Clock_24h()) snprintf(sub, sizeof(sub), "%s %02d:%02d", D[l.tm_wday], l.tm_hour, l.tm_min);
            else snprintf(sub, sizeof(sub), "%s %d:%02d %s", D[l.tm_wday], l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
        }
        mtb_Kit_Text(x, y + 9, Terminal4x6, m.state == 1 ? KIT_RED : KIT_DIM, sub, C_BG);
    }
    if (!d.matches) mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_DIM, "NO MATCHES THIS ROUND", C_BG);
}

static void draw(const Lt_Save_t& s, const Lt_Shared_t& d, int page){
    mtb_Panel_Fill_Screen(C_BG);
    if (!s.key[0]){
        mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, "ADD A FOOTBALL-DATA.ORG KEY", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "FREE AT FOOTBALL-DATA.ORG, THEN IN THE PHONE APP", C_BG);
        return;
    }
    if (!d.known){
        header(s, d, "");
        mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "LOADING...", C_BG);
        return;
    }
    if (page == PAGE_TABLE) draw_Table(s, d);
    else if (page == PAGE_SCORERS) draw_Scorers(s, d);
    else draw_Round(s, d);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void leagueTable_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_League, cmd_Search, cmd_Team, cmd_Key, cmd_Seconds, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; turns = 0; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "ltNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Lt_Shared_t* d = (Lt_Shared_t*)heap_caps_calloc(1, sizeof(Lt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pageAt = millis(), pushedMs = 0, lastDraw = 0;
    int page = PAGE_TABLE;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; }
        const int t = turns;
        if (t){ turns -= t; page = ((page + t) % PAGES + PAGES) % PAGES; pageAt = ms; redraw = true; }
        if (save.seconds && ms - pageAt >= save.seconds * 1000u){ page = (page + 1) % PAGES; pageAt = ms; redraw = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        if (redraw || ms - lastDraw >= 30000u){
            redraw = false;
            lastDraw = ms;
            Lt_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, page);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(80);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void lt_Values(JsonObject values){
    Lt_Save_t s;
    Lt_Shared_t* d = (Lt_Shared_t*)heap_caps_calloc(1, sizeof(Lt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[96];
    JsonObject o;
    if (!s.key[0]){ o = rows.add<JsonObject>(); o["label"] = "football-data.org"; o["value"] = "No key yet"; o["state"] = "warn"; }
    else if (!appOpen){ o = rows.add<JsonObject>(); o["label"] = "Table"; o["value"] = "Read while League Table is open"; o["state"] = "off"; }
    else if (!d->known){ o = rows.add<JsonObject>(); o["label"] = "Table"; o["value"] = d->why[0] ? d->why : "Loading"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        for (int i = 0; i < d->rows; i++){
            if (!(i < 3 || (s.teamId && d->row[i].id == s.teamId))) continue;
            o = rows.add<JsonObject>();
            snprintf(t, sizeof(t), "%u. %s", (unsigned)d->row[i].pos, d->row[i].name);
            o["label"] = t;
            snprintf(t, sizeof(t), "%d points, %u played, goal difference %+d", d->row[i].points, (unsigned)d->row[i].played, d->row[i].gd);
            o["value"] = t;
            o["state"] = s.teamId && d->row[i].id == s.teamId ? "ok" : "info";
        }
        if (d->scorers){ o = rows.add<JsonObject>(); o["label"] = "Top scorer"; snprintf(t, sizeof(t), "%s (%s), %u goals", d->scorer[0].name, d->scorer[0].team, (unsigned)d->scorer[0].goals); o["value"] = t; o["state"] = "info"; }
        if (d->why[0]){ o = rows.add<JsonObject>(); o["label"] = "Last check"; o["value"] = d->why; o["state"] = "warn"; }
    }
    values["league"] = s.league;
    values["seconds"] = s.seconds;
    values["apiKey_set"] = s.key[0] ? 1 : 0;
    JsonObject team = values["team"].to<JsonObject>();
    char id[12];
    snprintf(id, sizeof(id), "%lu", (unsigned long)s.teamId);
    team["name"] = s.teamName; team["value"] = id;
    heap_caps_free(d);
}
