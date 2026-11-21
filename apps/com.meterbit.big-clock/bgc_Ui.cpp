#include "bgc_Ui.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char* const WEEKDAYS[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char* const MONTHS[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
                                       "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

// A filled box shaded from top to bottom colour, by the panel row, so all digits share one gradient.
static void bgc_Shaded(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const Bgc_Palette_t& p, int16_t top, int16_t bottom){
    if (p.top == p.bottom){ mtb_Panel_Fill_Rect(x1, y1, x2, y2, p.top); return; }
    for (int16_t y = y1; y <= y2; y++) mtb_Panel_Draw_HLine(y, x1, x2, mtb_Kit_Blend(p.top, p.bottom, (float)(y - top) / (bottom - top)));
}

// A seven-segment digit; unlit segments in the ghost colour, or not drawn when @p ghosts is false (solid style).
static void bgc_Digit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, int digit, const Bgc_Palette_t& p, bool ghosts = true){
    static const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const uint8_t on = (digit >= 0 && digit <= 9) ? SEGMENTS[digit] : 0;
    const int16_t mid = y + (h - t) / 2;
    const int16_t box[7][4] = {
        {(int16_t)(x + t), y, (int16_t)(x + w - t - 1), (int16_t)(y + t - 1)},
        {(int16_t)(x + w - t), (int16_t)(y + t), (int16_t)(x + w - 1), (int16_t)(mid - 1)},
        {(int16_t)(x + w - t), (int16_t)(mid + t), (int16_t)(x + w - 1), (int16_t)(y + h - t - 1)},
        {(int16_t)(x + t), (int16_t)(y + h - t), (int16_t)(x + w - t - 1), (int16_t)(y + h - 1)},
        {x, (int16_t)(mid + t), (int16_t)(x + t - 1), (int16_t)(y + h - t - 1)},
        {x, (int16_t)(y + t), (int16_t)(x + t - 1), (int16_t)(mid - 1)},
        {(int16_t)(x + t), mid, (int16_t)(x + w - t - 1), (int16_t)(mid + t - 1)}};
    for (int s = 0; s < 7; s++){
        if (on & (1 << s)) bgc_Shaded(box[s][0], box[s][1], box[s][2], box[s][3], p, y, y + h - 1);
        else if (ghosts) mtb_Panel_Fill_Rect(box[s][0], box[s][1], box[s][2], box[s][3], p.ghost);
    }
}

static void bgc_Date_Line(char* dest, size_t size, const Bgc_View_t& v, bool longForm){
    if (longForm) snprintf(dest, size, "%s %d %s %d", WEEKDAYS[v.weekday % 7], v.day, MONTHS[(v.month + 11) % 12], v.year);
    else snprintf(dest, size, "%.3s %02d %.3s", WEEKDAYS[v.weekday % 7], v.day, MONTHS[(v.month + 11) % 12]);
}

static int bgc_Display_Hour(const Bgc_View_t& v){
    if (v.settings->hour24) return v.hour;
    return v.hour % 12 == 0 ? 12 : v.hour % 12;
}

//##############################################################################################################
static void bgc_Face_Segment(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    const int16_t w = 50, h = 92, t = 9, y = 6;
    const int16_t xs[4] = {4, 60, 146, 202};
    const int hour = bgc_Display_Hour(v);
    // A 12-hour clock leaves the first digit dark before ten.
    bgc_Digit(xs[0], y, w, h, t, (v.settings->hour24 || hour >= 10) ? hour / 10 : -1, p);
    bgc_Digit(xs[1], y, w, h, t, hour % 10, p);
    bgc_Digit(xs[2], y, w, h, t, v.minute / 10, p);
    bgc_Digit(xs[3], y, w, h, t, v.minute % 10, p);
    // The colon blinks with the seconds.
    const uint16_t colon = (v.second % 2 == 0 || !v.settings->showSeconds) ? p.accent : p.ghost;
    mtb_Panel_Fill_Rect(124, y + 24, 133, y + 33, colon);
    mtb_Panel_Fill_Rect(124, y + 58, 133, y + 67, colon);

    int16_t lineY = 108;
    if (v.settings->showSeconds){
        // Sixty ticks: the seconds gone, lit.
        for (int s = 0; s < 60; s++){
            const int16_t x = 8 + s * 4;
            mtb_Panel_Fill_Rect(x, 103, x + 2, 105, s <= v.second ? p.accent : p.ghost);
        }
        lineY = 112;
    }
    char text[48], meridiem[4] = "";
    if (!v.settings->hour24) snprintf(meridiem, sizeof(meridiem), "%s", v.hour < 12 ? "AM" : "PM");
    if (v.settings->showDate){
        bgc_Date_Line(text, sizeof(text), v, true);
        mtb_Kit_Text_Centre(128, lineY + 2, Terminal6x8, p.soft, text);
    }
    if (meridiem[0]) mtb_Kit_Text_Right(251, lineY + 2, Terminal6x8, p.accent, meridiem);
}

static void bgc_Face_Bold(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    char time[8], meridiem[4], seconds[4];
    bgc_Hour_Minute(time, sizeof(time), meridiem, sizeof(meridiem), v.hour, v.minute, v.settings->hour24, false);
    snprintf(seconds, sizeof(seconds), "%02d", v.second);
    const int16_t timeWidth = mtb_Kit_Text_Width(MS_Gothic31x60, time);
    const int16_t sideWidth = (v.settings->showSeconds || meridiem[0]) ? 30 : 0;
    const int16_t x = (256 - timeWidth - sideWidth) / 2;
    mtb_Kit_Text(x, 8, MS_Gothic31x60, p.main, time);
    const int16_t sideX = x + timeWidth + 6;
    if (meridiem[0]) mtb_Kit_Text(sideX, 14, Terminal10x17, p.soft, meridiem);
    if (v.settings->showSeconds) mtb_Kit_Text(sideX, 46, Terminal10x17, p.accent, seconds);
    if (v.settings->showDate){
        mtb_Panel_Draw_HLine(78, 24, 231, p.ghost);
        mtb_Kit_Text_Centre(128, 86, Terminal8x12, p.accent, WEEKDAYS[v.weekday % 7]);
        char date[32];
        snprintf(date, sizeof(date), "%d %s %d", v.day, MONTHS[(v.month + 11) % 12], v.year);
        mtb_Kit_Text_Centre(128, 104, Terminal8x12, p.soft, date);
    }
}

// A tapered hand: @p width wide at the centre, a point at the tip, a short tail behind the centre.
static void bgc_Hand(int16_t cx, int16_t cy, float angle, int16_t length, int16_t tail, int16_t width, uint16_t color){
    const float s = sinf(angle), c = cosf(angle);
    const int16_t tipX = cx + (int16_t)lroundf(s * length), tipY = cy - (int16_t)lroundf(c * length);
    const int16_t tailX = cx - (int16_t)lroundf(s * tail), tailY = cy + (int16_t)lroundf(c * tail);
    if (width <= 1){ mtb_Panel_Draw_Line(tailX, tailY, tipX, tipY, color); return; }
    const float half = width / 2.0f;
    const int16_t lx = cx - (int16_t)lroundf(c * half), ly = cy - (int16_t)lroundf(s * half);
    const int16_t rx = cx + (int16_t)lroundf(c * half), ry = cy + (int16_t)lroundf(s * half);
    mtb_Panel_Fill_Triangle(lx, ly, rx, ry, tipX, tipY, color);
    mtb_Panel_Fill_Triangle(lx, ly, rx, ry, tailX, tailY, color);
    mtb_Panel_Draw_Line(cx, cy, tipX, tipY, color);
}

// The dial and its hands, centred at (cx, cy) with radius r; the hands' lengths scale with it (as drawn at r 60).
static void bgc_Dial(const Bgc_View_t& v, int16_t cx, int16_t cy, int16_t r){
    const Bgc_Palette_t& p = v.palette;
    mtb_Panel_Draw_Circle(cx, cy, r, p.ghost);
    for (int m = 0; m < 60; m++){
        const float a = m * (float)M_PI / 30;
        const bool hourMark = m % 5 == 0;
        const int16_t inner = hourMark ? (m % 15 == 0 ? r - 10 : r - 7) : r - 3;
        const int16_t x1 = cx + (int16_t)lroundf(sinf(a) * inner), y1 = cy - (int16_t)lroundf(cosf(a) * inner);
        const int16_t x2 = cx + (int16_t)lroundf(sinf(a) * (r - 2)), y2 = cy - (int16_t)lroundf(cosf(a) * (r - 2));
        if (hourMark) mtb_Panel_Draw_Line(x1, y1, x2, y2, m % 15 == 0 ? p.main : p.soft);
        else mtb_Panel_Draw_PixelRGB565(x2, y2, p.soft);
    }
    const float minuteAngle = (v.minute + v.second / 60.0f) * (float)M_PI / 30;
    const float hourAngle = ((v.hour % 12) + v.minute / 60.0f) * (float)M_PI / 6;
    bgc_Hand(cx, cy, hourAngle, r * 32 / 60, r * 7 / 60, r * 7 / 60, p.main);
    bgc_Hand(cx, cy, minuteAngle, r * 50 / 60, r * 9 / 60, r * 5 / 60, p.soft == p.main ? p.main : mtb_Kit_Blend(p.main, p.soft, 0.25f));
    if (v.settings->showSeconds) bgc_Hand(cx, cy, v.second * (float)M_PI / 30, r * 54 / 60, r * 14 / 60, 1, p.accent);
    mtb_Panel_Fill_Circle(cx, cy, 4, p.accent);
    mtb_Panel_Fill_Circle(cx, cy, 1, BLACK);
}

static void bgc_Face_Analog(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    bgc_Dial(v, 64, 64, 60);

    char time[8], meridiem[4];
    bgc_Hour_Minute(time, sizeof(time), meridiem, sizeof(meridiem), v.hour, v.minute, v.settings->hour24, true);
    const int16_t right = 192;
    mtb_Kit_Big_Text(right - mtb_Kit_Big_Text_Width(Terminal10x17, 2, time) / 2, 14, Terminal10x17, 2, p.main, time);
    char line[16];
    if (v.settings->showSeconds) snprintf(line, sizeof(line), meridiem[0] ? ":%02d %s" : ":%02d", v.second, meridiem);
    else snprintf(line, sizeof(line), "%s", meridiem);
    mtb_Kit_Text_Centre(right, 54, Terminal8x12, p.accent, line);
    if (v.settings->showDate){
        mtb_Kit_Text_Centre(right, 78, Terminal8x12, p.soft, WEEKDAYS[v.weekday % 7]);
        char date[24];
        snprintf(date, sizeof(date), "%d %.3s %d", v.day, MONTHS[(v.month + 11) % 12], v.year);
        mtb_Kit_Text_Centre(right, 96, Terminal8x12, p.soft, date);
    }
}

static void bgc_Face_Words(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    char lines[4][16];
    const int count = bgc_Words(v.hour, v.minute, &lines[0][0], sizeof(lines[0]));
    // The hour word is twice the size; the rest in one size. Centred as a block.
    const int16_t small = 18, big = 36, gap = 3;
    const int hourLine = (strcmp(lines[count - 1], "O'CLOCK") == 0) ? count - 2 : count - 1;
    int16_t total = 0;
    for (int i = 0; i < count; i++) total += (i == hourLine ? big : small) + (i ? gap : 0);
    int16_t y = (120 - total) / 2;
    for (int i = 0; i < count; i++){
        const bool isHour = i == hourLine;
        const uint16_t color = i == 0 ? p.soft : isHour ? p.main : p.accent;
        if (isHour) mtb_Kit_Big_Text(128 - mtb_Kit_Big_Text_Width(MS_Gothic10x18, 2, lines[i]) / 2, y, MS_Gothic10x18, 2, color, lines[i]);
        else mtb_Kit_Text_Centre(128, y, MS_Gothic10x18, color, lines[i]);
        y += (isHour ? big : small) + gap;
    }
    // The minutes past the five the words show: up to four dots, as on classic word clocks.
    const int past = v.minute % 5;
    for (int i = 0; i < 4; i++) mtb_Panel_Fill_Circle(110 + i * 12, 122, 2, i < past ? p.accent : p.ghost);
}

//##############################################################################################################
// The time alone, as large as the panel allows. Segment and Bold: four digits the panel's height (Bold solid and
// thicker), the colon blinking with the seconds, AM/PM small between the colon's dots. Analog: the dial, centred, at
// the panel's height. Words: as its face, which shows only the time already.
static void bgc_Clock_Only(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    if (v.face == BGC_FACE_ANALOG){ bgc_Dial(v, 128, 64, 62); return; }
    if (v.face == BGC_FACE_WORDS){ bgc_Face_Words(v); return; }
    const bool bold = v.face == BGC_FACE_BOLD;
    const int16_t w = 56, h = 120, t = bold ? 16 : 12, y = 4;
    const int16_t xs[4] = {2, 64, 136, 198};
    const int hour = bgc_Display_Hour(v);
    bgc_Digit(xs[0], y, w, h, t, (v.settings->hour24 || hour >= 10) ? hour / 10 : -1, p, !bold);
    bgc_Digit(xs[1], y, w, h, t, hour % 10, p, !bold);
    bgc_Digit(xs[2], y, w, h, t, v.minute / 10, p, !bold);
    bgc_Digit(xs[3], y, w, h, t, v.minute % 10, p, !bold);
    const uint16_t colon = (v.second % 2 == 0 || !v.settings->showSeconds) ? p.accent : p.ghost;
    mtb_Panel_Fill_Rect(123, y + 28, 132, y + 39, colon);
    mtb_Panel_Fill_Rect(123, y + 80, 132, y + 91, colon);
    if (!v.settings->hour24){
        mtb_Kit_Text_Centre(128, y + 50, Terminal6x8, p.soft, v.hour < 12 ? "A" : "P");
        mtb_Kit_Text_Centre(128, y + 60, Terminal6x8, p.soft, "M");
    }
}

// The date alone: the day of the month in digits the panel's height on the left (solid for the Bold face, with
// unlit segments otherwise); the month and the weekday in large letters on the right, the year under them.
static void bgc_Calendar_Only(const Bgc_View_t& v){
    const Bgc_Palette_t& p = v.palette;
    const bool ghosts = v.face != BGC_FACE_BOLD;
    const int16_t w = 54, h = 112, t = ghosts ? 11 : 14, y = 8;
    if (v.day >= 10){
        bgc_Digit(6, y, w, h, t, v.day / 10, p, ghosts);
        bgc_Digit(66, y, w, h, t, v.day % 10, p, ghosts);
    } else {
        bgc_Digit(36, y, w, h, t, v.day, p, ghosts);
    }
    mtb_Panel_Draw_VLine(127, 12, 116, p.ghost);
    char month[4], weekday[4], year[8];
    snprintf(month, sizeof(month), "%.3s", MONTHS[(v.month + 11) % 12]);
    snprintf(weekday, sizeof(weekday), "%.3s", WEEKDAYS[v.weekday % 7]);
    snprintf(year, sizeof(year), "%d", v.year);
    const int16_t cx = 192;
    mtb_Kit_Big_Text(cx - mtb_Kit_Big_Text_Width(Terminal10x17, 3, month) / 2, 4, Terminal10x17, 3, p.main, month);
    mtb_Kit_Big_Text(cx - mtb_Kit_Big_Text_Width(Terminal10x17, 2, weekday) / 2, 62, Terminal10x17, 2, p.accent, weekday);
    mtb_Kit_Text_Centre(cx, 105, Terminal8x12, p.soft, year);
}

void bgc_Draw(const Bgc_View_t& v){
    mtb_Panel_Fill_Screen(BLACK);
    if (!v.clockValid){
        mtb_Kit_Text_Centre(128, 40, Terminal10x17, v.palette.main, "SETTING THE CLOCK");
        mtb_Kit_Text_Centre(128, 70, Terminal6x8, v.palette.soft, "Waiting for the time from the network");
        return;
    }
    if (v.settings->show == BGC_SHOW_CLOCK){ bgc_Clock_Only(v); return; }
    if (v.settings->show == BGC_SHOW_CALENDAR){ bgc_Calendar_Only(v); return; }
    switch (v.face){
        case BGC_FACE_BOLD: bgc_Face_Bold(v); break;
        case BGC_FACE_ANALOG: bgc_Face_Analog(v); break;
        case BGC_FACE_WORDS: bgc_Face_Words(v); break;
        default: bgc_Face_Segment(v); break;
    }
}
