/**
 * Space Launches: a countdown to the next orbital launch, from The Space Devs' Launch Library 2 (no key; 15 requests
 * an hour for everyone on one address, so it asks at most 8). Two requests: the upcoming launches in list form
 * (small), then the next one's details (rocket, mission, orbit, pad, its description scrolled along the bottom by
 * Pixlpal, App API 0.7). Page 0: the next launch with its countdown; page 1: the list. A launch whose time is known
 * only to the day, month or quarter shows "NET" (no earlier than) instead of a countdown. The last ten seconds tick
 * and the liftoff sounds, with the owner's say. Knob: turn or click for the page. NVS "splSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "mtb_text_scroll.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "spl_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "splSave"
#define SAVE_MAGIC  0x53504C01u
#define UA          "Pixlpal-SpaceLaunches/1.0 (https://github.com/Meterbit/pixlpal-apps)"
#define API         "https://ll.thespacedevs.com/2.3.0/launches/"
#define LIST_N      10
#define TICKER_Y    120
#define PROVIDERS   9

static const char* const PROVIDER[PROVIDERS] = {"", "SpaceX", "Rocket Lab", "United Launch Alliance", "Blue Origin", "Arianespace",
    "Indian Space Research Organization", "China Aerospace Science and Technology Corporation", "Mitsubishi Heavy Industries"};

EXT_RAM_BSS_ATTR TaskHandle_t spaceLaunches_Task_H = NULL;
static void spaceLaunches_App_Task(void* dApplication);
static void spl_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* spaceLaunches_App;
Mtb_Applications* spaceLaunches_App_GetInstance(){
    if (!spaceLaunches_App){
        spaceLaunches_App = new Mtb_Applications_FullScreen(spaceLaunches_App_Task, &spaceLaunches_Task_H, "Space Launches", {0,0}, 8192);
        spaceLaunches_App->mtb_App_Set_Mobile_UI(SPL_UI_MANIFEST, SPL_UI_API, nullptr, spl_Values);
    }
    return spaceLaunches_App;
}
MTB_REGISTER_APP(spaceLaunches_App, 0, 0)       // the catalogue gives the app its number

struct Spl_Save_t { uint32_t magic; uint8_t provider, page, sound, pad; };

struct Spl_Item_t {
    char id[40];
    char rocket[40], mission[48];
    char status[12];                    // "Go", "TBD", "TBC", "Hold", "In Flight", "Success", "Failure"...
    char precision[6];                  // "SEC", "MIN", "HR", "DAY", "M", "Q1".."Q4", "H1", "H2", "Y"
    uint32_t net;                       // unix
};
struct Spl_Detail_t {
    char id[40];
    char rocket[48], mission[56], provider[56], orbit[40], type[28], pad[48], place[64];
    char description[600];
    bool webcastLive;
    int probability;                    // -1 unknown
    uint32_t windowStart, windowEnd;
};

struct Spl_Shared_t {
    Spl_Item_t item[LIST_N];
    int items;
    bool listOk;
    Spl_Detail_t detail;
    bool detailOk;
    uint32_t checkedAt;                 // unix
    char why[48];
    uint32_t version;
    volatile bool stop, running, checkNow;
};

static EXT_RAM_BSS_ATTR Spl_Save_t save;
static EXT_RAM_BSS_ATTR Spl_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false;

static bool time_Valid(time_t t){ return t > 1700000000; }

static void read_Save(Spl_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.sound = 1; }
    if (s.provider >= PROVIDERS) s.provider = 0;
    if (s.page > 1) s.page = 0;
    s.sound = s.sound ? 1 : 0;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static void cmd_Provider(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["provider"], 0, PROVIDERS - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (save.provider != (uint8_t)v){ save.provider = (uint8_t)v; sh.checkNow = true; }
    phoneChanged = saveWanted = true;
}
static void cmd_Page(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["page"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.page = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) turns += 1; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, JsonDocument& doc, JsonDocument& filter, size_t max){
    const char* headers[] = {"Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, max, r, UA);
    int code = r.code;
    if (code == 200 && body){
        if (r.truncated || deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) code = -2;
    }
    heap_caps_free(body);
    return code;
}

static void copy_Ascii(char* d, size_t n, const char* s){ mtb_Kit_To_Ascii(d, n, s ? s : ""); }

static int fetch_List(uint8_t provider, Spl_Item_t* out, int& count){
    char url[256], enc[160];
    mtb_Kit_Url_Encode(enc, sizeof(enc), PROVIDER[provider]);
    snprintf(url, sizeof(url), API "upcoming/?limit=%d&mode=list%s%s", LIST_N, provider ? "&lsp__name=" : "", provider ? enc : "");
    JsonDocument doc(mtb_Kit_Json_Allocator()), filter;
    JsonObject f = filter["results"].add<JsonObject>();
    f["id"] = true; f["name"] = true; f["net"] = true; f["status"]["abbrev"] = true; f["net_precision"]["abbrev"] = true;
    const int code = get_Json(url, doc, filter, 40000);
    if (code != 200) return code;
    count = 0;
    for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()){
        if (count >= LIST_N) break;
        Spl_Item_t& it = out[count];
        memset(&it, 0, sizeof(it));
        snprintf(it.id, sizeof(it.id), "%s", r["id"] | "");
        // "Falcon 9 Block 5 | Starlink Group 10-12": the rocket, then the mission.
        char name[128];
        copy_Ascii(name, sizeof(name), r["name"] | "");
        char* bar = strstr(name, " | ");
        if (bar){ *bar = '\0'; snprintf(it.mission, sizeof(it.mission), "%s", bar + 3); }
        snprintf(it.rocket, sizeof(it.rocket), "%s", name);
        snprintf(it.status, sizeof(it.status), "%s", r["status"]["abbrev"] | "");
        snprintf(it.precision, sizeof(it.precision), "%s", r["net_precision"]["abbrev"] | "MIN");
        it.net = mtb_Kit_Parse_Iso_Time(r["net"] | "");
        if (it.id[0] && it.net) count++;
    }
    return 200;
}

static int fetch_Detail(const char* id, Spl_Detail_t& d){
    char url[160];
    snprintf(url, sizeof(url), API "%s/?mode=normal", id);
    JsonDocument doc(mtb_Kit_Json_Allocator()), filter;
    filter["id"] = true; filter["webcast_live"] = true; filter["probability"] = true;
    filter["window_start"] = true; filter["window_end"] = true;
    filter["rocket"]["configuration"]["full_name"] = true; filter["rocket"]["configuration"]["name"] = true;
    filter["launch_service_provider"]["name"] = true;
    filter["mission"]["name"] = true; filter["mission"]["type"] = true; filter["mission"]["description"] = true;
    filter["mission"]["orbit"]["name"] = true;
    filter["pad"]["name"] = true; filter["pad"]["location"]["name"] = true;
    const int code = get_Json(url, doc, filter, 60000);
    if (code != 200) return code;
    memset(&d, 0, sizeof(d));
    snprintf(d.id, sizeof(d.id), "%s", doc["id"] | id);
    const char* rocket = doc["rocket"]["configuration"]["full_name"] | (doc["rocket"]["configuration"]["name"] | "");
    copy_Ascii(d.rocket, sizeof(d.rocket), rocket);
    copy_Ascii(d.mission, sizeof(d.mission), doc["mission"]["name"] | "");
    copy_Ascii(d.provider, sizeof(d.provider), doc["launch_service_provider"]["name"] | "");
    copy_Ascii(d.orbit, sizeof(d.orbit), doc["mission"]["orbit"]["name"] | "");
    copy_Ascii(d.type, sizeof(d.type), doc["mission"]["type"] | "");
    copy_Ascii(d.pad, sizeof(d.pad), doc["pad"]["name"] | "");
    copy_Ascii(d.place, sizeof(d.place), doc["pad"]["location"]["name"] | "");
    copy_Ascii(d.description, sizeof(d.description), doc["mission"]["description"] | "");
    d.webcastLive = doc["webcast_live"] | false;
    d.probability = doc["probability"].is<int>() ? doc["probability"].as<int>() : -1;
    d.windowStart = mtb_Kit_Parse_Iso_Time(doc["window_start"] | "");
    d.windowEnd = mtb_Kit_Parse_Iso_Time(doc["window_end"] | "");
    return 200;
}

/** The launch to feature: the first not more than half an hour past (one just launched stays a while). */
static int featured(const Spl_Item_t* it, int n, time_t now){
    for (int i = 0; i < n; i++) if ((int64_t)it[i].net >= (int64_t)now - 1800) return i;
    return n ? n - 1 : -1;
}

static void why_Of(char* why, size_t n, int code){
    if (code == 429) snprintf(why, n, "LAUNCH LIBRARY: TOO MANY ASKS, WAITING");
    else if (code <= 0) snprintf(why, n, code == -2 ? "LAUNCH LIBRARY'S ANSWER WAS TOO BIG" : "NO ANSWER FROM THE LAUNCH LIBRARY");
    else snprintf(why, n, "LAUNCH LIBRARY: HTTP %d", code);
}

static void worker(void*){
    static Spl_Item_t items[LIST_N];
    static Spl_Detail_t detail;
    uint32_t nextAt = 0, lastCheckMs = 0;
    while (!sh.stop){
        const time_t now = time(nullptr);
        const bool asked = sh.checkNow && (!lastCheckMs || millis() - lastCheckMs >= 60000u);
        if (time_Valid(now) && (asked || (uint32_t)now >= nextAt)){
            sh.checkNow = false;
            lastCheckMs = millis() ? millis() : 1;
            uint8_t provider;
            portENTER_CRITICAL(&lock); provider = save.provider; portEXIT_CRITICAL(&lock);
            int count = 0;
            int code = fetch_List(provider, items, count);
            int k = -1;
            bool detailOk = false;
            if (code == 200){
                k = featured(items, count, now);
                if (k >= 0){
                    const bool same = sh.detailOk && !strcmp(sh.detail.id, items[k].id);
                    // The details change less: again only for another launch, or every hour.
                    if (!same || (uint32_t)now - sh.checkedAt >= 3600u || asked){
                        vTaskDelay(pdMS_TO_TICKS(1500));
                        const int dc = fetch_Detail(items[k].id, detail);
                        detailOk = dc == 200;
                        if (!detailOk && dc == 429) code = 429;
                    } else { detail = sh.detail; detailOk = true; }
                }
            }
            portENTER_CRITICAL(&lock);
            if (code == 200 || (code == 429 && count)){
                memcpy(sh.item, items, sizeof(items));
                sh.items = count; sh.listOk = true;
                if (detailOk){ sh.detail = detail; sh.detailOk = true; }
                else if (k >= 0 && strcmp(sh.detail.id, items[k].id)) sh.detailOk = false;
                sh.checkedAt = (uint32_t)now;
                sh.why[0] = '\0';
                if (code == 429) why_Of(sh.why, sizeof(sh.why), 429);
            } else why_Of(sh.why, sizeof(sh.why), code);
            // Every 15 minutes; every 5 in the hour before the featured launch; a long wait if asked too often.
            uint32_t every = 900;
            if (code == 200 && k >= 0){
                const int64_t to = (int64_t)items[k].net - (int64_t)now;
                if (to > -1800 && to < 3600) every = 300;
            }
            if (code == 429) every = 1200;
            else if (code != 200) every = 180;
            nextAt = (uint32_t)now + every;
            sh.version++;
            portEXIT_CRITICAL(&lock);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 6, 16), C_BAR = MTB_KIT_RGB(16, 22, 48);

static uint16_t status_Color(const char* s){
    if (!strcmp(s, "Go") || !strcmp(s, "Success")) return KIT_GREEN;
    if (!strcmp(s, "TBC")) return KIT_GOLD;
    if (!strcmp(s, "Hold") || !strcmp(s, "Failure") || !strcmp(s, "Partial Failure")) return KIT_RED;
    if (!strcmp(s, "In Flight")) return KIT_CYAN;
    return KIT_LABEL;
}

static bool exact(const char* precision){ return !strcmp(precision, "SEC") || !strcmp(precision, "MIN") || !strcmp(precision, "HR"); }

/** "SAT 29 NOV 04:50", or for a launch known only roughly "NET DEC 2026", "NET Q4 2026". */
static void when_Text(char* t, size_t n, const Spl_Item_t& it, bool withDay){
    static const char* const MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    static const char* const WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const time_t tt = (time_t)it.net;
    struct tm l;
    if (exact(it.precision)) localtime_r(&tt, &l); else gmtime_r(&tt, &l);
    if (exact(it.precision)){
        char c[12];
        if (mtb_Clock_24h()) snprintf(c, sizeof(c), "%02d:%02d", l.tm_hour, l.tm_min);
        else snprintf(c, sizeof(c), "%d:%02d%s", (l.tm_hour + 11) % 12 + 1, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
        if (withDay) snprintf(t, n, "%s %d %s %s%s", WD[l.tm_wday], l.tm_mday, MON[l.tm_mon], !strcmp(it.precision, "HR") ? "~" : "", c);
        else snprintf(t, n, "%d %s %s", l.tm_mday, MON[l.tm_mon], c);
    } else if (!strcmp(it.precision, "DAY")) snprintf(t, n, "NET %d %s", l.tm_mday, MON[l.tm_mon]);
    else if (it.precision[0] == 'Q' || it.precision[0] == 'H') snprintf(t, n, "NET %s %d", it.precision, l.tm_year + 1900);
    else if (!strcmp(it.precision, "Y")) snprintf(t, n, "NET %d", l.tm_year + 1900);
    else snprintf(t, n, "NET %s %d", MON[l.tm_mon], l.tm_year + 1900);
}

static void badge(int rightX, int y, const char* text, uint16_t color, uint16_t bg){
    char t[16];
    snprintf(t, sizeof(t), "%s", text);
    mtb_Kit_Upper(t);
    const int w = mtb_Kit_Text_Width(Terminal4x6, t);
    mtb_Panel_Fill_Rect((int16_t)(rightX - w - 3), (int16_t)y, (int16_t)rightX, (int16_t)(y + 7), color);
    mtb_Kit_Text_Right(rightX - 1, y + 1, Terminal4x6, bg, t, color);
}

static void page_Next(const Spl_Shared_t& d, time_t now, uint32_t ms){
    mtb_Panel_Fill_Rect(0, 0, W - 1, TICKER_Y - 1, C_BG);
    const int k = featured(d.item, d.items, now);
    if (!d.listOk || k < 0){
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL,
                            !time_Valid(now) ? "WAITING FOR THE TIME..." : d.why[0] ? d.why : d.listOk ? "NO LAUNCH PLANNED" : "ASKING THE LAUNCH LIBRARY...", C_BG);
        return;
    }
    const Spl_Item_t& it = d.item[k];
    const bool det = d.detailOk && !strcmp(d.detail.id, it.id);
    char t[96], f[96];
    // The top line: who launches, and the status.
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_BAR);
    snprintf(t, sizeof(t), "%s", det && d.detail.provider[0] ? d.detail.provider : "NEXT LAUNCH");
    mtb_Kit_Upper(t);
    int right = W - 2;
    if (det && d.detail.webcastLive && ((ms / 600) % 2)){ badge(right, 1, "LIVE", KIT_RED, KIT_TEXT); right -= 26; }
    else if (det && d.detail.webcastLive) right -= 26;
    const char* st = it.status[0] ? it.status : "TBD";
    badge(right, 1, st, status_Color(st), C_BAR);
    const int bw = mtb_Kit_Text_Width(Terminal4x6, st) + 8 + (W - 2 - right);
    mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, W - 8 - bw);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_LABEL, f, C_BAR);
    // The rocket and the mission.
    snprintf(t, sizeof(t), "%s", det && d.detail.rocket[0] ? d.detail.rocket : it.rocket);
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), PT_Sans_Narrow_Bold18x22, t, W - 8);
    mtb_Kit_Text(3, 12, PT_Sans_Narrow_Bold18x22, KIT_TEXT, f, C_BG);
    snprintf(t, sizeof(t), "%s", det && d.detail.mission[0] ? d.detail.mission : it.mission);
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal8x12, t, W - 8);
    mtb_Kit_Text(3, 36, Terminal8x12, KIT_CYAN, f, C_BG);
    if (det){
        snprintf(t, sizeof(t), "%s%s%s", d.detail.orbit, d.detail.orbit[0] && d.detail.place[0] ? "  -  " : "", d.detail.place);
        mtb_Kit_Upper(t);
        mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, W - 8);
        mtb_Kit_Text(3, 50, Terminal4x6, KIT_LABEL, f, C_BG);
    }
    // The countdown, or when it is expected.
    const int64_t to = (int64_t)it.net - (int64_t)now;
    if (exact(it.precision) && to > -3600){
        const int64_t a = to < 0 ? -to : to;
        const int days = (int)(a / 86400), hh = (int)(a / 3600 % 24), mm = (int)(a / 60 % 60), ss = (int)(a % 60);
        if (days) snprintf(t, sizeof(t), "%s%d:%02d:%02d", to >= 0 ? "T- " : "T+ ", hh, mm, ss);
        else snprintf(t, sizeof(t), "%s%02d:%02d:%02d", to >= 0 ? "T- " : "T+ ", hh, mm, ss);
        const bool hold = !strcmp(it.status, "Hold");
        const uint16_t c = hold ? KIT_RED : to < 0 ? KIT_GREEN : !strcmp(it.status, "Go") ? KIT_GOLD : KIT_TEXT;
        if (to <= 0 && to > -60 && !strcmp(it.status, "Go")) mtb_Kit_Text_Centre(W / 2, 60, PT_Sans_Narrow_Bold28x34, (ms / 300) % 2 ? KIT_GOLD : KIT_ORANGE, "LIFTOFF!", C_BG);
        else mtb_Kit_Text_Centre(days ? W / 2 + 22 : W / 2, 60, PT_Sans_Narrow_Bold28x34, c, t, C_BG);
        if (days){
            snprintf(f, sizeof(f), "%d DAY%s", days, days == 1 ? "" : "S");
            const int tw = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, t);
            mtb_Kit_Text_Right(W / 2 + 22 - tw / 2 - 6, 72, Terminal8x12, c, f, C_BG);
        }
    } else {
        when_Text(t, sizeof(t), it, true);
        mtb_Kit_Text_Centre(W / 2, 60, PT_Sans_Narrow_Bold28x34, KIT_LABEL, t, C_BG);
    }
    when_Text(t, sizeof(t), it, true);
    if (!exact(it.precision)) snprintf(t, sizeof(t), "THE DAY IS NOT SET YET");
    else if (det && d.detail.windowEnd > d.detail.windowStart){
        const int win = (int)((d.detail.windowEnd - d.detail.windowStart + 30) / 60);
        char wt[24];
        if (win >= 120) snprintf(wt, sizeof(wt), "  WINDOW %d H", win / 60); else if (win) snprintf(wt, sizeof(wt), "  WINDOW %d MIN", win); else snprintf(wt, sizeof(wt), "  INSTANTANEOUS");
        strncat(t, wt, sizeof(t) - strlen(t) - 1);
    }
    mtb_Kit_Text_Centre(W / 2, 98, Terminal6x8, KIT_TEXT, t, C_BG);
    if (det && d.detail.probability >= 0){
        snprintf(t, sizeof(t), "WEATHER %d%% GO", d.detail.probability);
        mtb_Kit_Text_Centre(W / 2, 109, Terminal4x6, d.detail.probability >= 70 ? KIT_GREEN : d.detail.probability >= 40 ? KIT_GOLD : KIT_RED, t, C_BG);
    }
}

static void page_List(const Spl_Shared_t& d, time_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, TICKER_Y - 1, C_BG);
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_BAR);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_TEXT, save.provider ? "UPCOMING LAUNCHES: " : "UPCOMING LAUNCHES", C_BAR);
    if (save.provider){
        char t[64];
        snprintf(t, sizeof(t), "%s", save.provider == 3 ? "ULA" : save.provider == 6 ? "ISRO" : save.provider == 7 ? "CASC" : save.provider == 8 ? "MHI" : PROVIDER[save.provider]);
        mtb_Kit_Upper(t);
        mtb_Kit_Text(3 + mtb_Kit_Text_Width(Terminal6x8, "UPCOMING LAUNCHES: "), 1, Terminal6x8, KIT_CYAN, t, C_BAR);
    }
    if (!d.listOk){
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "ASKING THE LAUNCH LIBRARY...", C_BG);
        return;
    }
    int y = 13;
    for (int i = 0; i < d.items && y < TICKER_Y - 10; i++){
        const Spl_Item_t& it = d.item[i];
        if ((int64_t)it.net < (int64_t)now - 1800) continue;
        char t[64], f[64];
        when_Text(t, sizeof(t), it, false);
        mtb_Kit_Text(3, y + 1, Terminal4x6, KIT_LABEL, t, C_BG);
        snprintf(t, sizeof(t), "%s", it.rocket);
        mtb_Kit_Upper(t);
        mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, 92);
        mtb_Kit_Text(64, y, Terminal6x8, KIT_TEXT, f, C_BG);
        snprintf(t, sizeof(t), "%s", it.mission);
        mtb_Kit_Upper(t);
        mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, 76);
        mtb_Kit_Text(160, y + 1, Terminal4x6, KIT_CYAN, f, C_BG);
        mtb_Panel_Fill_Rect(W - 6, (int16_t)(y + 1), W - 3, (int16_t)(y + 5), status_Color(it.status));
        y += 11;
    }
    if (y == 13) mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, KIT_LABEL, "NO LAUNCH PLANNED", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void spaceLaunches_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Provider, cmd_Page, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = saveWanted = false;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "splNet", 16384, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Spl_Shared_t* d = (Spl_Shared_t*)heap_caps_calloc(1, sizeof(Spl_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    Mtb_ScrollText_t ticker(0, TICKER_Y, W, Terminal6x8, MTB_KIT_RGB(150, 190, 255), 25, 1);
    ticker.backgroundColor = C_BG;
    char tickerFor[40] = "";
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0;
    int64_t lastBeep = 0;
    bool redraw = true, push = true, tickerOn = false;
    mtb_Panel_Fill_Rect(0, TICKER_Y, W - 1, H - 1, C_BG);
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const time_t now = time(nullptr);
        if (phoneChanged){ phoneChanged = false; redraw = push = true; }
        const int t = turns;
        if (t){ turns -= t; save.page = (uint8_t)(save.page ^ (t & 1)); redraw = push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        // The last ten seconds, and the liftoff.
        const int k = featured(d->item, d->items, now);
        if (save.sound && k >= 0 && exact(d->item[k].precision) && !strcmp(d->item[k].status, "Go")){
            const int64_t to = (int64_t)d->item[k].net - (int64_t)now;
            if (to >= 0 && to <= 10 && to != lastBeep){
                lastBeep = to;
                static const Snd_Note_t TICK[] = {{880, 60}};
                static const Snd_Note_t LIFT[] = {{392, 150}, {523, 150}, {659, 150}, {784, 400}};
                if (to) snd_Play(TICK, 1, 0.2f, false); else snd_Play(LIFT, 4, 0.3f, false);
            }
        }
        if (redraw || ms - lastDraw >= 250u){
            redraw = false;
            lastDraw = ms;
            if (save.page == 0) page_Next(*d, now, ms); else page_List(*d, now);
        }
        // The featured mission's description along the bottom, over and over.
        const bool want = k >= 0 && d->detailOk && !strcmp(d->detail.id, d->item[k].id) && d->detail.description[0];
        if (want && (!tickerOn || strcmp(tickerFor, d->detail.id) || !ticker.mtb_Scroll_Active())){
            ticker.mtb_Scroll_This_Text(d->detail.description);
            snprintf(tickerFor, sizeof(tickerFor), "%s", d->detail.id);
            tickerOn = true;
        } else if (!want && tickerOn){ ticker.mtb_Scroll_Active(STOP_SCROLL); tickerOn = false; mtb_Panel_Fill_Rect(0, TICKER_Y, W - 1, H - 1, C_BG); }
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push && ms - pushedMs >= 3000) || ms - pushedMs >= 30000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    ticker.mtb_Scroll_Active(STOP_SCROLL);
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void spl_Values(JsonObject values){
    Spl_Save_t s;
    Spl_Shared_t* d = (Spl_Shared_t*)heap_caps_calloc(1, sizeof(Spl_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(d, &sh, sizeof(sh)); portEXIT_CRITICAL(&lock); } else read_Save(s);
    const time_t now = time(nullptr);
    char t[160], w[40];
    JsonArray next = values["next"].to<JsonArray>();
    JsonArray later = values["later"].to<JsonArray>();
    const int k = featured(d->item, d->items, now);
    if (!appOpen || !d->listOk || k < 0){
        JsonObject o = next.add<JsonObject>();
        o["label"] = "Next launch";
        o["value"] = !appOpen ? "While Space Launches is open" : d->why[0] ? d->why : d->listOk ? "None planned" : "Asking";
        o["state"] = d->why[0] ? "warn" : "off";
    } else {
        const Spl_Item_t& it = d->item[k];
        const bool det = d->detailOk && !strcmp(d->detail.id, it.id);
        JsonObject o = next.add<JsonObject>();
        o["label"] = it.status[0] ? it.status : "TBD";
        snprintf(t, sizeof(t), "%s, %s", det && d->detail.rocket[0] ? d->detail.rocket : it.rocket, det && d->detail.mission[0] ? d->detail.mission : it.mission);
        o["value"] = t;
        o["state"] = !strcmp(it.status, "Go") ? "ok" : !strcmp(it.status, "Hold") ? "error" : "info";
        o = next.add<JsonObject>();
        o["label"] = "When";
        when_Text(w, sizeof(w), it, true);
        o["value"] = w; o["state"] = "info";
        if (det && d->detail.place[0]){
            o = next.add<JsonObject>();
            o["label"] = "From";
            snprintf(t, sizeof(t), "%s, %s", d->detail.pad, d->detail.place);
            o["value"] = t; o["state"] = "info";
        }
        int n = 0;
        for (int i = k + 1; i < d->items && n < 6; i++, n++){
            const Spl_Item_t& x = d->item[i];
            JsonObject r = later.add<JsonObject>();
            when_Text(w, sizeof(w), x, false);
            r["label"] = w;
            snprintf(t, sizeof(t), "%s, %s (%s)", x.rocket, x.mission, x.status);
            r["value"] = t;
            r["state"] = !strcmp(x.status, "Go") ? "ok" : "off";
        }
    }
    values["provider"] = s.provider;
    values["page"] = s.page;
    values["sound"] = s.sound;
    heap_caps_free(d);
}
