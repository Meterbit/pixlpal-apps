// Kids' Learning's screens: big, bright and simple.
#include "kid_Ui.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t K_GOLD = MTB_KIT_RGB(255, 200, 0);
static const uint16_t K_SKY = MTB_KIT_RGB(70, 170, 255);
static const uint16_t K_PINK = MTB_KIT_RGB(255, 100, 170);
static const uint16_t K_LIME = MTB_KIT_RGB(120, 230, 70);
static const uint16_t K_VIOLET = MTB_KIT_RGB(170, 110, 255);
static const uint16_t K_ORANGE = MTB_KIT_RGB(255, 140, 30);

uint16_t kid_Child_Color(uint8_t child){
    static const uint16_t C[KID_CHILDREN] = {MTB_KIT_RGB(70, 170, 255), MTB_KIT_RGB(255, 100, 170), MTB_KIT_RGB(120, 230, 70)};
    return C[child < KID_CHILDREN ? child : 0];
}

const char* kid_Menu_Name(uint8_t item){
    static const char* const N[KID_M_COUNT] = {"MATHS", "TIMES RACE", "SPELLING", "READING", "GAMES", "STARS"};
    return item < KID_M_COUNT ? N[item] : "";
}

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

static void text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back = BLACK){
    char a[96], b[96];
    mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    mtb_Kit_Text(x, y, font, color, b, back);
}

static void centre_Big(int16_t y, int scale, uint16_t color, const char* text){
    const int16_t w = mtb_Kit_Big_Text_Width(Terminal8x12, scale, text);
    mtb_Kit_Big_Text(128 - w / 2, y, Terminal8x12, scale, color, text);
}

static const Kid_Child_t& child(const Kid_View_t& v){ return v.settings->children[v.settings->active]; }

// "MIA  * 42" left, @p right on the right.
static void header(const Kid_View_t& v, const char* right){
    const Kid_Child_t& c = child(v);
    const uint16_t cc = kid_Child_Color(v.settings->active);
    char t[48];
    mtb_Kit_To_Ascii(t, sizeof(t), c.name);
    mtb_Kit_Upper(t);
    int16_t x = mtb_Kit_Text(3, 12, Terminal4x6, cc, t, BLACK);
    star(x + 8, 14, 4, K_GOLD);
    snprintf(t, sizeof(t), "%u", c.stars);
    mtb_Kit_Text(x + 14, 12, Terminal4x6, K_GOLD, t, BLACK);
    if (right){
        mtb_Kit_To_Ascii(t, sizeof(t), right);
        mtb_Kit_Upper(t);
        mtb_Kit_Text_Right(253, 12, Terminal4x6, KIT_LABEL, t, BLACK);
    }
    mtb_Panel_Draw_HLine(19, 0, 255, KIT_LINE);
}

static void time_Left(const Kid_View_t& v, char* out, size_t size){
    const uint16_t left = kid_Seconds_Left(*v.settings, child(v));
    if (left == 0xFFFF) out[0] = '\0';
    else snprintf(out, size, "%u MIN LEFT TODAY", (unsigned)((left + 59) / 60));
}

// Who is playing ---------------------------------------------------------------------------------------------------------
static void pick(const Kid_View_t& v){
    mtb_Kit_Text_Centre(128, 14, Terminal6x8, K_GOLD, "WHO IS PLAYING?", BLACK);
    int n = 0, slots[KID_CHILDREN];
    for (int i = 0; i < KID_CHILDREN; i++) if (v.settings->children[i].used) slots[n++] = i;
    const int16_t w = 76, gap = 6, x0 = 128 - (n * w + (n - 1) * gap) / 2;
    for (int k = 0; k < n; k++){
        const int i = slots[k];
        const int16_t x = (int16_t)(x0 + k * (w + gap));
        const bool sel = k == v.play->index;
        const uint16_t cc = kid_Child_Color((uint8_t)i);
        mtb_Panel_Fill_Rect(x, 32, x + w - 1, 104, sel ? mtb_Kit_Dim(cc, 90) : mtb_Kit_Dim(cc, 30));
        if (sel){ mtb_Panel_Draw_Rect(x, 32, x + w - 1, 104, KIT_TEXT); mtb_Panel_Draw_Rect(x + 1, 33, x + w - 2, 103, KIT_TEXT); }
        // A face.
        const uint16_t bg = sel ? mtb_Kit_Dim(cc, 90) : mtb_Kit_Dim(cc, 30);
        mtb_Panel_Fill_Circle(x + w / 2, 56, 15, cc);
        mtb_Panel_Fill_Circle(x + w / 2 - 5, 52, 2, BLACK);
        mtb_Panel_Fill_Circle(x + w / 2 + 5, 52, 2, BLACK);
        mtb_Panel_Draw_Line(x + w / 2 - 6, 61, x + w / 2 - 2, 64, BLACK);
        mtb_Panel_Draw_Line(x + w / 2 - 2, 64, x + w / 2 + 2, 64, BLACK);
        mtb_Panel_Draw_Line(x + w / 2 + 2, 64, x + w / 2 + 6, 61, BLACK);
        char a[20], b[20];
        mtb_Kit_To_Ascii(a, sizeof(a), v.settings->children[i].name);
        mtb_Kit_Fit(b, sizeof(b), Terminal6x8, a, w - 6);
        mtb_Kit_Text_Centre(x + w / 2, 80, Terminal6x8, KIT_TEXT, b, bg);
        char s[12];
        snprintf(s, sizeof(s), "%u", v.settings->children[i].stars);
        star(x + w / 2 - 10, 95, 4, K_GOLD);
        mtb_Kit_Text(x + w / 2 - 3, 93, Terminal4x6, K_GOLD, s, bg);
    }
    mtb_Kit_Text_Centre(128, 116, Terminal4x6, KIT_DIM, "TURN TO CHOOSE, CLICK TO START", BLACK);
}

// The menu ---------------------------------------------------------------------------------------------------------------------
static void icon(uint8_t item, int16_t cx, int16_t cy, uint16_t c, const Kid_View_t& v){
    switch (item){
        case KID_M_MATH: mtb_Kit_Big_Text(cx - 24, cy - 12, Terminal8x12, 2, c, "+x"); break;
        case KID_M_RACE:
            mtb_Panel_Draw_Circle(cx, cy + 2, 11, c);
            mtb_Panel_Draw_Circle(cx, cy + 2, 10, c);
            mtb_Panel_Draw_Line(cx, cy + 2, cx, cy - 5, c);
            mtb_Panel_Draw_Line(cx, cy + 2, cx + 5, cy + 4, c);
            mtb_Panel_Fill_Rect(cx - 2, cy - 13, cx + 2, cy - 10, c);
            break;
        case KID_M_SPELL: mtb_Kit_Big_Text(cx - 24, cy - 12, Terminal8x12, 2, c, "Ab"); break;
        case KID_M_READ:
            mtb_Panel_Fill_Rect(cx - 16, cy - 9, cx - 2, cy + 10, c);
            mtb_Panel_Fill_Rect(cx + 2, cy - 9, cx + 16, cy + 10, c);
            for (int k = 0; k < 3; k++){ mtb_Panel_Draw_HLine(cy - 4 + k * 5, cx - 13, cx - 5, BLACK); mtb_Panel_Draw_HLine(cy - 4 + k * 5, cx + 5, cx + 13, BLACK); }
            break;
        case KID_M_GAMES: {
            mtb_Panel_Fill_Rect(cx - 16, cy - 7, cx + 16, cy + 8, c);
            mtb_Panel_Fill_Rect(cx - 11, cy - 1, cx - 5, cy + 1, BLACK);
            mtb_Panel_Fill_Rect(cx - 9, cy - 3, cx - 7, cy + 3, BLACK);
            mtb_Panel_Fill_Circle(cx + 7, cy - 1, 2, BLACK);
            mtb_Panel_Fill_Circle(cx + 11, cy + 2, 2, BLACK);
            (void)v;
            break;
        }
        default: star(cx, cy, 13, c); break;
    }
}

static void menu(const Kid_View_t& v){
    char t[40];
    time_Left(v, t, sizeof(t));
    header(v, t);
    static const uint16_t C[KID_M_COUNT] = {K_SKY, K_ORANGE, K_PINK, K_LIME, K_VIOLET, K_GOLD};
    const Kid_Child_t& c = child(v);
    for (int i = 0; i < KID_M_COUNT; i++){
        const int16_t x = (int16_t)(1 + (i % 3) * 85), y = (int16_t)(22 + (i / 3) * 52), w = 84, h = 50;
        const bool sel = i == v.play->index;
        const uint16_t bg = sel ? mtb_Kit_Dim(C[i], 80) : mtb_Kit_Dim(C[i], 22);
        mtb_Panel_Fill_Rect(x, y, x + w - 1, y + h - 1, bg);
        if (sel){ mtb_Panel_Draw_Rect(x, y, x + w - 1, y + h - 1, KIT_TEXT); mtb_Panel_Draw_Rect(x + 1, y + 1, x + w - 2, y + h - 2, KIT_TEXT); }
        icon((uint8_t)i, x + w / 2, y + 19, sel ? KIT_TEXT : C[i], v);
        const char* label = kid_Menu_Name((uint8_t)i);
        char g[24];
        if (i == KID_M_GAMES){ snprintf(g, sizeof(g), c.gameSeconds ? "GAMES %u MIN" : "GAMES", (unsigned)((c.gameSeconds + 59) / 60)); label = g; }
        if (i == KID_M_RACE && c.bestRace){ snprintf(g, sizeof(g), "RACE (BEST %u)", c.bestRace); label = g; }
        mtb_Kit_Text_Centre(x + w / 2, y + h - 11, Terminal4x6, sel ? KIT_TEXT : KIT_LABEL, label, bg);
    }
}

// Maths and the race ------------------------------------------------------------------------------------------------------------
static void answer_Area(const Kid_View_t& v, bool choices){
    const Kid_Play_t& p = *v.play;
    if (choices){
        for (int k = 0; k < 3; k++){
            const int16_t x = (int16_t)(18 + k * 76), w = 68;
            const bool sel = k == p.choice;
            mtb_Panel_Fill_Rect(x, 62, x + w - 1, 96, sel ? mtb_Kit_Dim(K_SKY, 110) : mtb_Kit_Dim(K_SKY, 28));
            if (sel) mtb_Panel_Draw_Rect(x, 62, x + w - 1, 96, KIT_TEXT);
            char n[12];
            snprintf(n, sizeof(n), "%d", p.q.choices[k]);
            const int scale = mtb_Kit_Big_Text_Width(Terminal8x12, 2, n) <= w - 6 ? 2 : 1;
            const int16_t tw = mtb_Kit_Big_Text_Width(Terminal8x12, scale, n);
            mtb_Kit_Big_Text(x + w / 2 - tw / 2, scale == 2 ? 67 : 73, Terminal8x12, scale, KIT_TEXT, n);
        }
        mtb_Kit_Text_Centre(128, 110, Terminal4x6, KIT_DIM, "TURN TO CHOOSE, CLICK TO ANSWER", BLACK);
    } else {
        mtb_Panel_Draw_Rect(78, 62, 177, 96, K_SKY);
        mtb_Panel_Draw_Rect(79, 63, 176, 95, K_SKY);
        const bool blink = (v.ms / 400) % 2;
        char e[12];
        snprintf(e, sizeof(e), "%s%s", p.entry, blink ? "_" : " ");
        centre_Big(67, 2, KIT_TEXT, e);
        mtb_Kit_Text_Centre(128, 110, Terminal4x6, KIT_DIM, "TYPE THE ANSWER, PRESS ENTER", BLACK);
    }
}

static void feedback(const Kid_View_t& v){
    const Kid_Play_t& p = *v.play;
    if (!p.feedback) return;
    const bool ok = p.feedback == 1;
    const uint16_t c = ok ? K_LIME : KIT_RED;
    mtb_Panel_Fill_Rect(40, 56, 215, 102, BLACK);
    mtb_Panel_Draw_Rect(40, 56, 215, 102, c);
    mtb_Panel_Draw_Rect(41, 57, 214, 101, c);
    if (ok){ star(70, 79, 13, K_GOLD); centre_Big(68, 2, c, "  RIGHT!"); }
    else {
        char t[24];
        snprintf(t, sizeof(t), "%d", p.q.answer);
        mtb_Kit_Text_Centre(128, 62, Terminal6x8, KIT_TEXT, "NOT QUITE. IT IS", BLACK);
        centre_Big(74, 2, c, t);
    }
}

static void maths(const Kid_View_t& v, bool race){
    const Kid_Play_t& p = *v.play;
    char t[48];
    if (race){
        const int32_t left = (int32_t)(p.raceEnd - v.now);
        snprintf(t, sizeof(t), "TIME %d   RIGHT %u", left > 0 ? (int)left : 0, p.right);
    } else snprintf(t, sizeof(t), "LEVEL %u   %u/%u", child(v).mathLevel + 1, p.number + 1, KID_ROUND);
    header(v, t);
    if (race){
        const int32_t left = (int32_t)(p.raceEnd - v.now);
        mtb_Kit_Bar(3, 22, 250, 3, left > 0 ? left / (float)KID_RACE_SECONDS : 0, left < 10 ? KIT_RED : K_ORANGE);
    }
    char q[32];
    snprintf(q, sizeof(q), "%s ?", p.q.text);
    for (char* c = q; *c; c++) if (*c == '/') *c = ':';          // the division sign children know
    const int scale = mtb_Kit_Big_Text_Width(Terminal8x12, 3, q) <= 250 ? 3 : 2;
    centre_Big(scale == 3 ? 26 : 32, scale, KIT_TEXT, q);
    answer_Area(v, p.q.choice || !p.typing);
    feedback(v);
}

// Spelling -----------------------------------------------------------------------------------------------------------------------
static void spelling(const Kid_View_t& v){
    const Kid_Play_t& p = *v.play;
    char t[48];
    snprintf(t, sizeof(t), "WORD %u OF %u", p.number + 1, KID_SPELL_ROUND);
    header(v, t);
    if (!p.word) return;
    if (p.lookPhase){
        mtb_Kit_Text_Centre(128, 26, Terminal6x8, K_GOLD, "LOOK AND REMEMBER", BLACK);
        char w[40];
        mtb_Kit_To_Ascii(w, sizeof(w), p.word);
        const int scale = mtb_Kit_Big_Text_Width(Terminal8x12, 3, w) <= 250 ? 3 : 2;
        centre_Big(48, scale, K_PINK, w);
        const int32_t left = (int32_t)(p.lookUntil - v.ms);
        mtb_Kit_Bar(48, 100, 160, 4, left > 0 ? left / 4000.0f : 0, K_GOLD);
        return;
    }
    mtb_Kit_Text_Centre(128, 24, Terminal6x8, K_GOLD, v.settings->speak ? "LISTEN AND SPELL IT" : "NOW SPELL IT", BLACK);
    // Slots: one per letter of the word, the typed ones filled.
    const int n = (int)strlen(p.word), typed = (int)strlen(p.typed);
    const int slots = n > typed ? n : typed;
    const int16_t cw = slots > 12 ? 16 : 20, x0 = (int16_t)(128 - slots * cw / 2);
    for (int i = 0; i < slots; i++){
        const int16_t x = (int16_t)(x0 + i * cw);
        mtb_Panel_Draw_HLine(70, x + 2, x + cw - 3, i == typed ? K_GOLD : KIT_LABEL);
        if (i < typed){ char ch[2] = {(char)(p.typed[i] >= 'a' && p.typed[i] <= 'z' ? p.typed[i] - 32 : p.typed[i]), 0}; mtb_Kit_Big_Text(x + cw / 2 - 8, 44, Terminal8x12, 2, KIT_TEXT, ch); }
    }
    if (!p.typing){
        // The letter the knob shows, with its neighbours.
        char a[2] = {(char)('A' + p.letter), 0}, b[2] = {(char)('A' + (p.letter + 25) % 26), 0}, c[2] = {(char)('A' + (p.letter + 1) % 26), 0};
        mtb_Panel_Fill_Rect(108, 78, 147, 104, mtb_Kit_Dim(K_PINK, 80));
        mtb_Kit_Big_Text(120, 80, Terminal8x12, 2, KIT_TEXT, a);
        mtb_Kit_Big_Text(84, 84, Terminal8x12, 1, KIT_DIM, b);
        mtb_Kit_Big_Text(164, 84, Terminal8x12, 1, KIT_DIM, c);
        mtb_Kit_Text_Centre(128, 110, Terminal4x6, KIT_DIM, "CLICK ADD   2 CLICKS UNDO   HOLD CHECK", BLACK);
    } else mtb_Kit_Text_Centre(128, 110, Terminal4x6, KIT_DIM, "TYPE IT, PRESS ENTER", BLACK);
    if (p.feedback){
        const bool ok = p.feedback == 1;
        const uint16_t c = ok ? K_LIME : KIT_RED;
        mtb_Panel_Fill_Rect(30, 56, 225, 104, BLACK);
        mtb_Panel_Draw_Rect(30, 56, 225, 104, c);
        mtb_Panel_Draw_Rect(31, 57, 224, 103, c);
        char w[40];
        mtb_Kit_To_Ascii(w, sizeof(w), p.word);
        mtb_Kit_Text_Centre(128, 62, Terminal6x8, c, ok ? "WELL SPELT!" : "IT IS SPELT", BLACK);
        centre_Big(76, 2, KIT_TEXT, w);
    }
}

// Reading --------------------------------------------------------------------------------------------------------------------------
static void reading(const Kid_View_t& v){
    const Kid_Play_t& p = *v.play;
    header(v, p.readPaused ? "PAUSED" : "READING");
    const uint32_t s = p.readSeconds;
    char t[16];
    snprintf(t, sizeof(t), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    centre_Big(30, 4, p.readPaused ? KIT_LABEL : K_LIME, t);
    const Kid_Child_t& c = child(v);
    const uint32_t today = c.readWeek[0] * 60u + s;
    const float f = today / (v.settings->readGoal * 60.0f);
    mtb_Kit_Bar(28, 88, 200, 6, f > 1 ? 1 : f, f >= 1 ? K_GOLD : K_LIME);
    char g[48];
    snprintf(g, sizeof(g), "TODAY %u OF %u MINUTES", (unsigned)(today / 60), v.settings->readGoal);
    mtb_Kit_Text_Centre(128, 98, Terminal4x6, KIT_LABEL, g, BLACK);
    mtb_Kit_Text_Centre(128, 112, Terminal4x6, KIT_DIM, "CLICK: PAUSE   HOLD: I HAVE FINISHED", BLACK);
}

// The games ------------------------------------------------------------------------------------------------------------------------
static void games(const Kid_View_t& v){
    const Kid_Child_t& c = child(v);
    char t[40];
    snprintf(t, sizeof(t), "%u:%02u OF GAME TIME", (unsigned)(c.gameSeconds / 60), (unsigned)(c.gameSeconds % 60));
    header(v, t);
    static const char* const NAMES[] = {"STAR CATCHER", "CAVE GLIDER"};
    for (int i = 0; i < 2; i++){
        const int16_t x = (int16_t)(8 + i * 124), w = 116;
        const bool sel = i == v.play->index;
        const uint16_t cc = i ? K_SKY : K_GOLD;
        mtb_Panel_Fill_Rect(x, 24, x + w - 1, 92, sel ? mtb_Kit_Dim(cc, 70) : mtb_Kit_Dim(cc, 22));
        if (sel){ mtb_Panel_Draw_Rect(x, 24, x + w - 1, 92, KIT_TEXT); mtb_Panel_Draw_Rect(x + 1, 25, x + w - 2, 91, KIT_TEXT); }
        if (i == 0){
            star(x + 40, 44, 8, K_GOLD); star(x + 72, 36, 6, K_GOLD); star(x + 86, 54, 5, K_GOLD);
            mtb_Panel_Fill_Rect(x + 44, 70, x + 72, 74, KIT_TEXT); mtb_Panel_Fill_Rect(x + 46, 75, x + 70, 78, KIT_TEXT);
        } else {
            mtb_Panel_Fill_Triangle(x, 24, x + w - 1, 24, x + w / 2, 40, mtb_Kit_Dim(K_VIOLET, 140));
            mtb_Panel_Fill_Triangle(x, 92, x + w - 1, 92, x + w / 2, 76, mtb_Kit_Dim(K_VIOLET, 140));
            mtb_Panel_Fill_Triangle(x + 40, 54, x + 40, 62, x + 54, 58, KIT_TEXT);
        }
        mtb_Kit_Text_Centre(x + w / 2, 82, Terminal6x8, KIT_TEXT, NAMES[i], sel ? mtb_Kit_Dim(cc, 70) : mtb_Kit_Dim(cc, 22));
    }
    if (!c.gameSeconds){
        mtb_Kit_Text_Centre(128, 100, Terminal6x8, K_GOLD, "EARN GAME TIME FIRST:", BLACK);
        mtb_Kit_Text_Centre(128, 111, Terminal4x6, KIT_LABEL, "7 RIGHT OF 10 IN MATHS, SPELLING OR THE RACE", BLACK);
    } else mtb_Kit_Text_Centre(128, 106, Terminal4x6, KIT_DIM, "TURN TO CHOOSE, CLICK TO PLAY, HOLD TO GO BACK", BLACK);
}

static void game_Bar(const Kid_View_t& v, const char* left){
    const Kid_Child_t& c = child(v);
    mtb_Panel_Fill_Rect(0, 10, 255, 19, BLACK);
    mtb_Kit_Text(3, 12, Terminal4x6, K_GOLD, left, BLACK);
    char t[24];
    snprintf(t, sizeof(t), "%u:%02u", (unsigned)(c.gameSeconds / 60), (unsigned)(c.gameSeconds % 60));
    mtb_Kit_Text_Right(253, 12, Terminal4x6, c.gameSeconds < 30 ? KIT_RED : KIT_LABEL, t, BLACK);
}

static void catcher(const Kid_View_t& v){
    const Kid_Catch_t& g = v.play->catcher;
    char t[32];
    snprintf(t, sizeof(t), "SCORE %u", g.score);
    game_Bar(v, t);
    for (int i = 0; i < g.lives; i++) mtb_Panel_Fill_Circle(118 + i * 8, 14, 2, K_PINK);
    // A few still stars behind.
    for (int i = 0; i < 18; i++) mtb_Panel_Draw_PixelRGB565((int16_t)((i * 53 + 17) % 256), (int16_t)(22 + (i * 37) % 100), KIT_DIM);
    for (const auto& o : g.obj){
        if (o.kind == KID_NONE) continue;
        const int16_t y = (int16_t)(o.y >> 8);
        if (o.kind == KID_STAR) star(o.x, y, 5, K_GOLD);
        else if (o.kind == KID_HEART){ mtb_Panel_Fill_Circle(o.x - 2, y - 1, 2, K_PINK); mtb_Panel_Fill_Circle(o.x + 2, y - 1, 2, K_PINK); mtb_Panel_Fill_Triangle(o.x - 4, y, o.x + 4, y, o.x, y + 5, K_PINK); }
        else { mtb_Panel_Fill_Circle(o.x, y, 4, MTB_KIT_RGB(140, 110, 90)); mtb_Panel_Draw_Circle(o.x, y, 4, MTB_KIT_RGB(90, 70, 60)); }
    }
    const uint16_t bc = g.flash && g.lastHit == KID_ROCK ? KIT_RED : g.flash ? K_GOLD : K_SKY;
    mtb_Panel_Fill_Rect(g.basket - 13, 113, g.basket + 13, 116, bc);
    mtb_Panel_Fill_Rect(g.basket - 11, 117, g.basket + 11, 121, bc);
    if (g.over){
        mtb_Panel_Fill_Rect(48, 46, 207, 92, BLACK);
        mtb_Panel_Draw_Rect(48, 46, 207, 92, K_GOLD);
        mtb_Kit_Text_Centre(128, 52, Terminal8x12, K_GOLD, "GAME OVER", BLACK);
        mtb_Kit_Text_Centre(128, 68, Terminal6x8, KIT_TEXT, t, BLACK);
        mtb_Kit_Text_Centre(128, 80, Terminal4x6, KIT_DIM, "CLICK: AGAIN   HOLD: BACK", BLACK);
    }
}

static void cave(const Kid_View_t& v){
    const Kid_Cave_t& g = v.play->cave;
    char t[32];
    snprintf(t, sizeof(t), "%u M   GEMS %u", (unsigned)(g.distance / 10), g.gems);
    game_Bar(v, t);
    const uint16_t wall = mtb_Kit_Dim(K_VIOLET, 150), edge = K_VIOLET;
    for (int c = 0; c < KID_CAVE_COLUMNS; c++){
        const int16_t x = (int16_t)(c * 4 - g.shift);
        if (x > 255 || x + 3 < 0) continue;
        mtb_Panel_Fill_Rect(x < 0 ? 0 : x, KID_PLAY_TOP, x + 3 > 255 ? 255 : x + 3, g.top[c], wall);
        mtb_Panel_Fill_Rect(x < 0 ? 0 : x, g.bottom[c], x + 3 > 255 ? 255 : x + 3, KID_PLAY_BOTTOM, wall);
        mtb_Panel_Draw_HLine(g.top[c], x < 0 ? 0 : x, x + 3 > 255 ? 255 : x + 3, edge);
        mtb_Panel_Draw_HLine(g.bottom[c], x < 0 ? 0 : x, x + 3 > 255 ? 255 : x + 3, edge);
        if (g.gemY[c]) mtb_Panel_Fill_Triangle(x + 2, g.gemY[c] - 3, x - 1, g.gemY[c], x + 5, g.gemY[c], K_LIME), mtb_Panel_Fill_Triangle(x - 1, g.gemY[c], x + 5, g.gemY[c], x + 2, g.gemY[c] + 3, K_LIME);
    }
    // The glider.
    const int16_t sx = KID_SHIP_X, sy = g.shipY;
    mtb_Panel_Fill_Triangle(sx - 5, sy - 3, sx - 5, sy + 3, sx + 5, sy, g.over ? KIT_RED : KIT_TEXT);
    mtb_Panel_Fill_Rect(sx - 8, sy - 1, sx - 6, sy + 1, K_ORANGE);
    if (g.over){
        mtb_Panel_Fill_Rect(48, 46, 207, 92, BLACK);
        mtb_Panel_Draw_Rect(48, 46, 207, 92, K_SKY);
        mtb_Kit_Text_Centre(128, 52, Terminal8x12, K_SKY, "CRASH!", BLACK);
        snprintf(t, sizeof(t), "SCORE %u", (unsigned)kid_Cave_Score(g));
        mtb_Kit_Text_Centre(128, 68, Terminal6x8, KIT_TEXT, t, BLACK);
        mtb_Kit_Text_Centre(128, 80, Terminal4x6, KIT_DIM, "CLICK: AGAIN   HOLD: BACK", BLACK);
    }
}

// Stars, results, the end of the day ---------------------------------------------------------------------------------------------
static void stars(const Kid_View_t& v){
    const Kid_Child_t& c = child(v);
    header(v, "MY STARS");
    star(40, 50, 22, K_GOLD);
    char t[48];
    snprintf(t, sizeof(t), "%u", c.stars);
    mtb_Kit_Big_Text(72, 36, Terminal8x12, 3, K_GOLD, t);
    snprintf(t, sizeof(t), "%u %s IN A ROW", c.streak, c.streak == 1 ? "DAY" : "DAYS");
    mtb_Kit_Text(72, 70, Terminal6x8, c.streak ? K_ORANGE : KIT_LABEL, t, BLACK);
    snprintf(t, sizeof(t), "TODAY: %u RIGHT OF %u", c.todayRight, c.todayAsked);
    mtb_Kit_Text(8, 86, Terminal6x8, KIT_TEXT, t, BLACK);
    snprintf(t, sizeof(t), "READ %u OF %u MIN   BEST RACE %u", c.readWeek[0], v.settings->readGoal, c.bestRace);
    text_Fit(8, 98, Terminal4x6, KIT_LABEL, t, 244);
    mtb_Kit_Text_Centre(128, 112, Terminal4x6, KIT_DIM, "CLICK TO GO BACK", BLACK);
}

static void done(const Kid_View_t& v){
    const Kid_Play_t& p = *v.play;
    header(v, nullptr);
    star(36, 46, 16, K_GOLD);
    star(220, 46, 16, K_GOLD);
    char a[40];
    mtb_Kit_To_Ascii(a, sizeof(a), p.title);
    mtb_Kit_Text_Centre(128, 34, Terminal10x16, K_GOLD, a, BLACK);
    text_Fit(128 - mtb_Kit_Text_Width(Terminal6x8, p.line1) / 2, 62, Terminal6x8, KIT_TEXT, p.line1, 240);
    text_Fit(128 - mtb_Kit_Text_Width(Terminal6x8, p.line2) / 2, 76, Terminal6x8, K_LIME, p.line2, 240);
    mtb_Kit_Text_Centre(128, 108, Terminal4x6, KIT_DIM, "CLICK TO GO ON", BLACK);
}

static void locked(const Kid_View_t& v){
    const Kid_Play_t& p = *v.play;
    // A sleepy moon.
    mtb_Panel_Fill_Circle(48, 60, 22, K_GOLD);
    mtb_Panel_Fill_Circle(58, 52, 20, BLACK);
    star(96, 34, 4, KIT_TEXT); star(220, 40, 3, KIT_TEXT); star(200, 100, 3, KIT_TEXT);
    char a[48];
    mtb_Kit_To_Ascii(a, sizeof(a), p.title);
    mtb_Kit_Text_Centre(160, 46, Terminal8x12, KIT_TEXT, a, BLACK);
    mtb_Kit_To_Ascii(a, sizeof(a), p.line1);
    mtb_Kit_Text_Centre(160, 64, Terminal6x8, KIT_LABEL, a, BLACK);
    mtb_Kit_To_Ascii(a, sizeof(a), p.line2);
    mtb_Kit_Text_Centre(160, 76, Terminal6x8, KIT_LABEL, a, BLACK);
}

void kid_Draw(const Kid_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, BLACK);
    switch (v.play->screen){
        case KID_SCR_PICK: pick(v); break;
        case KID_SCR_MATH: maths(v, false); break;
        case KID_SCR_RACE: maths(v, true); break;
        case KID_SCR_SPELL: spelling(v); break;
        case KID_SCR_READ: reading(v); break;
        case KID_SCR_GAMES: games(v); break;
        case KID_SCR_CATCH: catcher(v); break;
        case KID_SCR_CAVE: cave(v); break;
        case KID_SCR_STARS: stars(v); break;
        case KID_SCR_DONE: done(v); break;
        case KID_SCR_LOCKED: locked(v); break;
        default: menu(v); break;
    }
    mtb_Panel_End_Update();
}
