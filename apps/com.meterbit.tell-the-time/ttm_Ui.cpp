// Tell the Time's screens, 256 x 118 under the status bar.
#include "ttm_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOP MTB_KIT_TOP
static const uint16_t FACE = MTB_KIT_RGB(250, 245, 225);
static const uint16_t HOUR_HAND = MTB_KIT_RGB(230, 40, 40);
static const uint16_t MIN_HAND = MTB_KIT_RGB(40, 110, 240);

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

const char* ttm_Level_Name(uint8_t l){
    static const char* const N[TTM_LEVELS] = {"O'CLOCK", "HALF PAST", "QUARTERS", "FIVE MINUTES", "ANY MINUTE", "TIME PASSING"};
    return N[l < TTM_LEVELS ? l : 0];
}

static void hand(int16_t cx, int16_t cy, float angle, int16_t len, int16_t width, uint16_t c){
    const float s = sinf(angle), co = -cosf(angle);
    const int16_t x = (int16_t)lroundf(cx + s * len), y = (int16_t)lroundf(cy + co * len);
    if (width <= 1){ mtb_Panel_Draw_Line(cx, cy, x, y, c); return; }
    // A filled bar: two triangles between the sides at the centre and at the tip (the tip a little narrower).
    const float px = -co, py = s, hw = width / 2.0f, tw = width / 3.0f;
    const int16_t ax = (int16_t)lroundf(cx + px * hw), ay = (int16_t)lroundf(cy + py * hw);
    const int16_t bx = (int16_t)lroundf(cx - px * hw), by = (int16_t)lroundf(cy - py * hw);
    const int16_t tx1 = (int16_t)lroundf(x + px * tw), ty1 = (int16_t)lroundf(y + py * tw);
    const int16_t tx2 = (int16_t)lroundf(x - px * tw), ty2 = (int16_t)lroundf(y - py * tw);
    mtb_Panel_Fill_Triangle(ax, ay, bx, by, tx1, ty1, c);
    mtb_Panel_Fill_Triangle(bx, by, tx1, ty1, tx2, ty2, c);
    mtb_Panel_Draw_Line(cx, cy, x, y, c);
}

void ttm_Clock_Face(int16_t cx, int16_t cy, int16_t r, uint16_t minute, uint8_t second, bool numbers){
    mtb_Panel_Fill_Circle(cx, cy, r, FACE);
    mtb_Panel_Draw_Circle(cx, cy, r, MTB_KIT_RGB(120, 120, 120));
    for (int i = 0; i < 60; i++){
        const float a = i * 6.2831853f / 60.0f;
        const int16_t outer = (int16_t)(r - 2), inner = (int16_t)(i % 5 ? r - 4 : r - 7);
        mtb_Panel_Draw_Line((int16_t)lroundf(cx + sinf(a) * inner), (int16_t)lroundf(cy - cosf(a) * inner),
                            (int16_t)lroundf(cx + sinf(a) * outer), (int16_t)lroundf(cy - cosf(a) * outer), i % 5 ? KIT_LABEL : BLACK);
    }
    if (numbers){
        for (int n = 1; n <= 12; n++){
            const float a = n * 6.2831853f / 12.0f;
            char t[4];
            snprintf(t, sizeof(t), "%d", n);
            const int16_t tx = (int16_t)lroundf(cx + sinf(a) * (r - 14)), ty = (int16_t)lroundf(cy - cosf(a) * (r - 14));
            mtb_Kit_Text_Centre(tx, (int16_t)(ty - 3), Terminal4x6, BLACK, t, FACE);
        }
    }
    const uint16_t m = minute % 720;
    hand(cx, cy, (m / 60 + (m % 60) / 60.0f) * 6.2831853f / 12.0f, (int16_t)(r * 0.52f), 6, HOUR_HAND);
    hand(cx, cy, (m % 60) * 6.2831853f / 60.0f, (int16_t)(r * 0.8f), 4, MIN_HAND);
    if (second < 60) hand(cx, cy, second * 6.2831853f / 60.0f, (int16_t)(r * 0.85f), 0, KIT_ORANGE);
    mtb_Panel_Fill_Circle(cx, cy, 3, BLACK);
}

static void menu(const Ttm_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "TELL THE TIME", KIT_GOLD);
    const char* names[2] = {"MY CLOCK", "PLAY"};
    for (int i = 0; i < 2; i++){
        const int16_t x = (int16_t)(18 + i * 116), y = TOP + 16;
        const bool sel = v.menu == i;
        const uint16_t c = i == 0 ? KIT_CYAN : KIT_GREEN;
        if (sel) mtb_Panel_Fill_Round_Rect(x, y, x + 104, y + 82, 8, mtb_Kit_Dim(c, 60));
        mtb_Panel_Draw_Round_Rect(x, y, x + 104, y + 82, 8, sel ? c : mtb_Kit_Dim(c, 110));
        if (i == 0) ttm_Clock_Face((int16_t)(x + 52), (int16_t)(y + 32), 24, v.minute, 60, false);
        else { kk_Star((int16_t)(x + 52), (int16_t)(y + 32), 22, KIT_GOLD); }
        mtb_Kit_Text_Centre((int16_t)(x + 52), (int16_t)(y + 64), Terminal8x12, sel ? KIT_TEXT : KIT_LABEL, names[i], sel ? mtb_Kit_Dim(c, 60) : BLACK);
    }
    char a[40];
    snprintf(a, sizeof(a), "LEVEL %u: %s", v.level + 1, ttm_Level_Name(v.level));
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 8, Terminal4x6, KIT_LABEL, a);
}

static void clock(const Ttm_View_t& v){
    clear();
    const Ttm_Settings_t& s = *v.s;
    ttm_Clock_Face(58, TOP + 56, 52, v.minute, v.second, true);
    char a[40], b[24];
    ttm_Digital(a, sizeof(a), v.minute, s.hour24);
    mtb_Kit_Big_Text(122, TOP + 4, Terminal10x16, 2, KIT_TEXT, a);
    const int h = v.minute / 60;
    const char* part = h < 12 ? "MORNING" : h < 17 ? "AFTERNOON" : h < 21 ? "EVENING" : "NIGHT";
    // The sun by day, the moon by night.
    if (h >= 7 && h < 19) mtb_Panel_Fill_Circle(238, TOP + 46, 6, KIT_GOLD);
    else { mtb_Panel_Fill_Circle(238, TOP + 46, 6, MTB_KIT_RGB(255, 230, 140)); mtb_Panel_Fill_Circle(241, TOP + 44, 5, BLACK); }
    mtb_Kit_Text(122, TOP + 42, Terminal6x8, KIT_LABEL, part);
    if (s.words){ ttm_Words(b, sizeof(b), v.minute); kk_Words(b, 186, TOP + 54, 130, Terminal6x8, KIT_GOLD, 2); }
    // The family's day: the next thing, and a line of the day.
    const int e = ttm_Next_Event(s, v.minute);
    if (e >= 0){
        const uint16_t in = (uint16_t)(s.events[e].minute - v.minute);
        ttm_Digital(b, sizeof(b), s.events[e].minute, s.hour24);
        char label[24];
        mtb_Kit_To_Ascii(label, sizeof(label), s.events[e].label);
        mtb_Kit_Upper(label);
        if (in < 60) snprintf(a, sizeof(a), "%s IN %u MIN", label, in);
        else snprintf(a, sizeof(a), "%s AT %s", label, b);
        kk_Words(a, 186, TOP + 78, 130, Terminal4x6, KIT_CYAN, 2);
    }
    const int16_t y = PANEL_RES_Y - 6, x0 = 122, x1 = 250;
    const uint16_t from = 6 * 60, to = 22 * 60;
    mtb_Panel_Draw_HLine(y, x0, x1, KIT_DIM);
    for (int i = 0; i < s.eventCount; i++){
        const uint16_t m = s.events[i].minute;
        if (m < from || m > to) continue;
        const int16_t x = (int16_t)(x0 + (x1 - x0) * (m - from) / (to - from));
        mtb_Panel_Fill_Rect(x - 1, y - 2, x + 1, y + 2, KIT_CYAN);
    }
    if (v.minute >= from && v.minute <= to){
        const int16_t x = (int16_t)(x0 + (x1 - x0) * (v.minute - from) / (to - from));
        mtb_Panel_Fill_Triangle(x - 3, y - 6, x + 3, y - 6, x, y - 2, KIT_ORANGE);
    }
}

static void question(const Ttm_View_t& v){
    clear();
    const Ttm_Question_t& q = *v.q;
    // Progress.
    for (int i = 0; i < TTM_ROUND; i++){
        const int16_t x = (int16_t)(124 + i * 16);
        const uint16_t c = i + 1 < v.number ? KIT_GREEN : i + 1 == v.number ? KIT_TEXT : KIT_LINE;
        mtb_Panel_Fill_Circle(x, TOP + 5, 3, c);
    }
    const uint16_t shown = q.kind == TTM_SET ? v.set : q.shown;
    ttm_Clock_Face(58, TOP + 59, 52, shown, 60, true);
    int16_t y = kk_Words(q.prompt, 188, TOP + 14, 132, Terminal8x12, KIT_GOLD, 3);
    if (q.kind == TTM_SET){
        if (!v.feedback) kk_Words("TURN THE KNOB TO MOVE THE HANDS. PRESS WHEN READY.", 188, (int16_t)(y + 6), 130, Terminal4x6, KIT_LABEL, 3);
    } else {
        for (int i = 0; i < 3; i++){
            const int16_t by = (int16_t)(y + 4 + i * 17);
            const bool sel = v.choice == i;
            uint16_t c = sel ? KIT_CYAN : KIT_DIM;
            if (v.feedback && q.choices[i] == q.target) c = KIT_GREEN;
            else if (v.feedback == 2 && sel) c = KIT_RED;
            if (sel) mtb_Panel_Fill_Round_Rect(124, by, 252, by + 14, 4, mtb_Kit_Dim(c, 60));
            mtb_Panel_Draw_Round_Rect(124, by, 252, by + 14, 4, c);
            mtb_Kit_Text_Centre(188, (int16_t)(by + 4), Terminal6x8, KIT_TEXT, q.choiceText[i], sel ? mtb_Kit_Dim(c, 60) : BLACK);
        }
    }
    if (v.feedback == 1){
        mtb_Panel_Fill_Round_Rect(124, PANEL_RES_Y - 22, 252, PANEL_RES_Y - 2, 5, MTB_KIT_RGB(0, 90, 30));
        mtb_Kit_Text_Centre(188, PANEL_RES_Y - 16, Terminal8x12, KIT_TEXT, "WELL DONE!", MTB_KIT_RGB(0, 90, 30));
    } else if (v.feedback == 2){
        mtb_Panel_Fill_Round_Rect(124, PANEL_RES_Y - 26, 252, PANEL_RES_Y - 2, 5, MTB_KIT_RGB(100, 30, 0));
        kk_Words(ttm_Hint(q, v.set), 188, PANEL_RES_Y - 23, 124, Terminal4x6, KIT_TEXT, 3, MTB_KIT_RGB(100, 30, 0));
    }
}

static void result(const Ttm_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "", KIT_GOLD);
    char a[40];
    snprintf(a, sizeof(a), "%u OUT OF %u!", v.right, TTM_ROUND);
    mtb_Kit_Big_Text((int16_t)(128 - mtb_Kit_Big_Text_Width(Terminal8x12, 2, a) / 2), TOP + 16, Terminal8x12, 2, KIT_GOLD, a);
    const int stars = v.right >= 7 ? 3 : v.right >= 5 ? 2 : v.right >= 3 ? 1 : 0;
    for (int i = 0; i < 3; i++) kk_Star((int16_t)(98 + i * 30), TOP + 62, 11, i < stars ? KIT_GOLD : KIT_LINE);
    if (v.levelUp){ snprintf(a, sizeof(a), "NEW LEVEL: %s", ttm_Level_Name(v.level)); mtb_Kit_Text_Centre(128, TOP + 82, Terminal6x8, KIT_GREEN, a); }
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 8, Terminal4x6, KIT_DIM, "PRESS TO PLAY AGAIN, HOLD FOR THE MENU");
    if (v.confettiMs) kk_Confetti(v.confettiMs, v.right, TOP);
}

void ttm_Draw(const Ttm_View_t& v){
    switch (v.screen){
        case TTM_SCR_PICK: kk_Pick_Screen(*v.k, TOP, v.pick); break;
        case TTM_SCR_CLOCK: clock(v); break;
        case TTM_SCR_QUESTION: question(v); break;
        case TTM_SCR_RESULT: result(v); break;
        case TTM_SCR_STOP: kk_Stop_Screen(*v.k, TOP, v.stop); break;
        default: menu(v); break;
    }
}
