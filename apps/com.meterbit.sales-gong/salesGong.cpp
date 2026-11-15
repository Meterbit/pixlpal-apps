/**
 * Sales Gong: the team's goal board. This month's total in large figures against the target, a bar with a marker
 * where the team should be by today (green ahead of it, amber behind), the days left, and a leaderboard. When a
 * person's amount goes up, the gong rings on screen and aloud with their name and the amount; hitting the target
 * gets its own celebration.
 *
 * The figures come from a sheet (a Google Sheet published as CSV, any CSV, or JSON: [{name, amount}]) read by a worker
 * task, or, without a sheet, from deals added on the phone (kept for the month). Knob: turn to page the
 * leaderboard, click to read the sheet now. NVS "sgoSave".
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
#include "sgo_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define PEOPLE      16
#define NAME_LEN    24
#define URL_LEN     320
#define GONGS       4
#define NVS_KEY     "sgoSave"
#define SAVE_MAGIC  0x53474F01u
#define UA          "Pixlpal-SalesGong/1.0"
#define GONG_MS     4500

EXT_RAM_BSS_ATTR TaskHandle_t salesGong_Task_H = NULL;
static void salesGong_App_Task(void* dApplication);
static void sgo_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* salesGong_App;
Mtb_Applications* salesGong_App_GetInstance(){
    if (!salesGong_App){
        salesGong_App = new Mtb_Applications_FullScreen(salesGong_App_Task, &salesGong_Task_H, "Sales Gong", {0,0}, 8192);
        salesGong_App->mtb_App_Set_Mobile_UI(SGO_UI_MANIFEST, SGO_UI_API, nullptr, sgo_Values);
    }
    return salesGong_App;
}
MTB_REGISTER_APP(salesGong_App, 0, 0)         // the catalogue gives the app its number

struct Sgo_Person_t { char name[NAME_LEN]; double amount; };

struct Sgo_Save_t {
    uint32_t magic;
    char url[URL_LEN];
    double target;
    char unit[6];
    uint8_t minutes, sound;
    uint32_t month;                     // yyyymm the deals added on the phone are for
    Sgo_Person_t manual[PEOPLE];
    uint8_t manualN;
};

enum : uint8_t { F_NONE = 0, F_OK, F_NOT_ALLOWED, F_HTTP, F_NO_ANSWER, F_EMPTY };

struct Sgo_Gong_t { char name[NAME_LEN]; double amount; };

struct Sgo_Shared_t {
    Sgo_Person_t people[PEOPLE];        // highest first
    uint8_t n;
    uint8_t fetch;
    int code;
    uint32_t fetchedAt, version;
    Sgo_Gong_t gong[GONGS];
    uint8_t gongN;
    volatile bool readNow, stop, running;
};

static EXT_RAM_BSS_ATTR Sgo_Save_t save;
static EXT_RAM_BSS_ATTR Sgo_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static uint32_t this_Month(void){
    pxl_time_t t;
    if (!pxl_time(&t)) return 0;
    return (uint32_t)(t.year * 100 + t.month);
}

static void read_Save(Sgo_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 5; s.sound = 1; snprintf(s.unit, sizeof(s.unit), "$"); }
    if (s.minutes < 1 || s.minutes > 30) s.minutes = 5;
    if (!(s.target >= 0 && s.target < 1e15)) s.target = 0;
    if (s.manualN > PEOPLE) s.manualN = 0;
    s.url[URL_LEN - 1] = s.unit[sizeof(s.unit) - 1] = '\0';
    for (int i = 0; i < PEOPLE; i++) s.manual[i].name[NAME_LEN - 1] = '\0';
}

// Amounts -------------------------------------------------------------------------------------------------------------------------
/** "1,234,567" (whole), or "1.23M", "45.2K" when compact. */
static void fmt_Amount(char* out, size_t n, double v, bool compact){
    const bool neg = v < 0;
    double a = fabs(v);
    if (compact && a >= 1e6){ snprintf(out, n, "%s%.*fM", neg ? "-" : "", a >= 1e8 ? 0 : a >= 1e7 ? 1 : 2, a / 1e6); return; }
    if (compact && a >= 1e4){ snprintf(out, n, "%s%.*fK", neg ? "-" : "", a >= 1e5 ? 0 : 1, a / 1e3); return; }
    if (a < 100 && a != floor(a)){ snprintf(out, n, "%s%.2f", neg ? "-" : "", a); return; }
    char digits[32];
    snprintf(digits, sizeof(digits), "%.0f", a);
    const int len = (int)strlen(digits);
    size_t k = 0;
    if (neg && k + 1 < n) out[k++] = '-';
    for (int i = 0; i < len && k + 1 < n; i++){
        out[k++] = digits[i];
        if ((len - i - 1) % 3 == 0 && i < len - 1 && k + 1 < n) out[k++] = ',';
    }
    out[k] = '\0';
}
static void fmt_Money(char* out, size_t n, const char* unit, double v, bool compact){
    char a[32];
    fmt_Amount(a, sizeof(a), v, compact);
    snprintf(out, n, "%s%s", unit, a);
}

/** The number in "$4,200.50", "4200", "(1,200)"; false if it has no digit. */
static bool parse_Amount(const char* s, double& out){
    char buf[40];
    size_t k = 0;
    bool digit = false, neg = false;
    for (; *s && k + 1 < sizeof(buf); s++){
        if (isdigit((unsigned char)*s)){ buf[k++] = *s; digit = true; }
        else if (*s == '.') buf[k++] = '.';
        else if (*s == '-' || *s == '(') neg = true;
    }
    buf[k] = '\0';
    if (!digit) return false;
    out = atof(buf) * (neg ? -1 : 1);
    return true;
}

static void add_To(Sgo_Person_t* list, uint8_t& n, const char* name, double amount){
    char clean[NAME_LEN];
    mtb_Kit_To_Ascii(clean, sizeof(clean), name);
    char* p = clean;
    while (*p == ' ') p++;
    size_t len = strlen(p);
    while (len && p[len - 1] == ' ') p[--len] = '\0';
    if (!len) return;
    for (int i = 0; i < n; i++) if (!strcasecmp(list[i].name, p)){ list[i].amount += amount; return; }
    if (n == PEOPLE){                                                       // full: the smallest makes room if this is larger
        int low = 0;
        for (int i = 1; i < n; i++) if (list[i].amount < list[low].amount) low = i;
        if (list[low].amount >= amount) return;
        snprintf(list[low].name, NAME_LEN, "%s", p); list[low].amount = amount;
        return;
    }
    snprintf(list[n].name, NAME_LEN, "%s", p);
    list[n].amount = amount;
    n++;
}

static void sort_People(Sgo_Person_t* list, int n){
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && list[j].amount > list[j - 1].amount; j--){ const Sgo_Person_t t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
}

// Reading the sheet -------------------------------------------------------------------------------------------------------------
/** A CSV: the name in the first column, the amount in the second. Quoted fields; a row without a number is skipped. */
static void read_Csv(const char* text, size_t length, Sgo_Person_t* out, uint8_t& n){
    const char* p = text;
    const char* end = text + length;
    unsigned rows = 0;
    while (p < end){
        char field[2][64];
        int col = 0;
        size_t k = 0;
        field[0][0] = field[1][0] = '\0';
        bool quoted = false;
        while (p < end){
            const char c = *p++;
            if (quoted){
                if (c == '"'){ if (p < end && *p == '"'){ if (col < 2 && k + 1 < 64) field[col][k++] = '"'; p++; } else quoted = false; }
                else if (col < 2 && k + 1 < 64) field[col][k++] = c;
                continue;
            }
            if (c == '"'){ quoted = true; continue; }
            if (c == ',' || c == ';' || c == '\t'){ if (col < 2) field[col][k] = '\0'; col++; k = 0; continue; }
            if (c == '\n' || c == '\r'){ if (c == '\r' && p < end && *p == '\n') p++; break; }
            if (col < 2 && k + 1 < 64) field[col][k++] = c;
        }
        if (col < 2) field[col][k] = '\0';
        double a;
        if (field[0][0] && parse_Amount(field[1], a)) add_To(out, n, field[0], a);
        if ((++rows & 63) == 0) pxl_compat_breathe();
    }
}

static bool read_Json(const char* text, size_t length, Sgo_Person_t* out, uint8_t& n){
    JsonDocument doc;
    if (deserializeJson(doc, text, length)) return false;
    JsonArray list = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["people"].as<JsonArray>();
    if (list.isNull()) list = doc["data"].as<JsonArray>();
    if (list.isNull()) return false;
    for (JsonVariant v : list){
        const char* name = v["name"] | v["rep"] | v["person"] | (const char*)nullptr;
        if (!name) continue;
        JsonVariant a = v["amount"];
        if (a.isNull()) a = v["value"];
        if (a.isNull()) a = v["total"];
        double amount;
        if (a.is<double>()) amount = a.as<double>();
        else if (!a.is<const char*>() || !parse_Amount(a.as<const char*>(), amount)) continue;
        add_To(out, n, name, amount);
    }
    return true;
}

static void queue_Gong(const char* name, double amount){
    if (sh.gongN == GONGS) memmove(&sh.gong[0], &sh.gong[1], sizeof(sh.gong[0]) * (GONGS - 1)), sh.gongN--;
    snprintf(sh.gong[sh.gongN].name, NAME_LEN, "%s", name);
    sh.gong[sh.gongN].amount = amount;
    sh.gongN++;
}

/** The board's new figures: a gong for each person whose amount went up (not on the first read). */
static void take_Board(Sgo_Person_t* list, uint8_t n, bool gongs){
    sort_People(list, n);
    if (gongs) for (int i = 0; i < n; i++){
        double before = 0;
        for (int j = 0; j < sh.n; j++) if (!strcasecmp(sh.people[j].name, list[i].name)){ before = sh.people[j].amount; break; }
        if (list[i].amount > before + 0.005) queue_Gong(list[i].name, list[i].amount - before);
    }
    memcpy(sh.people, list, sizeof(list[0]) * n);
    sh.n = n;
    sh.version++;
}

static EXT_RAM_BSS_ATTR Sgo_Person_t readList[PEOPLE];

static void worker(void*){
    uint32_t nextRead = 0;
    bool primed = false;                                                    // a first read rings nothing
    char lastUrl[URL_LEN] = "";
    while (!sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        Sgo_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRead = millis() + s.minutes * 60000u;
        if (strcmp(lastUrl, s.url)){ primed = false; snprintf(lastUrl, sizeof(lastUrl), "%s", s.url); }
        if (!s.url[0]) continue;
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(s.url, nullptr, nullptr, 0, 65536, r, UA);
        if (sh.stop){ heap_caps_free(body); break; }
        uint8_t fetch = F_OK, n = 0;
        if (r.code == PXL_HTTP_NOT_ALLOWED) fetch = F_NOT_ALLOWED;
        else if (r.code <= 0) fetch = F_NO_ANSWER;
        else if (r.code < 200 || r.code >= 300) fetch = F_HTTP;
        else if (body){
            const char* b = body;
            while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t' || (uint8_t)*b == 0xEF || (uint8_t)*b == 0xBB || (uint8_t)*b == 0xBF) b++;
            const size_t len = r.length - (size_t)(b - body);
            if (*b == '[' || *b == '{'){ if (!read_Json(b, len, readList, n)) fetch = F_EMPTY; }
            else if (*b == '<') fetch = F_EMPTY;                                // a web page: not the sheet's CSV link
            else read_Csv(b, len, readList, n);
            if (fetch == F_OK && !n) fetch = F_EMPTY;
        }
        heap_caps_free(body);
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.url, save.url)){
            sh.fetch = fetch;
            sh.code = r.code;
            if (fetch == F_OK){ take_Board(readList, n, primed); sh.fetchedAt = (uint32_t)time(nullptr); primed = true; }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
        if (fetch != F_OK && fetch != F_NOT_ALLOWED) nextRead = millis() + 120000u;
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

/** Without a sheet: the deals added on the phone make the board. */
static void manual_Board(void){
    Sgo_Person_t list[PEOPLE];
    uint8_t n = save.manualN;
    memcpy(list, save.manual, sizeof(list[0]) * n);
    sort_People(list, n);
    memcpy(sh.people, list, sizeof(list[0]) * n);
    sh.n = n;
    sh.fetch = F_OK;
    sh.version++;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Url(JsonDocument& c){
    const char* v = c["url"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    char url[URL_LEN];
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    if (n){
        const bool scheme = !strncasecmp(v, "https://", 8) || !strncasecmp(v, "http://", 7);
        if (!scheme || n >= URL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
        snprintf(url, sizeof(url), "%.*s", (int)n, v);
        const char* host = strstr(url, "://") + 3;
        if (!*host || *host == '/' || !strchr(host, '.')){ mtb_Ble_App_Cmd_Reject(); return; }
        for (const char* k = url; *k; k++) if ((unsigned char)*k <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
    } else url[0] = '\0';
    portENTER_CRITICAL(&lock);
    snprintf(save.url, sizeof(save.url), "%s", url);
    sh.n = 0; sh.fetch = F_NONE; sh.fetchedAt = 0;
    if (!url[0]) manual_Board();
    sh.version++;
    portEXIT_CRITICAL(&lock);
    sh.readNow = true;
    phoneChanged = true;
}
static void cmd_Target(JsonDocument& c){
    double v;
    JsonVariant t = c["target"];
    if (t.is<double>()) v = t.as<double>();
    else if (!t.is<const char*>() || !parse_Amount(t.as<const char*>(), v)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (v < 0 || v >= 1e15){ mtb_Ble_App_Cmd_Reject(); return; }
    save.target = v;
    phoneChanged = true;
}
static void cmd_Unit(JsonDocument& c){
    const char* v = c["unit"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char u[16];
    mtb_Kit_To_Ascii(u, sizeof(u), v);                                      // the panel's fonts are ASCII: "₦" becomes "N"
    if (strlen(u) > 4){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(save.unit, sizeof(save.unit), "%s", u);
    phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.minutes = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Deal(JsonDocument& c){
    const char* v = c["deal"] | (const char*)nullptr;
    if (!v || !appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    // "Ada 4200", "Ada Okafor $4,200": the last word is the amount.
    char text[80];
    snprintf(text, sizeof(text), "%s", v);
    size_t n = strlen(text);
    while (n && text[n - 1] == ' ') text[--n] = '\0';
    char* sp = strrchr(text, ' ');
    double amount;
    if (!sp || !parse_Amount(sp + 1, amount) || amount <= 0){ mtb_Ble_App_Cmd_Reject(); return; }
    *sp = '\0';
    if (!text[0]){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    if (!save.url[0]){
        const uint32_t m = this_Month();
        if (m && m != save.month){ save.month = m; save.manualN = 0; }
        add_To(save.manual, save.manualN, text, amount);
        manual_Board();
    }
    char name[NAME_LEN];
    mtb_Kit_To_Ascii(name, sizeof(name), text);
    queue_Gong(name, amount);
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Gong(JsonDocument&){
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); queue_Gong("", 0); portEXIT_CRITICAL(&lock);
}
static void cmd_Read(JsonDocument&){ if (!appOpen || !save.url[0]){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }
static void cmd_Clear(JsonDocument&){
    portENTER_CRITICAL(&lock);
    save.manualN = 0;
    if (!save.url[0]) manual_Board();
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 7, 12), C_HEAD = MTB_KIT_RGB(16, 18, 30), C_ROW = MTB_KIT_RGB(14, 16, 26);
static const char* const MONTHS[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

static double total_Of(const Sgo_Shared_t& d){ double t = 0; for (int i = 0; i < d.n; i++) t += d.people[i].amount; return t; }

static void draw_Board(const Sgo_Save_t& s, const Sgo_Shared_t& d, int page){
    mtb_Panel_Fill_Screen(C_BG);
    pxl_time_t now;
    const bool timeOk = pxl_time(&now) != 0;
    const int dim = timeOk ? mtb_Kit_Days_In_Month(now.year, now.month) : 30;
    char t[64], a[40], b[40];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_HEAD);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_LABEL, timeOk ? MONTHS[now.month - 1] : "THIS MONTH", C_HEAD);
    if (timeOk){
        const int left = dim - now.day;
        snprintf(t, sizeof(t), left == 0 ? "LAST DAY" : left == 1 ? "1 DAY LEFT" : "%d DAYS LEFT", left);
        mtb_Kit_Text_Right(W - 3, 1, Terminal6x8, KIT_LABEL, t, C_HEAD);
    }

    if (s.url[0] && d.fetch != F_OK){
        const char* l1 = "READING THE SHEET";
        const char* l2 = "";
        if (d.fetch == F_NOT_ALLOWED){ l1 = "THE SHEET ISN'T ALLOWED YET"; l2 = "ALLOW ITS SITE IN THE PHONE APP"; }
        else if (d.fetch == F_HTTP){ snprintf(t, sizeof(t), "THE SHEET ANSWERED %d", d.code); l1 = t; l2 = "CHECK THE LINK IN THE PHONE APP"; }
        else if (d.fetch == F_NO_ANSWER){ l1 = "THE SHEET DIDN'T ANSWER"; l2 = "TRYING AGAIN SOON"; }
        else if (d.fetch == F_EMPTY){ l1 = "NO NAMES AND AMOUNTS FOUND"; l2 = "PUBLISH THE SHEET AS CSV: NAME, AMOUNT"; }
        if (d.fetch != F_NONE || !d.n){
            mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, l1, C_BG);
            mtb_Kit_Text_Centre(W / 2, 68, Terminal4x6, KIT_LABEL, l2, C_BG);
            return;
        }
    }

    // The total, and the target.
    const double total = total_Of(d);
    fmt_Money(a, sizeof(a), s.unit, total, false);
    const uint8_t* big = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, a) <= 168 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
    mtb_Kit_Text(4, big == PT_Sans_Narrow_Bold28x34 ? 11 : 18, big, KIT_TEXT, a, C_BG);
    const float pace = timeOk ? ((float)(now.day - 1) + (float)now.hour / 24.0f) / (float)dim : 0.0f;
    uint16_t barColor = KIT_GOLD;
    if (s.target > 0){
        const float f = (float)(total / s.target);
        fmt_Money(b, sizeof(b), s.unit, s.target, true);
        snprintf(t, sizeof(t), "OF %s", b);
        mtb_Kit_Text_Right(W - 4, 14, Terminal6x8, KIT_LABEL, t, C_BG);
        snprintf(t, sizeof(t), "%d%%", (int)(f * 100 + 0.5f));
        const bool hit = f >= 1.0f;
        barColor = hit ? KIT_GOLD : (f >= pace ? KIT_GREEN : KIT_ORANGE);
        mtb_Kit_Text_Right(W - 4, 25, Terminal10x17, barColor, t, C_BG);
        mtb_Kit_Bar(4, 47, W - 8, 6, f > 1 ? 1 : f, barColor, KIT_LINE);
        if (timeOk && !hit){                                                     // where the team should be by now
            const int px = 4 + (int)(pace * (W - 9));
            mtb_Panel_Fill_Rect((int16_t)px, 45, (int16_t)(px + 1), 54, KIT_TEXT);
        }
        const char* note = hit ? "TARGET HIT" : f >= pace ? "AHEAD OF PACE" : "BEHIND PACE";
        if (timeOk) mtb_Kit_Text(4, 56, Terminal4x6, barColor, note, C_BG);
    } else {
        mtb_Kit_Text_Right(W - 4, 20, Terminal6x8, KIT_DIM, "NO TARGET SET", C_BG);
        mtb_Kit_Bar(4, 47, W - 8, 2, 1.0f, KIT_LINE, KIT_LINE);
    }

    // The leaderboard.
    const int top = 64, rowH = 12, rows = (H - top) / rowH;
    if (!d.n){
        mtb_Kit_Text_Centre(W / 2, 84, Terminal6x8, KIT_LABEL, s.url[0] ? "NO DEALS YET" : "ADD DEALS IN THE PHONE APP", C_BG);
        return;
    }
    const int pages = (d.n + rows - 1) / rows;
    const int pg = ((page % pages) + pages) % pages;
    const double lead = d.people[0].amount > 0 ? d.people[0].amount : 1;
    for (int k = 0; k < rows && pg * rows + k < d.n; k++){
        const int i = pg * rows + k;
        const Sgo_Person_t& p = d.people[i];
        const int y = top + k * rowH;
        const uint16_t rc = i == 0 ? KIT_GOLD : i == 1 ? MTB_KIT_RGB(200, 200, 210) : i == 2 ? MTB_KIT_RGB(205, 127, 50) : KIT_LABEL;
        mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 2), C_ROW);
        snprintf(t, sizeof(t), "%d", i + 1);
        mtb_Kit_Text_Right(13, y + 2, Terminal6x8, rc, t, C_ROW);
        char nm[NAME_LEN], fit[NAME_LEN];
        snprintf(nm, sizeof(nm), "%s", p.name);
        mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, 84);
        mtb_Kit_Text(18, y + 2, Terminal6x8, KIT_TEXT, fit, C_ROW);
        fmt_Money(a, sizeof(a), s.unit, p.amount, true);
        mtb_Kit_Text_Right(W - 4, y + 2, Terminal6x8, rc, a, C_ROW);
        const int bx = 106, bw = W - 4 - mtb_Kit_Text_Width(Terminal6x8, a) - 6 - bx;
        const float f = p.amount > 0 ? (float)(p.amount / lead) : 0;
        if (bw > 4) mtb_Kit_Bar(bx, y + 3, bw, 4, f, i == 0 ? KIT_GOLD : mtb_Kit_Dim(KIT_CYAN, 160), MTB_KIT_RGB(24, 26, 40));
    }
    if (pages > 1){ snprintf(t, sizeof(t), "%d/%d", pg + 1, pages); mtb_Kit_Text_Right(W - 3, 56, Terminal4x6, KIT_DIM, t, C_BG); }
}

/** The gong: a bronze disc swinging, rings spreading from it, and who it rings for. */
static void draw_Gong(const Sgo_Save_t& s, const Sgo_Gong_t& g, uint32_t t){
    const uint16_t bg = MTB_KIT_RGB(18, 10, 2);
    mtb_Panel_Fill_Screen(bg);
    const float swing = sinf((float)t / 90.0f) * 5.0f * expf(-(float)t / 1500.0f);
    const int cx = 52 + (int)swing, cy = 66;
    for (int k = 0; k < 4; k++){                                                // rings spreading out
        const int r = 30 + (int)(((t / 12) + k * 22) % 88);
        mtb_Panel_Draw_Circle((int16_t)cx, (int16_t)cy, (int16_t)r, mtb_Kit_Dim(KIT_GOLD, (uint8_t)(200 - r * 2 > 20 ? 200 - r * 2 : 20)));
    }
    mtb_Panel_Draw_Line(52, 8, (int16_t)cx, 36, KIT_LABEL);
    mtb_Panel_Fill_Circle((int16_t)cx, (int16_t)cy, 28, MTB_KIT_RGB(176, 120, 40));
    mtb_Panel_Fill_Circle((int16_t)cx, (int16_t)cy, 21, MTB_KIT_RGB(206, 150, 60));
    mtb_Panel_Fill_Circle((int16_t)cx, (int16_t)cy, 9, MTB_KIT_RGB(240, 196, 100));
    mtb_Panel_Draw_Circle((int16_t)cx, (int16_t)cy, 28, MTB_KIT_RGB(110, 70, 20));
    mtb_Kit_Text(102, 14, PT_Sans_Narrow38x34, KIT_GOLD, "GONG!", bg);
    if (g.name[0]){
        char nm[NAME_LEN], fit[NAME_LEN], a[40];
        snprintf(nm, sizeof(nm), "%s", g.name);
        mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, nm, W - 106);
        mtb_Kit_Text(104, 56, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, bg);
        char m[44];
        fmt_Money(a, sizeof(a), s.unit, g.amount, false);
        snprintf(m, sizeof(m), "+%s", a);
        mtb_Kit_Text(104, 84, Terminal10x17, KIT_GREEN, m, bg);
    } else mtb_Kit_Text(104, 62, Terminal8x12, KIT_TEXT, "WELL DONE, TEAM!", bg);
}

static void gong_Sound(void){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t GONG[] = {{392, 40}, {196, 160}, {147, 260}, {196, 160}, {147, 620}, {110, 900}};
    snd_Play(GONG, 6, 0.4f, false);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void salesGong_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Url, cmd_Target, cmd_Unit, cmd_Minutes, cmd_Sound, cmd_Deal, cmd_Gong, cmd_Read, cmd_Clear);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    const uint32_t m = this_Month();
    if (m && save.month && m != save.month){ save.manualN = 0; save.month = m; }
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    if (!save.url[0]) manual_Board();
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "sgoNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Sgo_Shared_t* d = (Sgo_Shared_t*)heap_caps_calloc(1, sizeof(Sgo_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, gongSince = 0;
    int page = 0;
    bool gongOn = false, redraw = true, push = true, saveNow = false, wasHit = false, hitPrimed = false;
    Sgo_Gong_t gong = {};
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; page += t; redraw = true; }
        if (clickRequested){ clickRequested = false; if (gongOn) gongOn = false; else sh.readNow = true; redraw = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        if (!gongOn && sh.gongN){
            gong = sh.gong[0];
            memmove(&sh.gong[0], &sh.gong[1], sizeof(sh.gong[0]) * (GONGS - 1));
            sh.gongN--;
            gongOn = true; gongSince = ms;
            portEXIT_CRITICAL(&lock);
            gong_Sound();
        } else portEXIT_CRITICAL(&lock);
        if (gongOn && ms - gongSince >= GONG_MS){ gongOn = false; redraw = true; }

        // Hitting the target: one gong for the team (not when the app opens already past it).
        if (save.target > 0 && d->fetch == F_OK){
            const bool hit = total_Of(*d) >= save.target;
            if (hit && !wasHit && hitPrimed){ portENTER_CRITICAL(&lock); queue_Gong("TARGET HIT!", total_Of(*d)); portEXIT_CRITICAL(&lock); }
            wasHit = hit; hitPrimed = true;
        }

        if (redraw || ms - lastDraw >= (gongOn ? 40u : 1000u)){
            redraw = false;
            lastDraw = ms;
            Sgo_Save_t* s = &save;
            mtb_Panel_Begin_Update();
            if (gongOn) draw_Gong(*s, gong, ms - gongSince); else draw_Board(*s, *d, page);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(gongOn ? 15 : 40);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void sgo_Values(JsonObject values){
    Sgo_Save_t* s = (Sgo_Save_t*)heap_caps_malloc(sizeof(Sgo_Save_t), MALLOC_CAP_SPIRAM);
    Sgo_Shared_t* d = (Sgo_Shared_t*)heap_caps_malloc(sizeof(Sgo_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else {
        read_Save(*s); memset(d, 0, sizeof(*d));
        if (!s->url[0]){ memcpy(d->people, s->manual, sizeof(s->manual[0]) * s->manualN); d->n = s->manualN; sort_People(d->people, d->n); d->fetch = F_OK; }
    }
    char t[96], a[40], b[40];
    values["url"] = s->url;
    fmt_Amount(a, sizeof(a), s->target, false);
    if (s->target > 0) values["target"] = a; else values["target"] = "";     // a (char*) is copied
    values["unit"] = s->unit;
    values["minutes"] = s->minutes;
    values["sound"] = s->sound;
    JsonArray month = values["month"].to<JsonArray>();
    JsonObject r = month.add<JsonObject>();
    double total = 0;
    for (int i = 0; i < d->n; i++) total += d->people[i].amount;
    fmt_Money(a, sizeof(a), s->unit, total, false);
    r["label"] = "Total";
    if (s->target > 0){
        fmt_Money(b, sizeof(b), s->unit, s->target, false);
        snprintf(t, sizeof(t), "%s of %s (%d%%)", a, b, (int)(total * 100 / s->target + 0.5));
        r["meter"]["value"] = "totalNow"; r["meter"]["max"] = "targetNow";
        values["totalNow"] = total; values["targetNow"] = s->target;
        r["state"] = total >= s->target ? "ok" : "info";
    } else { snprintf(t, sizeof(t), "%s (no target set)", a); r["state"] = "info"; }
    r["value"] = t;
    JsonObject src = month.add<JsonObject>();
    src["label"] = "Figures from";
    if (!s->url[0]){ src["value"] = "Deals added here"; src["state"] = "info"; }
    else if (!appOpen){ src["value"] = "The sheet, read while Sales Gong is open"; src["state"] = "off"; }
    else if (d->fetch == F_OK){ src["value"] = "The sheet"; src["state"] = "ok"; if (d->fetchedAt){ values["fetchedAt"] = d->fetchedAt; src["ago"] = "fetchedAt"; } }
    else if (d->fetch == F_NOT_ALLOWED){ src["value"] = "Allow the sheet's site when the phone app asks"; src["state"] = "warn"; }
    else if (d->fetch == F_EMPTY){ src["value"] = "No names and amounts found in the sheet"; src["state"] = "error"; }
    else if (d->fetch == F_NONE){ src["value"] = "Reading the sheet"; src["state"] = "info"; }
    else { snprintf(t, sizeof(t), "The sheet can't be read (%d)", d->code); src["value"] = t; src["state"] = "error"; }
    JsonArray board = values["board"].to<JsonArray>();
    for (int i = 0; i < d->n; i++){
        JsonObject o = board.add<JsonObject>();
        snprintf(t, sizeof(t), "%d. %s", i + 1, d->people[i].name);
        o["label"] = t;
        fmt_Money(a, sizeof(a), s->unit, d->people[i].amount, false);
        o["value"] = a;
        o["state"] = i == 0 ? "ok" : "info";
    }
    heap_caps_free(s);
    heap_caps_free(d);
}
