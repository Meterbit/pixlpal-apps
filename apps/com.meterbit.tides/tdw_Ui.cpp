// Tides & Water's screens, 256 x 118 under the status bar (rows 10-127).
#include "tdw_App.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t WATER      = MTB_KIT_RGB(0, 70, 140);
static const uint16_t WATER_DARK = MTB_KIT_RGB(0, 28, 60);
static const uint16_t HIGH_C     = MTB_KIT_RGB(80, 200, 255);
static const uint16_t LOW_C      = MTB_KIT_RGB(70, 110, 255);
#define TOP MTB_KIT_TOP

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

static void fit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text(x, y, font, color, fitted);
}

static void fit_Right(int16_t right, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth,
                      uint16_t background = BLACK){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text_Right(right, y, font, color, fitted, background);
}

/** "6,151" */
static void thousands(char* dest, size_t size, float v){
    char raw[24];
    snprintf(raw, sizeof(raw), "%.0f", (double)v);
    const size_t n = strlen(raw);
    size_t k = 0;
    for (size_t i = 0; i < n && k + 2 < size; i++){
        dest[k++] = raw[i];
        const size_t left = n - 1 - i;
        if (left && left % 3 == 0 && raw[i] != '-') dest[k++] = ',';
    }
    dest[k] = '\0';
}

static void title(const char* name, const Tdw_View_t& v, uint16_t color = KIT_GOLD){
    mtb_Kit_Text(2, TOP + 2, Terminal6x8, color, name);
    const int16_t x = 4 + mtb_Kit_Text_Width(Terminal6x8, name) + 6;
    fit_Right(253, TOP + 3, Terminal4x6, KIT_LABEL, v.s->place, 253 - x);
}

static void message(const char* head, const char* text){
    clear();
    mtb_Kit_Text_Centre(128, TOP + 18, Terminal8x12, KIT_GOLD, head);
    char ascii[256], lines[6][64];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    const int n = mtb_Kit_Wrap(ascii, Terminal6x8, 240, &lines[0][0], sizeof(lines[0]), 6);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, TOP + 40 + i * 11, Terminal6x8, KIT_TEXT, lines[i]);
}

// Tide now and the next 24 hours -------------------------------------------------------------------------------
static void page_Tide(const Tdw_View_t& v){
    const Tdw_Tide_t& t = v.d->tide;
    const Tdw_Settings_t& s = *v.s;
    const int32_t off = v.d->utcOffset;
    char a[48], b[48], c[24];
    clear();
    if (t.state != TDW_OK || t.n < 2){
        message("TIDES", t.state == TDW_WAIT || (!t.why[0] && t.state != TDW_NONE) ? "Asking for the tides..." : t.why[0] ? t.why : "No tides for this place");
        return;
    }
    fit_Text(2, TOP + 2, Terminal6x8, KIT_CYAN, s.place, 140);
    // Where the tide comes from.
    if (t.station[0]) snprintf(a, sizeof(a), "%s %s", tdw_Source_Name(t.source), t.station);
    else snprintf(a, sizeof(a), "%s model", tdw_Source_Name(t.source));
    fit_Right(253, TOP + 2, Terminal4x6, KIT_LABEL, a, 110);
    snprintf(a, sizeof(a), "above %s", t.datum[0] ? t.datum : "datum");
    fit_Right(253, TOP + 9, Terminal4x6, KIT_DIM, a, 110);

    float now = NAN, soon = NAN;
    tdw_Level_At(t, v.now, now);
    tdw_Level_At(t, v.now + 600, soon);
    const int ni = tdw_Next_Extreme(t, v.now);
    const bool rising = ni >= 0 ? t.e[ni].high : (!isnan(soon) && soon > now);
    tdw_Height_Text(a, sizeof(a), now, s.imperial);
    mtb_Kit_Text(2, TOP + 13, Terminal10x16, KIT_TEXT, a);
    const int16_t ax = 6 + mtb_Kit_Text_Width(Terminal10x16, a);
    mtb_Kit_Arrow(ax + 5, TOP + 20, 9, rising, rising ? HIGH_C : LOW_C);
    mtb_Kit_Text(ax + 13, TOP + 17, Terminal6x8, rising ? HIGH_C : LOW_C, rising ? "RISING" : "FALLING");
    if (t.observed && v.now - t.obsT < 3600){
        tdw_Height_Text(b, sizeof(b), t.obsH, s.imperial);
        snprintf(a, sizeof(a), "Gauge %s", b);
        mtb_Kit_Text(2, TOP + 31, Terminal4x6, KIT_GOLD, a);
    } else {
        const float range = tdw_Range(t, v.now - 43200);
        if (!isnan(range)){ tdw_Height_Text(b, sizeof(b), range, s.imperial); snprintf(a, sizeof(a), "Range today %s", b); mtb_Kit_Text(2, TOP + 31, Terminal4x6, KIT_LABEL, a); }
    }
    // The next two turns.
    for (int k = 0, i = ni; k < 2 && i >= 0 && i < t.ne; k++, i++){
        const Tdw_Extreme_t& e = t.e[i];
        tdw_Clock(c, sizeof(c), e.t, off);
        tdw_Height_Text(b, sizeof(b), e.h, s.imperial);
        snprintf(a, sizeof(a), "%s %s %s", e.high ? "HIGH" : "LOW ", c, b);
        const int16_t y = TOP + 17 + k * 9;
        mtb_Kit_Text(126, y, Terminal6x8, e.high ? HIGH_C : LOW_C, a);
        if (k == 0){
            tdw_Span(b, sizeof(b), (int32_t)(e.t - v.now));
            snprintf(a, sizeof(a), "%s in %s", e.high ? "High water" : "Low water", b);
            mtb_Kit_Text(126, TOP + 36, Terminal4x6, KIT_LABEL, a);
        }
    }

    // The curve: 6 hours back to 18 ahead.
    const int16_t y0 = TOP + 45, y1 = PANEL_RES_Y - 9;
    const uint32_t from = v.now - 6 * 3600, to = v.now + 18 * 3600;
    float lo = 1e9f, hi = -1e9f;
    for (int x = 0; x < PANEL_RES_X; x++){
        float h;
        if (tdw_Level_At(t, from + (uint32_t)((uint64_t)(to - from) * x / (PANEL_RES_X - 1)), h)){ lo = fminf(lo, h); hi = fmaxf(hi, h); }
    }
    if (hi < lo) return;
    const float pad = fmaxf(0.1f, (hi - lo) * 0.18f);
    lo -= pad;
    hi += pad;
    auto yOf = [&](float h){ return (int16_t)(y1 - (h - lo) / (hi - lo) * (y1 - y0)); };
    int16_t prevY = -1;
    for (int x = 0; x < PANEL_RES_X; x++){
        float h;
        if (!tdw_Level_At(t, from + (uint32_t)((uint64_t)(to - from) * x / (PANEL_RES_X - 1)), h)){ prevY = -1; continue; }
        const int16_t y = yOf(h);
        mtb_Panel_Draw_VLine(x, y + 1, y1, x < 64 ? WATER_DARK : WATER);
        if (prevY >= 0) mtb_Panel_Draw_Line(x - 1, prevY, x, y, HIGH_C); else mtb_Panel_Draw_PixelRGB565(x, y, HIGH_C);
        prevY = y;
    }
    // The high-water alert level, when it is in view.
    if (s.alertOn && s.highWaterCm){
        const float limit = s.highWaterCm / 100.0f;
        if (limit > lo && limit < hi){ const int16_t y = yOf(limit); for (int x = 0; x < PANEL_RES_X; x += 4) mtb_Panel_Draw_HLine(y, x, x + 1, KIT_RED); }
    }
    // Hours along the bottom, the turns, and now.
    for (uint32_t tt = ((from + off) / 21600 + 1) * 21600 - off; tt < to; tt += 21600){
        const int16_t x = (int16_t)((uint64_t)(tt - from) * (PANEL_RES_X - 1) / (to - from));
        tdw_Clock(c, sizeof(c), tt, off);
        c[2] = '\0';
        mtb_Panel_Draw_VLine(x, y1 + 1, y1 + 2, KIT_DIM);
        mtb_Kit_Text_Centre(x, y1 + 3, Terminal4x6, KIT_DIM, c);
    }
    for (int i = 0; i < t.ne; i++){
        const Tdw_Extreme_t& e = t.e[i];
        if (e.t < from || e.t > to) continue;
        const int16_t x = (int16_t)((uint64_t)(e.t - from) * (PANEL_RES_X - 1) / (to - from));
        const int16_t y = yOf(e.h);
        mtb_Panel_Fill_Circle(x, y, 1, KIT_TEXT);
        tdw_Clock(c, sizeof(c), e.t, off);
        const int16_t w = mtb_Kit_Text_Width(Terminal4x6, c);
        const int16_t lx = x - w / 2 < 0 ? 0 : x + w / 2 > 255 ? 255 - w : x - w / 2;
        mtb_Kit_Text(lx, e.high ? y - 8 : y + 3, Terminal4x6, KIT_TEXT, c, e.high ? BLACK : (x < 64 ? WATER_DARK : WATER));
    }
    const int16_t nx = 64;
    for (int y = y0; y <= y1; y += 3) mtb_Panel_Draw_PixelRGB565(nx, y, KIT_GOLD);
    if (!isnan(now)) mtb_Panel_Fill_Circle(nx, yOf(now), 2, KIT_GOLD);
}

// The table of highs and lows ------------------------------------------------------------------------------------
static void page_Table(const Tdw_View_t& v){
    const Tdw_Tide_t& t = v.d->tide;
    const int32_t off = v.d->utcOffset;
    clear();
    if (t.state != TDW_OK || t.ne < 2){ message("TIDE TIMES", t.why[0] ? t.why : "Asking for the tides..."); return; }
    title("TIDE TIMES", v);
    const float age = mtb_Kit_Moon_Age(v.now);
    const char* sn = tdw_Spring_Neap(age);
    char a[64], b[24], c[16];
    if (sn[0]) mtb_Kit_Text(80, TOP + 3, Terminal4x6, KIT_CYAN, sn);
    int first = 0;
    while (first < t.ne && t.e[first].t + 3 * 3600 < v.now) first++;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < t.ne; i++){ lo = fminf(lo, t.e[i].h); hi = fmaxf(hi, t.e[i].h); }
    const int next = tdw_Next_Extreme(t, v.now);
    for (int row = 0; row < 10 && first + row < t.ne; row++){
        const Tdw_Extreme_t& e = t.e[first + row];
        const int16_t y = TOP + 13 + row * 10;
        const bool past = e.t <= v.now;
        const uint16_t col = past ? KIT_DIM : (e.high ? HIGH_C : LOW_C);
        if (first + row == next) mtb_Kit_Arrow(4, y + 3, 5, false, KIT_GOLD);
        mtb_Kit_Text(10, y, Terminal6x8, past ? KIT_DIM : KIT_LABEL, tdw_Weekday(e.t, off));
        tdw_Clock(c, sizeof(c), e.t, off);
        mtb_Kit_Text(34, y, Terminal6x8, past ? KIT_DIM : KIT_TEXT, c);
        mtb_Kit_Text(70, y, Terminal6x8, col, e.high ? "HIGH" : "LOW");
        tdw_Height_Text(b, sizeof(b), e.h, v.s->imperial);
        mtb_Kit_Text_Right(150, y, Terminal6x8, past ? KIT_DIM : KIT_TEXT, b);
        const float f = hi > lo ? (e.h - lo) / (hi - lo) : 0.5f;
        mtb_Kit_Bar(158, y + 2, 94, 4, 0.05f + 0.95f * f, past ? KIT_DIM : col, KIT_LINE);
    }
    (void)a;
}

// The sea --------------------------------------------------------------------------------------------------------
static uint16_t severity_Color(uint8_t sev){
    switch (sev){ case 0: return KIT_GREEN; case 1: return KIT_CYAN; case 2: return KIT_GOLD; case 3: return KIT_ORANGE; default: return KIT_RED; }
}

static void page_Sea(const Tdw_View_t& v){
    const Tdw_Sea_t& sea = v.d->sea;
    const Tdw_Settings_t& s = *v.s;
    char a[48], b[24], c[16];
    clear();
    if (!sea.known){ message("SEA", sea.updated ? "No open sea near this place: the ocean model has no waves here." : "Asking for the sea..."); return; }
    title("SEA", v);
    // Waves.
    mtb_Kit_Text(2, TOP + 13, Terminal4x6, KIT_LABEL, "WAVES");
    tdw_Height_Text(a, sizeof(a), sea.wave, s.imperial);
    mtb_Kit_Text(2, TOP + 20, Terminal10x16, KIT_TEXT, a);
    const uint8_t sev = tdw_Sea_Severity(sea.wave);
    mtb_Kit_Text(2, TOP + 38, Terminal8x12, severity_Color(sev), tdw_Sea_State(sea.wave));
    snprintf(a, sizeof(a), "%.0f s from %s", (double)sea.wavePeriod, tdw_Compass(sea.waveDir));
    mtb_Kit_Text(2, TOP + 52, Terminal6x8, KIT_LABEL, a);
    // Swell.
    mtb_Kit_Text(96, TOP + 13, Terminal4x6, KIT_LABEL, "SWELL");
    tdw_Height_Text(a, sizeof(a), sea.swell, s.imperial);
    mtb_Kit_Text(96, TOP + 21, Terminal8x12, KIT_TEXT, a);
    if (!isnan(sea.swellPeriod)){ snprintf(a, sizeof(a), "%.0f s %s", (double)sea.swellPeriod, tdw_Compass(sea.swellDir)); mtb_Kit_Text(96, TOP + 35, Terminal6x8, KIT_LABEL, a); }
    // Sea temperature and current.
    mtb_Kit_Text(176, TOP + 13, Terminal4x6, KIT_LABEL, "WATER");
    tdw_Temp_Text(a, sizeof(a), sea.sst, s.imperial);
    mtb_Kit_Text(176, TOP + 21, Terminal8x12, KIT_GOLD, a);
    if (!isnan(sea.current)){
        mtb_Kit_Text(176, TOP + 37, Terminal4x6, KIT_LABEL, "CURRENT");
        tdw_Wind_Text(b, sizeof(b), sea.current, s.windUnit);
        snprintf(a, sizeof(a), "%s %s", b, tdw_Compass(sea.currentDir));
        mtb_Kit_Text(176, TOP + 45, Terminal6x8, KIT_TEXT, a);
    }
    // The next 24 hours.
    mtb_Panel_Draw_HLine(TOP + 66, 0, 255, KIT_LINE);
    if (!isnan(sea.waveMax24)){
        tdw_Height_Text(b, sizeof(b), sea.waveMax24, s.imperial);
        tdw_Clock(c, sizeof(c), sea.waveMaxAt, v.d->utcOffset);
        snprintf(a, sizeof(a), "Next 24 h: waves up to %s at %s (%s)", b, c, tdw_Sea_State(sea.waveMax24));
        fit_Text(2, TOP + 72, Terminal6x8, severity_Color(tdw_Sea_Severity(sea.waveMax24)), a, 252);
    }
    // Sea scale bar.
    static const float marks[] = {0.5f, 1.25f, 2.5f, 4.0f};
    const int16_t bx = 2, by = TOP + 88, bw = 252;
    for (int i = 0; i < 5; i++){
        const int16_t x0 = bx + bw * i / 5, x1 = bx + bw * (i + 1) / 5 - 2;
        mtb_Panel_Fill_Rect(x0, by, x1, by + 3, mtb_Kit_Dim(severity_Color((uint8_t)i), i == sev ? 255 : 70));
    }
    float pos = 0;
    const float w = isnan(sea.wave) ? 0 : sea.wave;
    if (w < marks[0]) pos = w / marks[0];
    else { int i = 0; while (i < 3 && w >= marks[i + 1]) i++; pos = i + 1 + (i < 3 ? (w - marks[i]) / (marks[i + 1] - marks[i]) : fminf(1, (w - 4) / 4)); }
    mtb_Kit_Arrow(bx + (int16_t)(bw * pos / 5), by + 8, 6, true, KIT_TEXT);
    mtb_Kit_Text(bx, by + 12, Terminal4x6, KIT_DIM, "CALM");
    mtb_Kit_Text_Right(253, by + 12, Terminal4x6, KIT_DIM, "DANGEROUS");
    snprintf(a, sizeof(a), "Ocean model, %.0f km out. Not for navigation", (double)sea.cellKm);
    mtb_Kit_Text_Centre(128, by + 12, Terminal4x6, KIT_DIM, a);
}

// Wind and weather -------------------------------------------------------------------------------------------------
static uint8_t wx_Icon(int code){
    if (code == 0 || code == 1) return KIT_WX_CLEAR;
    if (code == 2) return KIT_WX_PARTLY;
    if (code == 3) return KIT_WX_CLOUDY;
    if (code == 45 || code == 48) return KIT_WX_FOG;
    if (code >= 51 && code <= 57) return KIT_WX_DRIZZLE;
    if (code == 65 || code == 82) return KIT_WX_HEAVY_RAIN;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 81)) return KIT_WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return KIT_WX_SNOW;
    if (code == 96 || code == 99) return KIT_WX_HAIL;
    if (code >= 95) return KIT_WX_STORM;
    return KIT_WX_UNKNOWN;
}

static void page_Wind(const Tdw_View_t& v){
    const Tdw_Weather_t& w = v.d->wx;
    const Tdw_Settings_t& s = *v.s;
    char a[64], b[24];
    clear();
    if (!w.known){ message("WIND", "Asking for the wind..."); return; }
    title("WIND & WEATHER", v);
    // The compass: the arrow points where the wind blows to.
    const int16_t cx = 30, cy = TOP + 42, r = 24;
    mtb_Panel_Draw_Circle(cx, cy, r, KIT_LINE);
    mtb_Kit_Text_Centre(cx, cy - r - 1, Terminal4x6, KIT_LABEL, "N");
    if (!isnan(w.windDir)){
        const float to = (w.windDir + 180.0f) * (float)M_PI / 180.0f;
        const float dx = sinf(to), dy = -cosf(to);
        const int16_t tx = cx + (int16_t)(dx * (r - 3)), ty = cy + (int16_t)(dy * (r - 3));
        const int16_t fx = cx - (int16_t)(dx * (r - 3)), fy = cy - (int16_t)(dy * (r - 3));
        mtb_Panel_Draw_Line(fx, fy, tx, ty, KIT_CYAN);
        const float px = -dy, py = dx;
        mtb_Panel_Fill_Triangle(tx, ty, tx - (int16_t)(dx * 7 + px * 4), ty - (int16_t)(dy * 7 + py * 4), tx - (int16_t)(dx * 7 - px * 4), ty - (int16_t)(dy * 7 - py * 4), KIT_CYAN);
    }
    // Speed, gusts, Beaufort.
    tdw_Wind_Text(a, sizeof(a), w.wind, s.windUnit, false);
    mtb_Kit_Text(64, TOP + 14, Terminal10x16, KIT_TEXT, a);
    const int16_t ux = 66 + mtb_Kit_Text_Width(Terminal10x16, a);
    mtb_Kit_Text(ux, TOP + 21, Terminal6x8, KIT_LABEL, tdw_Wind_Unit(s.windUnit));
    snprintf(a, sizeof(a), "from %s", tdw_Compass(w.windDir));
    mtb_Kit_Text(ux, TOP + 13, Terminal4x6, KIT_LABEL, a);
    const int bf = tdw_Beaufort(w.wind);
    snprintf(a, sizeof(a), "F%d %s", bf, tdw_Beaufort_Name(bf));
    mtb_Kit_Text(64, TOP + 33, Terminal6x8, bf >= 7 ? KIT_RED : bf >= 5 ? KIT_GOLD : KIT_GREEN, a);
    tdw_Wind_Text(b, sizeof(b), w.gust, s.windUnit);
    snprintf(a, sizeof(a), "Gusts %s", b);
    mtb_Kit_Text(64, TOP + 43, Terminal6x8, w.gust >= 60 ? KIT_RED : KIT_TEXT, a);
    if (!isnan(w.gustMax) && w.gustMax > w.gust + 5){
        tdw_Wind_Text(b, sizeof(b), w.gustMax, s.windUnit);
        snprintf(a, sizeof(a), "up to %s in %u h", b, (unsigned)s.leadHours);
        mtb_Kit_Text(64, TOP + 53, Terminal4x6, KIT_LABEL, a);
    }
    // Weather now.
    mtb_Kit_Weather_Icon(204, TOP + 13, 24, wx_Icon(w.code), !w.day);
    tdw_Temp_Text(a, sizeof(a), w.temp, s.imperial);
    mtb_Kit_Text_Right(253, TOP + 40, Terminal6x8, KIT_TEXT, a);
    fit_Right(253, TOP + 50, Terminal4x6, KIT_LABEL, tdw_Weather_Text(w.code), 90);
    // Rain, the next 12 hours.
    const int16_t y0 = TOP + 72, y1 = PANEL_RES_Y - 9;
    mtb_Kit_Text(2, y0 - 2, Terminal4x6, KIT_LABEL, "RAIN");
    mtb_Kit_Text(2, y0 + 5, Terminal4x6, KIT_DIM, "NEXT");
    mtb_Kit_Text(2, y0 + 12, Terminal4x6, KIT_DIM, "12 H");
    for (int i = 0; i < TDW_RAIN_HOURS; i++){
        const int16_t x = 30 + i * 19;
        const int16_t h = (int16_t)((y1 - y0) * w.rainProb[i] / 100);
        const uint16_t col = w.rainMm[i] >= 5 ? KIT_BLUE : w.rainProb[i] >= 50 ? KIT_CYAN : mtb_Kit_Dim(KIT_CYAN, 120);
        if (h > 0) mtb_Panel_Fill_Rect(x, y1 - h, x + 14, y1, col);
        mtb_Panel_Draw_HLine(y1, x, x + 14, KIT_LINE);
        if (i % 3 == 0){
            char c[8];
            tdw_Clock(c, sizeof(c), v.now + i * 3600u, v.d->utcOffset);
            c[2] = '\0';
            mtb_Kit_Text(x, y1 + 2, Terminal4x6, KIT_DIM, c);
        }
    }
    if (w.stormIn >= 0){
        if (w.stormIn == 0) snprintf(a, sizeof(a), "THUNDERSTORM NOW");
        else snprintf(a, sizeof(a), "THUNDER IN %d H", w.stormIn);
        const int16_t tw = mtb_Kit_Text_Width(Terminal6x8, a);
        mtb_Panel_Fill_Rect(253 - tw - 4, y0 - 4, 253, y0 + 6, KIT_RED);
        mtb_Kit_Text_Right(251, y0 - 3, Terminal6x8, KIT_TEXT, a, KIT_RED);
    }
}

// The river ----------------------------------------------------------------------------------------------------------
static uint16_t river_Color(uint8_t level){
    switch (level){
        case TDW_RIVER_LOW: return KIT_GOLD;
        case TDW_RIVER_NORMAL: return KIT_GREEN;
        case TDW_RIVER_HIGH: return KIT_ORANGE;
        case TDW_RIVER_VERY_HIGH: case TDW_RIVER_EXTREME: return KIT_RED;
        default: return KIT_LABEL;
    }
}

static void page_River(const Tdw_View_t& v){
    const Tdw_River_t& r = v.d->river;
    char a[64], b[24];
    clear();
    if (!r.known){ message("RIVER", r.updated ? "No river big enough for the flood model near this place. Set a river place in the phone app." : "Asking for the river..."); return; }
    mtb_Kit_Text(2, TOP + 2, Terminal6x8, KIT_GOLD, "RIVER");
    fit_Right(253, TOP + 3, Terminal4x6, KIT_LABEL, v.s->riverPlace[0] ? v.s->riverPlace : v.s->place, 200);
    const float q = r.q[r.today];
    thousands(b, sizeof(b), q);
    mtb_Kit_Text(2, TOP + 13, Terminal10x16, KIT_TEXT, b);
    const int16_t x = 6 + mtb_Kit_Text_Width(Terminal10x16, b);
    mtb_Kit_Text(x, TOP + 13, Terminal4x6, KIT_LABEL, "m3/s");
    mtb_Kit_Text(x, TOP + 20, Terminal4x6, KIT_DIM, "flowing");
    const uint8_t level = tdw_River_Level(r, q);
    if (level) mtb_Kit_Text(x + 36, TOP + 15, Terminal8x12, river_Color(level), tdw_River_Name(level));
    // Trend: today against yesterday.
    if (r.today > 0 && r.q[r.today - 1] > 0){
        const float change = (q - r.q[r.today - 1]) / r.q[r.today - 1] * 100;
        snprintf(a, sizeof(a), "%+.0f%% since yesterday", (double)change);
        mtb_Kit_Text(2, TOP + 31, Terminal4x6, KIT_LABEL, a);
    }
    if (r.climate){
        thousands(b, sizeof(b), r.p50);
        snprintf(a, sizeof(a), "Usual %s", b);
        mtb_Kit_Text_Right(253, TOP + 31, Terminal4x6, KIT_LABEL, a);
    }
    // 7 days back, the month ahead.
    const int16_t y0 = TOP + 40, y1 = PANEL_RES_Y - 16;
    float hi = 0;
    for (int i = 0; i < r.n; i++) hi = fmaxf(hi, r.q[i]);
    if (r.climate) hi = fmaxf(hi, fminf(r.p99, hi * 1.6f));
    hi *= 1.08f;
    if (hi <= 0) return;
    const int16_t bw = (int16_t)(252 / (r.n > 0 ? r.n : 1));
    for (int i = 0; i < r.n; i++){
        if (r.q[i] < 0) continue;
        const int16_t h = (int16_t)((y1 - y0) * r.q[i] / hi);
        const int16_t bx = 2 + i * bw;
        uint16_t col = river_Color(tdw_River_Level(r, r.q[i]));
        if (!r.climate) col = HIGH_C;
        if (i < r.today) col = mtb_Kit_Dim(col, 110);
        mtb_Panel_Fill_Rect(bx, y1 - h, bx + bw - 2, y1, col);
    }
    const int16_t tx = 2 + r.today * bw + bw / 2;
    mtb_Panel_Draw_VLine(tx, y0 - 2, y1, KIT_TEXT);
    if (r.climate){
        const float lines[2] = {r.p90, r.p99};
        const uint16_t cols[2] = {KIT_ORANGE, KIT_RED};
        for (int k = 0; k < 2; k++){
            if (lines[k] >= hi) continue;
            const int16_t y = (int16_t)(y1 - (y1 - y0) * lines[k] / hi);
            for (int xx = 2; xx < 254; xx += 4) mtb_Panel_Draw_HLine(y, xx, xx + 1, cols[k]);
        }
    }
    mtb_Kit_Text(2, y1 + 3, Terminal4x6, KIT_DIM, "-7 d");
    mtb_Kit_Text_Centre(tx, y1 + 3, Terminal4x6, KIT_TEXT, "TODAY");
    mtb_Kit_Text_Right(253, y1 + 3, Terminal4x6, KIT_DIM, "+30 d");
    if (r.peak > q * 1.05f){
        thousands(b, sizeof(b), r.peak);
        snprintf(a, sizeof(a), "Peak %s on %s", b, tdw_Weekday(r.peakDay + 43200, 0));
        mtb_Kit_Text_Right(253, y1 + 9, Terminal4x6, river_Color(tdw_River_Level(r, r.peak)), a);
    }
    mtb_Kit_Text(2, y1 + 9, Terminal4x6, KIT_DIM, "GloFAS flood model");
}

// Sun, moon and spring tides -----------------------------------------------------------------------------------
static void page_Sky(const Tdw_View_t& v){
    const Tdw_Weather_t& w = v.d->wx;
    const int32_t off = v.d->utcOffset;
    char a[64], b[16], c[16];
    clear();
    title("SUN & MOON", v);
    // Sun.
    mtb_Panel_Fill_Circle(16, TOP + 30, 7, KIT_GOLD);
    mtb_Panel_Fill_Rect(4, TOP + 31, 28, TOP + 38, BLACK);
    mtb_Panel_Draw_HLine(TOP + 31, 2, 30, KIT_GOLD);
    if (w.sunrise[0]){
        tdw_Clock(b, sizeof(b), w.sunrise[0], off);
        tdw_Clock(c, sizeof(c), w.sunset[0], off);
        mtb_Kit_Text(38, TOP + 16, Terminal4x6, KIT_LABEL, "SUNRISE");
        mtb_Kit_Text(38, TOP + 23, Terminal8x12, KIT_TEXT, b);
        mtb_Kit_Text(92, TOP + 16, Terminal4x6, KIT_LABEL, "SUNSET");
        mtb_Kit_Text(92, TOP + 23, Terminal8x12, KIT_TEXT, c);
        tdw_Span(a, sizeof(a), (int32_t)(w.sunset[0] - w.sunrise[0]));
        snprintf(b, sizeof(b), "%s", "");
        char d[40];
        snprintf(d, sizeof(d), "Daylight %s", a);
        mtb_Kit_Text(38, TOP + 38, Terminal4x6, KIT_LABEL, d);
    }
    // Moon.
    const float age = mtb_Kit_Moon_Age(v.now);
    mtb_Kit_Moon(210, TOP + 30, 18, age);
    mtb_Kit_Text_Centre(210, TOP + 51, Terminal4x6, KIT_LABEL, mtb_Kit_Moon_Name(age));
    // Spring and neap tides.
    mtb_Panel_Draw_HLine(TOP + 60, 0, 255, KIT_LINE);
    const char* sn = tdw_Spring_Neap(age);
    if (sn[0]) snprintf(a, sizeof(a), "%s now", sn);
    else {
        const float days = tdw_Days_To_Spring(age);
        snprintf(a, sizeof(a), "Spring tides in %.0f day%s", (double)days, days >= 0.5f && days < 1.5f ? "" : "s");
    }
    mtb_Kit_Text(2, TOP + 66, Terminal6x8, KIT_CYAN, a);
    mtb_Kit_Text(2, TOP + 77, Terminal4x6, KIT_LABEL, !strcmp(sn, "Neap tides") ? "Half moon: the smallest range between high and low water."
                                                                               : "New and full moon bring the highest highs and the lowest lows.");
    const float range = tdw_Range(v.d->tide, v.now - 43200);
    if (v.d->tide.state == TDW_OK && !isnan(range)){
        tdw_Height_Text(b, sizeof(b), range, v.s->imperial);
        snprintf(a, sizeof(a), "Today's range: %s", b);
        mtb_Kit_Text(2, TOP + 88, Terminal6x8, KIT_TEXT, a);
    }
    if (v.d->tzName[0]){ snprintf(a, sizeof(a), "Times at the place (%s)", v.d->tzName); mtb_Kit_Text(2, PANEL_RES_Y - 7, Terminal4x6, KIT_DIM, a); }
}

// Alerts ---------------------------------------------------------------------------------------------------------------
static void page_Alerts(const Tdw_View_t& v){
    clear();
    mtb_Panel_Fill_Rect(0, TOP, 255, TOP + 11, KIT_RED);
    mtb_Kit_Text(3, TOP + 2, Terminal6x8, KIT_TEXT, "WATER ALERTS", KIT_RED);
    fit_Right(253, TOP + 3, Terminal4x6, KIT_TEXT, v.s->place, 150, KIT_RED);
    int16_t y = TOP + 15;
    for (int i = 0; i < v.d->alertCount && y < PANEL_RES_Y - 8; i++){
        char ascii[128], lines[3][64];
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), v.d->alerts[i].text);
        const int n = mtb_Kit_Wrap(ascii, Terminal6x8, 244, &lines[0][0], sizeof(lines[0]), 3);
        mtb_Panel_Fill_Circle(4, y + 3, 2, KIT_RED);
        for (int k = 0; k < n && y < PANEL_RES_Y - 8; k++, y += 10) mtb_Kit_Text(10, y, Terminal6x8, KIT_TEXT, lines[k]);
        y += 3;
    }
}

void tdw_Draw(const Tdw_View_t& v){
    mtb_Panel_Begin_Update();
    if (v.message) message("TIDES & WATER", v.message);
    else switch (v.page){
        case TDW_P_TIDE: page_Tide(v); break;
        case TDW_P_TABLE: page_Table(v); break;
        case TDW_P_SEA: page_Sea(v); break;
        case TDW_P_WIND: page_Wind(v); break;
        case TDW_P_RIVER: page_River(v); break;
        case TDW_P_SKY: page_Sky(v); break;
        case TDW_P_ALERTS: page_Alerts(v); break;
        default: break;
    }
    mtb_Panel_End_Update();
}
