// Pixlpal compat: the firmware's meterbit_ui_kit/mtb_kit_ui.cpp, with its text drawn by the compat layer (see
// mtb_Kit_Text). Keep the rest in step with the firmware's.
#include "mtb_kit_ui.h"
#include "pixlpal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//##############################################################################################################
// GLCD fonts: font[0] glyph gap, font[1] extra for a space, font[6] height; per glyph at (c - 32) * 4 + 8: width,
// then the offset of its rows, each ceil(width / 8) bytes, bits least significant first.
int16_t mtb_Kit_Text_Width(const uint8_t* font, const char* text){
    int16_t width = 0;
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        width += font[(*c - 32) * 4 + 8] + font[0];
        if (*c == ' ') width += font[1];
    }
    return (width > 0) ? width - font[0] : 0;
}

// Text: the glyphs go into a 1-bit buffer the text's size, drawn in one call (pxl_bitmap_mono), as the firmware's kit
// does. Like the firmware it draws no background, and text starting left of the screen starts at its edge.
void pxl_compat_glyphs(uint8_t* bits, int16_t stride, int16_t w, int16_t h, const uint8_t* font, const char* text, int x){
    for (const char* c = text; *c; c++){
        if (*c < ' ' || *c > '~') continue;
        const int at = (*c - 32) * 4 + 8;
        const int width = font[at];
        const int bytes = width / 8 + ((width % 8) ? 1 : 0);
        const uint8_t* rows = font + font[at + 1] + font[at + 2] * 256;
        if (*c != ' '){
            for (int row = 0; row < font[6] && row < h; row++)
                for (int col = 0; col < width; col++){
                    if (!(rows[row * bytes + col / 8] & (1 << (col % 8)))) continue;
                    const int px = x + col;
                    if (px >= 0 && px < w) bits[row * stride + (px >> 3)] |= (uint8_t)(1u << (px & 7));
                }
        }
        x += width + font[0] + (*c == ' ' ? font[1] : 0);
    }
}

int16_t mtb_Kit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t){
    if (text == nullptr || text[0] == '\0') return x;
    const int16_t left = x < 0 ? 0 : x;
    const int16_t width = mtb_Kit_Text_Width(font, text) + 8;          // + a glyph's spare columns
    const int16_t w = width < PANEL_RES_X - left ? width : PANEL_RES_X - left, h = font[6], stride = (w + 7) / 8;
    if (w <= 0 || y >= PANEL_RES_Y || y + h <= 0) return x + mtb_Kit_Text_Width(font, text);
    uint8_t small[512];
    const size_t need = (size_t)h * stride;
    uint8_t* buffer = need <= sizeof(small) ? small : (uint8_t*)malloc(need);
    if (buffer){
        memset(buffer, 0, need);
        pxl_compat_glyphs(buffer, stride, w, h, font, text, 0);
        pxl_bitmap_mono(left, y, w, h, buffer, color, PXL_CLEAR);
        if (buffer != small) free(buffer);
    }
    return x + mtb_Kit_Text_Width(font, text);
}

int16_t mtb_Kit_Text_Right(int16_t rightX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background){
    const int16_t x = rightX - mtb_Kit_Text_Width(font, text) + 1;
    mtb_Kit_Text(x, y, font, color, text, background);
    return x;
}

void mtb_Kit_Text_Centre(int16_t centreX, int16_t y, const uint8_t* font, uint16_t color, const char* text, uint16_t background){
    mtb_Kit_Text(centreX - mtb_Kit_Text_Width(font, text) / 2, y, font, color, text, background);
}

void mtb_Kit_Fit(char* dest, size_t size, const uint8_t* font, const char* text, int16_t maxWidth){
    if (size == 0) return;
    snprintf(dest, size, "%s", text ? text : "");
    size_t length = strlen(dest);
    if (mtb_Kit_Text_Width(font, dest) <= maxWidth) return;
    char trial[160];
    while (length > 0){
        dest[--length] = '\0';
        while (length > 0 && dest[length - 1] == ' ') dest[--length] = '\0';
        snprintf(trial, sizeof(trial), "%s.", dest);
        if (mtb_Kit_Text_Width(font, trial) <= maxWidth){ snprintf(dest, size, "%s", trial); return; }
    }
}

void mtb_Kit_Upper(char* text){ for (; *text; text++) *text = (char)toupper((unsigned char)*text); }

int mtb_Kit_Wrap(const char* text, const uint8_t* font, int16_t maxWidth, char* lines, size_t lineSize, int maxLines){
    if (maxLines <= 0 || lineSize < 4) return 0;
    int used = 0;
    const char* p = text ? text : "";
    while (*p == ' ') p++;
    while (*p && used < maxLines){
        char* line = lines + used * lineSize;
        size_t length = 0, lastBreak = 0;
        const char* breakAt = nullptr;
        line[0] = '\0';
        const char* q = p;
        while (*q && length + 1 < lineSize){
            line[length] = *q;
            line[length + 1] = '\0';
            if (mtb_Kit_Text_Width(font, line) > maxWidth){ line[length] = '\0'; break; }
            length++;
            q++;
            if (*q == ' ' || *q == '\0'){ lastBreak = length; breakAt = q; }
        }
        if (*q && breakAt != nullptr && lastBreak > 0){ length = lastBreak; line[length] = '\0'; q = breakAt; }
        if (length == 0){ q++; }       // A glyph wider than the line: skip it.
        while (*q == ' ') q++;
        p = q;
        used++;
        if (used == maxLines && *p){
            // Text left over: end the last line in "..", cut to fit.
            char trial[160];
            for (;;){
                snprintf(trial, sizeof(trial), "%s..", line);
                if (mtb_Kit_Text_Width(font, trial) <= maxWidth || line[0] == '\0') break;
                size_t n = strlen(line);
                line[n - 1] = '\0';
                while (n > 1 && line[n - 2] == ' '){ line[n - 2] = '\0'; n--; }
            }
            snprintf(line, lineSize, "%s", trial);
        }
    }
    return used;
}

//##############################################################################################################
int16_t mtb_Kit_Big_Text_Width(const uint8_t* font, int scale, const char* text){ return mtb_Kit_Text_Width(font, text) * scale; }

void mtb_Kit_Big_Text(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, const char* text){
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

void mtb_Kit_Segment_Digit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int digit, uint16_t color, uint16_t ghost){
    static const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const uint8_t on = (digit >= 0 && digit <= 9) ? SEGMENTS[digit] : 0;
    const int16_t mid = y + (h - t) / 2;
    auto seg = [&](int bit, int16_t x1, int16_t y1, int16_t x2, int16_t y2){
        if (on & (1 << bit)) mtb_Panel_Fill_Rect(x1, y1, x2, y2, color);
        else if (ghost != BLACK) mtb_Panel_Fill_Rect(x1, y1, x2, y2, ghost);
    };
    seg(0, x + t, y, x + w - t - 1, y + t - 1);
    seg(1, x + w - t, y + t, x + w - 1, mid - 1);
    seg(2, x + w - t, mid + t, x + w - 1, y + h - t - 1);
    seg(3, x + t, y + h - t, x + w - t - 1, y + h - 1);
    seg(4, x, mid + t, x + t - 1, y + h - t - 1);
    seg(5, x, y + t, x + t - 1, mid - 1);
    seg(6, x + t, mid, x + w - t - 1, mid + t - 1);
}

//##############################################################################################################
void mtb_Kit_Bar(int16_t x, int16_t y, int16_t w, int16_t h, float fraction, uint16_t color, uint16_t track){
    if (w <= 0 || h <= 0) return;
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    const int16_t filled = (int16_t)lroundf(fraction * w);
    if (filled < w) mtb_Panel_Fill_Rect(x + filled, y, x + w - 1, y + h - 1, track);
    if (filled > 0) mtb_Panel_Fill_Rect(x, y, x + filled - 1, y + h - 1, color);
}

void mtb_Kit_Sparkline(int16_t x, int16_t y, int16_t w, int16_t h, const float* values, int count, uint16_t color, uint16_t fill){
    if (count < 2 || w < 2 || h < 2) return;
    float low = values[0], high = values[0];
    for (int i = 1; i < count; i++){ if (values[i] < low) low = values[i]; if (values[i] > high) high = values[i]; }
    const float span = high - low > 0 ? high - low : 1;
    auto yOf = [&](float v){ return (int16_t)(y + h - 1 - lroundf((v - low) / span * (h - 1))); };
    auto xOf = [&](int i){ return (int16_t)(x + lroundf((float)i * (w - 1) / (count - 1))); };
    if (fill != BLACK){
        for (int i = 0; i + 1 < count; i++){
            const int16_t x1 = xOf(i), x2 = xOf(i + 1);
            for (int16_t px = x1; px <= x2; px++){
                const float t = x2 > x1 ? (float)(px - x1) / (x2 - x1) : 0;
                const int16_t py = (int16_t)lroundf(yOf(values[i]) + t * (yOf(values[i + 1]) - yOf(values[i])));
                if (py < y + h - 1) mtb_Panel_Draw_VLine(px, py + 1, y + h - 1, fill);
            }
        }
    }
    for (int i = 0; i + 1 < count; i++) mtb_Panel_Draw_Line(xOf(i), yOf(values[i]), xOf(i + 1), yOf(values[i + 1]), color);
}

void mtb_Kit_Arrow(int16_t cx, int16_t cy, int16_t size, bool up, uint16_t color){
    const int16_t half = size / 2;
    if (up) mtb_Panel_Fill_Triangle(cx - half, cy + half / 2, cx + half, cy + half / 2, cx, cy - half, color);
    else mtb_Panel_Fill_Triangle(cx - half, cy - half / 2, cx + half, cy - half / 2, cx, cy + half, color);
}

uint16_t mtb_Kit_Blend(uint16_t a, uint16_t b, float t){
    if (t <= 0) return a;
    if (t >= 1) return b;
    const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (int)lroundf((br - ar) * t), g = ag + (int)lroundf((bg - ag) * t), bl = ab + (int)lroundf((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t mtb_Kit_Dim(uint16_t color, uint8_t level){
    const int r = ((color >> 11) & 31) * level / 255, g = ((color >> 5) & 63) * level / 255, b = (color & 31) * level / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

//##############################################################################################################
// Latin-1 Supplement and Latin Extended-A without accents, for names like "Atlético" or "Beşiktaş".
static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static const char LATIN_A[] =
    "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";

void mtb_Kit_To_Ascii(char* dest, size_t size, const char* text){
    if (size == 0) return;
    size_t used = 0;
    const uint8_t* p = (const uint8_t*)(text ? text : "");
    while (*p && used + 1 < size){
        uint32_t cp;
        int extra;
        if (*p < 0x80){ cp = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0){ cp = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0){ cp = *p & 0x0F; extra = 2; }
        else if ((*p & 0xF8) == 0xF0){ cp = *p & 0x07; extra = 3; }
        else { p++; continue; }
        bool valid = true;
        for (int i = 1; i <= extra; i++){
            if ((p[i] & 0xC0) != 0x80){ valid = false; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += valid ? extra + 1 : 1;
        if (!valid) continue;
        char c = 0;
        if (cp == '\n' || cp == '\r' || cp == '\t') c = ' ';
        else if (cp >= 0x20 && cp < 0x7F) c = (char)cp;
        else if (cp >= 0xC0 && cp <= 0xFF) c = LATIN1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) c = LATIN_A[cp - 0x100];
        else if (cp == 0x2019 || cp == 0x2018 || cp == 0x2032) c = '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0x2033) c = '"';
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) c = '-';
        else if (cp == 0xA0 || cp == 0x2009 || cp == 0x202F) c = ' ';
        else if (cp == 0x2026){
            if (used + 3 < size){ dest[used++] = '.'; dest[used++] = '.'; c = '.'; }
        }
        // Collapse runs of spaces.
        if (c == ' ' && (used == 0 || dest[used - 1] == ' ')) continue;
        if (c) dest[used++] = c;
    }
    while (used > 0 && dest[used - 1] == ' ') used--;
    dest[used] = '\0';
}

void mtb_Kit_Ago(char* dest, size_t size, int32_t seconds){
    if (seconds < 0) seconds = 0;
    if (seconds < 60) snprintf(dest, size, "just now");
    else if (seconds < 3600) snprintf(dest, size, "%d min ago", (int)(seconds / 60));
    else if (seconds < 86400) snprintf(dest, size, "%d h ago", (int)(seconds / 3600));
    else snprintf(dest, size, "%d d ago", (int)(seconds / 86400));
}

int32_t mtb_Kit_Days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

int mtb_Kit_Weekday(int y, int m, int d){
    const int32_t days = mtb_Kit_Days_From_Civil(y, m, d);     // 1970-01-01 was a Thursday (3, Monday first).
    return (int)(((days % 7) + 7 + 3) % 7);
}

int mtb_Kit_Days_In_Month(int y, int m){
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return DAYS[m - 1];
}

uint32_t mtb_Kit_Parse_Iso_Time(const char* text){
    if (text == nullptr) return 0;
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (sscanf(text, "%4d-%2d-%2d", &y, &mo, &d) != 3 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    const char* t = text + 10;
    int32_t offset = 0;
    if (*t == 'T' || *t == ' '){
        if (sscanf(t + 1, "%2d:%2d", &h, &mi) != 2) return 0;
        t += 6;
        if (*t == ':'){ s = atoi(t + 1); t += 3; }
        while (*t == '.' || isdigit((unsigned char)*t)) t++;
        if (*t == '+' || *t == '-'){
            int oh = 0, om = 0;
            if (sscanf(t + 1, "%2d:%2d", &oh, &om) >= 1) offset = (oh * 3600 + om * 60) * (*t == '+' ? 1 : -1);
        }
    }
    const int64_t unix = (int64_t)mtb_Kit_Days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - offset;
    return unix > 0 ? (uint32_t)unix : 0;
}

bool mtb_Kit_Parse_Color(const char* text, uint16_t& color){
    if (text == nullptr) return false;
    const char* hex = nullptr;
    size_t digits = 0;
    if (text[0] == '#'){ hex = text + 1; digits = 6; }
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){ hex = text + 2; digits = strlen(hex) == 8 ? 8 : 6; }
    else return false;
    if (strlen(hex) != digits) return false;
    for (size_t i = 0; i < digits; i++) if (!isxdigit((unsigned char)hex[i])) return false;
    const unsigned long value = strtoul(hex, nullptr, 16);
    const uint8_t r = (value >> 16) & 0xFF, g = (value >> 8) & 0xFF, b = value & 0xFF;
    color = MTB_KIT_RGB(r, g, b);
    return true;
}

int mtb_Kit_Parse_Clock(const char* text){
    if (text == nullptr || strlen(text) != 5 || text[2] != ':') return -1;
    static const int DIGITS[4] = {0, 1, 3, 4};
    for (int i : DIGITS) if (!isdigit((unsigned char)text[i])) return -1;
    const int hours = (text[0] - '0') * 10 + (text[1] - '0'), minutes = (text[3] - '0') * 10 + (text[4] - '0');
    return hours < 24 && minutes < 60 ? hours * 60 + minutes : -1;
}

void mtb_Kit_Clock_Text(char* dest, size_t size, uint16_t minutes){
    snprintf(dest, size, "%02u:%02u", (unsigned)((minutes / 60) % 24), (unsigned)(minutes % 60));
}

void mtb_Kit_Url_Encode(char* dest, size_t size, const char* text){
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char* c = (const unsigned char*)(text ? text : ""); *c && used + 4 < size; c++){
        if (isalnum(*c) || *c == '-' || *c == '_' || *c == '.' || *c == '~') dest[used++] = (char)*c;
        else { dest[used++] = '%'; dest[used++] = HEX_DIGITS[*c >> 4]; dest[used++] = HEX_DIGITS[*c & 15]; }
    }
    if (size) dest[used] = '\0';
}
