#include "mtb_kit_weather.h"
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

void mtb_Kit_Wx_Band(const char* left, const char* right, uint16_t color){
    mtb_Panel_Fill_Rect(0, 10, 255, 20, color);
    char fitted[48];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, left, right && right[0] ? 170 : 248);
    mtb_Kit_Text(3, 12, Terminal6x8, KIT_TEXT, fitted, color);
    if (right && right[0]) mtb_Kit_Text_Right(252, 13, Terminal4x6, KIT_TEXT, right, color);
}

void mtb_Kit_Wx_Credit(const char* credit){ if (credit) mtb_Kit_Text_Right(252, 121, Terminal4x6, KIT_LABEL, credit); }

// "28" with a degree ring; returns the x after it.
static int16_t wx_Temp(int16_t x, int16_t y, const uint8_t* font, int scale, uint16_t color, float value, bool unit, bool imperial){
    if (std::isnan(value)) return mtb_Kit_Text(x, y, font, KIT_DIM, "--");
    char text[8];
    snprintf(text, sizeof(text), "%.0f", fabsf(value) < 0.5f ? 0.0f : value);     // No "-0".
    if (scale > 1) mtb_Kit_Big_Text(x, y, font, scale, color, text);
    else mtb_Kit_Text(x, y, font, color, text);
    int16_t after = x + mtb_Kit_Big_Text_Width(font, scale, text) + 2;
    const int16_t r = scale > 1 ? 3 : (font[6] >= 12 ? 2 : 1);
    mtb_Panel_Draw_Circle(after + r, y + r + 1, r, color);
    after += 2 * r + 3;
    if (unit) after = mtb_Kit_Text(after, y + (scale > 1 ? 2 : 0), scale > 1 ? Terminal8x12 : font, color, imperial ? "F" : "C");
    return after;
}

static void wx_Clock(char* dest, size_t size, uint32_t unix, int32_t offset){
    const uint32_t t = unix + offset;
    snprintf(dest, size, "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
}

static void wx_Updated(char* dest, size_t size, const Mtb_Kit_Wx_View_t& v, uint32_t fetched){
    if (fetched == 0 || v.now < fetched){ dest[0] = '\0'; return; }
    mtb_Kit_Ago(dest, size, (int32_t)(v.now - fetched));
    mtb_Kit_Upper(dest);
}

static uint32_t wx_Latest(const Mtb_Kit_Wx_Data_t& d){
    uint32_t latest = 0;
    for (uint32_t f : d.fetched) if (f > latest) latest = f;
    return latest;
}

//##############################################################################################################
static void wx_Draw_Now(const Mtb_Kit_Wx_View_t& v){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    const Mtb_Kit_Wx_Current_t& c = d.current;
    char right[24], place[KIT_WX_PLACE_LEN];
    wx_Updated(right, sizeof(right), v, wx_Latest(d));
    mtb_Kit_To_Ascii(place, sizeof(place), v.place ? v.place : "");
    mtb_Kit_Upper(place);
    mtb_Kit_Wx_Band(place, right, K_BAND);
    const bool imperial = d.imperial;
    mtb_Kit_Weather_Icon(4, 24, 52, c.icon, !c.day);
    wx_Temp(62, 26, Terminal10x17, 2, KIT_TEXT, c.temp, true, imperial);
    char text[40];
    mtb_Kit_Fit(text, sizeof(text), Terminal8x12, c.text, 112);
    mtb_Kit_Text(62, 64, Terminal8x12, KIT_GOLD, text);
    int16_t x = mtb_Kit_Text(62, 80, Terminal6x8, KIT_LABEL, "FEELS ");
    wx_Temp(x, 80, Terminal6x8, 1, KIT_TEXT, c.feels, false, imperial);
    const float high = d.dayCount ? d.days[0].high : NAN, low = d.dayCount ? d.days[0].low : NAN;
    x = mtb_Kit_Text(62, 92, Terminal6x8, K_WARM, "H ");
    x = wx_Temp(x, 92, Terminal6x8, 1, K_WARM, high, false, imperial);
    x = mtb_Kit_Text(x + 6, 92, Terminal6x8, K_COOL, "L ");
    wx_Temp(x, 92, Terminal6x8, 1, K_COOL, low, false, imperial);

    // The details column: the fourth row is the air when read, else the chance of rain.
    struct Row_t { const char* label; char value[24]; uint16_t color; };
    Row_t rows[4] = {{"HUMIDITY", "", KIT_TEXT}, {"WIND", "", KIT_TEXT}, {"UV INDEX", "", KIT_TEXT}, {"RAIN", "", KIT_TEXT}};
    if (!std::isnan(c.humidity)) snprintf(rows[0].value, sizeof(rows[0].value), "%.0f%%", c.humidity);
    if (!std::isnan(c.wind)) snprintf(rows[1].value, sizeof(rows[1].value), "%.0f %s %s", c.wind, imperial ? "MPH" : "KM/H", mtb_Kit_Wx_Cardinal(c.windDegrees));
    const float uv = !std::isnan(c.uv) ? c.uv : d.dayCount ? d.days[0].uvMax : NAN;
    if (!std::isnan(uv)){
        if (std::isnan(c.uv)) rows[2].label = "UV TODAY";
        snprintf(rows[2].value, sizeof(rows[2].value), "%.0f%s", uv, uv >= 8 ? " V.HIGH" : uv >= 6 ? " HIGH" : uv >= 3 ? " MOD" : " LOW");
    }
    if (d.air.valid){
        rows[3].label = "AIR";
        snprintf(rows[3].value, sizeof(rows[3].value), "%d %s", d.air.index, mtb_Kit_Aqi_Name(d.air.scale, d.air.index));
        char fitted[24];
        mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, rows[3].value, 76);
        snprintf(rows[3].value, sizeof(rows[3].value), "%s", fitted);
        rows[3].color = mtb_Kit_Aqi_Color(d.air.scale, d.air.index);
    } else {
        const float rain = !std::isnan(c.rainChance) ? c.rainChance : d.hourCount ? (float)d.hours[0].rain : NAN;
        if (!std::isnan(rain)) snprintf(rows[3].value, sizeof(rows[3].value), "%.0f%%", rain);
    }
    for (int i = 0; i < 4; i++){
        const int16_t y = 24 + i * 18;
        mtb_Kit_Text(178, y, Terminal4x6, KIT_LABEL, rows[i].label);
        mtb_Kit_Text(178, y + 7, Terminal6x8, rows[i].value[0] ? rows[i].color : KIT_DIM, rows[i].value[0] ? rows[i].value : "--");
    }
    // The wind's direction: an arrow the way it blows.
    if (!std::isnan(c.windDegrees) && c.wind > 0){
        const float a = (c.windDegrees + 180) * (float)M_PI / 180;       // From -> towards.
        const int16_t cx = 246, cy = 45;
        const int16_t tx = cx + (int16_t)lroundf(sinf(a) * 4), ty = cy - (int16_t)lroundf(cosf(a) * 4);
        mtb_Panel_Draw_Line(cx - (int16_t)lroundf(sinf(a) * 4), cy + (int16_t)lroundf(cosf(a) * 4), tx, ty, KIT_CYAN);
        mtb_Panel_Fill_Circle(tx, ty, 1, KIT_CYAN);
    }
    if (d.alertCount){
        const uint16_t red = d.alerts[0].severity >= KIT_WX_SEV_SEVERE ? MTB_KIT_RGB(180, 20, 20) : MTB_KIT_RGB(160, 90, 0);
        mtb_Panel_Fill_Rect(0, 104, 255, 114, red);
        char alert[80], fitted[80];
        snprintf(alert, sizeof(alert), "ALERT: %s", d.alerts[0].title);
        mtb_Kit_Upper(alert);
        mtb_Kit_Fit(fitted, sizeof(fitted), Terminal6x8, alert, 248);
        mtb_Kit_Text(4, 106, Terminal6x8, KIT_TEXT, fitted, red);
    }
    mtb_Kit_Wx_Credit(v.credit);
}

static void wx_Draw_Hours(const Mtb_Kit_Wx_View_t& v){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    const int step = d.hourStep ? d.hourStep : 1;
    char title[24], right[24];
    snprintf(title, sizeof(title), "NEXT %d HOURS", 12 * step);
    wx_Updated(right, sizeof(right), v, wx_Latest(d));
    mtb_Kit_Wx_Band(title, right, K_BAND);
    // From the hour now on.
    int first = 0;
    while (first + 1 < d.hourCount && d.hours[first + 1].start <= v.now) first++;
    const int shown = d.hourCount - first < 12 ? d.hourCount - first : 12;
    for (int i = 0; i < shown; i++){
        const Mtb_Kit_Wx_Hour_t& h = d.hours[first + i];
        const int16_t x = 4 + i * 21, cx = x + 10;
        char label[12];
        if (i == 0) snprintf(label, sizeof(label), "NOW");
        else snprintf(label, sizeof(label), "%02u", (unsigned)(((h.start + d.utcOffset) / 3600) % 24));
        mtb_Kit_Text_Centre(cx, 23, Terminal4x6, i == 0 ? KIT_GOLD : KIT_LABEL, label);
        mtb_Kit_Weather_Icon(x + 2, 30, 16, h.icon, !h.day);
        char temp[6];
        snprintf(temp, sizeof(temp), "%.0f", std::isnan(h.temp) || fabsf(h.temp) < 0.5f ? 0.0f : h.temp);
        mtb_Kit_Text_Centre(cx, 49, Terminal6x8, KIT_TEXT, std::isnan(h.temp) ? "--" : temp);
        const int16_t top = 60, bottom = 96;
        const int16_t height = (int16_t)((bottom - top) * (h.rain > 100 ? 100 : h.rain) / 100);
        mtb_Panel_Fill_Rect(x + 6, top, x + 14, bottom, KIT_LINE);
        if (height > 0) mtb_Panel_Fill_Rect(x + 6, bottom - height, x + 14, bottom, K_RAIN);
        char rain[6];
        snprintf(rain, sizeof(rain), "%u", (unsigned)h.rain);
        mtb_Kit_Text_Centre(cx, 99, Terminal4x6, h.rain >= 50 ? K_RAIN : KIT_DIM, rain);
    }
    mtb_Kit_Text(4, 110, Terminal4x6, KIT_DIM, "BARS: CHANCE OF RAIN, %");
    if (d.hourCount - first >= 2){
        float temps[KIT_WX_HOURS];
        int n = 0;
        for (int i = first; i < d.hourCount; i++) if (!std::isnan(d.hours[i].temp)) temps[n++] = d.hours[i].temp;
        char span[16];
        snprintf(span, sizeof(span), "%d H", (d.hourCount - first) * step);
        mtb_Kit_Text(112, 110, Terminal4x6, KIT_DIM, span);
        if (n >= 2) mtb_Kit_Sparkline(134, 107, 68, 10, temps, n, K_WARM);
    }
    mtb_Kit_Wx_Credit(v.credit);
}

// Days [from, from + 7) on their own scale.
static void wx_Draw_Day_Rows(const Mtb_Kit_Wx_View_t& v, int from, const char* title){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    char right[24];
    wx_Updated(right, sizeof(right), v, wx_Latest(d));
    mtb_Kit_Wx_Band(title, right, K_BAND);
    const int to = d.dayCount < from + 7 ? d.dayCount : from + 7;
    float low = 1e9f, high = -1e9f;
    for (int i = from; i < to; i++){
        if (!std::isnan(d.days[i].low) && d.days[i].low < low) low = d.days[i].low;
        if (!std::isnan(d.days[i].high) && d.days[i].high > high) high = d.days[i].high;
    }
    const float span = high > low ? high - low : 1;
    for (int i = from; i < to; i++){
        const Mtb_Kit_Wx_Day_t& day = d.days[i];
        const int16_t y = 23 + (i - from) * 14;
        char label[8];
        if (i == 0) snprintf(label, sizeof(label), "TODAY");
        else if (from == 0) snprintf(label, sizeof(label), "%s", WEEKDAYS[mtb_Kit_Weekday(day.year, day.month, day.mday)]);
        else snprintf(label, sizeof(label), "%.2s %2u", WEEKDAYS[mtb_Kit_Weekday(day.year, day.month, day.mday)], (unsigned)day.mday);
        mtb_Kit_Text(4, y + 2, Terminal6x8, i == 0 ? KIT_GOLD : KIT_TEXT, label);
        mtb_Kit_Weather_Icon(42, y, 13, day.icon, false);
        char rain[8];
        snprintf(rain, sizeof(rain), "%u%%", (unsigned)day.rain);
        mtb_Kit_Text(60, y + 3, Terminal4x6, day.rain >= 50 ? K_RAIN : KIT_DIM, rain);
        char t[8];
        snprintf(t, sizeof(t), "%.0f", std::isnan(day.low) ? 0.0f : day.low);
        mtb_Kit_Text_Right(110, y + 2, Terminal6x8, K_COOL, std::isnan(day.low) ? "--" : t);
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
    mtb_Kit_Wx_Credit(v.credit);
}

static void wx_Draw_Sky(const Mtb_Kit_Wx_View_t& v){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    mtb_Kit_Wx_Band("SUN AND MOON", "", K_BAND);
    // Moon from the date: every provider agrees on that.
    const float age = mtb_Kit_Moon_Age(v.now);
    mtb_Kit_Moon(206, 56, 22, age);
    mtb_Kit_Text_Centre(206, 84, Terminal4x6, KIT_TEXT, mtb_Kit_Moon_Name(age));
    if (d.dayCount == 0 || d.days[0].sunrise == 0){
        mtb_Kit_Text_Centre(80, 60, Terminal6x8, KIT_LABEL, "Waiting for the forecast");
        mtb_Kit_Wx_Credit(v.credit);
        return;
    }
    const Mtb_Kit_Wx_Day_t& today = d.days[0];
    char rise[8] = "--:--", set[8] = "--:--";
    if (today.sunrise) wx_Clock(rise, sizeof(rise), today.sunrise, d.utcOffset);
    if (today.sunset) wx_Clock(set, sizeof(set), today.sunset, d.utcOffset);
    const int16_t cx = 80, cy = 92, r = 60;
    for (int a = 0; a <= 180; a += 3){
        const float rad = a * (float)M_PI / 180;
        mtb_Panel_Draw_PixelRGB565(cx - (int16_t)lroundf(cosf(rad) * r), cy - (int16_t)lroundf(sinf(rad) * (r - 18)), KIT_DIM);
    }
    mtb_Panel_Draw_HLine(cy, cx - r - 8, cx + r + 8, KIT_LINE);
    if (today.sunrise && today.sunset && v.now > today.sunrise && v.now < today.sunset){
        const float rad = (float)(v.now - today.sunrise) / (today.sunset - today.sunrise) * (float)M_PI;
        mtb_Kit_Weather_Icon(cx - (int16_t)lroundf(cosf(rad) * r) - 8, cy - (int16_t)lroundf(sinf(rad) * (r - 18)) - 8, 16, KIT_WX_CLEAR, false);
    }
    mtb_Kit_Text(cx - r - 10, cy + 4, Terminal4x6, KIT_LABEL, "SUNRISE");
    mtb_Kit_Text(cx - r - 10, cy + 11, Terminal8x12, KIT_GOLD, rise);
    mtb_Kit_Text_Right(cx + r + 10, cy + 4, Terminal4x6, KIT_LABEL, "SUNSET");
    mtb_Kit_Text_Right(cx + r + 10, cy + 11, Terminal8x12, K_WARM, set);
    if (today.sunset > today.sunrise){
        const uint32_t length = today.sunset - today.sunrise;
        char day[32];
        snprintf(day, sizeof(day), "%uh %02um OF DAYLIGHT", (unsigned)(length / 3600), (unsigned)(length % 3600 / 60));
        mtb_Kit_Text_Centre(cx, 26, Terminal4x6, KIT_LABEL, day);
    }
    mtb_Kit_Wx_Credit(v.credit);
}

static void wx_Draw_Air(const Mtb_Kit_Wx_View_t& v){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    const Mtb_Kit_Wx_Air_t& a = d.air;
    static const char* const SCALES[3] = {"US AQI", "EUROPEAN AQI", "AIR QUALITY 1-5"};
    mtb_Kit_Wx_Band("AIR QUALITY", SCALES[a.scale < 3 ? a.scale : 0], K_BAND);
    if (!a.valid){ mtb_Kit_Text_Centre(128, 60, Terminal6x8, KIT_LABEL, "Not read yet"); mtb_Kit_Wx_Credit(v.credit); return; }
    const uint16_t color = mtb_Kit_Aqi_Color(a.scale, a.index);
    char text[24];
    snprintf(text, sizeof(text), "%d", a.index);
    mtb_Kit_Big_Text(6, 26, Terminal10x17, 2, color, text);
    char name[24];
    mtb_Kit_Fit(name, sizeof(name), Terminal8x12, mtb_Kit_Aqi_Name(a.scale, a.index), 118);
    mtb_Kit_Text(6, 64, Terminal8x12, color, name);
    // The index's scale, with a marker.
    const int maxIndex = a.scale == KIT_AQI_EU ? 120 : a.scale == KIT_AQI_FIVE ? 5 : 300;
    for (int16_t px = 6; px <= 120; px++){
        const int at = a.scale == KIT_AQI_FIVE ? 1 + (px - 6) * 5 / 115 : (px - 6) * maxIndex / 114;
        mtb_Panel_Draw_VLine(px, 82, 85, mtb_Kit_Dim(mtb_Kit_Aqi_Color(a.scale, at), 150));
    }
    const int16_t marker = 6 + (int16_t)(114 * (a.scale == KIT_AQI_FIVE ? (a.index - 1) / 4.0f : (a.index > maxIndex ? maxIndex : a.index) / (float)maxIndex));
    mtb_Panel_Fill_Rect(marker - 1, 79, marker + 1, 88, KIT_TEXT);

    // The pollutants, and pollen where it is measured.
    struct Item_t { const char* label; float value; };
    const Item_t items[4] = {{"PM2.5", a.pm25}, {"PM10", a.pm10}, {"OZONE", a.ozone}, {"NO2", a.no2}};
    for (int i = 0; i < 4; i++){
        const int16_t y = 25 + i * 11;
        mtb_Kit_Text(136, y + 1, Terminal4x6, KIT_LABEL, items[i].label);
        if (std::isnan(items[i].value)) snprintf(text, sizeof(text), "--");
        else snprintf(text, sizeof(text), "%.0f", items[i].value);
        mtb_Kit_Text_Right(214, y, Terminal6x8, KIT_TEXT, text);
    }
    mtb_Kit_Text(218, 26, Terminal4x6, KIT_DIM, "UG/M3");
    bool pollen = false;
    for (float p : a.pollen) if (!std::isnan(p)) pollen = true;
    if (pollen){
        static const char* const POLLEN[3] = {"GRASS", "BIRCH", "RAGWEED"};
        mtb_Kit_Text(136, 72, Terminal4x6, KIT_GOLD, "POLLEN, GRAINS/M3");
        for (int i = 0; i < 3; i++){
            const int16_t y = 80 + i * 9;
            mtb_Kit_Text(136, y, Terminal4x6, KIT_LABEL, POLLEN[i]);
            if (std::isnan(a.pollen[i])) snprintf(text, sizeof(text), "--");
            else snprintf(text, sizeof(text), "%.0f", a.pollen[i]);
            mtb_Kit_Text_Right(214, y - 1, Terminal6x8, a.pollen[i] >= 50 ? KIT_ORANGE : KIT_TEXT, text);
        }
    }
    mtb_Kit_Wx_Credit(v.credit);
}

static void wx_Draw_Alerts(const Mtb_Kit_Wx_View_t& v){
    const Mtb_Kit_Wx_Data_t& d = *v.data;
    if (d.alertCount == 0){
        mtb_Kit_Wx_Band("WEATHER ALERTS", "", K_BAND);
        mtb_Kit_Text_Centre(128, 60, Terminal8x12, KIT_GREEN, "NO ALERTS IN FORCE");
        mtb_Kit_Wx_Credit(v.credit);
        return;
    }
    const Mtb_Kit_Wx_Alert_t& a = d.alerts[0];
    static const char* const LEVELS[] = {"ALERT", "MINOR", "MODERATE", "SEVERE", "EXTREME"};
    const uint16_t color = a.severity >= KIT_WX_SEV_SEVERE ? MTB_KIT_RGB(180, 20, 20) : a.severity == KIT_WX_SEV_MODERATE ? MTB_KIT_RGB(170, 100, 0) : MTB_KIT_RGB(120, 110, 0);
    char right[16];
    snprintf(right, sizeof(right), "1 OF %u", (unsigned)d.alertCount);
    mtb_Kit_Wx_Band(LEVELS[a.severity <= KIT_WX_SEV_EXTREME ? a.severity : 0], right, color);
    char lines[3][80];
    const int n = mtb_Kit_Wrap(a.title, Terminal8x12, 248, &lines[0][0], sizeof(lines[0]), 3);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 24 + i * 14, Terminal8x12, KIT_TEXT, lines[i]);
    const int16_t y = 24 + n * 14 + 4;
    char text[72];
    mtb_Kit_Fit(text, sizeof(text), Terminal6x8, a.area, 248);
    mtb_Kit_Text(4, y, Terminal6x8, KIT_GOLD, text);
    if (a.end){
        char until[8];
        wx_Clock(until, sizeof(until), a.end, d.utcOffset);
        const int days = (int)(((int64_t)a.end + d.utcOffset) / 86400) - (int)(((int64_t)v.now + d.utcOffset) / 86400);
        if (days > 0) snprintf(text, sizeof(text), "UNTIL %s IN %d DAYS", until, days);
        else snprintf(text, sizeof(text), "UNTIL %s", until);
    } else snprintf(text, sizeof(text), "UNTIL FURTHER NOTICE");
    mtb_Kit_Text(4, y + 12, Terminal6x8, KIT_LABEL, text);
    char source[72], fitted[72];
    snprintf(source, sizeof(source), "ISSUED BY %s", a.source[0] ? a.source : "THE LOCAL AUTHORITY");
    mtb_Kit_Upper(source);
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal4x6, source, 248);
    mtb_Kit_Text(4, y + 28, Terminal4x6, KIT_TEXT, fitted);
    mtb_Kit_Wx_Credit(v.credit);
}

//##############################################################################################################
static bool wx_Has_Page(uint8_t page, const Mtb_Kit_Wx_Data_t& d, bool withApp){
    switch (page){
        case KIT_WX_PAGE_HOURS: return d.hourCount > 1;
        case KIT_WX_PAGE_DAYS: return d.dayCount > 0;
        case KIT_WX_PAGE_WEEKS: return d.dayCount > 7;
        case KIT_WX_PAGE_SKY: return d.dayCount > 0 && d.days[0].sunrise != 0;
        case KIT_WX_PAGE_AIR: return d.air.valid != 0;
        case KIT_WX_PAGE_ALERTS: return d.alertCount > 0;
        case KIT_WX_PAGE_APP: return withApp;
        default: return true;
    }
}

uint8_t mtb_Kit_Wx_Next_Page(uint8_t page, const Mtb_Kit_Wx_Data_t& d, bool withApp){
    for (int i = 1; i <= KIT_WX_PAGE_COUNT; i++){
        const uint8_t next = (uint8_t)((page + i) % KIT_WX_PAGE_COUNT);
        if (wx_Has_Page(next, d, withApp)) return next;
    }
    return KIT_WX_PAGE_NOW;
}

void mtb_Kit_Wx_Draw(const Mtb_Kit_Wx_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (v.message || v.data == nullptr){
        mtb_Kit_Wx_Band(v.appName ? v.appName : "WEATHER", "", K_BAND);
        mtb_Kit_Text_Centre(128, 40, Terminal10x17, KIT_GOLD, v.message ? v.message : "");
        if (v.detail){
            char lines[4][80];
            const int n = mtb_Kit_Wrap(v.detail, Terminal6x8, 236, &lines[0][0], sizeof(lines[0]), 4);
            for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 66 + i * 11, Terminal6x8, KIT_LABEL, lines[i]);
        }
        mtb_Kit_Wx_Credit(v.credit);
        return;
    }
    switch (v.page){
        case KIT_WX_PAGE_HOURS: wx_Draw_Hours(v); break;
        case KIT_WX_PAGE_DAYS: wx_Draw_Day_Rows(v, 0, v.data->dayCount >= 7 ? "7 DAYS" : "DAYS AHEAD"); break;
        case KIT_WX_PAGE_WEEKS: wx_Draw_Day_Rows(v, 7, "DAYS 8 TO 14"); break;
        case KIT_WX_PAGE_SKY: wx_Draw_Sky(v); break;
        case KIT_WX_PAGE_AIR: wx_Draw_Air(v); break;
        case KIT_WX_PAGE_ALERTS: wx_Draw_Alerts(v); break;
        default: wx_Draw_Now(v); break;
    }
}
