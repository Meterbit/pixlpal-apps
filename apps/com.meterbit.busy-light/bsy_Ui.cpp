// Busy Light's screens, the whole 256 x 128 panel: the sign, a knock, the reply from inside, and the day.
#include "bsy_App.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t bg){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text(x, y, font, color, fitted, bg);
}

static void centre_Fit(int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t bg){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text_Centre(128, y, font, color, fitted, bg);
}

/** The frame: a dark wash of the colour and a border @p glow bright. */
static void frame(uint16_t color, uint8_t glow, uint16_t& bg){
    bg = mtb_Kit_Dim(color, 34);
    mtb_Panel_Fill_Rect(0, 0, PANEL_RES_X - 1, PANEL_RES_Y - 1, bg);
    const uint16_t edge = mtb_Kit_Dim(color, glow);
    for (int i = 0; i < 3; i++) mtb_Panel_Draw_Rect(i, i, PANEL_RES_X - 1 - i, PANEL_RES_Y - 1 - i, edge);
}

/**
 * The status in the largest letters that fit: one line, or two split at a space, at twice the size of Terminal10x16
 * or Terminal8x12, else Terminal10x16 as it is. @p top..@p bottom is the room for it.
 */
static void big_Words(const char* text, uint16_t color, int16_t top, int16_t bottom){
    char ascii[64];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    struct Try_t { const uint8_t* font; int scale; int16_t h; };
    const Try_t tries[] = {{Terminal10x16, 2, 32}, {Terminal8x12, 2, 24}, {Terminal10x16, 1, 16}, {Terminal8x12, 1, 12}};
    const int16_t room = bottom - top, maxW = 240;
    // One line.
    for (const Try_t& t : tries){
        if (t.h > room) continue;
        const int16_t w = mtb_Kit_Big_Text_Width(t.font, t.scale, ascii);
        if (w <= maxW){ mtb_Kit_Big_Text((int16_t)(128 - w / 2), (int16_t)(top + (room - t.h) / 2), t.font, t.scale, color, ascii); return; }
    }
    // Two lines, split at the space nearest the middle.
    const int n = (int)strlen(ascii);
    int split = -1;
    for (int i = 0; i < n; i++) if (ascii[i] == ' ' && (split < 0 || abs(i - n / 2) < abs(split - n / 2))) split = i;
    if (split > 0){
        char a[64], b[64];
        snprintf(a, sizeof(a), "%.*s", split, ascii);
        snprintf(b, sizeof(b), "%s", ascii + split + 1);
        for (const Try_t& t : tries){
            const int16_t gap = (int16_t)(t.scale * 2);
            if (2 * t.h + gap > room) continue;
            const int16_t wa = mtb_Kit_Big_Text_Width(t.font, t.scale, a), wb = mtb_Kit_Big_Text_Width(t.font, t.scale, b);
            if (wa > maxW || wb > maxW) continue;
            const int16_t y = (int16_t)(top + (room - 2 * t.h - gap) / 2);
            mtb_Kit_Big_Text((int16_t)(128 - wa / 2), y, t.font, t.scale, color, a);
            mtb_Kit_Big_Text((int16_t)(128 - wb / 2), (int16_t)(y + t.h + gap), t.font, t.scale, color, b);
            return;
        }
    }
    centre_Fit((int16_t)(top + (room - 12) / 2), Terminal8x12, color, ascii, maxW, BLACK);
}

static void top_Line(const Bsy_View_t& v, uint16_t bg){
    const Bsy_Settings_t& s = *v.s;
    char clock[16];
    bsy_Clock(clock, sizeof(clock), v.now, v.utcOffset, s.hour24);
    const int16_t cx = mtb_Kit_Text_Right(PANEL_RES_X - 7, 6, Terminal6x8, KIT_LABEL, clock, bg);
    if (s.label[0]) fit_Text(7, 6, Terminal6x8, KIT_TEXT, s.label, (int16_t)(cx - 14), bg);
}

static void sign(const Bsy_View_t& v){
    const Bsy_Settings_t& s = *v.s;
    const Bsy_State_t& st = *v.st;
    const uint16_t color = bsy_Status_Color(st.status, s.customColor);
    const bool dim = st.status == BSY_OFF_WORK && s.offHours == BSY_OFF_DIM;
    const bool glows = s.pulse && (st.status == BSY_ON_AIR || st.status == BSY_PRESENTING);
    uint16_t bg;
    frame(dim ? mtb_Kit_Dim(color, 90) : color, glows ? v.glow : (uint8_t)(dim ? 60 : 255), bg);
    top_Line(v, bg);

    const char* words = st.status == BSY_CUSTOM ? s.customText : bsy_Status_Text(st.status);
    char detail[64];
    bsy_Detail(detail, sizeof(detail), st, s, v.now, v.utcOffset, v.weekday, v.minute);
    const bool title = s.showTitle && st.title[0] && st.from != BSY_FROM_PHONE;
    const bool bar = st.until > v.now && st.start && st.start < st.until;
    const bool knock = s.knock && bsy_Severity(st.status) >= 2;
    // The room the words get shrinks with what goes under them.
    int16_t under = 0;
    if (detail[0]) under += 14;
    if (bar) under += 7;
    if (title) under += 10;
    if (knock) under += 9;
    big_Words(words, dim ? mtb_Kit_Dim(color, 120) : color, 16, (int16_t)(PANEL_RES_Y - 6 - under));

    int16_t y = (int16_t)(PANEL_RES_Y - 6 - under);
    if (detail[0]){ centre_Fit(y + 1, Terminal8x12, dim ? KIT_LABEL : KIT_TEXT, detail, 240, bg); y += 14; }
    if (bar){
        const float f = (float)(v.now - st.start) / (float)(st.until - st.start);
        mtb_Kit_Bar(28, y + 1, 200, 3, f, color, mtb_Kit_Dim(color, 70));
        y += 7;
    }
    if (title){ centre_Fit(y + 1, Terminal6x8, KIT_LABEL, st.title, 236, bg); y += 10; }
    if (knock) centre_Fit(y + 1, Terminal4x6, KIT_LABEL, "TO KNOCK, PRESS THE KNOB", 236, bg);
}

static void knocked(const Bsy_View_t& v){
    uint16_t bg;
    const uint16_t amber = MTB_KIT_RGB(255, 170, 0);
    frame(amber, v.glow, bg);
    top_Line(v, bg);
    big_Words("KNOCK, KNOCK", amber, 18, 76);
    centre_Fit(82, Terminal8x12, KIT_TEXT, "WE'VE TOLD THEM YOU'RE HERE", 240, bg);
    char a[32], b[48];
    const uint32_t ago = v.now > v.knockedAt ? v.now - v.knockedAt : 0;
    if (ago < 60) snprintf(b, sizeof(b), "PLEASE WAIT FOR AN ANSWER");
    else { bsy_Span(a, sizeof(a), ago); snprintf(b, sizeof(b), "KNOCKED %s AGO, NO ANSWER YET", a); }
    centre_Fit(100, Terminal6x8, KIT_LABEL, b, 240, bg);
}

static void reply(const Bsy_View_t& v){
    const bool own = v.reply >= BSY_REPLIES;
    const uint16_t color = own ? KIT_CYAN : bsy_Reply_Color(v.reply);
    uint16_t bg;
    frame(color, 255, bg);
    top_Line(v, bg);
    big_Words(own ? (v.replyText ? v.replyText : "") : bsy_Reply_Text(v.reply), color, 16, 92);
    centre_Fit(100, Terminal6x8, KIT_LABEL, "AN ANSWER FROM INSIDE", 240, bg);
}

static void today(const Bsy_View_t& v){
    const Bsy_Settings_t& s = *v.s;
    mtb_Panel_Fill_Rect(0, 0, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    mtb_Kit_Text(6, 5, Terminal6x8, KIT_GOLD, "TODAY");
    char a[24], b[24], line[128];
    bsy_Clock(a, sizeof(a), v.now, v.utcOffset, s.hour24);
    mtb_Kit_Text_Right(PANEL_RES_X - 7, 5, Terminal6x8, KIT_LABEL, a);
    const uint32_t dayEnd = v.now + (uint32_t)(1440 - v.minute) * 60;
    int shown = 0;
    if (v.cal && v.cal->known){
        for (int i = 0; i < v.cal->count && shown < 8; i++){
            const Bsy_Event_t& e = v.cal->e[i];
            if (!bsy_Event_Busy(e, s.tentativeBusy) || e.end <= v.now || e.start >= dayEnd) continue;
            bsy_Clock(a, sizeof(a), e.start, v.utcOffset, s.hour24);
            bsy_Clock(b, sizeof(b), e.end, v.utcOffset, s.hour24);
            const bool now = e.start <= v.now;
            const uint8_t st = bsy_Event_Status(e);
            snprintf(line, sizeof(line), "%s-%s  %s", a, b, s.showTitle && e.title[0] ? e.title : bsy_Status_Name(st));
            const int16_t y = (int16_t)(19 + shown * 13);
            mtb_Panel_Fill_Rect(6, y, 8, y + 8, bsy_Status_Color(st, s.customColor));
            fit_Text(13, y, Terminal6x8, now ? KIT_TEXT : KIT_LABEL, line, 236, BLACK);
            shown++;
        }
        if (!shown) mtb_Kit_Text_Centre(128, 58, Terminal8x12, KIT_GREEN, "NO MORE MEETINGS TODAY");
    } else mtb_Kit_Text_Centre(128, 58, Terminal6x8, KIT_LABEL, "Link a calendar from the phone to see the day");
}

static void message(const char* text){
    mtb_Panel_Fill_Rect(0, 0, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    mtb_Kit_Text_Centre(128, 40, Terminal8x12, KIT_GOLD, "BUSY LIGHT");
    centre_Fit(64, Terminal6x8, KIT_TEXT, text, 240, BLACK);
}

void bsy_Draw(const Bsy_View_t& v){
    mtb_Panel_Begin_Update();
    if (v.message) message(v.message);
    else switch (v.screen){
        case BSY_SCREEN_KNOCKED: knocked(v); break;
        case BSY_SCREEN_REPLY: reply(v); break;
        case BSY_SCREEN_TODAY: today(v); break;
        default: sign(v); break;
    }
    mtb_Panel_End_Update();
}
