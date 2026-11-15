#include "fbl_Ui_Kit.h"
#include "fbl_Crest.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

int16_t fbl_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

int16_t fbl_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background){
    if (text[0] == '\0') return x;
    Mtb_FixedText_t widget(x < 0 ? 0 : x, y, font, color, background);
    widget.mtb_Write_String(text);
    return x + fbl_Text_Width(font, text);
}

int16_t fbl_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background){
    const int16_t x = rightX - fbl_Text_Width(font, text) + 1;
    fbl_Text(x, y, font, color, text, background);
    return x;
}

void fbl_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background){
    fbl_Text(centreX - fbl_Text_Width(font, text) / 2, y, font, color, text, background);
}

void fbl_Fit(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth){
    snprintf(dest, size, "%s", text);
    size_t length = strlen(dest);
    if (fbl_Text_Width(font, dest) <= maxWidth) return;
    while (length > 0){
        dest[--length] = '\0';
        while (length > 0 && dest[length - 1] == ' ') dest[--length] = '\0';
        char trial[64];
        snprintf(trial, sizeof(trial), "%s.", dest);
        if (fbl_Text_Width(font, trial) <= maxWidth){ snprintf(dest, size, "%s", trial); return; }
    }
}

void fbl_Upper(char* text){ for (; *text; text++) *text = (char)toupper((unsigned char)*text); }

//##############################################################################################################
// GLCD fonts: font[0] glyph gap, font[1] extra for a space, font[6] height; per glyph at (c - 32) * 4 + 8: width,
// then the offset of its rows, each ceil(width / 8) bytes, bits least significant first.
int16_t fbl_Big_Text_Width(const uint8_t* font, int scale, const char* text){ return fbl_Text_Width(font, text) * scale; }

void fbl_Big_Text(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, const char* text){
    const int height = font[6];
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        const int at = (*c - 32) * 4 + 8;
        const int width = font[at];
        const int bytes = width / 8 + ((width % 8) ? 1 : 0);
        const uint8_t* rows = font + font[at + 1] + font[at + 2] * 256;
        if (*c != ' '){
            for (int row = 0; row < height; row++){
                for (int b = 0; b < bytes; b++){
                    const uint8_t bits = rows[row * bytes + b];
                    for (int bit = 0; bit < 8; bit++){
                        const int col = b * 8 + bit;
                        if (col >= width || !(bits & (1 << bit))) continue;
                        const int16_t px = x + col * scale, py = y + row * scale;
                        mtb_Panel_Fill_Rect(px, py, px + scale - 1, py + scale - 1, color);
                    }
                }
            }
        }
        x += (width + font[0] + (*c == ' ' ? font[1] : 0)) * scale;
    }
}

//##############################################################################################################
void fbl_Segment_Digit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int digit, uint16_t color){
    static const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const uint8_t on = (digit >= 0 && digit <= 9) ? SEGMENTS[digit] : 0;
    const int16_t mid = y + (h - t) / 2;
    auto seg = [&](int bit, int16_t x1, int16_t y1, int16_t x2, int16_t y2){ if (on & (1 << bit)) mtb_Panel_Fill_Rect(x1, y1, x2, y2, color); };
    seg(0, x + t, y, x + w - t - 1, y + t - 1);
    seg(1, x + w - t, y + t, x + w - 1, mid - 1);
    seg(2, x + w - t, mid + t, x + w - 1, y + h - t - 1);
    seg(3, x + t, y + h - t, x + w - t - 1, y + h - 1);
    seg(4, x, mid + t, x + t - 1, y + h - t - 1);
    seg(5, x, y + t, x + t - 1, mid - 1);
    seg(6, x + t, mid, x + w - t - 1, mid + t - 1);
}

int16_t fbl_Segment_Number_Right(int16_t rightX, int16_t y, int16_t w, int16_t h, int16_t t, int value, uint16_t color){
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);
    const int n = (int)strlen(digits);
    int16_t x = rightX - n * w - (n - 1) * (t + 1) + 1;
    const int16_t left = x;
    for (int i = 0; i < n; i++){ fbl_Segment_Digit(x, y, w, h, t, digits[i] - '0', color); x += w + t + 1; }
    return left;
}

int16_t fbl_Segment_Number(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int value, uint16_t color){
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);
    for (const char* d = digits; *d; d++){ fbl_Segment_Digit(x, y, w, h, t, *d - '0', color); x += w + t + 1; }
    return x;
}

//##############################################################################################################
uint16_t fbl_Turf_At(int16_t x){ return ((x / 16) % 2) ? K_TURF_B : K_TURF_A; }

void fbl_Pitch(int16_t y1, int16_t y2){
    for (int16_t x = 0; x < PANEL_RES_X; x += 16) mtb_Panel_Fill_Rect(x, y1, x + 15, y2, fbl_Turf_At(x));
    mtb_Panel_Draw_VLine(128, y1, y2, K_TURF_LINE);
    mtb_Panel_Draw_Circle(128, (y1 + y2) / 2, 18, K_TURF_LINE);
    mtb_Panel_Fill_Circle(128, (y1 + y2) / 2, 1, K_TURF_LINE);
}

void fbl_Team_Code(char* dest, size_t size, const char* name){
    static const char* const SKIP[] = {"fc", "afc", "cf", "sc", "ac", "as", "ss", "us", "fk", "sk", "sv", "vfb", "vfl", "tsg", "1.", "club", "cd", "rc", "ud", "sd", "ca", "the"};
    char words[4][16];
    int count = 0;
    const char* p = name;
    while (*p && count < 4){
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        const size_t n = (size_t)(p - start);
        if (n == 0) break;
        bool skip = false;
        for (const char* s : SKIP) if (strlen(s) == n && strncasecmp(start, s, n) == 0) skip = true;
        if (skip) continue;
        snprintf(words[count++], sizeof(words[0]), "%.*s", (int)(n < 15 ? n : 15), start);
    }
    char code[4] = "";
    if (count == 0) snprintf(code, sizeof(code), "%.3s", name);
    else if (count == 1 || strlen(words[0]) >= 6) snprintf(code, sizeof(code), "%.3s", words[0]);
    else snprintf(code, sizeof(code), "%c%.2s", words[0][0], words[1]);        // "Man City" -> MCI
    fbl_Upper(code);
    snprintf(dest, size, "%s", code);
}

void fbl_Crest(uint32_t teamId, const char* teamName, int16_t x, int16_t y, uint16_t background){
    if (fbl_Crest_Draw(teamId, x, y, background)) return;
    // A shield with the initials, in a colour of its own for each team.
    static const uint16_t COLORS[] = {FBL_RGB(200, 16, 46), FBL_RGB(0, 80, 160), FBL_RGB(0, 120, 60), FBL_RGB(90, 30, 120),
                                      FBL_RGB(200, 120, 0), FBL_RGB(20, 20, 20), FBL_RGB(120, 0, 30), FBL_RGB(0, 110, 130)};
    const uint16_t color = COLORS[teamId % (sizeof(COLORS) / sizeof(COLORS[0]))];
    const int16_t s = FBL_CREST;
    mtb_Panel_Fill_Rect(x + 3, y + 2, x + s - 4, y + s * 2 / 3, color);
    mtb_Panel_Fill_Triangle(x + 3, y + s * 2 / 3, x + s - 4, y + s * 2 / 3, x + s / 2, y + s - 2, color);
    mtb_Panel_Draw_HLine(y + 2, x + 3, x + s - 4, K_TEXT);
    char code[8];
    fbl_Team_Code(code, sizeof(code), teamName);
    fbl_Text_Centre(x + s / 2, y + 9, Terminal6x8, K_TEXT, code, color);
}

//##############################################################################################################
// Fills a regular pentagon, clipped to the ball's circle.
static void fbl_Pentagon(int16_t cx, int16_t cy, float radius, float turn, int16_t ballX, int16_t ballY, int16_t ballR, uint16_t color){
    float px[5], py[5];
    for (int k = 0; k < 5; k++){
        const float a = turn + k * 2.0f * (float)M_PI / 5;
        px[k] = cx + radius * sinf(a);
        py[k] = cy - radius * cosf(a);
    }
    const int x1 = (int)(cx - radius) - 1, x2 = (int)(cx + radius) + 1, y1 = (int)(cy - radius) - 1, y2 = (int)(cy + radius) + 1;
    for (int y = y1; y <= y2; y++){
        for (int x = x1; x <= x2; x++){
            if ((x - ballX) * (x - ballX) + (y - ballY) * (y - ballY) > ballR * ballR) continue;
            bool inside = false;
            for (int i = 0, j = 4; i < 5; j = i++){
                if (((py[i] > y) != (py[j] > y)) && (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i])) inside = !inside;
            }
            if (inside) mtb_Panel_Draw_PixelRGB565(x, y, color);
        }
    }
}

void fbl_Ball(int16_t cx, int16_t cy, int16_t r){
    mtb_Panel_Fill_Circle(cx, cy, r, K_TEXT);
    const uint16_t patch = FBL_RGB(20, 20, 20);
    fbl_Pentagon(cx, cy, r * 0.32f, 0, cx, cy, r, patch);
    for (int k = 0; k < 5; k++){
        const float a = (float)M_PI / 5 + k * 2.0f * (float)M_PI / 5;
        fbl_Pentagon((int16_t)(cx + r * 0.82f * sinf(a)), (int16_t)(cy - r * 0.82f * cosf(a)), r * 0.3f, a + (float)M_PI, cx, cy, r, patch);
    }
    mtb_Panel_Draw_Circle(cx, cy, r, K_LABEL);
}

void fbl_Mark_Goal(int16_t cx, int16_t cy, uint16_t color){
    mtb_Panel_Fill_Circle(cx, cy, 2, color);
    mtb_Panel_Draw_PixelRGB565(cx, cy, BLACK);
}

void fbl_Mark_Card(int16_t x, int16_t y, uint16_t color){ mtb_Panel_Fill_Rect(x, y, x + 2, y + 4, color); }
