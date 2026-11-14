#include "cfx_Model.h"
#include <ctype.h>
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const Cfx_Currency_t CATALOGUE[] = {
    {"USD", "US DOLLAR"}, {"EUR", "EURO"}, {"GBP", "BRITISH POUND"}, {"JPY", "JAPANESE YEN"}, {"CNY", "CHINESE YUAN"},
    {"CHF", "SWISS FRANC"}, {"CAD", "CANADIAN DOLLAR"}, {"AUD", "AUSTRALIAN DOLLAR"}, {"NZD", "NEW ZEALAND DOLLAR"},
    {"NGN", "NIGERIAN NAIRA"}, {"GHS", "GHANAIAN CEDI"}, {"KES", "KENYAN SHILLING"}, {"ZAR", "SOUTH AFRICAN RAND"},
    {"EGP", "EGYPTIAN POUND"}, {"MAD", "MOROCCAN DIRHAM"}, {"XOF", "WEST AFRICAN CFA"}, {"XAF", "CENTRAL AFRICAN CFA"},
    {"ETB", "ETHIOPIAN BIRR"}, {"TZS", "TANZANIAN SHILLING"}, {"UGX", "UGANDAN SHILLING"}, {"RWF", "RWANDAN FRANC"},
    {"ZMW", "ZAMBIAN KWACHA"}, {"BWP", "BOTSWANA PULA"}, {"MUR", "MAURITIAN RUPEE"}, {"DZD", "ALGERIAN DINAR"},
    {"TND", "TUNISIAN DINAR"}, {"INR", "INDIAN RUPEE"}, {"PKR", "PAKISTANI RUPEE"}, {"BDT", "BANGLADESHI TAKA"},
    {"LKR", "SRI LANKAN RUPEE"}, {"HKD", "HONG KONG DOLLAR"}, {"SGD", "SINGAPORE DOLLAR"}, {"KRW", "SOUTH KOREAN WON"},
    {"TWD", "TAIWAN DOLLAR"}, {"THB", "THAI BAHT"}, {"MYR", "MALAYSIAN RINGGIT"}, {"IDR", "INDONESIAN RUPIAH"},
    {"PHP", "PHILIPPINE PESO"}, {"VND", "VIETNAMESE DONG"}, {"AED", "UAE DIRHAM"}, {"SAR", "SAUDI RIYAL"},
    {"QAR", "QATARI RIYAL"}, {"KWD", "KUWAITI DINAR"}, {"BHD", "BAHRAINI DINAR"}, {"OMR", "OMANI RIAL"},
    {"JOD", "JORDANIAN DINAR"}, {"ILS", "ISRAELI SHEKEL"}, {"TRY", "TURKISH LIRA"}, {"RUB", "RUSSIAN RUBLE"},
    {"UAH", "UKRAINIAN HRYVNIA"}, {"PLN", "POLISH ZLOTY"}, {"CZK", "CZECH KORUNA"}, {"HUF", "HUNGARIAN FORINT"},
    {"RON", "ROMANIAN LEU"}, {"SEK", "SWEDISH KRONA"}, {"NOK", "NORWEGIAN KRONE"}, {"DKK", "DANISH KRONE"},
    {"ISK", "ICELANDIC KRONA"}, {"MXN", "MEXICAN PESO"}, {"BRL", "BRAZILIAN REAL"}, {"ARS", "ARGENTINE PESO"},
    {"CLP", "CHILEAN PESO"}, {"COP", "COLOMBIAN PESO"}, {"PEN", "PERUVIAN SOL"}, {"JMD", "JAMAICAN DOLLAR"},
    {"TTD", "TRINIDAD DOLLAR"},
};

const Cfx_Currency_t* cfx_Catalogue(size_t& count){ count = sizeof(CATALOGUE) / sizeof(CATALOGUE[0]); return CATALOGUE; }

const char* cfx_Name(const char* code){
    for (const Cfx_Currency_t& c : CATALOGUE) if (strcmp(c.code, code) == 0) return c.name;
    return code;
}

bool cfx_Is_Code(const char* code){
    if (code == nullptr || strlen(code) != 3) return false;
    for (int i = 0; i < 3; i++) if (code[i] < 'A' || code[i] > 'Z') return false;
    return true;
}

//##############################################################################################################
uint8_t cfx_Error_Of(const char* type){
    if (type == nullptr) return CFX_ERR_OTHER;
    if (strcmp(type, "invalid-key") == 0) return CFX_ERR_KEY;
    if (strcmp(type, "inactive-account") == 0) return CFX_ERR_INACTIVE;
    if (strcmp(type, "quota-reached") == 0) return CFX_ERR_QUOTA;
    if (strcmp(type, "unsupported-code") == 0) return CFX_ERR_CODE;
    if (strcmp(type, "plan-upgrade-required") == 0) return CFX_ERR_PLAN;
    if (strcmp(type, "no-data-available") == 0) return CFX_ERR_NO_DATA;
    return CFX_ERR_OTHER;
}

static uint8_t cfx_Result(JsonVariantConst doc){
    const char* result = doc["result"];
    if (result && strcmp(result, "success") == 0) return CFX_ERR_NONE;
    return cfx_Error_Of(doc["error-type"]);
}

uint8_t cfx_Parse_Latest(JsonVariantConst doc, Cfx_Data_t& data){
    const uint8_t error = cfx_Result(doc);
    if (error != CFX_ERR_NONE) return error;
    JsonObjectConst rates = doc["conversion_rates"].is<JsonObjectConst>() ? doc["conversion_rates"].as<JsonObjectConst>() : doc["rates"].as<JsonObjectConst>();
    const char* base = doc["base_code"];
    if (rates.isNull() || base == nullptr || !cfx_Is_Code(base)) return CFX_ERR_OTHER;
    uint16_t count = 0;
    for (JsonPairConst pair : rates){
        if (count >= CFX_MAX_RATES) break;
        const char* code = pair.key().c_str();
        const float rate = pair.value().as<float>();
        if (!cfx_Is_Code(code) || !(rate > 0)) continue;
        memcpy(data.codes[count], code, 4);
        data.rates[count] = rate;
        count++;
    }
    if (count == 0) return CFX_ERR_OTHER;
    snprintf(data.base, sizeof(data.base), "%s", base);
    data.rateCount = count;
    data.lastUpdate = doc["time_last_update_unix"] | 0UL;
    data.nextUpdate = doc["time_next_update_unix"] | 0UL;
    return CFX_ERR_NONE;
}

uint8_t cfx_Parse_History(JsonVariantConst doc, const char (*watch)[4], uint8_t watchCount, float* rates, uint32_t& day){
    const uint8_t error = cfx_Result(doc);
    if (error != CFX_ERR_NONE) return error;
    JsonObjectConst table = doc["conversion_rates"];
    const int y = doc["year"] | 0, m = doc["month"] | 0, d = doc["day"] | 0;
    if (table.isNull() || y < 1990 || m < 1 || m > 12 || d < 1 || d > 31) return CFX_ERR_OTHER;
    // Days since 1970 of that date.
    int yy = y - (m <= 2);
    const int era = yy / 400;
    const unsigned yoe = (unsigned)(yy - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    day = (uint32_t)(era * 146097 + (int)(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468);
    for (uint8_t i = 0; i < watchCount; i++) rates[i] = table[watch[i]] | 0.0f;
    return CFX_ERR_NONE;
}

uint8_t cfx_Parse_Quota(JsonVariantConst doc, Cfx_Quota_t& quota){
    const uint8_t error = cfx_Result(doc);
    if (error != CFX_ERR_NONE) return error;
    quota.planQuota = doc["plan_quota"] | 0UL;
    quota.remaining = doc["requests_remaining"] | 0UL;
    quota.refreshDay = doc["refresh_day_of_month"] | 0;
    quota.exhausted = quota.remaining == 0;
    return quota.planQuota ? CFX_ERR_NONE : CFX_ERR_OTHER;
}

uint8_t cfx_Plan_Of_Quota(uint32_t planQuota){
    if (planQuota == 0) return CFX_PLAN_UNKNOWN;
    if (planQuota <= 1500) return CFX_PLAN_FREE;
    if (planQuota <= 30000) return CFX_PLAN_PRO;
    if (planQuota <= 125000) return CFX_PLAN_BUSINESS;
    return CFX_PLAN_VOLUME;
}

const char* cfx_Plan_Name(uint8_t plan){
    static const char* const NAMES[] = {"No key", "Checking", "Free", "Pro", "Business", "Volume"};
    return plan <= CFX_PLAN_VOLUME ? NAMES[plan] : "Unknown";
}

float cfx_Rate(const Cfx_Data_t& data, const char* code){
    if (strcmp(code, data.base) == 0) return 1;
    for (uint16_t i = 0; i < data.rateCount; i++) if (strcmp(data.codes[i], code) == 0) return data.rates[i];
    return 0;
}

//##############################################################################################################
static Cfx_History_t* cfx_History_Slot(Cfx_Data_t& data, const char* code){
    for (Cfx_History_t& h : data.history) if (strcmp(h.code, code) == 0) return &h;
    for (Cfx_History_t& h : data.history) if (h.code[0] == '\0'){ memset(&h, 0, sizeof(h)); memcpy(h.code, code, 4); return &h; }
    return nullptr;
}

const Cfx_History_t* cfx_History_Of(const Cfx_Data_t& data, const char* code){
    for (const Cfx_History_t& h : data.history) if (strcmp(h.code, code) == 0) return &h;
    return nullptr;
}

void cfx_History_Put(Cfx_Data_t& data, const char* code, uint32_t day, float rate){
    if (!(rate > 0)) return;
    Cfx_History_t* h = cfx_History_Slot(data, code);
    if (h == nullptr) return;
    // In day order: replace that day's sample, or insert, dropping the oldest when full.
    int at = h->count;
    for (int i = 0; i < h->count; i++){
        if (h->day[i] == day){ h->rate[i] = rate; return; }
        if (h->day[i] > day){ at = i; break; }
    }
    if (h->count == CFX_HISTORY_DAYS){
        if (at == 0) return;            // Older than all kept.
        memmove(&h->day[0], &h->day[1], sizeof(uint32_t) * (CFX_HISTORY_DAYS - 1));
        memmove(&h->rate[0], &h->rate[1], sizeof(float) * (CFX_HISTORY_DAYS - 1));
        h->count--;
        at--;
    }
    memmove(&h->day[at + 1], &h->day[at], sizeof(uint32_t) * (h->count - at));
    memmove(&h->rate[at + 1], &h->rate[at], sizeof(float) * (h->count - at));
    h->day[at] = day;
    h->rate[at] = rate;
    h->count++;
}

void cfx_History_Keep(Cfx_Data_t& data, const char (*watch)[4], uint8_t watchCount){
    for (Cfx_History_t& h : data.history){
        bool watched = false;
        for (uint8_t i = 0; i < watchCount; i++) if (strcmp(h.code, watch[i]) == 0) watched = true;
        if (!watched) memset(&h, 0, sizeof(h));
    }
}

float cfx_Rate_Days_Ago(const Cfx_History_t& h, uint32_t newestDay, int days){
    if (h.count == 0) return 0;
    const uint32_t target = newestDay - (uint32_t)days;
    // The nearest sample at or before the target, if within two days of it (weekends have no new rates).
    for (int i = h.count - 1; i >= 0; i--){
        if (h.day[i] <= target) return target - h.day[i] <= 2 ? h.rate[i] : 0;
    }
    return 0;
}

//##############################################################################################################
static void cfx_Group(char* dest, size_t size, const char* digits){
    // "1234567" -> "1,234,567"
    const size_t n = strlen(digits);
    size_t used = 0;
    for (size_t i = 0; i < n && used + 2 < size; i++){
        if (i > 0 && (n - i) % 3 == 0) dest[used++] = ',';
        dest[used++] = digits[i];
    }
    dest[used] = '\0';
}

void cfx_Format_Rate(char* dest, size_t size, double value){
    if (!(value > 0) || !std::isfinite(value)){ snprintf(dest, size, "-"); return; }
    if (value >= 1000){
        char whole[24], grouped[32];
        const double rounded = floor(value * 100 + 0.5) / 100;
        snprintf(whole, sizeof(whole), "%.0f", floor(rounded));
        cfx_Group(grouped, sizeof(grouped), whole);
        if (value >= 100000) snprintf(dest, size, "%s", grouped);
        else snprintf(dest, size, "%s.%02d", grouped, (int)lround((rounded - floor(rounded)) * 100) % 100);
        return;
    }
    if (value >= 1){ snprintf(dest, size, "%.4f", value); return; }
    // Four significant digits.
    const int decimals = 3 - (int)floor(log10(value));
    snprintf(dest, size, "%.*f", decimals > 10 ? 10 : decimals, value);
}

void cfx_Format_Change(char* dest, size_t size, double percent){
    if (fabs(percent) < 0.005) snprintf(dest, size, "0.00%%");
    else snprintf(dest, size, "%+.2f%%", percent);
}
