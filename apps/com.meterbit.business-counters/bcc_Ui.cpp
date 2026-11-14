// Business & Creator Counters' screens.
#include "bcc_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static const uint16_t K_MONEY = MTB_KIT_RGB(70, 230, 120);
static const uint16_t K_YT = MTB_KIT_RGB(255, 40, 40);
static const uint16_t K_PL = MTB_KIT_RGB(90, 160, 255);
static const uint16_t SOURCE_COLORS[BCC_SOURCES] = {MTB_KIT_RGB(120, 110, 255), MTB_KIT_RGB(0, 195, 247), MTB_KIT_RGB(150, 191, 72),
                                                    K_YT, KIT_GOLD, K_PL};

static void text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back = BLACK){
    char a[96], b[96];
    mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    mtb_Kit_Text(x, y, font, color, b, back);
}

static void centre_Fit(int16_t cx, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back = BLACK){
    char a[96], b[96];
    mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    mtb_Kit_Text_Centre(cx, y, font, color, b, back);
}

static void header(const char* left, uint16_t lc, const char* right, uint16_t rc){
    char a[64], b[64];
    int16_t rw = 0;
    if (right && right[0]){
        mtb_Kit_To_Ascii(a, sizeof(a), right);
        mtb_Kit_Upper(a);
        rw = mtb_Kit_Text_Width(Terminal4x6, a);
        mtb_Kit_Text_Right(253, 12, Terminal4x6, rc, a, BLACK);
    }
    mtb_Kit_To_Ascii(a, sizeof(a), left);
    mtb_Kit_Upper(a);
    mtb_Kit_Fit(b, sizeof(b), Terminal4x6, a, 246 - rw);
    mtb_Kit_Text(3, 12, Terminal4x6, lc, b, BLACK);
    mtb_Panel_Draw_HLine(19, 0, 255, KIT_LINE);
}

// The largest of a few sizes that fits @p width, centred.
static void big_Centre(int16_t cx, int16_t y, int16_t width, uint16_t color, const char* text){
    struct { const uint8_t* font; int scale; int16_t dy; } SIZES[] = {{Terminal10x16, 2, 0}, {Terminal8x12, 2, 4}, {Terminal10x16, 1, 8}, {Terminal8x12, 1, 10}};
    for (const auto& z : SIZES){
        const int16_t w = mtb_Kit_Big_Text_Width(z.font, z.scale, text);
        if (w <= width){ mtb_Kit_Big_Text(cx - w / 2, y + z.dy, z.font, z.scale, color, text); return; }
    }
    centre_Fit(cx, y + 10, Terminal6x8, color, text, width);
}

static const char* state_Text(uint8_t state){
    switch (state){
        case BCC_WAIT: return "CONNECTING";
        case BCC_KEY: return "CHECK KEY";
        case BCC_LIMIT: return "WAITING (LIMIT)";
        case BCC_PLAN: return "PLAN NEEDED";
        case BCC_ERROR: return "NO ANSWER";
        default: return "";
    }
}

// Sales ---------------------------------------------------------------------------------------------------------------
static void sales(const Bcc_View_t& v){
    const Bcc_Settings_t& s = *v.settings;
    const Bcc_Total_t& t = *v.total;
    bool test = false;
    for (int i = 0; i < BCC_SHOPS; i++) if (bcc_Source_Ready(s, (uint8_t)i) && v.status->shops[i].test) test = true;
    const time_t tt = (time_t)v.now;
    struct tm lt;
    localtime_r(&tt, &lt);
    char date[24];
    strftime(date, sizeof(date), "%a %d %b", &lt);
    header(v.muted ? "Today's sales (sound off)" : "Today's sales", KIT_GOLD, test ? "test mode" : date, test ? KIT_ORANGE : KIT_LABEL);

    char big[32], line[64];
    const long long shownCount = llround(v.shownCount);
    if (s.showAmounts && t.main >= 0){
        bcc_Format_Money(big, sizeof(big), t.money[t.main].code, (int64_t)llround(v.shownMain), false);
        if (mtb_Kit_Big_Text_Width(Terminal8x12, 2, big) > 248) bcc_Format_Money(big, sizeof(big), t.money[t.main].code, (int64_t)llround(v.shownMain), true);
        big_Centre(128, 23, 250, K_MONEY, big);
        char avg[24] = "";
        if (t.count && t.moneyCount == 1){
            bcc_Format_Money(avg, sizeof(avg), t.money[t.main].code, t.money[t.main].minor / (int64_t)t.count, true);
            snprintf(line, sizeof(line), "%lld %s   AVERAGE %s", shownCount, shownCount == 1 ? "SALE" : "SALES", avg);
        } else snprintf(line, sizeof(line), "%lld %s", shownCount, shownCount == 1 ? "SALE" : "SALES");
        centre_Fit(128, 59, Terminal6x8, KIT_TEXT, line, 250);
    } else {
        bcc_Format_Count(big, sizeof(big), shownCount, false);
        big_Centre(128, 23, 250, K_MONEY, big);
        mtb_Kit_Text_Centre(128, 59, Terminal6x8, KIT_TEXT, shownCount == 1 ? "SALE TODAY" : "SALES TODAY", BLACK);
    }

    // The goal, or the best day.
    if (s.showAmounts && t.goalFraction >= 0 && t.main >= 0){
        const bool reached = t.goalFraction >= 1;
        mtb_Kit_Bar(8, 71, 240, 6, t.goalFraction > 1 ? 1 : t.goalFraction, reached ? KIT_GREEN : KIT_GOLD);
        char goal[24];
        int64_t scale = 1;
        for (uint8_t i = 0; i < bcc_Decimals(t.money[t.main].code); i++) scale *= 10;
        bcc_Format_Money(goal, sizeof(goal), t.money[t.main].code, (int64_t)s.goal * scale, true);
        if (reached) snprintf(line, sizeof(line), "GOAL REACHED: %d%% OF %s", (int)(t.goalFraction * 100 + 0.5f), goal);
        else snprintf(line, sizeof(line), "%d%% OF TODAY'S %s GOAL", (int)(t.goalFraction * 100), goal);
        centre_Fit(128, 80, Terminal4x6, reached ? KIT_GREEN : KIT_LABEL, line, 250);
    } else if (s.showAmounts && v.log->bestDay && v.log->best.minor > 0){
        char best[24];
        bcc_Format_Money(best, sizeof(best), v.log->best.code, v.log->best.minor, true);
        if (v.log->bestDay == v.today) snprintf(line, sizeof(line), "YOUR BEST DAY SO FAR: %s", best);
        else snprintf(line, sizeof(line), "BEST DAY: %s", best);
        centre_Fit(128, 75, Terminal4x6, v.log->bestDay == v.today ? KIT_GREEN : KIT_DIM, line, 250);
    }

    // Each shop.
    int row = 0;
    for (int i = 0; i < BCC_SHOPS && row < 3; i++){
        if (!s.on[i]) continue;
        const Bcc_Sales_t& shop = v.status->shops[i];
        const int16_t y = 90 + row * 11;
        mtb_Panel_Fill_Circle(6, y + 3, 2, SOURCE_COLORS[i]);
        char name[16];
        snprintf(name, sizeof(name), "%s", bcc_Source_Name((uint8_t)i));
        mtb_Kit_Upper(name);
        mtb_Kit_Text(12, y, Terminal6x8, KIT_LABEL, name, BLACK);
        if (!bcc_Source_Ready(s, (uint8_t)i)){ mtb_Kit_Text_Right(252, y + 1, Terminal4x6, KIT_DIM, "SET UP IN THE PHONE APP", BLACK); row++; continue; }
        if (shop.updated && shop.day == v.today){
            char text[72], money[28];
            size_t k = (size_t)snprintf(text, sizeof(text), "%u%s", shop.count, shop.more ? "+" : "");
            if (s.showAmounts){
                for (uint8_t m = 0; m < shop.moneyCount && k < sizeof(text); m++){
                    bcc_Format_Money(money, sizeof(money), shop.money[m].code, shop.money[m].minor, true);
                    k += (size_t)snprintf(text + k, sizeof(text) - k, "   %s", money);
                }
            }
            text_Fit(72, y, Terminal6x8, KIT_TEXT, text, shop.state == BCC_OK ? 180 : 110);
        }
        if (shop.state != BCC_OK) mtb_Kit_Text_Right(252, y + 1, Terminal4x6, shop.state == BCC_WAIT ? KIT_DIM : KIT_RED, state_Text(shop.state), BLACK);
        row++;
    }
}

// Creators -------------------------------------------------------------------------------------------------------------
static void star(int16_t cx, int16_t cy, int16_t r, uint16_t c){
    int16_t x[10], y[10];
    for (int i = 0; i < 10; i++){
        const float a = -1.5707963f + i * 0.6283185f;
        const float rr = i % 2 ? r * 0.42f : (float)r;
        x[i] = (int16_t)lroundf(cx + rr * cosf(a));
        y[i] = (int16_t)lroundf(cy + rr * sinf(a));
    }
    for (int i = 0; i < 10; i++) mtb_Panel_Fill_Triangle(cx, cy, x[i], y[i], x[(i + 1) % 10], y[(i + 1) % 10], c);
}

static void play_Icon(int16_t cx, int16_t cy){
    mtb_Panel_Fill_Rect(cx - 9, cy - 6, cx + 9, cy + 6, K_YT);
    mtb_Panel_Fill_Triangle(cx - 3, cy - 4, cx - 3, cy + 4, cx + 4, cy, MTB_KIT_RGB(255, 255, 255));
}

static void live_Icon(int16_t cx, int16_t cy, uint32_t now){
    mtb_Panel_Fill_Circle(cx, cy, 4, KIT_GREEN);
    if (now % 2) mtb_Panel_Draw_Circle(cx, cy, 7, mtb_Kit_Dim(KIT_GREEN, 110));
}

// The whole number if it fits the column large, else the short form ("10.2K").
static void fit_Count(char* dest, size_t size, long long value, int16_t w){
    bcc_Format_Count(dest, size, value, false);
    if (mtb_Kit_Big_Text_Width(Terminal8x12, 2, dest) > w - 6) bcc_Format_Count(dest, size, value, true);
}

static void column(int16_t x, int16_t w, uint8_t src, const Bcc_View_t& v, int columns){
    const Bcc_Creator_t& c = v.status->creator;
    const int16_t cx = x + w / 2;
    if (src == BCC_YOUTUBE) play_Icon(cx, 30);
    else if (src == BCC_GITHUB) star(cx, 30, 8, KIT_GOLD);
    else live_Icon(cx, 30, v.now);
    char big[24] = "", label[24] = "", detail[64] = "", a[24], b[24];
    const bool known = c.updated[src] != 0;
    (void)columns;
    if (src == BCC_YOUTUBE){
        if (c.subsHidden) snprintf(big, sizeof(big), "HIDDEN");
        else fit_Count(big, sizeof(big), llround(v.shownSubs), w);
        snprintf(label, sizeof(label), "SUBSCRIBERS");
        bcc_Format_Count(a, sizeof(a), c.views, true);
        snprintf(detail, sizeof(detail), "%s VIEWS  %u VIDEOS", a, (unsigned)c.videos);
    } else if (src == BCC_GITHUB){
        const bool repos = c.repoCount > 0;
        fit_Count(big, sizeof(big), llround(repos ? v.shownStars : (double)c.followers), w);
        snprintf(label, sizeof(label), repos ? "STARS" : "FOLLOWERS");
        if (repos && v.settings->ghUser[0]){ bcc_Format_Count(a, sizeof(a), c.followers, true); snprintf(detail, sizeof(detail), "%s FOLLOWERS", a); }
        else if (c.repoCount == 1) snprintf(detail, sizeof(detail), "%s", c.repoName[0]);
        else if (repos) snprintf(detail, sizeof(detail), "ON %u REPOS", c.repoCount);
    } else {
        fit_Count(big, sizeof(big), c.live < 0 ? 0 : llround(v.shownLive), w);
        snprintf(label, sizeof(label), "VISITORS NOW");
        if (c.visitors >= 0){
            bcc_Format_Count(a, sizeof(a), c.visitors, true);
            bcc_Format_Count(b, sizeof(b), c.pageviews, true);
            snprintf(detail, sizeof(detail), "%s TODAY  %s VIEWS", a, b);
        }
    }
    if (known) big_Centre(cx, 42, w - 6, KIT_TEXT, big);
    else mtb_Kit_Text_Centre(cx, 52, Terminal6x8, KIT_DIM, c.state[src] == BCC_WAIT ? "..." : "-", BLACK);
    mtb_Kit_Text_Centre(cx, 80, Terminal4x6, SOURCE_COLORS[src], label, BLACK);
    if (c.state[src] != BCC_OK && c.state[src] != BCC_OFF) centre_Fit(cx, 92, Terminal4x6, c.state[src] == BCC_WAIT ? KIT_DIM : KIT_RED, state_Text(c.state[src]), w - 4);
    else if (detail[0]){
        char up[64];
        mtb_Kit_To_Ascii(up, sizeof(up), detail);
        mtb_Kit_Upper(up);
        if (mtb_Kit_Text_Width(Terminal4x6, up) <= w - 4) mtb_Kit_Text_Centre(cx, 92, Terminal4x6, KIT_LABEL, up, BLACK);
        else {
            char lines[2][32];
            const int n = mtb_Kit_Wrap(up, Terminal4x6, w - 4, &lines[0][0], sizeof(lines[0]), 2);
            for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(cx, 90 + i * 8, Terminal4x6, KIT_LABEL, lines[i], BLACK);
        }
    }
}

static void creators(const Bcc_View_t& v){
    const Bcc_Settings_t& s = *v.settings;
    uint8_t list[3];
    int n = 0;
    for (uint8_t src = BCC_YOUTUBE; src < BCC_SOURCES; src++) if (bcc_Source_Ready(s, src)) list[n++] = src;
    const Bcc_Creator_t& c = v.status->creator;
    header(c.channel[0] && bcc_Source_Ready(s, BCC_YOUTUBE) ? c.channel : "Creator counters", KIT_GOLD,
           bcc_Source_Ready(s, BCC_PLAUSIBLE) ? s.plSite : "", KIT_LABEL);
    if (!n) return;
    const int16_t w = (int16_t)(256 / n);
    for (int i = 0; i < n; i++){
        column((int16_t)(i * w), w, list[i], v, n);
        if (i) mtb_Panel_Draw_VLine((int16_t)(i * w), 24, 118, KIT_LINE);
    }
}

static void setup_Page(void){
    header("Business & creator counters", KIT_GOLD, "", KIT_LABEL);
    mtb_Kit_Text_Centre(128, 30, Terminal8x12, KIT_TEXT, "Nothing to count yet", BLACK);
    mtb_Kit_Text_Centre(128, 50, Terminal6x8, KIT_LABEL, "Sales: Stripe, Paystack, Shopify", BLACK);
    mtb_Kit_Text_Centre(128, 62, Terminal6x8, KIT_LABEL, "Creators: YouTube, GitHub, Plausible", BLACK);
    mtb_Kit_Text_Centre(128, 82, Terminal6x8, KIT_GOLD, "Set them up in the phone app:", BLACK);
    mtb_Kit_Text_Centre(128, 94, Terminal6x8, KIT_GOLD, "Business Counters", BLACK);
}

// The banner ------------------------------------------------------------------------------------------------------------
static const int16_t BX1 = 10, BY1 = 24, BX2 = 245, BY2 = 112;

void bcc_Draw_Banner_Frame(const Bcc_View_t& v){
    if (!v.banner) return;
    const uint16_t base = v.banner == BCC_BANNER_SALE ? K_MONEY : KIT_GOLD;
    const uint16_t c = (v.frame / 4) % 2 ? base : mtb_Kit_Dim(base, 110);
    mtb_Panel_Draw_Rect(BX1, BY1, BX2, BY2, c);
    mtb_Panel_Draw_Rect(BX1 + 1, BY1 + 1, BX2 - 1, BY2 - 1, c);
    // Sparkles drifting down the sides.
    for (int i = 0; i < 6; i++){
        const int16_t x = (int16_t)(i < 3 ? 2 + i * 2 : 248 + (i - 3) * 2);
        const int16_t y0 = (int16_t)(21 + ((v.frame * (2 + i % 3) + i * 23) % 104));
        mtb_Panel_Draw_VLine(x, 21, 126, BLACK);
        mtb_Panel_Draw_PixelRGB565(x, y0, i % 2 ? KIT_GOLD : base);
        if (y0 + 1 < 126) mtb_Panel_Draw_PixelRGB565(x, y0 + 1, mtb_Kit_Dim(base, 90));
    }
}

static void banner(const Bcc_View_t& v){
    const uint16_t base = v.banner == BCC_BANNER_SALE ? K_MONEY : KIT_GOLD;
    mtb_Panel_Fill_Rect(0, 20, 255, 127, BLACK);           // the page under it goes, all but the header
    if (v.banner == BCC_BANNER_SALE){ mtb_Panel_Fill_Circle(32, 44, 9, KIT_GOLD); mtb_Kit_Text_Centre(32, 40, Terminal6x8, BLACK, "$", KIT_GOLD); }
    else { star(32, 44, 11, KIT_GOLD); star(223, 44, 11, KIT_GOLD); }
    centre_Fit(128, 32, Terminal8x12, base, v.bannerTitle, 150);
    big_Centre(128, 50, 220, KIT_TEXT, v.bannerBig);
    centre_Fit(128, 92, Terminal6x8, KIT_LABEL, v.bannerDetail, 220);
    mtb_Kit_Text_Centre(128, BY2 - 9, Terminal4x6, KIT_DIM, "CLICK TO CLOSE", BLACK);
    bcc_Draw_Banner_Frame(v);
}

void bcc_Draw(const Bcc_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, BLACK);
    if (v.page == BCC_PAGE_SALES) sales(v);
    else if (v.page == BCC_PAGE_CREATOR) creators(v);
    else setup_Page();
    if (v.banner) banner(v);
    mtb_Panel_End_Update();
}
