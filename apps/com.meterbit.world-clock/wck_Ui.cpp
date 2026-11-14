#include "wck_Ui.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <stdio.h>
#include <string.h>

static const uint16_t K_RULE = MTB_KIT_RGB(35, 35, 35);
static const uint16_t K_NIGHT = MTB_KIT_RGB(18, 24, 70);
static const uint16_t K_MORNING = MTB_KIT_RGB(90, 70, 20);
static const uint16_t K_OFFICE = MTB_KIT_RGB(20, 110, 50);
static const uint16_t K_EVENING = MTB_KIT_RGB(100, 45, 20);
static const char* const DAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static uint16_t wck_Period_Color(uint8_t p){
    return p == WCK_OFFICE ? K_OFFICE : p == WCK_MORNING ? K_MORNING : p == WCK_EVENING ? K_EVENING : K_NIGHT;
}

static void wck_Upper_Fit(char* dest, size_t size, const char* text, const uint8_t* font, int16_t width){
    char upper[48];
    snprintf(upper, sizeof(upper), "%s", text);
    mtb_Kit_Upper(upper);
    mtb_Kit_Fit(dest, size, font, upper, width);
}

//##############################################################################################################
// Every city on its own row: name and country, the period of its day, its time, and how far it is from here.
static void wck_Draw_Board(const Wck_View_t& v){
    const Wck_Settings_t& s = *v.settings;
    const int rows = s.count;
    const int16_t rowH = rows <= 4 ? 28 : 19;
    for (int i = 0; i < rows; i++){
        const Wck_City_t& c = s.cities[i];
        const int16_t y = 10 + i * rowH;
        if (i) mtb_Panel_Draw_HLine(y, 0, 255, K_RULE);
        const int32_t offset = wck_Offset_Of(c.tz, v.utc);
        Wck_Local_t t;
        wck_Local(v.utc, offset, t);
        const uint8_t period = wck_Period(t.hour, t.minute, s.officeFrom, s.officeTo);
        mtb_Panel_Fill_Rect(0, y + 2, 2, y + rowH - 2, wck_Period_Color(period) | MTB_KIT_RGB(40, 40, 40));
        const bool day = t.hour >= 6 && t.hour < 18;
        mtb_Kit_Weather_Icon(6, y + (rowH - 14) / 2, 14, KIT_WX_CLEAR, !day);
        char name[32], country[40], time[12], meridiem[4], text[24];
        wck_Upper_Fit(name, sizeof(name), c.name, Terminal8x12, 110);
        mtb_Kit_Text(24, y + (rowH > 20 ? 4 : 2), Terminal8x12, c.color ? c.color : KIT_TEXT, name);
        wck_Upper_Fit(country, sizeof(country), c.country, Terminal4x6, 110);
        mtb_Kit_Text(24, y + (rowH > 20 ? 18 : 13), Terminal4x6, KIT_LABEL, country);
        // How far from here, and the day if it is another.
        wck_Difference_Text(offset - v.localOffset, text, sizeof(text));
        const int dayDiff = wck_Day_Difference(v.utc, offset, v.localOffset);
        mtb_Kit_Text_Right(170, y + (rowH > 20 ? 6 : 3), Terminal4x6, KIT_LABEL, text);
        if (dayDiff) mtb_Kit_Text_Right(170, y + (rowH > 20 ? 14 : 10), Terminal4x6, KIT_GOLD, dayDiff > 0 ? "TOMORROW" : "YESTERDAY");
        wck_Clock_Text(t, s.hour24, time, sizeof(time), meridiem, sizeof(meridiem));
        const uint8_t* font = rowH > 20 ? Terminal10x17 : Terminal8x12;
        int16_t right = 252;
        if (meridiem[0]) right = mtb_Kit_Text_Right(252, y + (rowH > 20 ? 9 : 5), Terminal6x8, KIT_LABEL, meridiem) - 3;
        mtb_Kit_Text_Right(right, y + (rowH > 20 ? 5 : 3), font, KIT_TEXT, time);
    }
}

// One city large: its flag, the time in big digits, the date, the part of its day.
static void wck_Draw_Focus(const Wck_View_t& v, Mtb_Kit_Box_t& flagBox){
    const Wck_Settings_t& s = *v.settings;
    const Wck_City_t& c = s.cities[v.focus % s.count];
    const int32_t offset = wck_Offset_Of(c.tz, v.utc);
    Wck_Local_t t;
    wck_Local(v.utc, offset, t);
    flagBox = {4, 14, 72, 48};
    mtb_Panel_Draw_Rect(flagBox.x - 1, flagBox.y - 1, flagBox.x + flagBox.w, flagBox.y + flagBox.h, K_RULE);
    char name[32], country[40], text[40], time[12], meridiem[4];
    wck_Upper_Fit(name, sizeof(name), c.name, Terminal10x17, 170);
    mtb_Kit_Text(84, 14, Terminal10x17, c.color ? c.color : KIT_TEXT, name);
    wck_Upper_Fit(country, sizeof(country), c.country, Terminal6x8, 170);
    mtb_Kit_Text(84, 33, Terminal6x8, KIT_LABEL, country);
    snprintf(text, sizeof(text), "%s %d %s %d", DAYS[t.weekday], t.day, MONTHS[(t.month + 11) % 12], t.year);
    mtb_Kit_Text(84, 44, Terminal6x8, KIT_CYAN, text);
    char difference[16];
    wck_Difference_Text(offset - v.localOffset, difference, sizeof(difference));
    const int dayDiff = wck_Day_Difference(v.utc, offset, v.localOffset);
    snprintf(text, sizeof(text), "%s FROM HERE%s", difference, dayDiff > 0 ? ", TOMORROW" : dayDiff < 0 ? ", YESTERDAY" : "");
    mtb_Kit_Text(84, 54, Terminal4x6, KIT_LABEL, strcmp(difference, "SAME") == 0 ? "SAME TIME AS HERE" : text);
    // The time.
    wck_Clock_Text(t, s.hour24, time, sizeof(time), meridiem, sizeof(meridiem));
    const int16_t w = mtb_Kit_Big_Text_Width(Terminal10x17, 3, time);
    const int16_t x = 128 - (w + (meridiem[0] ? 22 : 0)) / 2;
    mtb_Kit_Big_Text(x, 66, Terminal10x17, 3, KIT_TEXT, time);
    if (meridiem[0]) mtb_Kit_Text(x + w + 4, 70, Terminal8x12, KIT_LABEL, meridiem);
    char seconds[4];
    snprintf(seconds, sizeof(seconds), "%02d", t.second);
    if (meridiem[0]) mtb_Kit_Text(x + w + 6, 96, Terminal6x8, KIT_DIM, seconds);
    else mtb_Kit_Text(x + w + 4, 100, Terminal8x12, KIT_DIM, seconds);
    const uint8_t period = wck_Period(t.hour, t.minute, s.officeFrom, s.officeTo);
    mtb_Panel_Fill_Rect(0, 120, 255, 127, wck_Period_Color(period));
    mtb_Kit_Text_Centre(128, 121, Terminal4x6, KIT_TEXT, wck_Period_Name(period), wck_Period_Color(period));
    // Which city of how many.
    snprintf(text, sizeof(text), "%u/%u", (unsigned)(v.focus % s.count + 1), (unsigned)s.count);
    mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_TEXT, text, wck_Period_Color(period));
}

// Every city's hours side by side: one column per hour, the cursor column at the time being planned.
static void wck_Draw_Planner(const Wck_View_t& v){
    const Wck_Settings_t& s = *v.settings;
    const int64_t at = v.utc + (int64_t)v.plannerShift * 60;
    // The header: the time planned, in your time.
    Wck_Local_t here;
    wck_Local(at, v.localOffset, here);
    char time[12], meridiem[4], text[48];
    wck_Clock_Text(here, s.hour24, time, sizeof(time), meridiem, sizeof(meridiem));
    snprintf(text, sizeof(text), "%s %s%s%s YOUR TIME", DAYS[here.weekday], time, meridiem[0] ? " " : "", meridiem);
    mtb_Panel_Fill_Rect(0, 10, 255, 19, MTB_KIT_RGB(20, 26, 40));
    mtb_Kit_Text(3, 11, Terminal6x8, v.plannerShift ? KIT_GOLD : KIT_TEXT, text, MTB_KIT_RGB(20, 26, 40));
    mtb_Kit_Text_Right(252, 12, Terminal4x6, KIT_LABEL, v.plannerShift ? "TURN TO MOVE" : "NOW: TURN TO PLAN", MTB_KIT_RGB(20, 26, 40));
    const int16_t nameW = 62, cellW = 12, cols = (256 - nameW) / cellW, cursor = cols / 2;
    const int rows = s.count;
    const int16_t rowH = (int16_t)((116 - 22) / (rows ? rows : 1));
    for (int i = 0; i < rows; i++){
        const Wck_City_t& c = s.cities[i];
        const int16_t y = 22 + i * rowH;
        char name[24];
        wck_Upper_Fit(name, sizeof(name), c.name, Terminal6x8, nameW - 4);
        mtb_Kit_Text(2, y + (rowH - 8) / 2, Terminal6x8, c.color ? c.color : KIT_TEXT, name);
        for (int col = 0; col < cols; col++){
            const int64_t when = at + (int64_t)(col - cursor) * 3600;
            const int32_t offset = wck_Offset_Of(c.tz, when);
            Wck_Local_t t;
            wck_Local(when, offset, t);
            const uint8_t period = wck_Period(t.hour, t.minute, s.officeFrom, s.officeTo);
            const int16_t x = nameW + col * cellW;
            const uint16_t fill = col == cursor ? mtb_Kit_Blend(wck_Period_Color(period), KIT_TEXT, 0.35f) : wck_Period_Color(period);
            mtb_Panel_Fill_Rect(x, y, x + cellW - 2, y + rowH - 2, fill);
            char hour[4];
            const int h = s.hour24 ? t.hour : (t.hour % 12 == 0 ? 12 : t.hour % 12);
            snprintf(hour, sizeof(hour), "%d", h);
            mtb_Kit_Text_Centre(x + cellW / 2 - 1, y + (rowH - 6) / 2, Terminal4x6, col == cursor ? BLACK : KIT_TEXT, hour, fill);
        }
    }
    // The key.
    struct { uint16_t color; const char* name; } key[] = {{K_OFFICE, "WORK"}, {K_MORNING, "MORNING"}, {K_EVENING, "EVENING"}, {K_NIGHT, "NIGHT"}};
    int16_t x = 2;
    for (const auto& k : key){
        mtb_Panel_Fill_Rect(x, 121, x + 5, 126, k.color);
        x = mtb_Kit_Text(x + 8, 121, Terminal4x6, KIT_LABEL, k.name) + 8;
    }
}

void wck_Draw(const Wck_View_t& v, Mtb_Kit_Box_t* flagBox){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (flagBox) *flagBox = {0, 0, 0, 0};
    if (v.settings->count == 0) return;
    if (!v.clockValid){
        mtb_Kit_Text_Centre(128, 50, Terminal10x17, KIT_GOLD, "SETTING THE CLOCK");
        mtb_Kit_Text_Centre(128, 74, Terminal6x8, KIT_LABEL, "The world's times appear once Pixlpal knows the time.");
        return;
    }
    switch (v.view){
        case WCK_VIEW_FOCUS: { Mtb_Kit_Box_t box; wck_Draw_Focus(v, box); if (flagBox) *flagBox = box; break; }
        case WCK_VIEW_PLANNER: wck_Draw_Planner(v); break;
        default: wck_Draw_Board(v); break;
    }
}
