#include "opa_Model.h"
#include "opa_Config.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The codes the phone offers (opa_Mobile_Ui.h lists the same ones). The free plan and the keyless demo cover all
// but those marked: a plan without one leaves it out of the answer and the board shows it as "PLAN".
static const Opa_Commodity_t CATALOGUE[] = {
    {"BRENT_CRUDE_USD",          "BRENT",    "Brent crude",                  "$/bbl"},
    {"WTI_USD",                  "WTI",      "WTI crude",                    "$/bbl"},
    {"DUBAI_CRUDE_USD",          "DUBAI",    "Dubai crude",                  "$/bbl"},
    {"OPEC_BASKET_USD",          "OPEC BSK", "OPEC reference basket",        "$/bbl"},
    {"URALS_CRUDE_USD",          "URALS",    "Urals crude",                  "$/bbl"},
    {"WCS_CRUDE_USD",            "WCS",      "Western Canadian Select",      "$/bbl"},
    {"AZERI_LIGHT_USD",          "AZERI",    "Azeri Light",                  "$/bbl"},
    {"BASRAH_MEDIUM_USD",        "BASRAH M", "Basrah Medium",                "$/bbl"},
    {"BONNY_LIGHT_USD",          "BONNY LT", "Bonny Light (Nigeria)",        "$/bbl"},  // Paid plans
    {"NATURAL_GAS_USD",          "HH GAS",   "Henry Hub natural gas",        "$/MMBtu"},
    {"NATURAL_GAS_GBP",          "UK GAS",   "UK natural gas (NBP)",         "p/thm"},
    {"DUTCH_TTF_EUR",            "TTF GAS",  "Dutch TTF natural gas",        "EUR/MWh"},    // Paid plans
    {"JKM_LNG_USD",              "JKM LNG",  "JKM LNG, Asia",                "$/MMBtu"},    // Paid plans
    {"GASOLINE_RBOB_USD",        "RBOB",     "RBOB gasoline",                "$/gal"},
    {"GASOLINE_USD",             "GASOLINE", "Gasoline",                     "$/gal"},
    {"DIESEL_USD",               "DIESEL",   "Ultra low sulfur diesel",      "$/gal"},
    {"HEATING_OIL_USD",          "HEAT OIL", "Heating oil (NY Harbor ULSD)", "$/gal"},
    {"DIESEL_RETAIL_USD",        "DSL RETL", "US retail diesel",             "$/gal"},
    {"JET_FUEL_USD",             "JET",      "Jet fuel",                     "$/gal"},      // Paid plans
    {"PROPANE_MONT_BELVIEU_USD", "PROPANE",  "Propane, Mont Belvieu",        "$/gal"},
    {"COAL_USD",                 "COAL",     "Thermal coal",                 "$/t"},
    {"NEWCASTLE_COAL_USD",       "NEWC CL",  "Newcastle coal",               "$/t"},
    {"EU_CARBON_EUR",            "EU CO2",   "EU carbon allowance",          "EUR/t"},
    {"UK_CARBON_GBP",            "UK CO2",   "UK carbon allowance",          "GBP/t"},
    {"GOLD_USD",                 "GOLD",     "Gold",                         "$/oz"},
    {"SILVER_USD",               "SILVER",   "Silver",                       "$/oz"},
    {"EUR_USD",                  "EUR/USD",  "Euro in US dollars",           ""},
    {"GBP_USD",                  "GBP/USD",  "Pound in US dollars",          ""},
};

const Opa_Commodity_t* opa_Catalogue(size_t& count){
    count = sizeof(CATALOGUE) / sizeof(CATALOGUE[0]);
    return CATALOGUE;
}

const Opa_Commodity_t* opa_Find_Commodity(const char* code){
    if (code == nullptr) return nullptr;
    for (const Opa_Commodity_t& c : CATALOGUE) if (strcasecmp(c.code, code) == 0) return &c;
    return nullptr;
}

const char* opa_Label(const char* code){
    const Opa_Commodity_t* c = opa_Find_Commodity(code);
    return c ? c->label : code;
}

//##############################################################################################################
int32_t opa_Days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static bool opa_Digits(const char* text, int count, int& value){
    value = 0;
    for (int i = 0; i < count; i++){
        if (!isdigit((unsigned char)text[i])) return false;
        value = value * 10 + (text[i] - '0');
    }
    return true;
}

uint32_t opa_Parse_Iso_Time(const char* text){
    if (text == nullptr) return 0;
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (!opa_Digits(text, 4, y) || text[4] != '-' || !opa_Digits(text + 5, 2, mo) || text[7] != '-' || !opa_Digits(text + 8, 2, d)) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 1970) return 0;
    if (text[10] == 'T' || text[10] == ' '){
        if (!opa_Digits(text + 11, 2, h) || text[13] != ':' || !opa_Digits(text + 14, 2, mi)) return 0;
        if (text[16] == ':' && !opa_Digits(text + 17, 2, s)) return 0;
        // Times are UTC ("Z"); an explicit offset is applied.
        const char* zone = text + 16;
        while (*zone && *zone != 'Z' && *zone != '+' && !(*zone == '-' && zone > text + 16)) zone++;
        int64_t t = (int64_t)opa_Days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
        int zh, zm;
        if ((*zone == '+' || *zone == '-') && opa_Digits(zone + 1, 2, zh) && zone[3] == ':' && opa_Digits(zone + 4, 2, zm)){
            t -= (*zone == '+' ? 1 : -1) * (zh * 3600 + zm * 60);
        }
        return t > 0 ? (uint32_t)t : 0;
    }
    return (uint32_t)((int64_t)opa_Days_From_Civil(y, mo, d) * 86400);
}

int opa_Find_Quote(const Opa_Data_t& data, const char* code){
    for (int i = 0; i < data.quoteCount && i < OPA_MAX_WATCH; i++){
        if (strcasecmp(data.quotes[i].code, code) == 0) return i;
    }
    return -1;
}

//##############################################################################################################
void opa_Quote_Put(Opa_Quote_t& quote, float price, uint32_t observed, uint32_t fetched){
    quote.price = price;
    quote.observed = observed;
    quote.fetched = fetched;
    if (isnan(price) || observed == 0) return;
    if (quote.localCount > 0 && observed <= quote.local[quote.localCount - 1].t) return;   // The same price again, or older.
    // The newest sample always follows the price; it stays as a sample once it is OPA_LOCAL_GAP_SEC after the one
    // before it, and the next price starts a new one.
    if (quote.localCount >= 2 && quote.local[quote.localCount - 1].t - quote.local[quote.localCount - 2].t < OPA_LOCAL_GAP_SEC){
        quote.local[quote.localCount - 1] = {observed, price};
        return;
    }
    if (quote.localCount >= OPA_LOCAL_POINTS){
        memmove(quote.local, quote.local + 1, sizeof(Opa_Point_t) * (OPA_LOCAL_POINTS - 1));
        quote.localCount = OPA_LOCAL_POINTS - 1;
    }
    quote.local[quote.localCount++] = {observed, price};
}

// The price at about @p when, from points oldest first: the newest point at or before it, if that point is not more
// than a fifth of the window older. Returns NAN when there is none.
static float opa_Price_At(const Opa_Point_t* points, int count, uint32_t when, uint32_t window){
    for (int i = count - 1; i >= 0; i--){
        if (points[i].t <= when) return (when - points[i].t <= window / 5) ? points[i].v : NAN;
    }
    return NAN;
}

static const uint32_t WINDOW_SEC[OPA_W_COUNT] = {86400UL, 7 * 86400UL, 30 * 86400UL, 90 * 86400UL};

bool opa_Quote_Change(const Opa_Data_t& data, const Opa_Quote_t& quote, uint8_t window, float& percent){
    percent = NAN;
    if (window >= OPA_W_COUNT || isnan(quote.price)) return false;
    if (!isnan(quote.change[window])){
        percent = quote.change[window];
        return true;
    }
    const uint32_t now = quote.observed;
    if (now < WINDOW_SEC[window]) return false;
    const uint32_t when = now - WINDOW_SEC[window];
    float before = NAN;
    if (strcasecmp(data.chart.code, quote.code) == 0) before = opa_Price_At(data.chart.points, data.chart.count, when, WINDOW_SEC[window]);
    if (isnan(before)) before = opa_Price_At(quote.local, quote.localCount, when, WINDOW_SEC[window]);
    if (isnan(before) || before == 0) return false;
    percent = (quote.price - before) / before * 100.0f;
    return true;
}

//##############################################################################################################
void opa_Unit_Text(char* dest, size_t size, const Opa_Quote_t& quote){
    const Opa_Commodity_t* known = opa_Find_Commodity(quote.code);
    if (quote.unit[0] == '\0'){
        snprintf(dest, size, "%s", known ? known->unitText : "");
        return;
    }
    static const struct { const char* name; const char* abbreviation; } UNITS[] = {
        {"barrel", "bbl"}, {"bbl", "bbl"}, {"gallon", "gal"}, {"gal", "gal"}, {"mmbtu", "MMBtu"}, {"mwh", "MWh"},
        {"therm", "thm"}, {"tonne", "t"}, {"ton", "t"}, {"metric_ton", "t"}, {"mt", "t"}, {"troy_ounce", "oz"},
        {"ounce", "oz"}, {"oz", "oz"}, {"liter", "l"}, {"litre", "l"},
    };
    const char* unit = nullptr;
    for (const auto& u : UNITS) if (strcasecmp(u.name, quote.unit) == 0){ unit = u.abbreviation; break; }
    char shortUnit[6];
    if (unit == nullptr){       // An unknown unit: its first five letters.
        memcpy(shortUnit, quote.unit, sizeof(shortUnit) - 1);
        shortUnit[sizeof(shortUnit) - 1] = '\0';
        unit = shortUnit;
    }
    // UK gas is quoted in pence ("GBp").
    const bool dollars = quote.currency[0] == '\0' || strcmp(quote.currency, "USD") == 0;
    const char* money = dollars ? "$" : strcmp(quote.currency, "GBp") == 0 ? "p" : quote.currency;
    snprintf(dest, size, "%s/%s", money, unit);
}

static int opa_Decimals(float reference){
    const float magnitude = fabsf(reference);
    return magnitude < 2 ? 4 : magnitude < 10 ? 3 : magnitude < 1000 ? 2 : 1;
}

void opa_Format_Price(char* dest, size_t size, float value){
    if (isnan(value)){ snprintf(dest, size, "--"); return; }
    snprintf(dest, size, "%.*f", opa_Decimals(value), value);
}

void opa_Format_Change(char* dest, size_t size, float change, float reference){
    if (isnan(change)){ snprintf(dest, size, "--"); return; }
    snprintf(dest, size, "%c%.*f", change < 0 ? '-' : '+', opa_Decimals(reference), fabsf(change));
}

void opa_Format_Percent(char* dest, size_t size, float percent){
    if (isnan(percent)) snprintf(dest, size, "--");
    else snprintf(dest, size, fabsf(percent) < 100 ? "%+.2f%%" : "%+.0f%%", percent);
}

//##############################################################################################################
// The quota in force now: the last one the server reported, or a fresh one if it has reset since, or the Free plan's
// if the server never said. Without a known reset time, the quota is spread over a whole period.
static void opa_Quota_Now(const Opa_Account_t& account, uint32_t now, uint32_t& limit, uint32_t& remaining, uint32_t& secondsLeft){
    const uint32_t period = (account.window == OPA_QUOTA_MONTH || account.window == OPA_QUOTA_TRIAL) ? 30UL * 86400 : 86400UL;
    if (account.seen == 0 || account.limit == 0){
        limit = remaining = OPA_FREE_LIMIT;
        secondsLeft = 86400;
        return;
    }
    limit = account.limit;
    remaining = account.remaining;
    if (account.reset > now){
        secondsLeft = account.reset - now;
    } else {
        // Reset since: a full quota again, until the next reset, a whole number of periods after the last one.
        remaining = limit;
        secondsLeft = account.reset != 0 ? period - (now - account.reset) % period : period;
    }
    if (account.exhausted && account.reset > now) remaining = 0;
    if (secondsLeft < 60) secondsLeft = 60;
}

uint32_t opa_Plan_Available(const Opa_Account_t& account, uint32_t now, uint8_t sharePercent){
    uint32_t limit, remaining, secondsLeft;
    opa_Quota_Now(account, now, limit, remaining, secondsLeft);
    const uint32_t keep = (uint32_t)((uint64_t)limit * (100 - sharePercent) / 100) + OPA_RESERVE_REQUESTS;
    return remaining > keep ? remaining - keep : 0;
}

uint32_t opa_Plan_Quote_Interval(const Opa_Account_t& account, uint32_t now, uint8_t sharePercent, uint16_t floorMinutes,
                                 uint32_t extrasPerDay, bool& exhausted){
    uint32_t limit, remaining, secondsLeft;
    opa_Quota_Now(account, now, limit, remaining, secondsLeft);
    uint32_t available = opa_Plan_Available(account, now, sharePercent);
    const uint32_t extras = (uint32_t)((uint64_t)extrasPerDay * secondsLeft / 86400 + 1);
    available -= (extras < available / 2) ? extras : available / 2;
    exhausted = available == 0;
    uint32_t interval = exhausted ? secondsLeft + 60 : secondsLeft / available;
    const uint32_t floor = floorMinutes * 60UL;
    if (interval < floor) interval = floor;
    if (!exhausted && interval > OPA_SLOWEST_SEC) interval = OPA_SLOWEST_SEC;
    if (exhausted && interval > OPA_WAIT_MAX_SEC) interval = OPA_WAIT_MAX_SEC;
    return interval;
}

bool opa_Plan_Is_Paid(const Opa_Account_t& account){
    if (account.seen == 0 || account.window == OPA_QUOTA_DEMO) return false;
    return account.window == OPA_QUOTA_MONTH || account.window == OPA_QUOTA_TRIAL || account.limit > OPA_FREE_LIMIT;
}
