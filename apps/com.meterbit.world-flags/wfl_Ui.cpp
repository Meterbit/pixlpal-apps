#include "wfl_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>

static const uint16_t K_BAND = MTB_KIT_RGB(14, 22, 44);
static const uint16_t K_BACK = MTB_KIT_RGB(0, 0, 16);

static int wfl_Wrapped(int16_t x, int16_t y, const uint8_t* font, int16_t lineH, uint16_t color, const char* text, int16_t width, int maxLines){
    char lines[4][64];
    const int n = mtb_Kit_Wrap(text, font, width, &lines[0][0], sizeof(lines[0]), maxLines > 4 ? 4 : maxLines);
    for (int i = 0; i < n; i++) mtb_Kit_Text(x, y + i * lineH, font, color, lines[i], K_BACK);
    return n;
}

Mtb_Kit_Box_t wfl_Draw(const Wfl_View_t& v){
    const Wfl_Settings_t& s = *v.settings;
    mtb_Panel_Fill_Rect(0, 0, 255, 127, K_BACK);
    // The band.
    char left[40], right[16];
    if (s.mode == WFL_ONE) snprintf(left, sizeof(left), "FLAG");
    else {
        const char* from = s.source == WFL_FAVOURITE ? "FAVOURITES" : s.source == WFL_CONTINENT ? wfl_Continent_Name(s.continent) : "THE WORLD";
        snprintf(left, sizeof(left), "%s: %s", s.mode == WFL_QUIZ ? "QUIZ" : "FLAGS", from);
        mtb_Kit_Upper(left);
    }
    mtb_Panel_Fill_Rect(0, 0, 255, 9, K_BAND);
    mtb_Kit_Text(3, 2, Terminal4x6, KIT_LABEL, left, K_BAND);
    if (s.mode != WFL_ONE && v.total){ snprintf(right, sizeof(right), "%d / %d", v.position, v.total); mtb_Kit_Text_Right(252, 2, Terminal4x6, KIT_LABEL, right, K_BAND); }
    if (s.mode != WFL_ONE) mtb_Kit_Bar(0, 10, 256, 1, v.progress, s.mode == WFL_QUIZ && !v.revealed ? KIT_GOLD : MTB_KIT_RGB(40, 90, 160), K_BACK);

    const bool details = s.showInfo || s.mode == WFL_QUIZ;
    if (!details) return {6, 14, 244, 112};
    const Mtb_Kit_Box_t box = {4, 16, 150, 100};
    const int16_t x = 162, w = 90;
    const Wfl_Country_t& c = *v.country;
    if (s.mode == WFL_QUIZ && !v.revealed){
        mtb_Kit_Text(x, 18, Terminal8x12, KIT_GOLD, "WHICH", K_BACK);
        mtb_Kit_Text(x, 31, Terminal8x12, KIT_GOLD, "COUNTRY?", K_BACK);
        wfl_Draw_Progress(v);
        wfl_Wrapped(x, 96, Terminal4x6, 7, KIT_LABEL, "CLICK THE KNOB TO SEE THE ANSWER", w, 3);
        return box;
    }
    int16_t y = 16;
    const int n = wfl_Wrapped(x, y, Terminal8x12, 13, s.mode == WFL_QUIZ ? KIT_GOLD : KIT_TEXT, c.name, w, 3);
    y += n * 13 + 6;
    if (c.capital[0]){
        mtb_Kit_Text(x, y, Terminal4x6, KIT_LABEL, "CAPITAL", K_BACK);
        y += 8;
        y += wfl_Wrapped(x, y, Terminal6x8, 10, KIT_CYAN, c.capital, w, 2) * 10 + 4;
    }
    if (c.continent[0] && y < 108){
        char continent[24];
        snprintf(continent, sizeof(continent), "%s", c.continent);
        mtb_Kit_Upper(continent);
        mtb_Kit_Text(x, y, Terminal4x6, KIT_LABEL, "CONTINENT", K_BACK);
        mtb_Kit_Text(x, y + 8, Terminal6x8, MTB_KIT_RGB(120, 220, 120), continent, K_BACK);
    }
    return box;
}

void wfl_Draw_Flag_Placeholder(const Wfl_View_t& v, Mtb_Kit_Box_t box){
    mtb_Panel_Draw_Rect(box.x, box.y, box.x + box.w - 1, box.y + box.h - 1, MTB_KIT_RGB(40, 40, 60));
    mtb_Kit_Text_Centre(box.x + box.w / 2, box.y + box.h / 2 - 4, Terminal6x8, KIT_DIM, v.flagState == 2 ? "NO FLAG" : "LOADING", K_BACK);
}

void wfl_Draw_Progress(const Wfl_View_t& v){
    const Wfl_Settings_t& s = *v.settings;
    if (s.mode == WFL_ONE) return;
    mtb_Kit_Bar(0, 10, 256, 1, v.progress, s.mode == WFL_QUIZ && !v.revealed ? KIT_GOLD : MTB_KIT_RGB(40, 90, 160), K_BACK);
    if (s.mode == WFL_QUIZ && !v.revealed){
        char seconds[16];
        const int left = (int)((1 - v.progress) * s.quizSeconds + 0.99f);
        snprintf(seconds, sizeof(seconds), "%d", left < 0 ? 0 : left);
        mtb_Panel_Fill_Rect(162, 52, 252, 87, K_BACK);
        mtb_Kit_Big_Text(162, 52, Terminal10x17, 2, KIT_TEXT, seconds);
    }
}
