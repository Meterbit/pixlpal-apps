/**
 * Branch Rates: a bank branch's rates board. Pages in turn: the exchange rates (each currency's code, what the bank buys
 * it at and sells it at, four to a page) and the savings and deposit rates; the bank's name and the clock along the
 * top, when the rates were last changed, and a note along the bottom ("Rates may change without notice"), scrolling
 * if it is long.
 *
 * The rates are typed on the phone, or read from a JSON link the bank's own system keeps up to date (every 10
 * minutes; what is read replaces what was typed, and is kept for when the link doesn't answer). The knob: turn to go
 * through the pages, click to read the link now. NVS "brtSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include "brt_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define FX          8
#define DEP         4
#define PER_PAGE    4
#define URL_LEN     256
#define BODY_MAX    16384
#define NVS_KEY     "brtSave"
#define SAVE_MAGIC  0x42525401u
#define UA          "Pixlpal-BranchRates/1.0"
#define READ_EVERY  (10u * 60000u)

EXT_RAM_BSS_ATTR TaskHandle_t branchRates_Task_H = NULL;
static void branchRates_App_Task(void* dApplication);
static void brt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* branchRates_App;
Mtb_Applications* branchRates_App_GetInstance(){
    if (!branchRates_App){
        branchRates_App = new Mtb_Applications_FullScreen(branchRates_App_Task, &branchRates_Task_H, "Branch Rates", {0,0}, 8192);
        branchRates_App->mtb_App_Set_Mobile_UI(BRT_UI_MANIFEST, BRT_UI_API, nullptr, brt_Values);
    }
    return branchRates_App;
}
MTB_REGISTER_APP(branchRates_App, 0, 0)       // the catalogue gives the app its number

struct Brt_Fx_t  { char code[4]; char buy[12]; char sell[12]; };
struct Brt_Dep_t { char name[28]; char rate[10]; };

struct Brt_Save_t {
    uint32_t magic;
    char bank[36];
    char base[4];
    Brt_Fx_t fx[FX];
    uint8_t fxN;
    Brt_Dep_t dep[DEP];
    uint8_t depN;
    char note[96];
    char url[URL_LEN];
    uint8_t seconds;
    uint32_t updated;                   // when the rates last changed (Unix seconds; 0 not known)
};

enum : uint8_t { F_NONE, F_OK, F_NO_ANSWER, F_HTTP, F_NOT_JSON, F_NOT_ALLOWED };
struct Brt_Shared_t { volatile bool readNow, stop, running; uint8_t fetch; int code; uint32_t readAt; };

static EXT_RAM_BSS_ATTR Brt_Save_t save;
static Brt_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile uint32_t ratesVersion = 0;

static void read_Save(Brt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.seconds = 12;
        snprintf(s.note, sizeof(s.note), "Rates are indicative and may change without notice.");
    }
    if (s.fxN > FX) s.fxN = 0;
    if (s.depN > DEP) s.depN = 0;
    if (s.seconds < 5 || s.seconds > 60) s.seconds = 12;
    s.bank[sizeof(s.bank) - 1] = s.base[sizeof(s.base) - 1] = s.note[sizeof(s.note) - 1] = s.url[URL_LEN - 1] = '\0';
    for (int i = 0; i < FX; i++){ s.fx[i].code[3] = s.fx[i].buy[11] = s.fx[i].sell[11] = '\0'; }
    for (int i = 0; i < DEP; i++){ s.dep[i].name[sizeof(s.dep[i].name) - 1] = s.dep[i].rate[sizeof(s.dep[i].rate) - 1] = '\0'; }
}

static void trim(char*& p){
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = '\0';
}

/** A currency code: three letters. */
static bool code_Ok(const char* c, size_t n){
    if (n != 3) return false;
    for (size_t i = 0; i < n; i++) if (!isalpha((unsigned char)c[i])) return false;
    return true;
}

/** A rate as written ("1,520.00", "0.0123"): digits, commas and one point, at most 11 characters. */
static bool amount_Ok(const char* a){
    const size_t n = strlen(a);
    if (!n || n > 11) return false;
    int points = 0, digits = 0;
    for (size_t i = 0; i < n; i++){
        if (isdigit((unsigned char)a[i])) digits++;
        else if (a[i] == '.') points++;
        else if (a[i] != ',') return false;
    }
    return digits && points <= 1;
}

/** "5.5%", "5.5", "12 %": the rate with its % sign; false if it isn't one. */
static bool rate_Text(const char* r, char* out, size_t n){
    char t[16]; size_t k = 0;
    for (const char* p = r; *p && k < sizeof(t) - 1; p++) if (*p != ' ' && *p != '%') t[k++] = *p;
    t[k] = '\0';
    if (!amount_Ok(t) || strlen(t) > 7) return false;
    snprintf(out, n, "%s%%", t);
    return true;
}

static void changed(void){ save.updated = (uint32_t)time(nullptr); ratesVersion++; }

/** A rate the link gave as a number: as many decimals as a board shows for it. */
static void number_Text(double v, char* out, size_t n){
    if (v >= 1000) snprintf(out, n, "%.2f", v);
    else if (v >= 1) snprintf(out, n, "%.2f", v);
    else snprintf(out, n, "%.4f", v);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Bank(JsonDocument& c){
    const char* v = c["bank"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.bank, sizeof(save.bank), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Base(JsonDocument& c){
    const char* v = c["base"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[16]; snprintf(t, sizeof(t), "%s", v);
    char* p = t; trim(p);
    if (*p && !code_Ok(p, strlen(p))){ mtb_Ble_App_Cmd_Reject(); return; }
    for (char* k = p; *k; k++) *k = (char)toupper((unsigned char)*k);
    portENTER_CRITICAL(&lock); snprintf(save.base, sizeof(save.base), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Fx(JsonDocument& c){
    for (int i = 0; i < FX; i++){
        char key[8]; snprintf(key, sizeof(key), "fx%d", i);
        if (c[key].isNull()) continue;
        char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), c[key] | "");
        char* p = t; trim(p);
        Brt_Fx_t f = {};
        if (*p){
            char code[8] = "", buy[16] = "", sell[16] = "", more[4] = "";
            if (sscanf(p, "%7s %15s %15s %3s", code, buy, sell, more) != 3 || !code_Ok(code, strlen(code)) || !amount_Ok(buy) || !amount_Ok(sell)){ mtb_Ble_App_Cmd_Reject(); return; }
            for (int k = 0; k < 3; k++) f.code[k] = (char)toupper((unsigned char)code[k]);
            snprintf(f.buy, sizeof(f.buy), "%s", buy); snprintf(f.sell, sizeof(f.sell), "%s", sell);
        }
        portENTER_CRITICAL(&lock);
        if (!*p){
            if (i < save.fxN){ memmove(&save.fx[i], &save.fx[i + 1], sizeof(save.fx[0]) * (FX - 1 - i)); memset(&save.fx[FX - 1], 0, sizeof(save.fx[0])); save.fxN--; }
        } else {
            const int at = i < save.fxN ? i : save.fxN;
            save.fx[at] = f;
            if (at >= save.fxN) save.fxN++;
        }
        changed();
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Dep(JsonDocument& c){
    for (int i = 0; i < DEP; i++){
        char key[8]; snprintf(key, sizeof(key), "dep%d", i);
        if (c[key].isNull()) continue;
        char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), c[key] | "");
        char* p = t; trim(p);
        Brt_Dep_t d = {};
        if (*p){
            size_t n = strlen(p);                                               // the rate is the last word ("5.5%"; "12 %" too)
            if (n && p[n - 1] == '%'){ n--; while (n && p[n - 1] == ' ') n--; }
            size_t e = n;
            while (e && p[e - 1] != ' ') e--;
            char num[16]; snprintf(num, sizeof(num), "%.*s", (int)(n - e), p + e);
            if (!e || !rate_Text(num, d.rate, sizeof(d.rate))){ mtb_Ble_App_Cmd_Reject(); return; }
            p[e] = '\0';
            trim(p);
            if (!*p){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(d.name, sizeof(d.name), "%s", p);
        }
        portENTER_CRITICAL(&lock);
        if (!*p){
            if (i < save.depN){ memmove(&save.dep[i], &save.dep[i + 1], sizeof(save.dep[0]) * (DEP - 1 - i)); memset(&save.dep[DEP - 1], 0, sizeof(save.dep[0])); save.depN--; }
        } else {
            const int at = i < save.depN ? i : save.depN;
            save.dep[at] = d;
            if (at >= save.depN) save.depN++;
        }
        changed();
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Note(JsonDocument& c){
    const char* v = c["note"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[128]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.note, sizeof(save.note), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Url(JsonDocument& c){
    const char* v = c["url"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    char url[URL_LEN] = "";
    if (n){
        const bool ok = !strncasecmp(v, "https://", 8) || !strncasecmp(v, "http://", 7);
        if (!ok || n >= URL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
        snprintf(url, sizeof(url), "%.*s", (int)n, v);
        const char* host = strstr(url, "://") + 3;
        if (!*host || *host == '/' || !strchr(host, '.')){ mtb_Ble_App_Cmd_Reject(); return; }
        for (const char* k = url; *k; k++) if ((unsigned char)*k <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
    }
    portENTER_CRITICAL(&lock);
    snprintf(save.url, sizeof(save.url), "%s", url);
    sh.fetch = F_NONE; sh.readAt = 0;
    portEXIT_CRITICAL(&lock);
    sh.readNow = true;
    phoneChanged = true;
}
static void cmd_Read(JsonDocument&){ if (!appOpen || !save.url[0]){ mtb_Ble_App_Cmd_Reject(); return; } sh.readNow = true; }
static void cmd_Seconds(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seconds"], 5, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.seconds = (uint8_t)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The link ------------------------------------------------------------------------------------------------------------------------
/** A rate the link gave: a number, or text as a board writes it. */
static bool link_Amount(JsonVariantConst v, char* out, size_t n){
    if (v.is<double>() || v.is<long>()){ const double d = v.as<double>(); if (!(d > 0) || d >= 1e9) return false; number_Text(d, out, n); return true; }
    const char* s = v.as<const char*>();
    if (!s) return false;
    char t[16]; snprintf(t, sizeof(t), "%s", s);
    char* p = t; trim(p);
    if (!amount_Ok(p)) return false;
    snprintf(out, n, "%s", p);
    return true;
}

/** Reads the link's answer into @p s (its rates; the base and note if given). False if it isn't the expected JSON. */
static bool link_Rates(const char* body, size_t length, Brt_Save_t& s){
    JsonDocument doc;
    if (deserializeJson(doc, body, length) != DeserializationError::Ok || !doc.is<JsonObject>()) return false;
    JsonArrayConst fx = doc["fx"], dep = doc["deposit"];
    if (fx.isNull() && dep.isNull()) return false;
    if (!fx.isNull()){
        s.fxN = 0;
        for (JsonObjectConst f : fx){
            if (s.fxN >= FX) break;
            const char* code = f["code"] | "";
            Brt_Fx_t x = {};
            if (!code_Ok(code, strlen(code)) || !link_Amount(f["buy"], x.buy, sizeof(x.buy)) || !link_Amount(f["sell"], x.sell, sizeof(x.sell))) continue;
            for (int k = 0; k < 3; k++) x.code[k] = (char)toupper((unsigned char)code[k]);
            s.fx[s.fxN++] = x;
        }
    }
    if (!dep.isNull()){
        s.depN = 0;
        for (JsonObjectConst d : dep){
            if (s.depN >= DEP) break;
            Brt_Dep_t x = {};
            char name[64]; mtb_Kit_To_Ascii(name, sizeof(name), d["name"] | "");
            char* p = name; trim(p);
            char r[16];
            JsonVariantConst rv = d["rate"];
            if (rv.is<double>() || rv.is<long>()) snprintf(r, sizeof(r), "%g", rv.as<double>()); else snprintf(r, sizeof(r), "%s", rv | "");
            if (!*p || !rate_Text(r, x.rate, sizeof(x.rate))) continue;
            snprintf(x.name, sizeof(x.name), "%s", p);
            s.dep[s.depN++] = x;
        }
    }
    const char* base = doc["base"] | (const char*)nullptr;
    if (base && code_Ok(base, strlen(base))){ for (int k = 0; k < 3; k++) s.base[k] = (char)toupper((unsigned char)base[k]); s.base[3] = '\0'; }
    const char* note = doc["note"] | (const char*)nullptr;
    if (note){ char t[128]; mtb_Kit_To_Ascii(t, sizeof(t), note); char* p = t; trim(p); snprintf(s.note, sizeof(s.note), "%s", p); }
    return true;
}

static void worker(void*){
    uint32_t nextRead = 0;
    Brt_Save_t* s = (Brt_Save_t*)heap_caps_malloc(sizeof(Brt_Save_t), MALLOC_CAP_SPIRAM);
    while (s && !sh.stop){
        if (!sh.readNow && (int32_t)(millis() - nextRead) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.readNow = false;
        nextRead = millis() + READ_EVERY;
        portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
        if (!s->url[0]) continue;
        char url[URL_LEN]; snprintf(url, sizeof(url), "%s", s->url);
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, BODY_MAX, r, UA);
        if (sh.stop){ heap_caps_free(body); break; }
        uint8_t fetch;
        if (r.code == PXL_HTTP_NOT_ALLOWED) fetch = F_NOT_ALLOWED;
        else if (r.code <= 0) fetch = F_NO_ANSWER;
        else if (r.code < 200 || r.code >= 300) fetch = F_HTTP;
        else fetch = body && link_Rates(body, r.length, *s) ? F_OK : F_NOT_JSON;
        heap_caps_free(body);
        portENTER_CRITICAL(&lock);
        if (!strcmp(url, save.url)){                                            // not changed meanwhile
            sh.fetch = fetch; sh.code = r.code;
            if (fetch == F_OK){
                const bool same = s->fxN == save.fxN && s->depN == save.depN && !memcmp(s->fx, save.fx, sizeof(save.fx)) && !memcmp(s->dep, save.dep, sizeof(save.dep));
                memcpy(save.fx, s->fx, sizeof(save.fx)); save.fxN = s->fxN;
                memcpy(save.dep, s->dep, sizeof(save.dep)); save.depN = s->depN;
                memcpy(save.base, s->base, sizeof(save.base));
                memcpy(save.note, s->note, sizeof(save.note));
                if (!same) changed();
                sh.readAt = (uint32_t)time(nullptr);
            }
        }
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        if (fetch != F_OK && fetch != F_NOT_ALLOWED) nextRead = millis() + 120000u;    // try again sooner
    }
    heap_caps_free(s);
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 14, 22), C_HEAD = MTB_KIT_RGB(0, 70, 70), C_ROW = MTB_KIT_RGB(10, 26, 36), C_ROW2 = MTB_KIT_RGB(6, 20, 30);
static const uint16_t C_TEAL = MTB_KIT_RGB(0, 190, 170), C_BUY = MTB_KIT_RGB(90, 220, 140), C_SELL = MTB_KIT_RGB(255, 196, 60);

/** The pages: the exchange rates four at a time, then the deposit rates. */
static int page_Count(const Brt_Save_t& s){ return (s.fxN + PER_PAGE - 1) / PER_PAGE + (s.depN ? 1 : 0); }

static void updated_Text(uint32_t at, char* t, size_t n){
    if (at < 1700000000u){ t[0] = '\0'; return; }
    time_t x = (time_t)at, now = time(nullptr);
    struct tm a, b; localtime_r(&x, &a); localtime_r(&now, &b);
    static const char* const M[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) snprintf(t, n, "UPDATED %02d:%02d", a.tm_hour, a.tm_min);
    else snprintf(t, n, "UPDATED %d %s", a.tm_mday, M[a.tm_mon]);
}

/** Text that may start left of the screen: the kit starts such text at the edge, so the characters already off it go. */
static void text_From(int x, int16_t y, const uint8_t* font, uint16_t color, const char* t, uint16_t bg){
    while (*t && x < 0){ x += mtb_Kit_Text_Width(font, t) - mtb_Kit_Text_Width(font, t + 1); t++; }
    if (*t && x < W) mtb_Kit_Text((int16_t)x, y, font, color, t, bg);
}

static void draw(const Brt_Save_t& s, int page, uint32_t ms, bool stale){
    mtb_Panel_Fill_Screen(C_BG);
    char t[128], fit[128];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.bank[0] ? s.bank : "TODAY'S RATES");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 46);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, fit, C_HEAD);
    pxl_time_t pt;
    if (pxl_time(&pt)){ snprintf(t, sizeof(t), "%02d:%02d", (int)pt.hour, (int)pt.minute); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD); }
    const int pages = page_Count(s);
    if (!pages){
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, "NO RATES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, "TYPE THEM IN THE PHONE APP: USD 1520.00 1560.00", C_BG);
        return;
    }
    page = ((page % pages) + pages) % pages;
    const int fxPages = (s.fxN + PER_PAGE - 1) / PER_PAGE;
    // The title, when the rates changed, and the page dots.
    const bool fxPage = page < fxPages;
    mtb_Kit_Text(4, 14, Terminal8x12, C_TEAL, fxPage ? "EXCHANGE RATES" : "SAVINGS & DEPOSITS", C_BG);
    updated_Text(s.updated, t, sizeof(t));
    if (t[0]) mtb_Kit_Text_Right(W - 4, 14, Terminal4x6, stale ? KIT_ORANGE : KIT_LABEL, t, C_BG);
    if (pages > 1) for (int i = 0; i < pages; i++){
        const int x = W - 4 - (pages - 1 - i) * 6 - 3;
        mtb_Panel_Fill_Rect((int16_t)x, 22, (int16_t)(x + 2), 24, i == page ? C_TEAL : KIT_DIM);
    }
    const int rowTop = 38, rowH = 19;
    if (fxPage){
        mtb_Kit_Text(4, 29, Terminal4x6, KIT_LABEL, "CURRENCY", C_BG);
        snprintf(t, sizeof(t), s.base[0] ? "WE BUY (%s)" : "WE BUY", s.base);
        mtb_Kit_Text_Right(160, 29, Terminal4x6, C_BUY, t, C_BG);
        snprintf(t, sizeof(t), s.base[0] ? "WE SELL (%s)" : "WE SELL", s.base);
        mtb_Kit_Text_Right(W - 6, 29, Terminal4x6, C_SELL, t, C_BG);
        for (int k = 0; k < PER_PAGE; k++){
            const int i = page * PER_PAGE + k;
            if (i >= s.fxN) break;
            const Brt_Fx_t& f = s.fx[i];
            const int y = rowTop + k * (rowH + 1);
            const uint16_t bg = k % 2 ? C_ROW2 : C_ROW;
            mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 1), bg);
            mtb_Panel_Fill_Rect(4, (int16_t)(y + 3), 36, (int16_t)(y + rowH - 4), C_TEAL);
            mtb_Kit_Text_Centre(20, (int16_t)(y + rowH / 2 - 4), Terminal6x8, BLACK, f.code, C_TEAL);
            mtb_Kit_Text_Right(160, (int16_t)(y + rowH / 2 - 6), Terminal8x12, C_BUY, f.buy, bg);
            mtb_Kit_Text_Right(W - 6, (int16_t)(y + rowH / 2 - 6), Terminal8x12, C_SELL, f.sell, bg);
        }
    } else {
        mtb_Kit_Text(4, 29, Terminal4x6, KIT_LABEL, "PRODUCT", C_BG);
        mtb_Kit_Text_Right(W - 6, 29, Terminal4x6, C_SELL, "RATE A YEAR", C_BG);
        for (int i = 0; i < s.depN; i++){
            const Brt_Dep_t& d = s.dep[i];
            const int y = rowTop + i * (rowH + 1);
            const uint16_t bg = i % 2 ? C_ROW2 : C_ROW;
            mtb_Panel_Fill_Rect(0, (int16_t)y, W - 1, (int16_t)(y + rowH - 1), bg);
            mtb_Panel_Fill_Rect(0, (int16_t)y, 2, (int16_t)(y + rowH - 1), C_TEAL);
            snprintf(t, sizeof(t), "%s", d.name); mtb_Kit_Upper(t);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, 170);
            mtb_Kit_Text(8, (int16_t)(y + rowH / 2 - 4), Terminal6x8, KIT_TEXT, fit, bg);
            mtb_Kit_Text_Right(W - 6, (int16_t)(y + rowH / 2 - 6), Terminal8x12, C_SELL, d.rate, bg);
        }
    }
    // The note: still if it fits, else scrolling.
    if (s.note[0]){
        mtb_Panel_Fill_Rect(0, H - 9, W - 1, H - 1, C_HEAD);
        snprintf(t, sizeof(t), "%s", s.note); mtb_Kit_Upper(t);
        const int tw = mtb_Kit_Text_Width(Terminal4x6, t);
        if (tw <= W - 8) mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_TEXT, t, C_HEAD);
        else {
            const int span = tw + 60;
            const int x = W - (int)((ms / 40) % (uint32_t)(span + W));
            text_From(x, H - 7, Terminal4x6, KIT_TEXT, t, C_HEAD);
            if (x + span < W) text_From(x + span, H - 7, Terminal4x6, KIT_TEXT, t, C_HEAD);
        }
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void branchRates_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Bank, cmd_Base, cmd_Fx, cmd_Dep, cmd_Note, cmd_Url, cmd_Read, cmd_Seconds);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; knobClick = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.readNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "brtNet", 10240, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Brt_Save_t* s = (Brt_Save_t*)heap_caps_malloc(sizeof(Brt_Save_t), MALLOC_CAP_SPIRAM);
    if (!s){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t lastDraw = 0, pushedMs = 0, pageAt = millis(), handUntil = 0, seenVersion = ratesVersion;
    int page = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        if (ratesVersion != seenVersion){ seenVersion = ratesVersion; saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; page += t; pageAt = ms; handUntil = ms + 30000; lastDraw = 0; }
        if (knobClick){ knobClick = false; sh.readNow = true; }
        portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
        if ((int32_t)(handUntil - ms) <= 0 && ms - pageAt >= s->seconds * 1000u){ pageAt = ms; page++; lastDraw = 0; }
        const int pages = page_Count(*s);
        if (pages) page = ((page % pages) + pages) % pages;
        const bool scroll = s->note[0] && mtb_Kit_Text_Width(Terminal4x6, s->note) > W - 8;
        if (ms - lastDraw >= (scroll ? 40u : 1000u)){
            lastDraw = ms;
            const bool stale = s->url[0] && sh.fetch != F_OK && sh.fetch != F_NONE;
            mtb_Panel_Begin_Update();
            draw(*s, page, ms, stale);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
    mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s));
    heap_caps_free(s);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void brt_Values(JsonObject values){
    Brt_Save_t* s = (Brt_Save_t*)heap_caps_malloc(sizeof(Brt_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    Brt_Shared_t d = {};
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; d.fetch = sh.fetch; d.code = sh.code; d.readAt = sh.readAt; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    char t[96], key[8];
    values["bank"] = s->bank;
    values["base"] = s->base;
    for (int i = 0; i < FX; i++){
        snprintf(key, sizeof(key), "fx%d", i);
        if (i < s->fxN){ snprintf(t, sizeof(t), "%s %s %s", s->fx[i].code, s->fx[i].buy, s->fx[i].sell); values[key] = t; } else values[key] = "";
    }
    for (int i = 0; i < DEP; i++){
        snprintf(key, sizeof(key), "dep%d", i);
        if (i < s->depN){ snprintf(t, sizeof(t), "%s %s", s->dep[i].name, s->dep[i].rate); values[key] = t; } else values[key] = "";
    }
    values["note"] = s->note;
    values["url_set"] = s->url[0] ? 1 : 0;
    values["seconds"] = s->seconds;
    JsonArray rows = values["rates"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = "On the board";
    snprintf(t, sizeof(t), "%u currencies, %u deposit rates", (unsigned)s->fxN, (unsigned)s->depN);
    r["value"] = t; r["state"] = s->fxN || s->depN ? "ok" : "warn";
    char u[32]; updated_Text(s->updated, u, sizeof(u));
    if (u[0]){ r = rows.add<JsonObject>(); r["label"] = "Rates"; snprintf(t, sizeof(t), "%c%s", u[0], u + 1); for (char* k = t + 1; *k; k++) *k = (char)tolower((unsigned char)*k); r["value"] = t; r["state"] = "info"; }
    if (s->url[0]){
        r = rows.add<JsonObject>();
        r["label"] = "The link";
        if (!appOpen){ r["value"] = "Read while Branch Rates is open"; r["state"] = "off"; }
        else if (d.fetch == F_OK){ r["value"] = "Read: the board shows its rates"; r["state"] = "ok"; }
        else if (d.fetch == F_NOT_ALLOWED){ r["value"] = "Allow the link's site when the phone app asks"; r["state"] = "warn"; }
        else if (d.fetch == F_HTTP){ snprintf(t, sizeof(t), "It answered %d: check the link", d.code); r["value"] = t; r["state"] = "error"; }
        else if (d.fetch == F_NO_ANSWER){ r["value"] = "It didn't answer: showing the last rates"; r["state"] = "error"; }
        else if (d.fetch == F_NOT_JSON){ r["value"] = "Its answer isn't the rates JSON (see below)"; r["state"] = "error"; }
        else { r["value"] = "Reading it"; r["state"] = "info"; }
    }
    heap_caps_free(s);
}
