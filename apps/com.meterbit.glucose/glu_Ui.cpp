// Glucose Display's screens, the whole 256 x 128 panel: the reading now (large, with its trend and the last hours), a
// longer graph, and the day's numbers. An alarm turns the top line into a banner and makes the frame blink.
#include "glu_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint16_t C_URGENT = MTB_KIT_RGB(255, 30, 30);
static const uint16_t C_LOW    = MTB_KIT_RGB(255, 80, 40);
static const uint16_t C_IN     = MTB_KIT_RGB(40, 220, 90);
static const uint16_t C_HIGH   = MTB_KIT_RGB(255, 200, 0);
static const uint16_t C_VHIGH  = MTB_KIT_RGB(255, 120, 0);
static const uint16_t C_STALE  = MTB_KIT_RGB(110, 110, 110);
static const uint16_t C_BAND   = MTB_KIT_RGB(0, 40, 14);

struct Ctx_t {
    const Glu_View_t* v;
    uint8_t units;
    uint16_t uLow, low, high, uHigh;
    uint8_t level;                      /**< 255, or less at night */
};

static uint16_t lit(const Ctx_t& c, uint16_t color){ return c.level >= 255 ? color : mtb_Kit_Dim(color, c.level); }

static uint16_t range_Color(uint8_t range){
    switch (range){
        case GLU_RANGE_URGENT_LOW: return C_URGENT;
        case GLU_RANGE_LOW: return C_LOW;
        case GLU_RANGE_HIGH: return C_HIGH;
        case GLU_RANGE_URGENT_HIGH: return C_VHIGH;
        default: return C_IN;
    }
}

static uint16_t value_Color(const Ctx_t& c, uint16_t mgdl){ return range_Color(glu_Range(mgdl, c.uLow, c.low, c.high, c.uHigh)); }

static void fit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text(x, y, font, color, fitted);
}

static void clock_Text(char* d, size_t n, uint32_t t, int32_t offset, bool hour24){
    const uint32_t local = (uint32_t)((int64_t)t + offset);
    const int h = (int)((local / 3600) % 24), m = (int)((local / 60) % 60);
    if (hour24) snprintf(d, n, "%02d:%02d", h, m);
    else snprintf(d, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}

/** The top line: whose readings and the clock, or the alarm's banner. */
static void top_Line(const Ctx_t& c, const char* title){
    const Glu_View_t& v = *c.v;
    char t[32];
    clock_Text(t, sizeof(t), v.now, v.utcOffset, v.s->hour24);
    if (v.alarm != GLU_ALARM_NONE){
        const bool lowKind = v.alarm <= GLU_FALLING_FAST;
        const uint16_t bg = v.alarm == GLU_STALE ? KIT_DIM : lowKind ? C_URGENT : C_VHIGH;
        mtb_Panel_Fill_Rect(0, 0, PANEL_RES_X - 1, 10, v.flash ? bg : mtb_Kit_Dim(bg, 120));
        mtb_Kit_Text(3, 2, Terminal6x8, WHITE, glu_Alarm_Text(v.alarm), v.flash ? bg : mtb_Kit_Dim(bg, 120));
        mtb_Kit_Text_Right(PANEL_RES_X - 3, 2, Terminal6x8, WHITE, "CLICK: SNOOZE", v.flash ? bg : mtb_Kit_Dim(bg, 120));
        return;
    }
    const int16_t cx = mtb_Kit_Text_Right(PANEL_RES_X - 2, 1, Terminal6x8, lit(c, KIT_LABEL), t);
    char left[48];
    snprintf(left, sizeof(left), "%s%s%s", v.s->name[0] ? v.s->name : "", v.s->name[0] && title[0] ? "  " : "", title);
    fit_Text(2, 1, Terminal6x8, lit(c, KIT_TEXT), left, (int16_t)(cx - 30));
    if (v.snoozed) mtb_Kit_Text_Right((int16_t)(cx - 6), 1, Terminal6x8, lit(c, KIT_GOLD), "ZZ");
}

/** The value in seven-segment digits from @p x; returns the x after it. */
static int16_t big_Value(int16_t x, int16_t y, const char* text, uint16_t color){
    const int16_t w = 28, h = 54, t = 6, gap = 5;
    for (const char* p = text; *p; p++){
        if (*p == '.'){ mtb_Panel_Fill_Rect(x, (int16_t)(y + h - t), (int16_t)(x + t - 1), (int16_t)(y + h - 1), color); x = (int16_t)(x + t + gap); }
        else if (*p >= '0' && *p <= '9'){ mtb_Kit_Segment_Digit(x, y, w, h, t, *p - '0', color); x = (int16_t)(x + w + gap); }
        else if (*p == '-'){ mtb_Panel_Fill_Rect(x, (int16_t)(y + (h - t) / 2), (int16_t)(x + w - 1), (int16_t)(y + (h + t) / 2 - 1), color); x = (int16_t)(x + w + gap); }
    }
    return x;
}

/** A thick arrow from the centre (cx, cy) at @p degrees (0 right, 90 up). */
static void arrow(int16_t cx, int16_t cy, float degrees, int16_t len, uint16_t color){
    const float a = degrees * 3.14159265f / 180.0f, dx = cosf(a), dy = -sinf(a);
    const float px = -dy, py = dx;                              // across the shaft
    const float tailX = cx - dx * len / 2, tailY = cy - dy * len / 2;
    const float tipX = cx + dx * len / 2, tipY = cy + dy * len / 2;
    const float headLen = len * 0.42f, headW = len * 0.3f;
    const float baseX = tipX - dx * headLen, baseY = tipY - dy * headLen;
    // The shaft: a solid band, two triangles (lines side by side leave gaps on the diagonals).
    const float sw = 3.5f;
    const int16_t ax = (int16_t)lroundf(tailX + px * sw), ay = (int16_t)lroundf(tailY + py * sw);
    const int16_t bx = (int16_t)lroundf(tailX - px * sw), by = (int16_t)lroundf(tailY - py * sw);
    const int16_t cx2 = (int16_t)lroundf(baseX + dx + px * sw), cy2 = (int16_t)lroundf(baseY + dy + py * sw);
    const int16_t dx2 = (int16_t)lroundf(baseX + dx - px * sw), dy2 = (int16_t)lroundf(baseY + dy - py * sw);
    mtb_Panel_Fill_Triangle(ax, ay, bx, by, cx2, cy2, color);
    mtb_Panel_Fill_Triangle(bx, by, cx2, cy2, dx2, dy2, color);
    mtb_Panel_Fill_Triangle((int16_t)lroundf(tipX), (int16_t)lroundf(tipY), (int16_t)lroundf(baseX + px * headW), (int16_t)lroundf(baseY + py * headW),
                            (int16_t)lroundf(baseX - px * headW), (int16_t)lroundf(baseY - py * headW), color);
}

static void trend_Arrow(int16_t cx, int16_t cy, uint8_t trend, uint16_t color){
    switch (trend){
        case GLU_DOUBLE_UP: arrow((int16_t)(cx - 12), cy, 90, 40, color); arrow((int16_t)(cx + 12), cy, 90, 40, color); break;
        case GLU_SINGLE_UP: arrow(cx, cy, 90, 44, color); break;
        case GLU_45_UP: arrow(cx, cy, 45, 44, color); break;
        case GLU_FLAT: arrow(cx, cy, 0, 44, color); break;
        case GLU_45_DOWN: arrow(cx, cy, -45, 44, color); break;
        case GLU_SINGLE_DOWN: arrow(cx, cy, -90, 44, color); break;
        case GLU_DOUBLE_DOWN: arrow((int16_t)(cx - 12), cy, -90, 40, color); arrow((int16_t)(cx + 12), cy, -90, 40, color); break;
        case GLU_NOT_COMPUTABLE: case GLU_OUT_OF_RANGE: mtb_Kit_Big_Text((int16_t)(cx - 10), (int16_t)(cy - 16), Terminal10x16, 2, color, "?"); break;
        default: break;
    }
}

/** A reading's row in a graph from @p y0 (400 mg/dL) to @p y1 (40): a log scale, as Nightscout's, so lows get room. */
static int16_t graph_Y(float mg, int16_t y0, int16_t y1){
    if (mg > 400) mg = 400;
    if (mg < 40) mg = 40;
    return (int16_t)lroundf(y1 - logf(mg / 40.0f) / logf(10.0f) * (y1 - y0));
}

/**
 * The readings of the last @p hours in a box: the target band shaded, the low and high lines, each reading a dot in its
 * range's colour, hour ticks along the bottom.
 */
static void graph(const Ctx_t& c, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int hours, bool labels){
    const Glu_View_t& v = *c.v;
    const Glu_History_t& h = v.d->hist;
    const int16_t W = (int16_t)(x1 - x0);
    auto yOf = [&](float mg){ return graph_Y(mg, y0, y1); };
    const uint32_t span = (uint32_t)hours * 3600u, from = v.now - span;
    auto xOf = [&](uint32_t t){ return (int16_t)(x0 + (int32_t)((int64_t)(t - from) * W / (int64_t)span)); };
    // The band and its lines.
    const int16_t yl = yOf(c.low), yh = yOf(c.high);
    mtb_Panel_Fill_Rect(x0, yh, x1, yl, lit(c, C_BAND));
    for (int16_t x = x0; x <= x1; x += 3){
        mtb_Panel_Draw_PixelRGB565(x, yOf(c.uLow), lit(c, mtb_Kit_Dim(C_URGENT, 150)));
        mtb_Panel_Draw_PixelRGB565(x, yl, lit(c, mtb_Kit_Dim(C_IN, 170)));
        mtb_Panel_Draw_PixelRGB565(x, yh, lit(c, mtb_Kit_Dim(C_HIGH, 150)));
    }
    // Hour ticks.
    const int step = hours <= 3 ? 1 : hours <= 6 ? 1 : hours <= 12 ? 3 : 6;
    const uint32_t local = (uint32_t)((int64_t)v.now + v.utcOffset);
    uint32_t tick = v.now - (local % 3600u);
    for (int i = 0; i <= hours; i++, tick -= 3600u){
        if (tick < from) break;
        const int hourOfDay = (int)(((int64_t)tick + v.utcOffset) / 3600 % 24);
        if (hourOfDay % step) continue;
        const int16_t x = xOf(tick);
        for (int16_t y = y0; y <= y1; y += 4) mtb_Panel_Draw_PixelRGB565(x, y, lit(c, KIT_LINE));
        if (labels && x > x0 + 6 && x < x1 - 8){
            char t[8];
            snprintf(t, sizeof(t), "%d", v.s->hour24 ? hourOfDay : (hourOfDay % 12 ? hourOfDay % 12 : 12));
            mtb_Kit_Text_Centre(x, (int16_t)(y1 + 2), Terminal4x6, lit(c, KIT_DIM), t);
        }
    }
    // The readings: dots, two pixels across when the window is short.
    const int dot = hours <= 6 ? 2 : 1;
    for (int i = 0; i < h.count; i++){
        const Glu_Reading_t& r = h.r[i];
        if (r.t < from || r.t > v.now + 60) continue;
        int16_t x = xOf(r.t), y = yOf(r.mgdl);
        if (x > x1 - 2) x = (int16_t)(x1 - 2);
        const uint16_t col = lit(c, value_Color(c, r.mgdl));
        if (i == h.count - 1) mtb_Panel_Fill_Rect((int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(x + 2), (int16_t)(y + 2), col);
        else if (dot == 2) mtb_Panel_Fill_Rect(x, y, (int16_t)(x + 1), (int16_t)(y + 1), col);
        else mtb_Panel_Draw_PixelRGB565(x, y, col);
    }
}

static void now_Screen(const Ctx_t& c){
    const Glu_View_t& v = *c.v;
    const Glu_Data_t& d = *v.d;
    top_Line(c, "");
    const Glu_Reading_t* r = glu_Latest(d.hist);
    const uint32_t age = r && v.now > r->t ? v.now - r->t : 0;
    const bool stale = !r || age > (uint32_t)v.s->staleMinutes * 60u, gone = !r || age > 3600;
    char t[48];
    if (gone){
        big_Value(6, 14, "---", lit(c, C_STALE));
    } else {
        glu_Value_Text(t, sizeof(t), r->mgdl, c.units);
        const uint16_t color = stale ? C_STALE : value_Color(c, r->mgdl);
        const int16_t after = big_Value(6, 14, t, lit(c, color));
        const uint8_t trend = r->trend != GLU_TREND_NONE ? r->trend : glu_Trend_From_History(d.hist);
        if (!stale) trend_Arrow((int16_t)(after + 26), 41, trend, lit(c, color));
    }
    // The right column: units, change, age, and what Loop / AAPS add.
    const int16_t rx = 188;
    mtb_Kit_Text(rx, 16, Terminal6x8, lit(c, KIT_LABEL), glu_Units_Name(c.units));
    int delta;
    if (r && !stale && glu_Delta(d.hist, delta)){
        glu_Delta_Text(t, sizeof(t), delta, c.units);
        mtb_Kit_Text(rx, 27, Terminal10x16, lit(c, KIT_TEXT), t);
    }
    if (r){
        if (age < 90) snprintf(t, sizeof(t), "NOW");
        else if (age < 3600) snprintf(t, sizeof(t), "%lu MIN AGO", (unsigned long)(age / 60));
        else mtb_Kit_Ago(t, sizeof(t), (int32_t)age);
        mtb_Kit_Upper(t);
        mtb_Kit_Text(rx, 46, Terminal6x8, lit(c, stale ? C_HIGH : KIT_LABEL), t);
    }
    int16_t y = 57;
    if (d.extra.hasIob){ snprintf(t, sizeof(t), "IOB %.2fU", d.extra.iob); mtb_Kit_Text(rx, y, Terminal6x8, lit(c, KIT_CYAN), t); y = (int16_t)(y + 9); }
    if (d.extra.hasCob && y < 72){ snprintf(t, sizeof(t), "COB %.0fG", d.extra.cob); mtb_Kit_Text(rx, y, Terminal6x8, lit(c, KIT_ORANGE), t); }
    // Why there is no reading.
    int16_t graphTop = 80;
    if (gone || (d.state != GLU_NET_OK && d.state != GLU_NET_CONNECTING)){
        char ascii[96], lines[2][64];
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), d.state == GLU_NET_OK ? "NO READINGS FROM THE SENSOR" : d.error);
        const int n = mtb_Kit_Wrap(ascii, Terminal6x8, 244, &lines[0][0], sizeof(lines[0]), 2);
        if (n > 1) graphTop = 88;
        for (int i = 0; i < n; i++)
            mtb_Kit_Text(6, (int16_t)(70 + i * 8), Terminal6x8, lit(c, d.state == GLU_NET_OK ? C_STALE : C_HIGH), lines[i]);
    } else if (stale){
        snprintf(t, sizeof(t), "NO NEW READING FOR %lu MIN", (unsigned long)(age / 60));
        mtb_Kit_Text(6, 70, Terminal6x8, lit(c, C_HIGH), t);
    }
    graph(c, 2, graphTop, 253, 119, v.graphHours, true);
}

static void graph_Screen(const Ctx_t& c){
    const Glu_View_t& v = *c.v;
    char title[24];
    snprintf(title, sizeof(title), "LAST %u HOURS", v.graphHours);
    top_Line(c, title);
    // Scale labels in the units shown.
    const float marks[4] = {(float)c.uLow, (float)c.low, (float)c.high, (float)c.uHigh};
    for (float m : marks){
        char t[8];
        glu_Value_Text(t, sizeof(t), (uint16_t)m, c.units);
        const int16_t y = graph_Y(m, 14, 118);
        mtb_Kit_Text_Right(253, (int16_t)(y - 3), Terminal4x6, lit(c, KIT_LABEL), t);
    }
    graph(c, 2, 14, 236, 118, v.graphHours, true);
    const Glu_Reading_t* r = glu_Latest(v.d->hist);
    if (r){
        char t[16];
        glu_Value_Text(t, sizeof(t), r->mgdl, c.units);
        mtb_Kit_Text(4, 15, Terminal8x12, lit(c, value_Color(c, r->mgdl)), t);
    }
}

static void stats_Screen(const Ctx_t& c){
    const Glu_View_t& v = *c.v;
    top_Line(c, "LAST 24 HOURS");
    Glu_Stats_t st;
    glu_Stats(v.d->hist, v.now, 24, v.s->targetLow, v.s->targetHigh, c.uLow, st);
    if (st.readings < 12){ mtb_Kit_Text_Centre(128, 60, Terminal6x8, lit(c, KIT_LABEL), "NOT ENOUGH READINGS YET"); return; }
    char t[48];
    snprintf(t, sizeof(t), "%.0f%%", st.inRange);
    mtb_Kit_Big_Text(6, 16, Terminal10x16, 2, lit(c, C_IN), t);
    char a[12], b[12];
    glu_Value_Text(a, sizeof(a), v.s->targetLow, c.units);
    glu_Value_Text(b, sizeof(b), v.s->targetHigh, c.units);
    snprintf(t, sizeof(t), "IN RANGE %s-%s", a, b);
    mtb_Kit_Text(6, 50, Terminal6x8, lit(c, KIT_LABEL), t);
    // Time in ranges, one bar: urgent low, low, in, high.
    const int16_t bx = 6, bw = 244, by = 62, bh = 10;
    const float parts[4] = {st.urgentBelow, st.below - st.urgentBelow, st.inRange, st.above};
    const uint16_t cols[4] = {C_URGENT, C_LOW, C_IN, C_HIGH};
    int16_t x = bx;
    for (int i = 0; i < 4; i++){
        int16_t w = (int16_t)lroundf(parts[i] / 100.0f * bw);
        if (parts[i] > 0 && w < 2) w = 2;
        if (x + w > bx + bw) w = (int16_t)(bx + bw - x);
        if (w > 0) mtb_Panel_Fill_Rect(x, by, (int16_t)(x + w - 1), (int16_t)(by + bh - 1), lit(c, cols[i]));
        x = (int16_t)(x + w);
    }
    snprintf(t, sizeof(t), "BELOW %.0f%%", st.below);
    mtb_Kit_Text(6, 76, Terminal6x8, lit(c, C_LOW), t);
    snprintf(t, sizeof(t), "ABOVE %.0f%%", st.above);
    mtb_Kit_Text_Right(250, 76, Terminal6x8, lit(c, C_HIGH), t);
    // Average, GMI, variability, lows.
    glu_Value_Text(a, sizeof(a), (uint16_t)lroundf(st.mean), c.units);
    struct Cell_t { const char* label; char value[16]; };
    Cell_t cells[4] = {{"AVERAGE", ""}, {"GMI", ""}, {"CV", ""}, {"LOWS", ""}};
    snprintf(cells[0].value, sizeof(cells[0].value), "%s", a);
    snprintf(cells[1].value, sizeof(cells[1].value), "%.1f%%", st.gmi);
    snprintf(cells[2].value, sizeof(cells[2].value), "%.0f%%", st.cv);
    snprintf(cells[3].value, sizeof(cells[3].value), "%d", st.lows);
    for (int i = 0; i < 4; i++){
        const int16_t cx = (int16_t)(32 + i * 64);
        mtb_Kit_Text_Centre(cx, 90, Terminal4x6, lit(c, KIT_LABEL), cells[i].label);
        mtb_Kit_Text_Centre(cx, 98, Terminal8x12, lit(c, i == 2 && st.cv > 36 ? C_HIGH : KIT_TEXT), cells[i].value);
    }
    snprintf(t, sizeof(t), "%d READINGS OVER %.0f H", st.readings, st.hours);
    mtb_Kit_Text_Centre(128, 118, Terminal4x6, lit(c, KIT_DIM), t);
}

static void message(const char* text){
    mtb_Kit_Text_Centre(128, 40, Terminal8x12, KIT_GOLD, "GLUCOSE DISPLAY");
    char lines[3][64];
    const int n = mtb_Kit_Wrap(text, Terminal6x8, 240, &lines[0][0], sizeof(lines[0]), 3);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, (int16_t)(62 + i * 11), Terminal6x8, KIT_TEXT, lines[i]);
    mtb_Kit_Text_Centre(128, 112, Terminal4x6, KIT_DIM, "NOT A MEDICAL DEVICE. FOLLOW YOUR CGM APP.");
}

void glu_Draw(const Glu_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, 0, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.message) message(v.message);
    else {
        Ctx_t c = {&v, glu_Units(*v.s, &v.d->site), 0, 0, 0, 0, (uint8_t)(v.dim && v.alarm == GLU_ALARM_NONE ? 90 : 255)};
        glu_Ranges(*v.s, &v.d->site, c.uLow, c.low, c.high, c.uHigh);
        switch (v.screen){
            case GLU_SCREEN_GRAPH: graph_Screen(c); break;
            case GLU_SCREEN_STATS: stats_Screen(c); break;
            default: now_Screen(c); break;
        }
        if (v.alarm != GLU_ALARM_NONE && v.flash){
            const uint16_t edge = v.alarm <= GLU_FALLING_FAST ? C_URGENT : v.alarm == GLU_STALE ? KIT_DIM : C_VHIGH;
            for (int i = 0; i < 2; i++) mtb_Panel_Draw_Rect(i, (int16_t)(11 + i), (int16_t)(PANEL_RES_X - 1 - i), (int16_t)(PANEL_RES_Y - 1 - i), edge);
        }
    }
    mtb_Panel_End_Update();
}
