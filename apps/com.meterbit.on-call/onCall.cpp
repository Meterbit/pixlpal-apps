/**
 * On-Call Board: a team's open incidents and who is on call, from PagerDuty (US or EU account) or incident.io.
 *
 * Along the top, the state in a band seen across the room: red and flashing while an incident no one has taken is
 * open (PagerDuty's triggered, incident.io's triage), amber while incidents are acknowledged or being worked on,
 * green when all is clear. Below, the incidents, newest first: their number, title, urgency or severity, service,
 * who has them and how long ago; and along the bottom, who is on call now and until when.
 *
 * A new incident sounds an alarm (with "nag", again every minute while any is untaken); a click quiets it until the
 * next one. PagerDuty: GET /incidents (triggered, then acknowledged) and /oncalls (first level, earliest). incident.io:
 * GET /v2/incidents (triage, then live) and /v2/schedule_entries for the schedule given. The key comes from the
 * phone and stays on Pixlpal (NVS "oncSave"). Knob: click, quiet the alarm or check now; turn, more incidents.
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
#include "onc_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#define W           256
#define H           128
#define INCIDENTS   8
#define ONCALL      3
#define KEY_LEN     100
#define WHO_LEN     48
#define NVS_KEY     "oncSave"
#define SAVE_MAGIC  0x4F4E4301u
#define UA          "Pixlpal-OnCall/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t onCall_Task_H = NULL;
static void onCall_App_Task(void* dApplication);
static void onc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* onCall_App;
Mtb_Applications* onCall_App_GetInstance(){
    if (!onCall_App){
        onCall_App = new Mtb_Applications_FullScreen(onCall_App_Task, &onCall_Task_H, "On-Call Board", {0,0}, 8192);
        onCall_App->mtb_App_Set_Mobile_UI(ONC_UI_MANIFEST, ONC_UI_API, nullptr, onc_Values);
    }
    return onCall_App;
}
MTB_REGISTER_APP(onCall_App, 0, 0)       // the catalogue gives the app its number

enum : uint8_t { SVC_PAGERDUTY = 0, SVC_PAGERDUTY_EU, SVC_INCIDENTIO, SVC_COUNT };

struct Onc_Save_t {
    uint32_t magic;
    uint8_t service;
    char key[KEY_LEN];
    char who[WHO_LEN];          // PagerDuty: an escalation policy's ID; incident.io: a schedule's ID
    uint8_t minutes, sound, nag;
};

enum : uint8_t { ST_NEW = 0, ST_TAKEN };   // untaken (triggered, triage) or taken (acknowledged, live)

struct Onc_Incident_t {
    char ref[16], title[80], service[32], who[32], level[16];
    uint8_t state;
    bool high;                  // high urgency, or the most severe
    uint32_t at;
};

struct Onc_Person_t { char name[32], what[32]; uint32_t until; };

struct Onc_Shared_t {
    Onc_Incident_t inc[INCIDENTS];
    uint8_t count;
    uint32_t newTotal, takenTotal;
    Onc_Person_t person[ONCALL];
    uint8_t people;
    bool peopleKnown;
    char why[48];
    bool known;
    uint32_t roundAt, version;
    uint8_t fresh;              // incidents not there the round before
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Onc_Save_t save;
static EXT_RAM_BSS_ATTR Onc_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Onc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 2; s.sound = 1; }
    if (s.service >= SVC_COUNT) s.service = SVC_PAGERDUTY;
    if (s.minutes < 1 || s.minutes > 15) s.minutes = 2;
    s.key[KEY_LEN - 1] = s.who[WHO_LEN - 1] = '\0';
}

static bool printable_Token(const char* v){
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~') return false;
    return true;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void changed(void){ portENTER_CRITICAL(&lock); memset((void*)&sh.inc, 0, sizeof(sh.inc)); sh.count = 0; sh.known = false; sh.peopleKnown = false; sh.people = 0; sh.why[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock); sh.checkNow = true; phoneChanged = true; }
static void cmd_Service(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["service"], 0, SVC_COUNT - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (save.service == (uint8_t)v) return;
    save.service = (uint8_t)v;
    save.who[0] = '\0';                                     // a policy's ID means nothing to the other service
    changed();
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= KEY_LEN || !printable_Token(v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, KEY_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Who(JsonDocument& c){
    const char* v = c["who"] | (const char*)nullptr;
    if (!v || strlen(v) >= WHO_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!isalnum((unsigned char)*k)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.who, WHO_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 15, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.minutes = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    bool any = false;
    if (!c["sound"].isNull()){ if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; any = true; }
    if (!c["nag"].isNull()){ if (!mtb_Kit_Json_Range(c["nag"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.nag = (uint8_t)v; any = true; }
    if (!any){ mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static void copy_Ascii(char* dest, size_t n, const char* src){
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s", src ? src : "");
    for (char* k = tmp; *k; k++) if (*k == '\n' || *k == '\r'){ *k = '\0'; break; }
    mtb_Kit_To_Ascii(dest, n, tmp);
}

static void error_Text(int code, const char* name, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "NOT ALLOWED");
    else if (code == -2) snprintf(why, n, "%s'S ANSWER WAS TOO LONG", name);
    else if (code <= 0) snprintf(why, n, "NO ANSWER FROM %s", name);
    else if (code == 401) snprintf(why, n, "%s REFUSED THE KEY", name);
    else if (code == 403) snprintf(why, n, "THE KEY MAY NOT READ THIS");
    else if (code == 404) snprintf(why, n, "NOT FOUND: CHECK THE ID");
    else if (code == 429) snprintf(why, n, "%s SAYS WAIT: RATE LIMITED", name);
    else snprintf(why, n, "%s: HTTP %d", name, code);
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

static void iso_Time(char* out, size_t n, time_t t){
    struct tm u;
    gmtime_r(&t, &u);
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec);
}

struct Onc_Round_t {
    Onc_Incident_t inc[INCIDENTS];
    uint8_t count;
    uint32_t newTotal, takenTotal;
    Onc_Person_t person[ONCALL];
    uint8_t people;
    bool peopleKnown;
};

// PagerDuty ----------------------------------------------------------------------------------------------------------------------
static int pd_Incidents(const char* host, const char* auth, const char* status, uint8_t state, Onc_Round_t& r, uint32_t& total){
    char url[200];
    snprintf(url, sizeof(url), "https://%s/incidents?statuses%%5B%%5D=%s&limit=6&total=true&sort_by=created_at:desc", host, status);
    const char* headers[] = {"Authorization", auth, "Accept", "application/vnd.pagerduty+json;version=2", nullptr};
    JsonDocument doc, filter;
    filter["total"] = true;
    JsonObject f = filter["incidents"][0].to<JsonObject>();
    f["incident_number"] = true; f["title"] = true; f["urgency"] = true; f["created_at"] = true;
    f["service"]["summary"] = true; f["assignments"][0]["assignee"]["summary"] = true;
    const int code = get_Json(url, headers, doc, filter);
    if (code != 200) return code;
    JsonArrayConst list = doc["incidents"].as<JsonArrayConst>();
    total = doc["total"] | (uint32_t)list.size();
    for (JsonVariantConst i : list){
        if (r.count >= INCIDENTS) break;
        Onc_Incident_t& it = r.inc[r.count++];
        memset(&it, 0, sizeof(it));
        snprintf(it.ref, sizeof(it.ref), "#%lu", (unsigned long)(i["incident_number"] | 0u));
        copy_Ascii(it.title, sizeof(it.title), i["title"] | "");
        copy_Ascii(it.service, sizeof(it.service), i["service"]["summary"] | "");
        copy_Ascii(it.who, sizeof(it.who), i["assignments"][0]["assignee"]["summary"] | "");
        const char* u = i["urgency"] | "";
        it.high = !strcmp(u, "high");
        snprintf(it.level, sizeof(it.level), "%s", it.high ? "HIGH" : "LOW");
        it.state = state;
        it.at = mtb_Kit_Parse_Iso_Time(i["created_at"] | "");
    }
    return 200;
}

static int pd_Oncalls(const char* host, const char* auth, const char* policy, Onc_Round_t& r){
    char url[220];
    snprintf(url, sizeof(url), "https://%s/oncalls?earliest=true&limit=25%s%s", host, policy[0] ? "&escalation_policy_ids%5B%5D=" : "", policy);
    const char* headers[] = {"Authorization", auth, "Accept", "application/vnd.pagerduty+json;version=2", nullptr};
    JsonDocument doc, filter;
    JsonObject f = filter["oncalls"][0].to<JsonObject>();
    f["escalation_level"] = true; f["user"]["summary"] = true; f["escalation_policy"]["summary"] = true; f["end"] = true;
    const int code = get_Json(url, headers, doc, filter);
    if (code != 200) return code;
    r.people = 0;
    for (JsonVariantConst o : doc["oncalls"].as<JsonArrayConst>()){
        if ((o["escalation_level"] | 0) != 1 || r.people >= ONCALL) continue;
        char name[32];
        copy_Ascii(name, sizeof(name), o["user"]["summary"] | "");
        bool dup = !name[0];
        for (int k = 0; k < r.people; k++) if (!strcmp(r.person[k].name, name)) dup = true;
        if (dup) continue;
        Onc_Person_t& p = r.person[r.people++];
        snprintf(p.name, sizeof(p.name), "%s", name);
        copy_Ascii(p.what, sizeof(p.what), o["escalation_policy"]["summary"] | "");
        p.until = mtb_Kit_Parse_Iso_Time(o["end"] | "");          // none: on call until changed
    }
    r.peopleKnown = true;
    return 200;
}

// incident.io --------------------------------------------------------------------------------------------------------------------
static int io_Incidents(const char* auth, const char* category, uint8_t state, Onc_Round_t& r, uint32_t& total){
    char url[160];
    snprintf(url, sizeof(url), "https://api.incident.io/v2/incidents?status_category%%5Bone_of%%5D=%s&page_size=6", category);
    const char* headers[] = {"Authorization", auth, nullptr};
    JsonDocument doc, filter;
    filter["pagination_meta"]["total_record_count"] = true;
    JsonObject f = filter["incidents"][0].to<JsonObject>();
    f["reference"] = true; f["name"] = true; f["created_at"] = true;
    f["severity"]["name"] = true; f["incident_status"]["name"] = true;
    const int code = get_Json(url, headers, doc, filter);
    if (code != 200) return code;
    JsonArrayConst list = doc["incidents"].as<JsonArrayConst>();
    total = doc["pagination_meta"]["total_record_count"] | (uint32_t)list.size();
    for (JsonVariantConst i : list){
        if (r.count >= INCIDENTS) break;
        Onc_Incident_t& it = r.inc[r.count++];
        memset(&it, 0, sizeof(it));
        copy_Ascii(it.ref, sizeof(it.ref), i["reference"] | "");
        copy_Ascii(it.title, sizeof(it.title), i["name"] | "");
        copy_Ascii(it.level, sizeof(it.level), i["severity"]["name"] | "");
        copy_Ascii(it.service, sizeof(it.service), i["incident_status"]["name"] | "");
        // The most severe, by name (each organisation names and ranks its own): Critical, SEV1, SEV 1, P1.
        char lv[16];
        snprintf(lv, sizeof(lv), "%s", it.level);
        mtb_Kit_Upper(lv);
        it.high = strstr(lv, "CRITICAL") || !strncmp(lv, "SEV1", 4) || !strncmp(lv, "SEV 1", 5) || !strncmp(lv, "SEV-1", 5) || !strcmp(lv, "P1");
        it.state = state;
        it.at = mtb_Kit_Parse_Iso_Time(i["created_at"] | "");
    }
    return 200;
}

static int io_Schedule(const char* auth, const char* schedule, Onc_Round_t& r){
    char url[260], from[24], to[24];
    const time_t now = time(nullptr);
    iso_Time(from, sizeof(from), now);
    iso_Time(to, sizeof(to), now + 60);
    snprintf(url, sizeof(url), "https://api.incident.io/v2/schedule_entries?schedule_id=%s&entry_window_start=%s&entry_window_end=%s", schedule, from, to);
    const char* headers[] = {"Authorization", auth, nullptr};
    JsonDocument doc, filter;
    JsonObject f = filter["schedule_entries"]["final"][0].to<JsonObject>();
    f["user"]["name"] = true; f["start_at"] = true; f["end_at"] = true;
    const int code = get_Json(url, headers, doc, filter);
    if (code != 200) return code;
    r.people = 0;
    for (JsonVariantConst e : doc["schedule_entries"]["final"].as<JsonArrayConst>()){
        if (r.people >= ONCALL) break;
        const uint32_t start = mtb_Kit_Parse_Iso_Time(e["start_at"] | ""), end = mtb_Kit_Parse_Iso_Time(e["end_at"] | "");
        if (start && start > (uint32_t)now + 60) continue;           // a later shift
        Onc_Person_t& p = r.person[r.people++];
        copy_Ascii(p.name, sizeof(p.name), e["user"]["name"] | "");
        p.what[0] = '\0';
        p.until = end;
    }
    r.peopleKnown = true;
    return 200;
}

static bool same_Ref(const Onc_Incident_t& a, const Onc_Incident_t& b){ return !strcmp(a.ref, b.ref); }

static void worker(void*){
    uint32_t nextRound = 0, peopleAt = 0;
    static Onc_Round_t r;
    while (!sh.stop){
        if (!sh.checkNow && (int32_t)(millis() - nextRound) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        const bool asked = sh.checkNow;
        sh.checkNow = false;
        Onc_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRound = millis() + s.minutes * 60000u;
        if (!s.key[0]) continue;
        memset(&r, 0, sizeof(r));
        char why[48] = "", auth[KEY_LEN + 24];
        int code;
        const bool pd = s.service != SVC_INCIDENTIO;
        const char* name = pd ? "PAGERDUTY" : "INCIDENT.IO";
        if (pd){
            const char* host = s.service == SVC_PAGERDUTY_EU ? "api.eu.pagerduty.com" : "api.pagerduty.com";
            snprintf(auth, sizeof(auth), "Token token=%s", s.key);
            code = pd_Incidents(host, auth, "triggered", ST_NEW, r, r.newTotal);
            if (code == 200) code = pd_Incidents(host, auth, "acknowledged", ST_TAKEN, r, r.takenTotal);
            // Who is on call changes slowly: every 10 minutes, or when asked.
            if (code == 200 && (asked || !peopleAt || millis() - peopleAt >= 600000u)){
                if (pd_Oncalls(host, auth, s.who, r) == 200) peopleAt = millis();
            }
        } else {
            snprintf(auth, sizeof(auth), "Bearer %s", s.key);
            code = io_Incidents(auth, "triage", ST_NEW, r, r.newTotal);
            if (code == 200) code = io_Incidents(auth, "live", ST_TAKEN, r, r.takenTotal);
            if (code == 200 && s.who[0] && (asked || !peopleAt || millis() - peopleAt >= 600000u)){
                const int c2 = io_Schedule(auth, s.who, r);
                if (c2 == 200) peopleAt = millis();
                else error_Text(c2, "INCIDENT.IO", why, sizeof(why));
            }
        }
        memset(auth, 0, sizeof(auth));
        if (code != 200) error_Text(code, name, why, sizeof(why));
        portENTER_CRITICAL(&lock);
        if (s.service == save.service && !strcmp(s.key, save.key) && !strcmp(s.who, save.who)){   // not changed meanwhile
            snprintf(sh.why, sizeof(sh.why), "%s", why);
            if (code == 200){
                // A new incident: one not in the last round's lists (not on the first round). An incident moving from
                // untaken to taken is not new.
                uint8_t fresh = 0;
                if (sh.known)
                    for (int k = 0; k < r.count; k++){
                        bool seen = false;
                        for (int j = 0; j < sh.count; j++) if (same_Ref(r.inc[k], sh.inc[j])) seen = true;
                        if (!seen) fresh++;
                    }
                memcpy(sh.inc, r.inc, sizeof(r.inc));
                sh.count = r.count;
                sh.newTotal = r.newTotal;
                sh.takenTotal = r.takenTotal;
                if (r.peopleKnown){ memcpy(sh.person, r.person, sizeof(r.person)); sh.people = r.people; sh.peopleKnown = true; }
                sh.known = true;
                sh.fresh += fresh;
                sh.roundAt = (uint32_t)time(nullptr);
            }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 7, 12), C_RED = MTB_KIT_RGB(220, 30, 30), C_RED_DIM = MTB_KIT_RGB(70, 0, 0);
static const uint16_t C_AMBER = MTB_KIT_RGB(255, 160, 0), C_GREEN = MTB_KIT_RGB(30, 170, 80), C_FOOT = MTB_KIT_RGB(16, 20, 30);

static void ago_Text(char* t, size_t n, uint32_t at){
    const uint32_t now = (uint32_t)time(nullptr);
    if (!at || now < at){ t[0] = '\0'; return; }
    mtb_Kit_Ago(t, n, (int32_t)(now - at));
    mtb_Kit_Upper(t);
}

static void until_Text(char* t, size_t n, uint32_t until){
    if (!until){ t[0] = '\0'; return; }
    const time_t now = time(nullptr), u = (time_t)until;
    struct tm a, b;
    localtime_r(&now, &a);
    localtime_r(&u, &b);
    char clock[12];
    const bool h24 = mtb_Clock_24h();
    if (h24) snprintf(clock, sizeof(clock), "%02d:%02d", b.tm_hour, b.tm_min);
    else snprintf(clock, sizeof(clock), "%d:%02d %s", b.tm_hour % 12 ? b.tm_hour % 12 : 12, b.tm_min, b.tm_hour < 12 ? "AM" : "PM");
    static const char* const DAY[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) snprintf(t, n, "UNTIL %s", clock);
    else snprintf(t, n, "UNTIL %s %s", DAY[b.tm_wday % 7], clock);
}

static void draw(const Onc_Save_t& s, const Onc_Shared_t& d, int first, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[120], fit[120];
    const bool io = s.service == SVC_INCIDENTIO;
    // The band.
    uint16_t band = KIT_LINE, ink = KIT_LABEL;
    if (!s.key[0]) snprintf(t, sizeof(t), "ADD AN API KEY");
    else if (!d.known) snprintf(t, sizeof(t), "%s", d.why[0] ? "CAN'T CHECK" : "CHECKING...");
    else if (d.newTotal){
        const bool on = (ms / 450) % 2;
        band = on ? C_RED : C_RED_DIM; ink = on ? KIT_TEXT : C_RED;
        snprintf(t, sizeof(t), "%lu %s", (unsigned long)d.newTotal, io ? "IN TRIAGE" : "TRIGGERED");
    } else if (d.takenTotal){ band = C_AMBER; ink = BLACK; snprintf(t, sizeof(t), "%lu %s", (unsigned long)d.takenTotal, io ? "ACTIVE" : "ACKNOWLEDGED"); }
    else { band = C_GREEN; ink = BLACK; snprintf(t, sizeof(t), "ALL CLEAR"); }
    mtb_Panel_Fill_Rect(0, 0, W - 1, 25, band);
    mtb_Kit_Text_Centre(W / 2, 2, PT_Sans_Narrow_Bold18x22, ink, t, band);
    if (d.known && d.newTotal && d.takenTotal){
        snprintf(t, sizeof(t), "+%lu %s", (unsigned long)d.takenTotal, io ? "ACTIVE" : "ACKED");
        mtb_Kit_Text_Right(W - 3, 17, Terminal4x6, ink, t, band);
    }
    if (!s.key[0]){
        mtb_Kit_Text_Centre(W / 2, 54, Terminal6x8, KIT_LABEL, io ? "INCIDENT.IO" : "PAGERDUTY", C_BG);
        mtb_Kit_Text_Centre(W / 2, 68, Terminal4x6, KIT_LABEL, "ADD THE KEY IN THE PHONE APP, UNDER ON-CALL BOARD", C_BG);
        return;
    }
    if (d.why[0] && !d.known){
        char lines[3][64];
        const int n = mtb_Kit_Wrap(d.why, Terminal6x8, W - 16, &lines[0][0], sizeof(lines[0]), 3);
        for (int k = 0; k < n; k++) mtb_Kit_Text_Centre(W / 2, 46 + k * 11, Terminal6x8, KIT_ORANGE, lines[k], C_BG);
    }
    // The incidents.
    const int footY = 104;
    if (d.known && !d.count) mtb_Kit_Text_Centre(W / 2, 54, Terminal8x12, C_GREEN, "NO OPEN INCIDENTS", C_BG);
    for (int k = 0; k < 4 && first + k < d.count; k++){
        const Onc_Incident_t& it = d.inc[first + k];
        const int y = 29 + k * 18;
        const uint16_t sc = it.state == ST_NEW ? C_RED : C_AMBER;
        mtb_Panel_Fill_Rect(0, y, 2, y + 15, sc);
        const int16_t rx = mtb_Kit_Text(6, y + 1, Terminal4x6, sc, it.ref, C_BG);
        snprintf(t, sizeof(t), "%s", it.title);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - rx - 8);
        mtb_Kit_Text(rx + 4, y, Terminal6x8, KIT_TEXT, fit, C_BG);
        char ago[20], lv[16], svc[32], who[32];
        ago_Text(ago, sizeof(ago), it.at);
        snprintf(lv, sizeof(lv), "%s", it.level); mtb_Kit_Upper(lv);
        snprintf(svc, sizeof(svc), "%s", it.service); mtb_Kit_Upper(svc);
        snprintf(who, sizeof(who), "%s", it.who); mtb_Kit_Upper(who);
        int16_t x = 6;
        if (lv[0]) x = mtb_Kit_Text(x, y + 9, Terminal4x6, it.high ? KIT_RED : KIT_LABEL, lv, C_BG) + 6;
        snprintf(t, sizeof(t), "%s%s%s", svc, who[0] ? "  " : "", who);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - x - 50);
        mtb_Kit_Text(x, y + 9, Terminal4x6, KIT_LABEL, fit, C_BG);
        mtb_Kit_Text_Right(W - 3, y + 9, Terminal4x6, KIT_DIM, ago, C_BG);
    }
    if (d.count > 4){
        snprintf(t, sizeof(t), "%d-%d OF %d", first + 1, first + 4 < d.count ? first + 4 : d.count, d.count);
        mtb_Kit_Text_Right(W - 3, footY - 7, Terminal4x6, KIT_DIM, t, C_BG);
    }
    // Who is on call.
    mtb_Panel_Fill_Rect(0, footY, W - 1, H - 1, C_FOOT);
    mtb_Kit_Text(4, footY + 2, Terminal4x6, KIT_CYAN, "ON CALL", C_FOOT);
    if (d.known && d.why[0]){ mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, d.why, W - 50); mtb_Kit_Text_Right(W - 4, footY + 2, Terminal4x6, KIT_ORANGE, fit, C_FOOT); }
    if (!d.peopleKnown){
        mtb_Kit_Text(4, footY + 11, Terminal6x8, KIT_DIM, io && !s.who[0] ? "ADD A SCHEDULE'S ID IN THE PHONE APP" : "-", C_FOOT);
        return;
    }
    if (!d.people){ mtb_Kit_Text(4, footY + 11, Terminal6x8, KIT_DIM, "NO ONE", C_FOOT); return; }
    char line[120] = "";
    for (int k = 0; k < d.people && k < 2; k++){
        char name[32];
        snprintf(name, sizeof(name), "%s", d.person[k].name); mtb_Kit_Upper(name);
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", k ? ", " : "", name);
    }
    char until[24];
    until_Text(until, sizeof(until), d.person[0].until);
    const int16_t uw = mtb_Kit_Text_Width(Terminal4x6, until);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, line, W - uw - 14);
    mtb_Kit_Text(4, footY + 10, Terminal8x12, KIT_TEXT, fit, C_FOOT);
    mtb_Kit_Text_Right(W - 4, footY + 14, Terminal4x6, KIT_LABEL, until, C_FOOT);
    if (d.people == 1 && d.person[0].what[0]){
        char what[32]; snprintf(what, sizeof(what), "%s", d.person[0].what); mtb_Kit_Upper(what);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, what, 110);
        mtb_Kit_Text(40, footY + 2, Terminal4x6, KIT_LABEL, fit, C_FOOT);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void alarm(void){
    static const Snd_Note_t N[] = {{880, 160}, {0, 60}, {660, 160}, {0, 60}, {880, 160}, {0, 60}, {660, 300}};
    if (!save.sound || mtb_Silent_Mode()) return;
    snd_Play(N, 7, 0.4f, true);
}

static void onCall_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Service, cmd_Key, cmd_Who, cmd_Minutes, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "oncNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Onc_Shared_t* d = (Onc_Shared_t*)heap_caps_calloc(1, sizeof(Onc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, lastAlarm = 0;
    int first = 0;
    bool quiet = false, redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; first += t; redraw = true; }
        uint8_t fresh;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        fresh = sh.fresh; sh.fresh = 0;
        portEXIT_CRITICAL(&lock);
        const int most = d->count > 4 ? d->count - 4 : 0;
        if (first < 0) first = 0;
        if (first > most) first = most;
        if (clickRequested){
            clickRequested = false;
            if (d->newTotal && !quiet){ quiet = true; snd_Stop(); }         // quiet until the next one
            else sh.checkNow = true;
        }
        if (fresh){ quiet = false; first = 0; alarm(); lastAlarm = ms; }
        else if (save.nag && !quiet && d->newTotal && ms - lastAlarm >= 60000u){ alarm(); lastAlarm = ms; }
        if (!d->newTotal) quiet = false;
        const bool flashing = d->known && d->newTotal;
        if (redraw || ms - lastDraw >= (flashing ? 150u : 30000u)){
            redraw = false;
            lastDraw = ms;
            Onc_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, first, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void onc_Values(JsonObject values){
    Onc_Save_t* s = (Onc_Save_t*)heap_caps_malloc(sizeof(Onc_Save_t), MALLOC_CAP_SPIRAM);
    Onc_Shared_t* d = (Onc_Shared_t*)heap_caps_malloc(sizeof(Onc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    const bool io = s->service == SVC_INCIDENTIO;
    JsonArray rows = values["now"].to<JsonArray>();
    char t[140];
    if (!s->key[0]){ JsonObject o = rows.add<JsonObject>(); o["label"] = io ? "incident.io" : "PagerDuty"; o["value"] = "No API key yet"; o["state"] = "warn"; }
    else if (!appOpen){ JsonObject o = rows.add<JsonObject>(); o["label"] = io ? "incident.io" : "PagerDuty"; o["value"] = "Checked while On-Call Board is open"; o["state"] = "off"; }
    else if (!d->known){ JsonObject o = rows.add<JsonObject>(); o["label"] = io ? "incident.io" : "PagerDuty"; o["value"] = d->why[0] ? d->why : "Checking"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        JsonObject o = rows.add<JsonObject>();
        o["label"] = io ? "In triage" : "Triggered";
        snprintf(t, sizeof(t), "%lu", (unsigned long)d->newTotal); o["value"] = t; o["state"] = d->newTotal ? "error" : "ok";
        o = rows.add<JsonObject>();
        o["label"] = io ? "Active" : "Acknowledged";
        snprintf(t, sizeof(t), "%lu", (unsigned long)d->takenTotal); o["value"] = t; o["state"] = d->takenTotal ? "warn" : "ok";
        for (int k = 0; k < d->count && k < 3; k++){
            o = rows.add<JsonObject>();
            o["label"] = d->inc[k].ref;
            snprintf(t, sizeof(t), "%s%s%s", d->inc[k].title, d->inc[k].level[0] ? ", " : "", d->inc[k].level);
            o["value"] = t;
            o["state"] = d->inc[k].state == ST_NEW ? "error" : "warn";
        }
        if (d->peopleKnown){
            o = rows.add<JsonObject>();
            o["label"] = "On call";
            t[0] = '\0';
            for (int k = 0; k < d->people; k++) snprintf(t + strlen(t), sizeof(t) - strlen(t), "%s%s", k ? ", " : "", d->person[k].name);
            o["value"] = d->people ? t : "No one";
            o["state"] = "info";
        }
        if (d->why[0]){ o = rows.add<JsonObject>(); o["label"] = "Last check"; o["value"] = d->why; o["state"] = "warn"; }
    }
    values["service"] = s->service;
    values["apiKey_set"] = s->key[0] ? 1 : 0;
    values["who"] = s->who;
    values["minutes"] = s->minutes;
    values["sound"] = s->sound;
    values["nag"] = s->nag;
    heap_caps_free(s);
    heap_caps_free(d);
}
