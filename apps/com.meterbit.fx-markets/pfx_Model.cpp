#include "pfx_Model.h"
#include "mtb_kit_ui.h"
#include <cmath>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct Pfx_Currency_t { const char* code; const char* name; };

// Majors first: a pair with one of these is written with the earlier one first (EUR/USD, USD/JPY).
static const Pfx_Currency_t CURRENCIES[] = {
    {"EUR", "Euro"}, {"GBP", "British pound sterling"}, {"AUD", "Australian dollar"}, {"NZD", "New Zealand dollar"},
    {"USD", "US dollar"}, {"CAD", "Canadian dollar"}, {"CHF", "Swiss franc"}, {"JPY", "Japanese yen"},
    {"CNH", "Chinese yuan (offshore)"}, {"HKD", "Hong Kong dollar"}, {"SGD", "Singapore dollar"}, {"SEK", "Swedish krona"},
    {"NOK", "Norwegian krone"}, {"DKK", "Danish krone"}, {"PLN", "Polish zloty"}, {"CZK", "Czech koruna"}, {"HUF", "Hungarian forint"},
    {"TRY", "Turkish lira"}, {"ZAR", "South African rand"}, {"MXN", "Mexican peso"}, {"BRL", "Brazilian real"}, {"INR", "Indian rupee"},
    {"KRW", "South Korean won"}, {"THB", "Thai baht"}, {"ILS", "Israeli shekel"}, {"AED", "UAE dirham"}, {"SAR", "Saudi riyal"},
    {"NGN", "Nigerian naira"}, {"GHS", "Ghanaian cedi"}, {"KES", "Kenyan shilling"}, {"EGP", "Egyptian pound"}, {"XAU", "Gold (troy ounce)"},
    {"XAG", "Silver (troy ounce)"},
};
static const int CURRENCY_COUNT = (int)(sizeof(CURRENCIES) / sizeof(CURRENCIES[0]));

static int pfx_Rank(const char* code){
    for (int i = 0; i < CURRENCY_COUNT; i++) if (strcmp(CURRENCIES[i].code, code) == 0) return i;
    return 999;
}

const char* pfx_Currency_Name(const char* code){
    for (int i = 0; i < CURRENCY_COUNT; i++) if (strcmp(CURRENCIES[i].code, code) == 0) return CURRENCIES[i].name;
    return "";
}

bool pfx_Code_Valid(const char* code){
    if (code == nullptr || strlen(code) != 3) return false;
    for (int i = 0; i < 3; i++) if (!isupper((unsigned char)code[i])) return false;
    return true;
}

bool pfx_Key_Valid(const char* key){
    if (key == nullptr) return false;
    const size_t n = strlen(key);
    if (n < 20 || n >= PFX_KEY_LEN) return false;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)key[i]) && key[i] != '_' && key[i] != '-') return false;
    return true;
}

static void pfx_Set(Pfx_Pair_Code_t& p, const char* from, const char* to){
    memcpy(p.from, from, 3); p.from[3] = '\0';
    memcpy(p.to, to, 3); p.to[3] = '\0';
}

void pfx_Settings_Default(Pfx_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = PFX_SETTINGS_MAGIC;
    static const char* const DEFAULTS[][2] = {{"EUR", "USD"}, {"GBP", "USD"}, {"USD", "JPY"}, {"USD", "NGN"}, {"USD", "CHF"}, {"AUD", "USD"}};
    for (const auto& d : DEFAULTS) pfx_Set(s.pairs[s.pairCount++], d[0], d[1]);
    s.focus = 0;
    s.pageSeconds = 12;
    s.autoCycle = 1;
    s.intraday = 1;
}

void pfx_Settings_Clamp(Pfx_Settings_t& s){
    if (s.pairCount > PFX_PAIRS) s.pairCount = PFX_PAIRS;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.pairCount; i++){
        Pfx_Pair_Code_t p = s.pairs[i];
        p.from[3] = p.to[3] = '\0';
        if (pfx_Code_Valid(p.from) && pfx_Code_Valid(p.to) && strcmp(p.from, p.to) != 0) s.pairs[kept++] = p;
    }
    for (uint8_t i = kept; i < PFX_PAIRS; i++) memset(&s.pairs[i], 0, sizeof(s.pairs[i]));
    s.pairCount = kept;
    if (s.pairCount == 0){ Pfx_Settings_t d; pfx_Settings_Default(d); memcpy(s.pairs, d.pairs, sizeof(s.pairs)); s.pairCount = d.pairCount; }
    if (s.focus >= s.pairCount) s.focus = 0;
    if (s.pageSeconds < 4) s.pageSeconds = 4;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
    s.intraday = s.intraday ? 1 : 0;
    s.apiKey[PFX_KEY_LEN - 1] = '\0';
    if (s.apiKey[0] && !pfx_Key_Valid(s.apiKey)) memset(s.apiKey, 0, sizeof(s.apiKey));
}

bool pfx_Parse_Pair(const char* text, Pfx_Pair_Code_t& pair){
    if (text == nullptr) return false;
    char letters[8];
    int n = 0;
    for (const char* c = text; *c; c++){
        if (isalpha((unsigned char)*c)){ if (n >= 6) return false; letters[n++] = (char)toupper((unsigned char)*c); }
        else if (!(*c == '/' || *c == ' ' || *c == '-' || *c == ':')) return false;
    }
    if (n != 6) return false;
    letters[6] = '\0';
    char from[4] = {letters[0], letters[1], letters[2], 0}, to[4] = {letters[3], letters[4], letters[5], 0};
    if (strcmp(from, to) == 0) return false;
    pfx_Set(pair, from, to);
    return true;
}

void pfx_Pair_Text(const Pfx_Pair_Code_t& p, char* dest, size_t size){ snprintf(dest, size, "%s/%s", p.from, p.to); }

static void pfx_Add_Result(Pfx_Result_Item_t* results, int& count, int max, const char* a, const char* b){
    if (count >= max || strcmp(a, b) == 0) return;
    // Market order: the higher-ranked currency first.
    const char* from = pfx_Rank(a) <= pfx_Rank(b) ? a : b;
    const char* to = from == a ? b : a;
    char pair[8];
    snprintf(pair, sizeof(pair), "%s/%s", from, to);
    for (int i = 0; i < count; i++) if (strcmp(results[i].pair, pair) == 0) return;
    snprintf(results[count].pair, sizeof(results[count].pair), "%s", pair);
    snprintf(results[count].detail, sizeof(results[count].detail), "%.20s in %.20s", pfx_Currency_Name(from), pfx_Currency_Name(to));
    count++;
}

int pfx_Search(const char* query, Pfx_Result_Item_t* results, int max){
    if (query == nullptr || max <= 0) return 0;
    int count = 0;
    Pfx_Pair_Code_t direct;
    // "EUR/USD" or "usdkes": a pair as typed, when both are currencies here, or written with a slash (any market).
    if (pfx_Parse_Pair(query, direct)){
        const bool known = pfx_Rank(direct.from) != 999 && pfx_Rank(direct.to) != 999;
        if (known || strchr(query, '/')){ pfx_Add_Result(results, count, max, direct.from, direct.to); if (!known) return count; }
    }
    // The currencies the words name.
    int matched[8];
    int m = 0;
    const char* w = query;
    while (*w && m < 8){
        while (*w == ' ' || *w == '/' || *w == ',') w++;
        const size_t n = strcspn(w, " /,");
        if (n == 0) break;
        for (int i = 0; i < CURRENCY_COUNT && m < 8; i++){
            const bool code = n == 3 && strncasecmp(CURRENCIES[i].code, w, 3) == 0;
            bool name = false;
            if (n >= 3) for (const char* h = CURRENCIES[i].name; *h && !name; h++) if (strncasecmp(h, w, n) == 0 && (h == CURRENCIES[i].name || h[-1] == ' ')) name = true;
            if (code || name){
                bool seen = false;
                for (int k = 0; k < m; k++) if (matched[k] == i) seen = true;
                if (!seen) matched[m++] = i;
            }
        }
        w += n;
    }
    if (m >= 2) for (int a = 0; a < m; a++) for (int b = a + 1; b < m; b++) pfx_Add_Result(results, count, max, CURRENCIES[matched[a]].code, CURRENCIES[matched[b]].code);
    // One currency: against the dollar, the euro and the pound.
    static const char* const BASES[] = {"USD", "EUR", "GBP", "JPY"};
    for (int k = 0; k < m; k++) for (const char* base : BASES) pfx_Add_Result(results, count, max, CURRENCIES[matched[k]].code, base);
    return count;
}

int pfx_Decimals(double p){
    p = fabs(p);
    return p >= 100 ? 2 : p >= 10 ? 3 : p >= 1 ? 4 : 5;
}

//##############################################################################################################
uint8_t pfx_Error_Of(int httpCode, JsonVariantConst doc, char* text, size_t size){
    const char* status = doc["status"] | "";
    const char* message = doc["message"] | (doc["error"] | "");
    if (httpCode == 200 && (strcmp(status, "OK") == 0 || strcmp(status, "DELAYED") == 0)) return PFX_OK;
    snprintf(text, size, "%s", message[0] ? message : status[0] ? status : "Massive did not answer as expected");
    if (httpCode == 401 || strstr(message, "API Key") || strstr(message, "API key")) return PFX_KEY;
    if (httpCode == 403 || strcmp(status, "NOT_AUTHORIZED") == 0) return PFX_PLAN;
    if (httpCode == 429) return PFX_RATE;
    if (httpCode >= 500) return PFX_SERVER;
    if (httpCode >= 400) return PFX_BAD;
    return httpCode == 200 ? PFX_PARSE : PFX_NET;
}

static float pfx_F(JsonVariantConst v){ return v.is<float>() || v.is<int>() ? v.as<float>() : NAN; }

uint8_t pfx_Parse_Daily(JsonVariantConst doc, Pfx_Quote_t& q){
    JsonArrayConst results = doc["results"];
    if (results.isNull() || results.size() == 0) return PFX_PARSE;
    // The last PFX_DAYS bars.
    const size_t n = results.size(), first = n > PFX_DAYS ? n - PFX_DAYS : 0;
    q.barCount = 0;
    for (size_t i = first; i < n; i++){
        JsonVariantConst r = results[i];
        Pfx_Bar_t& b = q.bars[q.barCount];
        b.o = pfx_F(r["o"]); b.h = pfx_F(r["h"]); b.l = pfx_F(r["l"]); b.c = pfx_F(r["c"]);
        b.t = (uint32_t)((r["t"] | 0.0) / 1000);
        if (!std::isnan(b.c)) q.barCount++;
    }
    if (q.barCount == 0) return PFX_PARSE;
    const Pfx_Bar_t& last = q.bars[q.barCount - 1];
    if (!q.realtime){
        q.last = last.c;
        q.dayHigh = last.h;
        q.dayLow = last.l;
        q.updated = last.t;
        q.prevClose = q.barCount >= 2 ? q.bars[q.barCount - 2].c : NAN;
    }
    q.valid = 1;
    q.error = 0;
    return PFX_OK;
}

uint8_t pfx_Parse_Snapshot(JsonVariantConst doc, const Pfx_Settings_t& s, Pfx_Data_t& d, uint32_t now){
    JsonArrayConst tickers = doc["tickers"];
    if (tickers.isNull()) return PFX_PARSE;
    int found = 0;
    for (JsonVariantConst t : tickers){
        const char* ticker = t["ticker"] | "";          // "C:EURUSD"
        if (strncmp(ticker, "C:", 2) != 0 || strlen(ticker) != 8) continue;
        for (uint8_t i = 0; i < s.pairCount; i++){
            if (strncmp(ticker + 2, s.pairs[i].from, 3) != 0 || strncmp(ticker + 5, s.pairs[i].to, 3) != 0) continue;
            Pfx_Quote_t& q = d.quotes[i];
            const double ask = t["lastQuote"]["a"] | 0.0, bid = t["lastQuote"]["b"] | 0.0;
            const double close = t["day"]["c"] | 0.0;
            const double last = ask > 0 && bid > 0 ? (ask + bid) / 2 : close;
            if (last <= 0) continue;
            q.last = last;
            q.prevClose = t["prevDay"]["c"] | NAN;
            q.dayHigh = t["day"]["h"] | NAN;
            q.dayLow = t["day"]["l"] | NAN;
            const double updated = t["updated"] | 0.0;      // Nanoseconds.
            q.updated = updated > 0 ? (uint32_t)(updated / 1e9) : now;
            q.realtime = 1;
            q.valid = 1;
            q.error = 0;
            found++;
        }
    }
    return found ? PFX_OK : PFX_PARSE;
}

uint8_t pfx_Parse_Intraday(JsonVariantConst doc, Pfx_Data_t& d){
    JsonArrayConst results = doc["results"];
    if (results.isNull()) return PFX_PARSE;
    const size_t n = results.size(), first = n > PFX_INTRADAY ? n - PFX_INTRADAY : 0;
    d.intradayCount = 0;
    for (size_t i = first; i < n; i++){
        JsonVariantConst r = results[i];
        Pfx_Bar_t& b = d.intraday[d.intradayCount];
        b.o = pfx_F(r["o"]); b.h = pfx_F(r["h"]); b.l = pfx_F(r["l"]); b.c = pfx_F(r["c"]);
        b.t = (uint32_t)((r["t"] | 0.0) / 1000);
        if (!std::isnan(b.c)) d.intradayCount++;
    }
    return PFX_OK;
}

void pfx_Date(char* dest, size_t size, uint32_t t){
    const int32_t z = (int32_t)(t / 86400) + 719468;
    const int32_t era = z / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    const int year = (int)yoe + era * 400 + (month <= 2);
    snprintf(dest, size, "%04d-%02u-%02u", year, month, day);
}

//##############################################################################################################
// Daylight saving. The n-th Sunday (n = -1: the last) of a month, as a day number.
static int32_t pfx_Sunday(int year, int month, int n){
    if (n > 0){
        const int32_t first = mtb_Kit_Days_From_Civil(year, month, 1);
        const int wd = mtb_Kit_Weekday(year, month, 1);         // Monday 0
        return first + (6 - wd + 7) % 7 + (n - 1) * 7;
    }
    const int days = mtb_Kit_Days_In_Month(year, month);
    const int32_t last = mtb_Kit_Days_From_Civil(year, month, days);
    return last - (mtb_Kit_Weekday(year, month, days) + 1) % 7;
}

static int pfx_Year_Of(uint32_t t){
    char date[16];
    pfx_Date(date, sizeof(date), t);
    return atoi(date);
}

int32_t pfx_City_Offset(uint8_t s, uint32_t now){
    const int y = pfx_Year_Of(now);
    const int64_t t = now;
    switch (s){
        case PFX_TOKYO: return 9 * 3600;
        case PFX_LONDON: {
            // Last Sunday of March to the last Sunday of October, at 01:00 UTC.
            const int64_t on = (int64_t)pfx_Sunday(y, 3, -1) * 86400 + 3600, off = (int64_t)pfx_Sunday(y, 10, -1) * 86400 + 3600;
            return (t >= on && t < off) ? 3600 : 0;
        }
        case PFX_NEW_YORK: {
            // Second Sunday of March to the first Sunday of November, at 02:00 local.
            const int64_t on = (int64_t)pfx_Sunday(y, 3, 2) * 86400 + 7 * 3600, off = (int64_t)pfx_Sunday(y, 11, 1) * 86400 + 6 * 3600;
            return (t >= on && t < off) ? -4 * 3600 : -5 * 3600;
        }
        default: {
            // Sydney: summer time from the first Sunday of October to the first Sunday of April, at 02:00/03:00 local.
            const int64_t off = (int64_t)pfx_Sunday(y, 4, 1) * 86400 + 3 * 3600 - 11 * 3600;
            const int64_t on = (int64_t)pfx_Sunday(y, 10, 1) * 86400 + 2 * 3600 - 10 * 3600;
            return (t < off || t >= on) ? 11 * 3600 : 10 * 3600;
        }
    }
}

const char* pfx_Session_Name(uint8_t s){
    static const char* const NAMES[PFX_SESSIONS] = {"SYDNEY", "TOKYO", "LONDON", "NEW YORK"};
    return s < PFX_SESSIONS ? NAMES[s] : "";
}

Pfx_Session_Times_t pfx_Session(uint8_t s, uint32_t now){
    const int32_t offset = pfx_City_Offset(s, now);
    const int openHour = s == PFX_TOKYO ? 9 : 8;
    const int64_t localDay = ((int64_t)now + offset) / 86400;
    // Today's session in that city; if it is over, tomorrow's.
    int64_t open = localDay * 86400 + openHour * 3600 - offset;
    if ((int64_t)now >= open + 9 * 3600) open += 86400;
    return {(uint32_t)open, (uint32_t)(open + 9 * 3600)};
}

bool pfx_Market_Open(uint32_t now){
    const int32_t offset = pfx_City_Offset(PFX_NEW_YORK, now);
    const int64_t local = (int64_t)now + offset;
    const int day = (int)(((local / 86400) % 7 + 7 + 3) % 7);    // 1970-01-01 was a Thursday: 0 Monday.
    const int hour = (int)((local % 86400) / 3600);
    if (day == 5) return false;                                   // Saturday
    if (day == 6) return hour >= 17;                              // Sunday from 17:00
    if (day == 4) return hour < 17;                               // Friday until 17:00
    return true;
}

bool pfx_Session_Open(uint8_t s, uint32_t now){
    if (!pfx_Market_Open(now)) return false;
    const Pfx_Session_Times_t t = pfx_Session(s, now);
    return now >= t.open && now < t.close;
}

uint32_t pfx_Until_Open(uint32_t now){
    if (pfx_Market_Open(now)) return 0;
    // Hour by hour to the next open: at most two days.
    uint32_t t = now - now % 3600 + 3600;
    while (!pfx_Market_Open(t) && t - now < 3 * 86400) t += 3600;
    return t - now;
}
