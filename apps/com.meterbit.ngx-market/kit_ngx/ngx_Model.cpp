// NGX: stocks, formatting, movers, hours. No Arduino (tested on a PC).
#include "ngx_Model.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

int ngx_Find(const Ngx_Board_t& b, const char* symbol){
    if (!symbol || !symbol[0]) return -1;
    for (int i = 0; i < b.count; i++) if (!strcasecmp(b.stocks[i].symbol, symbol)) return i;
    return -1;
}

// Into a short list kept sorted (best first), at most @p max long.
static void keep(int* list, int& count, int max, const Ngx_Board_t& b, int i, bool up){
    const float p = b.stocks[i].changePct;
    auto better = [&](int other){ return up ? p > b.stocks[other].changePct : p < b.stocks[other].changePct; };
    if (count == max && !better(list[max - 1])) return;
    int k = count < max ? count++ : max - 1;
    while (k > 0 && better(list[k - 1])){ list[k] = list[k - 1]; k--; }
    list[k] = i;
}

void ngx_Movers(const Ngx_Board_t& b, int* g, int& gc, int* l, int& lc, int max){
    gc = lc = 0;
    if (max <= 0) return;
    for (int i = 0; i < b.count; i++){
        if (b.stocks[i].changePct > 0.0001f) keep(g, gc, max, b, i, true);
        else if (b.stocks[i].changePct < -0.0001f) keep(l, lc, max, b, i, false);
    }
}

static double size_Of(const Ngx_Stock_t& s){ return s.marketCap > 0 ? s.marketCap : s.price; }

int ngx_By_Size(const Ngx_Board_t& b, int* out, int max){
    int n = 0;
    for (int i = 0; i < b.count && n < max; i++) out[n++] = i;
    // Insertion sort: about 150 stocks.
    for (int i = 1; i < n; i++){
        const int k = out[i];
        int j = i - 1;
        while (j >= 0 && size_Of(b.stocks[out[j]]) < size_Of(b.stocks[k])){ out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return n;
}

void ngx_Breadth(const Ngx_Board_t& b, Ngx_Market_t& m){
    m.advancers = m.decliners = m.unchanged = 0;
    for (int i = 0; i < b.count; i++){
        const float p = b.stocks[i].changePct;
        if (p > 0.0001f) m.advancers++;
        else if (p < -0.0001f) m.decliners++;
        else m.unchanged++;
    }
}

static void commas(char* dest, size_t size, double whole){
    char digits[32];
    snprintf(digits, sizeof(digits), "%.0f", floor(whole));
    const size_t n = strlen(digits);
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < size; i++){
        if (i && (n - i) % 3 == 0 && k + 2 < size) dest[k++] = ',';
        dest[k++] = digits[i];
    }
    dest[k] = '\0';
}

static bool scaled(char* dest, size_t size, double a, const char* prefix){
    static const struct { double at; char unit; } U[] = {{1e12, 'T'}, {1e9, 'B'}, {1e6, 'M'}, {1e3, 'K'}};
    for (const auto& u : U){
        if (a >= u.at){
            const double v = a / u.at;
            snprintf(dest, size, v < 10 ? "%s%.2f%c" : v < 100 ? "%s%.1f%c" : "%s%.0f%c", prefix, v, u.unit);
            return true;
        }
    }
    return false;
}

void ngx_Naira(char* dest, size_t size, double value, bool compact){
    const bool negative = value < 0;
    const double a = fabs(value);
    char prefix[3] = "N";
    if (negative){ prefix[0] = '-'; prefix[1] = 'N'; prefix[2] = '\0'; }
    if (compact && a >= 100000 && scaled(dest, size, a, prefix)) return;
    if (a >= 100000){ char c[32]; commas(c, sizeof(c), a + 0.5); snprintf(dest, size, "%s%s", prefix, c); return; }
    char c[32];
    commas(c, sizeof(c), a);
    const int kobo = (int)lround((a - floor(a)) * 100);
    if (kobo == 100){ commas(c, sizeof(c), floor(a) + 1); snprintf(dest, size, "%s%s.00", prefix, c); return; }
    snprintf(dest, size, "%s%s.%02d", prefix, c, kobo);
}

void ngx_Count(char* dest, size_t size, double value){
    if (value >= 100000 && scaled(dest, size, value, "")) return;
    commas(dest, size, value);
}

void ngx_Percent(char* dest, size_t size, float pct){
    if (fabsf(pct) < 0.005f) snprintf(dest, size, "0.00%%");
    else snprintf(dest, size, "%+.2f%%", pct);
}

bool ngx_Trading_Time(int weekday, uint16_t minute){
    return weekday >= 1 && weekday <= 5 && minute >= 9 * 60 && minute < 16 * 60;
}

bool ngx_Symbol(const char* text, char* dest, size_t size){
    if (!text || size < 2) return false;
    while (*text == ' ') text++;
    size_t n = 0;
    for (; text[n] && text[n] != ' ' && n + 1 < size; n++){
        const char c = (char)toupper((unsigned char)text[n]);
        if (!(isupper((unsigned char)c) || isdigit((unsigned char)c) || c == '.' || c == '-')) return false;
        dest[n] = c;
    }
    dest[n] = '\0';
    for (const char* r = text + n; *r; r++) if (*r != ' ') return false;
    return n > 0 && n < 16;
}

int ngx_Parse_Watchlist(const char* text, char list[NGX_WATCH_MAX][16]){
    char out[NGX_WATCH_MAX][16] = {};
    int n = 0;
    char copy[260];
    snprintf(copy, sizeof(copy), "%s", text ? text : "");
    char* save = nullptr;
    for (char* t = strtok_r(copy, ", ;\n", &save); t; t = strtok_r(nullptr, ", ;\n", &save)){
        char s[16];
        if (!ngx_Symbol(t, s, sizeof(s))) return -1;
        bool dup = false;
        for (int i = 0; i < n; i++) if (!strcmp(out[i], s)) dup = true;
        if (dup) continue;
        if (n >= NGX_WATCH_MAX) return -1;
        memcpy(out[n++], s, sizeof(s));
    }
    memcpy(list, out, sizeof(out));
    return n;
}
