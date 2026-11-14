/**
 * Kobo Terminal, ID {4,7} (FINANCE): the Nigerian Exchange and fixed income from Kobo Terminal (koboterminal.com) with
 * the user's own API key: the All-Share Index and breadth, a heatmap of every stock, gainers and losers, a watchlist,
 * one stock with its fundamentals (Starter plan), the NGX indices, the CBN's T-bill and bond auctions (stop rates), and
 * the bonds on NGX. The plan is found by trying a Starter call and a Professional one once a day; calls are counted
 * against the day's allowance. Prices are 30 minutes delayed.
 *
 * The knob: click or turn, the next page (back by turning left); two clicks, ask again now; hold, pages turn by
 * themselves or stay.
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "kbt_App.h"
#include "ngx_Ui.h"
#include "kbt_Mobile_Ui.h"
#include <time.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

EXT_RAM_BSS_ATTR TaskHandle_t koboTerminal_Task_H = NULL;
static void koboTerminal_App_Task(void* dApplication);
static void kbt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *koboTerminal_App;
Mtb_Applications* koboTerminal_App_GetInstance() {
    if (!koboTerminal_App){
        koboTerminal_App = new Mtb_Applications_StatusBar(koboTerminal_App_Task, &koboTerminal_Task_H, "Kobo Terminal", {4,7}, 8192);
        koboTerminal_App->mtb_App_Set_Mobile_UI(KBT_UI_MANIFEST, KBT_UI_API, nullptr, kbt_Values);
    }
    return koboTerminal_App;
}
MTB_REGISTER_APP(koboTerminal_App, 4, 7)

EXT_RAM_BSS_ATTR static Kbt_Settings_t settings;
EXT_RAM_BSS_ATTR static Kbt_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static bool read_Settings(Kbt_Settings_t& s){
    kbt_Settings_Default(s);
    Kbt_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(KBT_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == KBT_SETTINGS_MAGIC;
    if (ok) s = saved;
    memset(&saved, 0, sizeof(saved));
    kbt_Settings_Clamp(s);
    return ok;
}

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Kbt_Settings_t* begin_Change(void){
    Kbt_Settings_t* next = (Kbt_Settings_t*)heap_caps_malloc(sizeof(Kbt_Settings_t), MALLOC_CAP_SPIRAM);
    if (!next) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}
static void end_Change(Kbt_Settings_t* next){
    kbt_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }

// {"stocks": [{name, value: "DANGCEM"}, ...]}
static void cmd_Watch(JsonDocument& c){
    if (!c["stocks"].is<JsonArray>()){ mtb_Ble_App_Cmd_Reject(); return; }
    Kbt_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    int count = 0;
    for (JsonVariant v : c["stocks"].as<JsonArray>()){
        char sym[16];
        if (count >= NGX_WATCH_MAX || !ngx_Symbol(v["value"] | (v.is<const char*>() ? v.as<const char*>() : ""), sym, sizeof(sym))){ count = -1; break; }
        bool dup = false;
        for (int i = 0; i < count; i++) if (!strcmp(n->watch[i], sym)) dup = true;
        if (!dup) memcpy(n->watch[count++], sym, sizeof(sym));
    }
    if (count < 0){ memset(n, 0, sizeof(*n)); heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); return; }
    n->watchCount = (uint8_t)count;
    end_Change(n);
}

// {"stock": {name, value}}
static void cmd_Focus(JsonDocument& c){
    char sym[16];
    if (!ngx_Symbol(c["stock"]["value"] | "", sym, sizeof(sym))){ mtb_Ble_App_Cmd_Reject(); return; }
    Kbt_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    memcpy(n->focus, sym, sizeof(sym));
    end_Change(n);
}

// {"query"}: the companies Kobo Terminal listed, by symbol, then by name.
static void search(JsonDocument& c, const char* id){
    const char* q = c["query"] | "";
    if (strlen(q) > 40){ mtb_Ble_App_Cmd_Reject(); return; }
    Kbt_Data_t* d = (Kbt_Data_t*)heap_caps_calloc(1, sizeof(Kbt_Data_t), MALLOC_CAP_SPIRAM);
    Ngx_Stock_t* stocks = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
    if (!d || !stocks){ heap_caps_free(d); heap_caps_free(stocks); mtb_Ble_App_Cmd_Reject(); return; }
    d->board.stocks = stocks;
    uint32_t version = 0xFFFFFFFFu;
    kbt_Net_Copy(*d, version);
    JsonDocument out;
    JsonArray results = out.to<JsonArray>();
    const size_t n = strlen(q);
    for (int pass = 0; pass < 2; pass++){
        for (int i = 0; i < d->board.count && results.size() < 15; i++){
            const Ngx_Stock_t& s = stocks[i];
            const bool bySymbol = !strncasecmp(s.symbol, q, n);
            bool byName = false;
            for (const char* p = s.name; *p && n; p++) if ((p == s.name || p[-1] == ' ') && !strncasecmp(p, q, n)){ byName = true; break; }
            if (pass == 0 ? !bySymbol : (bySymbol || !byName)) continue;
            JsonObject o = results.add<JsonObject>();
            o["name"] = s.symbol;
            o["value"] = s.symbol;
            char detail[64], price[20];
            ngx_Naira(price, sizeof(price), s.price, false);
            snprintf(detail, sizeof(detail), "%s, %s", s.name, price);
            o["detail"] = detail;
        }
    }
    const bool none = d->board.count == 0;
    heap_caps_free(stocks);
    heap_caps_free(d);
    if (koboTerminal_App) koboTerminal_App->mtb_App_Send_Search_Results(id, q, results, none ? "Open Kobo Terminal on Pixlpal first: it searches the companies it has fetched" : nullptr);
}
static void cmd_Search_Watch(JsonDocument& c){ search(c, "stocks"); }
static void cmd_Search_Focus(JsonDocument& c){ search(c, "focus"); }

static void cmd_Number(JsonDocument& c){
    long v;
    Kbt_Settings_t* n = nullptr;
    char key[4] = "p0";
    for (int i = 0; i < KBT_PAGES; i++){
        key[1] = (char)('0' + i);
        if (c[key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[key], 0, 1, v) || !(n = begin_Change())) break;
        if (v) n->pages = (uint8_t)(n->pages | (1u << i)); else n->pages = (uint8_t)(n->pages & ~(1u << i));
        end_Change(n);
        return;
    }
    if (!c["cycle"].isNull() && mtb_Kit_Json_Range(c["cycle"], 0, 1, v) && (n = begin_Change())){ n->cycle = (uint8_t)v; end_Change(n); return; }
    if (!c["seconds"].isNull() && mtb_Kit_Json_Range(c["seconds"], 5, 120, v) && (n = begin_Change())){ n->seconds = (uint8_t)v; end_Change(n); return; }
    mtb_Ble_App_Cmd_Reject();
}

// {"apiKey"}: kept on Pixlpal, never sent back.
static void cmd_Key(JsonDocument& c){
    const char* k = c["apiKey"] | "";
    while (*k == ' ') k++;
    const size_t len = strlen(k);
    bool ok = len < sizeof(settings.apiKey) && (len == 0 || len >= 16);
    for (size_t i = 0; ok && i < len; i++) if (!isalnum((unsigned char)k[i]) && k[i] != '_' && k[i] != '-') ok = false;
    if (!ok){ mtb_Ble_App_Cmd_Reject(); return; }
    Kbt_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(n->apiKey, 0, sizeof(n->apiKey));
    memcpy(n->apiKey, k, len);
    end_Change(n);
}

// The knob ------------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

static void lagos(uint32_t unix, int& wd, uint16_t& m){
    const uint32_t t = unix + 3600;
    wd = (int)((t / 86400 + 4) % 7);
    m = (uint16_t)(t % 86400 / 60);
}

// The app -------------------------------------------------------------------------------------------------------------------
static bool page_Shown(const Kbt_Settings_t& s, const Kbt_Data_t& d, uint8_t p){
    if (!(s.pages & (1u << p))) return false;
    if (p == KBT_P_TBILLS) return d.tbills.count > 0;
    if (p == KBT_P_BONDS) return d.bonds.count > 0;
    if (p == KBT_P_INDICES) return d.indices.count > 0;
    if (p == KBT_P_WATCH) return s.watchCount > 0;
    return true;
}

static void koboTerminal_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Refresh, cmd_Watch, cmd_Focus, cmd_Search_Watch, cmd_Search_Focus, cmd_Number, cmd_Key);
    THIS_APP->mtb_App_Init();
    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(KBT_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    longPressRequested = false;
    clicksRequested = 0;
    turns = 0;
    Kbt_Data_t* d = (Kbt_Data_t*)heap_caps_calloc(1, sizeof(Kbt_Data_t), MALLOC_CAP_SPIRAM);
    Ngx_Stock_t* stocks = (Ngx_Stock_t*)heap_caps_calloc(NGX_STOCKS_MAX, sizeof(Ngx_Stock_t), MALLOC_CAP_SPIRAM);
    Ngx_View_t* v = (Ngx_View_t*)heap_caps_calloc(1, sizeof(Ngx_View_t), MALLOC_CAP_SPIRAM);
    if (!d || !stocks || !v || !kbt_Net_Start(settings)){
        heap_caps_free(d); heap_caps_free(stocks); heap_caps_free(v);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    d->board.stocks = stocks;
    appOpen = true;
    uint8_t page = KBT_P_OVERVIEW;
    int scroll = 0;
    uint32_t version = 0, pageSince = millis(), scrollMs = millis(), phoneMs = 0, lastSecond = 0;
    bool redraw = true, phoneDirty = true, settingsDirty = false;
    char status[48] = "", message[160] = "";

    auto step_Page = [&](int dir){
        for (int i = 0; i < KBT_PAGES; i++){
            page = (uint8_t)((page + KBT_PAGES + dir) % KBT_PAGES);
            if (page_Shown(settings, *d, page)) break;
        }
        scroll = 0;
        pageSince = scrollMs = millis();
        redraw = true;
    };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            kbt_Net_Set_Settings(settings);
            settingsDirty = redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; kbt_Net_Refresh(); }
        if (kbt_Net_Copy(*d, version)) redraw = phoneDirty = true;
        if (!page_Shown(settings, *d, page)) step_Page(1);

        // The knob.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        const int t = turns;
        if (t) turns = 0;
        if (clicks == 1) step_Page(1);
        else if (clicks == 2) kbt_Net_Refresh();
        if (t) step_Page(t > 0 ? 1 : -1);
        if (longPressRequested){ longPressRequested = false; settings.cycle = settings.cycle ? 0 : 1; settingsDirty = phoneDirty = true; pageSince = ms; }
        if (settings.cycle && ms - pageSince >= settings.seconds * 1000u) step_Page(1);
        // Long lists scroll by themselves, a screenful every 5 seconds.
        const int rows = page == KBT_P_WATCH ? settings.watchCount : page == KBT_P_INDICES ? d->indices.count : page == KBT_P_TBILLS ? d->tbills.count
                       : page == KBT_P_BONDS ? d->bonds.count : 0;
        if (rows > NGX_VISIBLE_ROWS && ms - scrollMs >= 5000){ scrollMs = ms; scroll = scroll + NGX_VISIBLE_ROWS >= rows ? 0 : scroll + NGX_VISIBLE_ROWS; redraw = true; }
        if (now != lastSecond){ lastSecond = now; if (now % 30 == 0) redraw = true; }

        if (redraw){
            redraw = false;
            int wd;
            uint16_t minute;
            lagos(now, wd, minute);
            const bool trading = ngx_Trading_Time(wd, minute);
            const bool holiday = trading && d->statusKnown && !d->open;
            if (holiday) snprintf(status, sizeof(status), "Closed today");
            else if (trading) snprintf(status, sizeof(status), "Open, 30 min delayed");
            else snprintf(status, sizeof(status), wd == 0 || wd == 6 ? "Closed: weekend" : "Closed");
            memset(v, 0, sizeof(*v));
            v->board = &d->board;
            v->source = "Kobo Terminal";
            v->status = status;
            v->open = trading && !holiday;
            v->now = now;
            v->watch = settings.watch;
            v->watchCount = settings.watchCount;
            v->scroll = scroll;
            v->focus = ngx_Find(d->board, settings.focus);
            if (d->factsCount && !strcasecmp(d->factsSymbol, settings.focus)) for (int i = 0; i < d->factsCount && i < 6; i++) v->facts[i] = d->facts[i];
            const bool noData = d->board.count == 0 && !d->board.market.known;
            if (d->state == KBT_NO_KEY || ((d->state == KBT_KEY || d->state == KBT_DAY_USED) && noData)){
                v->page = NGX_MESSAGE;
                v->title = "Kobo Terminal";
                snprintf(message, sizeof(message), "%s%s", d->text, d->state == KBT_NO_KEY ? ". A free key: koboterminal.com/api" : "");
                v->message = message;
            } else if (noData){
                v->page = NGX_MESSAGE;
                v->title = "Kobo Terminal";
                v->message = "Asking Kobo Terminal for the market...";
            } else switch (page){
                case KBT_P_OVERVIEW: v->page = NGX_OVERVIEW; break;
                case KBT_P_HEATMAP: v->page = NGX_HEATMAP; break;
                case KBT_P_MOVERS: v->page = NGX_MOVERS; break;
                case KBT_P_WATCH: v->page = NGX_WATCH; break;
                case KBT_P_STOCK: v->page = NGX_STOCK; break;
                case KBT_P_INDICES: v->page = NGX_TABLE; v->table = &d->indices; break;
                case KBT_P_TBILLS: v->page = NGX_TABLE; v->table = &d->tbills; break;
                case KBT_P_BONDS: v->page = NGX_TABLE; v->table = &d->bonds; break;
                default: break;
            }
            ngx_Draw(*v);
        }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(KBT_NVS_KEY, &settings, sizeof(settings)); }
        if (phoneDirty && ms - phoneMs > 2000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    kbt_Net_Stop();
    if (settingsDirty) mtb_Write_Nvs_Struct(KBT_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(d);
    heap_caps_free(stocks);
    heap_caps_free(v);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static void kbt_Values(JsonObject values){
    Kbt_Settings_t s;
    if (appOpen){ portENTER_CRITICAL(&requestLock); s = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(s);
    JsonArray w = values["stocks"].to<JsonArray>();
    for (int i = 0; i < s.watchCount; i++){ JsonObject o = w.add<JsonObject>(); o["name"] = s.watch[i]; o["value"] = s.watch[i]; }
    values["stock"]["name"] = s.focus;
    values["stock"]["value"] = s.focus;
    char key[4] = "p0";
    for (int i = 0; i < KBT_PAGES; i++){ key[1] = (char)('0' + i); values[key] = (s.pages >> i) & 1; }
    values["cycle"] = s.cycle;
    values["seconds"] = s.seconds;
    values["apiKey_set"] = s.apiKey[0] ? 1 : 0;
    memset(s.apiKey, 0, sizeof(s.apiKey));

    JsonArray rows = values["market"].to<JsonArray>();
    if (!appOpen){ JsonObject o = rows.add<JsonObject>(); o["label"] = "NGX Market"; o["value"] = "Open Kobo Terminal on Pixlpal to see the market"; o["state"] = "info"; return; }
    Kbt_Data_t* d = (Kbt_Data_t*)heap_caps_calloc(1, sizeof(Kbt_Data_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    uint32_t version = 0xFFFFFFFFu;
    kbt_Net_Copy(*d, version);                 // board.stocks stays null: the market and the plan are enough here
    char t[160];
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "All-Share Index";
    if (d->board.market.known){
        char pct[16];
        ngx_Percent(pct, sizeof(pct), d->board.market.asiChangePct);
        snprintf(t, sizeof(t), "%.2f (%s), %u up, %u down", (double)d->board.market.asi, pct, d->board.market.advancers, d->board.market.decliners);
        o["value"] = t;
        o["state"] = d->board.market.asiChangePct >= 0 ? "ok" : "error";
    } else { o["value"] = d->text[0] ? d->text : "Waiting"; o["state"] = d->state == KBT_OK || d->state == KBT_WAIT ? "info" : "error"; }
    if (d->state != KBT_OK && d->state != KBT_WAIT && d->board.market.known){ o = rows.add<JsonObject>(); o["label"] = "Now"; o["value"] = d->text; o["state"] = "warn"; }
    o = rows.add<JsonObject>();
    o["label"] = "Plan";
    snprintf(t, sizeof(t), "%s: %lu of %lu calls today%s", kbt_Plan_Name(d->plan), (unsigned long)d->callsToday, (unsigned long)d->dailyLimit,
             d->plan == KBT_PERSONAL ? " (fundamentals need Starter)" : "");
    o["value"] = t;
    o["state"] = d->callsToday * 10 < d->dailyLimit * 9 ? "ok" : "warn";
    if (d->nextRefresh){
        const time_t tt = (time_t)d->nextRefresh;
        struct tm lt;
        localtime_r(&tt, &lt);
        o = rows.add<JsonObject>();
        o["label"] = "Next prices";
        snprintf(t, sizeof(t), "%02d:%02d (Kobo's prices are 30 minutes delayed, refreshed every 20)", lt.tm_hour, lt.tm_min);
        o["value"] = t;
        o["state"] = "info";
    }
    heap_caps_free(d);
}
