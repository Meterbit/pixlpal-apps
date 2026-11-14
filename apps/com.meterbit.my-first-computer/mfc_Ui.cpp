// My First Computer's screens, the whole 256 x 128 panel: the desktop and taskbar, windows, menus and dialogs, each
// program, signing in, the power, the lock and the parent code. Drawing also lists what can be clicked (the hits).
#include "mfc_App.h"
#include "mtb_kit_ui.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define RGB MTB_KIT_RGB
static Mfc_State_t* S = nullptr;

// Colours of the windows (a dark theme: easy on the eyes on LEDs).
static const uint16_t C_BODY    = RGB(22, 24, 32);
static const uint16_t C_PANEL   = RGB(40, 43, 56);
static const uint16_t C_BUTTON  = RGB(64, 68, 86);
static const uint16_t C_HOVER   = RGB(96, 104, 132);
static const uint16_t C_EDGE    = RGB(120, 126, 150);
static const uint16_t C_TEXT    = RGB(235, 235, 240);
static const uint16_t C_SOFT    = RGB(160, 165, 180);
static const uint16_t C_FIELD   = RGB(245, 245, 240);
static const uint16_t C_INK     = RGB(20, 20, 24);
static const uint16_t C_LINK    = RGB(110, 180, 255);
static const uint16_t C_BAR     = RGB(28, 30, 44);

uint16_t mfc_Theme_Color(uint8_t t){
    static const uint16_t T[MFC_THEMES] = {RGB(40, 100, 220), RGB(30, 150, 80), RGB(130, 70, 200), RGB(220, 110, 30)};
    return T[t % MFC_THEMES];
}

uint16_t mfc_Paint_Color(uint8_t i){
    static const uint16_t P[MFC_PAINT_COLORS] = {
        RGB(0, 0, 0), RGB(128, 128, 128), RGB(230, 30, 30), RGB(255, 140, 0), RGB(255, 220, 0), RGB(40, 190, 60),
        RGB(0, 200, 220), RGB(40, 90, 230), RGB(140, 60, 200), RGB(255, 120, 180), RGB(130, 80, 40), RGB(210, 170, 110),
        RGB(255, 255, 255), RGB(0, 110, 50), RGB(170, 255, 60), RGB(20, 30, 110)};
    return P[i % MFC_PAINT_COLORS];
}

static uint16_t theme(void){ return mfc_Theme_Color(S->signedIn || S->mode == MFC_M_DESKTOP ? mfc_Me(*S).theme : 0); }

// Small helpers ---------------------------------------------------------------------------------------------------------
static void hit(int x0, int y0, int x1, int y1, uint16_t id){
    if (S->hitCount >= MFC_HITS) return;
    Mfc_Hit_t& h = S->hits[S->hitCount++];
    h.x0 = (int16_t)x0; h.y0 = (int16_t)y0; h.x1 = (int16_t)x1; h.y1 = (int16_t)y1; h.id = id;
}
static bool hov(uint16_t id){ return S->hover == id; }

static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void frame(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Draw_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void hline(int y, int x0, int x1, uint16_t c){ mtb_Panel_Draw_HLine((int16_t)y, (int16_t)x0, (int16_t)x1, c); }
static void vline(int x, int y0, int y1, uint16_t c){ mtb_Panel_Draw_VLine((int16_t)x, (int16_t)y0, (int16_t)y1, c); }
static void dot(int x, int y, uint16_t c){ if (x >= 0 && y >= 0 && x < MFC_W && y < MFC_H) mtb_Panel_Draw_PixelRGB565((int16_t)x, (int16_t)y, c); }
static void disc(int x, int y, int r, uint16_t c){ mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, (int16_t)r, c); }
static void ring(int x, int y, int r, uint16_t c){ mtb_Panel_Draw_Circle((int16_t)x, (int16_t)y, (int16_t)r, c); }
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c){ mtb_Panel_Fill_Triangle((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2, c); }
static void line(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Draw_Line((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void rrect(int x0, int y0, int x1, int y1, int r, uint16_t c){ mtb_Panel_Fill_Round_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (int16_t)r, c); }
static void rframe(int x0, int y0, int x1, int y1, int r, uint16_t c){ mtb_Panel_Draw_Round_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (int16_t)r, c); }

static int tw(const uint8_t* font, const char* t){ return mtb_Kit_Text_Width(font, t); }
/** Text with no background (the wallpaper shows through). Returns the x after it. */
static int txt(int x, int y, const uint8_t* font, uint16_t c, const char* t){ mtb_Kit_Big_Text((int16_t)x, (int16_t)y, font, 1, c, t); return x + tw(font, t); }
static void txt_C(int cx, int y, const uint8_t* font, uint16_t c, const char* t){ txt(cx - tw(font, t) / 2, y, font, c, t); }
static void txt_R(int rx, int y, const uint8_t* font, uint16_t c, const char* t){ txt(rx - tw(font, t) + 1, y, font, c, t); }

/** Characters in cells of @p cw pixels (so columns line up), @p n of them from @p t. */
static void cells(int x, int y, const uint8_t* font, int cw, uint16_t c, const char* t, int n){
    char g[2] = {0, 0};
    for (int i = 0; i < n && t[i]; i++){
        if (t[i] == ' ' || t[i] == '\n') continue;
        g[0] = t[i];
        const int w = tw(font, g);
        txt(x + i * cw + (cw - w) / 2, y, font, c, g);
    }
}

/** Wrapped text, @p maxLines lines of @p lineH from y. Returns the lines used. */
static int wrap(int x, int y, int w, const uint8_t* font, int lineH, uint16_t c, const char* t, int maxLines, bool centre = false){
    static char lines[8][64];
    const int n = mtb_Kit_Wrap(t, font, (int16_t)w, &lines[0][0], sizeof(lines[0]), maxLines > 8 ? 8 : maxLines);
    for (int i = 0; i < n; i++){
        if (centre) txt_C(x + w / 2, y + i * lineH, font, c, lines[i]);
        else txt(x, y + i * lineH, font, c, lines[i]);
    }
    return n;
}

/** A raised button. */
static void button(int x0, int y0, int x1, int y1, const char* label, uint16_t id, uint16_t face = C_BUTTON, const uint8_t* font = Terminal4x6){
    const bool h = hov(id);
    rrect(x0, y0, x1, y1, 2, h ? C_HOVER : face);
    hline(y0, x0 + 2, x1 - 2, h ? C_TEXT : C_EDGE);
    rframe(x0, y0, x1, y1, 2, h ? C_TEXT : RGB(30, 32, 40));
    const int fh = font[6];
    txt_C((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2 - fh / 2, font, C_TEXT, label);
    hit(x0, y0, x1, y1, id);
}

static void chip(int cx, int cy, int r, uint8_t child, const char* name){
    disc(cx, cy, r, kk_Child_Color(child));
    char t[2] = {name && name[0] ? (char)(name[0] >= 'a' && name[0] <= 'z' ? name[0] - 32 : name[0]) : '?', 0};
    const uint8_t* f = r >= 12 ? Terminal10x16 : r >= 6 ? Terminal6x8 : Terminal4x6;
    txt_C(cx, cy - f[6] / 2, f, RGB(255, 255, 255), t);
}

// Pictures: the wallpapers, the icons, the secret pictures -------------------------------------------------------------------
static void gradient(int x0, int y0, int x1, int y1, uint16_t top, uint16_t bottom){
    for (int y = y0; y <= y1; y++) hline(y, x0, x1, mtb_Kit_Blend(top, bottom, (float)(y - y0) / (float)(y1 - y0 > 0 ? y1 - y0 : 1)));
}

static void wallpaper(uint8_t w, int y1){
    switch (w % MFC_WALLPAPERS){
        case 0:     // hills
            gradient(0, 0, MFC_W - 1, y1, RGB(70, 140, 240), RGB(170, 215, 255));
            disc(212, 26, 12, RGB(255, 220, 60));
            disc(60, y1 + 70, 100, RGB(60, 170, 70));
            disc(200, y1 + 80, 110, RGB(40, 140, 60));
            for (int i = 0; i < 3; i++){ const int x = 40 + i * 70, y = 22 + (i % 2) * 10; disc(x, y, 6, RGB(250, 250, 255)); disc(x + 7, y - 2, 7, RGB(250, 250, 255)); disc(x + 14, y, 5, RGB(250, 250, 255)); }
            break;
        case 1: {   // space
            gradient(0, 0, MFC_W - 1, y1, RGB(4, 6, 24), RGB(30, 16, 60));
            uint32_t r = 12345;
            for (int i = 0; i < 90; i++){ r = r * 1103515245u + 12345u; const int x = (int)((r >> 8) % MFC_W); r = r * 1103515245u + 12345u; const int y = (int)((r >> 8) % (y1 + 1)); dot(x, y, (i % 7) ? RGB(200, 200, 230) : RGB(255, 240, 160)); }
            disc(196, 40, 16, RGB(220, 120, 60));
            disc(190, 34, 5, RGB(240, 160, 100));
            line(170, 46, 222, 32, RGB(240, 210, 150));
            line(170, 47, 222, 33, RGB(200, 170, 110));
            break;
        }
        case 2:     // sea
            gradient(0, 0, MFC_W - 1, y1, RGB(20, 130, 200), RGB(4, 30, 90));
            for (int k = 0; k < 6; k++){
                const int y = 14 + k * 17;
                for (int x = 0; x < MFC_W; x += 2) dot(x, y + (int)(2 * sinf((x + k * 13) * 0.08f)), RGB(120, 200, 240));
            }
            for (int i = 0; i < 4; i++){ const int x = 30 + i * 60, y = 40 + (i % 2) * 30; disc(x, y, 4, RGB(255, 150, 40)); tri(x - 4, y, x - 9, y - 4, x - 9, y + 4, RGB(255, 150, 40)); dot(x + 2, y - 1, BLACK); }
            break;
        case 3:     // sunset
            gradient(0, 0, MFC_W - 1, y1, RGB(250, 120, 40), RGB(110, 40, 120));
            disc(128, y1 - 20, 22, RGB(255, 210, 80));
            rect(0, y1 - 18, MFC_W - 1, y1, RGB(50, 20, 50));
            for (int i = 0; i < 5; i++){ const int x = 20 + i * 52; tri(x, y1 - 18, x + 8, y1 - 40 - (i % 2) * 8, x + 16, y1 - 18, RGB(50, 20, 50)); }
            break;
        case 4:     // plain blue
            rect(0, 0, MFC_W - 1, y1, RGB(20, 60, 140));
            txt_C(128, y1 / 2 - 8, Terminal10x16, RGB(60, 110, 200), "PIXLPAL");
            break;
        default: {  // dots
            rect(0, 0, MFC_W - 1, y1, RGB(24, 24, 36));
            static const uint16_t D[5] = {RGB(230, 60, 60), RGB(250, 200, 40), RGB(60, 200, 90), RGB(70, 150, 255), RGB(200, 90, 220)};
            for (int y = 8; y < y1; y += 16) for (int x = 8 + (y / 16 % 2) * 8; x < MFC_W; x += 16) disc(x, y, 2, D[(x / 16 + y / 16) % 5]);
            break;
        }
    }
}

/** A program's picture, 22 x 18 from (x, y). */
static void icon(uint8_t p, int x, int y){
    switch (p){
        case MFC_P_MOUSE:
            rrect(x + 6, y + 3, x + 16, y + 17, 5, RGB(230, 230, 235));
            vline(x + 11, y + 3, y + 8, RGB(120, 120, 130));
            hline(y + 8, x + 6, x + 16, RGB(120, 120, 130));
            line(x + 11, y + 3, x + 14, y, RGB(160, 160, 170));
            break;
        case MFC_P_KEYS:
            rrect(x, y + 5, x + 21, y + 16, 2, RGB(70, 74, 90));
            for (int r = 0; r < 2; r++) for (int c = 0; c < 6; c++) rect(x + 2 + c * 3 + r, y + 7 + r * 3, x + 3 + c * 3 + r, y + 8 + r * 3, RGB(230, 230, 235));
            rect(x + 6, y + 13, x + 15, y + 14, RGB(230, 230, 235));
            break;
        case MFC_P_PAINT:
            disc(x + 10, y + 9, 8, RGB(220, 180, 120));
            disc(x + 7, y + 6, 2, RGB(230, 30, 30)); disc(x + 13, y + 5, 2, RGB(40, 90, 230)); disc(x + 15, y + 10, 2, RGB(40, 190, 60)); disc(x + 6, y + 12, 2, RGB(255, 220, 0));
            line(x + 13, y + 17, x + 21, y + 4, RGB(150, 90, 40));
            line(x + 14, y + 17, x + 21, y + 5, RGB(150, 90, 40));
            break;
        case MFC_P_WRITE:
            rect(x + 4, y + 1, x + 17, y + 17, RGB(245, 245, 240));
            for (int i = 0; i < 4; i++) hline(y + 5 + i * 3, x + 6, x + 15, RGB(120, 140, 200));
            line(x + 13, y + 15, x + 21, y + 7, RGB(255, 200, 40));
            line(x + 14, y + 16, x + 21, y + 8, RGB(255, 200, 40));
            break;
        case MFC_P_FILES:
            rect(x + 1, y + 4, x + 9, y + 6, RGB(230, 180, 40));
            rect(x + 1, y + 6, x + 21, y + 17, RGB(250, 205, 60));
            hline(y + 7, x + 1, x + 21, RGB(255, 230, 130));
            break;
        case MFC_P_CALC:
            rrect(x + 4, y, x + 18, y + 17, 2, RGB(70, 74, 90));
            rect(x + 6, y + 2, x + 16, y + 5, RGB(150, 220, 160));
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) rect(x + 6 + c * 4, y + 8 + r * 3, x + 8 + c * 4, y + 9 + r * 3, c == 2 && r == 2 ? RGB(255, 140, 0) : RGB(220, 220, 225));
            break;
        case MFC_P_PIANO:
            rect(x, y + 4, x + 21, y + 17, RGB(245, 245, 245));
            for (int i = 1; i < 6; i++) vline(x + i * 4 - 1, y + 4, y + 17, RGB(80, 80, 80));
            for (int i = 1; i <= 4; i++) if (i != 3) rect(x + i * 4 - 2, y + 4, x + i * 4, y + 11, BLACK);
            break;
        case MFC_P_WEB:
            disc(x + 11, y + 9, 8, RGB(50, 120, 230));
            disc(x + 8, y + 6, 3, RGB(60, 180, 80)); disc(x + 14, y + 12, 3, RGB(60, 180, 80));
            ring(x + 11, y + 9, 8, RGB(170, 210, 255));
            hline(y + 9, x + 3, x + 19, RGB(170, 210, 255));
            break;
        case MFC_P_SETTINGS:
            for (int k = 0; k < 8; k++){ const float a = k * 0.785f; disc(x + 11 + (int)(7 * cosf(a)), y + 9 + (int)(7 * sinf(a)), 2, RGB(170, 175, 190)); }
            disc(x + 11, y + 9, 6, RGB(170, 175, 190));
            disc(x + 11, y + 9, 2, RGB(40, 43, 56));
            break;
        case MFC_P_LICENCE:
            rrect(x + 1, y + 2, x + 21, y + 16, 2, RGB(60, 120, 200));
            kk_Star((int16_t)(x + 7), (int16_t)(y + 9), 4, KIT_GOLD);
            hline(y + 7, x + 13, x + 19, RGB(230, 230, 240)); hline(y + 10, x + 13, x + 18, RGB(230, 230, 240));
            break;
        case MFC_P_BIN: {
            const bool full = S->fileCount > 0 && S->signedIn;
            rect(x + 5, y + 3, x + 17, y + 4, RGB(170, 175, 185));
            tri(x + 5, y + 5, x + 17, y + 5, x + 15, y + 17, RGB(140, 145, 160));
            tri(x + 5, y + 5, x + 7, y + 17, x + 15, y + 17, RGB(140, 145, 160));
            for (int i = 0; i < 3; i++) vline(x + 9 + i * 2, y + 7, y + 15, RGB(100, 104, 120));
            if (full) disc(x + 11, y + 2, 2, RGB(245, 245, 240));
            break;
        }
    }
}

static void picture(uint8_t i, int x, int y){          // the secret pictures, 30 x 30
    const int cx = x + 15, cy = y + 15;
    switch (i){
        case 0: disc(cx, cy + 2, 9, RGB(250, 160, 60)); tri(cx - 9, cy - 2, cx - 6, cy - 13, cx - 2, cy - 6, RGB(250, 160, 60)); tri(cx + 9, cy - 2, cx + 6, cy - 13, cx + 2, cy - 6, RGB(250, 160, 60));
                dot(cx - 3, cy, BLACK); dot(cx + 3, cy, BLACK); dot(cx - 3, cy + 1, BLACK); dot(cx + 3, cy + 1, BLACK); break;
        case 1: kk_Star((int16_t)cx, (int16_t)cy, 12, KIT_GOLD); break;
        case 2: for (int k = 0; k < 8; k++){ const float a = k * 0.785f; line(cx, cy, cx + (int)(13 * cosf(a)), cy + (int)(13 * sinf(a)), RGB(255, 200, 0)); } disc(cx, cy, 7, RGB(255, 220, 40)); break;
        case 3: disc(cx - 5, cy - 3, 6, RGB(240, 50, 80)); disc(cx + 5, cy - 3, 6, RGB(240, 50, 80)); tri(cx - 11, cy - 1, cx + 11, cy - 1, cx, cy + 11, RGB(240, 50, 80)); break;
        case 4: disc(cx - 2, cy, 8, RGB(60, 160, 240)); tri(cx + 4, cy, cx + 13, cy - 7, cx + 13, cy + 7, RGB(60, 160, 240)); dot(cx - 6, cy - 2, BLACK); break;
        default: rect(cx - 2, cy + 3, cx + 2, cy + 13, RGB(130, 80, 40)); disc(cx, cy - 3, 10, RGB(50, 170, 70)); break;
    }
}

// The pointer -------------------------------------------------------------------------------------------------------------------
static const uint16_t ARROW[12] = {          // 1 white, 2 black outline, by 2 bits a pixel, 8 pixels a row
    0x0002, 0x000A, 0x0026, 0x0096, 0x0256, 0x0956, 0x2556, 0x0A96, 0x0096, 0x0216, 0x0218, 0x0080};

static void pointer(void){
    if (S->mode == MFC_M_BOOT || S->mode == MFC_M_SHUTDOWN || S->mode == MFC_M_LOCK) return;
    const int x = S->px, y = S->py;
    const int k = S->signedIn && mfc_Me(*S).bigPointer ? 2 : 1;
    auto px = [&](int dx, int dy, uint16_t c){ for (int a = 0; a < k; a++) for (int b = 0; b < k; b++) dot(x + dx * k + a, y + dy * k + b, c); };
    switch (S->pointer){
        case MFC_PTR_HAND:
            for (int dy = 0; dy < 4; dy++){ px(2, dy, C_TEXT); px(1, dy, BLACK); px(3, dy, BLACK); }
            for (int dy = 4; dy < 10; dy++) for (int dx = 0; dx < 8; dx++) px(dx, dy, (dx == 0 || dx == 7 || dy == 9) ? BLACK : C_TEXT);
            px(4, 5, BLACK); px(6, 5, BLACK);
            break;
        case MFC_PTR_TEXT:
            for (int dy = 0; dy < 10; dy++) px(2, dy, C_TEXT);
            px(1, 0, C_TEXT); px(3, 0, C_TEXT); px(1, 9, C_TEXT); px(3, 9, C_TEXT);
            for (int dy = 0; dy < 10; dy++){ px(1, dy == 0 || dy == 9 ? 10 : dy, BLACK); }
            break;
        case MFC_PTR_PEN: {
            const uint16_t c = S->paint.tool == T_ERASER ? RGB(255, 120, 200) : mfc_Paint_Color(S->paint.color);
            px(0, 0, c);
            for (int d = 2; d <= 3; d++){ px(-d, 0, BLACK); px(d, 0, BLACK); px(0, -d, BLACK); px(0, d, BLACK); }
            break;
        }
        case MFC_PTR_WAIT:
            for (int dy = 0; dy < 10; dy++){ const int w = dy < 5 ? 4 - dy : dy - 5; for (int dx = -w; dx <= w; dx++) px(4 + dx, dy, (dy % 9 == 0 || dx == -w || dx == w) ? BLACK : RGB(255, 210, 80)); }
            break;
        default:
            for (int dy = 0; dy < 12; dy++)
                for (int dx = 0; dx < 8; dx++){
                    const int v = (ARROW[dy] >> (dx * 2)) & 3;
                    if (v == 1) px(dx, dy, C_TEXT);
                    else if (v == 2) px(dx, dy, BLACK);
                }
            break;
    }
}

// Windows, menus, dialogs ----------------------------------------------------------------------------------------------------------
static void window(const char* title, uint8_t prog){
    rect(0, 0, MFC_W - 1, MFC_BAR_Y - 1, C_BODY);
    const uint16_t t = theme();
    rect(0, 0, MFC_W - 1, MFC_TOP - 1, t);
    hline(0, 0, MFC_W - 1, mtb_Kit_Blend(t, RGB(255, 255, 255), 0.4f));
    char shown[40];
    mtb_Kit_Fit(shown, sizeof(shown), Terminal6x8, title, 200);
    txt(3, 1, Terminal6x8, RGB(255, 255, 255), shown);
    // Minimise and close.
    const uint16_t idMin = MFC_HIT(H_MIN, 0), idClose = MFC_HIT(H_CLOSE, 0);
    rect(231, 1, 241, 8, hov(idMin) ? C_HOVER : mtb_Kit_Dim(t, 150));
    hline(6, 234, 238, RGB(255, 255, 255));
    hit(230, 0, 241, 9, idMin);
    rect(243, 1, 254, 8, hov(idClose) ? RGB(230, 40, 40) : RGB(170, 50, 50));
    line(246, 2, 251, 7, RGB(255, 255, 255)); line(251, 2, 246, 7, RGB(255, 255, 255));
    hit(243, 0, 255, 9, idClose);
    (void)prog;
}

static void menu_Bar(const char* const* names, int n){
    rect(0, MFC_TOP, MFC_W - 1, MFC_TOP + 9, C_PANEL);
    for (int i = 0; i < n; i++){
        const int x = 2 + i * 30;
        const uint16_t id = MFC_HIT(H_MENU, i);
        const bool open = S->menu.open && S->menu.which == i;
        if (open || hov(id)) rect(x, MFC_TOP + 1, x + 27, MFC_TOP + 8, open ? theme() : C_HOVER);
        txt(x + 3, MFC_TOP + 2, Terminal4x6, C_TEXT, names[i]);
        hit(x, MFC_TOP, x + 27, MFC_TOP + 9, id);
    }
}

static void menu_List(const Mfc_Menu_t& m, uint8_t kind){
    int w = 60;
    for (int i = 0; i < m.count; i++){ const int k = tw(Terminal4x6, m.label[i]) + 12; if (k > w) w = k; }
    const int h = m.count * 10 + 2;
    int x = m.x, y = m.y;
    if (x + w >= MFC_W) x = MFC_W - 1 - w;
    if (y + h >= MFC_BAR_Y) y = MFC_BAR_Y - 1 - h;
    rect(x + 2, y + 2, x + w + 2, y + h + 2, RGB(8, 8, 12));
    rect(x, y, x + w, y + h, C_PANEL);
    frame(x, y, x + w, y + h, C_EDGE);
    for (int i = 0; i < m.count; i++){
        const uint16_t id = MFC_HIT(kind, i);
        const int iy = y + 1 + i * 10;
        if (hov(id)) rect(x + 1, iy, x + w - 1, iy + 9, theme());
        txt(x + 6, iy + 2, Terminal4x6, C_TEXT, m.label[i]);
        hit(x, iy, x + w, iy + 9, id);
    }
}

static void dialog_Icon(uint8_t i, int x, int y){
    switch (i){
        case 1: disc(x + 7, y + 7, 7, RGB(60, 130, 230)); txt_C(x + 7, y + 3, Terminal6x8, RGB(255, 255, 255), "?"); break;
        case 2: tri(x + 7, y, x, y + 14, x + 14, y + 14, RGB(250, 190, 30)); txt_C(x + 7, y + 5, Terminal6x8, BLACK, "!"); break;
        case 3: kk_Star((int16_t)(x + 7), (int16_t)(y + 7), 7, KIT_GOLD); break;
        default: disc(x + 7, y + 7, 7, RGB(60, 130, 230)); txt_C(x + 7, y + 3, Terminal6x8, RGB(255, 255, 255), "i"); break;
    }
}

static void dialog(void){
    const Mfc_Dialog_t& d = S->dlg;
    const int x0 = 26, x1 = 229;
    // How tall: the text, a field or a list, the buttons.
    static char lines[5][64];
    const int textLines = d.text[0] ? mtb_Kit_Wrap(d.text, Terminal6x8, 160, &lines[0][0], sizeof(lines[0]), 5) : 0;
    const bool list = d.kind == D_OPEN && S->files.count > 0;
    const int body = (textLines ? textLines * 9 + 4 : 0) + (d.fieldOn ? 16 : 0) + (list ? 5 * 9 + 4 : 0);
    int h = 12 + (body < 18 ? 18 : body) + 18;
    if (h > 110) h = 110;
    const int y0 = (MFC_BAR_Y - h) / 2, y1 = y0 + h;
    rect(x0 + 3, y0 + 3, x1 + 3, y1 + 3, RGB(6, 6, 10));
    rect(x0, y0, x1, y1, C_PANEL);
    frame(x0, y0, x1, y1, C_EDGE);
    rect(x0, y0, x1, y0 + 9, theme());
    txt(x0 + 3, y0 + 1, Terminal6x8, RGB(255, 255, 255), d.title);
    int y = y0 + 14;
    dialog_Icon(d.icon, x0 + 6, y);
    const int tx = x0 + 28;
    for (int i = 0; i < textLines; i++) txt(tx, y + i * 9, Terminal6x8, C_TEXT, lines[i]);
    y += textLines ? textLines * 9 + 4 : 0;
    if (d.fieldOn){
        rect(tx, y, x1 - 8, y + 12, C_FIELD);
        frame(tx, y, x1 - 8, y + 12, theme());
        const int ex = txt(tx + 3, y + 3, Terminal6x8, C_INK, d.field);
        if ((S->ms / 500) % 2 == 0) vline(ex + 1, y + 2, y + 10, C_INK);
        hit(tx, y, x1 - 8, y + 12, MFC_HIT(H_DLG_FIELD, 0));
        y += 16;
    }
    if (list){
        const int top = S->files.top < S->files.count ? S->files.top : 0;
        for (int r = 0; r < 5 && top + r < S->files.count; r++){
            const int i = top + r;
            const uint16_t id = MFC_HIT(H_DLG_ITEM, i);
            const int ry = y + r * 9;
            const bool sel = d.chosen == i;
            if (sel || hov(id)) rect(tx, ry, x1 - 8, ry + 8, sel ? theme() : C_HOVER);
            char shown[MFC_NAME_LEN + 1];
            mfc_Name_Show(S->files.list[i].name, shown, sizeof(shown));
            rect(tx + 2, ry + 2, tx + 6, ry + 6, S->files.list[i].kind ? RGB(245, 245, 240) : RGB(250, 160, 60));
            txt(tx + 10, ry + 1, Terminal6x8, C_TEXT, shown);
            hit(tx, ry, x1 - 8, ry + 8, id);
        }
        y += 5 * 9 + 4;
    }
    // The buttons, on the right.
    int bx = x1 - 6;
    for (int b = d.buttons - 1; b >= 0; b--){
        const int w = tw(Terminal4x6, d.button[b]) + 12 < 40 ? 40 : tw(Terminal4x6, d.button[b]) + 12;
        button(bx - w, y1 - 15, bx, y1 - 4, d.button[b], MFC_HIT(H_DLG_BTN, b), b == 0 ? mtb_Kit_Dim(theme(), 200) : C_BUTTON);
        bx -= w + 4;
    }
}

// The desktop -------------------------------------------------------------------------------------------------------------------------
static void icon_Place(int n, int& x, int& y){ x = 4 + (n % 5) * 50; y = 3 + (n / 5) * 37; }

static void desktop(void){
    Mfc_Child_t& me = mfc_Me(*S);
    wallpaper(me.wallpaper, MFC_BAR_Y - 1);
    hit(0, 0, MFC_W - 1, MFC_BAR_Y - 1, MFC_HIT(H_DESKTOP, 0));
    int n = 0;
    for (uint8_t p = 0; p < MFC_PROGRAMS; p++){
        if (!mfc_Program_Open(*S->set, me, mfc_Age(*S), p)) continue;
        int x, y;
        icon_Place(n++, x, y);
        const uint16_t id = MFC_HIT(H_ICON, p);
        const bool sel = S->selectedIcon == (int8_t)p, h = hov(id);
        if (sel || h) rrect(x, y, x + 47, y + 33, 3, sel ? mtb_Kit_Dim(theme(), 170) : RGB(60, 70, 110));
        icon(p, x + 13, y + 2);
        const char* name = mfc_Program_Name(p);
        const int w = tw(Terminal4x6, name);
        // A dark band behind the name so it reads on any wallpaper.
        if (!sel && !h) rect(x + 24 - w / 2 - 1, y + 23, x + 24 + (w + 1) / 2, y + 30, RGB(10, 12, 20));
        txt_C(x + 24, y + 24, Terminal4x6, RGB(255, 255, 255), name);
        hit(x, y, x + 47, y + 33, id);
    }
}

static void taskbar(void){
    rect(0, MFC_BAR_Y, MFC_W - 1, MFC_H - 1, C_BAR);
    hline(MFC_BAR_Y, 0, MFC_W - 1, C_EDGE);
    const uint16_t idStart = MFC_HIT(H_START, 0);
    const uint16_t t = theme();
    rrect(1, MFC_BAR_Y + 2, 37, MFC_H - 2, 2, S->startOpen ? mtb_Kit_Dim(t, 160) : hov(idStart) ? mtb_Kit_Blend(t, RGB(255, 255, 255), 0.25f) : t);
    rect(4, MFC_BAR_Y + 3, 6, MFC_BAR_Y + 5, RGB(240, 80, 60)); rect(8, MFC_BAR_Y + 3, 10, MFC_BAR_Y + 5, RGB(80, 220, 90));
    rect(4, MFC_BAR_Y + 7, 6, MFC_BAR_Y + 9, RGB(70, 150, 255)); rect(8, MFC_BAR_Y + 7, 10, MFC_BAR_Y + 9, RGB(255, 210, 50));
    txt(13, MFC_BAR_Y + 4, Terminal4x6, RGB(255, 255, 255), "START");
    hit(0, MFC_BAR_Y, 38, MFC_H - 1, idStart);
    if (S->prog != MFC_PROGRAMS){
        const uint16_t id = MFC_HIT(H_TASK, 0);
        const int x0 = 42, x1 = 132;
        rrect(x0, MFC_BAR_Y + 2, x1, MFC_H - 2, 2, S->minimized ? (hov(id) ? C_HOVER : C_BUTTON) : RGB(80, 90, 120));
        if (!S->minimized) hline(MFC_H - 2, x0 + 3, x1 - 3, t);
        txt(x0 + 4, MFC_BAR_Y + 4, Terminal4x6, C_TEXT, mfc_Program_Name(S->prog));
        hit(x0, MFC_BAR_Y, x1, MFC_H - 1, id);
    }
    char clock[12];
    mfc_Clock_Text(clock, sizeof(clock));
    txt_R(238, MFC_BAR_Y + 4, Terminal4x6, C_TEXT, clock);
    chip(249, MFC_BAR_Y + 6, 5, S->k->child, kk_Name(*S->k));
}

static void start_Menu(void){
    Mfc_Child_t& me = mfc_Me(*S);
    int list[MFC_PROGRAMS], n = 0;
    for (uint8_t p = 0; p < MFC_PROGRAMS; p++) if (mfc_Program_Open(*S->set, me, mfc_Age(*S), p)) list[n++] = p;
    const int x0 = 0, x1 = 150, h = 13 + n * 9 + 2;
    const int y1 = MFC_BAR_Y - 1, y0 = y1 - (h < 46 ? 46 : h);
    rect(x0, y0, x1, y1, C_PANEL);
    frame(x0, y0, x1, y1, C_EDGE);
    rect(x0 + 1, y0 + 1, x1 - 1, y0 + 11, theme());
    chip(x0 + 7, y0 + 6, 4, S->k->child, kk_Name(*S->k));
    txt(x0 + 14, y0 + 3, Terminal4x6, RGB(255, 255, 255), kk_Name(*S->k));
    for (int i = 0; i < n; i++){
        const uint16_t id = MFC_HIT(H_START_ITEM, list[i]);
        const int y = y0 + 13 + i * 9;
        if (hov(id)) rect(x0 + 1, y, 96, y + 8, C_HOVER);
        rect(x0 + 4, y + 2, x0 + 8, y + 6, mfc_Theme_Color((uint8_t)list[i]));
        txt(x0 + 12, y + 2, Terminal4x6, C_TEXT, mfc_Program_Name((uint8_t)list[i]));
        hit(x0 + 1, y, 96, y + 8, id);
    }
    vline(98, y0 + 13, y1 - 2, C_EDGE);
    button(102, y1 - 30, x1 - 4, y1 - 19, "SIGN OUT", MFC_HIT(H_START_ITEM, 60));
    button(102, y1 - 15, x1 - 4, y1 - 4, "SHUT DOWN", MFC_HIT(H_START_ITEM, 61), RGB(150, 50, 50));
}

static void toast(void){
    if (!S->toast[0]) return;
    const int w = tw(Terminal4x6, S->toast) + 12;
    const int x0 = 128 - w / 2, y0 = MFC_BAR_Y - 14;
    rrect(x0, y0, x0 + w, y0 + 10, 3, RGB(10, 10, 16));
    rframe(x0, y0, x0 + w, y0 + 10, 3, KIT_GOLD);
    txt(x0 + 6, y0 + 3, Terminal4x6, C_TEXT, S->toast);
}

static void badge_Show(void){
    if (S->badge < 0) return;
    const uint32_t t = S->ms - S->badgeMs;
    rrect(38, 22, 218, 92, 6, RGB(16, 18, 40));
    rframe(38, 22, 218, 92, 6, KIT_GOLD);
    txt_C(128, 27, Terminal6x8, KIT_GOLD, "NEW BADGE!");
    kk_Star(128, 52, (int16_t)(10 + (t < 600 ? (int)(t / 120) : 5)), KIT_GOLD);
    txt_C(128, 68, Terminal8x12, RGB(255, 255, 255), mfc_Badge_Name((uint8_t)S->badge));
    txt_C(128, 83, Terminal4x6, C_SOFT, "FOR YOUR COMPUTER LICENCE");
    kk_Confetti(t, (uint32_t)S->badge * 7 + 1, 0);
}

// Mouse School -------------------------------------------------------------------------------------------------------------------------
static void animal(uint8_t kind, int x, int y){
    switch (kind & 3){
        case 0: rrect(x - 8, y - 5, x + 8, y + 5, 3, RGB(245, 245, 245)); disc(x - 3, y - 1, 2, BLACK); disc(x + 4, y + 2, 2, BLACK); disc(x + 9, y - 4, 3, RGB(245, 245, 245)); break;   // cow
        case 1: rrect(x - 8, y - 5, x + 8, y + 5, 4, RGB(250, 160, 180)); disc(x + 9, y - 2, 3, RGB(250, 160, 180)); dot(x + 10, y - 2, RGB(180, 80, 100)); break;              // pig
        case 2: for (int i = -1; i <= 1; i++) disc(x + i * 5, y, 5, RGB(235, 235, 225)); disc(x + 10, y - 2, 3, RGB(60, 60, 60)); break;                                                  // sheep
        default: disc(x, y + 1, 6, RGB(255, 220, 40)); disc(x + 5, y - 5, 4, RGB(255, 220, 40)); tri(x + 8, y - 5, x + 12, y - 4, x + 8, y - 3, RGB(255, 140, 0)); break;             // duck
    }
}

static void lesson_Menu(void){
    static const char* const N[MFC_LESSONS] = {"POINTING", "CLICKING", "DOUBLE CLICK", "DRAG AND DROP", "SCROLLING", "RIGHT CLICK"};
    static const uint8_t B[MFC_LESSONS] = {MFC_B_POINT, MFC_B_CLICK, MFC_B_DOUBLE, MFC_B_DRAG, MFC_B_SCROLL, MFC_B_RIGHT};
    txt_C(128, MFC_TOP + 3, Terminal6x8, C_TEXT, "CHOOSE A LESSON");
    const uint8_t lv = mfc_Level(mfc_Me(*S), mfc_Age(*S));
    for (int i = 0; i < MFC_LESSONS; i++){
        const int x = 6 + (i % 3) * 83, y = MFC_TOP + 14 + (i / 3) * 46;
        const bool open = i == L_SCROLL ? lv >= 2 : i == L_RIGHT ? lv >= 3 : true;
        const uint16_t id = MFC_HIT(H_LESSON, i);
        rrect(x, y, x + 78, y + 42, 4, hov(id) ? C_HOVER : C_PANEL);
        rframe(x, y, x + 78, y + 42, 4, open ? theme() : C_BUTTON);
        const int cx = x + 39, cy = y + 16;
        switch (i){
            case L_POINT: kk_Star((int16_t)cx, (int16_t)cy, 8, open ? KIT_GOLD : C_BUTTON); break;
            case L_CLICK: disc(cx, cy - 2, 7, open ? RGB(230, 60, 60) : C_BUTTON); line(cx, cy + 5, cx + 2, cy + 12, C_SOFT); break;
            case L_DOUBLE: rect(cx - 8, cy - 6, cx + 8, cy + 8, open ? RGB(60, 150, 230) : C_BUTTON); vline(cx, cy - 6, cy + 8, KIT_GOLD); hline(cy, cx - 8, cx + 8, KIT_GOLD); break;
            case L_DRAG: animal(0, cx - 4, cy); line(cx + 8, cy, cx + 16, cy, C_SOFT); tri(cx + 16, cy - 3, cx + 16, cy + 3, cx + 20, cy, C_SOFT); break;
            case L_SCROLL: rect(cx - 10, cy - 8, cx + 10, cy + 8, open ? RGB(20, 90, 160) : C_BUTTON); rect(cx - 3, cy + 2, cx + 3, cy + 6, KIT_GOLD); break;
            default: disc(cx - 4, cy, 5, open ? RGB(230, 60, 60) : C_BUTTON); disc(cx + 6, cy, 5, open ? RGB(60, 120, 230) : C_BUTTON); break;
        }
        txt_C(cx, y + 29, Terminal4x6, open ? C_TEXT : C_SOFT, N[i]);
        if (!open) txt_C(cx, y + 35, Terminal4x6, C_SOFT, i == L_SCROLL ? "LEVEL 2" : "LEVEL 3");
        else if (mfc_Has(mfc_Me(*S), B[i])) kk_Star((int16_t)(x + 70), (int16_t)(y + 7), 4, KIT_GOLD);
        hit(x, y, x + 78, y + 42, id);
    }
}

static void lesson_Done_Panel(void){
    rrect(48, 34, 208, 98, 6, RGB(16, 18, 40));
    rframe(48, 34, 208, 98, 6, KIT_GOLD);
    txt_C(128, 40, Terminal8x12, KIT_GOLD, "WELL DONE!");
    for (int i = 0; i < 3; i++) kk_Star((int16_t)(104 + i * 24), 64, 8, KIT_GOLD);
    button(58, 80, 124, 92, "MORE LESSONS", MFC_HIT(H_BTN, 0));
    button(132, 80, 198, 92, "AGAIN", MFC_HIT(H_BTN, 1));
}

static void mouse_School(void){
    Mfc_Mouse_School_t& m = S->ms_;
    if (m.lesson < 0){ lesson_Menu(); return; }
    const int top = MFC_TOP + 11;
    rect(0, MFC_TOP, MFC_W - 1, top - 1, C_PANEL);
    txt(3, MFC_TOP + 2, Terminal4x6, KIT_GOLD, m.hint);
    char t[16];
    snprintf(t, sizeof(t), "%u/%u", m.done < m.goal ? m.done : m.goal, m.goal);
    txt_R(252, MFC_TOP + 2, Terminal4x6, C_TEXT, t);
    switch (m.lesson){
        case L_POINT:
            for (int i = 0; i < m.count; i++){
                kk_Star(m.t[i].x, m.t[i].y, 9, m.t[i].state ? KIT_GOLD : RGB(70, 70, 90));
                if (!m.t[i].state) kk_Star(m.t[i].x, m.t[i].y, 4, RGB(150, 150, 60));
                hit(m.t[i].x - 10, m.t[i].y - 10, m.t[i].x + 10, m.t[i].y + 10, MFC_HIT(H_ITEM, i));
            }
            break;
        case L_CLICK: {
            static const uint16_t BC[6] = {RGB(230, 50, 50), RGB(60, 140, 240), RGB(250, 200, 40), RGB(60, 200, 90), RGB(220, 90, 220), RGB(255, 140, 30)};
            for (int i = 0; i < m.count; i++){
                const Mfc_Thing_t& b = m.t[i];
                if (b.y > MFC_BAR_Y + 8 || b.y < top - 8) continue;
                if (b.state){ for (int k = 0; k < 8; k++){ const float a = k * 0.785f; dot(b.x + (int)(9 * cosf(a)), b.y + (int)(9 * sinf(a)), BC[b.kind % 6]); } continue; }
                line(b.x, b.y + 8, b.x + 2, b.y + 16, C_SOFT);
                disc(b.x, b.y, 8, BC[b.kind % 6]);
                disc(b.x - 3, b.y - 3, 2, RGB(255, 255, 255));
                if (b.y >= top && b.y < MFC_BAR_Y) hit(b.x - 9, b.y - 9, b.x + 9, b.y + 9, MFC_HIT(H_ITEM, i));
            }
            break;
        }
        case L_DOUBLE:
            for (int i = 0; i < m.count; i++){
                const Mfc_Thing_t& p = m.t[i];
                const int wob = (p.kind & 0x80) ? ((S->ms / 60) % 2 ? 2 : -2) : 0;
                const int x = p.x + wob, y = p.y;
                static const uint16_t PC[4] = {RGB(230, 60, 60), RGB(60, 140, 240), RGB(60, 190, 90), RGB(200, 90, 220)};
                if (p.state){
                    rect(x - 12, y - 4, x + 12, y + 12, PC[p.kind & 3]);
                    kk_Star((int16_t)x, (int16_t)(y - 12), 8, KIT_GOLD);
                } else {
                    rect(x - 12, y - 10, x + 12, y + 12, PC[p.kind & 3]);
                    vline(x, y - 10, y + 12, KIT_GOLD); vline(x + 1, y - 10, y + 12, KIT_GOLD);
                    hline(y, x - 12, x + 12, KIT_GOLD);
                    disc(x - 3, y - 12, 3, KIT_GOLD); disc(x + 4, y - 12, 3, KIT_GOLD);
                    hit(x - 13, y - 16, x + 13, y + 13, MFC_HIT(H_ITEM, i));
                }
            }
            break;
        case L_DRAG: {
            const int bx = 170;
            tri(bx - 4, 52, bx + 40, 26, bx + 84, 52, RGB(150, 40, 40));
            rect(bx, 52, bx + 80, MFC_BAR_Y - 2, RGB(190, 50, 50));
            rect(bx + 28, 76, bx + 52, MFC_BAR_Y - 2, RGB(110, 60, 30));
            line(bx + 28, 76, bx + 52, MFC_BAR_Y - 2, RGB(240, 240, 240)); line(bx + 52, 76, bx + 28, MFC_BAR_Y - 2, RGB(240, 240, 240));
            txt_C(bx + 40, 58, Terminal4x6, RGB(255, 255, 255), "BARN");
            hit(bx, 40, bx + 80, MFC_BAR_Y - 2, MFC_HIT(H_TARGET, 0));
            rect(0, MFC_BAR_Y - 6, bx - 1, MFC_BAR_Y - 1, RGB(50, 130, 50));
            for (int i = 0; i < m.count; i++){
                if (i == m.held) continue;
                animal(m.t[i].kind, m.t[i].x, m.t[i].y);
                if (!m.t[i].state) hit(m.t[i].x - 10, m.t[i].y - 8, m.t[i].x + 12, m.t[i].y + 8, MFC_HIT(H_ITEM, i));
            }
            if (m.held >= 0) animal(m.t[m.held].kind, m.t[m.held].x, m.t[m.held].y);     // the one held, on top
            break;
        }
        case L_SCROLL: {
            const int viewH = MFC_BAR_Y - top, y0 = top - m.scroll;
            for (int y = top; y < MFC_BAR_Y; y++){
                const int depth = y - y0;
                hline(y, 0, 247, mtb_Kit_Blend(RGB(60, 170, 230), RGB(4, 20, 60), (float)depth / MFC_SCROLL_H));
            }
            for (int d = 50; d < MFC_SCROLL_H; d += 50){
                const int y = y0 + d;
                if (y < top || y > MFC_BAR_Y - 8) continue;
                char t[12];
                snprintf(t, sizeof(t), "%d M", d / 5);
                txt(4, y, Terminal4x6, RGB(160, 200, 230), t);
                const int fx = 40 + (d * 37) % 160;
                disc(fx, y + 3, 4, (d / 50) % 2 ? RGB(255, 150, 40) : RGB(250, 220, 60));
                tri(fx - 4, y + 3, fx - 9, y - 1, fx - 9, y + 7, (d / 50) % 2 ? RGB(255, 150, 40) : RGB(250, 220, 60));
            }
            const Mfc_Thing_t& c = m.t[0];
            const int cy = y0 + c.y;
            rect(0, y0 + MFC_SCROLL_H - 8, 247, y0 + MFC_SCROLL_H, RGB(200, 180, 120));
            if (cy > top - 20 && cy < MFC_BAR_Y + 10){
                rect(c.x - 14, cy - 8, c.x + 14, cy + 10, RGB(140, 80, 30));
                rect(c.x - 14, cy - 12, c.x + 14, cy - 8, RGB(170, 100, 40));
                rect(c.x - 2, cy - 4, c.x + 2, cy + 2, KIT_GOLD);
                if (cy - 12 >= top && cy + 10 < MFC_BAR_Y) hit(c.x - 15, cy - 13, c.x + 15, cy + 11, MFC_HIT(H_ITEM, 0));
            }
            // The scroll bar: how much of the long picture is in view, and where.
            rect(249, top, 255, MFC_BAR_Y - 1, C_PANEL);
            const int thumbH = viewH * viewH / MFC_SCROLL_H, max = MFC_SCROLL_H - viewH;
            const int ty = top + (max > 0 ? (viewH - thumbH) * m.scroll / max : 0);
            rect(250, ty, 254, ty + thumbH, C_EDGE);
            break;
        }
        case L_RIGHT: {
            static const uint16_t PC[3] = {RGB(230, 50, 50), RGB(60, 120, 240), RGB(250, 200, 40)};
            // The flower to paint: a petal for each round done.
            const int cx = 190, cy = 56;
            for (int k = 0; k < 6; k++){
                const float a = k * 1.047f;
                const uint16_t c = k < m.round * 2 ? PC[(k / 2) % 3] : RGB(60, 60, 75);
                disc(cx + (int)(13 * cosf(a)), cy + (int)(13 * sinf(a)), 7, c);
            }
            disc(cx, cy, 6, KIT_GOLD);
            vline(cx, cy + 8, MFC_BAR_Y - 2, RGB(40, 160, 60));
            for (int i = 0; i < m.count; i++){
                const Mfc_Thing_t& p = m.t[i];
                rect(p.x - 10, p.y - 8, p.x + 10, p.y + 12, RGB(170, 175, 190));
                rect(p.x - 10, p.y - 10, p.x + 10, p.y - 6, PC[i]);
                rect(p.x - 8, p.y - 4, p.x + 8, p.y + 10, PC[i]);
                if (i == m.want) kk_Star((int16_t)p.x, (int16_t)(p.y - 20), 6, KIT_GOLD);
                hit(p.x - 11, p.y - 11, p.x + 11, p.y + 13, MFC_HIT(H_ITEM, i));
            }
            break;
        }
    }
    if (m.done >= m.goal) lesson_Done_Panel();
}

// The keyboard ----------------------------------------------------------------------------------------------------------------------
#define KB_X 18
#define KB_Y 62
static void kb_Key(int x, int y, int w, const char* label, bool target, bool lit){
    const uint16_t face = target ? KIT_GOLD : lit ? RGB(60, 200, 90) : C_BUTTON;
    rrect(x, y, x + w - 2, y + 9, 1, face);
    txt_C(x + (w - 2) / 2, y + 2, Terminal4x6, target ? BLACK : C_TEXT, label);
}

/** The keyboard, with @p want (HID usage) gold and the key pressed last green. */
static void keyboard(uint8_t want, char wantChar, bool shiftToo){
    static const char* const ROWS[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const uint8_t BASE[4] = {0x1E, 0, 0, 0};
    const uint8_t lit = S->keys.lit;
    for (int r = 0; r < 4; r++){
        const int x0 = KB_X + (r == 0 ? 16 : r * 4 + 16), y = KB_Y + r * 11;
        for (int c = 0; ROWS[r][c]; c++){
            const char ch = ROWS[r][c];
            const uint8_t usage = r == 0 ? (uint8_t)(BASE[0] + (ch == '0' ? 9 : ch - '1')) : (uint8_t)(0x04 + ch - 'a');
            char l[2] = {(char)(ch >= 'a' ? ch - 32 : ch), 0};
            kb_Key(x0 + c * 14, y, 14, l, wantChar && ch == wantChar, lit == usage);
        }
    }
    kb_Key(KB_X, KB_Y, 16, "ESC", want == 0x29, lit == 0x29);
    kb_Key(KB_X + 156, KB_Y, 28, "BACK", want == 0x2A, lit == 0x2A);
    kb_Key(KB_X + 146, KB_Y + 22, 38, "ENTER", want == 0x28, lit == 0x28);
    kb_Key(KB_X, KB_Y + 33, 26, "SHIFT", shiftToo, false);
    kb_Key(KB_X + 128, KB_Y + 33, 32, "SHIFT", shiftToo, false);
    kb_Key(KB_X + 40, KB_Y + 44, 100, "SPACE", want == 0x2C, lit == 0x2C);
    kb_Key(KB_X + 188, KB_Y + 44, 14, "<", want == 0x50, lit == 0x50);
    kb_Key(KB_X + 216, KB_Y + 44, 14, ">", want == 0x4F, lit == 0x4F);
    kb_Key(KB_X + 202, KB_Y + 33, 14, "^", want == 0x52, lit == 0x52);
    kb_Key(KB_X + 202, KB_Y + 44, 14, "v", want == 0x51, lit == 0x51);
}

static void keys_Program(void){
    Mfc_Keys_t& k = S->keys;
    if (k.activity == K_MENU){
        txt_C(128, MFC_TOP + 3, Terminal6x8, C_TEXT, "LEARN THE KEYBOARD");
        static const char* const N[4] = {"FIND THE KEYS", "TYPE YOUR NAME", "SPECIAL KEYS", "NUMBERS"};
        static const uint8_t B[4] = {MFC_B_KEYS, MFC_B_NAME, MFC_B_SPECIAL, MFC_B_KEYS};
        for (int i = 0; i < 4; i++){
            const int x = 10 + (i % 2) * 120, y = MFC_TOP + 16 + (i / 2) * 32;
            const uint16_t id = MFC_HIT(H_LESSON, i + 1);
            rrect(x, y, x + 114, y + 27, 4, hov(id) ? C_HOVER : C_PANEL);
            rframe(x, y, x + 114, y + 27, 4, theme());
            txt_C(x + 57, y + 10, Terminal6x8, C_TEXT, N[i]);
            if (mfc_Has(mfc_Me(*S), B[i])) kk_Star((int16_t)(x + 106), (int16_t)(y + 7), 4, KIT_GOLD);
            hit(x, y, x + 114, y + 27, id);
        }
        if (!mfc_Have_Keyboard()) wrap(10, MFC_TOP + 86, 236, Terminal6x8, 9, KIT_ORANGE, "PLUG A USB KEYBOARD INTO PIXLPAL TO START.", 2, true);
        return;
    }
    button(3, MFC_TOP + 2, 40, MFC_TOP + 12, "< BACK", MFC_HIT(H_BTN, 0));
    char t[64];
    uint8_t want = 0;
    char wantChar = 0;
    bool shift = false;
    switch (k.activity){
        case K_FIND: case K_NUMBERS: {
            snprintf(t, sizeof(t), "%u/%u", k.done < k.goal ? k.done : k.goal, k.goal);
            txt_R(252, MFC_TOP + 4, Terminal4x6, C_TEXT, t);
            if (k.done >= k.goal){ txt_C(128, MFC_TOP + 22, Terminal10x16, KIT_GOLD, "YOU FOUND THEM ALL!"); break; }
            txt_C(128, MFC_TOP + 4, Terminal6x8, C_TEXT, "PRESS THIS KEY:");
            char l[2] = {(char)(k.target >= 'a' ? k.target - 32 : k.target), 0};
            mtb_Kit_Big_Text((int16_t)(128 - mtb_Kit_Big_Text_Width(Terminal10x16, 2, l) / 2), MFC_TOP + 16, Terminal10x16, 2, KIT_GOLD, l);
            if (k.wrong) txt_C(128, MFC_TOP + 50, Terminal4x6, KIT_ORANGE, "NOT THAT ONE. LOOK FOR THE GOLD KEY");
            wantChar = k.target;
            break;
        }
        case K_NAME: {
            char want_[20];
            snprintf(want_, sizeof(want_), "%s", kk_Name(*S->k));
            for (size_t i = 0; want_[i]; i++) want_[i] = (char)(i == 0 ? (want_[i] >= 'a' && want_[i] <= 'z' ? want_[i] - 32 : want_[i]) : (want_[i] >= 'A' && want_[i] <= 'Z' ? want_[i] + 32 : want_[i]));
            txt_C(128, MFC_TOP + 3, Terminal6x8, C_TEXT, k.done ? "THAT'S YOUR NAME!" : "TYPE YOUR NAME:");
            txt_C(128, MFC_TOP + 14, Terminal8x12, C_SOFT, want_);
            rect(60, MFC_TOP + 30, 196, MFC_TOP + 45, C_FIELD);
            const int ex = txt(64, MFC_TOP + 32, Terminal8x12, C_INK, k.typed);
            if (!k.done && (S->ms / 500) % 2 == 0) vline(ex + 1, MFC_TOP + 32, MFC_TOP + 43, C_INK);
            const size_t n = strlen(k.typed);
            if (!k.done){
                if (n < strlen(want_)){ wantChar = (char)(want_[n] >= 'A' && want_[n] <= 'Z' ? want_[n] + 32 : want_[n]); shift = n == 0; }
                if (n == 1 && k.typed[0] != want_[0]) txt_C(128, MFC_TOP + 48, Terminal4x6, KIT_ORANGE, "HOLD SHIFT FOR A CAPITAL LETTER. BACKSPACE RUBS OUT");
                else if (n) txt_C(128, MFC_TOP + 48, Terminal4x6, C_SOFT, "BACKSPACE RUBS OUT A LETTER");
            }
            break;
        }
        case K_SPECIAL: {
            static const char* const STEP[6] = {"PRESS THE SPACE BAR: IT MAKES A GAP", "PRESS ENTER: IT STARTS A NEW LINE", "PRESS BACKSPACE: IT RUBS OUT",
                                                "HOLD SHIFT AND PRESS A: A CAPITAL A", "PRESS THE RIGHT ARROW TO TAKE THE FROG HOME", "PRESS ESC: IT CLOSES THINGS"};
            static const uint8_t WANT[6] = {0x2C, 0x28, 0x2A, 0x04, 0x4F, 0x29};
            if (k.step >= 6){ txt_C(128, MFC_TOP + 20, Terminal10x16, KIT_GOLD, "ALL DONE!"); break; }
            txt_C(128, MFC_TOP + 16, Terminal6x8, C_TEXT, STEP[k.step]);
            if (k.wrong) txt_C(128, MFC_TOP + 27, Terminal4x6, KIT_ORANGE, "LOOK FOR THE GOLD KEY");
            want = WANT[k.step];
            if (k.step == 3){ wantChar = 'a'; want = 0; shift = true; }
            if (k.step == 4){
                for (int i = 0; i < 8; i++) disc(44 + i * 24, MFC_TOP + 40, i == 7 ? 7 : 3, i == 7 ? RGB(40, 160, 70) : RGB(30, 90, 150));
                disc(44 + k.frog * 24, MFC_TOP + 38, 5, RGB(90, 220, 90));
                dot(42 + k.frog * 24, MFC_TOP + 36, BLACK); dot(46 + k.frog * 24, MFC_TOP + 36, BLACK);
            }
            snprintf(t, sizeof(t), "%u/6", k.step);
            txt_R(252, MFC_TOP + 4, Terminal4x6, C_TEXT, t);
            break;
        }
    }
    keyboard(want, wantChar, shift);
}

// Paint --------------------------------------------------------------------------------------------------------------------------------
static void tool_Icon(uint8_t t, int x, int y, uint16_t c){
    switch (t){
        case T_PENCIL: line(x + 3, y + 10, x + 13, y + 1, KIT_GOLD); dot(x + 3, y + 10, c); dot(x + 2, y + 11, c); break;
        case T_BRUSH: line(x + 6, y + 8, x + 13, y + 1, RGB(150, 90, 40)); disc(x + 5, y + 9, 2, c); break;
        case T_BIG: disc(x + 8, y + 6, 5, c); break;
        case T_SPRAY: rect(x + 6, y + 4, x + 10, y + 11, C_SOFT); for (int i = 0; i < 5; i++) dot(x + 2 + i * 2, y + 1 + (i % 2), c); break;
        case T_FILL: tri(x + 3, y + 5, x + 9, y + 1, x + 12, y + 8, C_SOFT); disc(x + 13, y + 10, 2, c); break;
        default: rect(x + 3, y + 4, x + 13, y + 9, RGB(255, 150, 200)); rect(x + 3, y + 7, x + 13, y + 9, RGB(240, 240, 240)); break;
    }
}

static void paint(void){
    static const char* const MENUS[2] = {"FILE", "PICTURE"};
    menu_Bar(MENUS, 2);
    const uint16_t ink = mfc_Paint_Color(S->paint.color);
    for (int t = 0; t < MFC_TOOLS; t++){
        const int y = MFC_TOP + 12 + t * 15;
        const uint16_t id = MFC_HIT(H_TOOL, t);
        const bool sel = S->paint.tool == t;
        rect(2, y, 21, y + 13, sel ? mtb_Kit_Dim(theme(), 200) : hov(id) ? C_HOVER : C_PANEL);
        if (sel) frame(2, y, 21, y + 13, C_TEXT);
        tool_Icon((uint8_t)t, 3, y + 1, ink);
        hit(2, y, 21, y + 13, id);
    }
    if (S->canvas) mtb_Panel_Draw_FrameRGB565(MFC_CANVAS_X, MFC_CANVAS_Y, MFC_CANVAS_W, MFC_CANVAS_H, S->canvas);
    frame(MFC_CANVAS_X - 1, MFC_CANVAS_Y - 1, MFC_CANVAS_X + MFC_CANVAS_W, MFC_CANVAS_Y + MFC_CANVAS_H, C_EDGE);
    hit(MFC_CANVAS_X, MFC_CANVAS_Y, MFC_CANVAS_X + MFC_CANVAS_W - 1, MFC_CANVAS_Y + MFC_CANVAS_H - 1, MFC_HIT(H_CANVAS, 0));
    for (int c = 0; c < MFC_PAINT_COLORS; c++){
        const int x = MFC_CANVAS_X + c * 14, y = MFC_CANVAS_Y + MFC_CANVAS_H + 2;
        const uint16_t id = MFC_HIT(H_COLOR, c);
        rect(x, y, x + 12, y + 8, mfc_Paint_Color((uint8_t)c));
        if (S->paint.color == c && S->paint.tool != T_ERASER){ frame(x - 1, y - 1, x + 13, y + 9, C_TEXT); frame(x, y, x + 12, y + 8, BLACK); }
        else if (hov(id)) frame(x, y, x + 12, y + 8, C_EDGE);
        hit(x, y, x + 12, y + 8, id);
    }
    // The keyboard pen when the pen is down with the keys.
    if (S->paint.drawing && !mfc_Have_Mouse()) ring(S->px, S->py, 3, ink);
}

// My Writing ---------------------------------------------------------------------------------------------------------------------------
static void writing(void){
    static const char* const MENUS[2] = {"FILE", "TEXT"};
    menu_Bar(MENUS, 2);
    char t[24];
    snprintf(t, sizeof(t), "%d WORDS", mfc_Text_Words(S->text));
    txt_R(250, MFC_TOP + 2, Terminal4x6, C_SOFT, t);
    const bool big = S->write.big;
    const uint8_t* font = big ? Terminal8x12 : Terminal6x8;
    const int cw = big ? 8 : 6, lineH = MFC_WR_LINE(big), rows = MFC_WR_ROWS(big), cols = MFC_WR_COLS(big);
    rect(0, MFC_TOP + 10, MFC_W - 1, MFC_BAR_Y - 1, RGB(14, 16, 22));
    hit(0, MFC_TOP + 10, 247, MFC_BAR_Y - 1, MFC_HIT(H_TEXT, 0));
    static const uint16_t INK[4] = {RGB(235, 235, 240), RGB(255, 210, 80), RGB(120, 220, 255), RGB(140, 240, 140)};
    static uint16_t starts[MFC_LINES_MAX];
    const int lines = mfc_Text_Layout(S->text, cols, starts, MFC_LINES_MAX);
    int cl, cc;
    mfc_Text_Where(S->text, starts, lines, cl, cc);
    for (int r = 0; r < rows; r++){
        const int l = S->write.top + r;
        if (l >= lines) break;
        const int end = l + 1 < lines ? starts[l + 1] : S->text.len;
        int n = end - starts[l];
        if (n > cols + 1) n = cols + 1;
        cells(MFC_WR_X, MFC_WR_Y + r * lineH, font, cw, INK[S->write.color % 4], S->text.t + starts[l], n);
    }
    if (cl >= S->write.top && cl < S->write.top + rows && (S->ms / 450) % 2 == 0){
        const int x = MFC_WR_X + cc * cw - 1, y = MFC_WR_Y + (cl - S->write.top) * lineH - 1;
        vline(x, y, y + lineH - 1, KIT_GOLD);
    }
    if (lines > rows){
        rect(249, MFC_TOP + 11, 254, MFC_BAR_Y - 2, C_PANEL);
        const int h = MFC_BAR_Y - MFC_TOP - 13, th = h * rows / lines, ty = MFC_TOP + 11 + (h - th) * S->write.top / (lines - rows);
        rect(250, ty, 253, ty + th, C_EDGE);
    }
    if (!S->text.len && !mfc_Have_Keyboard()) wrap(10, 60, 236, Terminal6x8, 9, KIT_ORANGE, "PLUG A USB KEYBOARD INTO PIXLPAL TO WRITE.", 2, true);
    else if (!S->text.len) txt(MFC_WR_X + 6, MFC_WR_Y, Terminal6x8, RGB(80, 85, 100), "Start typing here...");
}

// My Files ------------------------------------------------------------------------------------------------------------------------------
static void file_Icon(uint8_t kind, int x, int y){
    if (kind == 0){ rect(x, y, x + 7, y + 6, RGB(120, 190, 255)); tri(x, y + 6, x + 3, y + 2, x + 6, y + 6, RGB(60, 170, 70)); dot(x + 5, y + 1, KIT_GOLD); }
    else { rect(x + 1, y, x + 6, y + 7, RGB(245, 245, 240)); hline(y + 2, x + 2, x + 5, RGB(120, 140, 200)); hline(y + 4, x + 2, x + 5, RGB(120, 140, 200)); }
}

static void files(void){
    Mfc_Files_View_t& f = S->files;
    const bool bin = f.folder == MFC_F_BIN;
    rect(0, MFC_TOP, MFC_W - 1, MFC_TOP + 12, C_PANEL);
    if (bin){
        button(74, MFC_TOP + 2, 116, MFC_TOP + 11, "RESTORE", MFC_HIT(H_BTN, 0));
        button(120, MFC_TOP + 2, 172, MFC_TOP + 11, "EMPTY BIN", MFC_HIT(H_BTN, 1), RGB(150, 50, 50));
    } else {
        button(74, MFC_TOP + 2, 104, MFC_TOP + 11, "OPEN", MFC_HIT(H_BTN, 0));
        button(108, MFC_TOP + 2, 144, MFC_TOP + 11, "RENAME", MFC_HIT(H_BTN, 1));
        button(148, MFC_TOP + 2, 184, MFC_TOP + 11, "DELETE", MFC_HIT(H_BTN, 2));
    }
    // The folders.
    static const char* const FOLDER[MFC_FOLDERS] = {"MY PICTURES", "MY WRITING", "RECYCLE BIN"};
    for (int i = 0; i < MFC_FOLDERS; i++){
        const int y = MFC_FL_Y + i * 16;
        const uint16_t id = MFC_HIT(H_FOLDER, i);
        const bool sel = f.folder == i, drop = f.dragging >= 0 && hov(id);
        if (sel || hov(id)) rect(1, y, 68, y + 14, drop ? KIT_GOLD : sel ? mtb_Kit_Dim(theme(), 190) : C_HOVER);
        if (i == MFC_F_BIN){ rect(4, y + 3, 10, y + 4, RGB(170, 175, 185)); tri(4, y + 5, 10, y + 5, 9, y + 12, RGB(140, 145, 160)); tri(4, y + 5, 5, y + 12, 9, y + 12, RGB(140, 145, 160)); }
        else { rect(3, y + 3, 7, y + 5, RGB(230, 180, 40)); rect(3, y + 5, 11, y + 12, RGB(250, 205, 60)); }
        txt(14, y + 5, Terminal4x6, drop ? BLACK : C_TEXT, FOLDER[i]);
        hit(1, y, 68, y + 14, id);
    }
    // The space used.
    const int used = S->fileCount > 0 ? S->fileCount : 0;
    char t[32];
    snprintf(t, sizeof(t), "%d OF %d FILES", used, MFC_FILES_MAX);
    txt(3, 82, Terminal4x6, C_SOFT, "SPACE USED");
    mtb_Kit_Bar(3, 90, 64, 5, (float)used / MFC_FILES_MAX, used >= MFC_FILES_MAX ? KIT_RED : used > MFC_FILES_MAX * 3 / 4 ? KIT_ORANGE : KIT_GREEN, C_BUTTON);
    txt(3, 98, Terminal4x6, C_SOFT, t);
    vline(70, MFC_TOP + 13, MFC_BAR_Y - 1, C_EDGE);
    // The files.
    hit(71, MFC_FL_Y, 247, MFC_BAR_Y - 1, MFC_HIT(H_ITEM, 0));
    if (!f.count) txt(80, MFC_FL_Y + 4, Terminal6x8, C_SOFT, bin ? "THE BIN IS EMPTY" : "NOTHING HERE YET");
    for (int r = 0; r < MFC_FL_ROWS && f.top + r < f.count; r++){
        const int i = f.top + r;
        const int y = MFC_FL_Y + r * MFC_FL_ROW;
        const uint16_t id = MFC_HIT(H_FILE, i);
        const bool sel = f.selected == i;
        if (sel || hov(id)) rect(72, y, 246, y + MFC_FL_ROW - 1, sel ? mtb_Kit_Dim(theme(), 190) : C_PANEL);
        file_Icon(f.list[i].kind, 75, y + 1);
        char shown[MFC_NAME_LEN + 1];
        mfc_Name_Show(f.list[i].name, shown, sizeof(shown));
        txt(86, y + 1, Terminal6x8, C_TEXT, shown);
        snprintf(t, sizeof(t), "%s  %lu KB", f.list[i].kind ? "WRITING" : "PICTURE", (unsigned long)((f.list[i].size + 1023) / 1024));
        txt_R(244, y + 2, Terminal4x6, C_SOFT, t);
        hit(72, y, 246, y + MFC_FL_ROW - 1, id);
    }
    if (f.count > MFC_FL_ROWS){
        rect(249, MFC_FL_Y, 254, MFC_BAR_Y - 2, C_PANEL);
        const int h = MFC_BAR_Y - 2 - MFC_FL_Y, th = h * MFC_FL_ROWS / f.count, ty = MFC_FL_Y + (h - th) * f.top / (f.count - MFC_FL_ROWS);
        rect(250, ty, 253, ty + th, C_EDGE);
    }
    // A file being dragged, under the pointer.
    if (f.dragging >= 0 && f.dragging < f.count){
        char shown[MFC_NAME_LEN + 1];
        mfc_Name_Show(f.list[f.dragging].name, shown, sizeof(shown));
        const int w = tw(Terminal4x6, shown) + 14;
        rect(S->px + 6, S->py + 4, S->px + 6 + w, S->py + 13, RGB(50, 60, 90));
        file_Icon(f.list[f.dragging].kind, S->px + 8, S->py + 5);
        txt(S->px + 18, S->py + 6, Terminal4x6, C_TEXT, shown);
    }
}

// The calculator ----------------------------------------------------------------------------------------------------------------------
static void calculator(void){
    rrect(MFC_CALC_X - 4, MFC_TOP + 2, MFC_CALC_X + 4 * MFC_CALC_BW + 2, MFC_BAR_Y - 2, 4, C_PANEL);
    rect(MFC_CALC_X, MFC_TOP + 6, MFC_CALC_X + 4 * MFC_CALC_BW - 2, MFC_CALC_Y - 3, RGB(150, 210, 160));
    const Mfc_Calc_t& c = S->calc;
    if (c.message[0]) txt_R(MFC_CALC_X + 4 * MFC_CALC_BW - 5, MFC_TOP + 10, Terminal6x8, RGB(150, 30, 30), c.message);
    else txt_R(MFC_CALC_X + 4 * MFC_CALC_BW - 5, MFC_TOP + 6, Terminal10x16, RGB(20, 40, 20), c.entry);
    if (c.op){ char o[2] = {c.op == '*' ? 'X' : c.op, 0}; txt(MFC_CALC_X + 3, MFC_TOP + 8, Terminal6x8, RGB(40, 80, 40), o); }
    int n;
    const Mfc_Calc_Button_t* b = mfc_Calc_Buttons(n);
    for (int i = 0; i < n; i++){
        const int x0 = MFC_CALC_X + b[i].col * MFC_CALC_BW, y0 = MFC_CALC_Y + b[i].row * MFC_CALC_BH;
        const uint16_t face = b[i].key == '=' ? RGB(220, 110, 30) : b[i].key == 'C' ? RGB(160, 50, 50) : strchr("+-*/<", b[i].key) ? RGB(80, 90, 130) : C_BUTTON;
        button(x0, y0, x0 + b[i].span * MFC_CALC_BW - 3, y0 + MFC_CALC_BH - 3, b[i].label, MFC_HIT(H_KEY, i), face, Terminal6x8);
    }
    txt(4, MFC_TOP + 8, Terminal4x6, C_SOFT, "CLICK THE");
    txt(4, MFC_TOP + 15, Terminal4x6, C_SOFT, "BUTTONS, OR");
    txt(4, MFC_TOP + 22, Terminal4x6, C_SOFT, "TYPE THE SUM");
    char t[24];
    snprintf(t, sizeof(t), "SUMS: %u", c.answers);
    txt(196, MFC_TOP + 8, Terminal4x6, C_SOFT, t);
}

// The piano ---------------------------------------------------------------------------------------------------------------------------
static void piano(void){
    Mfc_Piano_t& p = S->piano;
    button(4, MFC_TOP + 3, 64, MFC_TOP + 13, "FREE PLAY", MFC_HIT(H_BTN, 0), p.mode == PIANO_FREE ? mtb_Kit_Dim(theme(), 200) : C_BUTTON);
    for (int i = 0; i < MFC_SONGS; i++)
        button(70 + i * 72, MFC_TOP + 3, 136 + i * 72, MFC_TOP + 13, mfc_Song(i).name, MFC_HIT(H_BTN, i + 1),
               p.mode == PIANO_SONG && p.song == i ? mtb_Kit_Dim(theme(), 200) : C_BUTTON);
    int next = -1;
    if (p.mode == PIANO_SONG){
        const Mfc_Song_t& s = mfc_Song(p.song);
        next = s.keys[p.pos];
        char t[40];
        snprintf(t, sizeof(t), "PLAY THE GREEN KEY  %d/%d", p.pos, s.count);
        txt_C(128, MFC_TOP + 18, Terminal6x8, p.wrong ? KIT_ORANGE : C_TEXT, p.wrong ? "OOPS! LOOK FOR THE GREEN KEY" : t);
    } else txt_C(128, MFC_TOP + 18, Terminal6x8, C_SOFT, "CLICK THE KEYS, OR PRESS Z X C V B N M");
    if (p.lit >= 0) txt_C(128, MFC_TOP + 27, Terminal6x8, KIT_GOLD, mfc_Piano_Name(p.lit));
    const int y0 = MFC_PIANO_Y, y1 = MFC_BAR_Y - 2;
    for (int w = 0; w < MFC_PIANO_WHITE; w++){
        const int x = MFC_PIANO_X + w * MFC_PIANO_KEY_W;
        const uint16_t id = MFC_HIT(H_KEY, w);
        const uint16_t c = p.lit == w ? KIT_GOLD : next == w ? RGB(80, 220, 110) : hov(id) ? RGB(210, 220, 240) : RGB(245, 245, 245);
        rect(x, y0, x + MFC_PIANO_KEY_W - 2, y1, c);
        hit(x, y0, x + MFC_PIANO_KEY_W - 2, y1, id);
        if (w % 7 == 0){ char t[4]; snprintf(t, sizeof(t), "C%d", 4 + w / 7); txt_C(x + 8, y1 - 8, Terminal4x6, RGB(120, 120, 120), t); }
    }
    for (int w = 0; w < MFC_PIANO_WHITE - 1; w++){
        if (w % 7 == 2 || w % 7 == 6) continue;
        const int key = 100 + w;
        const int x = MFC_PIANO_X + (w + 1) * MFC_PIANO_KEY_W - 6;
        const uint16_t id = MFC_HIT(H_KEY, key);
        rect(x, y0, x + 10, y0 + 40, p.lit == key ? KIT_GOLD : next == key ? RGB(40, 170, 70) : hov(id) ? RGB(70, 70, 90) : RGB(20, 20, 24));
        hit(x, y0, x + 10, y0 + 40, id);
    }
}

// The Internet ---------------------------------------------------------------------------------------------------------------------------
static void web(void){
    Mfc_Web_t& w = S->web;
    const Mfc_Page_t& page = mfc_Web_Page(w.page);
    // The browser's bar.
    rect(0, MFC_TOP, MFC_W - 1, MFC_TOP + 12, C_PANEL);
    button(2, MFC_TOP + 2, 16, MFC_TOP + 11, "<", MFC_HIT(H_BTN, 0), w.at > 0 ? C_BUTTON : C_PANEL);
    button(18, MFC_TOP + 2, 32, MFC_TOP + 11, ">", MFC_HIT(H_BTN, 1), w.at + 1 < w.count ? C_BUTTON : C_PANEL);
    button(34, MFC_TOP + 2, 58, MFC_TOP + 11, "HOME", MFC_HIT(H_BTN, 2));
    rect(61, MFC_TOP + 2, 186, MFC_TOP + 11, C_FIELD);
    char address[40];
    snprintf(address, sizeof(address), "kidsnet/%s", page.address);
    txt(64, MFC_TOP + 4, Terminal4x6, C_INK, address);
    button(189, MFC_TOP + 2, 253, MFC_TOP + 11, "ASK A GROWN-UP", MFC_HIT(H_BTN, 3), RGB(40, 140, 80));
    // The page's header.
    rect(0, MFC_TOP + 13, MFC_W - 1, MFC_TOP + 22, page.color);
    txt(MFC_WEB_X, MFC_TOP + 15, Terminal6x8, RGB(255, 255, 255), page.title);
    int y = MFC_WEB_Y;
    if (page.kind == MFC_PAGE_HOME){
        const uint16_t id = MFC_HIT(H_FIELD, 0);
        rect(MFC_WEB_X, y, 190, y + 11, C_FIELD);
        frame(MFC_WEB_X, y, 190, y + 11, w.searchOn ? KIT_GOLD : theme());
        if (w.query[0]) txt(MFC_WEB_X + 3, y + 2, Terminal6x8, C_INK, w.query);
        else txt(MFC_WEB_X + 3, y + 3, Terminal4x6, RGB(140, 140, 140), "SEARCH KIDSNET...");
        if (w.searchOn && (S->ms / 500) % 2 == 0) vline(MFC_WEB_X + 4 + tw(Terminal6x8, w.query), y + 2, y + 9, C_INK);
        hit(MFC_WEB_X, y, 190, y + 11, id);
        button(194, y, 250, y + 11, "SEARCH", MFC_HIT(H_BTN, 14));
        y += 14;
    }
    if (page.kind == MFC_PAGE_RESULTS){
        char t[48];
        snprintf(t, sizeof(t), "PAGES FOR \"%s\":", w.query);
        txt(MFC_WEB_X, y, Terminal4x6, C_SOFT, t);
        if (!w.resultCount) txt(MFC_WEB_X, y + 10, Terminal6x8, C_TEXT, "Nothing found. Try another word.");
        for (int i = 0; i < w.resultCount; i++){
            const Mfc_Page_t& r = mfc_Web_Page(w.results[i]);
            const int ry = y + 9 + i * 12;
            const uint16_t id = MFC_HIT(H_ITEM, i);
            const int ex = txt(MFC_WEB_X, ry, Terminal6x8, hov(id) ? RGB(180, 220, 255) : C_LINK, r.title);
            hline(ry + 8, MFC_WEB_X, ex, hov(id) ? RGB(180, 220, 255) : C_LINK);
            char a[32];
            snprintf(a, sizeof(a), "kidsnet/%s", r.address);
            txt(ex + 8, ry + 1, Terminal4x6, RGB(110, 170, 110), a);
            hit(MFC_WEB_X, ry - 1, ex, ry + 9, id);
        }
        return;
    }
    // The page's words, laid out; links blue and underlined.
    if (w.laidOut != w.page){ mfc_Web_Layout(page.text, MFC_WEB_COLS, w.layout); w.laidOut = w.page; }
    const Mfc_Layout_t& L = w.layout;
    const int rows = (MFC_BAR_Y - y) / MFC_WEB_LINE;
    for (int i = 0; i < L.words; i++){
        const Mfc_Word_t& word = L.w[i];
        const int r = word.line - w.scroll;
        if (r < 0 || r >= rows) continue;
        const int x = MFC_WEB_X + word.col * 6, wy = y + r * MFC_WEB_LINE;
        if (word.link >= 0){
            const uint16_t id = MFC_HIT(H_LINK, word.link);
            const uint16_t c = hov(id) ? RGB(190, 225, 255) : C_LINK;
            cells(x, wy, Terminal6x8, 6, c, page.text + word.at, word.len);
            // The underline joins the words of one link.
            const bool more = i + 1 < L.words && L.w[i + 1].link == word.link && L.w[i + 1].line == word.line;
            hline(wy + 8, x, x + word.len * 6 - 1 + (more ? 6 : 0), c);
            hit(x - 1, wy - 1, x + word.len * 6 + (more ? 6 : 0), wy + 8, id);
        } else cells(x, wy, Terminal6x8, 6, C_TEXT, page.text + word.at, word.len);
    }
    int after = y + (L.lines - w.scroll) * MFC_WEB_LINE + 4;
    if (page.kind == MFC_PAGE_HOME){
        txt(MFC_WEB_X, after, Terminal4x6, C_SOFT, "OR TRY:");
        static const char* const SUGGEST[3] = {"LIONS", "PLANETS", "DINOSAURS"};
        for (int i = 0; i < 3; i++) button(40 + i * 56, after - 2, 92 + i * 56, after + 8, SUGGEST[i], MFC_HIT(H_BTN, 10 + i));
    }
    if (page.kind == MFC_PAGE_CLUB){
        static const char* const FIELD[3] = {"YOUR NAME", "HOME ADDRESS", "PASSWORD"};
        for (int i = 0; i < 3; i++){
            const int fy = after + i * 13;
            txt(MFC_WEB_X, fy + 3, Terminal4x6, C_SOFT, FIELD[i]);
            const uint16_t id = MFC_HIT(H_FIELD, i + 1);
            rect(64, fy, 170, fy + 10, C_FIELD);
            frame(64, fy, 170, fy + 10, hov(id) ? KIT_GOLD : C_EDGE);
            hit(64, fy, 170, fy + 10, id);
        }
        button(180, after, 250, after + 11, "JOIN THE CLUB", MFC_HIT(H_BTN, 20), RGB(120, 60, 160));
        button(180, after + 16, 250, after + 27, "ASK A GROWN-UP", MFC_HIT(H_BTN, 21), RGB(40, 140, 80));
    }
    if (L.lines > rows){
        rect(250, y, 255, MFC_BAR_Y - 1, C_PANEL);
        const int h = MFC_BAR_Y - 1 - y, th = h * rows / L.lines, ty = y + (h - th) * w.scroll / (L.lines - rows);
        rect(251, ty, 254, ty + th, C_EDGE);
    }
    // The prize pop-up: a trick, to be closed with its little X.
    if (page.kind == MFC_PAGE_GAMES && w.popup == 1){
        const bool flash = (S->ms / 400) % 2;
        rect(40, 34, 216, 104, flash ? RGB(255, 220, 0) : RGB(255, 60, 160));
        frame(40, 34, 216, 104, RGB(255, 255, 255));
        txt_C(128, 42, Terminal8x12, flash ? RGB(200, 0, 80) : RGB(255, 255, 0), "YOU WON!!!");
        txt_C(128, 58, Terminal6x8, BLACK, "A FREE TABLET IS WAITING!");
        button(72, 78, 184, 96, "CLAIM YOUR PRIZE!", MFC_HIT(H_POPUP_CLAIM, 0), RGB(0, 170, 60), Terminal6x8);
        // The X is small, as on real pop-ups.
        const uint16_t id = MFC_HIT(H_POPUP_X, 0);
        rect(208, 35, 215, 42, hov(id) ? RGB(230, 40, 40) : RGB(120, 120, 120));
        line(209, 36, 214, 41, RGB(255, 255, 255)); line(214, 36, 209, 41, RGB(255, 255, 255));
        hit(206, 34, 216, 43, id);
    }
}

// Settings, the Licence ------------------------------------------------------------------------------------------------------------------
static void check_Box(int x, int y, const char* label, bool on, uint16_t id){
    rect(x, y, x + 8, y + 8, C_FIELD);
    frame(x, y, x + 8, y + 8, hov(id) ? KIT_GOLD : C_EDGE);
    if (on){ line(x + 2, y + 4, x + 3, y + 6, RGB(20, 120, 40)); line(x + 3, y + 6, x + 7, y + 2, RGB(20, 120, 40)); line(x + 2, y + 5, x + 3, y + 7, RGB(20, 120, 40)); }
    const int ex = txt(x + 12, y + 2, Terminal4x6, C_TEXT, label);
    hit(x, y, ex, y + 8, id);
}

static void settings(void){
    Mfc_Child_t& me = mfc_Me(*S);
    txt(4, MFC_TOP + 4, Terminal4x6, KIT_GOLD, "WALLPAPER");
    static const uint16_t WP[MFC_WALLPAPERS][2] = {
        {RGB(70, 140, 240), RGB(60, 170, 70)}, {RGB(4, 6, 24), RGB(220, 120, 60)}, {RGB(20, 130, 200), RGB(4, 30, 90)},
        {RGB(250, 120, 40), RGB(110, 40, 120)}, {RGB(20, 60, 140), RGB(20, 60, 140)}, {RGB(24, 24, 36), RGB(250, 200, 40)}};
    for (int i = 0; i < MFC_WALLPAPERS; i++){
        const int x = 4 + i * 41, y = MFC_TOP + 12;
        const uint16_t id = MFC_HIT(H_SETTING, i);
        gradient(x, y, x + 36, y + 20, WP[i][0], WP[i][1]);
        if (me.wallpaper == i){ frame(x - 1, y - 1, x + 37, y + 21, C_TEXT); frame(x - 2, y - 2, x + 38, y + 22, KIT_GOLD); }
        else if (hov(id)) frame(x - 1, y - 1, x + 37, y + 21, C_EDGE);
        hit(x, y, x + 36, y + 20, id);
    }
    txt(4, MFC_TOP + 38, Terminal4x6, KIT_GOLD, "WINDOW COLOUR");
    for (int i = 0; i < MFC_THEMES; i++){
        const int x = 4 + i * 22, y = MFC_TOP + 46;
        const uint16_t id = MFC_HIT(H_SETTING, 10 + i);
        disc(x + 8, y + 7, 7, mfc_Theme_Color((uint8_t)i));
        if (me.theme == i) ring(x + 8, y + 7, 9, C_TEXT);
        else if (hov(id)) ring(x + 8, y + 7, 9, C_EDGE);
        hit(x, y - 2, x + 17, y + 16, id);
    }
    txt(110, MFC_TOP + 38, Terminal4x6, KIT_GOLD, "THE MOUSE");
    check_Box(110, MFC_TOP + 46, "BIG POINTER", me.bigPointer, MFC_HIT(H_SETTING, 20));
    check_Box(110, MFC_TOP + 58, "SLOWER DOUBLE CLICK", me.slowDouble, MFC_HIT(H_SETTING, 21));
    txt(4, MFC_TOP + 72, Terminal4x6, KIT_GOLD, "VOLUME");
    button(4, MFC_TOP + 80, 20, MFC_TOP + 92, "-", MFC_HIT(H_SETTING, 30), C_BUTTON, Terminal6x8);
    for (int i = 0; i < 10; i++) rect(24 + i * 9, MFC_TOP + 92 - (i + 1), 30 + i * 9, MFC_TOP + 92, i < me.volume ? KIT_GREEN : C_BUTTON);
    button(116, MFC_TOP + 80, 132, MFC_TOP + 92, "+", MFC_HIT(H_SETTING, 31), C_BUTTON, Terminal6x8);
    button(138, MFC_TOP + 80, 190, MFC_TOP + 92, "TEST SOUND", MFC_HIT(H_SETTING, 32));
}

static void licence(void){
    Mfc_Child_t& me = mfc_Me(*S);
    char t[64];
    snprintf(t, sizeof(t), "%s'S COMPUTER LICENCE", kk_Name(*S->k));
    mtb_Kit_Upper(t);
    txt(4, MFC_TOP + 3, Terminal4x6, KIT_GOLD, t);
    snprintf(t, sizeof(t), "LEVEL %u   %d OF %d BADGES", mfc_Level(me, mfc_Age(*S)), mfc_Badge_Count(me), MFC_BADGES);
    txt_R(252, MFC_TOP + 3, Terminal4x6, C_TEXT, t);
    for (int b = 0; b < MFC_BADGES; b++){
        const int x = 2 + (b % 5) * 51, y = MFC_TOP + 12 + (b / 5) * 19;
        const bool has = mfc_Has(me, (uint8_t)b);
        const uint16_t id = MFC_HIT(H_BADGE, b);
        const bool sel = S->licenceBadge == b;
        rrect(x, y, x + 49, y + 17, 2, sel ? mtb_Kit_Dim(theme(), 190) : hov(id) ? C_HOVER : C_PANEL);
        kk_Star((int16_t)(x + 7), (int16_t)(y + 8), 5, has ? KIT_GOLD : RGB(70, 70, 85));
        char name[24];
        snprintf(name, sizeof(name), "%s", mfc_Badge_Name((uint8_t)b));
        // Two lines when it is long.
        char* gap = tw(Terminal4x6, name) > 36 ? strchr(name, ' ') : nullptr;
        if (gap){ *gap = '\0'; txt(x + 14, y + 3, Terminal4x6, has ? C_TEXT : C_SOFT, name); txt(x + 14, y + 10, Terminal4x6, has ? C_TEXT : C_SOFT, gap + 1); }
        else txt(x + 14, y + 6, Terminal4x6, has ? C_TEXT : C_SOFT, name);
        hit(x, y, x + 49, y + 17, id);
    }
    const char* how = S->licenceBadge >= 0 ? mfc_Badge_How((uint8_t)S->licenceBadge) : "CLICK A BADGE TO SEE HOW TO EARN IT";
    char up[64];
    snprintf(up, sizeof(up), "%s", how);
    mtb_Kit_Upper(up);
    txt_C(128, MFC_BAR_Y - 8, Terminal4x6, S->licenceBadge >= 0 && mfc_Has(me, (uint8_t)S->licenceBadge) ? KIT_GREEN : C_SOFT, up);
}

// The other screens ----------------------------------------------------------------------------------------------------------------------
static void boot(void){
    rect(0, 0, MFC_W - 1, MFC_H - 1, BLACK);
    const uint32_t t = S->ms - S->modeMs;
    // Four coloured squares, then the name, then the bar.
    const uint16_t sq[4] = {RGB(240, 80, 60), RGB(80, 220, 90), RGB(70, 150, 255), RGB(255, 210, 50)};
    for (int i = 0; i < 4; i++) if (t > (uint32_t)i * 150) rect(112 + (i % 2) * 17, 22 + (i / 2) * 17, 127 + (i % 2) * 17, 37 + (i / 2) * 17, sq[i]);
    if (t > 700) txt_C(128, 64, Terminal10x16, RGB(255, 255, 255), "PIXLPAL");
    if (t > 900) txt_C(128, 82, Terminal6x8, C_SOFT, "MY FIRST COMPUTER");
    rect(78, 100, 178, 104, RGB(30, 30, 40));
    const int w = (int)(100 * (t > 2400 ? 2400 : t) / 2400);
    rect(78, 100, 78 + w, 104, RGB(70, 150, 255));
    txt_C(128, 112, Terminal4x6, RGB(90, 90, 110), "STARTING UP...");
}

static void signin(void){
    wallpaper(0, MFC_H - 1);
    rect(0, 0, MFC_W - 1, 14, RGB(10, 12, 24));
    txt_C(128, 4, Terminal6x8, RGB(255, 255, 255), "WHO IS USING THE COMPUTER?");
    const int n = S->k->count;
    for (int i = 0; i < n; i++){
        const int cx = 128 + (i * 2 - (n - 1)) * 42, cy = 58;
        const uint16_t id = MFC_HIT(H_AVATAR, i);
        const bool on = hov(id) || (S->usingKnob == false && !mfc_Have_Mouse() && S->pick == i);
        rrect(cx - 36, cy - 30, cx + 36, cy + 40, 6, on ? RGB(30, 50, 110) : RGB(10, 12, 24));
        if (on) rframe(cx - 36, cy - 30, cx + 36, cy + 40, 6, KIT_GOLD);
        chip(cx, cy - 6, 18, (uint8_t)i, S->k->names[i]);
        txt_C(cx, cy + 18, Terminal6x8, RGB(255, 255, 255), S->k->names[i]);
        char t[24];
        snprintf(t, sizeof(t), "LEVEL %u", mfc_Level(S->set->c[S->k->real[i] % MFC_CHILDREN], S->k->ages[i]));
        txt_C(cx, cy + 29, Terminal4x6, C_SOFT, t);
        hit(cx - 36, cy - 30, cx + 36, cy + 40, id);
    }
    txt_C(128, 114, Terminal4x6, RGB(255, 255, 255), "CLICK YOUR PICTURE TO SIGN IN");
}

static void secret(void){
    wallpaper(1, MFC_H - 1);
    chip(20, 14, 9, S->k->child, kk_Name(*S->k));
    txt(34, 6, Terminal6x8, RGB(255, 255, 255), kk_Name(*S->k));
    txt(34, 16, Terminal4x6, C_SOFT, S->secretMaking ? "MAKE YOUR SECRET: CLICK 2 PICTURES, AND REMEMBER THEM!" : "CLICK YOUR 2 SECRET PICTURES, IN ORDER");
    for (int i = 0; i < MFC_PICTURES; i++){
        const int x = 6 + i * 41, y = 36;
        const uint16_t id = MFC_HIT(H_PICTURE, i);
        rrect(x, y, x + 37, y + 37, 4, hov(id) ? RGB(60, 70, 120) : RGB(24, 26, 50));
        rframe(x, y, x + 37, y + 37, 4, hov(id) ? KIT_GOLD : C_EDGE);
        picture((uint8_t)i, x + 4, y + 4);
        hit(x, y, x + 37, y + 37, id);
    }
    for (int i = 0; i < MFC_SECRET_LEN; i++) disc(118 + i * 20, 86, 5, i < S->secretN ? KIT_GOLD : RGB(70, 70, 90));
    if (S->secretFails >= 3) wrap(10, 96, 236, Terminal4x6, 7, KIT_ORANGE, "FORGOTTEN? A GROWN-UP CAN MAKE THEM AGAIN ON THE PHONE.", 2, true);
    else if (S->secretFails) txt_C(128, 98, Terminal6x8, KIT_ORANGE, "NOT QUITE. TRY AGAIN!");
    button(4, 112, 50, 124, "< BACK", MFC_HIT(H_PARENT_BTN, 0));
}

static void shutdown(void){
    const uint32_t t = S->ms - S->modeMs;
    const float f = t > 2000 ? 1.0f : t / 2000.0f;
    rect(0, 0, MFC_W - 1, MFC_H - 1, mtb_Kit_Blend(RGB(20, 60, 140), BLACK, f));
    txt_C(128, 44, Terminal8x12, mtb_Kit_Blend(RGB(255, 255, 255), RGB(60, 60, 60), f), "SHUTTING DOWN...");
    char g[40];
    snprintf(g, sizeof(g), "GOODBYE %s!", kk_Name(*S->k));
    mtb_Kit_Upper(g);
    txt_C(128, 64, Terminal6x8, mtb_Kit_Blend(KIT_GOLD, RGB(40, 40, 40), f), g);
}

static void off(void){
    rect(0, 0, MFC_W - 1, MFC_H - 1, BLACK);
    const uint16_t id = MFC_HIT(H_POWER, 0);
    const uint16_t c = hov(id) ? RGB(80, 220, 90) : RGB(60, 150, 70);
    ring(128, 54, 18, c); ring(128, 54, 17, c);
    rect(126, 30, 130, 52, BLACK);
    rect(127, 32, 129, 52, c);
    hit(104, 30, 152, 78, id);
    txt_C(128, 88, Terminal6x8, C_SOFT, "THE COMPUTER IS OFF");
    txt_C(128, 100, Terminal4x6, RGB(110, 110, 120), "CLICK THE POWER BUTTON TO START IT");
}

static void lock(void){
    rect(0, 0, MFC_W - 1, MFC_H - 1, RGB(8, 12, 36));
    for (int i = 0; i < 30; i++) dot((i * 73) % MFC_W, (i * 37) % 100, RGB(150, 150, 200));
    disc(128, 34, 14, RGB(250, 230, 150));
    disc(136, 28, 13, RGB(8, 12, 36));
    const char* a = S->stopWhy == KK_BEDTIME ? "IT'S BEDTIME" : S->stopWhy == MFC_STOP_LOCKED ? "THE COMPUTER IS LOCKED"
                  : S->stopWhy == MFC_STOP_LEAVING ? "SEE YOU SOON!" : "COMPUTER TIME IS OVER";
    const char* b = S->stopWhy == KK_BEDTIME ? "Sleep well! The computer will be here tomorrow."
                  : S->stopWhy == MFC_STOP_LOCKED ? "A grown-up has locked the computer for now."
                  : S->stopWhy == MFC_STOP_LEAVING ? "Your work is saved."
                  : "That's all for today. Well done, and see you tomorrow!";
    txt_C(128, 60, Terminal8x12, RGB(255, 255, 255), a);
    wrap(20, 80, 216, Terminal6x8, 10, C_SOFT, b, 2, true);
}

static void parent(void){
    rect(0, 0, MFC_W - 1, MFC_H - 1, RGB(16, 16, 22));
    rect(0, 0, MFC_W - 1, 13, RGB(150, 50, 50));
    txt_C(128, 3, Terminal6x8, RGB(255, 255, 255), "GROWN-UPS ONLY");
    if (S->ms < S->codeWaitUntil){
        char t[48];
        snprintf(t, sizeof(t), "TOO MANY TRIES. WAIT %lu SECONDS", (unsigned long)((S->codeWaitUntil - S->ms + 999) / 1000));
        txt_C(128, 40, Terminal6x8, KIT_ORANGE, t);
        button(4, 112, 50, 124, "< BACK", MFC_HIT(H_PARENT_BTN, 0));
        return;
    }
    txt_C(128, 18, Terminal4x6, C_SOFT, "TYPE THE PARENT CODE (SET ON THE PHONE)");
    for (int i = 0; i < MFC_CODE_LEN; i++){
        const int x = 92 + i * 19;
        rect(x, 27, x + 14, 43, C_FIELD);
        if (i < S->codeN) disc(x + 7, 35, 3, C_INK);
        else if (i == S->codeN) frame(x, 27, x + 14, 43, KIT_GOLD);
    }
    for (int d = 0; d < 10; d++){
        const int x = 33 + (d % 5) * 39, y = 50 + (d / 5) * 22;
        char t[2] = {(char)('0' + d), 0};
        button(x, y, x + 34, y + 18, t, MFC_HIT(H_DIGIT, d), S->usingKnob && S->digit == d ? RGB(150, 110, 20) : C_BUTTON, Terminal8x12);
    }
    button(4, 112, 50, 124, "< BACK", MFC_HIT(H_PARENT_BTN, 0));
    button(196, 112, 252, 124, "RUB OUT", MFC_HIT(H_PARENT_BTN, 1));
    txt_C(128, 96, Terminal4x6, RGB(110, 110, 120), "THE KNOB: TURN TO A NUMBER, CLICK TO TYPE IT");
}

static void parent_Menu(void){
    rect(0, 0, MFC_W - 1, MFC_H - 1, RGB(16, 16, 22));
    rect(0, 0, MFC_W - 1, 13, RGB(40, 140, 80));
    txt_C(128, 3, Terminal6x8, RGB(255, 255, 255), "HELLO, GROWN-UP");
    button(40, 22, 216, 40, "LEAVE MY FIRST COMPUTER", MFC_HIT(H_PARENT_BTN, 10), RGB(150, 50, 50), Terminal6x8);
    button(40, 46, 216, 64, S->set->locked ? "UNLOCK THE COMPUTER" : "LOCK THE COMPUTER", MFC_HIT(H_PARENT_BTN, 11), C_BUTTON, Terminal6x8);
    button(40, 70, 216, 88, "SIGN THE CHILD OUT", MFC_HIT(H_PARENT_BTN, 12), C_BUTTON, Terminal6x8);
    button(40, 94, 216, 112, "BACK TO THE COMPUTER", MFC_HIT(H_PARENT_BTN, 13), RGB(40, 120, 70), Terminal6x8);
}

// The frame ---------------------------------------------------------------------------------------------------------------------------------
static void program(void){
    char title[40], shown[MFC_NAME_LEN + 1];
    const uint8_t p = S->prog;
    if (p == MFC_P_PAINT || p == MFC_P_WRITE){
        const char* name = p == MFC_P_PAINT ? S->paint.name : S->write.name;
        mfc_Name_Show(name, shown, sizeof(shown));
        const bool changed = p == MFC_P_PAINT ? S->paint.changed : S->text.changed && S->text.len;
        snprintf(title, sizeof(title), "%s - %s%s", p == MFC_P_PAINT ? "PAINT" : "MY WRITING", name[0] ? shown : "UNTITLED", changed ? " *" : "");
    } else if (p == MFC_P_FILES) snprintf(title, sizeof(title), "%s", S->files.folder == MFC_F_BIN ? "RECYCLE BIN" : "MY FILES");
    else snprintf(title, sizeof(title), "%s", mfc_Program_Name(p));
    window(title, p);
    switch (p){
        case MFC_P_MOUSE: mouse_School(); break;
        case MFC_P_KEYS: keys_Program(); break;
        case MFC_P_PAINT: paint(); break;
        case MFC_P_WRITE: writing(); break;
        case MFC_P_FILES: files(); break;
        case MFC_P_CALC: calculator(); break;
        case MFC_P_PIANO: piano(); break;
        case MFC_P_WEB: web(); break;
        case MFC_P_SETTINGS: settings(); break;
        case MFC_P_LICENCE: licence(); break;
        default: break;
    }
}

void mfc_Draw(Mfc_State_t& s){
    S = &s;
    s.hitCount = 0;
    switch (s.mode){
        case MFC_M_BOOT: boot(); break;
        case MFC_M_SIGNIN: signin(); break;
        case MFC_M_SECRET: secret(); break;
        case MFC_M_SHUTDOWN: shutdown(); break;
        case MFC_M_OFF: off(); break;
        case MFC_M_LOCK: lock(); break;
        case MFC_M_PARENT: parent(); break;
        case MFC_M_PARENT_MENU: parent_Menu(); break;
        default:
            if (s.prog != MFC_PROGRAMS && !s.minimized) program();
            else desktop();
            taskbar();
            if (s.menu.open && s.prog != MFC_PROGRAMS && !s.minimized) menu_List(s.menu, H_MENU_ITEM);
            if (s.startOpen) start_Menu();
            if (s.ctx.open) menu_List(s.ctx, H_CTX_ITEM);
            if (s.dlg.kind != D_NONE){
                // A dialog: only its own buttons can be clicked.
                s.hitCount = 0;
                dialog();
            }
            toast();
            badge_Show();
            break;
    }
    pointer();
    S = nullptr;
}
