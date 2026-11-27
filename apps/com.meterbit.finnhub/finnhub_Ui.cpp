#include "finnhub_Ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mtb_graphics.h"

#define FH_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_UP       = FH_RGB(40, 225, 100);
static const uint16_t C_UP_DIM   = FH_RGB(0, 72, 34);
static const uint16_t C_DOWN     = FH_RGB(255, 64, 48);
static const uint16_t C_DOWN_DIM = FH_RGB(88, 16, 10);
static const uint16_t C_AMBER    = FH_RGB(255, 176, 0);
static const uint16_t C_ACCENT   = FH_RGB(0, 200, 255);
static const uint16_t C_TEXT     = FH_RGB(235, 235, 235);
static const uint16_t C_LABEL    = FH_RGB(130, 130, 130);
static const uint16_t C_LINE     = FH_RGB(48, 48, 48);
static const uint16_t C_SELECTED = FH_RGB(16, 56, 96);

// Layout. Row and column numbers are inclusive.
static const int16_t LOGO_X = 3, LOGO_Y = 11, LOGO_SIZE = 56;
static const int16_t HERO_X = 64;
static const int16_t RIGHT_X = 253;  // mtb_Write_String() drops a glyph whose advance reaches the panel edge.
static const int16_t HERO_Y1 = 10, HERO_Y2 = 69, HERO_RULE_Y = 70;
static const int16_t PAGE_Y1 = 72, PAGE_Y2 = 107;
static const int16_t WATCH_RULE_Y = 108, WATCH_Y1 = 109, WATCH_Y2 = 117, NEWS_RULE_Y = 118;
static const int WATCH_VISIBLE = 5;

//************************************************************************************ */
// Pixel width of a string, using the same rules as Mtb_Static_Text_t::mtb_Write_String().
static int16_t fh_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

// Text is drawn on whatever is already there: a new widget has no previous string to erase.
static int16_t fh_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    Mtb_FixedText_t widget(x, y, font, color);
    widget.mtb_Write_String(text);
    return x + fh_Text_Width(font, text);
}

static int16_t fh_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    int16_t x = rightX - fh_Text_Width(font, text) + 1;
    fh_Text(x < 0 ? 0 : x, y, font, color, text);
    return x;
}

static void fh_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    int16_t x = centreX - fh_Text_Width(font, text) / 2;
    fh_Text(x < 0 ? 0 : x, y, font, color, text);
}

// Copies text into dest, cut to fit maxWidth pixels.
static void fh_Fit_Text(char* dest, size_t destSize, const uint8_t* font, const char* text, int16_t maxWidth){
    snprintf(dest, destSize, "%s", text);
    for (size_t len = strlen(dest); len > 0 && fh_Text_Width(font, dest) > maxWidth; len--) dest[len - 1] = '\0';
    size_t len = strlen(dest);
    while (len > 0 && dest[len - 1] == ' ') dest[--len] = '\0';
}

static uint16_t fh_Trend_Color(double value){ return (value < 0) ? C_DOWN : C_UP; }

static void fh_Format_Price(char* dest, size_t size, double value){
    if (isnan(value)) snprintf(dest, size, "--");
    else if (fabs(value) >= 10000) snprintf(dest, size, "%.0f", value);
    else if (fabs(value) < 1) snprintf(dest, size, "%.4f", value);
    else snprintf(dest, size, "%.2f", value);
}

static void fh_Format_Number(char* dest, size_t size, double value, const char* format){
    if (isnan(value)) snprintf(dest, size, "--");
    else snprintf(dest, size, format, value);
}

// Finnhub reports market capitalisation in millions.
static void fh_Format_Market_Cap(char* dest, size_t size, double millions){
    if (isnan(millions) || millions <= 0) snprintf(dest, size, "--");
    else if (millions >= 1e6) snprintf(dest, size, "%.2fT", millions / 1e6);
    else if (millions >= 1e3) snprintf(dest, size, "%.1fB", millions / 1e3);
    else snprintf(dest, size, "%.0fM", millions);
}

// "2026-10-23" -> "Oct 23"
static void fh_Format_Date(char* dest, size_t size, const char* isoDate){
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int month = 0, day = 0, year = 0;
    if (sscanf(isoDate, "%d-%d-%d", &year, &month, &day) != 3 || month < 1 || month > 12) { snprintf(dest, size, "--"); return; }
    snprintf(dest, size, "%.3s %d", &months[(month - 1) * 3], day);
}

static int16_t fh_Position(double value, double low, double high, int16_t x1, int16_t x2){
    if (high <= low) return (x1 + x2) / 2;
    double ratio = (value - low) / (high - low);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    return x1 + (int16_t)lround(ratio * (x2 - x1));
}

static void fh_Draw_Triangle(int16_t x, int16_t y, int16_t size, bool up, uint16_t color){
    if (up) mtb_Panel_Fill_Triangle(x, y + size - 1, x + 2 * (size - 1), y + size - 1, x + size - 1, y, color);
    else mtb_Panel_Fill_Triangle(x, y, x + 2 * (size - 1), y, x + size - 1, y + size - 1, color);
}

//************************************************************************************ */
void fh_Ui_Clear_Content(void){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, NEWS_RULE_Y, BLACK);   // not the ticker below: Pixlpal scrolls it
    mtb_Panel_Draw_HLine(HERO_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(WATCH_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(NEWS_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
}

void fh_Ui_Draw_Logo_Placeholder(const Fh_Stock_t& stock){
    mtb_Panel_Fill_Rect(LOGO_X, LOGO_Y, LOGO_X + LOGO_SIZE - 1, LOGO_Y + LOGO_SIZE - 1, BLACK);
    if (stock.hasLogo || stock.symbol[0] == '\0') return;
    mtb_Panel_Fill_Round_Rect(LOGO_X, LOGO_Y, LOGO_X + LOGO_SIZE - 1, LOGO_Y + LOGO_SIZE - 1, 8, C_SELECTED);
    char letter[2] = {stock.symbol[0], '\0'};
    fh_Text_Centre(LOGO_X + LOGO_SIZE / 2, LOGO_Y + (LOGO_SIZE - PT_Sans_Narrow38x34[6]) / 2, PT_Sans_Narrow38x34, C_TEXT, letter);
}

//************************************************************************************ */
static void fh_Draw_Day_Range(const Fh_Stock_t& stock){
    const int16_t y = 63;
    if (!stock.hasQuote || stock.high <= stock.low) return;
    char low[12], high[12];
    fh_Format_Price(low, sizeof(low), stock.low);
    fh_Format_Price(high, sizeof(high), stock.high);
    int16_t x1 = fh_Text(HERO_X, y, Terminal4x6, C_LABEL, low) + 4;
    int16_t x2 = fh_Text_Right(RIGHT_X, y, Terminal4x6, C_LABEL, high) - 5;

    mtb_Panel_Fill_Rect(x1, y + 1, x2, y + 3, C_LINE);
    int16_t priceX = fh_Position(stock.price, stock.low, stock.high, x1, x2);
    mtb_Panel_Fill_Rect(x1, y + 1, priceX, y + 3, stock.change < 0 ? C_DOWN_DIM : C_UP_DIM);
    mtb_Panel_Draw_VLine(fh_Position(stock.open, stock.low, stock.high, x1, x2), y, y + 4, C_LABEL);
    mtb_Panel_Fill_Rect(priceX - 1, y - 1, priceX + 1, y + 5, fh_Trend_Color(stock.change));
}

void fh_Ui_Draw_Hero(const Fh_Stock_t& stock, const Fh_Market_t& market, uint32_t nowMs){
    mtb_Panel_Fill_Rect(HERO_X, HERO_Y1, PANEL_RES_X - 1, HERO_Y2, BLACK);
    int16_t nameX = fh_Text(HERO_X, HERO_Y1, Terminal10x17, C_ACCENT, stock.symbol) + 8;

    // Market session chip with the time of the last trade below it.
    int16_t chipX = RIGHT_X + 1;
    if (market.known){
        uint16_t chipColor = market.isOpen ? C_UP : (strcmp(market.session, "CLOSED") == 0 ? C_LABEL : C_AMBER);
        chipX = RIGHT_X - fh_Text_Width(Terminal6x8, market.session) - 5;
        mtb_Panel_Fill_Round_Rect(chipX, HERO_Y1, RIGHT_X, HERO_Y1 + 9, 2, chipColor);
        fh_Text(chipX + 3, HERO_Y1 + 1, Terminal6x8, BLACK, market.session);
    }
    char line[48] = "";
    if (stock.hasQuote && stock.quoteTime > 0){
        struct tm local;
        localtime_r(&stock.quoteTime, &local);
        snprintf(line, sizeof(line), "%02d:%02d", local.tm_hour, local.tm_min);
    }
    int16_t timeX = fh_Text_Right(RIGHT_X, HERO_Y1 + 11, Terminal6x8, C_LABEL, line);

    if (stock.hasProfile){
        char fitted[48];
        fh_Fit_Text(fitted, sizeof(fitted), Terminal6x8, stock.name, chipX - nameX - 3);
        fh_Text(nameX, HERO_Y1 + 1, Terminal6x8, C_TEXT, fitted);
        snprintf(line, sizeof(line), "%s %s", stock.exchange, stock.industry);
        if (fh_Text_Width(Terminal6x8, line) > timeX - nameX - 3) snprintf(line, sizeof(line), "%s", stock.industry);
        fh_Fit_Text(fitted, sizeof(fitted), Terminal6x8, line, timeX - nameX - 3);
        fh_Text(nameX, HERO_Y1 + 11, Terminal6x8, C_LABEL, fitted);
    }

    // Price: flashes the colour of the move for a moment after each change.
    const int16_t priceY = 28;
    if (!stock.hasQuote){
        fh_Text(HERO_X, priceY, PT_Sans_Narrow38x34, C_LABEL, "--");
        if (stock.noQuoteForSymbol) fh_Text_Right(RIGHT_X, priceY + 13, Terminal8x12, C_AMBER, "NO QUOTE");
        return;
    }
    char price[16];
    fh_Format_Price(price, sizeof(price), stock.price);
    bool flashing = stock.tickDirection != 0 && (nowMs - stock.tickMs) < 1200;
    uint16_t priceColor = flashing ? (stock.tickDirection > 0 ? C_UP : C_DOWN) : C_TEXT;
    int16_t priceEnd = fh_Text(HERO_X, priceY, PT_Sans_Narrow38x34, priceColor, price);

    // Day change, in the largest font that still fits next to the price.
    char change[16], percent[16];
    snprintf(change, sizeof(change), "%.2f", fabs(stock.change));
    snprintf(percent, sizeof(percent), "%+.2f%%", stock.changePct);
    const uint16_t trend = fh_Trend_Color(stock.change);
    const int16_t available = RIGHT_X - priceEnd - 3;
    const int16_t triangleW = 11;
    const uint8_t* font = Terminal10x16;
    if (fh_Text_Width(font, change) + triangleW > available || fh_Text_Width(font, percent) > available) font = Terminal8x12;
    if (fh_Text_Width(font, change) + triangleW > available || fh_Text_Width(font, percent) > available) font = Terminal6x8;
    const int16_t height = font[6];
    const int16_t changeY = priceY + 1, percentY = priceY + 32 - height;
    int16_t changeX = fh_Text_Right(RIGHT_X, changeY, font, trend, change);
    fh_Draw_Triangle(changeX - triangleW, changeY + height / 2 - 3, 5, stock.change >= 0, trend);
    fh_Text_Right(RIGHT_X, percentY, font, trend, percent);

    fh_Draw_Day_Range(stock);
}

//************************************************************************************ */
// Page 0: price path of this session against the previous close, built from the quotes the app has seen.
static void fh_Draw_Session_Chart(const Fh_Stock_t& stock, int16_t x1, int16_t x2){
    const int16_t y1 = PAGE_Y1, y2 = PAGE_Y2;
    const int count = stock.historyCount < (x2 - x1 + 1) ? stock.historyCount : (x2 - x1 + 1);
    const int first = stock.historyCount - count;

    double low = stock.prevClose, high = stock.prevClose;
    for (int i = first; i < stock.historyCount; i++){
        if (isnan(stock.history[i])) continue;
        if (stock.history[i] < low) low = stock.history[i];
        if (stock.history[i] > high) high = stock.history[i];
    }
    double margin = (high - low) * 0.06;
    if (margin < stock.prevClose * 0.0005) margin = stock.prevClose * 0.0005;
    low -= margin;
    high += margin;
    const auto rowOf = [&](double value){ return (int16_t)(y2 - lround((value - low) / (high - low) * (y2 - y1))); };
    const int16_t baseY = rowOf(stock.prevClose);

    int16_t previousY = -1;
    int16_t lastX = -1, lastY = -1;
    uint16_t lastColor = C_TEXT;
    for (int i = first; i < stock.historyCount; i++){
        const int16_t x = x2 - (stock.historyCount - 1 - i);
        if (isnan(stock.history[i])) { previousY = -1; continue; }
        const int16_t y = rowOf(stock.history[i]);
        const bool above = stock.history[i] >= stock.prevClose;
        mtb_Panel_Draw_VLine(x, y, baseY, above ? C_UP_DIM : C_DOWN_DIM);
        lastColor = above ? C_UP : C_DOWN;
        mtb_Panel_Draw_VLine(x, previousY < 0 ? y : previousY, y, lastColor);
        previousY = y;
        lastX = x;
        lastY = y;
    }
    for (int16_t x = x1; x <= x2; x += 4) mtb_Panel_Draw_PixelRGB565(x, baseY, C_LABEL);
    if (lastX >= 0) mtb_Panel_Fill_Circle(lastX, lastY, 1, C_TEXT);
    fh_Text(x1 + 1, y1, Terminal6x8, C_LABEL, "TODAY");
}

static void fh_Draw_Page_Session(const Fh_Stock_t& stock){
    struct { const char* label; double value; } rows[4] = {
        {"OPEN", stock.open}, {"HIGH", stock.high}, {"LOW", stock.low}, {"PREV", stock.prevClose}};
    char value[16];

    if (stock.hasQuote && fh_Ui_History_Points(stock) >= 2){
        const int16_t statsX = 180;
        fh_Draw_Session_Chart(stock, 0, 175);
        for (int i = 0; i < 4; i++){
            const int16_t y = PAGE_Y1 + 1 + i * 9;
            fh_Format_Price(value, sizeof(value), stock.hasQuote ? rows[i].value : NAN);
            fh_Text(statsX, y, Terminal6x8, C_LABEL, rows[i].label);
            fh_Text_Right(RIGHT_X, y, Terminal6x8, C_TEXT, value);
        }
        return;
    }
    // No chart yet (market closed, or the app has just started): the same numbers as large tiles.
    for (int i = 0; i < 4; i++){
        const int16_t x = i * 64;
        fh_Format_Price(value, sizeof(value), stock.hasQuote ? rows[i].value : NAN);
        mtb_Panel_Draw_Round_Rect(x + 1, PAGE_Y1 + 1, x + 62, PAGE_Y2 - 1, 3, C_LINE);
        fh_Text_Centre(x + 32, PAGE_Y1 + 6, Terminal6x8, C_LABEL, rows[i].label);
        fh_Text_Centre(x + 32, PAGE_Y1 + 18, Terminal8x12, C_TEXT, value);
    }
}

//************************************************************************************ */
// Page 1: returns over seven horizons. Tile brightness shows how big the move is for that horizon.
static void fh_Draw_Page_Returns(const Fh_Stock_t& stock){
    struct { const char* label; float value; float fullScale; } tiles[7] = {
        {"1D", stock.hasQuote ? (float)stock.changePct : NAN, 4}, {"5D", stock.return5D, 8}, {"MTD", stock.returnMTD, 12},
        {"13W", stock.return13W, 25}, {"26W", stock.return26W, 40}, {"YTD", stock.returnYTD, 50}, {"1Y", stock.return52W, 60}};
    const int16_t tileW = 36, startX = 2, tileH = PAGE_Y2 - PAGE_Y1;

    for (int i = 0; i < 7; i++){
        const int16_t x = startX + i * tileW, centre = x + (tileW - 1) / 2;
        const float value = tiles[i].value;
        const bool known = !isnan(value);
        float strength = known ? fabsf(value) / tiles[i].fullScale : 0;
        if (strength > 1) strength = 1;
        const uint8_t shade = 28 + (uint8_t)(strength * 92);
        uint16_t background = !known ? FH_RGB(24, 24, 24) : (value < 0 ? FH_RGB(shade, shade / 8, shade / 10) : FH_RGB(shade / 10, shade, shade / 3));
        mtb_Panel_Fill_Round_Rect(x, PAGE_Y1, x + tileW - 2, PAGE_Y1 + tileH, 2, background);
        fh_Text_Centre(centre, PAGE_Y1 + 5, Terminal6x8, C_TEXT, tiles[i].label);

        char text[12];
        if (!known) snprintf(text, sizeof(text), "--");
        else {
            snprintf(text, sizeof(text), fabsf(value) < 10 ? "%+.1f%%" : "%+.0f%%", value);
        }
        fh_Text_Centre(centre, PAGE_Y1 + 21, Terminal6x8, FH_RGB(255, 255, 255), text);
    }
}

//************************************************************************************ */
// Page 2: key numbers of the company.
static void fh_Draw_Page_Fundamentals(const Fh_Stock_t& stock){
    char cap[12], pe[12], eps[12], beta[12], yield[12], margin[12];
    fh_Format_Market_Cap(cap, sizeof(cap), stock.hasProfile ? stock.marketCapMillions : NAN);
    fh_Format_Number(pe, sizeof(pe), stock.peRatio, "%.1f");
    fh_Format_Number(eps, sizeof(eps), stock.eps, "%.2f");
    fh_Format_Number(beta, sizeof(beta), stock.beta, "%.2f");
    fh_Format_Number(yield, sizeof(yield), stock.dividendYield, "%.2f%%");
    fh_Format_Number(margin, sizeof(margin), stock.netMargin, "%.1f%%");
    struct { const char* label; const char* value; uint16_t color; } cells[6] = {
        {"MKT CAP", cap, C_ACCENT}, {"P/E", pe, C_TEXT}, {"EPS", eps, stock.eps < 0 ? C_DOWN : C_TEXT},
        {"BETA", beta, C_TEXT}, {"DIV YIELD", yield, C_TEXT}, {"NET MARGIN", margin, stock.netMargin < 0 ? C_DOWN : C_TEXT}};

    for (int i = 0; i < 6; i++){
        const int16_t x = 4 + (i % 3) * 85, y = PAGE_Y1 + (i / 3) * 18;
        fh_Text(x, y, Terminal6x8, C_LABEL, cells[i].label);
        fh_Text(x, y + 7, Terminal8x12, cells[i].color, cells[i].value);
    }
}

//************************************************************************************ */
// Page 3: where the price sits in its 52-week range.
static void fh_Draw_Page_52Week(const Fh_Stock_t& stock){
    fh_Text(2, PAGE_Y1, Terminal6x8, C_LABEL, "52 WEEK RANGE");
    if (!stock.hasMetrics || isnan(stock.week52High) || isnan(stock.week52Low) || stock.week52High <= stock.week52Low){
        fh_Text(2, PAGE_Y1 + 14, Terminal8x12, C_LABEL, "--");
        return;
    }
    const int16_t x1 = 2, x2 = 253, barY = PAGE_Y1 + 13;
    for (int16_t x = x1; x <= x2; x++){
        // Red at the low, through amber, to green at the high.
        const int ratio = (x - x1) * 255 / (x2 - x1);
        uint8_t r = ratio < 128 ? 150 : (uint8_t)(150 - (ratio - 128) * 150 / 127);
        uint8_t g = ratio < 128 ? (uint8_t)(ratio * 150 / 127) : 150;
        mtb_Panel_Draw_VLine(x, barY, barY + 5, FH_RGB(r, g, 0));
    }
    if (stock.hasQuote){
        char text[32];
        const int16_t px = fh_Position(stock.price, stock.week52Low, stock.week52High, x1 + 3, x2 - 3);
        mtb_Panel_Fill_Triangle(px - 3, barY - 4, px + 3, barY - 4, px, barY - 1, C_TEXT);
        mtb_Panel_Draw_VLine(px, barY, barY + 5, C_TEXT);
        const double fromHigh = (stock.price - stock.week52High) / stock.week52High * 100;
        const double fromLow = (stock.price - stock.week52Low) / stock.week52Low * 100;
        snprintf(text, sizeof(text), "%+.1f%% FROM HIGH", fromHigh);
        fh_Text_Right(RIGHT_X - 1, PAGE_Y1, Terminal6x8, fromHigh > -5 ? C_UP : C_TEXT, text);
        if (stock.hasTarget){
            // Analysts' price target (paid plans): a mark on the bar, and how far it is from the price.
            const int16_t tx = fh_Position(stock.targetMean, stock.week52Low, stock.week52High, x1 + 3, x2 - 3);
            mtb_Panel_Draw_VLine(tx, barY - 2, barY + 7, C_ACCENT);
            mtb_Panel_Draw_HLine(barY + 7, tx - 2, tx + 2, C_ACCENT);
            const double upside = (stock.targetMean - stock.price) / stock.price * 100;
            snprintf(text, sizeof(text), "TARGET %.2f %+.0f%%", stock.targetMean, upside);
            fh_Text_Centre(127, PAGE_Y1 + 26, Terminal6x8, C_ACCENT, text);
        } else {
            snprintf(text, sizeof(text), "%+.0f%% FROM LOW", fromLow);
            fh_Text_Centre(127, PAGE_Y1 + 26, Terminal6x8, C_TEXT, text);
        }
    }
    char price[12], date[12];
    const int16_t rowY = PAGE_Y1 + 23;
    fh_Format_Price(price, sizeof(price), stock.week52Low);
    fh_Format_Date(date, sizeof(date), stock.week52LowDate);
    fh_Text(fh_Text(x1, rowY, Terminal8x12, C_DOWN, price) + 5, rowY + 3, Terminal6x8, C_LABEL, date);
    fh_Format_Price(price, sizeof(price), stock.week52High);
    fh_Format_Date(date, sizeof(date), stock.week52HighDate);
    fh_Text_Right(fh_Text_Right(x2, rowY, Terminal8x12, C_UP, price) - 5, rowY + 3, Terminal6x8, C_LABEL, date);
}

//************************************************************************************ */
// Page 4: analyst consensus on the left, earnings track record on the right.
static void fh_Draw_Page_Street(const Fh_Stock_t& stock){
    const int16_t leftX = 2, leftEnd = 122, rightX = 132;
    const int total = stock.strongBuy + stock.buy + stock.hold + stock.sell + stock.strongSell;
    char heading[16];
    snprintf(heading, sizeof(heading), "%d ANALYSTS", total);
    fh_Text(leftX, PAGE_Y1, Terminal6x8, C_LABEL, (stock.hasAnalysts && total > 0) ? heading : "ANALYSTS");
    if (stock.hasAnalysts && total > 0){
        const double score = (2.0 * stock.strongBuy + stock.buy - stock.sell - 2.0 * stock.strongSell) / total;
        const char* verdict = score >= 1.0 ? "STRONG BUY" : score >= 0.35 ? "BUY" : score > -0.35 ? "HOLD" : "SELL";
        const uint16_t verdictColor = score >= 0.35 ? C_UP : score > -0.35 ? C_AMBER : C_DOWN;
        fh_Text_Right(leftEnd, PAGE_Y1, Terminal6x8, verdictColor, verdict);

        struct { uint16_t count; uint16_t color; const char* label; } parts[5] = {
            {stock.strongBuy, FH_RGB(0, 200, 80), "SBUY"}, {stock.buy, FH_RGB(110, 210, 60), "BUY"}, {stock.hold, C_AMBER, "HOLD"},
            {stock.sell, FH_RGB(255, 110, 40), "SELL"}, {stock.strongSell, C_DOWN, "SSELL"}};
        const int16_t barY = PAGE_Y1 + 10, barW = leftEnd - leftX + 1;
        int running = 0;
        for (int i = 0; i < 5; i++){
            int16_t a = leftX + running * barW / total;
            running += parts[i].count;
            int16_t b = leftX + running * barW / total - 1;
            if (b >= a) mtb_Panel_Fill_Rect(a, barY, b, barY + 8, parts[i].color);
        }
        // Counts sit in five even columns below the bar.
        char text[8];
        for (int i = 0; i < 5; i++){
            const int16_t centre = leftX + barW * (2 * i + 1) / 10;
            snprintf(text, sizeof(text), "%u", parts[i].count);
            fh_Text_Centre(centre, PAGE_Y1 + 20, Terminal6x8, parts[i].color, text);
            fh_Text_Centre(centre, PAGE_Y1 + 29, Terminal4x6, C_LABEL, parts[i].label);
        }
    } else fh_Text(leftX, PAGE_Y1 + 14, Terminal8x12, C_LABEL, "--");

    mtb_Panel_Draw_VLine(127, PAGE_Y1, PAGE_Y2, C_LINE);

    fh_Text(rightX, PAGE_Y1, Terminal6x8, C_LABEL, "EPS BEAT %");
    if (stock.nextEarningsDate[0]){
        char date[12], text[24];
        fh_Format_Date(date, sizeof(date), stock.nextEarningsDate);
        snprintf(text, sizeof(text), "NEXT %s", date);
        fh_Text_Right(RIGHT_X - 1, PAGE_Y1, Terminal6x8, C_ACCENT, text);
    }
    if (stock.earningsCount == 0){
        fh_Text(rightX, PAGE_Y1 + 14, Terminal8x12, C_LABEL, "--");
        return;
    }
    // Oldest quarter on the left: one tile per quarter, green for a beat and red for a miss.
    const int16_t tileW = 30, tileY = PAGE_Y1 + 10;
    for (int i = 0; i < stock.earningsCount; i++){
        const int source = stock.earningsCount - 1 - i;
        const int16_t x = rightX + i * tileW, centre = x + (tileW - 2) / 2;
        const float surprise = stock.earningsSurprisePct[source];
        const uint16_t background = isnan(surprise) ? C_LINE : (surprise < 0 ? C_DOWN_DIM : C_UP_DIM);
        mtb_Panel_Fill_Round_Rect(x, tileY, x + tileW - 3, tileY + 15, 2, background);
        char text[10];
        if (isnan(surprise)) snprintf(text, sizeof(text), "--");
        else {
            snprintf(text, sizeof(text), "%+.1f", surprise);
            if (fh_Text_Width(Terminal6x8, text) > tileW - 4) snprintf(text, sizeof(text), "%+.0f", surprise);
        }
        fh_Text_Centre(centre, tileY + 4, Terminal6x8, C_TEXT, text);
        snprintf(text, sizeof(text), "Q%u", stock.earningsQuarter[source]);
        fh_Text_Centre(centre, tileY + 19, Terminal6x8, C_LABEL, text);
    }
}

//************************************************************************************ */
void fh_Ui_Draw_Page(uint8_t page, const Fh_Stock_t& stock, bool autoCycleHeld){
    mtb_Panel_Fill_Rect(0, PAGE_Y1, PANEL_RES_X - 1, PAGE_Y2, BLACK);
    switch (page){
        case 0: fh_Draw_Page_Session(stock); break;
        case 1: fh_Draw_Page_Returns(stock); break;
        case 2: fh_Draw_Page_Fundamentals(stock); break;
        case 3: fh_Draw_Page_52Week(stock); break;
        default: fh_Draw_Page_Street(stock); break;
    }
    // Page indicator, set into the rule above the panel. Amber: auto-cycling through the stocks is on hold.
    for (int i = 0; i < FH_PAGE_COUNT; i++){
        const int16_t x = RIGHT_X - (FH_PAGE_COUNT - 1 - i) * 7 - 4;
        mtb_Panel_Fill_Rect(x, HERO_RULE_Y, x + 4, HERO_RULE_Y + 1, (i == page) ? (autoCycleHeld ? C_AMBER : C_ACCENT) : FH_RGB(88, 88, 88));
    }
}

void fh_Ui_Draw_Watchlist(Fh_Stock_t* const* watch, int count, int selected){
    mtb_Panel_Fill_Rect(0, WATCH_Y1, PANEL_RES_X - 1, WATCH_Y2, BLACK);
    if (count <= 0) return;

    const int visible = (count < WATCH_VISIBLE) ? count : WATCH_VISIBLE;
    const int16_t cellW = PANEL_RES_X / WATCH_VISIBLE;
    int start = 0;
    if (count > WATCH_VISIBLE && selected >= 0) start = (selected - WATCH_VISIBLE / 2 + count) % count;

    for (int i = 0; i < visible; i++){
        const int index = (start + i) % count;
        const Fh_Stock_t* stock = watch[index];
        if (stock == nullptr) continue;
        const int16_t x = i * cellW;
        if (index == selected) mtb_Panel_Fill_Rect(x, WATCH_Y1, x + cellW - 2, WATCH_Y2, C_SELECTED);
        char ticker[FH_SYMBOL_LEN];
        fh_Fit_Text(ticker, sizeof(ticker), Terminal6x8, stock->symbol, 30);
        const int16_t textEnd = fh_Text(x + 2, WATCH_Y1 + 1, Terminal6x8, (index == selected) ? FH_RGB(255, 255, 255) : FH_RGB(170, 170, 170), ticker);
        if (!stock->hasQuote) continue;
        const uint16_t trend = fh_Trend_Color(stock->change);
        char percent[8];
        const int16_t cellEnd = x + cellW - 3;
        snprintf(percent, sizeof(percent), fabs(stock->changePct) < 10 ? "%.1f" : "%.0f", fabs(stock->changePct));
        // A long ticker gives up the decimal rather than the arrow.
        if (cellEnd - fh_Text_Width(Terminal6x8, percent) + 1 - textEnd < 7) snprintf(percent, sizeof(percent), "%.0f", fabs(stock->changePct));
        const int16_t percentX = fh_Text_Right(cellEnd, WATCH_Y1 + 1, Terminal6x8, trend, percent);
        fh_Draw_Triangle(percentX - 6, WATCH_Y1 + 3, 3, stock->change >= 0, trend);
    }
}

uint16_t fh_Ui_History_Points(const Fh_Stock_t& stock){
    uint16_t points = 0;
    for (uint16_t i = 0; i < stock.historyCount; i++) if (!isnan(stock.history[i])) points++;
    return points;
}
