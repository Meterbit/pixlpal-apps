/**
 * Cricket Live: current cricket matches from CricketData.org (GET /v1/currentMatches, the owner's free key, 100 a
 * day), one at a time: the series and format, each team's innings (runs/wickets and overs), the match's status ("India
 * need 34 runs in 20 balls", "Australia won by 5 wickets"), live ones marked. The owner's team's matches come first;
 * a wicket in its match (the wickets of an innings going up between checks) gets a call and a banner.
 *
 * Checked every 20 minutes by default (72 of the free plan's 100 a day). NVS "ckSave".
 * Knob: turn, the next match; click, check now.
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
#include "ck_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "ckSave"
#define SAVE_MAGIC  0x434B4C01u
#define UA          "Pixlpal-CricketLive/1.0"
#define KEY_LEN     48
#define MATCHES     12
#define INNINGS     4

EXT_RAM_BSS_ATTR TaskHandle_t cricketLive_Task_H = NULL;
static void cricketLive_App_Task(void* dApplication);
static void ck_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* cricketLive_App;
Mtb_Applications* cricketLive_App_GetInstance(){
    if (!cricketLive_App){
        cricketLive_App = new Mtb_Applications_FullScreen(cricketLive_App_Task, &cricketLive_Task_H, "Cricket Live", {0,0}, 8192);
        cricketLive_App->mtb_App_Set_Mobile_UI(CK_UI_MANIFEST, CK_UI_API, nullptr, ck_Values);
    }
    return cricketLive_App;
}
MTB_REGISTER_APP(cricketLive_App, 0, 0)       // the catalogue gives the app its number

struct Ck_Save_t {
    uint32_t magic;
    char key[KEY_LEN];
    char team[32];
    uint8_t minutes, seconds, sound;
};

struct Ck_Innings_t { char who[28]; uint16_t r; uint8_t w; float o; };
struct Ck_Match_t {
    char id[40], name[64], type[8], status[96], short1[8], short2[8];
    Ck_Innings_t inn[INNINGS];
    uint8_t innings;
    bool started, ended, mine;
};

struct Ck_Shared_t {
    Ck_Match_t m[MATCHES];
    uint8_t count;
    bool known;
    int hits, limit;
    char why[44];
    uint32_t at, version;
    uint8_t wicket;                     // a wicket in the owner's team's match since the last check: 1 + its index
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Ck_Save_t save;
static EXT_RAM_BSS_ATTR Ck_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Ck_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 20; s.seconds = 10; s.sound = 1; }
    if (s.minutes < 10 || s.minutes > 60) s.minutes = 20;
    if (s.seconds && (s.seconds < 5 || s.seconds > 60)) s.seconds = 10;
    s.key[KEY_LEN - 1] = s.team[sizeof(s.team) - 1] = '\0';
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!(isalnum((unsigned char)*k) || *k == '-')){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); sh.known = false; sh.why[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Team(JsonDocument& c){
    const char* v = c["team"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); mtb_Kit_To_Ascii(save.team, sizeof(save.team), v); portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 10, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint8_t)v; phoneChanged = true; }
static void cmd_Seconds(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seconds"], 0, 60, v) || (v && v < 5)){ mtb_Ble_App_Cmd_Reject(); return; } save.seconds = (uint8_t)v; phoneChanged = true; }
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static bool has_Word(const char* text, const char* word){
    if (!word[0]) return false;
    char a[96], b[40];
    mtb_Kit_To_Ascii(a, sizeof(a), text); mtb_Kit_Upper(a);
    mtb_Kit_To_Ascii(b, sizeof(b), word); mtb_Kit_Upper(b);
    return strstr(a, b) != nullptr;
}

static void check(const Ck_Save_t& s, Ck_Shared_t& r, char* why, size_t whyN){
    char url[200];
    snprintf(url, sizeof(url), "https://api.cricapi.com/v1/currentMatches?apikey=%s&offset=0", s.key);
    JsonDocument doc, filter;
    filter["status"] = true; filter["reason"] = true;
    filter["info"]["hitsToday"] = true; filter["info"]["hitsLimit"] = true;
    JsonObject f = filter["data"][0].to<JsonObject>();
    f["id"] = true; f["name"] = true; f["matchType"] = true; f["status"] = true; f["matchStarted"] = true; f["matchEnded"] = true;
    f["teams"] = true; f["teamInfo"][0]["name"] = true; f["teamInfo"][0]["shortname"] = true;
    JsonObject sc = f["score"][0].to<JsonObject>();
    sc["r"] = true; sc["w"] = true; sc["o"] = true; sc["inning"] = true;
    Mtb_Kit_Http_t h = {};
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 60000, h, UA);
    memset(url, 0, sizeof(url));
    int code = h.code;
    if (code == 200 && body && (h.truncated || deserializeJson(doc, body, h.length, DeserializationOption::Filter(filter)))) code = -2;
    heap_caps_free(body);
    if (code != 200){
        if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, whyN, "NOT ALLOWED");
        else if (code == -2) snprintf(why, whyN, "AN ANSWER PIXLPAL CAN'T READ");
        else if (code <= 0) snprintf(why, whyN, "NO ANSWER FROM CRICKETDATA.ORG");
        else snprintf(why, whyN, "CRICKETDATA.ORG: HTTP %d", code);
        return;
    }
    if (strcmp(doc["status"] | "", "success")){
        char m[64];
        mtb_Kit_To_Ascii(m, sizeof(m), doc["reason"] | "refused the request"); mtb_Kit_Upper(m);
        snprintf(why, whyN, "CRICKETDATA: %.30s", m);
        return;
    }
    r.hits = doc["info"]["hitsToday"] | -1;
    r.limit = doc["info"]["hitsLimit"] | -1;
    // Read every match, then put the owner's team's first, live ones next.
    static Ck_Match_t all[MATCHES * 2];
    int n = 0;
    for (JsonVariantConst m : doc["data"].as<JsonArrayConst>()){
        if (n >= MATCHES * 2) break;
        Ck_Match_t& o = all[n++];
        memset(&o, 0, sizeof(o));
        snprintf(o.id, sizeof(o.id), "%s", m["id"] | "");
        mtb_Kit_To_Ascii(o.name, sizeof(o.name), m["name"] | "");
        mtb_Kit_To_Ascii(o.type, sizeof(o.type), m["matchType"] | ""); mtb_Kit_Upper(o.type);
        mtb_Kit_To_Ascii(o.status, sizeof(o.status), m["status"] | "");
        o.started = m["matchStarted"] | false;
        o.ended = m["matchEnded"] | false;
        const char* t1 = m["teams"][0] | "", *t2 = m["teams"][1] | "";
        o.mine = has_Word(t1, s.team) || has_Word(t2, s.team);
        // Short names, from teamInfo matched by name (its order may differ).
        for (JsonVariantConst ti : m["teamInfo"].as<JsonArrayConst>()){
            const char* nm = ti["name"] | "";
            char* dst = !strcasecmp(nm, t1) ? o.short1 : !strcasecmp(nm, t2) ? o.short2 : nullptr;
            if (dst){ mtb_Kit_To_Ascii(dst, 8, ti["shortname"] | ""); mtb_Kit_Upper(dst); }
        }
        if (!o.short1[0]){ snprintf(o.short1, 8, "%.3s", t1); mtb_Kit_Upper(o.short1); }
        if (!o.short2[0]){ snprintf(o.short2, 8, "%.3s", t2); mtb_Kit_Upper(o.short2); }
        for (JsonVariantConst sc : m["score"].as<JsonArrayConst>()){
            if (o.innings >= INNINGS) break;
            Ck_Innings_t& in = o.inn[o.innings++];
            mtb_Kit_To_Ascii(in.who, sizeof(in.who), sc["inning"] | "");
            char* cut = strstr(in.who, " Inning"); if (cut) *cut = '\0';       // "India Inning 1" -> "India"
            in.r = sc["r"] | 0; in.w = sc["w"] | 0; in.o = sc["o"] | 0.0f;
        }
    }
    r.count = 0;
    for (int pass = 0; pass < 3 && r.count < MATCHES; pass++)
        for (int i = 0; i < n && r.count < MATCHES; i++){
            const Ck_Match_t& o = all[i];
            const bool live = o.started && !o.ended;
            if ((pass == 0 && o.mine) || (pass == 1 && !o.mine && live) || (pass == 2 && !o.mine && !live)) r.m[r.count++] = o;
        }
    r.known = true;
}

static void worker(void*){
    uint32_t next = 0;
    static Ck_Shared_t r;
    while (!sh.stop){
        if (!sh.checkNow && (int32_t)(millis() - next) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Ck_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        next = millis() + s.minutes * 60000u;
        if (!s.key[0]) continue;
        portENTER_CRITICAL(&lock); r = sh; portEXIT_CRITICAL(&lock);
        r.wicket = 0;
        char why[44] = "";
        check(s, r, why, sizeof(why));
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.key, save.key)){
            if (!why[0]){
                // A wicket: an innings of one of the owner's team's matches with more wickets than before.
                if (sh.known)
                    for (int i = 0; i < r.count && !r.wicket; i++){
                        if (!r.m[i].mine) continue;
                        for (int j = 0; j < sh.count; j++){
                            if (strcmp(sh.m[j].id, r.m[i].id)) continue;
                            for (int k = 0; k < r.m[i].innings && k < sh.m[j].innings; k++) if (r.m[i].inn[k].w > sh.m[j].inn[k].w) r.wicket = (uint8_t)(i + 1);
                        }
                    }
                memcpy(sh.m, r.m, sizeof(r.m)); sh.count = r.count; sh.known = true; sh.hits = r.hits; sh.limit = r.limit;
                if (r.wicket) sh.wicket = r.wicket;
                sh.at = (uint32_t)time(nullptr);
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
static const uint16_t C_BG = MTB_KIT_RGB(4, 10, 6), C_HEAD = MTB_KIT_RGB(14, 46, 22), C_LIVE = MTB_KIT_RGB(235, 40, 40);

static void draw(const Ck_Save_t& s, const Ck_Shared_t& d, int sel, const char* banner, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[96], fit[96];
    if (!s.key[0]){
        mtb_Kit_Text_Centre(W / 2, 48, Terminal8x12, KIT_TEXT, "ADD A CRICKETDATA.ORG KEY", C_BG);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal4x6, KIT_LABEL, "FREE AT CRICKETDATA.ORG, THEN IN THE PHONE APP", C_BG);
        return;
    }
    if (!d.known){ mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "LOADING...", C_BG); return; }
    if (!d.count){ mtb_Kit_Text_Centre(W / 2, 58, Terminal8x12, KIT_LABEL, "NO MATCHES ON NOW", C_BG); return; }
    const Ck_Match_t& m = d.m[sel % d.count];
    const bool live = m.started && !m.ended;
    // The header: the series (the match's name) and format.
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", m.name); mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - 60);
    mtb_Kit_Text(3, 3, Terminal4x6, KIT_TEXT, fit, C_HEAD);
    snprintf(t, sizeof(t), "%s %d/%d", m.type, sel % d.count + 1, d.count);
    mtb_Kit_Text_Right(W - 3, 3, Terminal4x6, KIT_LABEL, t, C_HEAD);
    // The innings, or the two teams before the start.
    if (!m.innings){
        snprintf(t, sizeof(t), "%s  V  %s", m.short1, m.short2);
        mtb_Kit_Text_Centre(W / 2, 22, PT_Sans_Narrow_Bold18x22, KIT_TEXT, t, C_BG);
    }
    const int shown = m.innings > 3 ? 3 : m.innings, first = m.innings - shown;
    for (int k = 0; k < shown; k++){
        const Ck_Innings_t& in = m.inn[first + k];
        const int y = 14 + k * 24;
        const bool current = live && first + k == m.innings - 1;
        char who[28];
        snprintf(who, sizeof(who), "%s", in.who); mtb_Kit_Upper(who);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, who, 120);
        mtb_Kit_Text(4, y + 5, Terminal8x12, current ? KIT_TEXT : KIT_LABEL, fit, C_BG);
        snprintf(t, sizeof(t), "%u/%u", (unsigned)in.r, (unsigned)in.w);
        const int16_t rx = mtb_Kit_Text_Right(200, y, PT_Sans_Narrow_Bold18x22, current ? KIT_GOLD : KIT_TEXT, t, C_BG);
        (void)rx;
        snprintf(t, sizeof(t), "(%.1f)", in.o);
        mtb_Kit_Text_Right(W - 4, y + 7, Terminal6x8, KIT_LABEL, t, C_BG);
    }
    // The status.
    char lines[2][64];
    char st[96];
    snprintf(st, sizeof(st), "%s", m.status); mtb_Kit_Upper(st);
    const int n = mtb_Kit_Wrap(st, Terminal6x8, W - 30, &lines[0][0], sizeof(lines[0]), 2);
    for (int k = 0; k < n; k++) mtb_Kit_Text(4, 106 + k * 10 - (n == 2 ? 6 : 0), Terminal6x8, m.ended ? KIT_GREEN : KIT_CYAN, lines[k], C_BG);
    if (live && (ms / 600) % 2) mtb_Panel_Fill_Circle(W - 8, 110, 3, C_LIVE);
    if (live) mtb_Kit_Text_Right(W - 14, 107, Terminal4x6, C_LIVE, "LIVE", C_BG);
    if (banner[0]){
        const bool on = (ms / 300) % 2;
        mtb_Panel_Fill_Rect(30, 38, W - 31, 78, on ? C_LIVE : MTB_KIT_RGB(80, 0, 0));
        mtb_Kit_Text_Centre(W / 2, 44, PT_Sans_Narrow_Bold28x34, KIT_TEXT, banner, on ? C_LIVE : MTB_KIT_RGB(80, 0, 0));
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void cricketLive_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Key, cmd_Team, cmd_Minutes, cmd_Seconds, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; turns = 0; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "ckNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Ck_Shared_t* d = (Ck_Shared_t*)heap_caps_calloc(1, sizeof(Ck_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pageAt = millis(), pushedMs = 0, lastDraw = 0, bannerUntil = 0;
    int sel = 0;
    char banner[16] = "";
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; if (banner[0]) bannerUntil = ms; else sh.checkNow = true; }
        const int t = turns;
        if (t){ turns -= t; sel += t; if (sel < 0) sel += 1000; pageAt = ms; redraw = true; }
        uint8_t wicket;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        wicket = sh.wicket; sh.wicket = 0;
        portEXIT_CRITICAL(&lock);
        if (wicket){
            sel = wicket - 1; pageAt = ms;
            snprintf(banner, sizeof(banner), "WICKET!");
            bannerUntil = ms + 8000;
            static const Snd_Note_t N[] = {{880, 120}, {0, 40}, {880, 120}, {0, 40}, {660, 300}};
            if (save.sound && !mtb_Silent_Mode()) snd_Play(N, 5, 0.3f, true);
        }
        if (banner[0] && (int32_t)(ms - bannerUntil) >= 0){ banner[0] = '\0'; redraw = true; }
        if (save.seconds && d->count > 1 && !banner[0] && ms - pageAt >= save.seconds * 1000u){ sel++; pageAt = ms; redraw = true; }
        bool anyLive = false;
        if (d->count){ const Ck_Match_t& m = d->m[sel % d->count]; anyLive = m.started && !m.ended; }
        if (redraw || banner[0] || (anyLive && ms - lastDraw >= 600u) || ms - lastDraw >= 30000u){
            redraw = false;
            lastDraw = ms;
            Ck_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, d->count ? sel % d->count : 0, banner, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(banner[0] ? 50 : 100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void ck_Values(JsonObject values){
    Ck_Save_t s;
    Ck_Shared_t* d = (Ck_Shared_t*)heap_caps_calloc(1, sizeof(Ck_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[140];
    JsonObject o;
    if (!s.key[0]){ o = rows.add<JsonObject>(); o["label"] = "CricketData.org"; o["value"] = "No key yet"; o["state"] = "warn"; }
    else if (!appOpen){ o = rows.add<JsonObject>(); o["label"] = "Matches"; o["value"] = "Checked while Cricket Live is open"; o["state"] = "off"; }
    else if (!d->known){ o = rows.add<JsonObject>(); o["label"] = "Matches"; o["value"] = d->why[0] ? d->why : "Loading"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        for (int i = 0; i < d->count && i < 5; i++){
            const Ck_Match_t& m = d->m[i];
            o = rows.add<JsonObject>();
            snprintf(t, sizeof(t), "%s v %s", m.short1, m.short2);
            o["label"] = t;
            o["value"] = m.status;
            o["state"] = m.started && !m.ended ? "warn" : m.ended ? "ok" : "info";
        }
        if (d->limit > 0){ o = rows.add<JsonObject>(); o["label"] = "Checks today"; snprintf(t, sizeof(t), "%d of %d", d->hits, d->limit); o["value"] = t; o["state"] = d->hits >= d->limit ? "warn" : "off"; }
        if (d->why[0]){ o = rows.add<JsonObject>(); o["label"] = "Last check"; o["value"] = d->why; o["state"] = "warn"; }
    }
    values["apiKey_set"] = s.key[0] ? 1 : 0;
    values["team"] = s.team; values["minutes"] = s.minutes; values["seconds"] = s.seconds; values["sound"] = s.sound;
    heap_caps_free(d);
}
