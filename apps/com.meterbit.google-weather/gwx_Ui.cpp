#include "gwx_Ui.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t K_BAND = MTB_KIT_RGB(10, 50, 100);
static const uint16_t K_RAIN = MTB_KIT_RGB(70, 150, 255);
static const uint16_t K_WARM = MTB_KIT_RGB(255, 150, 40);
static const uint16_t K_COOL = MTB_KIT_RGB(80, 170, 255);
static const char* const WEEKDAYS[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};      // Monday first

static void gwx_Band(const char* left, const char* right, uint16_t color = K_BAND){
    mtb_Panel_Fill_Rect(0, 10, 255, 20, color);
    char fitted[48];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, left, right && right[0] ? 170 : 248);
    mtb_Kit_Text(3, 12, Terminal6x8, KIT_TEXT, fitted, color);
    if (right && right[0]) mtb_Kit_Text_Right(252, 13, Terminal4x6, KIT_TEXT, right, color);
}

// "Google Maps", as the terms ask: unmodified, on one line.
static void gwx_Attribution(void){ mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_LABEL, "Google Maps"); }

// "28" with a degree ring; returns the x after it.
static int16_t gwx_Temp(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, float value, bool unit, bool imperial){
    if (std::isnan(value)) return mtb_Kit_Text(x, y, font, KIT_DIM, "--");
    char text[8];
    snprintf(text, sizeof(text), "%.0f", value == 0 ? 0.0f : value);     // No "-0".
    if (scale > 1) mtb_Kit_Big_Text(x, y, font, scale, color, text);
    else mtb_Kit_Text(x, y, font, color, text);
    int16_t after = x + mtb_Kit_Big_Text_Width(font, scale, text) + 2;
    const int16_t r = scale > 1 ? 3 : (font[6] >= 12 ? 2 : 1);
    mtb_Panel_Draw_Circle(after + r, y + r + 1, r, color);
    after += 2 * r + 3;
    if (unit) after = mtb_Kit_Text(after, y + (scale > 1 ? 2 : 0), scale > 1 ? Terminal8x12 : font, color, imperial ? "F" : "C");
    return after;
}

static void gwx_Local(uint32_t unix, int32_t offset, int& hour, int& minute){
    const uint32_t t = unix + offset;
    hour = (int)((t / 3600) % 24);
    minute = (int)((t / 60) % 60);
}

static void gwx_Clock(char* dest, size_t size, uint32_t unix, int32_t offset){
    int h, m;
    gwx_Local(unix, offset, h, m);
    snprintf(dest, size, "%02d:%02d", h, m);
}

static void gwx_Updated(char* dest, size_t size, const Gwx_View_t& v, uint32_t fetched){
    if (fetched == 0 || v.now < fetched){ dest[0] = '\0'; return; }
    mtb_Kit_Ago(dest, size, (int32_t)(v.now - fetched));
    mtb_Kit_Upper(dest);
}

//##############################################################################################################
static void gwx_Draw_Now(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    const Gwx_Current_t& c = d.current;
    char right[24], place[GWX_PLACE_LEN];
    gwx_Updated(right, sizeof(right), v, d.fetched[0]);
    mtb_Kit_To_Ascii(place, sizeof(place), v.settings->place);
    mtb_Kit_Upper(place);
    gwx_Band(place, right);
    const bool imperial = v.settings->imperial;
    mtb_Kit_Weather_Icon(4, 24, 52, c.icon, !c.day);
    gwx_Temp(62, 26, Terminal10x17, 2, KIT_TEXT, c.temp, true, imperial);
    char text[40];
    mtb_Kit_Fit(text, sizeof(text), Terminal8x12, c.text, 112);
    mtb_Kit_Text(62, 64, Terminal8x12, KIT_GOLD, text);
    int16_t x = mtb_Kit_Text(62, 80, Terminal6x8, KIT_LABEL, "FEELS ");
    gwx_Temp(x, 80, Terminal6x8, 1, KIT_TEXT, c.feels, false, imperial);
    // Today's range.
    const float high = d.dayCount ? d.days[0].high : c.high, low = d.dayCount ? d.days[0].low : c.low;
    x = mtb_Kit_Text(62, 92, Terminal6x8, K_WARM, "H ");
    x = gwx_Temp(x, 92, Terminal6x8, 1, K_WARM, high, false, imperial);
    x = mtb_Kit_Text(x + 6, 92, Terminal6x8, K_COOL, "L ");
    gwx_Temp(x, 92, Terminal6x8, 1, K_COOL, low, false, imperial);

    // The details column.
    struct Row_t { const char* label; char value[20]; };
    Row_t rows[4] = {{"HUMIDITY", ""}, {"WIND", ""}, {"UV INDEX", ""}, {"RAIN", ""}};
    if (c.humidity >= 0) snprintf(rows[0].value, sizeof(rows[0].value), "%.0f%%", c.humidity);
    if (c.wind >= 0) snprintf(rows[1].value, sizeof(rows[1].value), "%.0f %s %s", c.wind, imperial ? "MPH" : "KM/H", c.windFrom);
    if (c.uv >= 0) snprintf(rows[2].value, sizeof(rows[2].value), "%.0f%s", c.uv, c.uv >= 8 ? " V.HIGH" : c.uv >= 6 ? " HIGH" : c.uv >= 3 ? " MOD" : " LOW");
    if (c.rainChance >= 0) snprintf(rows[3].value, sizeof(rows[3].value), "%.0f%%", c.rainChance);
    for (int i = 0; i < 4; i++){
        const int16_t y = 24 + i * 18;
        mtb_Kit_Text(178, y, Terminal4x6, KIT_LABEL, rows[i].label);
        mtb_Kit_Text(178, y + 7, Terminal6x8, rows[i].value[0] ? KIT_TEXT : KIT_DIM, rows[i].value[0] ? rows[i].value : "--");
    }
    // The wind's direction: an arrow the way it blows.
    if (c.windDegrees >= 0 && c.wind > 0){
        const float a = (c.windDegrees + 180) * (float)M_PI / 180;       // From -> towards.
        const int16_t cx = 246, cy = 45;
        const int16_t tx = cx + (int16_t)lroundf(sinf(a) * 4), ty = cy - (int16_t)lroundf(cosf(a) * 4);
        mtb_Panel_Draw_Line(cx - (int16_t)lroundf(sinf(a) * 4), cy + (int16_t)lroundf(cosf(a) * 4), tx, ty, KIT_CYAN);
        mtb_Panel_Fill_Circle(tx, ty, 1, KIT_CYAN);
    }
    // An alert in force, on a red ribbon.
    if (d.alertCount){
        const uint16_t red = d.alerts[0].severity >= GWX_SEV_SEVERE ? MTB_KIT_RGB(180, 20, 20) : MTB_KIT_RGB(160, 90, 0);
        mtb_Panel_Fill_Rect(0, 104, 255, 114, red);
        char alert[72];
        snprintf(alert, sizeof(alert), "ALERT: %s", d.alerts[0].title);
        mtb_Kit_Upper(alert);
        char fitted[72];
        mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, alert, 248);
        mtb_Kit_Text(4, 106, Terminal6x8, KIT_TEXT, fitted, red);
    }
    gwx_Attribution();
}

static void gwx_Draw_Hours(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    char right[24];
    gwx_Updated(right, sizeof(right), v, d.fetched[1]);
    gwx_Band("NEXT 12 HOURS", right);
    const int shown = d.hourCount < 12 ? d.hourCount : 12;
    for (int i = 0; i < shown; i++){
        const Gwx_Hour_t& h = d.hours[i];
        const int16_t x = 4 + i * 21, cx = x + 10;
        int hour, minute;
        gwx_Local(h.start, d.utcOffset, hour, minute);
        char label[6];
        snprintf(label, sizeof(label), i == 0 ? "NOW" : "%02d", hour);
        mtb_Kit_Text_Centre(cx, 23, Terminal4x6, i == 0 ? KIT_GOLD : KIT_LABEL, label);
        mtb_Kit_Weather_Icon(x + 2, 30, 16, h.icon, !h.day);
        char temp[6];
        snprintf(temp, sizeof(temp), "%.0f", h.temp == 0 ? 0.0f : h.temp);
        mtb_Kit_Text_Centre(cx, 49, Terminal6x8, KIT_TEXT, temp);
        // The chance of rain, as a bar.
        const int16_t top = 60, bottom = 96;
        const int16_t height = (int16_t)((bottom - top) * h.rain / 100);
        mtb_Panel_Fill_Rect(x + 6, top, x + 14, bottom, KIT_LINE);
        if (height > 0) mtb_Panel_Fill_Rect(x + 6, bottom - height, x + 14, bottom, K_RAIN);
        char rain[6];
        snprintf(rain, sizeof(rain), "%u", (unsigned)h.rain);
        mtb_Kit_Text_Centre(cx, 99, Terminal4x6, h.rain >= 50 ? K_RAIN : KIT_DIM, rain);
    }
    mtb_Kit_Text(4, 110, Terminal4x6, KIT_DIM, "BARS: CHANCE OF RAIN, %");
    if (d.hourCount >= 2){
        float temps[GWX_HOURS];
        for (int i = 0; i < d.hourCount; i++) temps[i] = d.hours[i].temp;
        mtb_Kit_Text(112, 110, Terminal4x6, KIT_DIM, "24 H");
        mtb_Kit_Sparkline(132, 107, 70, 10, temps, d.hourCount, K_WARM);
    }
    gwx_Attribution();
}

static void gwx_Draw_Days(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    char right[24];
    gwx_Updated(right, sizeof(right), v, d.fetched[2]);
    gwx_Band("7 DAYS", right);
    const int shown = d.dayCount < 7 ? d.dayCount : 7;
    float low = 1e9f, high = -1e9f;
    for (int i = 0; i < shown; i++){
        if (!std::isnan(d.days[i].low) && d.days[i].low < low) low = d.days[i].low;
        if (!std::isnan(d.days[i].high) && d.days[i].high > high) high = d.days[i].high;
    }
    const float span = high > low ? high - low : 1;
    for (int i = 0; i < shown; i++){
        const Gwx_Day_t& day = d.days[i];
        const int16_t y = 23 + i * 14;
        const int wd = mtb_Kit_Weekday(day.year, day.month, day.mday);
        mtb_Kit_Text(4, y + 2, Terminal6x8, i == 0 ? KIT_GOLD : KIT_TEXT, i == 0 ? "TODAY" : WEEKDAYS[wd]);
        mtb_Kit_Weather_Icon(42, y, 13, day.icon, false);
        char rain[8];
        snprintf(rain, sizeof(rain), "%u%%", (unsigned)day.rain);
        mtb_Kit_Text(60, y + 3, Terminal4x6, day.rain >= 50 ? K_RAIN : KIT_DIM, rain);
        char t[8];
        snprintf(t, sizeof(t), "%.0f", std::isnan(day.low) ? 0.0f : day.low);
        mtb_Kit_Text_Right(110, y + 2, Terminal6x8, K_COOL, std::isnan(day.low) ? "--" : t);
        // The day's range on the week's scale, cool to warm.
        const int16_t x1 = 116, x2 = 214;
        mtb_Panel_Fill_Rect(x1, y + 4, x2, y + 5, KIT_LINE);
        if (!std::isnan(day.low) && !std::isnan(day.high)){
            const int16_t a = x1 + (int16_t)lroundf((day.low - low) / span * (x2 - x1));
            const int16_t b = x1 + (int16_t)lroundf((day.high - low) / span * (x2 - x1));
            for (int16_t px = a; px <= b; px++) mtb_Panel_Draw_VLine(px, y + 3, y + 6, mtb_Kit_Blend(K_COOL, K_WARM, (float)(px - x1) / (x2 - x1)));
        }
        snprintf(t, sizeof(t), "%.0f", std::isnan(day.high) ? 0.0f : day.high);
        mtb_Kit_Text(220, y + 2, Terminal6x8, K_WARM, std::isnan(day.high) ? "--" : t);
    }
    gwx_Attribution();
}

static void gwx_Draw_Sky(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    gwx_Band("SUN AND MOON", "");
    if (d.dayCount == 0){ mtb_Kit_Text_Centre(128, 60, Terminal6x8, KIT_LABEL, "Waiting for the forecast"); gwx_Attribution(); return; }
    const Gwx_Day_t& today = d.days[0];
    char rise[8] = "--:--", set[8] = "--:--";
    if (today.sunrise) gwx_Clock(rise, sizeof(rise), today.sunrise, d.utcOffset);
    if (today.sunset) gwx_Clock(set, sizeof(set), today.sunset, d.utcOffset);
    // The sun's arc, and where it is on it now.
    const int16_t cx = 80, cy = 92, r = 60;
    for (int a = 0; a <= 180; a += 3){
        const float rad = a * (float)M_PI / 180;
        mtb_Panel_Draw_PixelRGB565(cx - (int16_t)lroundf(cosf(rad) * r), cy - (int16_t)lroundf(sinf(rad) * (r - 18)), KIT_DIM);
    }
    mtb_Panel_Draw_HLine(cy, cx - r - 8, cx + r + 8, KIT_LINE);
    if (today.sunrise && today.sunset && v.now > today.sunrise && v.now < today.sunset){
        const float f = (float)(v.now - today.sunrise) / (today.sunset - today.sunrise);
        const float rad = f * (float)M_PI;
        mtb_Kit_Weather_Icon(cx - (int16_t)lroundf(cosf(rad) * r) - 8, cy - (int16_t)lroundf(sinf(rad) * (r - 18)) - 8, 16, KIT_WX_CLEAR, false);
    }
    mtb_Kit_Text(cx - r - 10, cy + 4, Terminal4x6, KIT_LABEL, "SUNRISE");
    mtb_Kit_Text(cx - r - 10, cy + 11, Terminal8x12, KIT_GOLD, rise);
    mtb_Kit_Text_Right(cx + r + 10, cy + 4, Terminal4x6, KIT_LABEL, "SUNSET");
    mtb_Kit_Text_Right(cx + r + 10, cy + 11, Terminal8x12, K_WARM, set);
    if (today.sunrise && today.sunset > today.sunrise){
        const uint32_t length = today.sunset - today.sunrise;
        char day[32];
        snprintf(day, sizeof(day), "%uh %02um OF DAYLIGHT", (unsigned)(length / 3600), (unsigned)(length % 3600 / 60));
        mtb_Kit_Text_Centre(cx, 26, Terminal4x6, KIT_LABEL, day);
    }
    // The moon.
    static const float AGES[8] = {0, 3.7f, 7.4f, 11.1f, 14.8f, 18.5f, 22.1f, 25.8f};
    const float age = today.moonPhase < 8 ? AGES[today.moonPhase] : mtb_Kit_Moon_Age(v.now);
    mtb_Kit_Moon(206, 56, 22, age);
    mtb_Kit_Text_Centre(206, 84, Terminal4x6, KIT_TEXT, mtb_Kit_Moon_Name(age));
    gwx_Attribution();
}

static void gwx_Draw_Alerts(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    if (d.alertCount == 0){ gwx_Band("WEATHER ALERTS", ""); mtb_Kit_Text_Centre(128, 60, Terminal8x12, KIT_GREEN, "NO ALERTS IN FORCE"); gwx_Attribution(); return; }
    const Gwx_Alert_t& a = d.alerts[0];
    static const char* const LEVELS[] = {"ALERT", "MINOR", "MODERATE", "SEVERE", "EXTREME"};
    const uint16_t color = a.severity >= GWX_SEV_SEVERE ? MTB_KIT_RGB(180, 20, 20) : a.severity == GWX_SEV_MODERATE ? MTB_KIT_RGB(170, 100, 0) : MTB_KIT_RGB(120, 110, 0);
    char right[16];
    snprintf(right, sizeof(right), "1 OF %u", (unsigned)d.alertCount);
    gwx_Band(LEVELS[a.severity <= GWX_SEV_EXTREME ? a.severity : 0], right, color);
    char lines[3][80];
    const int n = mtb_Kit_Wrap(a.title, Terminal8x12, 248, &lines[0][0], sizeof(lines[0]), 3);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 24 + i * 14, Terminal8x12, KIT_TEXT, lines[i]);
    int16_t y = 24 + n * 14 + 4;
    char text[64];
    mtb_Kit_Fit(text, sizeof(text), Terminal6x8, a.area, 248);
    mtb_Kit_Text(4, y, Terminal6x8, KIT_GOLD, text);
    if (a.end){
        char until[8];
        gwx_Clock(until, sizeof(until), a.end, d.utcOffset);
        const int days = (int)((a.end + d.utcOffset) / 86400) - (int)((v.now + d.utcOffset) / 86400);
        snprintf(text, sizeof(text), days > 0 ? "UNTIL %s IN %d DAYS" : "UNTIL %s", until, days);
    } else snprintf(text, sizeof(text), "UNTIL FURTHER NOTICE");
    mtb_Kit_Text(4, y + 12, Terminal6x8, KIT_LABEL, text);
    // The agency that issued it, as Google requires.
    snprintf(text, sizeof(text), "ISSUED BY %s", a.source[0] ? a.source : "THE LOCAL AUTHORITY");
    mtb_Kit_Upper(text);
    char fitted[64];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal4x6, text, 248);
    mtb_Kit_Text(4, y + 28, Terminal4x6, KIT_TEXT, fitted);
    gwx_Attribution();
}

static void gwx_Draw_Usage(const Gwx_View_t& v){
    const Gwx_Data_t& d = *v.data;
    gwx_Band("GOOGLE WEATHER API", "YOUR BUDGET");
    const uint32_t budget = v.settings->monthlyBudget;
    char text[64];
    snprintf(text, sizeof(text), "%u", (unsigned)d.usedCalls);
    const int16_t x = mtb_Kit_Text(4, 25, Terminal10x17, KIT_GOLD, text);
    snprintf(text, sizeof(text), "OF %u CALLS THIS MONTH", (unsigned)budget);
    mtb_Kit_Text(x + 6, 31, Terminal6x8, KIT_TEXT, text);
    mtb_Kit_Bar(4, 48, 248, 6, budget ? (float)d.usedCalls / budget : 0, d.usedCalls * 10 > budget * 9 ? KIT_ORANGE : KIT_GREEN);
    if (v.interval) snprintf(text, sizeof(text), "CONDITIONS EVERY %u MIN, FORECASTS LESS OFTEN", (unsigned)((v.interval + 30) / 60));
    else snprintf(text, sizeof(text), "BUDGET SPENT: WAITING FOR NEXT MONTH");
    mtb_Kit_Text(4, 60, Terminal4x6, KIT_LABEL, text);
    char lines[4][80];
    const int n = mtb_Kit_Wrap("GOOGLE BILLS YOUR CLOUD PROJECT FOR USE BEYOND ITS FREE MONTHLY CALLS. SET THE BUDGET IN THE PIXLPAL APP; GOOGLE'S CONSOLE HAS THE EXACT COUNT.",
                               Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 4);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 74 + i * 8, Terminal4x6, KIT_DIM, lines[i]);
    gwx_Attribution();
}

void gwx_Draw(const Gwx_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.message){
        gwx_Band("GOOGLE WEATHER", "");
        mtb_Kit_Text_Centre(128, 40, Terminal10x17, KIT_GOLD, v.message);
        if (v.detail){
            char lines[4][80];
            const int n = mtb_Kit_Wrap(v.detail, Terminal6x8, 236, &lines[0][0], sizeof(lines[0]), 4);
            for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 66 + i * 11, Terminal6x8, KIT_LABEL, lines[i]);
        }
        gwx_Attribution();
        return;
    }
    switch (v.page){
        case GWX_PAGE_HOURS: gwx_Draw_Hours(v); break;
        case GWX_PAGE_DAYS: gwx_Draw_Days(v); break;
        case GWX_PAGE_SKY: gwx_Draw_Sky(v); break;
        case GWX_PAGE_ALERTS: gwx_Draw_Alerts(v); break;
        case GWX_PAGE_USAGE: gwx_Draw_Usage(v); break;
        default: gwx_Draw_Now(v); break;
    }
}
