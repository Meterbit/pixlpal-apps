#include "mtb_kit_icons.h"
#include "mtb_kit_ui.h"
#include <math.h>

static const uint16_t SUN    = MTB_KIT_RGB(255, 200, 0);
static const uint16_t RAY    = MTB_KIT_RGB(255, 150, 0);
static const uint16_t MOON   = MTB_KIT_RGB(230, 225, 190);
static const uint16_t CLOUD  = MTB_KIT_RGB(205, 215, 230);
static const uint16_t SHADE  = MTB_KIT_RGB(120, 130, 150);
static const uint16_t DARKCL = MTB_KIT_RGB(90, 95, 110);
static const uint16_t DROP   = MTB_KIT_RGB(70, 150, 255);
static const uint16_t FLAKE  = MTB_KIT_RGB(235, 245, 255);
static const uint16_t BOLT   = MTB_KIT_RGB(255, 230, 40);
static const uint16_t MIST   = MTB_KIT_RGB(160, 165, 175);

static void kit_Sun(int16_t cx, int16_t cy, int16_t r){
    for (int i = 0; i < 8; i++){
        const float a = i * (float)M_PI / 4;
        const int16_t x1 = cx + (int16_t)lroundf(cosf(a) * (r + 2)), y1 = cy + (int16_t)lroundf(sinf(a) * (r + 2));
        const int16_t x2 = cx + (int16_t)lroundf(cosf(a) * (r + 2 + r * 0.6f)), y2 = cy + (int16_t)lroundf(sinf(a) * (r + 2 + r * 0.6f));
        mtb_Panel_Draw_Line(x1, y1, x2, y2, RAY);
    }
    mtb_Panel_Fill_Circle(cx, cy, r, SUN);
}

static void kit_Crescent(int16_t cx, int16_t cy, int16_t r){
    mtb_Panel_Fill_Circle(cx, cy, r, MOON);
    mtb_Panel_Fill_Circle(cx + r * 2 / 3, cy - r / 3, r * 4 / 5, BLACK);
}

// A cloud whose bottom edge is y + h - 1, spanning x .. x + w - 1.
static void kit_Cloud(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color){
    const int16_t base = y + h - 1;
    const int16_t r1 = h * 2 / 5, r2 = h / 2, r3 = h * 3 / 10;
    mtb_Panel_Fill_Circle(x + r1, base - r1, r1, color);
    mtb_Panel_Fill_Circle(x + w * 9 / 20, base - r2 - h / 10, r2, color);
    mtb_Panel_Fill_Circle(x + w - 1 - r3, base - r3, r3, color);
    mtb_Panel_Fill_Rect(x + r1, base - r1, x + w - 1 - r3, base, color);
}

static void kit_Drops(int16_t x, int16_t y, int16_t w, int16_t h, int count, int16_t length, uint16_t color){
    for (int i = 0; i < count; i++){
        const int16_t px = x + (int16_t)((i + 0.5f) * w / count);
        const int16_t py = y + (i % 2) * (h / 4);
        mtb_Panel_Draw_Line(px, py, px - length / 3, py + length, color);
        if (length > 4) mtb_Panel_Draw_Line(px + 1, py, px + 1 - length / 3, py + length, color);
    }
}

static void kit_Flakes(int16_t x, int16_t y, int16_t w, int16_t h, int count, int16_t r){
    for (int i = 0; i < count; i++){
        const int16_t cx = x + (int16_t)((i + 0.5f) * w / count), cy = y + r + (i % 2) * (h / 3);
        mtb_Panel_Draw_HLine(cy, cx - r, cx + r, FLAKE);
        mtb_Panel_Draw_VLine(cx, cy - r, cy + r, FLAKE);
        mtb_Panel_Draw_Line(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, FLAKE);
        mtb_Panel_Draw_Line(cx - r + 1, cy + r - 1, cx + r - 1, cy - r + 1, FLAKE);
    }
}

void mtb_Kit_Weather_Icon(int16_t x, int16_t y, int16_t s, uint8_t weather, bool night){
    const int16_t cloudY = y + s / 6, cloudH = s * 11 / 20;
    const int16_t below = cloudY + cloudH + 1, belowH = y + s - below;
    switch (weather){
        case KIT_WX_CLEAR:
            if (night) kit_Crescent(x + s / 2, y + s / 2, s * 3 / 10);
            else kit_Sun(x + s / 2, y + s / 2, s / 4);
            break;
        case KIT_WX_HOT:
            kit_Sun(x + s / 2, y + s / 2, s / 4);
            mtb_Panel_Fill_Circle(x + s / 2, y + s / 2, s / 8, MTB_KIT_RGB(255, 90, 0));
            break;
        case KIT_WX_PARTLY:
            if (night) kit_Crescent(x + s * 2 / 3, y + s / 3, s / 5);
            else kit_Sun(x + s * 2 / 3, y + s / 3, s / 6);
            kit_Cloud(x, y + s / 4, s * 4 / 5, s * 3 / 5, CLOUD);
            break;
        case KIT_WX_CLOUDY:
            kit_Cloud(x + s / 5, y + s / 8, s * 4 / 5, s / 2, SHADE);
            kit_Cloud(x, y + s / 4, s * 4 / 5, s * 3 / 5, CLOUD);
            break;
        case KIT_WX_FOG:
            kit_Cloud(x + s / 8, y + s / 8, s * 3 / 4, s / 2, SHADE);
            for (int i = 0; i < 3; i++){
                const int16_t ly = y + s * 13 / 20 + i * (s / 8 > 2 ? s / 8 : 2);
                mtb_Panel_Draw_HLine(ly, x + (i % 2 ? s / 6 : 0), x + s - 1 - (i % 2 ? 0 : s / 6), MIST);
            }
            break;
        case KIT_WX_DRIZZLE:
        case KIT_WX_RAIN:
        case KIT_WX_HEAVY_RAIN:
            kit_Cloud(x, cloudY, s, cloudH, weather == KIT_WX_HEAVY_RAIN ? DARKCL : CLOUD);
            kit_Drops(x + s / 8, below, s * 3 / 4, belowH, weather == KIT_WX_DRIZZLE ? 2 : weather == KIT_WX_RAIN ? 3 : 4,
                      weather == KIT_WX_DRIZZLE ? belowH / 3 : belowH * 2 / 3, DROP);
            break;
        case KIT_WX_SNOW:
            kit_Cloud(x, cloudY, s, cloudH, CLOUD);
            kit_Flakes(x + s / 8, below, s * 3 / 4, belowH, 3, s / 14 > 1 ? s / 14 : 1);
            break;
        case KIT_WX_SLEET:
        case KIT_WX_HAIL:
            kit_Cloud(x, cloudY, s, cloudH, CLOUD);
            kit_Drops(x + s / 8, below, s * 3 / 8, belowH, 2, belowH / 2, DROP);
            if (weather == KIT_WX_SLEET) kit_Flakes(x + s / 2, below, s * 3 / 8, belowH, 2, s / 16 > 1 ? s / 16 : 1);
            else for (int i = 0; i < 2; i++) mtb_Panel_Fill_Circle(x + s * 5 / 8 + i * s / 6, below + belowH / 3 + i * belowH / 4, s / 20 > 1 ? s / 20 : 1, FLAKE);
            break;
        case KIT_WX_STORM: {
            kit_Cloud(x, cloudY, s, cloudH, DARKCL);
            const int16_t bx = x + s / 2, by = below - 2;
            mtb_Panel_Fill_Triangle(bx, by, bx - s / 6, by + belowH * 3 / 5, bx + s / 20, by + belowH * 3 / 5, BOLT);
            mtb_Panel_Fill_Triangle(bx - s / 20, by + belowH * 2 / 5, bx + s / 6, by + belowH * 2 / 5, bx - s / 10, by + belowH, BOLT);
            kit_Drops(x + s / 10, below, s / 4, belowH, 1, belowH / 2, DROP);
            kit_Drops(x + s * 2 / 3, below, s / 4, belowH, 1, belowH / 2, DROP);
            break;
        }
        case KIT_WX_WIND:
            for (int i = 0; i < 3; i++){
                const int16_t ly = y + s / 4 + i * s / 4;
                const int16_t len = s * (i == 1 ? 9 : 6) / 10;
                mtb_Panel_Draw_HLine(ly, x + s / 10, x + s / 10 + len, CLOUD);
                mtb_Panel_Draw_Circle(x + s / 10 + len, ly - s / 12, s / 12 > 1 ? s / 12 : 1, CLOUD);
            }
            break;
        default:
            kit_Cloud(x, y + s / 4, s, s / 2, SHADE);
            mtb_Kit_Text_Centre(x + s / 2, y + s / 2 - 3, Terminal6x8, KIT_TEXT, "?", SHADE);
            break;
    }
}

//##############################################################################################################
float mtb_Kit_Moon_Age(uint32_t unixTime){
    static const double SYNODIC = 29.530588853;
    static const double NEW_MOON_2000 = 947182440.0;     // 2000-01-06 18:14 UTC
    double age = fmod(((double)unixTime - NEW_MOON_2000) / 86400.0, SYNODIC);
    if (age < 0) age += SYNODIC;
    return (float)age;
}

const char* mtb_Kit_Moon_Name(float age){
    static const char* const NAMES[8] = {"NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
                                        "FULL MOON", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"};
    int index = (int)floorf((age / 29.530588f) * 8 + 0.5f) % 8;
    return NAMES[index < 0 ? 0 : index];
}

void mtb_Kit_Moon(int16_t cx, int16_t cy, int16_t r, float age){
    static const uint16_t DARK = MTB_KIT_RGB(40, 40, 52);
    const float phase = age / 29.530588f;                       // 0 new, 0.5 full
    const float lit = (1 - cosf(phase * 2 * (float)M_PI)) / 2;  // Illuminated fraction.
    const bool waxing = phase < 0.5f;
    for (int16_t dy = -r; dy <= r; dy++){
        const float half = sqrtf((float)(r * r - dy * dy));
        // The terminator: an ellipse whose half-width runs from +half (new) through 0 (quarter) to -half (full).
        const float terminator = half * (1 - 2 * lit);
        for (int16_t dx = (int16_t)-lroundf(half); dx <= (int16_t)lroundf(half); dx++){
            const float side = waxing ? dx : -dx;               // Waxing: the right side is lit.
            const bool on = lit > 0.97f || (lit > 0.03f && side >= terminator);
            mtb_Panel_Draw_PixelRGB565(cx + dx, cy + dy, on ? MOON : DARK);
        }
    }
}
