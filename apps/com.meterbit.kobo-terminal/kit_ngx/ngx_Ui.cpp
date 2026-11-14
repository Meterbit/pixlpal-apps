// NGX screens.
#include "ngx_Ui.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t K_UP = MTB_KIT_RGB(40, 220, 100);
static const uint16_t K_DOWN = MTB_KIT_RGB(240, 60, 60);
static const uint16_t K_NGX = MTB_KIT_RGB(0, 170, 110);      // NGX green

static uint16_t sign_Color(float v){ return v > 0.0001f ? K_UP : v < -0.0001f ? K_DOWN : KIT_LABEL; }

static void ascii_Upper(char* dest, size_t size, const char* text){
    mtb_Kit_To_Ascii(dest, size, text ? text : "");
    mtb_Kit_Upper(dest);
}

static void text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, bool upper = true){
    char a[96], b[96];
    if (upper) ascii_Upper(a, sizeof(a), text); else mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    mtb_Kit_Text(x, y, font, color, b, BLACK);
}

static void header(const Ngx_View_t& v, const char* title){
    char a[48];
    ascii_Upper(a, sizeof(a), v.status);
    const int16_t rw = mtb_Kit_Text_Width(Terminal4x6, a);
    mtb_Kit_Text_Right(253, 12, Terminal4x6, v.open ? K_UP : KIT_LABEL, a, BLACK);
    text_Fit(3, 12, Terminal4x6, KIT_GOLD, title, 246 - rw);
    mtb_Panel_Draw_HLine(19, 0, 255, KIT_LINE);
}

static void footer(const Ngx_View_t& v){
    char a[48], b[24];
    snprintf(a, sizeof(a), "DATA: %s", v.source ? v.source : "");
    ascii_Upper(b, sizeof(b), a);
    mtb_Kit_Text(3, 121, Terminal4x6, KIT_DIM, b, BLACK);
    const uint32_t up = v.board ? v.board->updated : 0;
    if (up && v.now > up){
        char ago[24], t[32];
        mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(v.now - up));
        snprintf(t, sizeof(t), "UPDATED %s", ago);
        ascii_Upper(a, sizeof(a), t);
        mtb_Kit_Text_Right(253, 121, Terminal4x6, KIT_DIM, a, BLACK);
    }
}

// The market's day -----------------------------------------------------------------------------------------------------
static void overview(const Ngx_View_t& v){
    const Ngx_Market_t& m = v.board->market;
    header(v, "NGX All-Share Index");
    char t[96], a[24], b[24], c[24];
    if (!m.known){ mtb_Kit_Text_Centre(128, 50, Terminal8x12, KIT_LABEL, "Waiting for the market", BLACK); footer(v); return; }
    snprintf(t, sizeof(t), "%.2f", (double)m.asi);
    char comma[24];
    {   // 105,432.18
        char whole[24];
        snprintf(whole, sizeof(whole), "%.0f", floor((double)m.asi));
        const size_t n = strlen(whole);
        size_t k = 0;
        for (size_t i = 0; i < n; i++){ if (i && (n - i) % 3 == 0) comma[k++] = ','; comma[k++] = whole[i]; }
        snprintf(comma + k, sizeof(comma) - k, ".%02d", (int)lround((m.asi - floor(m.asi)) * 100) % 100);
    }
    const uint16_t sc = sign_Color(m.asiChangePct);
    const int16_t w = mtb_Kit_Big_Text_Width(Terminal10x16, 2, comma);
    if (w <= 250) mtb_Kit_Big_Text(128 - w / 2, 23, Terminal10x16, 2, KIT_TEXT, comma);
    else mtb_Kit_Text_Centre(128, 30, Terminal10x16, KIT_TEXT, comma, BLACK);
    ngx_Percent(a, sizeof(a), m.asiChangePct);
    if (m.asiChange != 0) snprintf(t, sizeof(t), "%+.2f  (%s)", (double)m.asiChange, a); else snprintf(t, sizeof(t), "%s", a);
    mtb_Kit_Text_Centre(128, 58, Terminal8x12, sc, t, BLACK);
    if (m.ytdPct != 0){ ngx_Percent(a, sizeof(a), m.ytdPct); snprintf(t, sizeof(t), "YEAR TO DATE %s", a); mtb_Kit_Text_Right(253, 22, Terminal4x6, sign_Color(m.ytdPct), t, BLACK); }

    // Breadth.
    const int total = m.advancers + m.decliners + m.unchanged;
    if (total){
        const int16_t x0 = 8, width = 240;
        const int16_t wa = (int16_t)(width * m.advancers / total), wd = (int16_t)(width * m.decliners / total);
        mtb_Panel_Fill_Rect(x0, 74, x0 + width - 1, 78, KIT_LINE);
        if (wa) mtb_Panel_Fill_Rect(x0, 74, x0 + wa - 1, 78, K_UP);
        if (wd) mtb_Panel_Fill_Rect(x0 + width - wd, 74, x0 + width - 1, 78, K_DOWN);
        snprintf(t, sizeof(t), "%u UP", m.advancers);
        mtb_Kit_Text(x0, 81, Terminal4x6, K_UP, t, BLACK);
        snprintf(t, sizeof(t), "%u UNCHANGED", m.unchanged);
        mtb_Kit_Text_Centre(128, 81, Terminal4x6, KIT_LABEL, t, BLACK);
        snprintf(t, sizeof(t), "%u DOWN", m.decliners);
        mtb_Kit_Text_Right(x0 + width, 81, Terminal4x6, K_DOWN, t, BLACK);
    }
    // The day's trading.
    ngx_Naira(a, sizeof(a), m.value, true);
    ngx_Count(b, sizeof(b), m.volume);
    ngx_Count(c, sizeof(c), m.deals);
    snprintf(t, sizeof(t), "VALUE %s   VOLUME %s   DEALS %s", a, b, c);
    mtb_Kit_Text_Centre(128, 94, Terminal4x6, KIT_TEXT, t, BLACK);
    if (m.marketCap > 0){
        ngx_Naira(a, sizeof(a), m.marketCap, true);
        snprintf(t, sizeof(t), "MARKET CAP %s%s%s", a, m.date[0] ? "   SESSION " : "", m.date);
        mtb_Kit_Text_Centre(128, 104, Terminal4x6, KIT_LABEL, t, BLACK);
    }
    footer(v);
}

// Every stock, by size ---------------------------------------------------------------------------------------------------
static uint16_t heat(float pct){
    const float f = fminf(fabsf(pct) / 5.0f, 1.0f);
    if (fabsf(pct) < 0.0001f) return MTB_KIT_RGB(45, 45, 50);
    const uint8_t level = (uint8_t)(70 + 185 * f);
    return pct > 0 ? MTB_KIT_RGB(0, level, (uint8_t)(level * 0.35f)) : MTB_KIT_RGB(level, 0, (uint8_t)(level * 0.1f));
}

static void heatmap(const Ngx_View_t& v){
    header(v, "Every stock, by size");
    static int order[NGX_STOCKS_MAX];
    const int n = ngx_By_Size(*v.board, order, NGX_STOCKS_MAX);
    // The six largest named, then the rest as small tiles.
    int k = 0;
    for (int r = 0; r < 2; r++){
        for (int c = 0; c < 3 && k < n; c++, k++){
            const Ngx_Stock_t& s = v.board->stocks[order[k]];
            const int16_t x = (int16_t)(c * 85 + 1), y = (int16_t)(21 + r * 22);
            const uint16_t col = heat(s.changePct);
            mtb_Panel_Fill_Rect(x, y, x + 83, y + 20, col);
            char sym[16], pct[16];
            ascii_Upper(sym, sizeof(sym), s.symbol);
            mtb_Kit_Text(x + 3, y + 3, Terminal6x8, KIT_TEXT, sym, col);
            ngx_Percent(pct, sizeof(pct), s.changePct);
            mtb_Kit_Text(x + 3, y + 12, Terminal4x6, KIT_TEXT, pct, col);
        }
    }
    for (int i = 0; k < n && i < 16 * 5; i++, k++){
        const int16_t x = (int16_t)((i % 16) * 16), y = (int16_t)(66 + (i / 16) * 10);
        mtb_Panel_Fill_Rect(x + 1, y, x + 14, y + 8, heat(v.board->stocks[order[k]].changePct));
    }
    footer(v);
}

static void movers(const Ngx_View_t& v){
    header(v, "Gainers and losers");
    int g[7], l[7], gc, lc;
    ngx_Movers(*v.board, g, gc, l, lc, 7);
    mtb_Kit_Text(4, 22, Terminal4x6, K_UP, "GAINERS", BLACK);
    mtb_Kit_Text(132, 22, Terminal4x6, K_DOWN, "LOSERS", BLACK);
    mtb_Panel_Draw_VLine(127, 22, 116, KIT_LINE);
    for (int side = 0; side < 2; side++){
        const int* list = side ? l : g;
        const int count = side ? lc : gc;
        const int16_t x0 = side ? 132 : 4;
        for (int i = 0; i < count; i++){
            const Ngx_Stock_t& s = v.board->stocks[list[i]];
            const int16_t y = (int16_t)(31 + i * 12);
            char sym[16], pct[16], price[20];
            ascii_Upper(sym, sizeof(sym), s.symbol);
            mtb_Kit_Text(x0, y, Terminal6x8, KIT_TEXT, sym, BLACK);
            ngx_Percent(pct, sizeof(pct), s.changePct);
            mtb_Kit_Text_Right(x0 + 120, y, Terminal6x8, side ? K_DOWN : K_UP, pct, BLACK);
            ngx_Naira(price, sizeof(price), s.price, true);
            mtb_Kit_Text_Right(x0 + 76, y + 1, Terminal4x6, KIT_LABEL, price, BLACK);
        }
        if (!count) mtb_Kit_Text(x0, 34, Terminal4x6, KIT_DIM, "NONE TODAY", BLACK);
    }
    footer(v);
}

static void watchlist(const Ngx_View_t& v){
    header(v, "Watchlist");
    if (!v.watchCount){ mtb_Kit_Text_Centre(128, 54, Terminal6x8, KIT_LABEL, "Add stocks in the phone app", BLACK); footer(v); return; }
    for (int r = 0; r < NGX_VISIBLE_ROWS && v.scroll + r < v.watchCount; r++){
        const char* sym = v.watch[v.scroll + r];
        const int i = ngx_Find(*v.board, sym);
        const int16_t y = (int16_t)(23 + r * 12);
        char a[16];
        ascii_Upper(a, sizeof(a), sym);
        mtb_Kit_Text(4, y, Terminal6x8, KIT_TEXT, a, BLACK);
        if (i < 0){ mtb_Kit_Text(80, y + 1, Terminal4x6, KIT_DIM, "NOT FOUND", BLACK); continue; }
        const Ngx_Stock_t& s = v.board->stocks[i];
        char price[20], pct[16];
        ngx_Naira(price, sizeof(price), s.price, false);
        mtb_Kit_Text_Right(160, y, Terminal6x8, KIT_TEXT, price, BLACK);
        ngx_Percent(pct, sizeof(pct), s.changePct);
        mtb_Kit_Text_Right(214, y, Terminal6x8, sign_Color(s.changePct), pct, BLACK);
        // A bar for the move (5% fills it).
        const int16_t w = (int16_t)fminf(fabsf(s.changePct) / 5.0f * 34, 34);
        if (w) mtb_Panel_Fill_Rect(218, y + 2, 218 + w, y + 5, sign_Color(s.changePct));
    }
    footer(v);
}

static void range_Bar(int16_t y, const char* label, float lo, float hi, float now){
    if (hi <= lo || lo <= 0) return;
    char a[20], b[20];
    mtb_Kit_Text(4, y, Terminal4x6, KIT_LABEL, label, BLACK);
    ngx_Naira(a, sizeof(a), lo, true);
    ngx_Naira(b, sizeof(b), hi, true);
    mtb_Kit_Text(44, y, Terminal4x6, KIT_DIM, a, BLACK);
    mtb_Kit_Text_Right(253, y, Terminal4x6, KIT_DIM, b, BLACK);
    const int16_t x0 = 90, x1 = 210;
    mtb_Panel_Draw_HLine(y + 3, x0, x1, KIT_LINE);
    const float f = fminf(fmaxf((now - lo) / (hi - lo), 0), 1);
    const int16_t x = (int16_t)(x0 + f * (x1 - x0));
    mtb_Panel_Fill_Rect(x - 1, y, x + 1, y + 6, KIT_GOLD);
}

static void stock(const Ngx_View_t& v){
    if (v.focus < 0 || v.focus >= v.board->count){ header(v, "A stock"); mtb_Kit_Text_Centre(128, 54, Terminal6x8, KIT_LABEL, "Pick one in the phone app", BLACK); footer(v); return; }
    const Ngx_Stock_t& s = v.board->stocks[v.focus];
    header(v, s.sector[0] ? s.sector : "A stock");
    char a[48], b[24];
    ascii_Upper(a, sizeof(a), s.symbol);
    mtb_Kit_Text(4, 22, Terminal10x16, K_NGX, a, BLACK);
    text_Fit(4, 40, Terminal4x6, KIT_LABEL, s.name, 120);
    ngx_Naira(a, sizeof(a), s.price, false);
    mtb_Kit_Text(4, 50, Terminal10x16, KIT_TEXT, a, BLACK);
    ngx_Percent(b, sizeof(b), s.changePct);
    if (s.change != 0){ char c[48]; snprintf(c, sizeof(c), "%+.2f  %s", (double)s.change, b); mtb_Kit_Text(4, 69, Terminal6x8, sign_Color(s.changePct), c, BLACK); }
    else mtb_Kit_Text(4, 69, Terminal6x8, sign_Color(s.changePct), b, BLACK);
    // The chart, else the facts, on the right.
    if (v.chart && v.chartCount >= 2){
        mtb_Kit_Sparkline(130, 24, 122, 46, v.chart, v.chartCount, v.chart[v.chartCount - 1] >= v.chart[0] ? K_UP : K_DOWN);
        if (v.chartLabel){ ascii_Upper(a, sizeof(a), v.chartLabel); mtb_Kit_Text_Right(252, 72, Terminal4x6, KIT_DIM, a, BLACK); }
    } else {
        int line = 0;
        for (int i = 0; i < 6 && v.facts[i]; i++) text_Fit(132, (int16_t)(24 + 9 * line++), Terminal4x6, KIT_TEXT, v.facts[i], 120);
    }
    range_Bar(82, "TODAY", s.low, s.high, s.price);
    range_Bar(92, "52 WEEKS", s.low52, s.high52, s.price);
    char t[96], vol[24], cap[24];
    ngx_Count(vol, sizeof(vol), s.volume);
    size_t k = (size_t)snprintf(t, sizeof(t), "VOLUME %s", vol);
    if (s.marketCap > 0){ ngx_Naira(cap, sizeof(cap), s.marketCap, true); k += (size_t)snprintf(t + k, sizeof(t) - k, "   MARKET CAP %s", cap); }
    if (s.pe > 0) snprintf(t + k, sizeof(t) - k, "   P/E %.1f", (double)s.pe);
    mtb_Kit_Text(4, 104, Terminal4x6, KIT_LABEL, t, BLACK);
    footer(v);
}

static void table(const Ngx_View_t& v){
    const Ngx_Table_t& t = *v.table;
    header(v, t.title);
    if (!t.count){ mtb_Kit_Text_Centre(128, 54, Terminal6x8, KIT_LABEL, "Nothing yet", BLACK); footer(v); return; }
    for (int r = 0; r < NGX_VISIBLE_ROWS && v.scroll + r < t.count; r++){
        const Ngx_Row_t& row = t.rows[v.scroll + r];
        const int16_t y = (int16_t)(23 + r * 12);
        text_Fit(4, y, Terminal6x8, KIT_TEXT, row.label, row.detail[0] ? 88 : 120);
        if (row.detail[0]) text_Fit(96, y + 1, Terminal4x6, KIT_DIM, row.detail, row.change[0] ? 64 : 100);
        char a[24];
        ascii_Upper(a, sizeof(a), row.value);
        mtb_Kit_Text_Right(row.change[0] ? 206 : 252, y, Terminal6x8, KIT_TEXT, a, BLACK);
        if (row.change[0]){
            ascii_Upper(a, sizeof(a), row.change);
            mtb_Kit_Text_Right(252, y + 1, Terminal4x6, row.sign > 0 ? K_UP : row.sign < 0 ? K_DOWN : KIT_LABEL, a, BLACK);
        }
    }
    if (t.count > NGX_VISIBLE_ROWS){
        char a[40];
        snprintf(a, sizeof(a), "%d-%d OF %d", v.scroll + 1, v.scroll + NGX_VISIBLE_ROWS < t.count ? v.scroll + NGX_VISIBLE_ROWS : t.count, t.count);
        mtb_Kit_Text_Centre(128, 121, Terminal4x6, KIT_DIM, a, BLACK);
    }
    footer(v);
}

static void message(const Ngx_View_t& v){
    header(v, v.title ? v.title : "");
    char a[200], lines[5][48];
    mtb_Kit_To_Ascii(a, sizeof(a), v.message ? v.message : "");
    const int n = mtb_Kit_Wrap(a, Terminal6x8, 244, &lines[0][0], sizeof(lines[0]), 5);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, (int16_t)(36 + i * 12), Terminal6x8, KIT_TEXT, lines[i], BLACK);
    footer(v);
}

void ngx_Draw(const Ngx_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, BLACK);
    switch (v.page){
        case NGX_OVERVIEW: overview(v); break;
        case NGX_HEATMAP: heatmap(v); break;
        case NGX_MOVERS: movers(v); break;
        case NGX_WATCH: watchlist(v); break;
        case NGX_STOCK: stock(v); break;
        case NGX_TABLE: if (v.table) table(v); break;
        default: message(v); break;
    }
    mtb_Panel_End_Update();
}
