#include "mtb_kit_news.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>

static const uint16_t K_ROW = MTB_KIT_RGB(18, 22, 32);

static void kit_News_Band(const char* left, const char* right, uint16_t color, bool fresh){
    mtb_Panel_Fill_Rect(0, 10, 255, 20, color);
    int16_t x = 3;
    if (fresh){
        mtb_Panel_Fill_Rect(2, 11, 22, 19, KIT_GOLD);
        mtb_Kit_Text(4, 13, Terminal4x6, BLACK, "NEW", KIT_GOLD);
        x = 26;
    }
    char fitted[40];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, left, 200 - x);
    mtb_Kit_Text(x, 12, Terminal6x8, KIT_TEXT, fitted, color);
    if (right && right[0]) mtb_Kit_Text_Right(252, 13, Terminal4x6, KIT_TEXT, right, color);
}

static const char* kit_Feed_Name(const Mtb_Kit_News_View_t& v, uint8_t feed){
    return feed < v.config->feedCount ? v.config->feeds[feed].name : "NEWS";
}

static uint16_t kit_Feed_Color(const Mtb_Kit_News_View_t& v, uint8_t feed){
    return feed < v.config->feedCount ? v.config->feeds[feed].color : MTB_KIT_RGB(60, 60, 80);
}

void mtb_Kit_News_Draw_Card(const Mtb_Kit_News_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.orderCount == 0) return;
    const int index = v.current % v.orderCount;
    const Mtb_Kit_Story_t& story = v.data->stories[v.order[index]];
    char right[16];
    snprintf(right, sizeof(right), "%d/%d", index + 1, v.orderCount);
    const uint16_t color = kit_Feed_Color(v, story.feed);
    kit_News_Band(kit_Feed_Name(v, story.feed), right, color, story.fresh);

    // The source and how old the story is.
    char source[40];
    mtb_Kit_Fit(source, sizeof(source), Terminal6x8, story.source[0] ? story.source : "UNKNOWN SOURCE", 170);
    const int16_t after = mtb_Kit_Text(4, 24, Terminal6x8, KIT_GOLD, source);
    if (story.published && v.now > story.published){
        char age[20];
        mtb_Kit_Ago(age, sizeof(age), (int32_t)(v.now - story.published));
        mtb_Kit_Upper(age);
        mtb_Kit_Text(after + 6, 25, Terminal4x6, KIT_LABEL, age);
    }

    // The headline, as large as it fits whole; or the summary.
    struct Size_t { const uint8_t* font; int16_t line; int lines; };
    static const Size_t SIZES[] = {{Terminal10x17, 20, 4}, {Terminal8x12, 14, 6}, {Terminal6x8, 10, 8}};
    char lines[9][KIT_NEWS_TITLE];
    const char* text = v.details ? story.summary : story.title;
    if (v.details && text[0] == '\0') text = "No summary came with this story.";
    int start = v.details ? 2 : 0;          // The summary in the small size, to fit more.
    for (int s = start; s < 3; s++){
        const int used = mtb_Kit_Wrap(text, SIZES[s].font, 248, &lines[0][0], sizeof(lines[0]), SIZES[s].lines + 1);
        if (used <= SIZES[s].lines || s == 2){
            const int shown = used < SIZES[s].lines ? used : SIZES[s].lines;
            if (used > SIZES[s].lines) mtb_Kit_Wrap(text, SIZES[s].font, 248, &lines[0][0], sizeof(lines[0]), SIZES[s].lines);
            for (int i = 0; i < shown; i++) mtb_Kit_Text(4, 36 + i * SIZES[s].line, SIZES[s].font, v.details ? KIT_LABEL : KIT_TEXT, lines[i]);
            break;
        }
    }

    // How long until the next story, and the credit.
    const int16_t creditWidth = v.credit ? mtb_Kit_Text_Width(Terminal4x6, v.credit) : 0;
    const int16_t barEnd = 248 - creditWidth - 6;
    if (v.paused) mtb_Kit_Text(4, 121, Terminal4x6, KIT_LABEL, "PAUSED: LONG PRESS TO GO ON");
    else mtb_Kit_Bar(4, 123, barEnd - 4, 2, v.progress, color, KIT_LINE);
    if (v.credit) mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_DIM, v.credit);
}

void mtb_Kit_News_Draw_List(const Mtb_Kit_News_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.orderCount == 0) return;
    const int current = v.current % v.orderCount;
    char right[16];
    snprintf(right, sizeof(right), "%d/%d", current + 1, v.orderCount);
    kit_News_Band("HEADLINES", right, MTB_KIT_RGB(30, 34, 50), false);
    const int rows = 6;          // The seventh would meet the credit line.
    int first = current - 2;
    if (first > v.orderCount - rows) first = v.orderCount - rows;
    if (first < 0) first = 0;
    for (int r = 0; r < rows && first + r < v.orderCount; r++){
        const int i = first + r;
        const Mtb_Kit_Story_t& story = v.data->stories[v.order[i]];
        const int16_t y = 22 + r * 15;
        const bool lit = i == current;
        const uint16_t background = lit ? K_ROW : BLACK;
        if (lit) mtb_Panel_Fill_Rect(0, y - 1, 255, y + 13, K_ROW);
        mtb_Panel_Fill_Rect(2, y, 4, y + 12, kit_Feed_Color(v, story.feed));
        char meta[48], age[16] = "";
        if (story.published && v.now > story.published) mtb_Kit_Ago(age, sizeof(age), (int32_t)(v.now - story.published));
        snprintf(meta, sizeof(meta), "%s%s%s", story.source, age[0] ? "  " : "", age);
        mtb_Kit_Upper(meta);
        char fitted[64];
        mtb_Kit_Fit(fitted, sizeof(fitted), Terminal4x6, meta, 200);
        mtb_Kit_Text(8, y, Terminal4x6, story.fresh ? KIT_GOLD : KIT_LABEL, fitted, background);
        mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, story.title, 244);
        mtb_Kit_Text(8, y + 6, Terminal6x8, lit ? KIT_TEXT : MTB_KIT_RGB(190, 190, 190), fitted, background);
    }
    if (v.credit) mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_DIM, v.credit);
}

void mtb_Kit_News_Draw_Message(const char* title, const char* detail, const char* credit){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    kit_News_Band("NEWS", "", MTB_KIT_RGB(30, 34, 50), false);
    mtb_Kit_Text_Centre(128, 40, Terminal10x17, KIT_GOLD, title);
    if (detail){
        char lines[4][80];
        const int n = mtb_Kit_Wrap(detail, Terminal6x8, 236, &lines[0][0], sizeof(lines[0]), 4);
        for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 68 + i * 11, Terminal6x8, KIT_LABEL, lines[i]);
    }
    if (credit) mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_DIM, credit);
}
