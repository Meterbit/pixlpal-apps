// Treasure Hunt's screens, 256 x 118 under the status bar.
#include "trh_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOP MTB_KIT_TOP
static const uint16_t TRH_WOOD = MTB_KIT_RGB(140, 80, 30), TRH_WOOD_DARK = MTB_KIT_RGB(90, 50, 20), TRH_GOLD = MTB_KIT_RGB(255, 200, 40);

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

/** A treasure chest, its lid @p open (0 shut, 1 open) with gold glowing inside. */
static void chest(int16_t cx, int16_t base, int16_t w, float open, uint32_t ms){
    const int16_t h = (int16_t)(w * 0.55f), x0 = (int16_t)(cx - w / 2), x1 = (int16_t)(cx + w / 2), y0 = (int16_t)(base - h);
    if (open > 0.05f){
        // Gold heaped inside, sparkling.
        mtb_Panel_Fill_Rect((int16_t)(x0 + 3), (int16_t)(y0 - 4), (int16_t)(x1 - 3), (int16_t)(y0 + 4), TRH_GOLD);
        for (int i = 0; i < 6; i++){
            const int16_t sx = (int16_t)(x0 + 6 + (i * 37 + ms / 120) % (w - 12)), sy = (int16_t)(y0 - 6 - (i * 13 % 9));
            if ((ms / 200 + i) % 3 == 0) kk_Star(sx, sy, 3, KIT_TEXT);
        }
    }
    mtb_Panel_Fill_Rect(x0, y0, x1, base, TRH_WOOD);
    mtb_Panel_Draw_Rect(x0, y0, x1, base, TRH_WOOD_DARK);
    mtb_Panel_Fill_Rect(x0, (int16_t)(y0 + h / 3), x1, (int16_t)(y0 + h / 3 + 3), TRH_GOLD);
    mtb_Panel_Fill_Rect((int16_t)(cx - 4), (int16_t)(y0 + h / 3 - 2), (int16_t)(cx + 4), (int16_t)(y0 + h / 3 + 7), TRH_GOLD);
    // The lid: a curved top that tips back.
    const int16_t lh = (int16_t)(h * 0.5f * (1 - open)), ly = (int16_t)(y0 - lh - (open > 0.05f ? 2 + (int)(open * 10) : 0));
    if (open < 0.95f){
        mtb_Panel_Fill_Rect(x0, ly, x1, (int16_t)(ly + lh), TRH_WOOD);
        mtb_Panel_Draw_Rect(x0, ly, x1, (int16_t)(ly + lh), TRH_WOOD_DARK);
    } else {
        mtb_Panel_Fill_Rect(x0, (int16_t)(y0 - 20), x1, (int16_t)(y0 - 16), TRH_WOOD_DARK);
    }
}

static void ready(const Trh_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "TREASURE HUNT", KIT_GOLD);
    chest(64, TOP + 92, 76, 0, v.ms);
    char a[48];
    snprintf(a, sizeof(a), "%u CLUES TO FIND", v.s->count);
    mtb_Kit_Text_Centre(186, TOP + 30, Terminal8x12, KIT_TEXT, a);
    if (v.s->bestSeconds){
        char t[16];
        trh_Time_Text(t, sizeof(t), v.s->bestSeconds);
        snprintf(a, sizeof(a), "BEST TIME %s", t);
        mtb_Kit_Text_Centre(186, TOP + 50, Terminal6x8, KIT_LABEL, a);
    }
    const bool blink = (v.ms / 500) & 1;
    mtb_Kit_Text_Centre(186, TOP + 76, Terminal8x12, blink ? KIT_GREEN : MTB_KIT_RGB(20, 120, 50), "PRESS TO START");
}

static void clue(const Trh_View_t& v){
    clear();
    const Trh_Clue_t& c = v.s->clues[v.clue];
    // Where we are: a path of steps.
    for (int i = 0; i < v.s->count; i++){
        const int16_t x = (int16_t)(8 + i * 14), y = TOP + 6;
        if (i < v.clue) mtb_Panel_Fill_Circle(x, y, 4, KIT_GREEN);
        else if (i == v.clue) mtb_Panel_Fill_Circle(x, y, 4, (v.ms / 400) & 1 ? KIT_GOLD : KIT_ORANGE);
        else mtb_Panel_Draw_Circle(x, y, 4, KIT_DIM);
    }
    if (v.s->showTimer){ char t[16]; trh_Time_Text(t, sizeof(t), v.seconds); mtb_Kit_Text_Right(253, TOP + 3, Terminal6x8, KIT_LABEL, t); }
    char a[24];
    snprintf(a, sizeof(a), "CLUE %u", v.clue + 1);
    mtb_Kit_Text_Centre(128, TOP + 16, Terminal6x8, KIT_GOLD, a);
    kk_Words(c.text, 128, TOP + 28, 244, Terminal8x12, KIT_TEXT, 4);
    if (v.hint && c.hint[0]){
        mtb_Panel_Fill_Round_Rect(8, PANEL_RES_Y - 26, 247, PANEL_RES_Y - 3, 5, MTB_KIT_RGB(70, 50, 0));
        char h[64];
        snprintf(h, sizeof(h), "HINT: %s", c.hint);
        kk_Words(h, 128, PANEL_RES_Y - 22, 232, Terminal6x8, KIT_GOLD, 2, MTB_KIT_RGB(70, 50, 0));
    } else {
        mtb_Kit_Text_Centre(128, PANEL_RES_Y - 9, Terminal4x6, KIT_LABEL,
                            v.s->useCodes ? "FOUND THE CARD? PRESS TO TYPE ITS CODE. HOLD FOR A HINT" : "FOUND IT? A GROWN-UP MOVES YOU ON. HOLD FOR A HINT");
    }
}

static void code(const Trh_View_t& v){
    clear();
    char a[24];
    snprintf(a, sizeof(a), "CODE FOR CLUE %u", v.clue + 1);
    mtb_Kit_Text_Centre(128, TOP + 4, Terminal8x12, KIT_GOLD, a);
    const int16_t shake = v.wrong ? (int16_t)(4 * sinf(v.ms * 0.06f)) : 0;
    for (int i = 0; i < 3; i++){
        const int16_t x = (int16_t)(68 + i * 44 + shake), y = TOP + 26;
        const bool sel = v.digit == i;
        const uint16_t c = v.wrong ? KIT_RED : sel ? KIT_CYAN : KIT_DIM;
        mtb_Panel_Fill_Round_Rect(x, y, (int16_t)(x + 36), (int16_t)(y + 48), 6, sel ? mtb_Kit_Dim(c, 60) : BLACK);
        mtb_Panel_Draw_Round_Rect(x, y, (int16_t)(x + 36), (int16_t)(y + 48), 6, c);
        snprintf(a, sizeof(a), "%u", v.digits[i]);
        mtb_Kit_Big_Text((int16_t)(x + 18 - mtb_Kit_Big_Text_Width(Terminal10x16, 2, a) / 2), (int16_t)(y + 8), Terminal10x16, 2, i <= v.digit ? KIT_TEXT : KIT_LABEL, a);
        if (sel && !v.wrong){
            mtb_Panel_Fill_Triangle((int16_t)(x + 18), (int16_t)(y - 5), (int16_t)(x + 13), (int16_t)(y - 1), (int16_t)(x + 23), (int16_t)(y - 1), c);
            mtb_Panel_Fill_Triangle((int16_t)(x + 18), (int16_t)(y + 53), (int16_t)(x + 13), (int16_t)(y + 49), (int16_t)(x + 23), (int16_t)(y + 49), c);
        }
    }
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 14, Terminal6x8, v.wrong ? KIT_RED : KIT_LABEL, v.wrong ? "NOT QUITE. LOOK AT THE CARD AGAIN!" : "TURN TO CHANGE, PRESS FOR THE NEXT");
}

static void treasure(const Trh_View_t& v){
    clear();
    const float open = v.ms > 1500 ? 1.0f : v.ms / 1500.0f;
    chest(64, TOP + 96, 84, open, v.ms);
    mtb_Kit_Text_Centre(186, TOP + 6, Terminal10x16, KIT_GOLD, "TREASURE!");
    kk_Words(v.s->prize[0] ? v.s->prize : "YOU FOUND IT!", 186, TOP + 30, 134, Terminal6x8, KIT_TEXT, 3);
    char a[32], t[16];
    trh_Time_Text(t, sizeof(t), v.seconds);
    snprintf(a, sizeof(a), "TIME %s", t);
    mtb_Kit_Text_Centre(186, TOP + 66, Terminal8x12, KIT_CYAN, a);
    if (v.best) mtb_Kit_Text_Centre(186, TOP + 82, Terminal6x8, KIT_GREEN, "A NEW BEST TIME!");
    if (v.ms < 4000) kk_Confetti(v.ms, 7, TOP);
}

void trh_Draw(const Trh_View_t& v){
    switch (v.screen){
        case TRH_SCR_PICK: kk_Pick_Screen(*v.k, TOP, v.pick); break;
        case TRH_SCR_CLUE: clue(v); break;
        case TRH_SCR_CODE: code(v); break;
        case TRH_SCR_TREASURE: treasure(v); break;
        case TRH_SCR_EMPTY:
            clear();
            chest(128, TOP + 60, 60, 0, v.ms);
            kk_Words("ASK A GROWN-UP TO SET UP A HUNT ON THE PHONE", 128, TOP + 70, 240, Terminal8x12, KIT_TEXT, 3);
            break;
        default: ready(v); break;
    }
}
