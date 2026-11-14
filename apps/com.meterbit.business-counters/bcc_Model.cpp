// Business & Creator Counters: settings, money, numbers and milestones. No Arduino (tested on a PC).
#include "bcc_Model.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char* const SOURCE_NAMES[BCC_SOURCES] = {"Stripe", "Paystack", "Shopify", "YouTube", "GitHub", "Plausible"};

const char* bcc_Source_Name(uint8_t source){ return source < BCC_SOURCES ? SOURCE_NAMES[source] : ""; }

void bcc_Settings_Default(Bcc_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = BCC_SETTINGS_MAGIC;
    strcpy(s.plHost, "plausible.io");
    s.chime = BCC_CHIME_ALL;
    s.showAmounts = 1;
    s.quietFrom = 22 * 60;
    s.quietTo = 7 * 60;
    s.cycle = 20;
    s.volume = 6;
}

static void terminate(char* text, size_t size){ text[size - 1] = '\0'; }

static void strip_Controls(char* text){
    char* w = text;
    for (const char* r = text; *r; r++) if ((unsigned char)*r >= ' ') *w++ = *r;
    *w = '\0';
}

void bcc_Settings_Clamp(Bcc_Settings_t& s){
    s.magic = BCC_SETTINGS_MAGIC;
    for (uint8_t& on : s.on) on = on ? 1 : 0;
    char* texts[] = {s.stripeKey, s.paystackKey, s.shopStore, s.shopClientId, s.shopSecret, s.ytKey, s.ytChannel, s.ghToken,
                     s.ghRepos[0], s.ghRepos[1], s.ghRepos[2], s.ghUser, s.plKey, s.plSite, s.plHost, s.goalCurrency};
    const size_t sizes[] = {sizeof(s.stripeKey), sizeof(s.paystackKey), sizeof(s.shopStore), sizeof(s.shopClientId), sizeof(s.shopSecret),
                            sizeof(s.ytKey), sizeof(s.ytChannel), sizeof(s.ghToken), sizeof(s.ghRepos[0]), sizeof(s.ghRepos[1]),
                            sizeof(s.ghRepos[2]), sizeof(s.ghUser), sizeof(s.plKey), sizeof(s.plSite), sizeof(s.plHost), sizeof(s.goalCurrency)};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++){ terminate(texts[i], sizes[i]); strip_Controls(texts[i]); }
    if (!s.plHost[0]) strcpy(s.plHost, "plausible.io");
    for (char* c = s.goalCurrency; *c; c++) *c = (char)toupper((unsigned char)*c);
    if (s.goalCurrency[0] && strlen(s.goalCurrency) != 3) s.goalCurrency[0] = '\0';
    if (s.goal > 1000000000u) s.goal = 1000000000u;
    if (s.chime > BCC_CHIME_ALL) s.chime = BCC_CHIME_ALL;
    s.announce = s.announce ? 1 : 0;
    s.showAmounts = s.showAmounts ? 1 : 0;
    s.quietOn = s.quietOn ? 1 : 0;
    if (s.quietFrom >= 1440) s.quietFrom = 22 * 60;
    if (s.quietTo >= 1440) s.quietTo = 7 * 60;
    if (s.cycle != 0 && s.cycle < 5) s.cycle = 5;
    if (s.cycle > 120) s.cycle = 120;
    if (s.volume < 1 || s.volume > 10) s.volume = 6;
}

bool bcc_Source_Ready(const Bcc_Settings_t& s, uint8_t source){
    if (source >= BCC_SOURCES || !s.on[source]) return false;
    switch (source){
        case BCC_STRIPE: return s.stripeKey[0];
        case BCC_PAYSTACK: return s.paystackKey[0];
        case BCC_SHOPIFY: return s.shopStore[0] && s.shopSecret[0];
        case BCC_YOUTUBE: return s.ytKey[0] && s.ytChannel[0];
        case BCC_GITHUB: return s.ghRepos[0][0] || s.ghRepos[1][0] || s.ghRepos[2][0] || s.ghUser[0];
        case BCC_PLAUSIBLE: return s.plKey[0] && s.plSite[0];
        default: return false;
    }
}

static bool name_Char(char c){ return isalnum((unsigned char)c) || c == '-'; }

bool bcc_Shop_Domain(const char* text, char* dest, size_t size){
    if (!text || size < 2) return false;
    while (*text == ' ') text++;
    if (!strncasecmp(text, "https://", 8)) text += 8;
    else if (!strncasecmp(text, "http://", 7)) text += 7;
    char name[64];
    size_t n = 0;
    while (text[n] && text[n] != '.' && text[n] != '/' && text[n] != ' ' && n + 1 < sizeof(name)){
        if (!name_Char(text[n])) return false;
        name[n] = (char)tolower((unsigned char)text[n]);
        n++;
    }
    name[n] = '\0';
    if (n == 0 || n > 60 || name[0] == '-') return false;
    // Only a myshopify.com address (or the bare name): a shop's own domain does not serve the Admin API.
    const char* rest = text + n;
    if (*rest == '.' && strncasecmp(rest, ".myshopify.com", 14) != 0) return false;
    if (snprintf(dest, size, "%s.myshopify.com", name) >= (int)size) return false;
    return true;
}

bool bcc_Repo_Name(const char* text, char* dest, size_t size){
    if (!text) return false;
    while (*text == ' ') text++;
    const char* p = strstr(text, "github.com/");
    if (p) text = p + 11;
    char owner[40], repo[64];
    size_t a = 0, b = 0;
    while (*text && *text != '/' && a + 1 < sizeof(owner)){
        if (!name_Char(*text)) return false;
        owner[a++] = *text++;
    }
    owner[a] = '\0';
    if (*text != '/' || a == 0) return false;
    text++;
    while (*text && *text != '/' && *text != ' ' && *text != '#' && *text != '?' && b + 1 < sizeof(repo)){
        if (!name_Char(*text) && *text != '.' && *text != '_') return false;
        repo[b++] = *text++;
    }
    repo[b] = '\0';
    if (b > 4 && !strcasecmp(repo + b - 4, ".git")) repo[b -= 4] = '\0';
    if (b == 0) return false;
    return snprintf(dest, size, "%s/%s", owner, repo) < (int)size;
}

bool bcc_Repos_Parse(const char* text, char repos[BCC_REPOS][64]){
    char out[BCC_REPOS][64] = {};
    int n = 0;
    char copy[260];
    snprintf(copy, sizeof(copy), "%s", text ? text : "");
    char* save = nullptr;
    for (char* t = strtok_r(copy, ",; \n", &save); t; t = strtok_r(nullptr, ",; \n", &save)){
        if (n >= BCC_REPOS || !bcc_Repo_Name(t, out[n], sizeof(out[n]))) return false;
        n++;
    }
    memcpy(repos, out, sizeof(out));
    return true;
}

// Money -----------------------------------------------------------------------------------------------------------
uint8_t bcc_Decimals(const char* code){
    static const char* const ZERO[] = {"BIF", "CLP", "DJF", "GNF", "ISK", "JPY", "KMF", "KRW", "MGA", "PYG", "RWF", "UGX", "VND", "VUV",
                                       "XAF", "XOF", "XPF"};
    static const char* const THREE[] = {"BHD", "JOD", "KWD", "OMR", "TND"};
    if (!code) return 2;
    for (const char* z : ZERO) if (!strcasecmp(code, z)) return 0;
    for (const char* t : THREE) if (!strcasecmp(code, t)) return 3;
    return 2;
}

bool bcc_Money_Add(Bcc_Money_t* list, uint8_t& count, const char* code, int64_t minor){
    char c[4] = "";
    for (int i = 0; i < 3 && code && code[i]; i++) c[i] = (char)toupper((unsigned char)code[i]);
    if (strlen(c) != 3) return false;
    for (uint8_t i = 0; i < count; i++) if (!strcmp(list[i].code, c)){ list[i].minor += minor; return true; }
    if (count >= BCC_CURRENCIES) return false;
    memcpy(list[count].code, c, 4);
    list[count].minor = minor;
    count++;
    return true;
}

bool bcc_Parse_Amount(const char* text, const char* code, int64_t& minor){
    if (!text) return false;
    while (*text == ' ') text++;
    bool negative = false;
    if (*text == '-'){ negative = true; text++; }
    const uint8_t decimals = bcc_Decimals(code);
    int64_t whole = 0, fraction = 0;
    int digits = 0, fractionDigits = 0;
    for (; isdigit((unsigned char)*text); text++, digits++){
        if (whole > 100000000000000LL) return false;
        whole = whole * 10 + (*text - '0');
    }
    if (*text == '.'){
        text++;
        for (; isdigit((unsigned char)*text); text++){
            if (fractionDigits < decimals){ fraction = fraction * 10 + (*text - '0'); fractionDigits++; }
            digits++;
        }
    }
    if (digits == 0 || *text != '\0') return false;
    while (fractionDigits < decimals){ fraction *= 10; fractionDigits++; }
    int64_t scale = 1;
    for (uint8_t i = 0; i < decimals; i++) scale *= 10;
    minor = whole * scale + fraction;
    if (negative) minor = -minor;
    return true;
}

// "1234567" -> "1,234,567"
static void commas(char* dest, size_t size, uint64_t value){
    char digits[24];
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    const size_t n = strlen(digits);
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < size; i++){
        if (i && (n - i) % 3 == 0 && k + 2 < size) dest[k++] = ',';
        dest[k++] = digits[i];
    }
    dest[k] = '\0';
}

// 123456 -> "123K", 1234567 -> "1.23M"
static void compact(char* dest, size_t size, uint64_t value){
    static const char UNITS[] = {'K', 'M', 'B', 'T'};
    double v = (double)value;
    int u = -1;
    while (v >= 1000.0 && u < 3){ v /= 1000.0; u++; }
    if (u < 0){ snprintf(dest, size, "%llu", (unsigned long long)value); return; }
    // Rounding up to the next unit ("999.95K" is "1.00M").
    if (v >= 999.5 && u < 3){ v /= 1000.0; u++; }
    if (v < 10) snprintf(dest, size, "%.2f%c", v, UNITS[u]);
    else if (v < 100) snprintf(dest, size, "%.1f%c", v, UNITS[u]);
    else snprintf(dest, size, "%.0f%c", v, UNITS[u]);
}

void bcc_Format_Money(char* dest, size_t size, const char* code, int64_t minor, bool compactForm){
    const uint8_t decimals = bcc_Decimals(code);
    uint64_t scale = 1;
    for (uint8_t i = 0; i < decimals; i++) scale *= 10;
    const bool negative = minor < 0;
    const uint64_t a = (uint64_t)(negative ? -minor : minor);
    const uint64_t whole = a / scale, cents = a % scale;
    char number[32];
    if (compactForm && whole >= 100000) compact(number, sizeof(number), whole);
    else {
        commas(number, sizeof(number), whole);
        if (decimals && whole < 10000 && !compactForm){
            char tail[8];
            snprintf(tail, sizeof(tail), ".%0*llu", (int)decimals, (unsigned long long)cents);
            strncat(number, tail, sizeof(number) - strlen(number) - 1);
        }
    }
    const bool dollar = code && !strcasecmp(code, "USD");
    if (dollar) snprintf(dest, size, "%s$%s", negative ? "-" : "", number);
    else snprintf(dest, size, "%s%s%s", code ? code : "", negative ? " -" : " ", number);
}

void bcc_Format_Count(char* dest, size_t size, int64_t value, bool compactForm){
    const bool negative = value < 0;
    const uint64_t a = (uint64_t)(negative ? -value : value);
    char number[24];
    if (compactForm && a >= 10000) compact(number, sizeof(number), a);
    else commas(number, sizeof(number), a);
    snprintf(dest, size, "%s%s", negative ? "-" : "", number);
}

void bcc_Speak_Money(char* dest, size_t size, const char* code, int64_t minor){
    static const struct { const char* code; const char* one; const char* many; const char* cent; } W[] = {
        {"USD", "dollar", "dollars", "cents"}, {"CAD", "Canadian dollar", "Canadian dollars", "cents"},
        {"AUD", "Australian dollar", "Australian dollars", "cents"}, {"EUR", "euro", "euros", "cents"}, {"GBP", "pound", "pounds", "pence"},
        {"NGN", "naira", "naira", "kobo"}, {"GHS", "cedi", "cedis", "pesewas"}, {"KES", "shilling", "shillings", "cents"},
        {"ZAR", "rand", "rand", "cents"}, {"INR", "rupee", "rupees", "paise"}, {"JPY", "yen", "yen", ""}, {"XOF", "franc", "francs", ""}};
    const uint8_t decimals = bcc_Decimals(code);
    int64_t scale = 1;
    for (uint8_t i = 0; i < decimals; i++) scale *= 10;
    const int64_t a = minor < 0 ? -minor : minor;
    const long long whole = (long long)(a / scale), part = (long long)(a % scale);
    const char *one = nullptr, *many = nullptr;
    for (const auto& w : W) if (code && !strcasecmp(code, w.code)){ one = w.one; many = w.many; }
    if (!one){
        // An unknown currency: its letters, spaced, read one by one.
        char letters[8] = "";
        if (code && strlen(code) == 3) snprintf(letters, sizeof(letters), "%c %c %c", code[0], code[1], code[2]);
        if (part) snprintf(dest, size, "%lld point %0*lld %s", whole, (int)decimals, part, letters);
        else snprintf(dest, size, "%lld %s", whole, letters);
        return;
    }
    if (part && decimals) snprintf(dest, size, "%lld %s %lld", whole, whole == 1 ? one : many, part);
    else snprintf(dest, size, "%lld %s", whole, whole == 1 ? one : many);
}

// The day ---------------------------------------------------------------------------------------------------------
void bcc_Total(const Bcc_Settings_t& s, const Bcc_Sales_t* shops, uint32_t today, Bcc_Total_t& t){
    memset(&t, 0, sizeof(t));
    t.main = -1;
    t.goalFraction = -1;
    if (s.goalCurrency[0]) bcc_Money_Add(t.money, t.moneyCount, s.goalCurrency, 0);
    for (int i = 0; i < BCC_SHOPS; i++){
        const Bcc_Sales_t& shop = shops[i];
        if (!bcc_Source_Ready(s, (uint8_t)i) || shop.day != today || !shop.updated) continue;
        t.count += shop.count;
        for (uint8_t m = 0; m < shop.moneyCount; m++) bcc_Money_Add(t.money, t.moneyCount, shop.money[m].code, shop.money[m].minor);
    }
    if (t.moneyCount) t.main = 0;       // the goal's currency if set (added first), else the first shop's
    if (t.main >= 0 && s.goal){
        uint64_t scale = 1;
        for (uint8_t i = 0; i < bcc_Decimals(t.money[0].code); i++) scale *= 10;
        t.goalFraction = (float)((double)t.money[0].minor / ((double)s.goal * (double)scale));
        if (t.goalFraction < 0) t.goalFraction = 0;
    }
}

int64_t bcc_Milestone(int64_t before, int64_t after){
    if (after <= before || after < 10) return 0;
    int64_t found = 0;
    for (int64_t base = 10; base > 0 && base <= after; base *= 10){
        const int64_t steps[3] = {base, base * 5 / 2, base * 5};
        for (int64_t m : steps) if (m > before && m <= after) found = m;
        if (base > 100000000000000000LL) break;
    }
    return found;
}

void bcc_Log_Clear(Bcc_Log_t& log){
    memset(&log, 0, sizeof(log));
    log.magic = BCC_SETTINGS_MAGIC;
    log.seenSubs = -1;
    log.seenStars = log.seenFollowers = 0xFFFFFFFFu;
}

bool bcc_In_Window(uint16_t minute, uint16_t from, uint16_t to){
    if (from == to) return false;
    if (from < to) return minute >= from && minute < to;
    return minute >= from || minute < to;
}
