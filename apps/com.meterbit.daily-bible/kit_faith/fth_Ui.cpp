// Screen parts the FAITH apps share.
#include "fth_Ui.h"
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>

void fth_Text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back){
    char a[200], b[200];
    mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    mtb_Kit_Text(x, y, font, color, b, back);
}

void fth_Header(const char* left, uint16_t leftColor, const char* right){
    char a[96], b[96];
    int16_t rw = 0;
    mtb_Kit_To_Ascii(a, sizeof(a), right ? right : "");
    mtb_Kit_Upper(a);
    if (a[0]){
        mtb_Kit_Fit(b, sizeof(b), Terminal4x6, a, 150);
        rw = mtb_Kit_Text_Width(Terminal4x6, b);
        mtb_Kit_Text_Right(253, 12, Terminal4x6, KIT_LABEL, b, BLACK);
    }
    mtb_Kit_To_Ascii(a, sizeof(a), left ? left : "");
    mtb_Kit_Upper(a);
    mtb_Kit_Fit(b, sizeof(b), Terminal4x6, a, 248 - rw - 8);
    mtb_Kit_Text(3, 12, Terminal4x6, leftColor, b, BLACK);
    mtb_Panel_Draw_HLine(19, 0, 255, KIT_LINE);
}

void fth_Message(const char* title, const char* detail){
    char a[240], lines[4][64];
    mtb_Kit_To_Ascii(a, sizeof(a), title ? title : "");
    mtb_Kit_Text_Centre(128, 42, Terminal8x12, KIT_TEXT, a, BLACK);
    if (!detail) return;
    mtb_Kit_To_Ascii(a, sizeof(a), detail);
    const int n = mtb_Kit_Wrap(a, Terminal6x8, 240, &lines[0][0], sizeof(lines[0]), 4);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 60 + i * 10, Terminal6x8, KIT_LABEL, lines[i], BLACK);
}

#define VERSE_LINES 40
struct Lines_t { char line[VERSE_LINES][48]; uint8_t dim[VERSE_LINES]; int count; };
EXT_RAM_BSS_ATTR static Lines_t L;
EXT_RAM_BSS_ATTR static char ascii[FTH_VERSE_LEN + 8];

static void lines_Of(const Fth_Verse_t& v){
    L.count = 0;
    auto add = [&](const char* text, uint8_t dim){
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
        const int room = VERSE_LINES - L.count;
        if (room <= 0) return;
        const int n = mtb_Kit_Wrap(ascii, Terminal6x8, 246, &L.line[L.count][0], sizeof(L.line[0]), room);
        for (int i = 0; i < n; i++) L.dim[L.count + i] = dim;
        L.count += n;
    };
    if (v.extra[0]){
        add(v.extra, 1);
        if (L.count < VERSE_LINES){ L.line[L.count][0] = '\0'; L.dim[L.count++] = 0; }
    }
    add(v.text, 0);
}

int fth_Verse_Lines(const Fth_Verse_t& v){
    lines_Of(v);
    return L.count;
}

void fth_Draw_Verse(const Fth_Verse_t& v, const char* title, uint16_t titleColor, int scroll, const char* error){
    fth_Header(title, titleColor, v.text[0] ? v.reference : "");
    if (!v.text[0]){ fth_Message("Today's verse", error && error[0] ? error : "On its way: Pixlpal is reading it now."); return; }
    lines_Of(v);
    const int fit = FTH_VERSE_FIT;
    const int first = L.count > fit ? scroll % (L.count - fit + 1) : 0;
    for (int i = 0; i < fit && first + i < L.count; i++)
        mtb_Kit_Text(4, 22 + i * 10, Terminal6x8, L.dim[first + i] ? KIT_LABEL : KIT_TEXT, L.line[first + i], BLACK);
    if (L.count > fit){
        const int16_t top = 22, h = 88;
        mtb_Panel_Draw_VLine(254, top, top + h, KIT_LINE);
        const int16_t bh = (int16_t)(h * fit / L.count), by = (int16_t)(top + (h - bh) * first / (L.count - fit));
        mtb_Panel_Draw_VLine(254, by, by + bh, KIT_LABEL);
    }
}

void fth_Draw_Alert(const char* alert, const char* detail, bool flash, uint16_t color){
    if (!alert) return;
    const int16_t x1 = 14, y1 = 30, x2 = 241, y2 = 104;
    mtb_Panel_Fill_Rect(x1, y1, x2, y2, BLACK);
    const uint16_t border = flash ? color : mtb_Kit_Dim(color, 90);
    mtb_Panel_Draw_Rect(x1, y1, x2, y2, border);
    mtb_Panel_Draw_Rect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, border);
    char a[64], b[64];
    mtb_Kit_To_Ascii(a, sizeof(a), alert);
    mtb_Kit_Upper(a);
    if (mtb_Kit_Text_Width(Terminal10x16, a) <= x2 - x1 - 12) mtb_Kit_Text_Centre(128, y1 + 12, Terminal10x16, color, a, BLACK);
    else { mtb_Kit_Fit(b, sizeof(b), Terminal8x12, a, x2 - x1 - 12); mtb_Kit_Text_Centre(128, y1 + 14, Terminal8x12, color, b, BLACK); }
    if (detail && detail[0]){
        mtb_Kit_To_Ascii(a, sizeof(a), detail);
        mtb_Kit_Fit(b, sizeof(b), Terminal6x8, a, x2 - x1 - 12);
        mtb_Kit_Text_Centre(128, y1 + 38, Terminal6x8, KIT_TEXT, b, BLACK);
    }
    mtb_Kit_Text_Centre(128, y2 - 12, Terminal4x6, KIT_DIM, "CLICK TO DISMISS", BLACK);
}
