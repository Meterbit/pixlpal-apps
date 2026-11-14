#include "opa_Ui.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mtb_graphics.h"

#define OPA_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_UP       = OPA_RGB(40, 225, 100);
static const uint16_t C_UP_DIM   = OPA_RGB(0, 56, 26);
static const uint16_t C_DOWN     = OPA_RGB(255, 64, 48);
static const uint16_t C_DOWN_DIM = OPA_RGB(72, 14, 8);
static const uint16_t C_AMBER    = OPA_RGB(255, 176, 0);
static const uint16_t C_ACCENT   = OPA_RGB(255, 150, 20);
static const uint16_t C_TEXT     = OPA_RGB(235, 235, 235);
static const uint16_t C_LABEL    = OPA_RGB(130, 130, 130);
static const uint16_t C_DIM      = OPA_RGB(80, 80, 80);
static const uint16_t C_LINE     = OPA_RGB(48, 48, 48);
static const uint16_t C_SELECTED = OPA_RGB(84, 48, 8);
static const uint16_t C_NEUTRAL  = OPA_RGB(150, 150, 150);
static const uint16_t C_LOCK     = OPA_RGB(110, 70, 160);

// Layout. Row and column numbers are inclusive.
static const int16_t LEFT_X = 3;
static const int16_t RIGHT_X = 253;     // mtb_Write_String() drops a glyph whose advance reaches the panel edge.
static const int16_t HERO_Y1 = 10, HERO_Y2 = 40, HERO_RULE_Y = 41;
static const int16_t PAGE_Y1 = 43, PAGE_Y2 = 106;
static const int16_t STRIP_RULE_Y = 108, STRIP_Y1 = 109, STRIP_Y2 = 117, TICKER_RULE_Y = 118;

//##############################################################################################################
// Pixel width of a string, using the same rules as Mtb_Static_Text_t::mtb_Write_String().
static int16_t opa_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

// The widget paints its background behind the whole string, so text on a coloured box passes that colour.
static int16_t opa_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK){
    if (text[0] == '\0') return x;
    Mtb_FixedText_t widget(x < 0 ? 0 : x, y, font, color, background);
    widget.mtb_Write_String(text);
    return x + opa_Text_Width(font, text);
}

static int16_t opa_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK){
    const int16_t x = rightX - opa_Text_Width(font, text) + 1;
    opa_Text(x, y, font, color, text, background);
    return x;
}

static void opa_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background = BLACK){
    opa_Text(centreX - opa_Text_Width(font, text) / 2, y, font, color, text, background);
}

// Copies text into dest, cut to fit maxWidth pixels.
static void opa_Fit_Text(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth){
    snprintf(dest, size, "%s", text);
    for (size_t length = strlen(dest); length > 0 && opa_Text_Width(font, dest) > maxWidth; length--) dest[length - 1] = '\0';
    size_t length = strlen(dest);
    while (length > 0 && dest[length - 1] == ' ') dest[--length] = '\0';
}

static void opa_Upper(char* text){ for (; *text; text++) *text = (char)toupper((unsigned char)*text); }

static uint16_t opa_Trend_Color(float value){ return isnan(value) || value == 0 ? C_NEUTRAL : value < 0 ? C_DOWN : C_UP; }

static void opa_Draw_Triangle(int16_t x, int16_t y, int16_t size, bool up, uint16_t color){
    if (up) mtb_Panel_Fill_Triangle(x, y + size - 1, x + 2 * (size - 1), y + size - 1, x + size - 1, y, color);
    else mtb_Panel_Fill_Triangle(x, y, x + 2 * (size - 1), y, x + size - 1, y + size - 1, color);
}

static void opa_Dotted_HLine(int16_t y, int16_t x1, int16_t x2, uint16_t color){
    for (int16_t x = x1; x <= x2; x += 3) mtb_Panel_Draw_PixelRGB565(x, y, color);
}

// A 5x6 padlock, top-left at (x, y).
static void opa_Draw_Lock(int16_t x, int16_t y, uint16_t color){
    mtb_Panel_Draw_HLine(y, x + 1, x + 3, color);
    mtb_Panel_Draw_VLine(x, y + 1, y + 2, color);
    mtb_Panel_Draw_VLine(x + 4, y + 1, y + 2, color);
    mtb_Panel_Fill_Rect(x, y + 3, x + 4, y + 5, color);
}

// A 5x5 tick, top-left at (x, y).
static void opa_Draw_Tick(int16_t x, int16_t y, uint16_t color){
    mtb_Panel_Draw_Line(x, y + 2, x + 1, y + 3, color);
    mtb_Panel_Draw_Line(x + 1, y + 3, x + 4, y, color);
    mtb_Panel_Draw_Line(x, y + 3, x + 1, y + 4, color);
    mtb_Panel_Draw_Line(x + 1, y + 4, x + 4, y + 1, color);
}

static void opa_Draw_Notice(const char* text, int16_t y1, int16_t y2){
    // Up to four lines, broken at spaces.
    char line[48];
    const uint8_t* font = Terminal6x8;
    const int16_t maxWidth = RIGHT_X - LEFT_X;
    const char* rest = text;
    int16_t y = (y1 + y2) / 2 - 18;
    for (int lineNo = 0; lineNo < 4 && *rest; lineNo++, y += 10){
        size_t take = 0, lastSpace = 0;
        while (rest[take] && take + 1 < sizeof(line)){
            memcpy(line, rest, take + 1);
            line[take + 1] = '\0';
            if (opa_Text_Width(font, line) > maxWidth) break;
            if (rest[take] == ' ') lastSpace = take;
            take++;
        }
        if (rest[take] && lastSpace > 0) take = lastSpace;
        memcpy(line, rest, take);
        line[take] = '\0';
        opa_Text_Centre(128, y, font, C_LABEL, line);
        rest += take;
        while (*rest == ' ') rest++;
    }
}

// "14:32" for today, "05 NOV" otherwise, in local time.
static void opa_Format_When(char* dest, size_t size, uint32_t when, uint32_t now, bool forceDate = false){
    if (when == 0){ snprintf(dest, size, "--"); return; }
    const time_t whenTime = (time_t)when, nowTime = (time_t)now;
    struct tm local, today;
    localtime_r(&whenTime, &local);
    localtime_r(&nowTime, &today);
    const bool sameDay = local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
    strftime(dest, size, (sameDay && !forceDate) ? "%H:%M" : "%d %b", &local);
    opa_Upper(dest);
}

static const Opa_Quote_t* opa_Selected(const Opa_View_t& view){
    if (view.data == nullptr || view.selected < 0 || view.selected >= view.data->quoteCount) return nullptr;
    return &view.data->quotes[view.selected];
}

void opa_Ui_Plan_Name(char* dest, size_t size, const Opa_Account_t& account, bool demo){
    if (demo || account.window == OPA_QUOTA_DEMO){ snprintf(dest, size, "DEMO"); return; }
    if (account.seen == 0){ snprintf(dest, size, "CHECKING"); return; }
    if (account.window == OPA_QUOTA_TRIAL){ snprintf(dest, size, "TRIAL"); return; }
    if (account.tier[0] != '\0'){
        // "starter_v2" -> "STARTER", "enterprise_v2" is sold as Scale.
        snprintf(dest, size, "%s", account.tier);
        char* suffix = strstr(dest, "_v");
        if (suffix) *suffix = '\0';
        for (char* c = dest; *c; c++) *c = (*c == '_') ? ' ' : (char)toupper((unsigned char)*c);
        if (strcmp(dest, "ENTERPRISE") == 0) snprintf(dest, size, "SCALE");
        if (strcmp(dest, "PAID ACTIVE") == 0) snprintf(dest, size, "PAID");
        return;
    }
    snprintf(dest, size, account.window == OPA_QUOTA_MONTH ? "PAID" : "FREE");
}

//##############################################################################################################
void opa_Ui_Clear(void){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    mtb_Panel_Draw_HLine(HERO_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(STRIP_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(TICKER_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
}

void opa_Ui_Draw_Hero(const Opa_View_t& view){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, HERO_Y2, BLACK);
    const Opa_Quote_t* quote = opa_Selected(view);
    if (quote == nullptr) return;
    const Opa_Commodity_t* known = opa_Find_Commodity(quote->code);
    char text[64], fitted[64], unit[16];

    opa_Text(LEFT_X, 11, Terminal8x12, C_ACCENT, opa_Label(quote->code));

    // Change over 24 hours, top right: amount and per cent.
    float percent;
    if (opa_Quote_Change(*view.data, *quote, OPA_W_24H, percent)){
        const uint16_t color = opa_Trend_Color(percent);
        const float before = quote->price / (1 + percent / 100);
        char amount[16], percentText[16];
        opa_Format_Change(amount, sizeof(amount), quote->price - before, quote->price);
        opa_Format_Percent(percentText, sizeof(percentText), percent);
        snprintf(text, sizeof(text), "%s %s", amount, percentText);
        const int16_t x = opa_Text_Right(RIGHT_X, 11, Terminal8x12, color, text);
        if (percent != 0) opa_Draw_Triangle(x - 13, 13, 6, percent > 0, color);
        opa_Text_Right(x - 17, 14, Terminal4x6, C_DIM, "24H");
    }

    // The price, large, with its unit.
    int16_t x;
    if (!isnan(quote->price)){
        opa_Format_Price(text, sizeof(text), quote->price);
        x = opa_Text(LEFT_X, 24, Terminal10x16, quote->state == OPA_Q_STALE ? C_AMBER : C_TEXT, text);
    } else x = opa_Text(LEFT_X, 24, Terminal10x16, C_DIM, "--");
    opa_Unit_Text(unit, sizeof(unit), *quote);
    x = opa_Text(x + 4, 32, Terminal6x8, C_LABEL, unit);

    // What the number is, and where and when it is from: always on screen.
    const int16_t columnX = x + 8;
    opa_Fit_Text(fitted, sizeof(fitted), Terminal6x8, known ? known->name : quote->code, RIGHT_X - columnX);
    opa_Text_Right(RIGHT_X, 24, Terminal6x8, C_LABEL, fitted);
    char when[16];
    opa_Format_When(when, sizeof(when), quote->observed, view.now);
    const char* source = view.data->demo ? "OILPRICEAPI DEMO" : "OILPRICEAPI";
    if (quote->state == OPA_Q_MISSING) snprintf(text, sizeof(text), "%s  NOT ON THIS PLAN", source);
    else if (quote->state == OPA_Q_STALE) snprintf(text, sizeof(text), "%s  STALE %s", source, when);
    else snprintf(text, sizeof(text), "%s  %s", source, when);
    opa_Fit_Text(fitted, sizeof(fitted), Terminal6x8, text, RIGHT_X - columnX);
    const bool amber = view.stale || quote->state == OPA_Q_STALE || quote->state == OPA_Q_MISSING;
    opa_Text_Right(RIGHT_X, 33, Terminal6x8, amber ? C_AMBER : C_DIM, fitted);
}

//##############################################################################################################
// The points to chart: the history from the API if it is of this code, else what the device recorded itself.
static bool opa_Chart_Points(const Opa_View_t& view, const Opa_Quote_t& quote, const Opa_Point_t*& points, int& count, bool& fromApi){
    const Opa_Chart_t& chart = view.data->chart;
    fromApi = strcasecmp(chart.code, quote.code) == 0 && chart.range == view.range && chart.count >= 2;
    if (fromApi){ points = chart.points; count = chart.count; }
    else { points = quote.local; count = quote.localCount; }
    return count >= 2;
}

static const char* const RANGE_TEXT[OPA_RANGE_COUNT] = {"24 HOURS, HOURLY", "7 DAYS, HOURLY", "30 DAYS, DAILY", "1 YEAR, WEEKLY"};

static void opa_Draw_Chart(const Opa_View_t& view){
    const Opa_Quote_t* quote = opa_Selected(view);
    const Opa_Point_t* points = nullptr;
    int count = 0;
    bool fromApi = false;
    if (quote == nullptr || !opa_Chart_Points(view, *quote, points, count, fromApi)){
        const bool locked = view.data->access[OPA_F_HISTORY].state == OPA_ACCESS_LOCKED;
        opa_Draw_Notice(view.notice ? view.notice
                        : locked ? "Price history is on paid plans. Pixlpal draws the chart from the prices it receives: it fills in as they come."
                                 : "Collecting price history...", PAGE_Y1, PAGE_Y2);
        return;
    }
    float low = points[0].v, high = low;
    for (int i = 0; i < count; i++){
        if (points[i].v < low) low = points[i].v;
        if (points[i].v > high) high = points[i].v;
    }
    char text[64], lowText[16], highText[16];
    opa_Format_Price(lowText, sizeof(lowText), low);
    opa_Format_Price(highText, sizeof(highText), high);
    opa_Text(LEFT_X, 44, Terminal4x6, fromApi ? C_LABEL : C_LOCK, fromApi ? RANGE_TEXT[view.range] : "RECORDED BY THIS PIXLPAL");
    snprintf(text, sizeof(text), "HIGH %s  LOW %s", highText, lowText);
    opa_Text_Right(RIGHT_X, 44, Terminal4x6, C_LABEL, text);

    const int16_t X1 = LEFT_X, X2 = RIGHT_X, Y1 = 53, Y2 = 98;
    const uint32_t t0 = points[0].t, t1 = points[count - 1].t;
    const float span = (high > low) ? high - low : 1.0f;
    const float base = (high > low) ? low : low - 0.5f;
    auto yOf = [&](float value){ return (int16_t)lroundf(Y2 - (value - base) / span * (Y2 - Y1)); };
    // By time, so gaps (weekends, a device switched off) show as gaps.
    auto xOf = [&](uint32_t t){ return (int16_t)(X1 + (t1 > t0 ? (int64_t)(t - t0) * (X2 - X1) / (t1 - t0) : 0)); };
    const bool rising = points[count - 1].v >= points[0].v;
    const uint16_t line = rising ? C_UP : C_DOWN;
    const uint16_t fill = rising ? C_UP_DIM : C_DOWN_DIM;

    opa_Dotted_HLine(Y1, X1, X2, C_LINE);
    opa_Dotted_HLine((Y1 + Y2) / 2, X1, X2, C_LINE);
    opa_Dotted_HLine(Y2, X1, X2, C_LINE);
    for (int i = 0; i + 1 < count; i++){
        const int16_t xa = xOf(points[i].t), xb = xOf(points[i + 1].t);
        const int16_t ya = yOf(points[i].v), yb = yOf(points[i + 1].v);
        for (int16_t x = xa; x <= xb; x++){
            const int16_t y = (xb == xa) ? ya : (int16_t)(ya + (int32_t)(yb - ya) * (x - xa) / (xb - xa));
            if (y + 1 <= Y2) mtb_Panel_Draw_VLine(x, y + 1, Y2, fill);
        }
    }
    for (int i = 0; i + 1 < count; i++) mtb_Panel_Draw_Line(xOf(points[i].t), yOf(points[i].v), xOf(points[i + 1].t), yOf(points[i + 1].v), line);
    mtb_Panel_Fill_Circle(xOf(t1), yOf(points[count - 1].v), 2, C_TEXT);

    char when[16];
    const bool dates = t1 - t0 > 36UL * 3600;
    opa_Format_When(when, sizeof(when), t0, view.now, dates);
    opa_Text(LEFT_X, 101, Terminal4x6, C_DIM, when);
    opa_Format_When(when, sizeof(when), t1, view.now, dates);
    opa_Text_Right(RIGHT_X, 101, Terminal4x6, C_DIM, when);
    if (!fromApi && view.data->access[OPA_F_HISTORY].state == OPA_ACCESS_LOCKED) opa_Text_Centre(128, 101, Terminal4x6, C_LOCK, "FULL HISTORY: DEVELOPER PLAN");
    else {
        char unit[16];
        opa_Unit_Text(unit, sizeof(unit), *quote);
        opa_Text_Centre(128, 101, Terminal4x6, C_DIM, unit);
    }
}

// The plan each change window first comes with.
static const char* const WINDOW_LABEL[OPA_W_COUNT] = {"24 HOURS", "7 DAYS", "30 DAYS", "90 DAYS"};
static const char* const WINDOW_PLAN[OPA_W_COUNT] = {"FREE", "STARTER", "PROFESSIONAL", "SCALE"};

static void opa_Draw_Changes(const Opa_View_t& view){
    const Opa_Quote_t* quote = opa_Selected(view);
    if (quote == nullptr || isnan(quote->price)){
        opa_Draw_Notice(view.notice ? view.notice : "No prices yet.", PAGE_Y1, PAGE_Y2);
        return;
    }
    char text[48];
    for (int w = 0; w < OPA_W_COUNT; w++){
        const int16_t centre = 32 + w * 64;
        opa_Text_Centre(centre, 45, Terminal4x6, C_LABEL, WINDOW_LABEL[w]);
        float percent;
        if (opa_Quote_Change(*view.data, *quote, (uint8_t)w, percent)){
            opa_Format_Percent(text, sizeof(text), percent);
            opa_Text_Centre(centre, 53, Terminal8x12, opa_Trend_Color(percent), text);
            const float before = quote->price / (1 + percent / 100);
            opa_Format_Change(text, sizeof(text), quote->price - before, quote->price);
            opa_Text_Centre(centre, 67, Terminal4x6, C_DIM, text);
        } else if (view.data->demo || view.data->account.seen == 0){
            opa_Text_Centre(centre, 55, Terminal6x8, C_DIM, "--");
        } else {
            // Not reported on this plan: say which plan has it.
            opa_Draw_Lock(centre - 3, 54, C_LOCK);
            opa_Text_Centre(centre, 64, Terminal4x6, C_LOCK, WINDOW_PLAN[w]);
        }
        if (w > 0) mtb_Panel_Draw_VLine(w * 64 - 1, 45, 72, C_LINE);
    }

    // Where the price sits in the chart's range.
    const Opa_Point_t* points;
    int count;
    bool fromApi;
    if (!opa_Chart_Points(view, *quote, points, count, fromApi)) return;
    float low = points[0].v, high = low;
    for (int i = 0; i < count; i++){
        if (points[i].v < low) low = points[i].v;
        if (points[i].v > high) high = points[i].v;
    }
    if (quote->price < low) low = quote->price;
    if (quote->price > high) high = quote->price;
    char lowText[16], highText[16];
    opa_Format_Price(lowText, sizeof(lowText), low);
    opa_Format_Price(highText, sizeof(highText), high);
    const int16_t barY = 88, barX1 = 52, barX2 = 203;
    opa_Text(LEFT_X, 78, Terminal4x6, C_LABEL, fromApi ? "RANGE OVER THE CHART" : "RANGE RECORDED BY THIS PIXLPAL");
    opa_Text_Right(barX1 - 5, barY - 2, Terminal6x8, C_TEXT, lowText);
    opa_Text(barX2 + 5, barY - 2, Terminal6x8, C_TEXT, highText);
    mtb_Panel_Fill_Rect(barX1, barY, barX2, barY + 3, C_LINE);
    const float ratio = (high > low) ? (quote->price - low) / (high - low) : 0.5f;
    const int16_t marker = barX1 + (int16_t)lroundf(ratio * (barX2 - barX1));
    mtb_Panel_Fill_Rect(barX1, barY, marker, barY + 3, C_SELECTED);
    opa_Draw_Triangle(marker - 3, barY + 5, 4, true, C_ACCENT);
    snprintf(text, sizeof(text), "%.0f%% OF THE WAY FROM LOW TO HIGH", ratio * 100);
    opa_Text_Centre(128, 100, Terminal4x6, C_DIM, text);
}

// "+1.3%" or "+12%": short enough for the board.
static void opa_Format_Short_Percent(char* dest, size_t size, float percent){
    if (isnan(percent)) snprintf(dest, size, "--");
    else snprintf(dest, size, fabsf(percent) < 10 ? "%+.1f%%" : "%+.0f%%", percent);
}

// The watchlist, two columns of six.
static void opa_Draw_Board(const Opa_View_t& view){
    const Opa_Data_t& data = *view.data;
    for (int i = 0; i < data.quoteCount && i < OPA_MAX_WATCH; i++){
        const Opa_Quote_t& quote = data.quotes[i];
        const int16_t x = (i / 6) * 129, y = PAGE_Y1 + 1 + (i % 6) * 10;
        const bool selected = i == view.selected;
        const uint16_t background = selected ? C_SELECTED : BLACK;
        if (selected) mtb_Panel_Fill_Rect(x, y - 1, x + 126, y + 8, C_SELECTED);
        opa_Text(x + 3, y, Terminal6x8, selected ? C_ACCENT : C_TEXT, opa_Label(quote.code), background);
        char text[16];
        if (quote.state == OPA_Q_MISSING && isnan(quote.price)){
            opa_Draw_Lock(x + 120, y + 1, C_LOCK);
            opa_Text_Right(x + 115, y, Terminal6x8, C_LOCK, "PLAN", background);
            continue;
        }
        opa_Format_Price(text, sizeof(text), quote.price);
        const uint16_t priceColor = isnan(quote.price) ? C_DIM : (quote.state == OPA_Q_CURRENT ? C_TEXT : C_AMBER);
        opa_Text_Right(x + 88, y, Terminal6x8, priceColor, text, background);
        float percent = NAN;
        opa_Quote_Change(data, quote, OPA_W_24H, percent);
        opa_Format_Short_Percent(text, sizeof(text), percent);
        opa_Text_Right(x + 124, y, Terminal6x8, opa_Trend_Color(percent), text, background);
    }
    mtb_Panel_Draw_VLine(127, PAGE_Y1, PAGE_Y2, C_LINE);
}

// The watchlist as tiles coloured by the day's move: deeper colour, bigger move (full at 3%).
static void opa_Draw_Heat(const Opa_View_t& view){
    const Opa_Data_t& data = *view.data;
    const int count = data.quoteCount;
    if (count == 0) return;
    const int columns = count <= 2 ? count : count <= 6 ? 3 : 4;
    const int rows = (count + columns - 1) / columns;
    const int16_t tileW = PANEL_RES_X / columns, tileH = (PAGE_Y2 - PAGE_Y1 + 1) / rows;
    for (int i = 0; i < count; i++){
        const Opa_Quote_t& quote = data.quotes[i];
        const int16_t x = (i % columns) * tileW, y = PAGE_Y1 + (i / columns) * tileH;
        float percent = NAN;
        opa_Quote_Change(data, quote, OPA_W_24H, percent);
        uint16_t tile = OPA_RGB(28, 28, 28);
        if (!isnan(percent) && percent != 0){
            const float strength = fminf(fabsf(percent) / 3.0f, 1.0f);
            const int level = 40 + (int)(strength * 140);
            tile = percent > 0 ? OPA_RGB(0, level, level / 3) : OPA_RGB(level, level / 6, 0);
        }
        mtb_Panel_Fill_Rect(x + 1, y + 1, x + tileW - 2, y + tileH - 2, tile);
        const uint16_t textColor = (i == view.selected) ? C_AMBER : C_TEXT;
        const int16_t centre = x + tileW / 2;
        char text[16];
        if (quote.state == OPA_Q_MISSING && isnan(quote.price)) snprintf(text, sizeof(text), "PLAN");
        else opa_Format_Short_Percent(text, sizeof(text), percent);
        if (tileH >= 20){
            const int16_t top = y + (tileH - 18) / 2;
            opa_Text_Centre(centre, top, Terminal6x8, textColor, opa_Label(quote.code), tile);
            opa_Text_Centre(centre, top + 10, Terminal6x8, textColor, text, tile);
        } else {
            char line[32];
            snprintf(line, sizeof(line), "%s %s", opa_Label(quote.code), text);
            opa_Text_Centre(centre, y + (tileH - 6) / 2, Terminal4x6, textColor, line, tile);
        }
    }
}

// ICE Brent futures by delivery month: the shape of the curve says whether the market wants oil now (backwardation)
// or later (contango).
static void opa_Draw_Curve(const Opa_View_t& view){
    const Opa_Curve_t& curve = view.data->curve;
    if (curve.count < 2){
        opa_Draw_Notice("No futures curve yet.", PAGE_Y1, PAGE_Y2);
        return;
    }
    float low = curve.price[0], high = low;
    for (int i = 0; i < curve.count; i++){
        if (curve.price[i] < low) low = curve.price[i];
        if (curve.price[i] > high) high = curve.price[i];
    }
    const int last = curve.count - 1;
    const float spread = curve.price[0] - curve.price[last];
    char text[64], a[16];
    opa_Text(LEFT_X, 44, Terminal4x6, C_LABEL, "ICE BRENT FUTURES CURVE");
    opa_Format_Change(a, sizeof(a), spread, curve.price[0]);
    snprintf(text, sizeof(text), "%s  M1-M%d %s", spread > 0 ? "BACKWARDATION" : spread < 0 ? "CONTANGO" : "FLAT", curve.count, a);
    opa_Text_Right(RIGHT_X, 44, Terminal4x6, spread > 0 ? C_UP : spread < 0 ? C_DOWN : C_NEUTRAL, text);

    const int16_t Y1 = 56, Y2 = 92;
    const float span = high > low ? high - low : 1.0f;
    const int16_t slot = (RIGHT_X - LEFT_X) / curve.count;
    auto xOf = [&](int i){ return (int16_t)(LEFT_X + slot / 2 + i * slot); };
    auto yOf = [&](float v){ return (int16_t)lroundf(Y2 - (v - low) / span * (Y2 - Y1)); };
    opa_Dotted_HLine(Y1, LEFT_X, RIGHT_X, C_LINE);
    opa_Dotted_HLine(Y2, LEFT_X, RIGHT_X, C_LINE);
    for (int i = 0; i + 1 < curve.count; i++) mtb_Panel_Draw_Line(xOf(i), yOf(curve.price[i]), xOf(i + 1), yOf(curve.price[i + 1]), C_ACCENT);
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    for (int i = 0; i < curve.count; i++){
        mtb_Panel_Fill_Circle(xOf(i), yOf(curve.price[i]), i == 0 ? 2 : 1, i == 0 ? C_TEXT : C_ACCENT);
        const int month = atoi(curve.month[i] + 5);
        if (month >= 1 && month <= 12 && (curve.count <= 8 || i % 2 == 0)) opa_Text_Centre(xOf(i), 95, Terminal4x6, C_DIM, MONTHS[month - 1]);
    }
    opa_Format_Price(a, sizeof(a), curve.price[0]);
    snprintf(text, sizeof(text), "FRONT %s", a);
    opa_Text(LEFT_X, 101, Terminal4x6, C_TEXT, text);
    opa_Format_Price(a, sizeof(a), curve.price[last]);
    snprintf(text, sizeof(text), "%s %s", curve.month[last], a);
    opa_Text_Right(RIGHT_X, 101, Terminal4x6, C_LABEL, text);
}

// Baker Hughes US rig count on the left, OPEC crude output (with Nigeria) on the right.
static void opa_Draw_Energy(const Opa_View_t& view){
    const Opa_Rigs_t& rigs = view.data->rigs;
    const Opa_Opec_t& opec = view.data->opec;
    char text[64];
    if (rigs.fetched != 0){
        opa_Text(LEFT_X, 44, Terminal4x6, C_LABEL, "US RIG COUNT (BAKER HUGHES)");
        snprintf(text, sizeof(text), "%d", rigs.total);
        const int16_t after = opa_Text(LEFT_X, 52, Terminal8x12, C_TEXT, text);
        snprintf(text, sizeof(text), "%+d W/W  %+d Y/Y", rigs.weekChange, rigs.yearChange);
        opa_Text(after + 5, 56, Terminal4x6, opa_Trend_Color(rigs.weekChange), text);
        for (int b = 0; b < OPA_BASINS && rigs.basin[b][0]; b++){
            const int16_t y = 68 + b * 8;
            opa_Text(LEFT_X, y, Terminal4x6, C_LABEL, rigs.basin[b]);
            snprintf(text, sizeof(text), "%d", rigs.basinCount[b]);
            opa_Text_Right(96, y, Terminal4x6, C_TEXT, text);
            snprintf(text, sizeof(text), "%+d", rigs.basinChange[b]);
            opa_Text_Right(122, y, Terminal4x6, opa_Trend_Color(rigs.basinChange[b]), text);
        }
        opa_Text(LEFT_X, 101, Terminal4x6, C_DIM, rigs.date);
    } else opa_Text(LEFT_X, 70, Terminal6x8, C_DIM, "Rigs: waiting");
    mtb_Panel_Draw_VLine(127, PAGE_Y1, PAGE_Y2, C_LINE);

    const int16_t x = 132;
    if (opec.fetched != 0){
        snprintf(text, sizeof(text), "OPEC CRUDE OUTPUT  %s", opec.month);
        opa_Text(x, 44, Terminal4x6, C_LABEL, text);
        snprintf(text, sizeof(text), "%.2f", opec.total);
        const int16_t after = opa_Text(x, 52, Terminal8x12, C_TEXT, text);
        opa_Text(after + 3, 57, Terminal4x6, C_LABEL, "MB/D");
        if (!isnan(opec.monthChange)){
            snprintf(text, sizeof(text), "%+.2f M/M", opec.monthChange);
            opa_Text_Right(RIGHT_X, 56, Terminal4x6, opa_Trend_Color(opec.monthChange), text);
        }
        if (!isnan(opec.nigeria)){
            opa_Text(x, 70, Terminal6x8, C_ACCENT, "NIGERIA");
            snprintf(text, sizeof(text), "%.2f MB/D", opec.nigeria);
            opa_Text_Right(RIGHT_X, 70, Terminal6x8, C_TEXT, text);
            if (!isnan(opec.nigeriaQuota) && opec.nigeriaQuota > 0){
                snprintf(text, sizeof(text), "QUOTA %.2f  %.0f%% OF QUOTA", opec.nigeriaQuota, opec.nigeria / opec.nigeriaQuota * 100);
                opa_Text(x, 80, Terminal4x6, C_LABEL, text);
            }
        }
        char headline[48];
        opa_Fit_Text(headline, sizeof(headline), Terminal4x6, opec.headline, RIGHT_X - x);
        opa_Upper(headline);
        opa_Text(x, 92, Terminal4x6, C_DIM, headline);
        opa_Text(x, 101, Terminal4x6, C_DIM, "OPEC MONTHLY REPORT");
    } else opa_Text(x, 70, Terminal6x8, C_DIM, "OPEC: waiting");
}

// What the plan gives, what it uses, and what an upgrade would add.
static void opa_Draw_Plan(const Opa_View_t& view){
    const Opa_Data_t& data = *view.data;
    const Opa_Account_t& account = data.account;
    const bool demo = view.status && view.status->demo;
    char text[80], plan[24];
    opa_Ui_Plan_Name(plan, sizeof(plan), account, demo);
    snprintf(text, sizeof(text), "OILPRICEAPI PLAN: %s", plan);
    opa_Text(LEFT_X, 44, Terminal6x8, C_ACCENT, text);
    if (account.seen != 0 && account.reset != 0){
        char when[16];
        opa_Format_When(when, sizeof(when), account.reset, view.now);
        snprintf(text, sizeof(text), "RESETS %s", when);
        opa_Text_Right(RIGHT_X, 45, Terminal4x6, C_LABEL, text);
    }

    // Quota bar.
    const int16_t barX1 = LEFT_X, barX2 = 150, barY = 55;
    mtb_Panel_Fill_Rect(barX1, barY, barX2, barY + 4, C_LINE);
    if (account.seen != 0 && account.limit > 0){
        const uint32_t used = account.limit > account.remaining ? account.limit - account.remaining : 0;
        const int16_t fill = barX1 + (int16_t)((int64_t)(barX2 - barX1) * used / account.limit);
        const bool low = account.remaining * 10 < account.limit;
        if (used > 0) mtb_Panel_Fill_Rect(barX1, barY, fill, barY + 4, low ? C_DOWN : C_ACCENT);
        // The share kept back for the key's other uses.
        const int16_t keep = barX2 - (int16_t)((int32_t)(barX2 - barX1) * (100 - view.share) / 100);
        if (view.share < 100) mtb_Panel_Draw_VLine(keep, barY - 1, barY + 5, C_TEXT);
        const char* window = account.window == OPA_QUOTA_MONTH ? "THIS MONTH" : account.window == OPA_QUOTA_TRIAL ? "IN TRIAL" : "TODAY";
        snprintf(text, sizeof(text), "%u / %u %s", (unsigned)used, (unsigned)account.limit, window);
    } else snprintf(text, sizeof(text), demo ? "DEMO: 25 A DAY" : "QUOTA NOT YET KNOWN");
    opa_Text(barX2 + 6, barY - 1, Terminal4x6, C_TEXT, text);

    if (view.status && view.status->quoteInterval > 0){
        const uint32_t minutes = (view.status->quoteInterval + 30) / 60;
        if (view.status->quotaUsed) snprintf(text, sizeof(text), "QUOTA USED UP: PRICES RESUME WHEN IT RESETS");
        else if (demo) snprintf(text, sizeof(text), "DEMO PRICES EVERY %u MIN. A FREE KEY GIVES 50 REQUESTS A DAY", (unsigned)minutes);
        else if (minutes >= 120) snprintf(text, sizeof(text), "PRICES EVERY %u H, PACED TO LAST UNTIL THE RESET", (unsigned)(minutes / 60));
        else snprintf(text, sizeof(text), "PRICES EVERY %u MIN, PACED TO LAST UNTIL THE RESET", (unsigned)minutes);
        opa_Text(LEFT_X, 63, Terminal4x6, view.status->quotaUsed ? C_DOWN : C_LABEL, text);
    }

    // Features: a tick if this key has it, a padlock and the plan that brings it if not.
    bool hasWindow[OPA_W_COUNT] = {};
    for (int i = 0; i < data.quoteCount; i++) for (int w = 0; w < OPA_W_COUNT; w++) if (!isnan(data.quotes[i].change[w])) hasWindow[w] = true;
    const bool known = !demo && account.seen != 0;
    struct { const char* name; const char* plan; bool on; } features[] = {
        {"LIVE PRICES",      "FREE",         !demo || data.quotesFetched != 0},
        {"24H CHANGE",       "FREE",         hasWindow[OPA_W_24H] || data.demo},
        {"HISTORY CHART",    "DEVELOPER",    data.access[OPA_F_HISTORY].state == OPA_ACCESS_OK},
        {"7D CHANGE",        "STARTER",      hasWindow[OPA_W_7D]},
        {"30D CHANGE",       "PROFESSIONAL", hasWindow[OPA_W_30D]},
        {"FUTURES CURVE",    "PROFESSIONAL", data.access[OPA_F_FUTURES].state == OPA_ACCESS_OK},
        {"90D CHANGE",       "SCALE",        hasWindow[OPA_W_90D]},
        {"RIGS + OPEC",      "SCALE",        data.access[OPA_F_RIGS].state == OPA_ACCESS_OK || data.access[OPA_F_OPEC].state == OPA_ACCESS_OK},
    };
    bool anyLocked = false;
    for (int i = 0; i < 8; i++){
        const int16_t x = (i / 4) * 128 + LEFT_X, y = 71 + (i % 4) * 7;
        if (features[i].on) opa_Draw_Tick(x, y, C_UP);
        else if (known || demo) opa_Draw_Lock(x, y - 1, C_LOCK);
        else mtb_Panel_Fill_Rect(x + 1, y + 2, x + 3, y + 2, C_DIM);
        opa_Text(x + 8, y, Terminal4x6, features[i].on ? C_TEXT : C_LABEL, features[i].name);
        if (!features[i].on){
            opa_Text_Right(x + 122, y, Terminal4x6, C_LOCK, features[i].plan);
            anyLocked = true;
        }
    }
    if (demo) opa_Text_Centre(128, 101, Terminal4x6, C_AMBER, "FREE KEY, NO CARD: OILPRICEAPI.COM/SIGNUP");
    else if (anyLocked) opa_Text_Centre(128, 101, Terminal4x6, C_AMBER, "MORE DATA: OILPRICEAPI.COM/PRICING");
}

bool opa_Ui_Page_Available(const Opa_View_t& view, uint8_t page){
    if (view.data == nullptr) return page == OPA_PAGE_PLAN;
    switch (page){
        case OPA_PAGE_CURVE:  return view.data->curve.count >= 2 && view.data->access[OPA_F_FUTURES].state == OPA_ACCESS_OK;
        case OPA_PAGE_ENERGY: return (view.data->rigs.fetched != 0 && view.data->access[OPA_F_RIGS].state == OPA_ACCESS_OK)
                                  || (view.data->opec.fetched != 0 && view.data->access[OPA_F_OPEC].state == OPA_ACCESS_OK);
        case OPA_PAGE_HEAT:   return view.data->quotesFetched != 0 && view.data->quoteCount >= 3;
        default:              return page < OPA_PAGE_COUNT;
    }
}

void opa_Ui_Draw_Page(const Opa_View_t& view){
    mtb_Panel_Fill_Rect(0, PAGE_Y1, PANEL_RES_X - 1, PAGE_Y2, BLACK);
    if (view.data == nullptr) return;
    switch (view.page){
        case OPA_PAGE_CHART:   opa_Draw_Chart(view); break;
        case OPA_PAGE_CHANGES: opa_Draw_Changes(view); break;
        case OPA_PAGE_BOARD:   opa_Draw_Board(view); break;
        case OPA_PAGE_HEAT:    opa_Draw_Heat(view); break;
        case OPA_PAGE_CURVE:   opa_Draw_Curve(view); break;
        case OPA_PAGE_ENERGY:  opa_Draw_Energy(view); break;
        default:               opa_Draw_Plan(view); break;
    }
}

// The watchlist as chips, starting one before the featured code so it stays second from the left.
void opa_Ui_Draw_Strip(const Opa_View_t& view){
    mtb_Panel_Fill_Rect(0, STRIP_Y1, PANEL_RES_X - 1, STRIP_Y2, BLACK);
    if (view.data == nullptr || view.data->quoteCount == 0) return;
    const int count = view.data->quoteCount;
    const int selectedAt = (view.selected >= 0 && view.selected < count) ? view.selected : 0;
    int16_t x = 1;
    for (int k = 0; k < count; k++){
        const int index = (selectedAt - 1 + count + k) % count;
        const Opa_Quote_t& quote = view.data->quotes[index];
        const char* label = opa_Label(quote.code);
        const int16_t width = opa_Text_Width(Terminal6x8, label) + 15;
        if (x + width > PANEL_RES_X - 1) break;
        const bool selected = index == view.selected;
        const uint16_t background = selected ? C_SELECTED : BLACK;
        if (selected) mtb_Panel_Fill_Round_Rect(x, STRIP_Y1, x + width - 1, STRIP_Y2, 2, C_SELECTED);
        const int16_t after = opa_Text(x + 3, STRIP_Y1 + 1, Terminal6x8, selected ? C_ACCENT : C_LABEL, label, background);
        float percent = NAN;
        if (opa_Quote_Change(*view.data, quote, OPA_W_24H, percent) && percent != 0) opa_Draw_Triangle(after + 3, STRIP_Y1 + 2, 4, percent > 0, opa_Trend_Color(percent));
        else mtb_Panel_Fill_Rect(after + 3, STRIP_Y1 + 4, after + 8, STRIP_Y1 + 4, C_DIM);
        x += width + 3;
    }
}
