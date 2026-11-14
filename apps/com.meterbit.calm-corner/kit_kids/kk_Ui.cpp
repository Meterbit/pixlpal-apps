// The kids apps' shared screens. No Arduino: tested on a PC.
#include "kk_Kids.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

uint8_t kk_Stop(const Kk_Session_t& k, uint16_t minute){
    if (k.left == 0) return KK_TIME_UP;
    if (k.bedtimeOn && k.bedFrom != k.bedTo){
        const bool bed = k.bedFrom < k.bedTo ? (minute >= k.bedFrom && minute < k.bedTo) : (minute >= k.bedFrom || minute < k.bedTo);
        if (bed) return KK_BEDTIME;
    }
    return KK_GO;
}

const char* kk_Name(const Kk_Session_t& k){ return k.child < k.count ? k.names[k.child] : ""; }
uint8_t kk_Age(const Kk_Session_t& k){ return k.child < k.count ? k.ages[k.child] : 7; }
uint16_t kk_Stars(const Kk_Session_t& k){ return k.child < k.count ? k.stars[k.child] : 0; }

uint16_t kk_Child_Color(uint8_t child){
    static const uint16_t C[] = {MTB_KIT_RGB(255, 120, 40), MTB_KIT_RGB(60, 170, 255), MTB_KIT_RGB(200, 90, 255)};
    return C[child % 3];
}

void kk_Star(int16_t cx, int16_t cy, int16_t r, uint16_t color){
    int16_t x[10], y[10];
    for (int i = 0; i < 10; i++){
        const float a = -1.5707963f + i * 0.6283185f;
        const float rr = (i & 1) ? r * 0.45f : (float)r;
        x[i] = (int16_t)lroundf(cx + rr * cosf(a));
        y[i] = (int16_t)lroundf(cy + rr * sinf(a));
    }
    for (int i = 0; i < 10; i++) mtb_Panel_Fill_Triangle(cx, cy, x[i], y[i], x[(i + 1) % 10], y[(i + 1) % 10], color);
}

void kk_Header(const Kk_Session_t& k, int16_t top, const char* title, uint16_t titleColor){
    const uint16_t c = kk_Child_Color(k.child);
    char name[24], ascii[24];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), kk_Name(k));
    mtb_Kit_Fit(name, sizeof(name), Terminal6x8, ascii, 70);
    mtb_Panel_Fill_Circle(5, top + 5, 4, c);
    mtb_Kit_Text(12, top + 2, Terminal6x8, c, name);
    if (title && title[0]) mtb_Kit_Text_Centre(128, top + 2, Terminal6x8, titleColor, title);
    char s[12];
    snprintf(s, sizeof(s), "%u", kk_Stars(k));
    const int16_t x = mtb_Kit_Text_Right(253, top + 2, Terminal6x8, KIT_GOLD, s);
    kk_Star((int16_t)(x - 7), (int16_t)(top + 5), 4, KIT_GOLD);
}

void kk_Confetti(uint32_t ms, uint32_t seed, int16_t top){
    static const uint16_t C[] = {KIT_RED, KIT_GOLD, KIT_GREEN, KIT_CYAN, KIT_ORANGE, MTB_KIT_RGB(255, 80, 200)};
    uint32_t s = seed * 2654435761u + 1;
    for (int i = 0; i < 60; i++){
        s = s * 1664525u + 1013904223u;
        const int16_t x0 = (int16_t)(s % 256);
        s = s * 1664525u + 1013904223u;
        const float speed = 40.0f + (float)(s % 60);
        const float t = ms / 1000.0f;
        const int16_t y = (int16_t)(top - 10 + speed * t - (float)(s % 40));
        const int16_t x = (int16_t)(x0 + 6.0f * sinf(t * 4.0f + i));
        if (y < top || y > PANEL_RES_Y - 2) continue;
        mtb_Panel_Fill_Rect(x, y, x + 1, y + ((i & 1) ? 2 : 1), C[i % 6]);
    }
}

int16_t kk_Words(const char* text, int16_t cx, int16_t y, int16_t width, const uint8_t* font, uint16_t color, int maxLines, uint16_t background){
    char ascii[200], lines[6][64];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    if (maxLines > 6) maxLines = 6;
    const int n = mtb_Kit_Wrap(ascii, font, width, &lines[0][0], sizeof(lines[0]), maxLines);
    const int16_t h = (int16_t)(font[6] + 2);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(cx, (int16_t)(y + i * h), font, color, lines[i], background);
    return (int16_t)(y + n * h);
}

void kk_Pick_Screen(const Kk_Session_t& k, int16_t top, uint8_t selected){
    mtb_Panel_Fill_Rect(0, top, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    mtb_Kit_Text_Centre(128, top + 4, Terminal8x12, KIT_GOLD, "WHO IS PLAYING?");
    const int n = k.count ? k.count : 1;
    const int16_t w = 76, gap = 6, x0 = (int16_t)(128 - (n * w + (n - 1) * gap) / 2);
    for (int i = 0; i < n; i++){
        const int16_t x = (int16_t)(x0 + i * (w + gap)), y = (int16_t)(top + 26);
        const uint16_t c = kk_Child_Color((uint8_t)i);
        if (i == selected) mtb_Panel_Fill_Round_Rect(x, y, x + w, y + 70, 6, mtb_Kit_Dim(c, 70));
        mtb_Panel_Draw_Round_Rect(x, y, x + w, y + 70, 6, i == selected ? c : mtb_Kit_Dim(c, 120));
        // A face.
        mtb_Panel_Fill_Circle(x + w / 2, y + 22, 13, c);
        mtb_Panel_Fill_Circle(x + w / 2 - 5, y + 19, 2, BLACK);
        mtb_Panel_Fill_Circle(x + w / 2 + 5, y + 19, 2, BLACK);
        for (int dx = -6; dx <= 6; dx++){                       // a smile
            const int16_t sy = (int16_t)(y + 25 + (36 - dx * dx) / 9);
            mtb_Panel_Draw_PixelRGB565((int16_t)(x + w / 2 + dx), sy, BLACK);
            mtb_Panel_Draw_PixelRGB565((int16_t)(x + w / 2 + dx), (int16_t)(sy + 1), BLACK);
        }
        char name[24], ascii[24];
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), k.names[i]);
        mtb_Kit_Fit(name, sizeof(name), Terminal6x8, ascii, w - 4);
        mtb_Kit_Text_Centre(x + w / 2, y + 42, Terminal6x8, KIT_TEXT, name, i == selected ? mtb_Kit_Dim(c, 70) : BLACK);
        char a[16];
        snprintf(a, sizeof(a), "AGE %u", k.ages[i]);
        mtb_Kit_Text_Centre(x + w / 2, y + 55, Terminal4x6, KIT_LABEL, a, i == selected ? mtb_Kit_Dim(c, 70) : BLACK);
    }
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 9, Terminal4x6, KIT_DIM, "TURN TO CHOOSE, PRESS TO START");
}

void kk_Stop_Screen(const Kk_Session_t& k, int16_t top, uint8_t why){
    mtb_Panel_Fill_Rect(0, top, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    char a[48], name[20];
    mtb_Kit_To_Ascii(name, sizeof(name), kk_Name(k));
    mtb_Kit_Upper(name);
    if (why == KK_BEDTIME){
        // A moon and stars.
        const int16_t cy = (int16_t)(top + 36);
        mtb_Panel_Fill_Circle(128, cy, 18, MTB_KIT_RGB(255, 230, 140));
        mtb_Panel_Fill_Circle(137, cy - 6, 16, BLACK);
        kk_Star(90, (int16_t)(top + 20), 4, KIT_GOLD);
        kk_Star(170, (int16_t)(top + 28), 3, KIT_GOLD);
        kk_Star(160, (int16_t)(top + 12), 2, KIT_GOLD);
        snprintf(a, sizeof(a), "BEDTIME, %s", name);
        mtb_Kit_Text_Centre(128, (int16_t)(top + 64), Terminal8x12, KIT_CYAN, a);
        mtb_Kit_Text_Centre(128, (int16_t)(top + 84), Terminal6x8, KIT_LABEL, "SLEEP WELL. SEE YOU TOMORROW!");
    } else {
        kk_Star(128, (int16_t)(top + 30), 20, KIT_GOLD);
        snprintf(a, sizeof(a), "THAT'S ALL FOR TODAY, %s", name);
        kk_Words(a, 128, (int16_t)(top + 58), 240, Terminal8x12, KIT_GOLD, 2);
        mtb_Kit_Text_Centre(128, (int16_t)(top + 92), Terminal6x8, KIT_LABEL, "TIME TO PLAY SOMETHING ELSE");
    }
}
