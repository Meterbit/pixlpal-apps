// NGX Market: settings, NGN Market's answers, pacing. No Arduino (tested on a PC).
#include "ngm_Model.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// The device gives a PSRAM allocator (ngm_Net.cpp); a PC test the default one.
ArduinoJson::Allocator* ngm_Json_Allocator(void);

void ngm_Settings_Default(Ngm_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = NGM_SETTINGS_MAGIC;
    static const char* const WATCH[] = {"DANGCEM", "MTNN", "GTCO", "ZENITHBANK", "AIRTELAFRI", "BUAFOODS"};
    for (const char* w : WATCH) snprintf(s.watch[s.watchCount++], 16, "%s", w);
    strcpy(s.focus, "DANGCEM");
    s.pages = (uint8_t)((1u << NGM_P_OVERVIEW) | (1u << NGM_P_HEATMAP) | (1u << NGM_P_MOVERS) | (1u << NGM_P_WATCH) | (1u << NGM_P_STOCK) |
                        (1u << NGM_P_INDICES) | (1u << NGM_P_FOREX) | (1u << NGM_P_DIVIDENDS));
    s.cycle = 1;
    s.seconds = 15;
    s.chartPeriod = 1;
}

void ngm_Settings_Clamp(Ngm_Settings_t& s){
    s.magic = NGM_SETTINGS_MAGIC;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
    if (s.watchCount > NGX_WATCH_MAX) s.watchCount = NGX_WATCH_MAX;
    for (auto& w : s.watch) w[15] = '\0';
    s.focus[15] = '\0';
    if (!s.pages) s.pages = 1u << NGM_P_OVERVIEW;
    s.cycle = s.cycle ? 1 : 0;
    if (s.seconds < 5) s.seconds = 5;
    if (s.seconds > 120) s.seconds = 120;
    if (s.chartPeriod > 3) s.chartPeriod = 1;
}

uint8_t ngm_Plan_Of(const char* n){
    static const char* const N[] = {"free", "hobby", "starter", "pro", "business", "enterprise"};
    for (uint8_t i = 0; i < 6; i++) if (n && !strcasecmp(n, N[i])) return i;
    return NGM_UNKNOWN;
}

const char* ngm_Plan_Name(uint8_t p){
    static const char* const N[] = {"Free", "Hobby", "Starter", "Pro", "Business", "Enterprise", "Unknown"};
    return N[p <= NGM_UNKNOWN ? p : NGM_UNKNOWN];
}

const char* ngm_Period(uint8_t p){ static const char* const P[] = {"7d", "30d", "90d", "1y"}; return P[p < 4 ? p : 1]; }
const char* ngm_Period_Label(uint8_t p){ static const char* const P[] = {"7 days", "30 days", "90 days", "a year"}; return P[p < 4 ? p : 1]; }

uint8_t ngm_Needs(uint8_t screen){ return screen == NGM_P_DIVIDENDS ? NGM_STARTER : NGM_FREE; }

// Times ----------------------------------------------------------------------------------------------------------------
static int64_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static uint32_t iso(const char* t){
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (!t || sscanf(t, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) < 3) return 0;
    const int64_t v = days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    return v > 0 ? (uint32_t)v : 0;
}

void ngm_Lagos(uint32_t unix, int& weekday, uint16_t& minute){
    const uint32_t t = unix + 3600;
    weekday = (int)((t / 86400 + 4) % 7);                // 1 January 1970 was a Thursday
    minute = (uint16_t)(t % 86400 / 60);
}

uint32_t ngm_Refresh_Seconds(uint32_t remaining, uint32_t per, uint32_t now, uint32_t resetAt){
    const uint32_t MIN = 20 * 60, MAX = 6 * 3600;
    if (!per) per = 1;
    if (!resetAt || resetAt <= now) return MIN;
    // Trading seconds left: seven hours on each weekday until the reset (today counted from now).
    uint32_t trading = 0;
    for (uint32_t t = now; t < resetAt && trading < 400u * 3600; ){
        int wd;
        uint16_t m;
        ngm_Lagos(t, wd, m);
        if (ngx_Trading_Time(wd, m)){
            const uint32_t left = (16 * 60 - m) * 60;
            const uint32_t step = left < resetAt - t ? left : resetAt - t;
            trading += step;
            t += step ? step : 60;
        } else {
            // On to the next minute the market could be open (by whole hours; precise enough here).
            t += m < 9 * 60 ? (uint32_t)(9 * 60 - m) * 60 : (uint32_t)(24 * 60 - m) * 60;
        }
    }
    const uint32_t usable = remaining > remaining / 10 ? remaining - remaining / 10 : 0;
    if (!usable) return MAX;
    const uint64_t need = (uint64_t)trading * per / usable;
    return need < MIN ? MIN : need > MAX ? MAX : (uint32_t)need;
}

// Answers ----------------------------------------------------------------------------------------------------------------
static void why_Set(char* why, size_t size, const char* text){ if (why && size) snprintf(why, size, "%s", text); }

uint8_t ngm_Check(const char* body, size_t length, int code, Ngm_Meta_t& meta, char* why, size_t ws){
    JsonDocument filter;
    filter["success"] = true;
    filter["meta"] = true;
    filter["error"] = true;
    JsonDocument doc;
    const bool parsed = body && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    if (parsed && doc["meta"].is<JsonObject>()){
        JsonObject m = doc["meta"];
        if (m["plan"].is<const char*>()) meta.plan = ngm_Plan_Of(m["plan"]);
        meta.callsUsed = m["calls_used"] | meta.callsUsed;
        meta.callsRemaining = m["calls_remaining"] | meta.callsRemaining;
        if (m["reset_at"].is<const char*>()) meta.resetAt = iso(m["reset_at"]);
        meta.known = true;
    }
    if (code == 200 && parsed && (doc["success"] | false)) return NGM_OK;
    const char* ecode = parsed ? (doc["error"]["code"] | "") : "";
    if (code <= 0){ why_Set(why, ws, "Cannot reach NGN Market"); return NGM_ERROR; }
    if (!strcmp(ecode, "MISSING_API_KEY") || !strcmp(ecode, "INVALID_API_KEY") || code == 401){ why_Set(why, ws, "NGN Market refused the API key"); return NGM_KEY; }
    if (!strcmp(ecode, "IP_NOT_ALLOWED")){ why_Set(why, ws, "This key only works from other IP addresses (its allowlist)"); return NGM_KEY; }
    if (!strcmp(ecode, "PLAN_REQUIRED")){
        char t[96];
        snprintf(t, sizeof(t), "Needs NGN Market's %s plan", (const char*)(doc["error"]["required_plan"] | "higher"));
        why_Set(why, ws, t);
        return NGM_PLAN;
    }
    if (!strcmp(ecode, "QUOTA_EXCEEDED")){ why_Set(why, ws, "This month's calls are used up"); return NGM_QUOTA; }
    if (!strcmp(ecode, "RATE_LIMITED") || code == 429){ why_Set(why, ws, "Too many calls a minute: waiting"); return NGM_RATE; }
    if (code == 404){ why_Set(why, ws, "No data for that (a day without trading?)"); return NGM_ERROR; }
    char t[64];
    snprintf(t, sizeof(t), "NGN Market answered HTTP %d", code);
    why_Set(why, ws, t);
    return NGM_ERROR;
}

static void copy(char* dest, size_t size, const char* text){ snprintf(dest, size, "%s", text ? text : ""); }

bool ngm_Parse_Snapshot(const char* body, size_t length, Ngx_Market_t& m){
    JsonDocument doc(ngm_Json_Allocator());
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonObject d = doc["data"];
    if (d.isNull() || !d["asi"].is<float>()) return false;
    m.asi = d["asi"] | 0.0f;
    m.asiChange = d["asi_change"] | 0.0f;
    m.asiChangePct = d["asi_change_percent"] | 0.0f;
    m.ytdPct = d["ytd_asi_change_percent"] | 0.0f;
    m.deals = d["deals"] | 0u;
    m.volume = d["volume"] | 0.0;
    m.value = d["value_traded"] | 0.0;
    m.marketCap = d["market_cap"]["equity"] | 0.0;
    m.advancers = d["breadth"]["advancers"] | 0;
    m.decliners = d["breadth"]["decliners"] | 0;
    m.unchanged = d["breadth"]["unchanged"] | 0;
    copy(m.date, sizeof(m.date), d["date"] | "");
    m.known = true;
    return true;
}

int ngm_Parse_Companies(const char* body, size_t length, Ngx_Board_t& b){
    JsonDocument filter;
    JsonObject f = filter["data"]["data"].add<JsonObject>();
    for (const char* k : {"symbol", "name", "sector", "price", "prev_close", "day_high", "day_low", "volume", "market_cap", "price_change",
                          "price_change_percent", "high52wk", "low52wk"}) f[k] = true;
    filter["data"]["pagination"]["total_pages"] = true;
    JsonDocument doc(ngm_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter))) return 0;
    JsonArray list = doc["data"]["data"];
    if (list.isNull()) return 0;
    for (JsonObject c : list){
        if (b.count >= NGX_STOCKS_MAX) break;
        const char* sym = c["symbol"] | "";
        if (!sym[0]) continue;
        Ngx_Stock_t& s = b.stocks[b.count++];
        memset(&s, 0, sizeof(s));
        copy(s.symbol, sizeof(s.symbol), sym);
        copy(s.name, sizeof(s.name), c["name"] | "");
        copy(s.sector, sizeof(s.sector), c["sector"] | "");
        s.price = c["price"] | 0.0f;
        s.prevClose = c["prev_close"] | 0.0f;
        s.high = c["day_high"] | 0.0f;
        s.low = c["day_low"] | 0.0f;
        s.volume = c["volume"] | 0.0;
        s.marketCap = c["market_cap"] | 0.0;
        s.change = c["price_change"] | 0.0f;
        s.changePct = c["price_change_percent"] | 0.0f;
        s.high52 = c["high52wk"] | 0.0f;
        s.low52 = c["low52wk"] | 0.0f;
    }
    const int pages = doc["data"]["pagination"]["total_pages"] | 1;
    return pages < 1 ? 1 : pages;
}

bool ngm_Parse_Status(const char* body, size_t length, Ngm_Status_t& s){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length)) return false;
    JsonObject d = doc["data"];
    if (d.isNull()) return false;
    memset(&s, 0, sizeof(s));
    s.open = d["is_open"] | false;
    copy(s.reason, sizeof(s.reason), d["reason"] | "");
    copy(s.nextOpen, sizeof(s.nextOpen), d["next_open"]["label"] | "");
    copy(s.holiday, sizeof(s.holiday), d["holiday"]["name"] | "");
    s.known = true;
    return true;
}

static void fmt_Number(char* dest, size_t size, float v){
    if (v >= 1000) snprintf(dest, size, "%.0f", (double)v);
    else if (v >= 10) snprintf(dest, size, "%.2f", (double)v);
    else snprintf(dest, size, "%.4f", (double)v);
}

bool ngm_Parse_Forex(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length) || !doc["data"]["rates"].is<JsonArray>()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "Naira exchange rates");
    for (JsonObject r : doc["data"]["rates"].as<JsonArray>()){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        snprintf(row.label, sizeof(row.label), "%s", (const char*)(r["currency"] | "?"));
        char n[16];
        fmt_Number(n, sizeof(n), r["rate"] | 0.0f);
        snprintf(row.value, sizeof(row.value), "N%s", n);
        const float pct = r["daily_change_percent"] | 0.0f;
        ngx_Percent(row.change, sizeof(row.change), pct);
        // The naira weakening (more naira for a dollar) shows red.
        row.sign = pct > 0.0001f ? -1 : pct < -0.0001f ? 1 : 0;
        snprintf(row.detail, sizeof(row.detail), "PER 1 %s", (const char*)(r["currency"] | ""));
    }
    return true;
}

bool ngm_Parse_Indices(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length) || !doc["data"].is<JsonArray>()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "NGX indices");
    for (JsonObject r : doc["data"].as<JsonArray>()){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        copy(row.label, sizeof(row.label), r["symbol"] | "");
        const char* name = r["name"] | "";
        const char* short_ = strncmp(name, "NGX ", 4) ? name : name + 4;
        copy(row.detail, sizeof(row.detail), short_);
        snprintf(row.value, sizeof(row.value), "%.2f", (double)(r["value"] | 0.0f));
        const float pct = r["change_pct"] | 0.0f;
        ngx_Percent(row.change, sizeof(row.change), pct);
        row.sign = pct > 0.0001f ? 1 : pct < -0.0001f ? -1 : 0;
    }
    return true;
}

bool ngm_Parse_Dividends(const char* body, size_t length, Ngx_Table_t& t){
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length) || !doc["data"]["dividends"].is<JsonArray>()) return false;
    t.count = 0;
    copy(t.title, sizeof(t.title), "Dividends coming");
    for (JsonObject r : doc["data"]["dividends"].as<JsonArray>()){
        if (t.count >= NGX_ROWS_MAX) break;
        Ngx_Row_t& row = t.rows[t.count++];
        memset(&row, 0, sizeof(row));
        copy(row.label, sizeof(row.label), r["symbol"] | "");
        const char* ex = r["ex_dividend_date"] | "";
        snprintf(row.detail, sizeof(row.detail), "EX %s %s", ex, (const char*)(r["type"] | ""));
        ngx_Naira(row.value, sizeof(row.value), r["dividend"] | 0.0, false);
        const float y = r["yield"] | 0.0f;
        if (y > 0) snprintf(row.change, sizeof(row.change), "%.1f%% YLD", (double)y);
        row.sign = 1;
    }
    return true;
}

int ngm_Parse_Chart(const char* body, size_t length, float* prices, int max){
    JsonDocument doc(ngm_Json_Allocator());
    if (!body || deserializeJson(doc, body, length)) return 0;
    JsonArray pts = doc["data"]["data"];
    if (pts.isNull()) return 0;
    const int total = (int)pts.size();
    const int count = total < max ? total : max;
    // Thinned evenly to @p max points, oldest first, the first and last always kept.
    int n = 0;
    for (int i = 0; i < count; i++){
        const int at = total <= max ? i : (int)((int64_t)i * (total - 1) / (max - 1));
        JsonVariant p = pts[at];
        const float close = p.is<JsonArray>() ? (p[1] | 0.0f) : (p["close"] | (p["price"] | 0.0f));
        if (close > 0) prices[n++] = close;
    }
    return n;
}
