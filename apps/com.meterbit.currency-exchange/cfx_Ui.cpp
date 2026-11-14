#include "cfx_Ui.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint16_t K_BAND = MTB_KIT_RGB(0, 50, 60);
static const uint16_t K_UP = MTB_KIT_RGB(40, 210, 110);
static const uint16_t K_DOWN = MTB_KIT_RGB(245, 70, 60);
static const uint16_t K_FILL = MTB_KIT_RGB(0, 40, 50);
static const int CFX_BOARD_ROWS = 8;

uint8_t cfx_Board_Pages(uint8_t count){ return count == 0 ? 1 : (uint8_t)((count + CFX_BOARD_ROWS - 1) / CFX_BOARD_ROWS); }

static void cfx_Band(const char* left, const char* right){
    mtb_Panel_Fill_Rect(0, 10, 255, 20, K_BAND);
    mtb_Kit_Text(3, 12, Terminal6x8, KIT_TEXT, left, K_BAND);
    if (right && right[0]) mtb_Kit_Text_Right(252, 13, Terminal4x6, KIT_CYAN, right, K_BAND);
}

static void cfx_Footer(const Cfx_View_t& v){
    char text[64];
    if (v.settings->apiKey[0] == '\0') snprintf(text, sizeof(text), "RATES BY EXCHANGE RATE API");
    else if (v.data->lastUpdate){
        const time_t t = (time_t)v.data->lastUpdate;
        struct tm tm;
        gmtime_r(&t, &tm);
        char when[24];
        strftime(when, sizeof(when), "%d %b %H:%M UTC", &tm);
        snprintf(text, sizeof(text), "RATES OF %s", when);
        mtb_Kit_Upper(text);
    } else text[0] = '\0';
    mtb_Kit_Text(3, 121, Terminal4x6, KIT_DIM, text);
    if (v.data->nextUpdate > v.now){
        const uint32_t s = v.data->nextUpdate - v.now;
        char next[24];
        if (s >= 3600) snprintf(next, sizeof(next), "NEXT IN %u H", (unsigned)((s + 1800) / 3600));
        else snprintf(next, sizeof(next), "NEXT IN %u MIN", (unsigned)((s + 59) / 60));
        mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_DIM, next);
    }
}

// Rate of target per one base, or the inverse; 0 if unknown.
static double cfx_Shown_Rate(const Cfx_View_t& v, const char* code){
    const double rate = cfx_Rate(*v.data, code);
    if (!(rate > 0)) return 0;
    return v.settings->inverse ? 1.0 / rate : rate;
}

// Per cent change over @p days of a watched code's rate (as shown: inverse too). False if not known.
static bool cfx_Change(const Cfx_View_t& v, const char* code, int days, double& percent){
    const Cfx_History_t* h = cfx_History_Of(*v.data, code);
    if (h == nullptr || h->count < 2) return false;
    const uint32_t newest = h->day[h->count - 1];
    const float then = cfx_Rate_Days_Ago(*h, newest, days);
    const float now = h->rate[h->count - 1];
    if (!(then > 0) || !(now > 0)) return false;
    percent = v.settings->inverse ? (then / now - 1) * 100 : (now / then - 1) * 100;
    return true;
}

//##############################################################################################################
static void cfx_Draw_Pair(const Cfx_View_t& v){
    const Cfx_Settings_t& s = *v.settings;
    const char* target = s.watch[v.featured % (s.watchCount ? s.watchCount : 1)];
    const char* from = s.inverse ? target : s.base;
    const char* to = s.inverse ? s.base : target;
    char band[40];
    snprintf(band, sizeof(band), "%s / %s", from, to);
    char right[16];
    snprintf(right, sizeof(right), "%u OF %u", (unsigned)(v.featured + 1), (unsigned)s.watchCount);
    cfx_Band(band, right);

    // "1 US DOLLAR =", then the rate in big digits and the code.
    char line[48];
    snprintf(line, sizeof(line), "1 %s =", cfx_Name(from));
    char fitted[48];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, line, 248);
    mtb_Kit_Text(4, 24, Terminal6x8, KIT_LABEL, fitted);
    const double rate = cfx_Shown_Rate(v, target);
    char text[24];
    if (rate >= 10000){ cfx_Format_Rate(text, sizeof(text), floor(rate + 0.5)); char* dot = strchr(text, '.'); if (dot) *dot = '\0'; }
    else cfx_Format_Rate(text, sizeof(text), rate);
    int scale = 2;
    if (mtb_Kit_Big_Text_Width(Terminal10x17, 2, text) > 205) scale = 1;
    const int16_t width = mtb_Kit_Big_Text_Width(Terminal10x17, scale, text);
    mtb_Kit_Big_Text(4, scale == 2 ? 34 : 46, Terminal10x17, scale, KIT_TEXT, text);
    mtb_Kit_Text(4 + width + 6, 54, Terminal8x12, KIT_CYAN, to);
    char name[32];
    mtb_Kit_Fit(name, sizeof(name), Terminal4x6, cfx_Name(to), 251 - (4 + width + 6));
    if (strcmp(name, to) != 0) mtb_Kit_Text(4 + width + 6, 46, Terminal4x6, KIT_DIM, name);

    // The changes.
    static const int DAYS[3] = {1, 7, 30};
    static const char* const LABELS[3] = {"1D", "7D", "30D"};
    int16_t x = 4;
    bool any = false;
    for (int i = 0; i < 3; i++){
        double percent;
        if (!cfx_Change(v, target, DAYS[i], percent)) continue;
        any = true;
        char change[16];
        cfx_Format_Change(change, sizeof(change), percent);
        const uint16_t color = percent > 0.004 ? K_UP : percent < -0.004 ? K_DOWN : KIT_LABEL;
        if (fabs(percent) >= 0.005) mtb_Kit_Arrow(x + 3, 77, 6, percent > 0, color);
        x = mtb_Kit_Text(x + 9, 74, Terminal6x8, color, change) + 3;
        x = mtb_Kit_Text(x, 75, Terminal4x6, KIT_DIM, LABELS[i]) + 12;
    }
    if (!any) mtb_Kit_Text(4, 75, Terminal4x6, KIT_DIM, "CHANGES SHOW FROM TOMORROW: PIXLPAL KEEPS A RATE A DAY");

    // A month of daily rates.
    const Cfx_History_t* h = cfx_History_Of(*v.data, target);
    if (h && h->count >= 2){
        float values[CFX_HISTORY_DAYS];
        for (int i = 0; i < h->count; i++) values[i] = v.settings->inverse ? 1.0f / h->rate[i] : h->rate[i];
        const bool up = values[h->count - 1] >= values[0];
        mtb_Kit_Sparkline(4, 86, 248, 30, values, h->count, up ? K_UP : K_DOWN, K_FILL);
        char days[16];
        snprintf(days, sizeof(days), "%u DAYS", (unsigned)(h->day[h->count - 1] - h->day[0] + 1));
        mtb_Kit_Text_Right(252, 75, Terminal4x6, KIT_DIM, days);
    } else {
        mtb_Panel_Draw_HLine(116, 4, 251, KIT_LINE);
    }
    cfx_Footer(v);
}

static void cfx_Draw_Board(const Cfx_View_t& v){
    const Cfx_Settings_t& s = *v.settings;
    char band[32], right[24];
    snprintf(band, sizeof(band), s.inverse ? "VALUE IN %s" : "1 %s BUYS", s.base);
    const uint8_t pages = cfx_Board_Pages(s.watchCount);
    if (pages > 1) snprintf(right, sizeof(right), "PAGE %u/%u", (unsigned)(v.boardPage + 1), (unsigned)pages);
    else snprintf(right, sizeof(right), "1 DAY");
    cfx_Band(band, right);
    const int first = v.boardPage * CFX_BOARD_ROWS;
    for (int r = 0; r < CFX_BOARD_ROWS && first + r < s.watchCount; r++){
        const char* code = s.watch[first + r];
        const int16_t y = 23 + r * 12;
        if (r % 2) mtb_Panel_Fill_Rect(0, y - 1, 255, y + 10, MTB_KIT_RGB(12, 12, 16));
        const uint16_t background = (r % 2) ? MTB_KIT_RGB(12, 12, 16) : BLACK;
        mtb_Kit_Text(4, y + 1, Terminal6x8, KIT_CYAN, code, background);
        char name[24];
        mtb_Kit_Fit(name, sizeof(name), Terminal4x6, cfx_Name(code), 92);
        if (strcmp(name, code) != 0) mtb_Kit_Text(30, y + 2, Terminal4x6, KIT_LABEL, name, background);
        char rate[24];
        cfx_Format_Rate(rate, sizeof(rate), cfx_Shown_Rate(v, code));
        mtb_Kit_Text_Right(203, y + 1, Terminal6x8, KIT_TEXT, rate, background);
        double percent;
        if (cfx_Change(v, code, 1, percent)){
            char change[16];
            cfx_Format_Change(change, sizeof(change), percent);
            mtb_Kit_Text_Right(252, y + 1, Terminal6x8, percent > 0.004 ? K_UP : percent < -0.004 ? K_DOWN : KIT_LABEL, change, background);
        }
    }
    cfx_Footer(v);
}

static void cfx_Draw_Convert(const Cfx_View_t& v){
    const Cfx_Settings_t& s = *v.settings;
    cfx_Band("CONVERTER", s.base);
    char amount[32];
    cfx_Format_Rate(amount, sizeof(amount), s.amount);
    if (s.amount >= 1 && s.amount == floor(s.amount)){ char* dot = strchr(amount, '.'); if (dot) *dot = '\0'; }
    char line[48];
    snprintf(line, sizeof(line), "%s %s", amount, s.base);
    mtb_Kit_Text_Centre(128, 23, Terminal10x17, KIT_GOLD, line);
    const int shown = s.watchCount < 8 ? s.watchCount : 8;
    for (int i = 0; i < shown; i++){
        const int16_t x = (i % 2) ? 134 : 2, y = 44 + (i / 2) * 18;
        const char* code = s.watch[i];
        mtb_Kit_Text(x, y + 2, Terminal6x8, KIT_CYAN, code);
        const double rate = cfx_Rate(*v.data, code);
        char value[24];
        cfx_Format_Rate(value, sizeof(value), rate > 0 ? s.amount * rate : 0);
        char fitted[24];
        // Long amounts in the smaller font rather than cut.
        const uint8_t* font = mtb_Kit_Text_Width(Terminal8x12, value) <= 92 ? Terminal8x12 : Terminal6x8;
        mtb_Kit_Fit(fitted, sizeof(fitted), font, value, 92);
        mtb_Kit_Text_Right(x + 119, font == Terminal8x12 ? y : y + 2, font, KIT_TEXT, fitted);
    }
    cfx_Footer(v);
}

static void cfx_Draw_Plan(const Cfx_View_t& v){
    const Cfx_Data_t& d = *v.data;
    cfx_Band("YOUR PLAN", "EXCHANGERATE-API.COM");
    char text[64];
    const bool keyless = v.settings->apiKey[0] == '\0';
    char planName[20];
    snprintf(planName, sizeof(planName), "%s", keyless ? "NO KEY" : cfx_Plan_Name(d.plan));
    mtb_Kit_Upper(planName);
    mtb_Kit_Text(4, 24, Terminal10x17, KIT_GOLD, planName);
    // How often the provider updates this plan's rates.
    if (d.nextUpdate > d.lastUpdate && d.lastUpdate){
        const uint32_t every = d.nextUpdate - d.lastUpdate;
        if (every >= 20UL * 3600) snprintf(text, sizeof(text), "RATES UPDATE DAILY");
        else if (every >= 50UL * 60) snprintf(text, sizeof(text), "RATES UPDATE HOURLY");
        else snprintf(text, sizeof(text), "RATES UPDATE EVERY %u MIN", (unsigned)((every + 30) / 60));
        mtb_Kit_Text_Right(252, 28, Terminal6x8, KIT_TEXT, text);
    }
    int16_t y = 46;
    if (!keyless && d.quota.seen && d.quota.planQuota){
        mtb_Kit_Bar(4, y, 248, 6, (float)d.quota.remaining / d.quota.planQuota, d.quota.remaining * 10 < d.quota.planQuota ? KIT_ORANGE : KIT_GREEN);
        snprintf(text, sizeof(text), "%u OF %u REQUESTS LEFT, RENEWS ON DAY %u", (unsigned)d.quota.remaining, (unsigned)d.quota.planQuota, (unsigned)d.quota.refreshDay);
        mtb_Kit_Text(4, y + 9, Terminal4x6, KIT_LABEL, text);
        y += 20;
    }
    struct Feature_t { const char* name; bool on; };
    const bool paid = d.plan >= CFX_PLAN_PRO;
    const Feature_t features[] = {
        {"DAILY RATES, 160+ CURRENCIES", true},
        {"HOURLY RATES", paid},
        {"RATES EVERY 5 MINUTES", d.plan >= CFX_PLAN_BUSINESS},
        {"7- AND 30-DAY CHANGES AT ONCE (HISTORY)", !keyless && !d.historyLocked && paid},
    };
    for (const Feature_t& f : features){
        mtb_Panel_Fill_Rect(6, y + 1, 9, y + 4, f.on ? KIT_GREEN : KIT_DIM);
        mtb_Kit_Text(14, y, Terminal6x8, f.on ? KIT_TEXT : KIT_DIM, f.name);
        y += 11;
    }
    const char* hint = keyless ? "A FREE KEY FROM EXCHANGERATE-API.COM: 1,500 REQUESTS A MONTH, NO ATTRIBUTION"
                     : d.plan == CFX_PLAN_FREE ? "PRO ($10/MONTH) ADDS HOURLY RATES AND HISTORY: SAME KEY"
                     : "";
    if (hint[0]){
        char lines[2][80];
        const int n = mtb_Kit_Wrap(hint, Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 2);
        for (int i = 0; i < n; i++) mtb_Kit_Text(4, y + 2 + i * 7, Terminal4x6, KIT_CYAN, lines[i]);
    }
    cfx_Footer(v);
}

void cfx_Draw(const Cfx_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.message){
        cfx_Band("CURRENCY EXCHANGE", "");
        mtb_Kit_Text_Centre(128, 42, Terminal10x17, KIT_GOLD, v.message);
        if (v.detail){
            char lines[3][64];
            const int n = mtb_Kit_Wrap(v.detail, Terminal6x8, 236, &lines[0][0], sizeof(lines[0]), 3);
            for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 70 + i * 11, Terminal6x8, KIT_LABEL, lines[i]);
        }
        return;
    }
    switch (v.page){
        case CFX_PAGE_BOARD: cfx_Draw_Board(v); break;
        case CFX_PAGE_CONVERT: cfx_Draw_Convert(v); break;
        case CFX_PAGE_PLAN: cfx_Draw_Plan(v); break;
        default: cfx_Draw_Pair(v); break;
    }
}
