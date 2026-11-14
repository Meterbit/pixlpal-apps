// Calm Corner's screens, 256 x 118 under the status bar. Soft colours, slow movement.
#include "clm_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOP MTB_KIT_TOP

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

/** A curve through (cx - w, cy), (cx, cy + bend), (cx + w, cy), @p t thick. */
static void curve(int16_t cx, int16_t cy, int16_t w, int16_t bend, int16_t t, uint16_t c){
    for (int dx = -w; dx <= w; dx++){
        const float f = 1.0f - (float)(dx * dx) / (float)(w * w);
        const int16_t y = (int16_t)lroundf(cy + bend * f);
        for (int k = 0; k < t; k++) mtb_Panel_Draw_PixelRGB565((int16_t)(cx + dx), (int16_t)(y + k), c);
    }
}

void clm_Face(int16_t cx, int16_t cy, int16_t r, uint8_t f, uint8_t level){
    const uint16_t c = mtb_Kit_Dim(clm_Feeling_Color(f), level), ink = BLACK;
    mtb_Panel_Fill_Circle(cx, cy, r, c);
    const int16_t ex = (int16_t)(r * 0.38f), ey = (int16_t)(cy - r * 0.2f), er = (int16_t)(r > 20 ? 3 : 2);
    const int16_t my = (int16_t)(cy + r * 0.38f), mw = (int16_t)(r * 0.42f), t = (int16_t)(r > 20 ? 2 : 1);
    switch (f){
        case CLM_CALM: case CLM_TIRED:
            // Closed (or nearly closed) eyes.
            curve((int16_t)(cx - ex), ey, er + 1, f == CLM_CALM ? 2 : 0, t, ink);
            curve((int16_t)(cx + ex), ey, er + 1, f == CLM_CALM ? 2 : 0, t, ink);
            break;
        default:
            mtb_Panel_Fill_Circle((int16_t)(cx - ex), ey, f == CLM_EXCITED ? er + 1 : er, ink);
            mtb_Panel_Fill_Circle((int16_t)(cx + ex), ey, f == CLM_EXCITED ? er + 1 : er, ink);
            break;
    }
    switch (f){
        case CLM_HAPPY: curve(cx, my - 2, mw, (int16_t)(r * 0.22f), t, ink); break;
        case CLM_CALM: curve(cx, my, (int16_t)(mw * 0.7f), (int16_t)(r * 0.1f), t, ink); break;
        case CLM_EXCITED: mtb_Panel_Fill_Circle(cx, my, (int16_t)(r * 0.2f), ink); break;
        case CLM_TIRED:
            mtb_Panel_Draw_HLine(my, (int16_t)(cx - mw / 2), (int16_t)(cx + mw / 2), ink);
            mtb_Kit_Text((int16_t)(cx + r - 2), (int16_t)(cy - r - 4), r > 20 ? Terminal6x8 : Terminal4x6, KIT_LABEL, "z");
            break;
        case CLM_SAD:
            curve(cx, my + 2, mw, (int16_t)(-r * 0.2f), t, ink);
            mtb_Panel_Fill_Circle((int16_t)(cx + ex), (int16_t)(ey + er + 4), (int16_t)(r > 20 ? 2 : 1), KIT_CYAN);
            break;
        case CLM_WORRIED:
            for (int dx = -mw; dx <= mw; dx++){
                const int16_t y = (int16_t)lroundf(my + 2 * sinf(dx * 0.6f));
                for (int k = 0; k < t; k++) mtb_Panel_Draw_PixelRGB565((int16_t)(cx + dx), (int16_t)(y + k), ink);
            }
            mtb_Panel_Draw_Line((int16_t)(cx - ex - er), (int16_t)(ey - er - 3), (int16_t)(cx - ex + er), (int16_t)(ey - er - 5), ink);
            mtb_Panel_Draw_Line((int16_t)(cx + ex - er), (int16_t)(ey - er - 5), (int16_t)(cx + ex + er), (int16_t)(ey - er - 3), ink);
            break;
        case CLM_ANGRY:
            curve(cx, my + 2, mw, (int16_t)(-r * 0.15f), t, ink);
            for (int k = 0; k < t; k++){
                mtb_Panel_Draw_Line((int16_t)(cx - ex - er - 1), (int16_t)(ey - er - 5 + k), (int16_t)(cx - ex + er + 1), (int16_t)(ey - er - 1 + k), ink);
                mtb_Panel_Draw_Line((int16_t)(cx + ex - er - 1), (int16_t)(ey - er - 1 + k), (int16_t)(cx + ex + er + 1), (int16_t)(ey - er - 5 + k), ink);
            }
            break;
    }
}

static void feel(const Clm_View_t& v, const char* title){
    clear();
    mtb_Kit_Text_Centre(128, TOP + 3, Terminal8x12, KIT_TEXT, title);
    const uint8_t f = v.feeling;
    const uint8_t prev = (uint8_t)((f + CLM_FEELINGS - 1) % CLM_FEELINGS), next = (uint8_t)((f + 1) % CLM_FEELINGS);
    clm_Face(42, TOP + 52, 16, prev, 90);
    clm_Face(214, TOP + 52, 16, next, 90);
    clm_Face(128, TOP + 50, 30, f, 255);
    mtb_Kit_Text_Centre(128, TOP + 86, Terminal10x16, clm_Feeling_Color(f), clm_Feeling_Name(f));
    for (int i = 0; i < CLM_FEELINGS; i++) mtb_Panel_Fill_Circle((int16_t)(128 - 3 * 9 + i * 9), PANEL_RES_Y - 5, 2, i == f ? KIT_TEXT : KIT_LINE);
}

static const char* const SHORT[CLM_CHOICES] = {"BREATHE", "LOOK", "STRETCH", "LIGHTS", "I'M OK"};

static void answer(const Clm_View_t& v){
    clear();
    clm_Face(26, TOP + 24, 20, v.feeling, 255);
    kk_Words(clm_Answer(v.feeling), 150, TOP + 6, 196, Terminal6x8, KIT_TEXT, 4);
    const uint8_t suggested = clm_Suggest(v.feeling);
    for (int i = 0; i < CLM_CHOICES; i++){
        const int row = i < 3 ? 0 : 1, col = i < 3 ? i : i - 3;
        const int16_t w = 78, x = (int16_t)(row == 0 ? 5 + col * 84 : 47 + col * 84), y = (int16_t)(TOP + 52 + row * 30);
        const bool sel = v.choice == i;
        const uint16_t c = i == CLM_CHOICES - 1 ? KIT_LABEL : clm_Feeling_Color(v.feeling);
        if (sel) mtb_Panel_Fill_Round_Rect(x, y, x + w, y + 24, 6, mtb_Kit_Dim(c, 80));
        mtb_Panel_Draw_Round_Rect(x, y, x + w, y + 24, 6, sel ? c : mtb_Kit_Dim(c, 100));
        mtb_Kit_Text_Centre((int16_t)(x + w / 2), (int16_t)(y + 8), Terminal6x8, KIT_TEXT, SHORT[i], sel ? mtb_Kit_Dim(c, 80) : BLACK);
        if (i == suggested) kk_Star((int16_t)(x + w - 6), (int16_t)(y + 5), 3, KIT_GOLD);
    }
}

static void breathe(const Clm_View_t& v){
    clear();
    Clm_Breath_t b;
    clm_Breath_At(v.s->breathStyle, kk_Age(*v.k), v.s->breaths, v.ms, b);
    const uint16_t inC = MTB_KIT_RGB(90, 170, 255), outC = MTB_KIT_RGB(120, 220, 170);
    const uint16_t c = b.phase == CLM_IN || b.phase == CLM_HOLD_IN ? inC : outC;
    const int16_t cx = 64, cy = TOP + 58;
    const int16_t r = (int16_t)(10 + b.size * 40);
    mtb_Panel_Fill_Circle(cx, cy, (int16_t)(r + 3), mtb_Kit_Dim(c, 50));
    mtb_Panel_Fill_Circle(cx, cy, r, mtb_Kit_Dim(c, 170));
    mtb_Panel_Fill_Circle((int16_t)(cx - r / 3), (int16_t)(cy - r / 3), (int16_t)(r / 5 + 1), mtb_Kit_Dim(KIT_TEXT, 160));   // a shine
    const char* words = b.phase == CLM_IN ? "BREATHE IN" : b.phase == CLM_OUT ? "BREATHE OUT" : b.phase == CLM_DONE ? "WELL DONE" : "HOLD";
    mtb_Kit_Text_Centre(190, TOP + 20, Terminal8x12, c, words);
    char a[16];
    if (b.phase != CLM_DONE){
        snprintf(a, sizeof(a), "%u", b.secondsLeft);
        mtb_Kit_Big_Text((int16_t)(190 - mtb_Kit_Big_Text_Width(Terminal10x16, 2, a) / 2), TOP + 40, Terminal10x16, 2, KIT_TEXT, a);
    }
    for (int i = 0; i < v.s->breaths; i++){
        const int16_t x = (int16_t)(190 - (v.s->breaths - 1) * 6 + i * 12);
        mtb_Panel_Fill_Circle(x, PANEL_RES_Y - 12, 3, i + 1 < b.breath || b.phase == CLM_DONE ? c : i + 1 == b.breath ? KIT_TEXT : KIT_LINE);
    }
}

static void ground(const Clm_View_t& v){
    clear();
    const uint8_t n = clm_Ground_Count(v.groundStep);
    char a[24];
    snprintf(a, sizeof(a), "FIND %u", n);
    mtb_Kit_Big_Text((int16_t)(128 - mtb_Kit_Big_Text_Width(Terminal10x16, 2, a) / 2), TOP + 6, Terminal10x16, 2, KIT_GOLD, a);
    mtb_Kit_Text_Centre(128, TOP + 44, Terminal8x12, KIT_TEXT, clm_Ground_Text(v.groundStep));
    for (int i = 0; i < n; i++){
        const int16_t x = (int16_t)(128 - (n - 1) * 15 + i * 30);
        if (i < v.groundFound) kk_Star(x, TOP + 76, 10, KIT_GOLD);
        else mtb_Panel_Draw_Circle(x, TOP + 76, 9, KIT_DIM);
    }
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 9, Terminal4x6, KIT_LABEL, "PRESS THE KNOB FOR EACH ONE YOU FIND");
}

/** A stick figure: @p pose 0-5, @p t 0-1 through the move. */
static void figure(int16_t cx, int16_t base, int pose, float t){
    const uint16_t c = MTB_KIT_RGB(255, 210, 160);
    const int16_t hy = (int16_t)(base - 66), sy = (int16_t)(base - 52), hip = (int16_t)(base - 26);
    float lean = 0;
    if (pose == 1) lean = -0.35f * sinf(t * 3.1416f);
    if (pose == 2) lean = 0.35f * sinf(t * 3.1416f);
    const int16_t hx = (int16_t)(cx + lean * 30), shx = (int16_t)(cx + lean * 22);
    mtb_Panel_Draw_Circle(hx, hy, 7, c);
    mtb_Panel_Draw_Line(hx, (int16_t)(hy + 7), shx, sy, c);            // the neck
    mtb_Panel_Draw_Line(shx, sy, cx, hip, c);
    mtb_Panel_Draw_Line(cx, hip, (int16_t)(cx - 10), base, c);
    mtb_Panel_Draw_Line(cx, hip, (int16_t)(cx + 10), base, c);
    int16_t lx, ly, rx, ry;
    switch (pose){
        case 0: { const int16_t up = (int16_t)(18 + 6 * sinf(t * 6.283f)); lx = (int16_t)(shx - 16); ly = (int16_t)(sy - up); rx = (int16_t)(shx + 16); ry = ly; break; }
        case 1: case 2: lx = (int16_t)(shx - 14 + lean * 30); ly = (int16_t)(sy - 20); rx = (int16_t)(shx + 14 + lean * 30); ry = ly; break;
        case 3: { const float a = t * 12.566f; lx = (int16_t)(shx - 14 + 3 * cosf(a)); ly = (int16_t)(sy + 10 + 3 * sinf(a)); rx = (int16_t)(shx + 14 - 3 * cosf(a)); ry = ly; break; }
        case 4: { const int16_t j = (int16_t)(3 * sinf(t * 60.0f)); lx = (int16_t)(shx - 16); ly = (int16_t)(sy + 12 + j); rx = (int16_t)(shx + 16); ry = (int16_t)(sy + 12 - j); break; }
        default: lx = (int16_t)(shx + 8); ly = (int16_t)(sy + 6); rx = (int16_t)(shx - 8); ry = (int16_t)(sy + 6); break;
    }
    mtb_Panel_Draw_Line(shx, sy, lx, ly, c);
    mtb_Panel_Draw_Line(shx, sy, rx, ry, c);
}

static void stretch(const Clm_View_t& v){
    clear();
    const uint32_t each = CLM_STRETCH_SECONDS * 1000u;
    const float t = (float)(v.ms % each) / (float)each;
    figure(56, PANEL_RES_Y - 6, v.stretch, t);
    kk_Words(clm_Stretch_Text(v.stretch), 176, TOP + 20, 150, Terminal8x12, KIT_TEXT, 3);
    char a[16];
    snprintf(a, sizeof(a), "%d OF %d", v.stretch + 1, CLM_STRETCHES);
    mtb_Kit_Text_Centre(176, TOP + 66, Terminal6x8, KIT_LABEL, a);
    mtb_Kit_Bar(110, TOP + 84, 132, 4, t, MTB_KIT_RGB(120, 220, 170), KIT_LINE);
}

static void lights(const Clm_View_t& v){
    const float t = v.ms / 1000.0f;
    // Slow waves of colour, kept dim.
    for (int x = 0; x < PANEL_RES_X; x += 4){
        for (int y = TOP; y < PANEL_RES_Y; y += 4){
            const float a = sinf(x * 0.025f + t * 0.5f) + sinf(y * 0.04f - t * 0.33f) + sinf((x + y) * 0.015f + t * 0.21f);
            const float h = (a + 3.0f) / 6.0f;
            const uint8_t r = (uint8_t)(25 + 45 * h), g = (uint8_t)(35 + 35 * (1 - h)), b = (uint8_t)(60 + 50 * h);
            mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + 3), (int16_t)(y + 3), MTB_KIT_RGB(r, g, b));
        }
    }
    const uint32_t total = v.s->lightsMinutes * 60000u;
    if (v.ms < total){
        char a[16];
        const uint32_t left = (total - v.ms) / 1000;
        snprintf(a, sizeof(a), "%lu:%02lu", (unsigned long)(left / 60), (unsigned long)(left % 60));
        mtb_Kit_Text_Right(252, PANEL_RES_Y - 8, Terminal4x6, MTB_KIT_RGB(140, 140, 160), a, MTB_KIT_RGB(40, 40, 70));
    }
}

static void thanks(const Clm_View_t& v){
    clear();
    clm_Face(128, TOP + 34, 24, v.feeling, 255);
    char a[48], name[20];
    mtb_Kit_To_Ascii(name, sizeof(name), kk_Name(*v.k));
    mtb_Kit_Upper(name);
    snprintf(a, sizeof(a), "THANK YOU FOR TELLING ME, %s", name);
    kk_Words(a, 128, TOP + 66, 240, Terminal8x12, KIT_TEXT, 2);
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 9, Terminal4x6, KIT_LABEL, "YOU CAN COME BACK ANY TIME");
}

void clm_Draw(const Clm_View_t& v){
    switch (v.screen){
        case CLM_SCR_PICK: kk_Pick_Screen(*v.k, TOP, v.pick); break;
        case CLM_SCR_ANSWER: answer(v); break;
        case CLM_SCR_BREATHE: breathe(v); break;
        case CLM_SCR_GROUND: ground(v); break;
        case CLM_SCR_STRETCH: stretch(v); break;
        case CLM_SCR_LIGHTS: lights(v); break;
        case CLM_SCR_AFTER: feel(v, "HOW DO YOU FEEL NOW?"); break;
        case CLM_SCR_THANKS: thanks(v); break;
        default: feel(v, "HOW DO YOU FEEL?"); break;
    }
}
