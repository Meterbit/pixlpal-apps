/**
 * HSE Board: a site's safety board, to be read from across a canteen, a gatehouse or a control room.
 *
 * Pages in turn (the days counter between each of the others, so it is never long off the screen):
 *   - Days without a lost-time injury, in giant digits, with the best record and a bar towards it ("new site record"
 *     once passed). The record goes up by itself.
 *   - This month's and this year's lost-time injuries, medical treatment cases, first aid cases, near misses and
 *     safety observations (the month's start again on the 1st, the year's on 1 January).
 *   - Man-hours without a lost-time injury and the year's LTIFR and TRIFR, from the man-hours worked a day (per
 *     1,000,000 hours, or per 200,000 as OSHA counts).
 *   - The safety message of the week.
 *   - A safety reminder for the sectors chosen (manufacturing, oil and gas, mining, construction, facility
 *     management, warehouse, general: one or more, their reminders in turn) as an ISO 7010-style sign.
 *   - On a milestone day (30, 100, 365... days), a celebration, with a chime when it comes.
 *
 * The figures are recorded from the phone (an event today, or corrections), or read from a sheet (CSV or JSON: names
 * and values) by a worker task, so every board on a site shows the same figures. Knob: turn or click for the next
 * page. Settings and figures in the app's saved data (under 1 KB).
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
#include "hse_Mobile_Ui.h"
#include "hse_Sectors.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define URL_LEN     300
#define SITE_LEN    28
#define MSG_LEN     120
#define NVS_KEY     "hseSave"
#define SAVE_MAGIC  0x48534501u
#define UA          "Pixlpal-HSEBoard/1.0"
#define KINDS       5               // lost-time injury, medical treatment, first aid, near miss, observation
#define NO_UNDO     0xFF

EXT_RAM_BSS_ATTR TaskHandle_t hseBoard_Task_H = NULL;
static void hseBoard_App_Task(void* dApplication);
static void hse_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* hseBoard_App;
Mtb_Applications* hseBoard_App_GetInstance(){
    if (!hseBoard_App){
        hseBoard_App = new Mtb_Applications_FullScreen(hseBoard_App_Task, &hseBoard_Task_H, "HSE Board", {0,0}, 8192);
        hseBoard_App->mtb_App_Set_Mobile_UI(HSE_UI_MANIFEST, HSE_UI_API, HSE_UI_DATA, hse_Values);
    }
    return hseBoard_App;
}
MTB_REGISTER_APP(hseBoard_App, 0, 0)          // the catalogue gives the app its number

struct Hse_Save_t {
    uint32_t magic;
    uint8_t sector, reminders, sound, seconds, minutes, rateBase, undoKind;
    uint8_t sectors;                // bit N: sector N's reminders are shown (0 in 1.0.0's saves: just `sector`)
    int32_t lastLti;                // the day of the last lost-time injury (days since 1970-01-01); 0: none recorded
    uint32_t record;                // the best run, in days
    uint32_t hoursPerDay;           // man-hours worked a day, on average; 0: no hours or rates
    uint32_t month;                 // yyyymm the month's counts are for
    uint32_t year;                  // the year the year's counts are for
    uint16_t m[KINDS], y[KINDS];    // this month's and this year's counts
    int32_t chimed;                 // the day a milestone last chimed
    int32_t undoLastLti;            // before the last event recorded here (undoKind: its kind, NO_UNDO: none)
    uint32_t undoRecord;
    char site[SITE_LEN];
    char message[MSG_LEN];
    char url[URL_LEN];
};
static_assert(sizeof(Hse_Save_t) <= PXL_SAVE_MAX, "HSE Board's saved data must fit the app's 1 KB");

enum : uint8_t { F_NONE = 0, F_OK, F_NOT_ALLOWED, F_HTTP, F_NO_ANSWER, F_EMPTY };
struct Hse_Sheet_t { uint8_t fetch; int code; uint32_t fetchedAt; volatile bool readNow, stop, running; };

static EXT_RAM_BSS_ATTR Hse_Save_t save;
static EXT_RAM_BSS_ATTR Hse_Sheet_t sheet;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static const char* const KIND_NAME[KINDS] = {"LOST-TIME INJURY", "MEDICAL TREATMENT", "FIRST AID", "NEAR MISSES", "OBSERVATIONS"};
static const char* const KIND_PHONE[KINDS] = {"Lost-time injuries", "Medical treatment cases", "First aid cases", "Near misses", "Safety observations"};
static const char* const KIND_KEY[KINDS] = {"lti", "mtc", "fac", "nm", "obs"};
static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* const MONTH_NAMES[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

static void defaults(Hse_Save_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = SAVE_MAGIC;
    s.reminders = 1; s.sound = 1; s.seconds = 10; s.minutes = 10; s.undoKind = NO_UNDO;
}

static void read_Save(Hse_Save_t& s){
    defaults(s);
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC) defaults(s);
    if (s.sector >= HSE_SECTOR_COUNT) s.sector = 0;
    s.sectors &= (uint8_t)((1u << HSE_SECTOR_COUNT) - 1);
    if (!s.sectors) s.sectors = (uint8_t)(1u << s.sector);
    if (s.seconds < 5 || s.seconds > 60) s.seconds = 10;
    if (s.minutes < 1 || s.minutes > 60) s.minutes = 10;
    s.rateBase = s.rateBase ? 1 : 0;
    s.site[SITE_LEN - 1] = s.message[MSG_LEN - 1] = s.url[URL_LEN - 1] = '\0';
}

// The sectors chosen: their reminders one after another; one sector's slogan, or the General one for several.
static uint8_t reminder_Count(const Hse_Save_t& s){
    unsigned n = 0;
    for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++) if (s.sectors >> i & 1) n += HSE_SECTORS[i].count;
    return (uint8_t)n;
}
static const Hse_Reminder_t& reminder_At(const Hse_Save_t& s, unsigned index){
    for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++){
        if (!(s.sectors >> i & 1)) continue;
        if (index < HSE_SECTORS[i].count) return HSE_SECTORS[i].reminders[index];
        index -= HSE_SECTORS[i].count;
    }
    return HSE_SECTORS[0].reminders[0];
}
static const char* slogan(const Hse_Save_t& s){
    for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++) if (s.sectors == (1u << i)) return HSE_SECTORS[i].slogan;
    return HSE_SECTORS[0].slogan;
}

// Days -----------------------------------------------------------------------------------------------------------------
static bool today(pxl_time_t& t, int32_t& day){
    if (!pxl_time(&t)) return false;
    day = mtb_Kit_Days_From_Civil(t.year, t.month, t.day);
    return true;
}

/** The days without a lost-time injury (-1: none recorded, or the clock isn't set). */
static int32_t days_Without(const Hse_Save_t& s, int32_t todayDay){
    if (!s.lastLti || todayDay < s.lastLti) return -1;
    return todayDay - s.lastLti;
}

static void civil_From_Days(int32_t z, int& y, int& m, int& d){         // H. Hinnant's algorithm
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

/** "2025-03-14" -> its day; 0 if it isn't a date. */
static int32_t parse_Date(const char* s){
    int y, m, d;
    if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > mtb_Kit_Days_In_Month(y, m)) return 0;
    return mtb_Kit_Days_From_Civil(y, m, d);
}

static bool milestone(int32_t days){
    static const int32_t MARKS[] = {30, 50, 100, 150, 200, 250, 300, 365, 400, 500, 600, 700, 730, 750, 800, 900, 1000};
    for (int32_t m : MARKS) if (days == m) return true;
    return days > 1000 && (days % 250 == 0 || days % 365 == 0);
}

/** A new month or year: its counts start again. The best record follows the days. Caller holds the lock. */
static bool roll_Periods(Hse_Save_t& s, const pxl_time_t& t, int32_t todayDay){
    bool changed = false;
    const uint32_t month = (uint32_t)(t.year * 100 + t.month);
    if (s.month != month){ if (s.month) memset(s.m, 0, sizeof(s.m)); s.month = month; changed = true; }
    if (s.year != (uint32_t)t.year){ if (s.year) memset(s.y, 0, sizeof(s.y)); s.year = (uint32_t)t.year; changed = true; }
    const int32_t days = days_Without(s, todayDay);
    if (days > 0 && (uint32_t)days > s.record){ s.record = (uint32_t)days; changed = true; }
    return changed;
}

// Numbers --------------------------------------------------------------------------------------------------------------
/** "1,234,567", or "12.3M" when compact and large. */
static void fmt_Count(char* out, size_t n, double v, bool compact){
    if (compact && v >= 1e7){ snprintf(out, n, "%.1fM", v / 1e6); return; }
    char digits[24];
    snprintf(digits, sizeof(digits), "%.0f", v < 0 ? 0 : v);
    const int len = (int)strlen(digits);
    size_t k = 0;
    for (int i = 0; i < len && k + 1 < n; i++){
        out[k++] = digits[i];
        if ((len - i - 1) % 3 == 0 && i < len - 1 && k + 1 < n) out[k++] = ',';
    }
    out[k] = '\0';
}

static double rate_Base(const Hse_Save_t& s){ return s.rateBase ? 200000.0 : 1000000.0; }

/** The year's hours so far, and its LTIFR and TRIFR (lost-time and medical treatment cases). False without hours. */
static bool rates(const Hse_Save_t& s, const pxl_time_t& t, double& hoursYear, double& ltifr, double& trifr){
    if (!s.hoursPerDay) return false;
    hoursYear = (double)(t.yearday + 1) * s.hoursPerDay;
    ltifr = s.y[0] * rate_Base(s) / hoursYear;
    trifr = (s.y[0] + s.y[1]) * rate_Base(s) / hoursYear;
    return true;
}

// The sheet -------------------------------------------------------------------------------------------------------------
/** One name and value from the sheet, into the figures. Caller holds the lock. True if it was a name it knows. */
static bool take_Value(Hse_Save_t& s, const char* rawName, const char* value){
    char name[24];
    size_t k = 0;
    for (const char* p = rawName; *p && k + 1 < sizeof(name); p++){
        const char c = (char)tolower((unsigned char)*p);
        if (isalnum((unsigned char)c)) name[k++] = c;
        else if ((c == '_' || c == ' ' || c == '-') && k && name[k - 1] != '_') name[k++] = '_';
    }
    while (k && name[k - 1] == '_') k--;
    name[k] = '\0';
    while (*value == ' ') value++;
    if (!strcmp(name, "last_lti")){ const int32_t d = parse_Date(value); if (d || !*value){ s.lastLti = d; return true; } return false; }
    if (!strcmp(name, "message")){ mtb_Kit_To_Ascii(s.message, sizeof(s.message), value); return true; }
    if (!strcmp(name, "site")){ mtb_Kit_To_Ascii(s.site, sizeof(s.site), value); return true; }
    char* end;
    const long v = strtol(value, &end, 10);
    if (end == value || v < 0) return false;
    if (!strcmp(name, "record")){ s.record = (uint32_t)v; return true; }
    if (!strcmp(name, "hours_per_day")){ if (v > 10000000) return false; s.hoursPerDay = (uint32_t)v; return true; }
    for (int i = 0; i < KINDS; i++){
        char key[16];
        snprintf(key, sizeof(key), "%s_month", KIND_KEY[i]);
        if (!strcmp(name, key)){ s.m[i] = (uint16_t)(v > 65535 ? 65535 : v); return true; }
        snprintf(key, sizeof(key), "%s_year", KIND_KEY[i]);
        if (!strcmp(name, key)){ s.y[i] = (uint16_t)(v > 65535 ? 65535 : v); return true; }
    }
    return false;
}

/** A CSV of names and values (quoted fields; a heading row or an unknown name is skipped). The number it knew. */
static int read_Csv(Hse_Save_t& s, const char* text, size_t length){
    const char* p = text;
    const char* end = text + length;
    int known = 0;
    unsigned rows = 0;
    while (p < end){
        char field[2][MSG_LEN];
        int col = 0;
        size_t k = 0;
        field[0][0] = field[1][0] = '\0';
        bool quoted = false;
        while (p < end){
            const char c = *p++;
            if (quoted){
                if (c == '"'){ if (p < end && *p == '"'){ if (col < 2 && k + 1 < MSG_LEN) field[col][k++] = '"'; p++; } else quoted = false; }
                else if (col < 2 && k + 1 < MSG_LEN) field[col][k++] = c;
                continue;
            }
            if (c == '"'){ quoted = true; continue; }
            if (c == ',' || c == ';' || c == '\t'){ if (col < 2) field[col][k] = '\0'; col++; k = 0; continue; }
            if (c == '\n' || c == '\r'){ if (c == '\r' && p < end && *p == '\n') p++; break; }
            if (col < 2 && k + 1 < MSG_LEN) field[col][k++] = c;
        }
        if (col < 2) field[col][k] = '\0';
        if (field[0][0] && take_Value(s, field[0], field[1])) known++;
        if ((++rows & 63) == 0) pxl_compat_breathe();
    }
    return known;
}

/** A JSON object of names and values. The number it knew; -1 if it isn't JSON. */
static int read_Json(Hse_Save_t& s, const char* text, size_t length){
    JsonDocument doc;
    if (deserializeJson(doc, text, length) || !doc.is<JsonObject>()) return -1;
    int known = 0;
    for (JsonPair kv : doc.as<JsonObject>()){
        char value[MSG_LEN];
        if (kv.value().is<const char*>()) snprintf(value, sizeof(value), "%s", kv.value().as<const char*>());
        else if (kv.value().is<long>()) snprintf(value, sizeof(value), "%ld", kv.value().as<long>());
        else if (kv.value().is<double>()) snprintf(value, sizeof(value), "%.0f", kv.value().as<double>());
        else continue;
        if (take_Value(s, kv.key().c_str(), value)) known++;
    }
    return known;
}

static void worker(void*){
    uint32_t nextRead = 0;
    while (!sheet.stop){
        if (!sheet.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sheet.readNow = false;
        char url[URL_LEN];
        uint8_t minutes;
        portENTER_CRITICAL(&lock); snprintf(url, sizeof(url), "%s", save.url); minutes = save.minutes; portEXIT_CRITICAL(&lock);
        nextRead = millis() + minutes * 60000u;
        if (!url[0]) continue;
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 32768, r, UA);
        if (sheet.stop){ heap_caps_free(body); break; }
        uint8_t fetch = F_OK;
        Hse_Save_t* next = (Hse_Save_t*)heap_caps_malloc(sizeof(Hse_Save_t), MALLOC_CAP_SPIRAM);
        if (!next){ heap_caps_free(body); nextRead = millis() + 60000u; continue; }
        portENTER_CRITICAL(&lock); *next = save; portEXIT_CRITICAL(&lock);
        if (r.code == PXL_HTTP_NOT_ALLOWED) fetch = F_NOT_ALLOWED;
        else if (r.code <= 0) fetch = F_NO_ANSWER;
        else if (r.code < 200 || r.code >= 300) fetch = F_HTTP;
        else if (body){
            const char* b = body;
            while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t' || (uint8_t)*b == 0xEF || (uint8_t)*b == 0xBB || (uint8_t)*b == 0xBF) b++;
            const size_t len = r.length - (size_t)(b - body);
            int known;
            if (*b == '{') known = read_Json(*next, b, len);
            else if (*b == '<') known = -1;                         // a web page: not the sheet's CSV link
            else known = read_Csv(*next, b, len);
            if (known <= 0) fetch = F_EMPTY;
        } else fetch = F_EMPTY;
        heap_caps_free(body);
        portENTER_CRITICAL(&lock);
        if (!strcmp(url, save.url)){
            sheet.fetch = fetch;
            sheet.code = r.code;
            if (fetch == F_OK){
                // The sheet's figures; the settings made on the phone stay.
                save.lastLti = next->lastLti; save.record = next->record; save.hoursPerDay = next->hoursPerDay;
                memcpy(save.m, next->m, sizeof(save.m)); memcpy(save.y, next->y, sizeof(save.y));
                memcpy(save.message, next->message, sizeof(save.message)); memcpy(save.site, next->site, sizeof(save.site));
                save.undoKind = NO_UNDO;
                sheet.fetchedAt = (uint32_t)time(nullptr);
            }
        }
        portEXIT_CRITICAL(&lock);
        heap_caps_free(next);
        phoneChanged = true;
        if (fetch != F_OK && fetch != F_NOT_ALLOWED) nextRead = millis() + 120000u;
    }
    sheet.running = false;
    vTaskDelete(nullptr);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Event(JsonDocument& c){
    long kind;
    pxl_time_t t;
    int32_t day;
    if (!mtb_Kit_Json_Range(c["event"], 0, KINDS - 1, kind) || !today(t, day)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    roll_Periods(save, t, day);
    save.undoKind = (uint8_t)kind;
    save.undoLastLti = save.lastLti;
    save.undoRecord = save.record;
    if (kind == 0){
        const int32_t days = days_Without(save, day);
        if (days > 0 && (uint32_t)days > save.record) save.record = (uint32_t)days;
        save.lastLti = day;
    }
    if (save.m[kind] < 65535) save.m[kind]++;
    if (save.y[kind] < 65535) save.y[kind]++;
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Undo(JsonDocument&){
    portENTER_CRITICAL(&lock);
    const uint8_t kind = save.undoKind;
    if (kind < KINDS){
        if (kind == 0){ save.lastLti = save.undoLastLti; save.record = save.undoRecord; }
        if (save.m[kind]) save.m[kind]--;
        if (save.y[kind]) save.y[kind]--;
        save.undoKind = NO_UNDO;
    }
    portEXIT_CRITICAL(&lock);
    if (kind >= KINDS){ mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_Text(JsonDocument& c){
    if (c["site"].is<const char*>()){
        char v[SITE_LEN + 8];
        mtb_Kit_To_Ascii(v, sizeof(v), c["site"].as<const char*>());       // the panel's fonts are ASCII
        if (strlen(v) >= SITE_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
        portENTER_CRITICAL(&lock); snprintf(save.site, sizeof(save.site), "%s", v); portEXIT_CRITICAL(&lock);
    } else if (c["message"].is<const char*>()){
        char v[MSG_LEN + 8];
        mtb_Kit_To_Ascii(v, sizeof(v), c["message"].as<const char*>());
        if (strlen(v) >= MSG_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
        portENTER_CRITICAL(&lock); snprintf(save.message, sizeof(save.message), "%s", v); portEXIT_CRITICAL(&lock);
    } else if (c["url"].is<const char*>()){
        const char* v = c["url"].as<const char*>();
        while (*v == ' ') v++;
        size_t n = strlen(v);
        while (n && v[n - 1] == ' ') n--;
        char url[URL_LEN];
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
        sheet.fetch = F_NONE; sheet.fetchedAt = 0;
        portEXIT_CRITICAL(&lock);
        sheet.readNow = true;
    } else { mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
// Command 3: {"sectors": ["General", "Oil and gas"]} (names, or numbers 0-6), one or more; or 1.0.0's {"sector": 0-6}.
static void cmd_Sector(JsonDocument& c){
    uint8_t mask = 0;
    long v;
    if (c["sectors"].is<JsonArrayConst>()){
        for (JsonVariantConst item : c["sectors"].as<JsonArrayConst>()){
            int found = -1;
            if (mtb_Kit_Json_Range(item, 0, HSE_SECTOR_COUNT - 1, v)) found = (int)v;
            else if (item.is<const char*>())
                for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++) if (!strcmp(item.as<const char*>(), HSE_SECTORS[i].name)) found = (int)i;
            if (found < 0){ mtb_Ble_App_Cmd_Reject(); return; }
            mask |= (uint8_t)(1u << found);
        }
    } else if (mtb_Kit_Json_Range(c["sector"], 0, HSE_SECTOR_COUNT - 1, v)) mask = (uint8_t)(1u << v);
    if (!mask){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    save.sectors = mask;
    for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++) if (mask >> i & 1){ save.sector = (uint8_t)i; break; }   // the first, as 1.0.0 kept it
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static bool whole_Number(JsonVariantConst field, long low, long high, long& out){
    if (mtb_Kit_Json_Range(field, low, high, out)) return true;
    if (!field.is<const char*>()) return false;
    const char* s = field.as<const char*>();
    char digits[16];
    size_t k = 0;
    for (; *s; s++){
        if (isdigit((unsigned char)*s)){ if (k + 1 < sizeof(digits)) digits[k++] = *s; else return false; }
        else if (*s != ',' && *s != ' ') return false;              // "1,200" and "1 200" are fine; "-3" and "2.5" aren't
    }
    if (!k) return false;
    digits[k] = '\0';
    out = atol(digits);
    return out >= low && out <= high;
}
static void cmd_Hours(JsonDocument& c){
    long v;
    if (!whole_Number(c["hours"], 0, 10000000, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.hoursPerDay = (uint32_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Rate_Base(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["rateBase"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.rateBase = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Seconds(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["seconds"], 5, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.seconds = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Option(JsonDocument& c){
    long v;
    if (mtb_Kit_Json_Range(c["reminders"], 0, 1, v)) save.reminders = (uint8_t)v;
    else if (mtb_Kit_Json_Range(c["sound"], 0, 1, v)) save.sound = (uint8_t)v;
    else { mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_Last_Lti(JsonDocument& c){
    const char* v = c["lastLti"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    int32_t day = 0;
    if (*v){
        day = parse_Date(v);
        pxl_time_t t;
        int32_t now;
        if (!day || (today(t, now) && day > now)){ mtb_Ble_App_Cmd_Reject(); return; }
    }
    portENTER_CRITICAL(&lock); save.lastLti = day; save.undoKind = NO_UNDO; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Figure(JsonDocument& c){
    long v;
    bool done = false;
    portENTER_CRITICAL(&lock);
    if (c["record"].is<const char*>() || c["record"].is<long>()){
        if (whole_Number(c["record"], 0, 100000, v)){ save.record = (uint32_t)v; done = true; }
    } else for (int i = 0; i < KINDS && !done; i++){
        char key[8];
        snprintf(key, sizeof(key), "%s_m", KIND_KEY[i]);
        if (!c[key].isNull()){ if (whole_Number(c[key], 0, 65535, v)){ save.m[i] = (uint16_t)v; done = true; } break; }
        snprintf(key, sizeof(key), "%s_y", KIND_KEY[i]);
        if (!c[key].isNull()){ if (whole_Number(c[key], 0, 65535, v)){ save.y[i] = (uint16_t)v; done = true; } break; }
    }
    if (done) save.undoKind = NO_UNDO;
    portEXIT_CRITICAL(&lock);
    if (!done){ mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_Read(JsonDocument&){ if (!appOpen || !save.url[0]){ mtb_Ble_App_Cmd_Reject(); return; } sheet.readNow = true; }
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.minutes = (uint8_t)v;
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int steps = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) steps++; }
static void on_Turn(rotary_encoder_rotation_t d){ steps += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 6, 10), C_HEAD = MTB_KIT_RGB(14, 20, 28), C_ROW = MTB_KIT_RGB(12, 15, 22);
static const uint16_t C_SAFE = MTB_KIT_RGB(40, 210, 90), C_BLUE = MTB_KIT_RGB(0, 92, 210), C_YELLOW = MTB_KIT_RGB(255, 205, 0);
static const uint16_t C_RED = MTB_KIT_RGB(225, 30, 30), C_SIGN_GREEN = MTB_KIT_RGB(0, 150, 70);
static const uint16_t KIND_COLOUR[KINDS] = {MTB_KIT_RGB(225, 40, 40), MTB_KIT_RGB(255, 130, 0), MTB_KIT_RGB(255, 200, 0), MTB_KIT_RGB(80, 200, 255), MTB_KIT_RGB(70, 130, 255)};

enum Page_t : uint8_t { P_DAYS = 0, P_STATS, P_RATES, P_MESSAGE, P_REMINDER, P_MILESTONE };

static void header(const Hse_Save_t& s, bool timeOk, const pxl_time_t& t){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    char left[SITE_LEN + 4], fit[SITE_LEN + 4];
    snprintf(left, sizeof(left), "%s", s.site[0] ? s.site : slogan(s));
    mtb_Kit_Upper(left);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, left, 170);
    mtb_Kit_Text(3, 2, Terminal6x8, C_SAFE, fit, C_HEAD);
    if (timeOk){
        char date[20];
        snprintf(date, sizeof(date), "%d %s %d", (int)t.day, MONTHS[(t.month + 11) % 12], (int)t.year);
        mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_LABEL, date, C_HEAD);
    }
}

static void centred_Note(const char* first, const char* second){
    mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, first, C_BG);
    if (second) mtb_Kit_Text_Centre(W / 2, 70, Terminal6x8, KIT_LABEL, second, C_BG);
}

/** The days without a lost-time injury: up to four digits the board's height, the best record and a bar towards it. */
static void draw_Days(const Hse_Save_t& s, int32_t days, int32_t todayDay){
    mtb_Kit_Text_Centre(W / 2, 14, Terminal6x8, KIT_LABEL, "DAYS WITHOUT A LOST-TIME INJURY", C_BG);
    if (days < 0){
        mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, "WHEN WAS THE LAST ONE?", C_BG);
        mtb_Kit_Text_Centre(W / 2, 70, Terminal6x8, KIT_LABEL, "SET ITS DATE IN THE PHONE APP", C_BG);
        return;
    }
    const int32_t shown = days > 9999 ? 9999 : days;
    char digits[8];
    snprintf(digits, sizeof(digits), "%ld", (long)shown);
    const int n = (int)strlen(digits);
    const int16_t w = 42, h = 70, t = 9, gap = 10;
    const int16_t total = (int16_t)(n * w + (n - 1) * gap);
    const int16_t x0 = (W - total) / 2, y0 = 25;
    const uint16_t colour = days == 0 ? C_RED : C_SAFE;
    for (int i = 0; i < n; i++)
        mtb_Kit_Segment_Digit((int16_t)(x0 + i * (w + gap)), y0, w, h, t, digits[i] - '0', colour, mtb_Kit_Dim(colour, 22));

    char line[48];
    const bool newRecord = days > 0 && (uint32_t)days >= s.record;
    if (newRecord) mtb_Kit_Text(4, 102, Terminal6x8, KIT_GOLD, "NEW SITE RECORD!", C_BG);
    else { snprintf(line, sizeof(line), "BEST RECORD %lu DAYS", (unsigned long)s.record); mtb_Kit_Text(4, 102, Terminal6x8, KIT_TEXT, line, C_BG); }
    int y, m, d;
    civil_From_Days(s.lastLti, y, m, d);
    snprintf(line, sizeof(line), days == 0 ? "LAST ONE TODAY" : "SINCE %d %s %d", d, MONTHS[(m + 11) % 12], y);
    mtb_Kit_Text_Right(W - 4, 102, Terminal6x8, KIT_LABEL, line, C_BG);
    const float f = s.record ? (float)days / (float)s.record : (days ? 1.0f : 0.0f);
    mtb_Kit_Bar(4, 115, W - 8, 6, f > 1 ? 1 : f, newRecord ? KIT_GOLD : C_SAFE, KIT_LINE);
    (void)todayDay;
}

/** This month's and this year's counts, one kind a row. */
static void draw_Stats(const Hse_Save_t& s, bool timeOk, const pxl_time_t& t){
    char title[32];
    snprintf(title, sizeof(title), "SAFETY STATISTICS");
    mtb_Kit_Text(4, 13, Terminal6x8, KIT_LABEL, title, C_BG);
    mtb_Kit_Text_Right(196, 13, Terminal6x8, KIT_LABEL, timeOk ? MONTHS[(t.month + 11) % 12] : "MONTH", C_BG);
    char year[8];
    snprintf(year, sizeof(year), "%d", timeOk ? (int)t.year : 0);
    mtb_Kit_Text_Right(W - 4, 13, Terminal6x8, KIT_LABEL, timeOk ? year : "YEAR", C_BG);
    for (int i = 0; i < KINDS; i++){
        const int16_t y = (int16_t)(24 + i * 21);
        mtb_Panel_Fill_Rect(0, y, W - 1, (int16_t)(y + 18), C_ROW);
        mtb_Panel_Fill_Rect(3, (int16_t)(y + 3), 6, (int16_t)(y + 15), KIND_COLOUR[i]);
        mtb_Kit_Text(12, (int16_t)(y + 4), Terminal8x12, KIT_TEXT, KIND_NAME[i], C_ROW);
        char a[8], b[8];
        snprintf(a, sizeof(a), "%u", s.m[i]);
        snprintf(b, sizeof(b), "%u", s.y[i]);
        const uint16_t mc = i == 0 ? (s.m[0] ? C_RED : C_SAFE) : KIT_TEXT;
        const uint16_t yc = i == 0 ? (s.y[0] ? C_RED : C_SAFE) : KIT_TEXT;
        mtb_Kit_Text_Right(196, (int16_t)(y + 1), Terminal10x17, mc, a, C_ROW);
        mtb_Kit_Text_Right(W - 4, (int16_t)(y + 1), Terminal10x17, yc, b, C_ROW);
    }
}

/** Man-hours without a lost-time injury, and the year's rates. */
static void draw_Rates(const Hse_Save_t& s, int32_t days, const pxl_time_t& t){
    mtb_Kit_Text_Centre(W / 2, 14, Terminal6x8, KIT_LABEL, "MAN-HOURS WITHOUT A LOST-TIME INJURY", C_BG);
    char a[24], b[64];
    if (days >= 0){
        fmt_Count(a, sizeof(a), (double)days * s.hoursPerDay, true);
        const uint8_t* big = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, a) <= W - 8 ? PT_Sans_Narrow_Bold28x34 : PT_Sans_Narrow_Bold18x22;
        mtb_Kit_Text_Centre(W / 2, 26, big, C_SAFE, a, C_BG);
    } else mtb_Kit_Text_Centre(W / 2, 36, Terminal8x12, KIT_LABEL, "SET THE LAST LTI DATE", C_BG);
    double hoursYear, ltifr, trifr;
    rates(s, t, hoursYear, ltifr, trifr);
    const int16_t y = 70;
    mtb_Panel_Fill_Round_Rect(4, y, 124, y + 34, 4, C_ROW);
    mtb_Panel_Fill_Round_Rect(132, y, W - 5, y + 34, 4, C_ROW);
    mtb_Kit_Text(10, y + 4, Terminal6x8, KIT_LABEL, "LTIFR", C_ROW);
    mtb_Kit_Text(138, y + 4, Terminal6x8, KIT_LABEL, "TRIFR", C_ROW);
    snprintf(a, sizeof(a), "%.2f", ltifr);
    mtb_Kit_Text_Right(118, y + 14, Terminal10x17, s.y[0] ? C_RED : C_SAFE, a, C_ROW);
    snprintf(a, sizeof(a), "%.2f", trifr);
    mtb_Kit_Text_Right(W - 11, y + 14, Terminal10x17, (s.y[0] + s.y[1]) ? KIND_COLOUR[1] : C_SAFE, a, C_ROW);
    char hours[16];
    fmt_Count(hours, sizeof(hours), s.hoursPerDay, false);
    snprintf(b, sizeof(b), "THIS YEAR, PER %s HOURS. %s MAN-HOURS A DAY", s.rateBase ? "200,000" : "1,000,000", hours);
    char fit[64];
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, b, W - 8);
    mtb_Kit_Text_Centre(W / 2, 112, Terminal4x6, KIT_LABEL, fit, C_BG);
}

/** Text as large as it fits in w x h at (x, y): PT Sans 18x22 in up to @p bigLines lines, else Terminal 8x12. */
static void fitted_Text(int16_t x, int16_t y, int16_t w, int16_t h, const char* text, int bigLines, uint16_t colour, bool centre){
    char lines[8][64];
    int n = mtb_Kit_Wrap(text, PT_Sans_Narrow_Bold18x22, w, &lines[0][0], sizeof(lines[0]), bigLines + 1);
    const uint8_t* font = PT_Sans_Narrow_Bold18x22;
    int16_t lineH = 24;
    if (n > bigLines){
        font = Terminal8x12; lineH = 14;
        n = mtb_Kit_Wrap(text, Terminal8x12, w, &lines[0][0], sizeof(lines[0]), h / lineH);
    }
    int16_t ty = (int16_t)(y + (h - n * lineH) / 2);
    for (int i = 0; i < n; i++, ty += lineH){
        if (centre) mtb_Kit_Text_Centre((int16_t)(x + w / 2), ty, font, colour, lines[i], C_BG);
        else mtb_Kit_Text(x, ty, font, colour, lines[i], C_BG);
    }
}

static void exclamation(int16_t cx, int16_t cy, uint16_t colour){
    mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(cy - 18), (int16_t)(cx + 3), (int16_t)(cy + 6), colour);
    mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(cy + 11), (int16_t)(cx + 3), (int16_t)(cy + 17), colour);
}

/** An ISO 7010-style sign about 64 px across, centred at (cx, cy). */
static void draw_Sign(Hse_Sign_t sign, int16_t cx, int16_t cy){
    switch (sign){
        case SIGN_MANDATORY:            // M001: a blue disc, a white exclamation mark
            mtb_Panel_Fill_Circle(cx, cy, 32, C_BLUE);
            exclamation(cx, cy, WHITE);
            break;
        case SIGN_WARNING:              // W001: a yellow triangle with a black border, a black exclamation mark
            mtb_Panel_Fill_Triangle(cx, (int16_t)(cy - 34), (int16_t)(cx - 38), (int16_t)(cy + 30), (int16_t)(cx + 38), (int16_t)(cy + 30), BLACK);
            mtb_Panel_Fill_Triangle(cx, (int16_t)(cy - 26), (int16_t)(cx - 31), (int16_t)(cy + 26), (int16_t)(cx + 31), (int16_t)(cy + 26), C_YELLOW);
            mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(cy - 10), (int16_t)(cx + 3), (int16_t)(cy + 10), BLACK);
            mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(cy + 14), (int16_t)(cx + 3), (int16_t)(cy + 20), BLACK);
            break;
        case SIGN_PROHIBITION:          // P001: a red ring with a bar across it
            mtb_Panel_Fill_Circle(cx, cy, 32, C_RED);
            mtb_Panel_Fill_Circle(cx, cy, 24, WHITE);
            for (int k = -4; k <= 4; k++)
                mtb_Panel_Draw_Line((int16_t)(cx - 18 + k), (int16_t)(cy - 18 - k), (int16_t)(cx + 18 + k), (int16_t)(cy + 18 - k), C_RED);
            break;
        default:                        // a safe condition: a green square, a white tick
            mtb_Panel_Fill_Round_Rect((int16_t)(cx - 30), (int16_t)(cy - 30), (int16_t)(cx + 30), (int16_t)(cy + 30), 6, C_SIGN_GREEN);
            for (int k = -3; k <= 3; k++){
                mtb_Panel_Draw_Line((int16_t)(cx - 18), (int16_t)(cy + k), (int16_t)(cx - 6), (int16_t)(cy + 12 + k), WHITE);
                mtb_Panel_Draw_Line((int16_t)(cx - 6), (int16_t)(cy + 12 + k), (int16_t)(cx + 18), (int16_t)(cy - 14 + k), WHITE);
            }
            break;
    }
}

static void draw_Reminder(const Hse_Reminder_t& r){
    static const char* const KIND[] = {"MUST DO", "WARNING", "DO NOT", "SAFE PRACTICE"};
    static const uint16_t KIND_C[] = {MTB_KIT_RGB(90, 150, 255), MTB_KIT_RGB(255, 205, 0), MTB_KIT_RGB(255, 80, 80), MTB_KIT_RGB(60, 210, 110)};
    draw_Sign(r.sign, 44, 70);
    mtb_Kit_Text(90, 14, Terminal6x8, KIND_C[r.sign], KIND[r.sign], C_BG);
    fitted_Text(90, 24, W - 94, 100, r.text, 4, KIT_TEXT, false);
}

static void draw_Message(const Hse_Save_t& s){
    mtb_Panel_Fill_Triangle(10, 12, 3, 24, 17, 24, C_YELLOW);
    mtb_Panel_Fill_Rect(9, 16, 10, 20, BLACK);
    mtb_Kit_Text(22, 15, Terminal6x8, C_YELLOW, "SAFETY MESSAGE OF THE WEEK", C_BG);
    char text[MSG_LEN];
    snprintf(text, sizeof(text), "%s", s.message);
    mtb_Kit_Upper(text);
    fitted_Text(6, 28, W - 12, 98, text, 4, KIT_TEXT, true);
}

/** A milestone: the days in gold, sparkles about them. */
static void draw_Milestone(int32_t days, uint32_t ms){
    char line[24];
    snprintf(line, sizeof(line), "%ld DAYS", (long)days);
    for (int k = 0; k < 40; k++){
        uint32_t h = (uint32_t)k * 2654435761u + (ms / 120) * 40503u;
        const int16_t x = (int16_t)(h % W), y = (int16_t)(12 + (h >> 8) % (H - 12));
        const uint16_t c = (h >> 16) % 3 == 0 ? KIT_GOLD : (h >> 16) % 3 == 1 ? C_SAFE : WHITE;
        mtb_Panel_Draw_PixelRGB565(x, y, mtb_Kit_Dim(c, (uint8_t)(80 + (h >> 20) % 170)));
    }
    mtb_Kit_Text_Centre(W / 2, 20, PT_Sans_Narrow_Bold28x34, KIT_GOLD, line, BLACK);
    mtb_Kit_Text_Centre(W / 2, 62, Terminal8x12, KIT_TEXT, "WITHOUT A LOST-TIME INJURY", BLACK);
    mtb_Kit_Text_Centre(W / 2, 86, Terminal10x17, C_SAFE, "WELL DONE, TEAM!", BLACK);
}

static void milestone_Chime(void){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t CHIME[] = {{523, 160}, {659, 160}, {784, 160}, {1047, 420}};
    snd_Play(CHIME, 4, 0.35f, false);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void hseBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Event, cmd_Undo, cmd_Text, cmd_Sector, cmd_Hours, cmd_Rate_Base, cmd_Seconds,
                                       cmd_Option, cmd_Last_Lti, cmd_Figure, cmd_Read, cmd_Minutes);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    steps = 0; phoneChanged = false;
    memset((void*)&sheet, 0, sizeof(sheet));
    sheet.readNow = true;
    sheet.running = true;
    if (xTaskCreatePinnedToCore(worker, "hseNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sheet.running = false;
    appOpen = true;

    Hse_Save_t* s = (Hse_Save_t*)heap_caps_malloc(sizeof(Hse_Save_t), MALLOC_CAP_SPIRAM);
    if (!s){ sheet.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t step = 0, pageSince = millis(), lastDraw = 0, pushedMs = 0;
    uint8_t reminder = 0;
    Page_t page = P_DAYS, shownPage = P_DAYS;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        pxl_time_t t = {};
        int32_t day = 0;
        const bool timeOk = today(t, day);
        portENTER_CRITICAL(&lock);
        if (timeOk && roll_Periods(save, t, day)) saveNow = push = true;
        *s = save;
        portEXIT_CRITICAL(&lock);
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        const int32_t days = timeOk ? days_Without(*s, day) : -1;
        const bool celebrate = days > 0 && milestone(days);
        if (celebrate && s->chimed != day){
            portENTER_CRITICAL(&lock); save.chimed = day; portEXIT_CRITICAL(&lock);
            saveNow = true;
            milestone_Chime();
        }

        // The pages this board has now: the days between each of the others.
        Page_t others[5];
        int n = 0;
        others[n++] = P_STATS;
        if (s->hoursPerDay) others[n++] = P_RATES;
        if (s->message[0]) others[n++] = P_MESSAGE;
        if (s->reminders) others[n++] = P_REMINDER;
        if (celebrate) others[n++] = P_MILESTONE;
        const int knob = steps;
        if (knob){ steps -= knob; step += (uint32_t)knob; pageSince = ms; }
        else if (ms - pageSince >= (uint32_t)s->seconds * 1000u){ step++; pageSince = ms; }
        page = (step % 2 == 0) ? P_DAYS : others[(step / 2) % n];
        if (page != shownPage){
            if (page == P_REMINDER) reminder = (uint8_t)((reminder + 1) % reminder_Count(*s));
            shownPage = page;
            redraw = true;
        }

        if (redraw || ms - lastDraw >= (page == P_MILESTONE ? 100u : 1000u)){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            mtb_Panel_Fill_Screen(page == P_MILESTONE ? BLACK : C_BG);
            if (page != P_MILESTONE) header(*s, timeOk, t);
            if (!timeOk && (page == P_DAYS || page == P_STATS || page == P_RATES)) centred_Note("WAITING FOR THE DATE", "PIXLPAL SETS ITS CLOCK FROM THE INTERNET");
            else switch (page){
                case P_STATS: draw_Stats(*s, timeOk, t); break;
                case P_RATES: draw_Rates(*s, days, t); break;
                case P_MESSAGE: draw_Message(*s); break;
                case P_REMINDER: draw_Reminder(reminder_At(*s, reminder % reminder_Count(*s))); break;
                case P_MILESTONE: draw_Milestone(days, ms); break;
                default: draw_Days(*s, days, day); break;
            }
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(page == P_MILESTONE ? 30 : 60);
    }
    appOpen = false;
    sheet.stop = true;
    for (int i = 0; i < 400 && sheet.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
    mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s));
    heap_caps_free(s);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void hse_Values(JsonObject values){
    Hse_Save_t* s = (Hse_Save_t*)heap_caps_malloc(sizeof(Hse_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    Hse_Sheet_t sh = {};
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; sh.fetch = sheet.fetch; sh.code = sheet.code; sh.fetchedAt = sheet.fetchedAt; portEXIT_CRITICAL(&lock); }
    else read_Save(*s);
    pxl_time_t t = {};
    int32_t day = 0;
    const bool timeOk = today(t, day);
    if (timeOk) roll_Periods(*s, t, day);           // a copy: what the board will show, not saved from here
    const int32_t days = timeOk ? days_Without(*s, day) : -1;
    char text[96], a[24];

    values["site"] = s->site;
    values["message"] = s->message;
    values["url"] = s->url;
    values["sector"] = s->sector;
    JsonArray sectors = values["sectors"].to<JsonArray>();
    for (unsigned i = 0; i < HSE_SECTOR_COUNT; i++) if (s->sectors >> i & 1) sectors.add(HSE_SECTORS[i].name);
    values["hours"] = s->hoursPerDay;
    values["rateBase"] = s->rateBase;
    values["seconds"] = s->seconds;
    values["reminders"] = s->reminders;
    values["sound"] = s->sound;
    values["minutes"] = s->minutes;
    values["record"] = s->record;
    if (s->lastLti){ int y, m, d; civil_From_Days(s->lastLti, y, m, d); snprintf(text, sizeof(text), "%04d-%02d-%02d", y, m, d); values["lastLti"] = text; }
    else values["lastLti"] = "";
    for (int i = 0; i < KINDS; i++){
        char key[8];
        snprintf(key, sizeof(key), "%s_m", KIND_KEY[i]); values[key] = s->m[i];
        snprintf(key, sizeof(key), "%s_y", KIND_KEY[i]); values[key] = s->y[i];
    }

    JsonArray todayRows = values["today"].to<JsonArray>();
    JsonObject r = todayRows.add<JsonObject>();
    r["label"] = "Days without a lost-time injury";
    if (!timeOk){ r["value"] = "Waiting for Pixlpal's clock"; r["state"] = "warn"; }
    else if (days < 0){ r["value"] = "Set the date of the last one (Correct the Figures)"; r["state"] = "warn"; }
    else {
        snprintf(text, sizeof(text), "%ld", (long)days);
        r["value"] = text;
        r["state"] = days == 0 ? "error" : "ok";
        if (s->record) { r["meter"]["value"] = "daysNow"; r["meter"]["max"] = "recordNow"; values["daysNow"] = days; values["recordNow"] = s->record > (uint32_t)days ? s->record : (uint32_t)days; }
    }
    r = todayRows.add<JsonObject>();
    r["label"] = "Best record";
    snprintf(text, sizeof(text), "%lu days%s", (unsigned long)s->record, days > 0 && (uint32_t)days >= s->record ? " (now)" : "");
    r["value"] = text;
    r["state"] = "info";
    r = todayRows.add<JsonObject>();
    r["label"] = "Figures from";
    if (!s->url[0]){ r["value"] = "Recorded here"; r["state"] = "info"; }
    else if (!appOpen){ r["value"] = "The sheet, read while HSE Board is open"; r["state"] = "off"; }
    else if (sh.fetch == F_OK){ r["value"] = "The sheet"; r["state"] = "ok"; if (sh.fetchedAt){ values["fetchedAt"] = sh.fetchedAt; r["ago"] = "fetchedAt"; } }
    else if (sh.fetch == F_NOT_ALLOWED){ r["value"] = "Allow the sheet's site when the phone app asks"; r["state"] = "warn"; }
    else if (sh.fetch == F_EMPTY){ r["value"] = "No names and values the board knows in the sheet"; r["state"] = "error"; }
    else if (sh.fetch == F_NONE){ r["value"] = "Reading the sheet"; r["state"] = "info"; }
    else { snprintf(text, sizeof(text), "The sheet can't be read (%d)", sh.code); r["value"] = text; r["state"] = "error"; }

    JsonArray counts = values["counts"].to<JsonArray>();
    for (int i = 0; i < KINDS; i++){
        JsonObject c = counts.add<JsonObject>();
        c["label"] = KIND_PHONE[i];
        snprintf(text, sizeof(text), "%u this month, %u this year", s->m[i], s->y[i]);
        c["value"] = text;
        c["state"] = i == 0 ? (s->y[0] ? "error" : "ok") : i == 1 ? (s->y[1] ? "warn" : "ok") : "info";
    }

    JsonArray rateRows = values["rates"].to<JsonArray>();
    double hoursYear, ltifr, trifr;
    if (timeOk && rates(*s, t, hoursYear, ltifr, trifr)){
        JsonObject h = rateRows.add<JsonObject>();
        h["label"] = "Man-hours without a lost-time injury";
        if (days >= 0){ fmt_Count(a, sizeof(a), (double)days * s->hoursPerDay, false); h["value"] = a; } else h["value"] = "Set the date of the last one";
        h["state"] = "info";
        JsonObject l = rateRows.add<JsonObject>();
        snprintf(text, sizeof(text), "LTIFR this year (per %s hours)", s->rateBase ? "200,000" : "1,000,000");
        l["label"] = text;
        snprintf(a, sizeof(a), "%.2f", ltifr);
        l["value"] = a;
        l["state"] = s->y[0] ? "error" : "ok";
        JsonObject tr = rateRows.add<JsonObject>();
        snprintf(text, sizeof(text), "TRIFR this year (per %s hours)", s->rateBase ? "200,000" : "1,000,000");
        tr["label"] = text;
        snprintf(a, sizeof(a), "%.2f", trifr);
        tr["value"] = a;
        tr["state"] = (s->y[0] + s->y[1]) ? "warn" : "ok";
    }
    heap_caps_free(s);
}
