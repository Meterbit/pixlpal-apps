#include "bgc_Model.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>

void bgc_Settings_Default(Bgc_Settings_t& settings){
    memset(&settings, 0, sizeof(settings));
    settings.magic = BGC_SETTINGS_MAGIC;
    settings.face = BGC_FACE_SEGMENT;
    settings.theme = BGC_THEME_CLASSIC;
    settings.showSeconds = 1;
    settings.showDate = 1;
    settings.nightStyle = BGC_NIGHT_RED;
    settings.nightFrom = 22 * 60;
    settings.nightTo = 7 * 60;
    settings.customColor = MTB_KIT_RGB(255, 80, 160);
}

void bgc_Settings_Clamp(Bgc_Settings_t& settings){
    if (settings.face >= BGC_FACE_COUNT) settings.face = BGC_FACE_SEGMENT;
    if (settings.theme >= BGC_THEME_COUNT) settings.theme = BGC_THEME_CLASSIC;
    settings.hour24 = settings.hour24 ? 1 : 0;
    settings.showSeconds = settings.showSeconds ? 1 : 0;
    settings.showDate = settings.showDate ? 1 : 0;
    settings.night = settings.night ? 1 : 0;
    if (settings.autoFaceMinutes > 60) settings.autoFaceMinutes = 60;
    if (settings.nightStyle > BGC_NIGHT_DIM) settings.nightStyle = BGC_NIGHT_RED;
    settings.nightFrom %= 1440;
    settings.nightTo %= 1440;
}

bool bgc_In_Window(uint16_t from, uint16_t to, uint16_t minute){
    if (from == to) return false;
    if (from < to) return minute >= from && minute < to;
    return minute >= from || minute < to;      // Across midnight.
}

uint16_t bgc_Hue(uint16_t hue, uint8_t value){
    hue %= 360;
    const int sector = hue / 60, f = (hue % 60) * 255 / 60;
    int r = 0, g = 0, b = 0;
    switch (sector){
        case 0: r = 255; g = f; break;
        case 1: r = 255 - f; g = 255; break;
        case 2: g = 255; b = f; break;
        case 3: g = 255 - f; b = 255; break;
        case 4: r = f; b = 255; break;
        default: r = 255; b = 255 - f; break;
    }
    return MTB_KIT_RGB(r * value / 255, g * value / 255, b * value / 255);
}

Bgc_Palette_t bgc_Palette(const Bgc_Settings_t& settings, bool night, uint32_t now){
    Bgc_Palette_t p;
    switch (settings.theme){
        case BGC_THEME_AMBER:
            p = {MTB_KIT_RGB(255, 170, 0), MTB_KIT_RGB(255, 90, 0), MTB_KIT_RGB(200, 120, 20), MTB_KIT_RGB(40, 22, 0), 0, 0};
            break;
        case BGC_THEME_ICE:
            p = {MTB_KIT_RGB(170, 230, 255), MTB_KIT_RGB(60, 160, 255), MTB_KIT_RGB(110, 170, 210), MTB_KIT_RGB(12, 28, 44), 0, 0};
            break;
        case BGC_THEME_MATRIX:
            p = {MTB_KIT_RGB(60, 255, 90), MTB_KIT_RGB(180, 255, 120), MTB_KIT_RGB(30, 170, 60), MTB_KIT_RGB(0, 34, 8), 0, 0};
            break;
        case BGC_THEME_SUNSET:
            p = {MTB_KIT_RGB(255, 120, 60), MTB_KIT_RGB(255, 210, 60), MTB_KIT_RGB(230, 120, 150), MTB_KIT_RGB(40, 14, 24),
                 MTB_KIT_RGB(255, 200, 40), MTB_KIT_RGB(220, 40, 120)};
            break;
        case BGC_THEME_RAINBOW: {
            const uint16_t hue = (uint16_t)((now % 3600) / 10);         // Once round the colour wheel an hour.
            p = {bgc_Hue(hue, 255), bgc_Hue(hue + 180, 255), bgc_Hue(hue + 30, 190), bgc_Hue(hue, 36), 0, 0};
            break;
        }
        case BGC_THEME_CUSTOM:
            p = {settings.customColor, mtb_Kit_Blend(settings.customColor, KIT_TEXT, 0.45f), mtb_Kit_Dim(settings.customColor, 170),
                 mtb_Kit_Dim(settings.customColor, 30), 0, 0};
            break;
        default:
            p = {MTB_KIT_RGB(245, 245, 245), MTB_KIT_RGB(0, 200, 255), MTB_KIT_RGB(150, 150, 150), MTB_KIT_RGB(26, 26, 30), 0, 0};
            break;
    }
    if (p.top == 0 && p.bottom == 0){ p.top = p.main; p.bottom = p.main; }
    if (night){
        if (settings.nightStyle == BGC_NIGHT_RED){
            // Deep red: easy on eyes adjusted to the dark.
            p = {MTB_KIT_RGB(150, 0, 0), MTB_KIT_RGB(110, 0, 0), MTB_KIT_RGB(80, 0, 0), MTB_KIT_RGB(18, 0, 0), 0, 0};
            p.top = p.bottom = p.main;
        } else {
            p.main = mtb_Kit_Dim(p.main, 70); p.accent = mtb_Kit_Dim(p.accent, 70); p.soft = mtb_Kit_Dim(p.soft, 70);
            p.ghost = mtb_Kit_Dim(p.ghost, 120); p.top = mtb_Kit_Dim(p.top, 70); p.bottom = mtb_Kit_Dim(p.bottom, 70);
        }
    }
    return p;
}

//##############################################################################################################
static const char* const NUMBERS[] = {"TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT",
                                      "NINE", "TEN", "ELEVEN", "TWELVE"};

int bgc_Words(int hour, int minute, char* lines, size_t lineSize){
    // The five minutes begun: 7:23 reads "twenty past seven"; the face's dots add the rest.
    const int rounded = minute / 5 * 5;
    const char* hourWord = NUMBERS[((rounded > 30 ? hour + 1 : hour) % 12 + 12) % 12];
    const char* part = nullptr;
    const char* relation = rounded > 30 ? "TO" : "PAST";
    switch (rounded > 30 ? 60 - rounded : rounded){
        case 5: part = "FIVE"; break;
        case 10: part = "TEN"; break;
        case 15: part = "QUARTER"; break;
        case 20: part = "TWENTY"; break;
        case 25: part = "TWENTY FIVE"; break;
        case 30: part = "HALF"; break;
        default: break;
    }
    int n = 0;
    auto add = [&](const char* text){ snprintf(lines + n * lineSize, lineSize, "%s", text); n++; };
    add("IT IS");
    if (part == nullptr){ add(hourWord); add("O'CLOCK"); }
    else { add(part); add(relation); add(hourWord); }
    return n;
}

void bgc_Hour_Minute(char* dest, size_t size, char* meridiem, size_t meridiemSize, int hour, int minute, bool hour24, bool leadingZero){
    if (hour24){
        snprintf(dest, size, "%02d:%02d", hour, minute);
        if (meridiemSize) meridiem[0] = '\0';
        return;
    }
    const int h12 = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(dest, size, leadingZero ? "%02d:%02d" : "%d:%02d", h12, minute);
    snprintf(meridiem, meridiemSize, "%s", hour < 12 ? "AM" : "PM");
}
