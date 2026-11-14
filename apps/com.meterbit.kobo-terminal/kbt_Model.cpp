// Kobo Terminal: settings, answers, the day's calls. No Arduino (tested on a PC).
#include "kbt_Model.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

ArduinoJson::Allocator* kbt_Json_Allocator(void);

void kbt_Settings_Default(Kbt_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = KBT_SETTINGS_MAGIC;
    static const char* const WATCH[] = {"DANGCEM", "MTNN", "GTCO", "ZENITHBANK", "SEPLAT", "ACCESSCORP"};
    for (const char* w : WATCH) snprintf(s.watch[s.watchCount++], 16, "%s", w);
    strcpy(s.focus, "MTNN");
    s.pages = 0xFF;
    s.cycle = 1;
    s.seconds = 15;
}

void kbt_Settings_Clamp(Kbt_Settings_t& s){
    s.magic = KBT_SETTINGS_MAGIC;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
    if (s.watchCount > NGX_WATCH_MAX) s.watchCount = NGX_WATCH_MAX;
    for (auto& w : s.watch) w[15] = '\0';
    s.focus[15] = '\0';
    if (!s.pages) s.pages = 1;
    s.cycle = s.cycle ? 1 : 0;
    if (s.seconds < 5) s.seconds = 5;
    if (s.seconds > 120) s.seconds = 120;
}

const char* kbt_Plan_Name(uint8_t p){
    static const char* const N[] = {"Personal (free)", "Starter", "Professional", "Institutional", "Checking"};
    return N[p <= KBT_UNKNOWN ? p : KBT_UNKNOWN];
}

uint32_t kbt_Daily_Limit(uint8_t p){
    switch (p){ case KBT_STARTER: return 1000; case KBT_PROFESSIONAL: return 10000; case KBT_INSTITUTIONAL: return 50000; default: return 100; }
}

uint32_t kbt_Refresh_Seconds(uint32_t left, uint32_t per, uint16_t m){
    const uint32_t MIN = 20 * 60, MAX = 3 * 3600;
    if (!per) per = 3;
    const uint32_t trading = m < 9 * 60 ? 7 * 3600u : m < 16 * 60 ? (16 * 60 - m) * 60u : 0;
    const uint32_t usable = left > left * 15 / 100 ? left - left * 15 / 100 : 0;     // 15% kept for the rest of the day
    if (usable < per) return MAX;
    const uint32_t need = trading * per / usable;
    return need < MIN ? MIN : need > MAX ? MAX : need;
}

static void why_Set(char* why, size_t size, const char* text){ if (why && size) snprintf(why, size, "%s", text); }
static void copy(char* dest, size_t size, const char* text){ snprintf(dest, size, "%s", text ? text : ""); }
static bool has(const char* text, const char* part){
    const size_t n = strlen(part);
    for (const char* p = text; p && *p; p++) if (!strncasecmp(p, part, n)) return true;
    return false;
}

uint8_t kbt_Check(const char* body, size_t length, int code, char* why, size_t ws){
    if (code == 200 && body && length) return KBT_OK;
    JsonDocument doc;
    const char* message = (body && !deserializeJson(doc, body, length)) ? (doc["message"] | (doc["error"].is<const char*>() ? doc["error"].as<const char*>() : "")) : "";
    if (code <= 0){ why_Set(why, ws, "Cannot reach Kobo Terminal"); return KBT_ERROR; }
    if (code == 401){ why_Set(why, ws, "Kobo Terminal refused the API key"); return KBT_KEY; }
    if (code == 403 || (code == 402) || has(message, "plan") || has(message, "upgrade") || has(message, "tier")){
        why_Set(why, ws, "Not in this Kobo Terminal plan");
        return KBT_PLAN;
    }
    if (code == 429){
        if (has(message, "day") || has(message, "daily")){ why_Set(why, ws, "Today's calls are used up"); return KBT_DAY_USED; }
        why_Set(why, ws, "Too many calls a minute: waiting");
        return KBT_RATE;
    }
    char t[96];
    if (message[0]) snprintf(t, sizeof(t), "Kobo Terminal: %.70s", message);
    else snprintf(t, sizeof(t), "Kobo Terminal answered HTTP %d", code);
    why_Set(why, ws, t);
    return KBT_ERROR;
}

// The list in an answer: the root itself, or under "data" or @p other.
static JsonArray list_Of(JsonDocument& doc, const char* other){
    if (doc.is<JsonArray>()) return doc.as<JsonArray>();
    if (doc["data"].is<JsonArray>()) return doc["data"].as<JsonArray>();
    if (other && doc[other].is<JsonArray>()) return doc[other].as<JsonArray>();
    if (doc["data"][other ? other : "items"].is<JsonArray>()) return doc["data"][other ? other : "items"].as<JsonArray>();
    return JsonArray();
}

static float num(JsonVariantConst v){
    if (v.is<const char*>()) return (float)atof(v.as<const char*>());
    return v | 0.0f;
}

int kbt_Parse_Stocks(const char* body, size_t length, Ngx_Board_t& b){
    JsonDocument doc(kbt_Json_Allocator());
    if (!body || deserializeJson(doc, body, length)) return -1;
    JsonArray list = list_Of(doc, "stocks");
    if (list.isNull()) return -1;
    b.count = 0;
    for (JsonObject c : list){
        if (b.count >= NGX_STOCKS_MAX) break;
        const char* sym = c["symbol"] | "";
        if (!sym[0]) continue;
        Ngx_Stock_t& s = b.stocks[b.count++];
        memset(&s, 0, sizeof(s));
        copy(s.symbol, sizeof(s.symbol), sym);
        copy(s.name, sizeof(s.name), c["name"] | "");
        copy(s.sector, sizeof(s.sector), c["sector"] | "");
        s.price = num(c["current_price"]);
        s.changePct = num(c["change_percent"]);
        s.volume = num(c["volume"]);
        s.pe = num(c["pe_ratio"]);
        const double shares = c["shares_outstanding"] | 0.0;
        s.marketCap = shares > 0 ? shares * s.price : 0;
        // The naira change from the percent and the price.
        if (s.changePct != 0 && s.price > 0) s.change = s.price - s.price / (1 + s.changePct / 100.0f);
    }
    return b.count;
}

bool kbt_Parse_Market(const char* body, size_t length, Ngx_Market_t& m){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonObject d = doc["data"].is<JsonObject>() ? doc["data"].as<JsonObject>() : doc.as<JsonObject>();
    if (d.isNull() || d["asi"].isNull()) return false;
    m.asi = num(d["asi"]);
    m.asiChangePct = num(d["pct_change"]);
    m.asiChange = m.asiChangePct != 0 ? m.asi - m.asi / (1 + m.asiChangePct / 100.0f) : 0;
    m.marketCap = d["market_cap"] | 0.0;
    m.volume = d["volume"] | 0.0;
    m.value = d["value"] | 0.0;
    m.deals = d["deals"] | 0u;
    m.advancers = d["advancers"] | 0;
    m.decliners = d["decliners"] | 0;
    m.unchanged = d["unchanged"] | 0;
    m.known = true;
    return true;
}

bool kbt_Parse_Indices(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonArray list = list_Of(doc, "indices");
    if (list.isNull()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "NGX indices");
    for (JsonObject r : list){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        copy(row.label, sizeof(row.label), r["code"] | "");
        const char* name = r["name"] | "";
        copy(row.detail, sizeof(row.detail), strncmp(name, "NGX ", 4) ? name : name + 4);
        snprintf(row.value, sizeof(row.value), "%.2f", (double)num(r["currentPrice"]));
        const float pct = num(r["changePercentage"]);
        ngx_Percent(row.change, sizeof(row.change), pct);
        row.sign = pct > 0.0001f ? 1 : pct < -0.0001f ? -1 : 0;
    }
    return true;
}

bool kbt_Parse_Auctions(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonArray list = list_Of(doc, "auctions");
    if (list.isNull()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "CBN auctions: stop rates");
    for (JsonObject r : list){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        const char* type = r["instrument_type"] | "";
        const char* tenor = r["tenor_label"] | "";
        snprintf(row.label, sizeof(row.label), "%.10s %.12s", type, tenor);
        snprintf(row.detail, sizeof(row.detail), "%s", (const char*)(r["auction_date"] | ""));
        snprintf(row.value, sizeof(row.value), "%.2f%%", (double)num(r["stop_rate"]));
        const float sub = num(r["subscription_rate"]);
        if (sub > 0) snprintf(row.change, sizeof(row.change), "%.1fX BID", (double)sub);
        row.sign = 0;
    }
    return true;
}

bool kbt_Parse_Bonds(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc(kbt_Json_Allocator());
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonArray list = list_Of(doc, "bonds");
    if (list.isNull()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "Bonds on NGX");
    for (JsonObject r : list){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        copy(row.label, sizeof(row.label), r["ticker"] | (r["name"] | ""));
        const char* maturity = r["maturity_date"] | "";
        snprintf(row.detail, sizeof(row.detail), "%.2f%% TO %.10s", (double)num(r["coupon_rate"]), maturity);
        const float price = num(r["clean_price"]);
        if (price > 0) snprintf(row.value, sizeof(row.value), "%.2f", (double)price);
        else copy(row.value, sizeof(row.value), "-");
    }
    return true;
}

bool kbt_Parse_Status(const char* body, size_t length, bool& open, char* message, size_t size){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonObject d = doc["data"].is<JsonObject>() ? doc["data"].as<JsonObject>() : doc.as<JsonObject>();
    const char* st = d["status"] | "";
    if (!st[0]) return false;
    open = !strcasecmp(st, "open");
    copy(message, size, d["message"] | st);
    return true;
}

int kbt_Parse_Fundamentals(const char* body, size_t length, const char* symbol, char lines[6][40]){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return 0;
    JsonVariant f = doc["fundamentals"];
    // Keyed by symbol, or the one stock's figures directly.
    JsonObject one;
    if (f[symbol].is<JsonObject>()) one = f[symbol];
    else if (f.is<JsonObject>() && !f["pe_ratio"].isNull()) one = f.as<JsonObject>();
    else if (f.is<JsonArray>()) for (JsonObject o : f.as<JsonArray>()) if (!strcasecmp(o["symbol"] | "", symbol)) one = o;
    if (one.isNull()) return 0;
    int n = 0;
    auto add = [&](const char* label, JsonVariantConst v, const char* fmt){
        if (n >= 6 || v.isNull()) return;
        const float x = num(v);
        if (x == 0 && !v.is<const char*>()) return;
        char value[24];
        snprintf(value, sizeof(value), fmt, (double)x);
        snprintf(lines[n++], 40, "%s %s", label, value);
    };
    add("P/E", one["pe_ratio"], "%.1f");
    add("EPS", one["eps"], "N%.2f");
    add("DIVIDEND YIELD", one["dividend_yield"], "%.2f%%");
    add("ROE", one["roe"], "%.1f%%");
    add("PROFIT MARGIN", one["profit_margin"], "%.1f%%");
    add("BETA", one["beta"], "%.2f");
    return n;
}
