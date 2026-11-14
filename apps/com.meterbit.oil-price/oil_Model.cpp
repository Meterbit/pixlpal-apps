#include "oil_Model.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

//##############################################################################################################
// Howard Hinnant's days_from_civil and civil_from_days.
int32_t oil_Days_From_Civil(int year, int month, int day){
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

void oil_Civil_From_Days(int32_t days, int& year, int& month, int& day){
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = (int)(doy - (153 * mp + 2) / 5 + 1);
    month = (int)(mp < 10 ? mp + 3 : mp - 9);
    year = (int)yoe + era * 400 + (month <= 2);
}

static bool oil_Digits(const char* text, int count, int& value){
    value = 0;
    for (int i = 0; i < count; i++){
        if (!isdigit((unsigned char)text[i])) return false;
        value = value * 10 + (text[i] - '0');
    }
    return true;
}

int32_t oil_Parse_Iso_Date(const char* text){
    int year, month, day = 1;
    if (text == nullptr || !oil_Digits(text, 4, year) || text[4] != '-' || !oil_Digits(text + 5, 2, month)) return 0;
    if (text[7] == '-' && !oil_Digits(text + 8, 2, day)) return 0;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
    return oil_Days_From_Civil(year, month, day);
}

int32_t oil_Parse_Us_Date(const char* text){
    if (text == nullptr) return 0;
    while (*text == ' ') text++;
    int month = -1;
    for (int i = 0; i < 12; i++){
        if (strncmp(text, MONTHS[i], 3) == 0){ month = i + 1; break; }
    }
    if (month < 0) return 0;
    while (*text && *text != ' ') text++;      // "Nov" or "November"
    char* end;
    const long day = strtol(text, &end, 10);
    if (end == text || *end != ',') return 0;
    const long year = strtol(end + 1, &end, 10);
    if (day < 1 || day > 31 || year < 1970 || year > 2200) return 0;
    return oil_Days_From_Civil((int)year, month, (int)day);
}

void oil_Format_Iso_Date(char* dest, size_t size, int32_t day, bool monthOnly){
    int y, m, d;
    oil_Civil_From_Days(day, y, m, d);
    if (monthOnly) snprintf(dest, size, "%04d-%02d", y, m);
    else snprintf(dest, size, "%04d-%02d-%02d", y, m, d);
}

void oil_Format_Short_Date(char* dest, size_t size, int32_t day, bool monthly){
    int y, m, d;
    oil_Civil_From_Days(day, y, m, d);
    if (monthly) snprintf(dest, size, "%s %02d", MONTHS[m - 1], y % 100);
    else snprintf(dest, size, "%02d %s", d, MONTHS[m - 1]);
}

//##############################################################################################################
void oil_Series_Put(Oil_Series_t& series, int32_t day, float value){
    if (isnan(value) || isinf(value)) return;
    int i = series.count;
    while (i > 0 && series.points[i - 1].day > day) i--;
    if (i > 0 && series.points[i - 1].day == day){
        series.points[i - 1].value = value;     // EIA revises recent values: the newest answer wins.
        return;
    }
    if (series.count == OIL_HISTORY_POINTS){
        if (i == 0) return;                     // Older than everything kept.
        memmove(series.points, series.points + 1, (size_t)(i - 1) * sizeof(Oil_Point_t));
        i--;
    } else {
        memmove(series.points + i + 1, series.points + i, (size_t)(series.count - i) * sizeof(Oil_Point_t));
        series.count++;
    }
    series.points[i].day = day;
    series.points[i].value = value;
}

const Oil_Point_t* oil_Series_Last(const Oil_Series_t& series){
    return series.count > 0 ? &series.points[series.count - 1] : nullptr;
}

const Oil_Point_t* oil_Series_At_Or_Before(const Oil_Series_t& series, int32_t day){
    for (int i = series.count - 1; i >= 0; i--){
        if (series.points[i].day <= day) return &series.points[i];
    }
    return nullptr;
}

bool oil_Series_Change(const Oil_Series_t& series, int32_t daysBack, float& change, float& percent){
    const Oil_Point_t* last = oil_Series_Last(series);
    if (last == nullptr) return false;
    const Oil_Point_t* before = nullptr;
    if (daysBack == 0) before = (series.count >= 2) ? &series.points[series.count - 2] : nullptr;
    else {
        before = oil_Series_At_Or_Before(series, last->day - daysBack);
        // A value much older than asked for would understate how far back the comparison goes.
        if (before != nullptr && last->day - before->day > daysBack + daysBack / 4 + 4) before = nullptr;
    }
    if (before == nullptr || before == last) return false;
    change = last->value - before->value;
    percent = (before->value != 0) ? change / fabsf(before->value) * 100.0f : NAN;
    return true;
}

bool oil_Series_Range(const Oil_Series_t& series, int32_t days, float& low, float& high){
    const Oil_Point_t* last = oil_Series_Last(series);
    if (last == nullptr) return false;
    bool found = false;
    for (int i = 0; i < series.count; i++){
        if (series.points[i].day < last->day - days) continue;
        const float v = series.points[i].value;
        if (!found || v < low) low = v;
        if (!found || v > high) high = v;
        found = true;
    }
    return found;
}

int oil_Find_Series(const Oil_Data_t& data, const char* code){
    if (code == nullptr) return -1;
    for (int i = 0; i < data.seriesCount; i++){
        if (strcmp(data.series[i].code, code) == 0) return i;
    }
    return -1;
}

//##############################################################################################################
// Case-insensitive search for a whole word or word start, so "oil" does not match "soil".
static bool oil_Has_Word(const char* text, const char* word){
    const size_t length = strlen(word);
    for (const char* p = text; *p; p++){
        if (p != text && isalnum((unsigned char)p[-1])) continue;
        size_t i = 0;
        while (i < length && p[i] && tolower((unsigned char)p[i]) == word[i]) i++;
        if (i == length) return true;
    }
    return false;
}

uint8_t oil_Headline_Score(const char* title){
    static const char* const regional[] = {"opec", "nigeria", "africa", "saudi", "libya", "angola"};
    static const char* const oilGas[] = {"oil", "crude", "brent", "wti", "petroleum", "gasoline", "diesel", "distillate",
                                         "jet fuel", "refin", "lng", "natural gas", "propane", "heating oil", "fuel",
                                         "barrel", "drilling", "rig", "pipeline", "shut-in", "offshore", "upstream"};
    static const char* const energy[] = {"energy", "electric", "power", "coal", "solar", "wind", "nuclear", "battery"};
    for (const char* w : regional) if (oil_Has_Word(title, w)) return 3;
    for (const char* w : oilGas) if (oil_Has_Word(title, w)) return 2;
    for (const char* w : energy) if (oil_Has_Word(title, w)) return 1;
    return 0;
}

//##############################################################################################################
// Unicode code point to the ASCII the fonts can show; 0 drops it.
static char oil_Ascii_For(uint32_t cp){
    if (cp >= 32 && cp < 127) return (char)cp;
    switch (cp){
        case 0x2018: case 0x2019: case 0x201B: case 0x2032: return '\'';
        case 0x201C: case 0x201D: case 0x2033: return '"';
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
        case 0x00A0: case 0x2009: case 0x200A: case 0x202F: case '\t': case '\n': case '\r': return ' ';
        case 0x2022: case 0x00B7: return '-';
        default: return 0;
    }
}

static uint32_t oil_Named_Entity(const char* name, size_t length){
    static const struct { const char* name; uint32_t cp; } table[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"rdquo", 0x201D}, {"ldquo", 0x201C}, {"mdash", 0x2014},
        {"ndash", 0x2013}, {"hellip", 0x2026}, {"rsaquo", '>'}, {"lsaquo", '<'}, {"middot", 0xB7}, {"bull", 0x2022},
    };
    for (const auto& entry : table){
        if (strlen(entry.name) == length && strncmp(entry.name, name, length) == 0) return entry.cp;
    }
    return 0;
}

void oil_Clean_Text(char* dest, size_t size, const char* src){
    size_t n = 0;
    bool space = true;      // Drops leading spaces and collapses runs.
    if (size == 0) return;
    if (src == nullptr) src = "";
    auto put = [&](char c){
        if (c == ' '){
            if (space) return;
            space = true;
        } else space = false;
        if (n + 1 < size) dest[n++] = c;
    };
    while (*src){
        const uint8_t c = (uint8_t)*src;
        uint32_t cp = 0;
        if (c == '<'){                              // Markup inside a title, such as <em>.
            const char* close = strchr(src, '>');
            src = close ? close + 1 : src + strlen(src);
            continue;
        }
        if (c == '&'){
            const char* semi = strchr(src, ';');
            if (semi != nullptr && semi - src <= 10){
                if (src[1] == '#'){
                    cp = (src[2] == 'x' || src[2] == 'X') ? strtoul(src + 3, nullptr, 16) : strtoul(src + 2, nullptr, 10);
                } else cp = oil_Named_Entity(src + 1, (size_t)(semi - src - 1));
                if (cp != 0){
                    src = semi + 1;
                    if (cp == 0x2026){ put('.'); put('.'); put('.'); }
                    else if (char a = oil_Ascii_For(cp)) put(a);
                    continue;
                }
            }
            put('&');
            src++;
            continue;
        }
        if (c < 0x80){
            if (char a = oil_Ascii_For(c)) put(a);
            src++;
            continue;
        }
        // UTF-8 sequence. A byte that does not start one (Latin-1 text) is dropped.
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        cp = (extra == 3) ? (c & 0x07) : (extra == 2) ? (c & 0x0F) : (c & 0x1F);
        int i = 1;
        for (; i <= extra && ((uint8_t)src[i] & 0xC0) == 0x80; i++) cp = (cp << 6) | ((uint8_t)src[i] & 0x3F);
        src += (extra > 0 && i == extra + 1) ? i : 1;
        if (extra == 0) continue;
        if (cp == 0x2026){ put('.'); put('.'); put('.'); }
        else if (char a = oil_Ascii_For(cp)) put(a);
    }
    while (n > 0 && dest[n - 1] == ' ') n--;
    dest[n] = '\0';
}
