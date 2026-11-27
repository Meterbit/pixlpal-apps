#include "oil_Ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mtb_graphics.h"

#define OIL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_UP       = OIL_RGB(40, 225, 100);
static const uint16_t C_UP_DIM   = OIL_RGB(0, 56, 26);
static const uint16_t C_DOWN     = OIL_RGB(255, 64, 48);
static const uint16_t C_DOWN_DIM = OIL_RGB(72, 14, 8);
static const uint16_t C_AMBER    = OIL_RGB(255, 176, 0);
static const uint16_t C_ACCENT   = OIL_RGB(255, 150, 20);
static const uint16_t C_TEXT     = OIL_RGB(235, 235, 235);
static const uint16_t C_LABEL    = OIL_RGB(130, 130, 130);
static const uint16_t C_DIM      = OIL_RGB(80, 80, 80);
static const uint16_t C_LINE     = OIL_RGB(48, 48, 48);
static const uint16_t C_SELECTED = OIL_RGB(84, 48, 8);
static const uint16_t C_NEUTRAL  = OIL_RGB(150, 150, 150);

// Layout. Row and column numbers are inclusive.
static const int16_t LEFT_X = 3;
static const int16_t RIGHT_X = 253;     // mtb_Write_String() drops a glyph whose advance reaches the panel edge.
static const int16_t HERO_Y1 = 10, HERO_Y2 = 40, HERO_RULE_Y = 41;
static const int16_t PAGE_Y1 = 43, PAGE_Y2 = 106;
static const int16_t STRIP_RULE_Y = 108, STRIP_Y1 = 109, STRIP_Y2 = 117, TICKER_RULE_Y = 118;
static const int32_t TREND_DAYS = 91;
static const int32_t RANGE_DAYS = 182;

//##############################################################################################################
// Pixel width of a string, using the same rules as Mtb_Static_Text_t::mtb_Write_String().
static int16_t oil_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

// The widget paints its background behind the whole string, so text on a coloured box passes that colour.
static int16_t oil_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK){
    if (text[0] == '\0') return x;
    Mtb_FixedText_t widget(x < 0 ? 0 : x, y, font, color, background);
    widget.mtb_Write_String(text);
    return x + oil_Text_Width(font, text);
}

static int16_t oil_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK){
    const int16_t x = rightX - oil_Text_Width(font, text) + 1;
    oil_Text(x, y, font, color, text, background);
    return x;
}

static void oil_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    oil_Text(centreX - oil_Text_Width(font, text) / 2, y, font, color, text);
}

// Copies text into dest, cut to fit maxWidth pixels.
static void oil_Fit_Text(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth){
    snprintf(dest, size, "%s", text);
    for (size_t length = strlen(dest); length > 0 && oil_Text_Width(font, dest) > maxWidth; length--) dest[length - 1] = '\0';
    size_t length = strlen(dest);
    while (length > 0 && dest[length - 1] == ' ') dest[--length] = '\0';
}

// In these fonts a comma passes for a decimal point, so digit groups are set apart by a gap instead.
static const int16_t GROUP_GAP = 4;

static int16_t oil_Grouped_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    char group[24];
    size_t n = 0;
    for (const char* c = text; ; c++){
        if (*c == ',' || *c == '\0'){
            group[n] = '\0';
            x = oil_Text(x, y, font, color, group);
            n = 0;
            if (*c == '\0') return x;
            x += GROUP_GAP;
        } else if (n + 1 < sizeof(group)) group[n++] = *c;
    }
}

static uint16_t oil_Trend_Color(float value){ return isnan(value) || value == 0 ? C_NEUTRAL : value < 0 ? C_DOWN : C_UP; }

static void oil_Draw_Triangle(int16_t x, int16_t y, int16_t size, bool up, uint16_t color){
    if (up) mtb_Panel_Fill_Triangle(x, y + size - 1, x + 2 * (size - 1), y + size - 1, x + size - 1, y, color);
    else mtb_Panel_Fill_Triangle(x, y, x + 2 * (size - 1), y, x + size - 1, y + size - 1, color);
}

static void oil_Dotted_HLine(int16_t y, int16_t x1, int16_t x2, uint16_t color){
    for (int16_t x = x1; x <= x2; x += 3) mtb_Panel_Draw_PixelRGB565(x, y, color);
}

//##############################################################################################################
const char* oil_Ui_Unit_Text(uint8_t unit){
    switch (unit){
        case OIL_UNIT_USD_BBL:   return "$/bbl";
        case OIL_UNIT_USD_GAL:   return "$/gal";
        case OIL_UNIT_USD_MMBTU: return "$/MMBtu";
        default:                 return "kb/d";
    }
}

void oil_Ui_Format_Value(char* dest, size_t size, float value, uint8_t unit){
    if (isnan(value)){ snprintf(dest, size, "--"); return; }
    if (unit == OIL_UNIT_USD_GAL){ snprintf(dest, size, "%.3f", value); return; }
    if (unit != OIL_UNIT_KBD){ snprintf(dest, size, "%.2f", value); return; }
    const long whole = lroundf(value);
    if (labs(whole) >= 1000) snprintf(dest, size, "%ld,%03ld", whole / 1000, labs(whole) % 1000);
    else snprintf(dest, size, "%ld", whole);
}

void oil_Ui_Format_Percent(char* dest, size_t size, float percent){
    if (isnan(percent)) snprintf(dest, size, "--");
    else snprintf(dest, size, fabsf(percent) < 100 ? "%+.2f%%" : "%+.0f%%", percent);
}

static void oil_Format_Change(char* dest, size_t size, float change, uint8_t unit){
    char value[16];
    oil_Ui_Format_Value(value, sizeof(value), fabsf(change), unit);
    snprintf(dest, size, "%c%s", change < 0 ? '-' : '+', value);
}

static const Oil_Series_t* oil_Selected(const Oil_View_t& view){
    if (view.data == nullptr || view.selected < 0 || view.selected >= view.data->seriesCount) return nullptr;
    return &view.data->series[view.selected];
}

static void oil_Draw_Notice(const char* text, int16_t y1, int16_t y2){
    // Up to three lines, broken at spaces.
    char line[48];
    const uint8_t* font = Terminal6x8;
    const int16_t maxWidth = RIGHT_X - LEFT_X;
    const char* rest = text;
    int16_t y = (y1 + y2) / 2 - 13;
    for (int lineNo = 0; lineNo < 3 && *rest; lineNo++, y += 10){
        size_t take = 0, lastSpace = 0;
        while (rest[take] && take + 1 < sizeof(line)){
            memcpy(line, rest, take + 1);
            line[take + 1] = '\0';
            if (oil_Text_Width(font, line) > maxWidth) break;
            if (rest[take] == ' ') lastSpace = take;
            take++;
        }
        if (rest[take] && lastSpace > 0) take = lastSpace;
        memcpy(line, rest, take);
        line[take] = '\0';
        oil_Text_Centre(128, y, font, C_LABEL, line);
        rest += take;
        while (*rest == ' ') rest++;
    }
}

//##############################################################################################################
void oil_Ui_Clear(void){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, TICKER_RULE_Y, BLACK);   // not the ticker below: Pixlpal scrolls it
    mtb_Panel_Draw_HLine(HERO_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(STRIP_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(TICKER_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
}

void oil_Ui_Draw_Hero(const Oil_View_t& view){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, HERO_Y2, BLACK);
    const Oil_Series_t* series = oil_Selected(view);
    if (series == nullptr) return;
    const Oil_Point_t* last = oil_Series_Last(*series);
    char text[64], fitted[64];

    oil_Text(LEFT_X, 11, Terminal8x12, C_ACCENT, series->code);

    // Change since the previous published value, top right.
    float change = NAN, percent = NAN;
    if (oil_Series_Change(*series, 0, change, percent)){
        const uint16_t color = oil_Trend_Color(change);
        char changeText[16], percentText[16];
        oil_Format_Change(changeText, sizeof(changeText), change, series->unit);
        oil_Ui_Format_Percent(percentText, sizeof(percentText), percent);
        snprintf(text, sizeof(text), "%s %s", changeText, percentText);
        const int16_t x = oil_Text_Right(RIGHT_X, 11, Terminal8x12, color, text);
        if (change != 0) oil_Draw_Triangle(x - 13, 13, 6, change > 0, color);
    }

    // The value, large, with its unit.
    int16_t x = LEFT_X;
    if (last != nullptr){
        oil_Ui_Format_Value(text, sizeof(text), last->value, series->unit);
        x = (series->unit == OIL_UNIT_KBD) ? oil_Grouped_Text(LEFT_X, 24, Terminal10x16, C_TEXT, text) : oil_Text(LEFT_X, 24, Terminal10x16, C_TEXT, text);
    } else x = oil_Text(LEFT_X, 24, Terminal10x16, C_DIM, "--");
    x = oil_Text(x + 4, 32, Terminal6x8, C_LABEL, oil_Ui_Unit_Text(series->unit));

    // What the number is, and where and when it is from: always on screen.
    const int16_t columnX = x + 8;
    oil_Fit_Text(fitted, sizeof(fitted), Terminal6x8, series->name, RIGHT_X - columnX);
    oil_Text_Right(RIGHT_X, 24, Terminal6x8, C_LABEL, fitted);
    if (last != nullptr){
        char date[16];
        oil_Format_Short_Date(date, sizeof(date), last->day, series->cadence == OIL_MONTHLY);
        snprintf(text, sizeof(text), "%s  %s", view.data->sourceName, date);
    } else snprintf(text, sizeof(text), "%s", view.data->sourceName);
    oil_Fit_Text(fitted, sizeof(fitted), Terminal6x8, text, RIGHT_X - columnX);
    oil_Text_Right(RIGHT_X, 33, Terminal6x8, view.stale ? C_AMBER : C_DIM, fitted);
}

//##############################################################################################################
static void oil_Draw_Trend(const Oil_View_t& view){
    const Oil_Series_t* series = oil_Selected(view);
    const Oil_Point_t* last = series ? oil_Series_Last(*series) : nullptr;
    int first = 0;
    if (last != nullptr){
        const int32_t since = last->day - (series->cadence == OIL_MONTHLY ? 731 : TREND_DAYS);
        while (first < series->count && series->points[first].day < since) first++;
    }
    const int count = (last != nullptr) ? series->count - first : 0;
    if (count < 2){
        oil_Draw_Notice(view.notice ? view.notice : "Collecting price history...", PAGE_Y1, PAGE_Y2);
        return;
    }
    float low = series->points[first].value, high = low;
    for (int i = first; i < series->count; i++){
        if (series->points[i].value < low) low = series->points[i].value;
        if (series->points[i].value > high) high = series->points[i].value;
    }
    char text[48], lowText[16], highText[16];
    oil_Ui_Format_Value(lowText, sizeof(lowText), low, series->unit);
    oil_Ui_Format_Value(highText, sizeof(highText), high, series->unit);
    snprintf(text, sizeof(text), series->cadence == OIL_MONTHLY ? "2 YEARS, MONTHLY" : "3 MONTHS, DAILY");
    oil_Text(LEFT_X, 44, Terminal4x6, C_LABEL, text);
    snprintf(text, sizeof(text), "HIGH %s  LOW %s", highText, lowText);
    oil_Text_Right(RIGHT_X, 44, Terminal4x6, C_LABEL, text);

    const int16_t X1 = LEFT_X, X2 = RIGHT_X, Y1 = 53, Y2 = 98;
    const float span = (high > low) ? high - low : 1.0f;
    const float base = (high > low) ? low : low - 0.5f;
    auto yOf = [&](float value){ return (int16_t)lroundf(Y2 - (value - base) / span * (Y2 - Y1)); };
    auto xOf = [&](int i){ return (int16_t)(X1 + (int32_t)(i - first) * (X2 - X1) / (count - 1)); };
    const bool rising = last->value >= series->points[first].value;
    const uint16_t line = rising ? C_UP : C_DOWN;
    const uint16_t fill = rising ? C_UP_DIM : C_DOWN_DIM;

    oil_Dotted_HLine(Y1, X1, X2, C_LINE);
    oil_Dotted_HLine((Y1 + Y2) / 2, X1, X2, C_LINE);
    oil_Dotted_HLine(Y2, X1, X2, C_LINE);
    // Area under the line, a column at a time, then the line on top.
    for (int i = first; i + 1 < series->count; i++){
        const int16_t xa = xOf(i), xb = xOf(i + 1);
        const int16_t ya = yOf(series->points[i].value), yb = yOf(series->points[i + 1].value);
        for (int16_t x = xa; x <= xb; x++){
            const int16_t y = (xb == xa) ? ya : (int16_t)(ya + (int32_t)(yb - ya) * (x - xa) / (xb - xa));
            if (y + 1 <= Y2) mtb_Panel_Draw_VLine(x, y + 1, Y2, fill);
        }
    }
    for (int i = first; i + 1 < series->count; i++){
        mtb_Panel_Draw_Line(xOf(i), yOf(series->points[i].value), xOf(i + 1), yOf(series->points[i + 1].value), line);
    }
    mtb_Panel_Fill_Circle(xOf(series->count - 1), yOf(last->value), 2, C_TEXT);

    char date[16];
    oil_Format_Short_Date(date, sizeof(date), series->points[first].day, series->cadence == OIL_MONTHLY);
    oil_Text(LEFT_X, 101, Terminal4x6, C_DIM, date);
    oil_Format_Short_Date(date, sizeof(date), last->day, series->cadence == OIL_MONTHLY);
    oil_Text_Right(RIGHT_X, 101, Terminal4x6, C_DIM, date);
    oil_Text_Centre(128, 101, Terminal4x6, C_DIM, oil_Ui_Unit_Text(series->unit));
}

// Every daily benchmark, two columns of four.
static void oil_Draw_Board(const Oil_View_t& view){
    int shown = 0;
    for (int i = 0; i < view.data->seriesCount && shown < 8; i++){
        const Oil_Series_t& series = view.data->series[i];
        if (series.kind != OIL_KIND_PRICE) continue;
        const int16_t x = (shown / 4) * 129, y = PAGE_Y1 + (shown % 4) * 16;
        const bool selected = i == view.selected;
        const uint16_t background = selected ? C_SELECTED : BLACK;
        if (selected) mtb_Panel_Fill_Rect(x, y, x + 126, y + 14, C_SELECTED);
        oil_Text(x + 3, y + 4, Terminal6x8, selected ? C_ACCENT : C_TEXT, series.code, background);
        const Oil_Point_t* last = oil_Series_Last(series);
        char text[16];
        float change = NAN, percent = NAN;
        oil_Ui_Format_Value(text, sizeof(text), last ? last->value : NAN, series.unit);
        oil_Text_Right(x + 82, y + 4, Terminal6x8, last ? C_TEXT : C_DIM, text, background);
        if (last != nullptr) oil_Series_Change(series, 0, change, percent);
        oil_Ui_Format_Percent(text, sizeof(text), percent);
        oil_Text_Right(x + 124, y + 4, Terminal6x8, oil_Trend_Color(percent), text, background);
        shown++;
    }
    mtb_Panel_Draw_VLine(127, PAGE_Y1, PAGE_Y2, C_LINE);
}

static void oil_Draw_Stats(const Oil_View_t& view){
    const Oil_Series_t* series = oil_Selected(view);
    const Oil_Point_t* last = series ? oil_Series_Last(*series) : nullptr;
    if (last == nullptr){
        oil_Draw_Notice(view.notice ? view.notice : "No prices yet.", PAGE_Y1, PAGE_Y2);
        return;
    }
    const bool monthly = series->cadence == OIL_MONTHLY;
    static const char* const dailyLabels[] = {"1 DAY", "1 WEEK", "1 MONTH", "3 MONTHS"};
    static const char* const monthlyLabels[] = {"1 MONTH", "3 MONTHS", "6 MONTHS", "1 YEAR"};
    static const int32_t dailyBack[] = {0, 7, 30, 91};
    static const int32_t monthlyBack[] = {0, 92, 183, 365};
    char text[48];
    for (int i = 0; i < 4; i++){
        const int16_t centre = 32 + i * 64;
        oil_Text_Centre(centre, 45, Terminal4x6, C_LABEL, monthly ? monthlyLabels[i] : dailyLabels[i]);
        float change = NAN, percent = NAN;
        oil_Series_Change(*series, monthly ? monthlyBack[i] : dailyBack[i], change, percent);
        oil_Ui_Format_Percent(text, sizeof(text), percent);
        oil_Text_Centre(centre, 53, Terminal8x12, oil_Trend_Color(percent), text);
        if (!isnan(change)){
            oil_Format_Change(text, sizeof(text), change, series->unit);
            oil_Text_Centre(centre, 67, Terminal4x6, C_DIM, text);
        }
        if (i > 0) mtb_Panel_Draw_VLine(i * 64 - 1, 45, 72, C_LINE);
    }

    // Where the value sits in its range.
    float low, high;
    const int32_t rangeDays = monthly ? 731 : RANGE_DAYS;
    if (!oil_Series_Range(*series, rangeDays, low, high)) return;
    char lowText[16], highText[16];
    oil_Ui_Format_Value(lowText, sizeof(lowText), low, series->unit);
    oil_Ui_Format_Value(highText, sizeof(highText), high, series->unit);
    const int16_t barY = 88, barX1 = 46, barX2 = 209;
    const int16_t labelEnd = oil_Text(LEFT_X, 78, Terminal4x6, C_LABEL, monthly ? "2-YEAR RANGE" : "6-MONTH RANGE");
    const int32_t daysHeld = last->day - series->points[0].day;
    if (daysHeld + 7 < rangeDays){
        snprintf(text, sizeof(text), "(%d DAYS HELD)", (int)daysHeld);
        oil_Text(labelEnd + 6, 78, Terminal4x6, C_DIM, text);
    }
    oil_Text_Right(barX1 - 5, barY - 2, Terminal6x8, C_TEXT, lowText);
    oil_Text(barX2 + 5, barY - 2, Terminal6x8, C_TEXT, highText);
    mtb_Panel_Fill_Rect(barX1, barY, barX2, barY + 3, C_LINE);
    const float ratio = (high > low) ? (last->value - low) / (high - low) : 0.5f;
    const int16_t marker = barX1 + (int16_t)lroundf(ratio * (barX2 - barX1));
    mtb_Panel_Fill_Rect(barX1, barY, marker, barY + 3, C_SELECTED);
    oil_Draw_Triangle(marker - 3, barY + 5, 4, true, C_ACCENT);
    snprintf(text, sizeof(text), "%.0f%% OF THE WAY FROM LOW TO HIGH", ratio * 100);
    oil_Text_Centre(128, 100, Terminal4x6, C_DIM, text);
}

// Crude output of Nigeria and OPEC; Bonny Light's monthly price on the top row when there is one.
static void oil_Draw_Supply(const Oil_View_t& view){
    const Oil_Data_t& data = *view.data;
    char text[96], value[16], date[16];
    const int bonnyIndex = oil_Find_Series(data, "BONNY LT");
    const Oil_Point_t* bonny = bonnyIndex >= 0 ? oil_Series_Last(data.series[bonnyIndex]) : nullptr;
    if (bonny != nullptr){
        oil_Ui_Format_Value(value, sizeof(value), bonny->value, OIL_UNIT_USD_BBL);
        oil_Format_Short_Date(date, sizeof(date), bonny->day, true);
        snprintf(text, sizeof(text), "BONNY LIGHT %s $/BBL  %s, US IMPORT FOB", value, date);
        oil_Text(LEFT_X, 44, Terminal4x6, C_AMBER, text);
    } else oil_Text(LEFT_X, 44, Terminal4x6, C_LABEL, "CRUDE OIL OUTPUT, THOUSAND BARRELS A DAY (EIA ESTIMATES)");

    int panels = 0;
    for (int i = 0; i < data.seriesCount && panels < 2; i++){
        const Oil_Series_t& series = data.series[i];
        if (series.kind != OIL_KIND_SUPPLY) continue;
        const int16_t x = panels * 129 + LEFT_X;
        const int16_t right = panels * 129 + 124;
        panels++;
        oil_Text(x, 52, Terminal6x8, C_ACCENT, series.code);
        const Oil_Point_t* last = oil_Series_Last(series);
        if (last == nullptr){
            oil_Text(x, 64, Terminal6x8, C_DIM, view.notice ? "No data" : "Waiting...");
            continue;
        }
        oil_Format_Short_Date(date, sizeof(date), last->day, true);
        oil_Text_Right(right, 53, Terminal4x6, C_LABEL, date);
        oil_Ui_Format_Value(value, sizeof(value), last->value, OIL_UNIT_KBD);
        const int16_t after = oil_Grouped_Text(x, 62, Terminal8x12, C_TEXT, value);
        oil_Text(after + 3, 67, Terminal4x6, C_LABEL, "kb/d");
        float change = NAN, percent = NAN;
        if (oil_Series_Change(series, 0, change, percent)){
            oil_Ui_Format_Percent(text, sizeof(text), percent);
            const int16_t labelX = oil_Text_Right(right, 66, Terminal4x6, C_DIM, "M/M");
            oil_Text_Right(labelX - 4, 64, Terminal6x8, oil_Trend_Color(percent), text);
        }

        // The last twelve months as bars, scaled to their own range so month-to-month moves show.
        const int bars = series.count < 12 ? series.count : 12;
        const int start = series.count - bars;
        float low = series.points[start].value, high = low;
        for (int b = start; b < series.count; b++){
            if (series.points[b].value < low) low = series.points[b].value;
            if (series.points[b].value > high) high = series.points[b].value;
        }
        const float floor = low - (high - low) * 0.25f;
        const int16_t barsY1 = 79, barsY2 = 105, barWidth = 8;
        for (int b = 0; b < bars; b++){
            const float v = series.points[start + b].value;
            const float ratio = (high > floor) ? (v - floor) / (high - floor) : 1.0f;
            const int16_t top = barsY2 - (int16_t)lroundf(ratio * (barsY2 - barsY1));
            const int16_t bx = x + (12 - bars + b) * 10;
            mtb_Panel_Fill_Rect(bx, top, bx + barWidth - 1, barsY2, b == bars - 1 ? C_ACCENT : C_SELECTED);
        }
    }
    mtb_Panel_Draw_VLine(127, 52, PAGE_Y2, C_LINE);
}

// Two headlines, each on up to three lines, the date first.
static void oil_Draw_News(const Oil_View_t& view){
    const Oil_Data_t& data = *view.data;
    if (data.headlineCount == 0){
        oil_Draw_Notice(view.notice ? view.notice : "No headlines yet.", PAGE_Y1, PAGE_Y2);
        return;
    }
    char text[48];
    oil_Text(LEFT_X, 44, Terminal4x6, C_AMBER, data.newsSource);
    const int first = view.newsFirst % data.headlineCount;
    const int count = data.headlineCount > 1 ? 2 : 1;
    if (count == 1) snprintf(text, sizeof(text), "1 OF 1");
    else snprintf(text, sizeof(text), "%d+%d OF %d", first + 1, (first + 1) % data.headlineCount + 1, data.headlineCount);
    oil_Text_Right(RIGHT_X, 44, Terminal4x6, C_DIM, text);

    for (int k = 0; k < count; k++){
        const Oil_Headline_t& headline = data.headlines[(first + k) % data.headlineCount];
        int16_t y = 52 + k * 28;
        if (k > 0) oil_Dotted_HLine(y - 3, LEFT_X, RIGHT_X, C_LINE);
        char date[16] = "";
        if (headline.day != 0) oil_Format_Short_Date(date, sizeof(date), headline.day, false);
        int16_t x = oil_Text(LEFT_X, y, Terminal6x8, headline.score >= 2 ? C_ACCENT : C_LABEL, date);
        if (date[0]) x += 6;

        // Word wrap into the space left on the first line and two full lines.
        const char* rest = headline.title;
        for (int line = 0; line < 3 && *rest; line++, y += 9){
            const int16_t room = RIGHT_X - x;
            char piece[64];
            size_t take = 0, lastSpace = 0;
            while (rest[take] && take + 1 < sizeof(piece)){
                memcpy(piece, rest, take + 1);
                piece[take + 1] = '\0';
                if (oil_Text_Width(Terminal6x8, piece) > room) break;
                if (rest[take] == ' ') lastSpace = take;
                take++;
            }
            const bool more = rest[take] != '\0';
            if (more && lastSpace > 0) take = lastSpace;
            memcpy(piece, rest, take);
            piece[take] = '\0';
            if (line == 2 && more){     // Cut: end on "..." within the line.
                while (take > 0 && oil_Text_Width(Terminal6x8, piece) + oil_Text_Width(Terminal6x8, "...") > room) piece[--take] = '\0';
                strcat(piece, "...");
            }
            oil_Text(x, y, Terminal6x8, C_TEXT, piece);
            rest += (line == 2 && more) ? strlen(rest) : take;
            while (*rest == ' ') rest++;
            x = LEFT_X;
        }
    }
}

void oil_Ui_Draw_Page(const Oil_View_t& view){
    mtb_Panel_Fill_Rect(0, PAGE_Y1, PANEL_RES_X - 1, PAGE_Y2, BLACK);
    if (view.data == nullptr) return;
    switch (view.page){
        case OIL_PAGE_TREND:  oil_Draw_Trend(view); break;
        case OIL_PAGE_BOARD:  oil_Draw_Board(view); break;
        case OIL_PAGE_STATS:  oil_Draw_Stats(view); break;
        case OIL_PAGE_SUPPLY: oil_Draw_Supply(view); break;
        default:              oil_Draw_News(view); break;
    }
}

// The daily benchmarks as chips, starting one before the selected one so it stays second from the left.
void oil_Ui_Draw_Strip(const Oil_View_t& view){
    mtb_Panel_Fill_Rect(0, STRIP_Y1, PANEL_RES_X - 1, STRIP_Y2, BLACK);
    if (view.data == nullptr) return;
    int prices[OIL_MAX_SERIES];
    int count = 0, selectedAt = 0;
    for (int i = 0; i < view.data->seriesCount; i++){
        if (view.data->series[i].kind != OIL_KIND_PRICE) continue;
        if (i == view.selected) selectedAt = count;
        prices[count++] = i;
    }
    if (count == 0) return;
    int16_t x = 1;
    for (int k = 0; k < count; k++){
        const int index = prices[(selectedAt - 1 + count + k) % count];
        const Oil_Series_t& series = view.data->series[index];
        const int16_t width = oil_Text_Width(Terminal6x8, series.code) + 15;
        if (x + width > PANEL_RES_X - 1) break;
        const bool selected = index == view.selected;
        const uint16_t background = selected ? C_SELECTED : BLACK;
        if (selected) mtb_Panel_Fill_Round_Rect(x, STRIP_Y1, x + width - 1, STRIP_Y2, 2, C_SELECTED);
        const int16_t after = oil_Text(x + 3, STRIP_Y1 + 1, Terminal6x8, selected ? C_ACCENT : C_LABEL, series.code, background);
        float change = NAN, percent = NAN;
        if (oil_Series_Change(series, 0, change, percent) && change != 0) oil_Draw_Triangle(after + 3, STRIP_Y1 + 2, 4, change > 0, oil_Trend_Color(change));
        else mtb_Panel_Fill_Rect(after + 3, STRIP_Y1 + 4, after + 8, STRIP_Y1 + 4, C_DIM);
        x += width + 3;
    }
}
