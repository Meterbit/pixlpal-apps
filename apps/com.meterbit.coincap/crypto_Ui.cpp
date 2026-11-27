#include "crypto_Ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mtb_graphics.h"

#define CC_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_UP       = CC_RGB(40, 225, 100);
static const uint16_t C_UP_DIM   = CC_RGB(0, 72, 34);
static const uint16_t C_DOWN     = CC_RGB(255, 64, 48);
static const uint16_t C_DOWN_DIM = CC_RGB(88, 16, 10);
static const uint16_t C_AMBER    = CC_RGB(255, 176, 0);
static const uint16_t C_ACCENT   = CC_RGB(255, 150, 20);
static const uint16_t C_TEXT     = CC_RGB(235, 235, 235);
static const uint16_t C_LABEL    = CC_RGB(130, 130, 130);
static const uint16_t C_LINE     = CC_RGB(48, 48, 48);
static const uint16_t C_SELECTED = CC_RGB(84, 48, 8);

// Layout. Row and column numbers are inclusive.
static const int16_t ICON_X = 3, ICON_Y = 11, ICON_SIZE = 32;
static const int16_t RANK_Y1 = 45, RANK_Y2 = 55;
static const int16_t HERO_X = 40;
static const int16_t RIGHT_X = 253;  // mtb_Write_String() drops a glyph whose advance reaches the panel edge.
static const int16_t HERO_Y1 = 10, HERO_Y2 = 56, HERO_RULE_Y = 57;
static const int16_t PAGE_Y1 = 59, PAGE_Y2 = 107;
static const int16_t WATCH_RULE_Y = 108, WATCH_Y1 = 109, WATCH_Y2 = 117, TICKER_RULE_Y = 118;
static const int WATCH_VISIBLE = 5;
static const uint32_t DAY_SEC = 86400;
static const uint32_t CHART_SEC = 7 * DAY_SEC;

//************************************************************************************ */
// Pixel width of a string, using the same rules as Mtb_Static_Text_t::mtb_Write_String().
static int16_t cc_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

// Text is drawn on whatever is already there: a new widget has no previous string to erase.
static int16_t cc_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    Mtb_FixedText_t widget(x, y, font, color);
    widget.mtb_Write_String(text);
    return x + cc_Text_Width(font, text);
}

static int16_t cc_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    int16_t x = rightX - cc_Text_Width(font, text) + 1;
    cc_Text(x < 0 ? 0 : x, y, font, color, text);
    return x;
}

static void cc_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    int16_t x = centreX - cc_Text_Width(font, text) / 2;
    cc_Text(x < 0 ? 0 : x, y, font, color, text);
}

// Copies text into dest, cut to fit maxWidth pixels.
static void cc_Fit_Text(char* dest, size_t destSize, const uint8_t* font, const char* text, int16_t maxWidth){
    snprintf(dest, destSize, "%s", text);
    for (size_t len = strlen(dest); len > 0 && cc_Text_Width(font, dest) > maxWidth; len--) dest[len - 1] = '\0';
    size_t len = strlen(dest);
    while (len > 0 && dest[len - 1] == ' ') dest[--len] = '\0';
}

// Width and drawing of a price in a font whose comma would pass for a decimal point: the digit groups are set apart instead.
static const int16_t GROUP_GAP = 6;

static int16_t cc_Grouped_Width(const uint8_t* font, const char* text){
    char plain[24];
    size_t n = 0;
    int16_t gaps = 0;
    for (const char* c = text; *c && n + 1 < sizeof(plain); c++){
        if (*c == ',') gaps += GROUP_GAP;
        else plain[n++] = *c;
    }
    plain[n] = '\0';
    return cc_Text_Width(font, plain) + gaps;
}

static int16_t cc_Grouped_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text){
    char group[24];
    size_t n = 0;
    for (const char* c = text; ; c++){
        if (*c == ',' || *c == '\0'){
            group[n] = '\0';
            x = cc_Text(x, y, font, color, group);
            n = 0;
            if (*c == '\0') return x;
            x += GROUP_GAP;
        } else if (n + 1 < sizeof(group)) group[n++] = *c;
    }
}

static uint16_t cc_Trend_Color(double value){ return (value < 0) ? C_DOWN : C_UP; }

// "76104.4" -> "76,104"
static void cc_Add_Thousands(char* dest, size_t size, double value, int decimals){
    char digits[32];
    snprintf(digits, sizeof(digits), "%.*f", decimals, fabs(value));
    const char* point = strchr(digits, '.');
    const int whole = point ? (int)(point - digits) : (int)strlen(digits);
    size_t n = 0;
    if (value < 0 && n + 1 < size) dest[n++] = '-';
    for (int i = 0; digits[i] && n + 1 < size; i++){
        if (i > 0 && i < whole && (whole - i) % 3 == 0 && n + 2 < size) dest[n++] = ',';
        dest[n++] = digits[i];
    }
    dest[n] = '\0';
}

static void cc_Format_Price(char* dest, size_t size, double value){
    const double magnitude = fabs(value);
    if (isnan(value)) snprintf(dest, size, "--");
    else if (magnitude >= 10000) cc_Add_Thousands(dest, size, value, 0);
    else if (magnitude >= 1) cc_Add_Thousands(dest, size, value, 2);
    else if (magnitude >= 0.01) snprintf(dest, size, "%.4f", value);
    else if (magnitude >= 0.0001) snprintf(dest, size, "%.6f", value);
    else snprintf(dest, size, "%.8f", value);
}

void cc_Ui_Format_Big(char* dest, unsigned size, double value){
    const double magnitude = fabs(value);
    if (isnan(value)) snprintf(dest, size, "--");
    else if (magnitude >= 1e12) snprintf(dest, size, "%.2fT", value / 1e12);
    else if (magnitude >= 1e9) snprintf(dest, size, "%.2fB", value / 1e9);
    else if (magnitude >= 1e6) snprintf(dest, size, "%.2fM", value / 1e6);
    else if (magnitude >= 1e3) snprintf(dest, size, "%.1fK", value / 1e3);
    else snprintf(dest, size, "%.0f", value);
}

void cc_Ui_Format_Duration(char* dest, unsigned size, uint32_t seconds){
    if (seconds < 3600) snprintf(dest, size, "%um", (unsigned)(seconds / 60));
    else if (seconds < DAY_SEC && seconds % 3600 < 60) snprintf(dest, size, "%uh", (unsigned)(seconds / 3600));
    else if (seconds < DAY_SEC) snprintf(dest, size, "%uh %02um", (unsigned)(seconds / 3600), (unsigned)(seconds % 3600 / 60));
    else snprintf(dest, size, "%ud %uh", (unsigned)(seconds / DAY_SEC), (unsigned)(seconds % DAY_SEC / 3600));
}

static void cc_Format_Percent(char* dest, size_t size, double value){
    if (isnan(value)) snprintf(dest, size, "--");
    else snprintf(dest, size, fabs(value) < 100 ? "%+.2f%%" : "%+.0f%%", value);
}

static int16_t cc_Position(double value, double low, double high, int16_t x1, int16_t x2){
    if (high <= low) return (x1 + x2) / 2;
    double ratio = (value - low) / (high - low);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    return x1 + (int16_t)lround(ratio * (x2 - x1));
}

static void cc_Draw_Triangle(int16_t x, int16_t y, int16_t size, bool up, uint16_t color){
    if (up) mtb_Panel_Fill_Triangle(x, y + size - 1, x + 2 * (size - 1), y + size - 1, x + size - 1, y, color);
    else mtb_Panel_Fill_Triangle(x, y, x + 2 * (size - 1), y, x + size - 1, y + size - 1, color);
}

// Price of the chart at a past moment, or NAN if the chart holds nothing within half a day of it.
static double cc_Price_At(const Cc_Coin_t& coin, uint32_t when){
    const uint32_t tolerance = DAY_SEC / 2;
    for (int i = 0; i < coin.historyCount; i++){
        const Cc_Point_t& point = coin.history[i];
        if (point.time < when) continue;
        if (i == 0) return (point.time - when <= tolerance) ? point.price : NAN;
        const Cc_Point_t& before = coin.history[i - 1];
        if (when - before.time > tolerance && point.time - when > tolerance) return NAN;
        const double ratio = (double)(when - before.time) / (double)(point.time - before.time);
        return before.price + (point.price - before.price) * ratio;
    }
    return NAN;
}

static double cc_Return_Since(const Cc_Coin_t& coin, uint32_t nowUnix, uint32_t seconds){
    const double past = cc_Price_At(coin, nowUnix - seconds);
    return (coin.hasQuote && !isnan(past) && past > 0) ? (coin.price - past) / past * 100 : NAN;
}

// Lowest and highest price of the last seven days, the current one included. Returns false if there is no chart.
static bool cc_Chart_Range(const Cc_Coin_t& coin, uint32_t nowUnix, double& low, double& high){
    bool found = false;
    for (int i = 0; i < coin.historyCount; i++){
        if (coin.history[i].time + CHART_SEC < nowUnix) continue;
        const double value = coin.history[i].price;
        if (!found || value < low) low = value;
        if (!found || value > high) high = value;
        found = true;
    }
    if (found && coin.hasQuote){
        if (coin.price < low) low = coin.price;
        if (coin.price > high) high = coin.price;
    }
    return found;
}

//************************************************************************************ */
void cc_Ui_Clear_Content(void){
    mtb_Panel_Fill_Rect(0, HERO_Y1, PANEL_RES_X - 1, TICKER_RULE_Y, BLACK);   // not the ticker below: Pixlpal scrolls it
    mtb_Panel_Draw_HLine(HERO_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(WATCH_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    mtb_Panel_Draw_HLine(TICKER_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
}

void cc_Ui_Draw_Icon_Placeholder(const Cc_Coin_t& coin){
    mtb_Panel_Fill_Rect(ICON_X, ICON_Y, ICON_X + ICON_SIZE - 1, ICON_Y + ICON_SIZE - 1, BLACK);
    if (coin.hasIcon || coin.symbol[0] == '\0') return;
    mtb_Panel_Fill_Circle(ICON_X + ICON_SIZE / 2, ICON_Y + ICON_SIZE / 2, ICON_SIZE / 2 - 1, C_SELECTED);
    char letter[2] = {coin.symbol[0], '\0'};
    cc_Text_Centre(ICON_X + ICON_SIZE / 2, ICON_Y + (ICON_SIZE - Terminal10x16[6]) / 2, Terminal10x16, C_TEXT, letter);
}

void cc_Ui_Draw_Hero(const Cc_Coin_t& coin, uint32_t nowUnix){
    mtb_Panel_Fill_Rect(HERO_X, HERO_Y1, PANEL_RES_X - 1, HERO_Y2, BLACK);
    mtb_Panel_Fill_Rect(0, RANK_Y1 - 1, HERO_X - 1, HERO_Y2, BLACK);

    char line[40], fitted[40];
    if (coin.rank > 0){
        snprintf(line, sizeof(line), "#%u", coin.rank);
        mtb_Panel_Fill_Round_Rect(ICON_X, RANK_Y1, ICON_X + ICON_SIZE - 1, RANK_Y2, 2, C_SELECTED);
        cc_Text_Centre(ICON_X + ICON_SIZE / 2, RANK_Y1 + 2, Terminal6x8, C_TEXT, line);
    }

    const int16_t nameX = cc_Text(HERO_X, HERO_Y1, Terminal8x12, C_ACCENT, coin.symbol) + 7;

    // Prices refresh hours apart, so the screen always says how old the one on it is.
    int16_t ageX = RIGHT_X + 1;
    if (coin.hasQuote && coin.quoteTime > 0 && nowUnix != 0){
        const uint32_t age = (nowUnix > coin.quoteTime) ? nowUnix - coin.quoteTime : 0;
        char duration[16];
        cc_Ui_Format_Duration(duration, sizeof(duration), age);
        if (age < 60) snprintf(line, sizeof(line), "now");
        else snprintf(line, sizeof(line), "%s ago", duration);
        ageX = cc_Text_Right(RIGHT_X, HERO_Y1 + 2, Terminal6x8, age > 24 * 3600 ? C_DOWN : age > 8 * 3600 ? C_AMBER : C_LABEL, line);
    }
    cc_Fit_Text(fitted, sizeof(fitted), Terminal6x8, coin.name, ageX - nameX - 4);
    cc_Text(nameX, HERO_Y1 + 2, Terminal6x8, C_TEXT, fitted);

    const int16_t priceY = 23;
    if (!coin.hasQuote){
        cc_Text(HERO_X, priceY, PT_Sans_Narrow38x34, C_LABEL, "--");
        if (coin.unknownToApi) cc_Text_Right(RIGHT_X, priceY + 13, Terminal8x12, C_AMBER, "NOT ON COINCAP");
        return;
    }

    // Price, in the large font unless a very small coin needs too many decimals for it.
    char price[20];
    cc_Format_Price(price, sizeof(price), coin.price);
    cc_Text(HERO_X, priceY + 2, Terminal8x12, C_ACCENT, "$");
    const int16_t priceX = HERO_X + 9;
    const uint8_t* priceFont = PT_Sans_Narrow38x34;
    int16_t priceRow = priceY;
    if (cc_Grouped_Width(priceFont, price) > 150){
        priceFont = Terminal10x17;
        priceRow = priceY + 8;
    }
    const int16_t priceEnd = (priceFont == PT_Sans_Narrow38x34) ? cc_Grouped_Text(priceX, priceRow, priceFont, C_TEXT, price) : cc_Text(priceX, priceRow, priceFont, C_TEXT, price);

    // 24h change, in the largest font that still fits next to the price.
    char percent[14], change[20];
    const double pastPrice = coin.price / (1 + coin.changePct / 100.0);
    cc_Format_Percent(percent, sizeof(percent), coin.changePct);
    cc_Format_Price(change, sizeof(change), fabs(coin.price - pastPrice));
    const uint16_t trend = cc_Trend_Color(coin.changePct);
    const int16_t available = RIGHT_X - priceEnd - 3;
    const int16_t triangleW = 11;
    const uint8_t* font = Terminal10x16;
    if (cc_Text_Width(font, change) + triangleW > available || cc_Text_Width(font, percent) > available) font = Terminal8x12;
    if (cc_Text_Width(font, change) + triangleW > available || cc_Text_Width(font, percent) > available) font = Terminal6x8;
    const int16_t height = font[6];
    cc_Text_Right(RIGHT_X, priceY + 1, font, trend, percent);
    const int16_t changeY = priceY + 33 - height;
    const int16_t changeX = cc_Text_Right(RIGHT_X, changeY, font, trend, change);
    cc_Draw_Triangle(changeX - triangleW, changeY + height / 2 - 3, 5, coin.changePct >= 0, trend);
}

//************************************************************************************ */
// Page 0: the last seven days against the price they started from. A dotted column marks each day.
static void cc_Draw_Chart(const Cc_Coin_t& coin, uint32_t nowUnix, int16_t x1, int16_t x2){
    const int16_t y1 = PAGE_Y1 + 1, y2 = PAGE_Y2 - 1;
    const uint32_t startTime = nowUnix - CHART_SEC;
    double low = 0, high = 0;
    if (!cc_Chart_Range(coin, nowUnix, low, high)) return;
    double base = cc_Price_At(coin, startTime);
    if (isnan(base)) base = coin.history[0].price;
    if (base < low) low = base;
    if (base > high) high = base;
    double margin = (high - low) * 0.06;
    if (margin <= 0) margin = high * 0.001;
    low -= margin;
    high += margin;
    const auto rowOf = [&](double value){ return (int16_t)(y2 - lround((value - low) / (high - low) * (y2 - y1))); };
    const int16_t baseY = rowOf(base);

    int16_t previousY = -1, lastX = -1, lastY = -1;
    int next = 0;
    for (int16_t x = x1; x <= x2; x++){
        const uint32_t when = startTime + (uint32_t)((uint64_t)CHART_SEC * (x - x1) / (x2 - x1));
        while (next < coin.historyCount && coin.history[next].time < when) next++;
        double value;
        if (next >= coin.historyCount){
            // After the newest chart point the line runs on to the current price.
            if (coin.historyCount == 0 || !coin.hasQuote || when > coin.quoteTime) continue;
            const Cc_Point_t& last = coin.history[coin.historyCount - 1];
            const double ratio = (coin.quoteTime > last.time) ? (double)(when - last.time) / (double)(coin.quoteTime - last.time) : 1.0;
            value = last.price + (coin.price - last.price) * ratio;
        } else if (next == 0){
            if (coin.history[0].time > when + 21600) continue;
            value = coin.history[0].price;
        } else {
            const Cc_Point_t& a = coin.history[next - 1];
            const Cc_Point_t& b = coin.history[next];
            value = a.price + (b.price - a.price) * ((double)(when - a.time) / (double)(b.time - a.time));
        }
        const int16_t y = rowOf(value);
        const bool above = value >= base;
        mtb_Panel_Draw_VLine(x, y, baseY, above ? C_UP_DIM : C_DOWN_DIM);
        mtb_Panel_Draw_VLine(x, previousY < 0 ? y : previousY, y, above ? C_UP : C_DOWN);
        previousY = y;
        lastX = x;
        lastY = y;
    }
    for (int day = 1; day < 7; day++){
        const int16_t x = x1 + (x2 - x1) * day / 7;
        for (int16_t y = y1; y <= y2; y += 4) mtb_Panel_Draw_PixelRGB565(x, y, C_LABEL);
    }
    for (int16_t x = x1; x <= x2; x += 4) mtb_Panel_Draw_PixelRGB565(x, baseY, C_LABEL);
    if (lastX >= 0) mtb_Panel_Fill_Circle(lastX, lastY, 1, C_TEXT);
}

static void cc_Draw_Page_Chart(const Cc_Coin_t& coin, uint32_t nowUnix){
    double low = NAN, high = NAN;
    if (!cc_Chart_Range(coin, nowUnix, low, high)){
        cc_Text_Centre(PANEL_RES_X / 2, PAGE_Y1 + 14, Terminal8x12, C_LABEL, "7-DAY CHART");
        cc_Text_Centre(PANEL_RES_X / 2, PAGE_Y1 + 30, Terminal6x8, C_LABEL, "Loads when API credits allow");
        return;
    }
    const int16_t statsX = 205;
    cc_Draw_Chart(coin, nowUnix, 0, statsX - 5);
    char value[20];
    const double change = cc_Return_Since(coin, nowUnix, CHART_SEC);
    struct { const char* label; double number; uint16_t color; } rows[3] = {
        {"7D HIGH", high, C_TEXT}, {"7D LOW", low, C_TEXT}, {"7D CHG", change, isnan(change) ? C_LABEL : cc_Trend_Color(change)}};
    for (int i = 0; i < 3; i++){
        const int16_t y = PAGE_Y1 + 1 + i * 16;
        if (i == 2) cc_Format_Percent(value, sizeof(value), rows[i].number);
        else cc_Format_Price(value, sizeof(value), rows[i].number);
        cc_Text(statsX, y, Terminal4x6, C_LABEL, rows[i].label);
        cc_Text(statsX, y + 7, cc_Text_Width(Terminal6x8, value) > RIGHT_X - statsX ? Terminal4x6 : Terminal6x8, rows[i].color, value);
    }
}

//************************************************************************************ */
// Page 1: the size of the market for this coin.
static void cc_Draw_Page_Market(const Cc_Coin_t& coin){
    char cap[14], volume[14], turnover[14], vwap[20], versus[14], rank[10];
    cc_Ui_Format_Big(cap, sizeof(cap), coin.hasQuote ? coin.marketCap : NAN);
    cc_Ui_Format_Big(volume, sizeof(volume), coin.hasQuote ? coin.volume24h : NAN);
    const double turnoverPct = (coin.hasQuote && coin.marketCap > 0) ? coin.volume24h / coin.marketCap * 100 : NAN;
    if (isnan(turnoverPct)) snprintf(turnover, sizeof(turnover), "--");
    else snprintf(turnover, sizeof(turnover), "%.1f%%", turnoverPct);
    cc_Format_Price(vwap, sizeof(vwap), coin.hasQuote ? coin.vwap24h : NAN);
    const double versusVwap = (coin.hasQuote && coin.vwap24h > 0) ? (coin.price - coin.vwap24h) / coin.vwap24h * 100 : NAN;
    cc_Format_Percent(versus, sizeof(versus), versusVwap);
    snprintf(rank, sizeof(rank), coin.rank > 0 ? "#%u" : "--", coin.rank);
    struct { const char* label; const char* value; uint16_t color; } cells[6] = {
        {"MARKET CAP $", cap, C_ACCENT}, {"24H VOLUME $", volume, C_TEXT}, {"VOL / CAP", turnover, C_TEXT},
        {"24H AVG PRICE", vwap, C_TEXT}, {"PRICE VS AVG", versus, isnan(versusVwap) ? C_LABEL : cc_Trend_Color(versusVwap)}, {"RANK", rank, C_TEXT}};

    for (int i = 0; i < 6; i++){
        const int16_t x = 4 + (i % 3) * 85, y = PAGE_Y1 + 1 + (i / 3) * 24;
        cc_Text(x, y, Terminal6x8, C_LABEL, cells[i].label);
        cc_Text(x, y + 9, Terminal8x12, cells[i].color, cells[i].value);
    }
}

//************************************************************************************ */
// Page 2: how much of the coin exists, and what it is worth against Bitcoin.
static void cc_Draw_Page_Supply(const Cc_Coin_t& coin, const Cc_Coin_t* bitcoin){
    const int16_t x1 = 2, x2 = RIGHT_X - 1, barY = PAGE_Y1 + 11;
    const bool capped = coin.hasQuote && coin.maxSupply > 0;
    char text[48], number[14];
    cc_Text(x1, PAGE_Y1, Terminal6x8, C_LABEL, "CIRCULATING");
    cc_Ui_Format_Big(number, sizeof(number), coin.hasQuote ? coin.supply : NAN);
    snprintf(text, sizeof(text), "%s %s", number, coin.symbol);
    cc_Text_Right(RIGHT_X - 1, PAGE_Y1, Terminal6x8, C_TEXT, text);

    mtb_Panel_Fill_Rect(x1, barY, x2, barY + 7, C_LINE);
    if (capped){
        const int16_t fillX = cc_Position(coin.supply, 0, coin.maxSupply, x1, x2);
        mtb_Panel_Fill_Rect(x1, barY, fillX, barY + 7, C_ACCENT);
        cc_Ui_Format_Big(number, sizeof(number), coin.maxSupply);
        snprintf(text, sizeof(text), "%.1f%% OF MAX %s", coin.supply / coin.maxSupply * 100, number);
        cc_Text_Centre(PANEL_RES_X / 2, barY + 10, Terminal6x8, C_TEXT, text);
    } else {
        for (int16_t x = x1; x <= x2; x += 6) mtb_Panel_Draw_VLine(x, barY, barY + 7, C_LABEL);
        cc_Text_Centre(PANEL_RES_X / 2, barY + 10, Terminal6x8, C_LABEL, coin.hasQuote ? "NO SUPPLY CAP" : "--");
    }

    // Worth if every coin that can ever exist were out, and the coin measured in Bitcoin.
    char diluted[14] = "--", inBitcoin[16] = "--", capShare[14] = "--";
    if (capped) cc_Ui_Format_Big(diluted, sizeof(diluted), coin.price * coin.maxSupply);
    const char* bitcoinLabel = "PRICE IN BTC";
    if (coin.hasQuote && coin.price > 0 && strcmp(coin.id, "bitcoin") == 0){
        bitcoinLabel = "SATS PER $1";
        snprintf(inBitcoin, sizeof(inBitcoin), "%.0f", 1e8 / coin.price);
    } else if (coin.hasQuote && bitcoin != nullptr && bitcoin->hasQuote && bitcoin->price > 0){
        snprintf(inBitcoin, sizeof(inBitcoin), "%.6f", coin.price / bitcoin->price);
        if (bitcoin->marketCap > 0) snprintf(capShare, sizeof(capShare), "%.2f%%", coin.marketCap / bitcoin->marketCap * 100);
    }
    struct { const char* label; const char* value; } cells[3] = {{"FULLY DILUTED $", diluted}, {bitcoinLabel, inBitcoin}, {"CAP VS BTC", capShare}};
    for (int i = 0; i < 3; i++){
        const int16_t x = 4 + i * 95, y = PAGE_Y1 + 29;
        cc_Text(x, y, Terminal6x8, C_LABEL, cells[i].label);
        cc_Text(x, y + 8, Terminal8x12, C_TEXT, cells[i].value);
    }
}

//************************************************************************************ */
// Page 3: returns over three horizons, then where the price sits in its 7-day range.
static void cc_Draw_Page_Momentum(const Cc_Coin_t& coin, uint32_t nowUnix){
    const double versusVwap = (coin.hasQuote && coin.vwap24h > 0) ? (coin.price - coin.vwap24h) / coin.vwap24h * 100 : NAN;
    struct { const char* label; double value; float fullScale; } tiles[4] = {
        {"24H", coin.hasQuote ? coin.changePct : NAN, 8}, {"3 DAYS", cc_Return_Since(coin, nowUnix, 3 * DAY_SEC), 15},
        {"7 DAYS", cc_Return_Since(coin, nowUnix, 7 * DAY_SEC), 25}, {"VS 24H AVG", versusVwap, 6}};
    const int16_t tileW = 64, tileH = 23;

    for (int i = 0; i < 4; i++){
        const int16_t x = i * tileW, centre = x + (tileW - 1) / 2;
        const double value = tiles[i].value;
        const bool known = !isnan(value);
        float strength = known ? fabs(value) / tiles[i].fullScale : 0;
        if (strength > 1) strength = 1;
        const uint8_t shade = 28 + (uint8_t)(strength * 92);
        const uint16_t background = !known ? CC_RGB(24, 24, 24) : (value < 0 ? CC_RGB(shade, shade / 8, shade / 10) : CC_RGB(shade / 10, shade, shade / 3));
        mtb_Panel_Fill_Round_Rect(x + 1, PAGE_Y1, x + tileW - 2, PAGE_Y1 + tileH, 2, background);
        cc_Text_Centre(centre, PAGE_Y1 + 2, Terminal6x8, C_TEXT, tiles[i].label);
        char text[14];
        cc_Format_Percent(text, sizeof(text), value);
        cc_Text_Centre(centre, PAGE_Y1 + 11, Terminal8x12, CC_RGB(255, 255, 255), text);
    }

    double low = NAN, high = NAN;
    const int16_t x1 = 2, x2 = RIGHT_X - 1, barY = PAGE_Y1 + 31;
    if (!cc_Chart_Range(coin, nowUnix, low, high) || high <= low){
        cc_Text_Centre(PANEL_RES_X / 2, barY + 3, Terminal6x8, C_LABEL, "7-day range loads with the chart");
        return;
    }
    for (int16_t x = x1; x <= x2; x++){
        // Red at the low, through amber, to green at the high.
        const int ratio = (x - x1) * 255 / (x2 - x1);
        const uint8_t r = ratio < 128 ? 150 : (uint8_t)(150 - (ratio - 128) * 150 / 127);
        const uint8_t g = ratio < 128 ? (uint8_t)(ratio * 150 / 127) : 150;
        mtb_Panel_Draw_VLine(x, barY, barY + 4, CC_RGB(r, g, 0));
    }
    char text[24];
    const int16_t rowY = barY + 8;
    cc_Format_Price(text, sizeof(text), low);
    cc_Text(x1, rowY, Terminal6x8, C_DOWN, text);
    cc_Format_Price(text, sizeof(text), high);
    cc_Text_Right(x2, rowY, Terminal6x8, C_UP, text);
    if (coin.hasQuote){
        const int16_t px = cc_Position(coin.price, low, high, x1 + 3, x2 - 3);
        mtb_Panel_Fill_Triangle(px - 3, barY - 4, px + 3, barY - 4, px, barY - 1, C_TEXT);
        mtb_Panel_Draw_VLine(px, barY, barY + 4, C_TEXT);
        snprintf(text, sizeof(text), "%.0f%% OF 7D RANGE", (coin.price - low) / (high - low) * 100);
        cc_Text_Centre(PANEL_RES_X / 2, rowY, Terminal6x8, C_TEXT, text);
    }
}

//************************************************************************************ */
// Page 4: the user's coins ranked by 24h change on the left, the market's biggest gainers on the right.
static void cc_Draw_Page_Leaders(const Cc_Coin_t& coin, Cc_Coin_t* const* watch, int watchCount, const Cc_Market_t& market){
    const int16_t splitX = 134;
    cc_Text(2, PAGE_Y1, Terminal6x8, C_LABEL, "YOUR COINS 24H");

    const Cc_Coin_t* ranked[100];
    int count = 0;
    for (int i = 0; i < watchCount && count < 100; i++){
        const Cc_Coin_t* candidate = watch[i];
        if (candidate == nullptr || !candidate->hasQuote) continue;
        int at = count++;
        while (at > 0 && ranked[at - 1]->changePct < candidate->changePct){
            ranked[at] = ranked[at - 1];
            at--;
        }
        ranked[at] = candidate;
    }
    // Five rows fit: with more coins than that, the three best and the two worst.
    const int rows = count < 5 ? count : 5;
    float largest = 1;
    for (int i = 0; i < count; i++) if (fabsf(ranked[i]->changePct) > largest) largest = fabsf(ranked[i]->changePct);
    const int16_t barZero = 76, barReach = 19;
    for (int r = 0; r < rows; r++){
        const Cc_Coin_t* row = ranked[(count > 5 && r >= 3) ? count - 5 + r : r];
        const int16_t y = PAGE_Y1 + 9 + r * 8;
        char text[CC_SYMBOL_LEN], percent[12];
        cc_Fit_Text(text, sizeof(text), Terminal6x8, row->symbol, 36);
        cc_Text(2, y, Terminal6x8, row == &coin ? C_ACCENT : C_TEXT, text);
        const int16_t reach = (int16_t)lroundf(fabsf(row->changePct) / largest * barReach);
        const uint16_t trend = cc_Trend_Color(row->changePct);
        if (row->changePct < 0) mtb_Panel_Fill_Rect(barZero - reach, y + 1, barZero, y + 5, trend);
        else mtb_Panel_Fill_Rect(barZero, y + 1, barZero + reach, y + 5, trend);
        snprintf(percent, sizeof(percent), fabsf(row->changePct) < 100 ? "%+.1f" : "%+.0f", row->changePct);
        cc_Text_Right(splitX - 5, y, Terminal6x8, trend, percent);
    }

    mtb_Panel_Draw_VLine(splitX, PAGE_Y1, PAGE_Y2, C_LINE);
    cc_Text(splitX + 4, PAGE_Y1, Terminal6x8, C_LABEL, "TOP 100 GAINERS");
    if (market.gainerCount == 0){
        cc_Text(splitX + 4, PAGE_Y1 + 14, Terminal8x12, C_LABEL, "--");
        return;
    }
    for (int i = 0; i < market.gainerCount; i++){
        const int16_t y = PAGE_Y1 + 10 + i * 13;
        char percent[12];
        char symbol[CC_SYMBOL_LEN];
        cc_Fit_Text(symbol, sizeof(symbol), Terminal8x12, market.gainers[i].symbol, 62);
        cc_Text(splitX + 4, y, Terminal8x12, C_TEXT, symbol);
        snprintf(percent, sizeof(percent), fabsf(market.gainers[i].changePct) < 100 ? "%+.1f%%" : "%+.0f%%", market.gainers[i].changePct);
        cc_Text_Right(RIGHT_X, y, Terminal8x12, cc_Trend_Color(market.gainers[i].changePct), percent);
    }
}

//************************************************************************************ */
void cc_Ui_Draw_Page(uint8_t page, const Cc_Coin_t& coin, Cc_Coin_t* const* watch, int watchCount, const Cc_Coin_t* bitcoin, const Cc_Market_t& market, uint32_t nowUnix, bool autoCycleHeld){
    mtb_Panel_Fill_Rect(0, PAGE_Y1 - 1, PANEL_RES_X - 1, PAGE_Y2, BLACK);
    mtb_Panel_Draw_HLine(HERO_RULE_Y, 0, PANEL_RES_X - 1, C_LINE);
    switch (page){
        case 0: cc_Draw_Page_Chart(coin, nowUnix); break;
        case 1: cc_Draw_Page_Market(coin); break;
        case 2: cc_Draw_Page_Supply(coin, bitcoin); break;
        case 3: cc_Draw_Page_Momentum(coin, nowUnix); break;
        default: cc_Draw_Page_Leaders(coin, watch, watchCount, market); break;
    }
    // Page indicator, set into the rule above the panel. Amber: auto-cycling through the coins is on hold.
    for (int i = 0; i < CC_PAGE_COUNT; i++){
        const int16_t x = RIGHT_X - (CC_PAGE_COUNT - 1 - i) * 7 - 4;
        mtb_Panel_Fill_Rect(x, HERO_RULE_Y, x + 4, HERO_RULE_Y + 1, (i == page) ? (autoCycleHeld ? C_AMBER : C_ACCENT) : CC_RGB(88, 88, 88));
    }
}

void cc_Ui_Draw_Watchlist(Cc_Coin_t* const* watch, int count, int selected){
    mtb_Panel_Fill_Rect(0, WATCH_Y1, PANEL_RES_X - 1, WATCH_Y2, BLACK);
    if (count <= 0) return;

    const int visible = (count < WATCH_VISIBLE) ? count : WATCH_VISIBLE;
    const int16_t cellW = PANEL_RES_X / WATCH_VISIBLE;
    int start = 0;
    if (count > WATCH_VISIBLE && selected >= 0) start = (selected - WATCH_VISIBLE / 2 + count) % count;

    for (int i = 0; i < visible; i++){
        const int index = (start + i) % count;
        const Cc_Coin_t* coin = watch[index];
        if (coin == nullptr) continue;
        const int16_t x = i * cellW;
        if (index == selected) mtb_Panel_Fill_Rect(x, WATCH_Y1, x + cellW - 2, WATCH_Y2, C_SELECTED);
        char ticker[CC_SYMBOL_LEN];
        cc_Fit_Text(ticker, sizeof(ticker), Terminal6x8, coin->symbol, 28);
        const int16_t textEnd = cc_Text(x + 2, WATCH_Y1 + 1, Terminal6x8, (index == selected) ? CC_RGB(255, 255, 255) : CC_RGB(170, 170, 170), ticker);
        if (!coin->hasQuote) continue;
        const uint16_t trend = cc_Trend_Color(coin->changePct);
        char percent[8];
        const int16_t cellEnd = x + cellW - 3;
        snprintf(percent, sizeof(percent), fabsf(coin->changePct) < 10 ? "%.1f" : "%.0f", fabsf(coin->changePct));
        // A long ticker gives up the decimal rather than the arrow.
        if (cellEnd - cc_Text_Width(Terminal6x8, percent) + 1 - textEnd < 7) snprintf(percent, sizeof(percent), "%.0f", fabsf(coin->changePct));
        const int16_t percentX = cc_Text_Right(cellEnd, WATCH_Y1 + 1, Terminal6x8, trend, percent);
        cc_Draw_Triangle(percentX - 6, WATCH_Y1 + 3, 3, coin->changePct >= 0, trend);
    }
}
