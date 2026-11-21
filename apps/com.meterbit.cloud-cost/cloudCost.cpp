/**
 * Cloud Cost Meter: this month's cloud bill so far, for up to four accounts: AWS (Cost Explorer), DigitalOcean,
 * Akamai (Linode), or a JSON address of the owner's own. At the top, the month so far for all of them in large figures,
 * where the month is heading at this pace (the cost so far over the part of the month gone, UTC, as the clouds bill)
 * and the budgets; below, each account against its budget: green within it, amber heading over it, red over it.
 * An account that starts heading over its budget, or goes over it, gets a chime and a banner, once a month each.
 *
 * DigitalOcean: GET /v2/customers/my/balance (month_to_date_usage). Akamai: GET /v4/account (balance_uninvoiced).
 * Both every 30 minutes. AWS: POST ce.us-east-1.amazonaws.com GetCostAndUsage (UnblendedCost, this month), signed with
 * SigV4 (cc_Sign.cpp), only every few hours (AWS charges $0.01 a request). Own: GET the address, the value at a path.
 * Keys come from the phone and stay on Pixlpal (NVS "ccSave"). Knob: click, check now.
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
#include "cc_Mobile_Ui.h"
#include "cc_Sign.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#define W           256
#define H           128
#define ACCOUNTS    4
#define LABEL_LEN   16
#define SECRET_LEN  136
#define PATH_LEN    40
#define NVS_KEY     "ccSave"
#define SAVE_MAGIC  0x43434D01u
#define UA          "Pixlpal-CloudCost/1.0"
#define EVERY_MS    (30u * 60u * 1000u)      // DigitalOcean, Akamai, own
#define AWS_HOST    "ce.us-east-1.amazonaws.com"

EXT_RAM_BSS_ATTR TaskHandle_t cloudCost_Task_H = NULL;
static void cloudCost_App_Task(void* dApplication);
static void cc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* cloudCost_App;
Mtb_Applications* cloudCost_App_GetInstance(){
    if (!cloudCost_App){
        cloudCost_App = new Mtb_Applications_FullScreen(cloudCost_App_Task, &cloudCost_Task_H, "Cloud Cost Meter", {0,0}, 8192);
        cloudCost_App->mtb_App_Set_Mobile_UI(CC_UI_MANIFEST, CC_UI_API, nullptr, cc_Values);
    }
    return cloudCost_App;
}
MTB_REGISTER_APP(cloudCost_App, 0, 0)       // the catalogue gives the app its number

enum : uint8_t { P_NONE = 0, P_DO, P_LINODE, P_AWS, P_OWN, P_COUNT };
static const char* const P_NAME[P_COUNT] = {"", "DIGITALOCEAN", "AKAMAI", "AWS", "OWN"};

struct Cc_Account_t {
    uint8_t provider;
    char label[LABEL_LEN];
    char secret[SECRET_LEN];        // a token; AWS "key ID:secret"; own: its address
    char path[PATH_LEN];            // own: where the cost is
    uint32_t budget;                // whole dollars a month; 0 none
};

struct Cc_Save_t {
    uint32_t magic;
    Cc_Account_t a[ACCOUNTS];
    uint8_t awsHours, sound;
    uint16_t alertMonth;            // the month (year * 12 + month) the alerts below are for
    uint8_t alerted;                // bit 2i: account i heading over its budget, bit 2i+1: over it
    float cost[ACCOUNTS];           // the last reading of each, kept so a reopened app shows it and AWS isn't asked again
    uint32_t costAt[ACCOUNTS];      // when (Unix time); 0 none
};
static_assert(sizeof(Cc_Save_t) <= 1024, "an installed app saves at most 1 KB");

struct Cc_Result_t {
    bool known;
    float cost;                     // this month so far, USD
    char why[40];
    uint32_t at;                    // Unix time it was read
};

struct Cc_Shared_t {
    Cc_Result_t r[ACCOUNTS];
    uint32_t version;
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Cc_Save_t save;
static EXT_RAM_BSS_ATTR Cc_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Cc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.awsHours = 8; s.sound = 1; }
    if (s.awsHours < 1 || s.awsHours > 24) s.awsHours = 8;
    for (int i = 0; i < ACCOUNTS; i++){
        Cc_Account_t& a = s.a[i];
        if (a.provider >= P_COUNT) a.provider = P_NONE;
        a.label[LABEL_LEN - 1] = a.secret[SECRET_LEN - 1] = a.path[PATH_LEN - 1] = '\0';
    }
}

/** The account a command is for: its key is name1 ... name4. */
static int account_Of(JsonDocument& c, const char* name, const char** value){
    for (int i = 0; i < ACCOUNTS; i++){
        char key[16];
        snprintf(key, sizeof(key), "%s%d", name, i + 1);
        if (c[key].isNull()) continue;
        *value = c[key].is<const char*>() ? c[key].as<const char*>() : nullptr;
        return i;
    }
    return -1;
}

static bool printable(const char* v){
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~') return false;
    return true;
}

static bool secret_Ok(uint8_t provider, const char* v){
    if (!*v) return true;                                   // forget it
    if (!printable(v)) return false;
    if (provider == P_DO || provider == P_LINODE) return strlen(v) >= 16;
    if (provider == P_AWS){
        const char* colon = strchr(v, ':');
        if (!colon || colon - v < 16 || colon - v > 32 || strlen(colon + 1) < 30 || strlen(colon + 1) > 64) return false;
        for (const char* k = v; k < colon; k++) if (!isalnum((unsigned char)*k)) return false;
        return true;
    }
    if (provider == P_OWN) return !strncmp(v, "https://", 8) && strlen(v) > 12;
    return false;
}

static bool path_Ok(const char* v){
    for (const char* k = v; *k; k++) if (!(isalnum((unsigned char)*k) || strchr("_-.", *k))) return false;
    return true;
}

static void forget_Result(int i){ portENTER_CRITICAL(&lock); memset(&sh.r[i], 0, sizeof(sh.r[i])); save.costAt[i] = 0; sh.version++; portEXIT_CRITICAL(&lock); }

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Provider(JsonDocument& c){
    for (int i = 0; i < ACCOUNTS; i++){
        char key[8];
        snprintf(key, sizeof(key), "prov%d", i + 1);
        if (c[key].isNull()) continue;
        long v;
        if (!mtb_Kit_Json_Range(c[key], 0, P_COUNT - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
        if (save.a[i].provider != (uint8_t)v){
            portENTER_CRITICAL(&lock);
            save.a[i].provider = (uint8_t)v;
            memset(save.a[i].secret, 0, SECRET_LEN);        // a key means nothing to another cloud
            save.a[i].path[0] = '\0';
            portEXIT_CRITICAL(&lock);
            forget_Result(i);
        }
        sh.checkNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Label(JsonDocument& c){
    const char* v = nullptr;
    const int i = account_Of(c, "label", &v);
    if (i < 0 || !v || strlen(v) >= LABEL_LEN * 2){ mtb_Ble_App_Cmd_Reject(); return; }
    char clean[LABEL_LEN];
    mtb_Kit_To_Ascii(clean, sizeof(clean), v);
    portENTER_CRITICAL(&lock); snprintf(save.a[i].label, LABEL_LEN, "%s", clean); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Secret(JsonDocument& c){
    const char* v = nullptr;
    const int i = account_Of(c, "secret", &v);
    if (i < 0 || !v || strlen(v) >= SECRET_LEN || save.a[i].provider == P_NONE || !secret_Ok(save.a[i].provider, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.a[i].secret, SECRET_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    forget_Result(i);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Path(JsonDocument& c){
    const char* v = nullptr;
    const int i = account_Of(c, "path", &v);
    if (i < 0 || !v || strlen(v) >= PATH_LEN || !path_Ok(v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.a[i].path, PATH_LEN, "%s", v); portEXIT_CRITICAL(&lock);
    forget_Result(i);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Budget(JsonDocument& c){
    for (int i = 0; i < ACCOUNTS; i++){
        char key[10];
        snprintf(key, sizeof(key), "budget%d", i + 1);
        if (c[key].isNull()) continue;
        long v;
        if (!mtb_Kit_Json_Range(c[key], 0, 10000000, v)){ mtb_Ble_App_Cmd_Reject(); return; }
        save.a[i].budget = (uint32_t)v;
        save.alerted &= (uint8_t)~(3u << (2 * i));           // a new budget: its alerts again
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Aws_Hours(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["awsHours"], 1, 24, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.awsHours = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static void error_Text(int code, const char* name, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "ALLOW THE ADDRESS ON THE PHONE");
    else if (code == -2) snprintf(why, n, "AN ANSWER PIXLPAL CAN'T READ");
    else if (code == -4) snprintf(why, n, "NO COST WHERE THE PATH SAYS");
    else if (code <= 0) snprintf(why, n, "NO ANSWER FROM %s", name);
    else if (code == 401) snprintf(why, n, "%s REFUSED THE KEY", name);
    else if (code == 403) snprintf(why, n, "THE KEY MAY NOT READ BILLING");
    else if (code == 429) snprintf(why, n, "%s SAYS WAIT", name);
    else snprintf(why, n, "%s: HTTP %d", name, code);
}

/** A cost: a number, or text such as "12.34" or "$1,234.56". */
static bool cost_Of(JsonVariantConst v, float& out){
    if (v.is<float>() || v.is<long>()){ out = v.as<float>(); return true; }
    const char* t = v.as<const char*>();
    if (!t) return false;
    char clean[32];
    size_t n = 0;
    for (const char* k = t; *k && n + 1 < sizeof(clean); k++) if (isdigit((unsigned char)*k) || *k == '.' || *k == '-') clean[n++] = *k;
    clean[n] = '\0';
    if (!n) return false;
    char* end;
    out = strtof(clean, &end);
    return end != clean;
}

static int get_Raw(const char* url, const char* const* headers, JsonDocument& doc, JsonDocument* filter){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 30000, r, UA);
    if (r.code >= 200 && r.code < 300 && body){
        const DeserializationError e = filter ? deserializeJson(doc, body, r.length, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body, r.length);
        if (r.truncated || e) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static int check_Do(const Cc_Account_t& a, float& cost){
    char auth[SECRET_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", a.secret);
    const char* headers[] = {"Authorization", auth, nullptr};
    JsonDocument doc, filter;
    filter["month_to_date_usage"] = true;
    const int code = get_Raw("https://api.digitalocean.com/v2/customers/my/balance", headers, doc, &filter);
    memset(auth, 0, sizeof(auth));
    if (code == 200 && !cost_Of(doc["month_to_date_usage"], cost)) return -2;
    return code;
}

static int check_Linode(const Cc_Account_t& a, float& cost){
    char auth[SECRET_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", a.secret);
    const char* headers[] = {"Authorization", auth, nullptr};
    JsonDocument doc, filter;
    filter["balance_uninvoiced"] = true;
    filter["uninvoiced_balance"] = true;
    const int code = get_Raw("https://api.linode.com/v4/account", headers, doc, &filter);
    memset(auth, 0, sizeof(auth));
    if (code == 200 && !cost_Of(doc["balance_uninvoiced"], cost) && !cost_Of(doc["uninvoiced_balance"], cost)) return -2;
    return code;
}

static int check_Own(const Cc_Account_t& a, float& cost){
    JsonDocument doc;
    const int code = get_Raw(a.secret, nullptr, doc, nullptr);
    if (code != 200) return code;
    JsonVariantConst v = doc.as<JsonVariantConst>();
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", a.path);
    for (char* part = path; part && *part;){
        char* dot = strchr(part, '.');
        if (dot) *dot = '\0';
        if (v.is<JsonArrayConst>() && isdigit((unsigned char)part[0])) v = v[atoi(part)];
        else v = v[(const char*)part];
        part = dot ? dot + 1 : nullptr;
    }
    return cost_Of(v, cost) ? 200 : -4;
}

static int check_Aws(const Cc_Account_t& a, float& cost, char* why, size_t whyN){
    const time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    if (u.tm_mday == 1){ cost = 0; return 200; }            // a new month: nothing yet (its end must be after its start)
    char body[200];
    const int n = snprintf(body, sizeof(body),
        "{\"TimePeriod\":{\"Start\":\"%04d-%02d-01\",\"End\":\"%04d-%02d-%02d\"},\"Granularity\":\"MONTHLY\",\"Metrics\":[\"UnblendedCost\"]}",
        u.tm_year + 1900, u.tm_mon + 1, u.tm_year + 1900, u.tm_mon + 1, u.tm_mday);
    char amzDate[20];
    snprintf(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec);
    char id[40], secret[72];
    const char* colon = strchr(a.secret, ':');
    if (!colon) return 401;
    snprintf(id, sizeof(id), "%.*s", (int)(colon - a.secret), a.secret);
    snprintf(secret, sizeof(secret), "%s", colon + 1);
    static const char CT[] = "application/x-amz-json-1.1", TARGET[] = "AWSInsightsIndexService.GetCostAndUsage";
    char auth[300];
    const bool signedOk = cc_Aws_Authorization(id, secret, "us-east-1", "ce", AWS_HOST, CT, TARGET, amzDate, body, (size_t)n, auth, sizeof(auth));
    memset(secret, 0, sizeof(secret));
    if (!signedOk) return -1;
    const char* headers[] = {"Content-Type", CT, "X-Amz-Target", TARGET, "X-Amz-Date", amzDate, "Authorization", auth, nullptr};
    Mtb_Kit_Http_t r = {};
    char* answer = mtb_Kit_Http_Send("POST", "https://" AWS_HOST "/", headers, body, (size_t)n, nullptr, 0, 16000, r, UA);
    memset(auth, 0, sizeof(auth));
    int code = r.code;
    if (answer && code > 0){
        JsonDocument doc;
        if (deserializeJson(doc, answer, r.length)) { if (code == 200) code = -2; }
        else if (code == 200){
            if (!cost_Of(doc["ResultsByTime"][0]["Total"]["UnblendedCost"]["Amount"], cost)) code = -2;
        } else {
            // AWS says why: {"__type": "...#AccessDeniedException", "Message": "..."}.
            const char* type = doc["__type"] | "";
            const char* hash = strrchr(type, '#');
            char t[48];
            snprintf(t, sizeof(t), "AWS: %s", hash ? hash + 1 : type);
            if (strstr(t, "AccessDenied")) snprintf(why, whyN, "AWS: THE KEY MAY NOT READ COSTS");
            else if (strstr(t, "UnrecognizedClient") || strstr(t, "InvalidSignature") || strstr(t, "SignatureDoesNotMatch")) snprintf(why, whyN, "AWS REFUSED THE KEY");
            else if (strstr(t, "DataUnavailable")) snprintf(why, whyN, "AWS: TURN ON COST EXPLORER");
            else if (type[0]){ mtb_Kit_Upper(t); snprintf(why, whyN, "%.39s", t); }
        }
    }
    heap_caps_free(answer);
    return code;
}

static void worker(void*){
    // When each account is due (Unix time): a reading kept from before counts, so AWS isn't asked on every opening.
    uint32_t due[ACCOUNTS];
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < ACCOUNTS; i++){
        const uint32_t every = save.a[i].provider == P_AWS ? save.awsHours * 3600u : EVERY_MS / 1000u;
        due[i] = save.costAt[i] ? save.costAt[i] + every : 0;
    }
    portEXIT_CRITICAL(&lock);
    while (!sh.stop){
        if (sh.checkNow){ sh.checkNow = false; for (int i = 0; i < ACCOUNTS; i++) due[i] = 0; }
        Cc_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        const uint32_t now = (uint32_t)time(nullptr);
        bool any = false;
        for (int i = 0; i < ACCOUNTS && !sh.stop; i++){
            const Cc_Account_t& a = s.a[i];
            if (a.provider == P_NONE || !a.secret[0] || (a.provider == P_OWN && !a.path[0])) continue;
            if (s.costAt[i] == 0 && due[i] > now + 60) due[i] = 0;           // a new key or address: read it now
            if (due[i] > now) continue;
            const uint32_t every = a.provider == P_AWS ? s.awsHours * 3600u : EVERY_MS / 1000u;
            any = true;
            float cost = 0;
            char why[40] = "";
            int code;
            switch (a.provider){
                case P_DO: code = check_Do(a, cost); break;
                case P_LINODE: code = check_Linode(a, cost); break;
                case P_AWS: code = check_Aws(a, cost, why, sizeof(why)); break;
                default: code = check_Own(a, cost); break;
            }
            if (code != 200 && !why[0]) error_Text(code, P_NAME[a.provider], why, sizeof(why));
            const uint32_t at = (uint32_t)time(nullptr);
            due[i] = at + (code == 200 ? every : a.provider == P_AWS ? 600u : 300u);      // after a problem: again in 10 or 5 minutes
            portENTER_CRITICAL(&lock);
            if (a.provider == save.a[i].provider && !strcmp(a.secret, save.a[i].secret) && !strcmp(a.path, save.a[i].path)){   // not changed meanwhile
                Cc_Result_t& r = sh.r[i];
                snprintf(r.why, sizeof(r.why), "%s", why);
                if (code == 200){ r.known = true; r.cost = cost; r.at = at; save.cost[i] = cost; save.costAt[i] = at; }
                sh.version++;
            }
            portEXIT_CRITICAL(&lock);
        }
        if (!any) vTaskDelay(pdMS_TO_TICKS(500));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// The month --------------------------------------------------------------------------------------------------------------------------
struct Cc_Month_t { int year, month, day, days; float gone; };      // gone: the part of the month past (0-1), UTC

static Cc_Month_t month_Now(void){
    const time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    Cc_Month_t m;
    m.year = u.tm_year + 1900; m.month = u.tm_mon + 1; m.day = u.tm_mday;
    m.days = mtb_Kit_Days_In_Month(m.year, m.month);
    m.gone = ((float)(u.tm_mday - 1) * 86400.0f + u.tm_hour * 3600.0f + u.tm_min * 60.0f) / ((float)m.days * 86400.0f);
    return m;
}

/** Where the month is heading: the cost so far over the part gone. Not shown in the month's first day. */
static bool forecast(float cost, const Cc_Month_t& m, float& out){
    if (m.gone < 1.0f / m.days) return false;
    out = cost / m.gone;
    return true;
}

static void money(char* t, size_t n, float v, bool cents){
    if (v < 0) v = 0;
    const unsigned long whole = (unsigned long)v;
    const unsigned c = (unsigned)((v - (float)whole) * 100.0f + 0.5f) % 100;
    char digits[24], grouped[32];
    snprintf(digits, sizeof(digits), "%lu", whole);
    const size_t len = strlen(digits);
    size_t g = 0;
    for (size_t k = 0; k < len; k++){ grouped[g++] = digits[k]; if ((len - k - 1) % 3 == 0 && k + 1 < len) grouped[g++] = ','; }
    grouped[g] = '\0';
    if (cents) snprintf(t, n, "$%s.%02u", grouped, c); else snprintf(t, n, "$%s", grouped);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 12), C_HEAD = MTB_KIT_RGB(16, 20, 30);
static const uint16_t C_OK = MTB_KIT_RGB(40, 200, 100), C_WARN = MTB_KIT_RGB(255, 170, 0), C_OVER = MTB_KIT_RGB(235, 45, 45);
static const char* const MONTH[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

static uint16_t budget_Color(float cost, float fc, bool hasFc, uint32_t budget){
    if (!budget) return KIT_CYAN;
    if (cost > (float)budget) return C_OVER;
    if (hasFc && fc > (float)budget) return C_WARN;
    return C_OK;
}

static void draw(const Cc_Save_t& s, const Cc_Shared_t& d, const char* banner, bool bannerOver, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const Cc_Month_t m = month_Now();
    char t[96], fit[96];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_HEAD);
    snprintf(t, sizeof(t), "CLOUD COST  %s", MONTH[(m.month - 1) % 12]);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_LABEL, t, C_HEAD);
    snprintf(t, sizeof(t), "DAY %d OF %d", m.day, m.days);
    mtb_Kit_Text_Right(W - 3, 2, Terminal4x6, KIT_DIM, t, C_HEAD);
    int used[ACCOUNTS], n = 0;
    for (int i = 0; i < ACCOUNTS; i++) if (s.a[i].provider != P_NONE) used[n++] = i;
    if (!n){
        mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, "NO ACCOUNTS YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 68, Terminal4x6, KIT_LABEL, "ADD THEM IN THE PHONE APP", C_BG);
        return;
    }
    // The total: the accounts read so far, their budgets.
    float total = 0, budget = 0;
    int known = 0;
    for (int k = 0; k < n; k++){ const Cc_Result_t& r = d.r[used[k]]; if (r.known){ total += r.cost; known++; budget += (float)s.a[used[k]].budget; } }
    float fc = 0;
    const bool hasFc = known && forecast(total, m, fc);
    if (known){
        money(t, sizeof(t), total, total < 10000);
        const uint16_t c = budget > 0 ? budget_Color(total, fc, hasFc, (uint32_t)budget) : KIT_TEXT;
        mtb_Kit_Text(4, 12, PT_Sans_Narrow_Bold28x34, c, t, C_BG);
        if (hasFc){ char f[24]; money(f, sizeof(f), fc, false); snprintf(t, sizeof(t), "HEADING FOR %s", f); mtb_Kit_Text_Right(W - 4, 16, Terminal6x8, KIT_LABEL, t, C_BG); }
        if (budget > 0){
            char b[24]; money(b, sizeof(b), budget, false);
            snprintf(t, sizeof(t), "BUDGET %s", b);
            mtb_Kit_Text_Right(W - 4, 28, Terminal6x8, KIT_DIM, t, C_BG);
            // The bar: so far, and where the month is heading (a tick), against the budgets.
            const float span = (hasFc && fc > budget ? fc : budget) * 1.05f;
            mtb_Kit_Bar(4, 48, W - 8, 4, total / span, c);
            const int bx = 4 + (int)((W - 8) * budget / span);
            mtb_Panel_Fill_Rect(bx, 46, bx, 53, KIT_TEXT);
            if (hasFc){ const int fx = 4 + (int)((W - 8) * (fc < span ? fc : span) / span); mtb_Panel_Fill_Rect(fx, 47, fx, 52, KIT_LABEL); }
        }
        if (known < n){ snprintf(t, sizeof(t), "%d OF %d ACCOUNTS", known, n); mtb_Kit_Text_Right(W - 4, 38, Terminal4x6, KIT_ORANGE, t, C_BG); }
    } else {
        bool problem = false;
        for (int k = 0; k < n; k++) if (d.r[used[k]].why[0]) problem = true;
        mtb_Kit_Text(4, 20, Terminal8x12, KIT_LABEL, problem ? "CAN'T READ THE COSTS YET" : "READING THE COSTS...", C_BG);
    }
    // The accounts.
    const int rowH = 17, top = 58;
    for (int k = 0; k < n; k++){
        const int i = used[k], y = top + k * rowH;
        const Cc_Account_t& a = s.a[i];
        const Cc_Result_t& r = d.r[i];
        char name[LABEL_LEN + 16];
        snprintf(name, sizeof(name), "%s", a.label[0] ? a.label : P_NAME[a.provider]);
        mtb_Kit_Upper(name);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, name, 92);
        mtb_Kit_Text(4, y, Terminal6x8, KIT_TEXT, fit, C_BG);
        if (!a.secret[0]){ mtb_Kit_Text(100, y + 1, Terminal4x6, KIT_DIM, a.provider == P_OWN ? "ADD ITS ADDRESS" : "ADD ITS KEY", C_BG); continue; }
        if (!r.known){
            mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, r.why[0] ? r.why : "READING...", W - 104);
            mtb_Kit_Text(100, y + 1, Terminal4x6, r.why[0] ? KIT_ORANGE : KIT_DIM, fit, C_BG);
            continue;
        }
        float f = 0;
        const bool hf = forecast(r.cost, m, f);
        const uint16_t c = budget_Color(r.cost, f, hf, a.budget);
        money(t, sizeof(t), r.cost, true);
        mtb_Kit_Text_Right(W - 4, y, Terminal6x8, c, t, C_BG);
        // Below: a bar against the budget, and where it's heading.
        char sub[64] = "";
        if (hf){ char fs[24]; money(fs, sizeof(fs), f, false); snprintf(sub, sizeof(sub), "HEADING FOR %s", fs); }
        if (a.budget){ char bs[24]; money(bs, sizeof(bs), (float)a.budget, false); snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), "%sOF %s", sub[0] ? "  " : "", bs); }
        if (r.why[0]) snprintf(sub, sizeof(sub), "%s", r.why);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, sub, 150);
        mtb_Kit_Text(4, y + 9, Terminal4x6, r.why[0] ? KIT_ORANGE : KIT_DIM, fit, C_BG);
        if (a.budget) mtb_Kit_Bar(160, y + 10, W - 164, 3, r.cost / (float)a.budget, c);
    }
    if (banner[0]){
        const bool on = !bannerOver || (ms / 400) % 2;
        const uint16_t bg = bannerOver ? (on ? C_OVER : MTB_KIT_RGB(60, 0, 0)) : C_WARN;
        mtb_Panel_Fill_Rect(8, 44, W - 9, 82, bg);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, banner, W - 28);
        mtb_Kit_Text_Centre(W / 2, 57, Terminal8x12, on ? BLACK : C_OVER, fit, bg);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chime(bool over){
    static const Snd_Note_t HEADING[] = {{660, 120}, {0, 60}, {660, 120}, {0, 60}, {523, 260}};
    static const Snd_Note_t OVER[] = {{523, 160}, {0, 50}, {440, 160}, {0, 50}, {349, 360}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (over) snd_Play(OVER, 5, 0.35f, true); else snd_Play(HEADING, 5, 0.3f, false);
}

static void cloudCost_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Provider, cmd_Label, cmd_Secret, cmd_Path, cmd_Budget, cmd_Aws_Hours, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    {
        const time_t now = time(nullptr);
        struct tm u, k;
        gmtime_r(&now, &u);
        for (int i = 0; i < ACCOUNTS; i++){
            if (!save.costAt[i] || save.a[i].provider == P_NONE) continue;
            const time_t at = (time_t)save.costAt[i];
            gmtime_r(&at, &k);
            if (k.tm_year != u.tm_year || k.tm_mon != u.tm_mon){ save.costAt[i] = 0; continue; }    // last month's
            sh.r[i].known = true; sh.r[i].cost = save.cost[i]; sh.r[i].at = save.costAt[i];
        }
    }
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "ccNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Cc_Shared_t* d = (Cc_Shared_t*)heap_caps_calloc(1, sizeof(Cc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, bannerUntil = 0;
    char banner[48] = "";
    bool bannerOver = false, redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; if (banner[0]) bannerUntil = ms; else sh.checkNow = true; }
        portENTER_CRITICAL(&lock);
        const bool fresh = sh.version != version;
        if (fresh){ *d = sh; version = sh.version; redraw = push = saveNow = true; }
        portEXIT_CRITICAL(&lock);
        // Budget alerts: once a month each, for an account heading over its budget and for one over it.
        if (fresh && time(nullptr) > 1700000000){
            const Cc_Month_t m = month_Now();
            const uint16_t month = (uint16_t)(m.year * 12 + m.month);
            if (save.alertMonth != month){ save.alertMonth = month; save.alerted = 0; saveNow = true; }
            for (int i = 0; i < ACCOUNTS; i++){
                const Cc_Account_t& a = save.a[i];
                const Cc_Result_t& r = d->r[i];
                if (a.provider == P_NONE || !a.budget || !r.known) continue;
                float f = 0;
                const bool over = r.cost > (float)a.budget, heading = forecast(r.cost, m, f) && f > (float)a.budget;
                const uint8_t bit = over ? (uint8_t)(2u << (2 * i)) : heading ? (uint8_t)(1u << (2 * i)) : 0;
                if (!bit || (save.alerted & bit) || (over && (save.alerted & (2u << (2 * i))))) continue;
                save.alerted |= bit;
                if (over) save.alerted |= (uint8_t)(1u << (2 * i));
                saveNow = true;
                char name[LABEL_LEN + 16];
                snprintf(name, sizeof(name), "%s", a.label[0] ? a.label : P_NAME[a.provider]);
                mtb_Kit_Upper(name);
                snprintf(banner, sizeof(banner), over ? "%.20s IS OVER BUDGET" : "%.18s HEADING OVER BUDGET", name);
                bannerOver = over;
                bannerUntil = ms + 12000;
                chime(over);
                break;
            }
        }
        if (banner[0] && (int32_t)(ms - bannerUntil) >= 0){ banner[0] = '\0'; redraw = true; }
        if (redraw || banner[0] || ms - lastDraw >= 60000u){
            redraw = false;
            lastDraw = ms;
            Cc_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, banner, bannerOver, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(banner[0] ? 60 : 100);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void cc_Values(JsonObject values){
    Cc_Save_t* s = (Cc_Save_t*)heap_caps_malloc(sizeof(Cc_Save_t), MALLOC_CAP_SPIRAM);
    Cc_Shared_t* d = (Cc_Shared_t*)heap_caps_malloc(sizeof(Cc_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    static const char* const NAME[P_COUNT] = {"", "DigitalOcean", "Akamai", "AWS", "Own address"};
    const Cc_Month_t m = month_Now();
    JsonArray rows = values["now"].to<JsonArray>();
    char t[120];
    for (int i = 0; i < ACCOUNTS; i++){
        const Cc_Account_t& a = s->a[i];
        char key[12];
        snprintf(key, sizeof(key), "prov%d", i + 1); values[key] = a.provider;
        snprintf(key, sizeof(key), "label%d", i + 1); values[key] = a.label;
        snprintf(key, sizeof(key), "path%d", i + 1); values[key] = a.path;
        snprintf(key, sizeof(key), "budget%d", i + 1); values[key] = a.budget;
        snprintf(key, sizeof(key), "secret%d_set", i + 1); values[key] = a.secret[0] ? 1 : 0;
        if (a.provider == P_NONE) continue;
        const Cc_Result_t& r = d->r[i];
        JsonObject o = rows.add<JsonObject>();
        o["label"] = a.label[0] ? a.label : NAME[a.provider];
        if (!a.secret[0]){ o["value"] = a.provider == P_OWN ? "No address yet" : "No key yet"; o["state"] = "warn"; continue; }
        if (!appOpen){ o["value"] = "Read while Cloud Cost Meter is open"; o["state"] = "off"; continue; }
        if (!r.known){ o["value"] = r.why[0] ? r.why : "Reading"; o["state"] = r.why[0] ? "warn" : "info"; continue; }
        char cost[24], fs[24] = "", bs[24] = "";
        money(cost, sizeof(cost), r.cost, true);
        float f = 0;
        const bool hf = forecast(r.cost, m, f);
        if (hf) money(fs, sizeof(fs), f, false);
        if (a.budget) money(bs, sizeof(bs), (float)a.budget, false);
        snprintf(t, sizeof(t), "%s so far%s%s%s%s", cost, hf ? ", heading for " : "", fs, a.budget ? ", budget " : "", bs);
        o["value"] = t;
        o["state"] = !a.budget ? "info" : r.cost > (float)a.budget ? "error" : hf && f > (float)a.budget ? "warn" : "ok";
    }
    values["awsHours"] = s->awsHours;
    values["sound"] = s->sound;
    heap_caps_free(s);
    heap_caps_free(d);
}
