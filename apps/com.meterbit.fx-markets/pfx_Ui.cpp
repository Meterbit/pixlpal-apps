#include "pfx_Ui.h"
#include "mtb_kit_ui.h"
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t K_BAND = MTB_KIT_RGB(15, 40, 70);
static const uint16_t K_ROW = MTB_KIT_RGB(20, 26, 40);
static const char CREDIT[] = "DATA: MASSIVE";

static void pfx_Band(const char* left, const char* right){
    mtb_Panel_Fill_Rect(0, 10, 255, 20, K_BAND);
    mtb_Kit_Text(3, 12, Terminal6x8, KIT_TEXT, left, K_BAND);
    if (right && right[0]) mtb_Kit_Text_Right(252, 13, Terminal4x6, KIT_TEXT, right, K_BAND);
}

static void pfx_Credit(void){ mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_DIM, CREDIT); }

static void pfx_Price(char* dest, size_t size, double price){ snprintf(dest, size, "%.*f", pfx_Decimals(price), price); }

static bool pfx_Change(const Pfx_Quote_t& q, float& percent){
    if (!q.valid || std::isnan(q.prevClose) || q.prevClose <= 0) return false;
    percent = (float)((q.last - q.prevClose) / q.prevClose * 100);
    return true;
}

static uint16_t pfx_Change_Color(float p){ return p > 0.005f ? KIT_GREEN : p < -0.005f ? KIT_RED : KIT_LABEL; }

//##############################################################################################################
static void pfx_Draw_Board(const Pfx_View_t& v){
    const Pfx_Settings_t& s = *v.settings;
    const Pfx_Data_t& d = *v.data;
    pfx_Band("FX MARKETS", d.realtime ? "LIVE" : "END OF DAY");
    for (uint8_t i = 0; i < s.pairCount; i++){
        const Pfx_Quote_t& q = d.quotes[i];
        const int16_t y = 23 + i * 12;
        const bool focus = i == s.focus;
        const uint16_t bg = focus ? K_ROW : BLACK;
        if (focus) mtb_Panel_Fill_Rect(0, y - 1, 255, y + 10, K_ROW);
        char text[24];
        pfx_Pair_Text(s.pairs[i], text, sizeof(text));
        mtb_Kit_Text(4, y + 1, Terminal6x8, KIT_TEXT, text, bg);
        if (q.error){ mtb_Kit_Text(70, y + 2, Terminal4x6, KIT_RED, "MASSIVE HAS NO SUCH MARKET", bg); continue; }
        if (!q.valid){ mtb_Kit_Text(70, y + 2, Terminal4x6, KIT_DIM, "WAITING", bg); continue; }
        pfx_Price(text, sizeof(text), q.last);
        mtb_Kit_Text_Right(112, y + 1, Terminal6x8, KIT_GOLD, text, bg);
        float change;
        if (pfx_Change(q, change)){
            snprintf(text, sizeof(text), "%+.2f%%", change);
            const uint16_t color = pfx_Change_Color(change);
            mtb_Kit_Text_Right(160, y + 1, Terminal6x8, color, text, bg);
            if (fabsf(change) > 0.005f) mtb_Kit_Arrow(117, y + 4, 3, change > 0, color);
        }
        if (q.barCount >= 2){
            float closes[PFX_DAYS];
            for (uint8_t k = 0; k < q.barCount; k++) closes[k] = q.bars[k].c;
            const bool up = q.bars[q.barCount - 1].c >= q.bars[0].c;
            mtb_Kit_Sparkline(168, y, 84, 9, closes, q.barCount, up ? MTB_KIT_RGB(60, 180, 100) : MTB_KIT_RGB(200, 70, 70));
        }
    }
    pfx_Credit();
}

static void pfx_Candles(const Pfx_Bar_t* bars, int count, int16_t x0, int16_t y0, int16_t w, int16_t h, double& low, double& high){
    low = 1e18; high = -1e18;
    for (int i = 0; i < count; i++){ if (bars[i].l < low) low = bars[i].l; if (bars[i].h > high) high = bars[i].h; }
    if (!(high > low)){ high = low + 1e-6; }
    const float step = (float)w / count;
    const int16_t body = step >= 5 ? (int16_t)(step - 2) : step >= 3 ? 2 : 1;
    auto Y = [&](double p){ return (int16_t)(y0 + h - 1 - (p - low) / (high - low) * (h - 1)); };
    for (int i = 0; i < count; i++){
        const Pfx_Bar_t& b = bars[i];
        const int16_t cx = (int16_t)(x0 + step * i + step / 2);
        const uint16_t color = b.c >= b.o ? KIT_GREEN : KIT_RED;
        mtb_Panel_Draw_VLine(cx, Y(b.h), Y(b.l), mtb_Kit_Dim(color, 150));
        int16_t top = Y(b.o > b.c ? b.o : b.c), bottom = Y(b.o > b.c ? b.c : b.o);
        if (bottom < top) bottom = top;
        mtb_Panel_Fill_Rect(cx - body / 2, top, cx - body / 2 + body - 1, bottom, color);
    }
}

static void pfx_Draw_Chart(const Pfx_View_t& v){
    const Pfx_Settings_t& s = *v.settings;
    const Pfx_Data_t& d = *v.data;
    const uint8_t f = s.focus < s.pairCount ? s.focus : 0;
    const Pfx_Quote_t& q = d.quotes[f];
    char pair[16], title[48];
    pfx_Pair_Text(s.pairs[f], pair, sizeof(pair));
    const bool intraday = d.realtime && s.intraday && d.intradayPair == f && d.intradayCount >= 4;
    snprintf(title, sizeof(title), "%s  %s", pair, intraday ? "TODAY, 15 MIN" : "30 DAYS");
    char right[40];
    snprintf(right, sizeof(right), "%.14s / %.14s", pfx_Currency_Name(s.pairs[f].from), pfx_Currency_Name(s.pairs[f].to));
    mtb_Kit_Upper(right);
    pfx_Band(title, right);
    if (!q.valid){ mtb_Kit_Text_Centre(128, 60, Terminal6x8, KIT_LABEL, q.error ? "Massive has no market for this pair" : "Waiting for prices"); pfx_Credit(); return; }
    char text[24];
    pfx_Price(text, sizeof(text), q.last);
    const int16_t x = mtb_Kit_Text(4, 24, Terminal10x17, KIT_GOLD, text);
    float change;
    if (pfx_Change(q, change)){
        snprintf(text, sizeof(text), "%+.2f%%", change);
        mtb_Kit_Text(x + 8, 29, Terminal8x12, pfx_Change_Color(change), text);
    }
    if (q.updated && v.now > q.updated){
        char ago[20];
        mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(v.now - q.updated));
        mtb_Kit_Upper(ago);
        mtb_Kit_Text_Right(252, 31, Terminal4x6, KIT_LABEL, q.realtime ? ago : "LAST CLOSE");
    }
    double low, high;
    const Pfx_Bar_t* bars = intraday ? d.intraday : q.bars;
    const int count = intraday ? d.intradayCount : q.barCount;
    if (count == 0){ pfx_Credit(); return; }
    pfx_Candles(bars, count, 4, 46, 206, 66, low, high);
    pfx_Price(text, sizeof(text), high);
    mtb_Kit_Text(214, 46, Terminal4x6, KIT_GREEN, text);
    pfx_Price(text, sizeof(text), low);
    mtb_Kit_Text(214, 106, Terminal4x6, KIT_RED, text);
    mtb_Kit_Text(214, 74, Terminal4x6, KIT_DIM, intraday ? "TODAY" : "30 D");
    char from[16];
    pfx_Date(from, sizeof(from), bars[0].t);
    mtb_Kit_Text(4, 114, Terminal4x6, KIT_DIM, intraday ? "" : from + 5);
    pfx_Credit();
}

static void pfx_Draw_Sessions(const Pfx_View_t& v){
    const bool open = pfx_Market_Open(v.now);
    char right[40];
    if (open) snprintf(right, sizeof(right), "FOREX MARKET OPEN");
    else { const uint32_t wait = pfx_Until_Open(v.now); snprintf(right, sizeof(right), "CLOSED: OPENS IN %uD %uH", (unsigned)(wait / 86400), (unsigned)(wait % 86400 / 3600)); }
    pfx_Band("TRADING SESSIONS", right);
    // A day in your time: midnight to midnight.
    const int16_t x0 = 64, x1 = 250, w = x1 - x0;
    const int64_t dayStart = ((int64_t)v.now + v.localOffset) / 86400 * 86400 - v.localOffset;
    for (int h = 0; h <= 24; h += 3){
        const int16_t x = x0 + (int16_t)(w * h / 24);
        mtb_Panel_Draw_VLine(x, 24, 100, KIT_LINE);
        char label[4];
        snprintf(label, sizeof(label), "%02d", h % 24);
        if (h < 24) mtb_Kit_Text(x + 1, 103, Terminal4x6, KIT_DIM, label);
    }
    static const uint16_t COLORS[PFX_SESSIONS] = {MTB_KIT_RGB(200, 120, 30), MTB_KIT_RGB(200, 50, 80), MTB_KIT_RGB(60, 120, 230), MTB_KIT_RGB(40, 170, 90)};
    int openCount = 0;
    for (uint8_t s = 0; s < PFX_SESSIONS; s++){
        const int16_t y = 27 + s * 18;
        const bool isOpen = pfx_Session_Open(s, v.now);
        if (isOpen) openCount++;
        mtb_Kit_Text(4, y, Terminal6x8, isOpen ? KIT_TEXT : KIT_LABEL, pfx_Session_Name(s));
        mtb_Kit_Text(4, y + 9, Terminal4x6, isOpen ? KIT_GREEN : KIT_DIM, isOpen ? "OPEN" : "CLOSED");
        // Today's sessions on the line (yesterday's and tomorrow's where they reach into today).
        for (int k = -1; k <= 1; k++){
            const Pfx_Session_Times_t t = pfx_Session(s, (uint32_t)(dayStart + 43200 + k * 86400));
            int64_t a = (int64_t)t.open - dayStart, b = (int64_t)t.close - dayStart;
            if (b <= 0 || a >= 86400) continue;
            if (a < 0) a = 0;
            if (b > 86400) b = 86400;
            const int16_t xa = x0 + (int16_t)(w * a / 86400), xb = x0 + (int16_t)(w * b / 86400);
            mtb_Panel_Fill_Rect(xa, y + 2, xb, y + 10, isOpen ? COLORS[s] : mtb_Kit_Dim(COLORS[s], 110));
        }
    }
    // Now.
    const int16_t xn = x0 + (int16_t)(w * ((int64_t)v.now - dayStart) / 86400);
    mtb_Panel_Draw_VLine(xn, 23, 100, KIT_TEXT);
    mtb_Panel_Fill_Rect(xn - 1, 23, xn + 1, 24, KIT_TEXT);
    const bool overlap = pfx_Session_Open(PFX_LONDON, v.now) && pfx_Session_Open(PFX_NEW_YORK, v.now);
    mtb_Kit_Text(4, 112, Terminal4x6, overlap ? KIT_GOLD : KIT_DIM, overlap ? "LONDON AND NEW YORK OVERLAP: THE BUSIEST HOURS" : openCount ? "YOUR TIME; 08:00-17:00 LOCAL IN EACH CITY" : "NO SESSION OPEN NOW");
    pfx_Credit();
}

static void pfx_Draw_Plan(const Pfx_View_t& v){
    const Pfx_Data_t& d = *v.data;
    pfx_Band("MASSIVE", "YOUR PLAN");
    const char* plan = !d.planKnown ? "CHECKING THE PLAN" : d.realtime ? "STARTER OR HIGHER" : "CURRENCIES BASIC (FREE)";
    mtb_Kit_Text(4, 25, Terminal8x12, d.realtime ? KIT_GREEN : KIT_GOLD, plan);
    char text[96];
    snprintf(text, sizeof(text), "%s", d.realtime ? "REAL-TIME PRICES EVERY 15 SECONDS, TODAY'S 15-MINUTE CHART" : "END-OF-DAY PRICES, 30-DAY CHARTS, 5 CALLS A MINUTE");
    mtb_Kit_Text(4, 42, Terminal4x6, KIT_TEXT, text);
    snprintf(text, sizeof(text), "%u", (unsigned)d.callsToday);
    const int16_t x = mtb_Kit_Text(4, 54, Terminal10x17, KIT_TEXT, text);
    mtb_Kit_Text(x + 6, 60, Terminal6x8, KIT_LABEL, "CALLS TODAY");
    char lines[4][80];
    const int n = mtb_Kit_Wrap(d.realtime ? "PAID PLANS ARE PICKED UP BY THEMSELVES: PIXLPAL CHECKS ONCE A DAY. MASSIVE'S INDIVIDUAL PLANS ARE FOR PERSONAL, NON-COMMERCIAL USE."
                                          : "UPGRADE AT MASSIVE.COM/PRICING (CURRENCIES STARTER) AND PIXLPAL SWITCHES TO LIVE PRICES WITHIN A DAY. INDIVIDUAL PLANS ARE FOR PERSONAL, NON-COMMERCIAL USE.",
                               Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 4);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 80 + i * 8, Terminal4x6, KIT_DIM, lines[i]);
    pfx_Credit();
}

void pfx_Draw(const Pfx_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.message){
        pfx_Band("FX MARKETS", "");
        mtb_Kit_Text_Centre(128, 40, Terminal10x17, KIT_GOLD, v.message);
        if (v.detail){
            char lines[4][80];
            const int n = mtb_Kit_Wrap(v.detail, Terminal6x8, 236, &lines[0][0], sizeof(lines[0]), 4);
            for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 66 + i * 11, Terminal6x8, KIT_LABEL, lines[i]);
        }
        pfx_Credit();
        return;
    }
    switch (v.page){
        case PFX_PAGE_CHART: pfx_Draw_Chart(v); break;
        case PFX_PAGE_SESSIONS: pfx_Draw_Sessions(v); break;
        case PFX_PAGE_PLAN: pfx_Draw_Plan(v); break;
        default: pfx_Draw_Board(v); break;
    }
}
