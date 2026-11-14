// The FAITH apps' shared model. No network, no panel.
#include "fth_Kit.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void fth_Copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

void fth_Clean_Text(char* out, size_t size, const char* in){
    size_t k = 0;
    bool space = false, cut = false;
    for (const char* p = in ? in : ""; *p; p++){
        const bool ws = *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t';
        if (ws){ space = k > 0; continue; }
        if (space){ if (k + 1 >= size){ cut = true; break; } out[k++] = ' '; space = false; }
        if (k + 1 >= size){ cut = true; break; }
        out[k++] = *p;
    }
    out[k] = '\0';
    if (cut && size > 8){
        size_t at = k > size - 5 ? size - 5 : k;
        while (at > 0 && ((unsigned char)out[at] & 0xC0) == 0x80) at--;
        memcpy(out + at, "...", 4);
    }
}

// Bible verses -----------------------------------------------------------------------------------------------------------
size_t fth_Bible_Url(const char* reference, const char* translation, char* out, size_t size){
    char ref[96];
    size_t k = 0;
    for (const char* p = reference; *p && k + 4 < sizeof(ref); p++){
        const char c = *p;
        if (isalnum((unsigned char)c) || c == ':' || c == '-' || c == ',' || c == '.') ref[k++] = c;
        else k += (size_t)snprintf(ref + k, sizeof(ref) - k, "%%%02X", (unsigned char)c);
    }
    ref[k] = '\0';
    for (const char* p = translation; *p; p++) if (!isalnum((unsigned char)*p) && *p != '-') return 0;
    const int n = snprintf(out, size, "https://bible-api.com/%s?translation=%s", ref, translation);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

bool fth_Parse_Bible(JsonDocument& doc, const char* translation, Fth_Verse_t& v){
    const char* text = doc["text"] | "";
    if (!text[0]) return false;
    fth_Clean_Text(v.text, sizeof(v.text), text);
    v.extra[0] = '\0';
    char t[12];
    fth_Copy(t, sizeof(t), doc["translation_id"] | translation);
    for (char* p = t; *p; p++) *p = (char)toupper((unsigned char)*p);
    char ref[40];
    mtb_Kit_To_Ascii(ref, sizeof(ref), doc["reference"] | "");
    snprintf(v.reference, sizeof(v.reference), "%.32s (%s)", ref, t);
    return true;
}

uint32_t fth_Pick(uint32_t day, uint8_t pick, uint32_t count, uint32_t stride){
    return (uint32_t)(((uint64_t)day * stride + (uint64_t)pick * 7u) % count);
}

static const char* const BIBLE_REFS[] = {
    "Genesis 1:1", "Genesis 1:27", "Genesis 28:15", "Exodus 14:14", "Exodus 15:2", "Numbers 6:24-26", "Deuteronomy 6:5", "Deuteronomy 31:6",
    "Deuteronomy 31:8", "Joshua 1:9", "1 Samuel 16:7", "1 Chronicles 16:34", "2 Chronicles 7:14", "Nehemiah 8:10", "Job 19:25",
    "Psalms 1:1-2", "Psalms 4:8", "Psalms 16:11", "Psalms 18:2", "Psalms 19:14", "Psalms 23:1-3", "Psalms 23:4", "Psalms 27:1",
    "Psalms 27:14", "Psalms 28:7", "Psalms 30:5", "Psalms 31:24", "Psalms 32:8", "Psalms 34:8", "Psalms 34:18", "Psalms 37:4",
    "Psalms 37:5", "Psalms 40:1", "Psalms 46:1", "Psalms 46:10", "Psalms 51:10", "Psalms 55:22", "Psalms 56:3", "Psalms 62:1",
    "Psalms 73:26", "Psalms 84:11", "Psalms 90:12", "Psalms 91:1-2", "Psalms 91:11", "Psalms 100:4-5", "Psalms 103:2-3",
    "Psalms 118:24", "Psalms 119:105", "Psalms 121:1-2", "Psalms 127:1", "Psalms 139:14", "Psalms 143:8", "Psalms 145:18",
    "Psalms 147:3", "Proverbs 3:5-6", "Proverbs 4:23", "Proverbs 15:1", "Proverbs 16:3", "Proverbs 16:9", "Proverbs 17:17",
    "Proverbs 18:10", "Proverbs 22:6", "Proverbs 27:17", "Proverbs 31:25", "Ecclesiastes 3:1", "Isaiah 9:6", "Isaiah 26:3",
    "Isaiah 40:8", "Isaiah 40:29", "Isaiah 40:31", "Isaiah 41:10", "Isaiah 43:2", "Isaiah 43:19", "Isaiah 53:5", "Isaiah 54:10",
    "Isaiah 55:8-9", "Isaiah 58:11", "Jeremiah 17:7", "Jeremiah 29:11", "Jeremiah 29:13", "Jeremiah 31:3", "Lamentations 3:22-23",
    "Micah 6:8", "Nahum 1:7", "Zephaniah 3:17", "Habakkuk 3:19", "Matthew 5:9", "Matthew 5:14", "Matthew 5:16", "Matthew 6:33",
    "Matthew 6:34", "Matthew 7:7", "Matthew 11:28", "Matthew 11:29-30", "Matthew 19:26", "Matthew 22:37-39", "Matthew 28:20",
    "Mark 9:23", "Mark 10:27", "Mark 11:24", "Luke 1:37", "Luke 6:31", "Luke 6:38", "Luke 12:34", "John 1:5", "John 3:16",
    "John 8:12", "John 10:10", "John 11:25", "John 13:34", "John 14:1", "John 14:6", "John 14:27", "John 15:5", "John 15:13",
    "John 16:33", "Acts 1:8", "Acts 20:35", "Romans 5:8", "Romans 8:1", "Romans 8:28", "Romans 8:31", "Romans 8:38-39",
    "Romans 10:9", "Romans 12:2", "Romans 12:12", "Romans 15:13", "1 Corinthians 10:13", "1 Corinthians 13:4-5",
    "1 Corinthians 13:13", "1 Corinthians 16:14", "2 Corinthians 4:16", "2 Corinthians 5:7", "2 Corinthians 5:17",
    "2 Corinthians 12:9", "Galatians 2:20", "Galatians 5:22-23", "Galatians 6:9", "Ephesians 2:8", "Ephesians 3:20",
    "Ephesians 4:32", "Ephesians 6:10", "Philippians 1:6", "Philippians 4:6-7", "Philippians 4:8", "Philippians 4:13",
    "Philippians 4:19", "Colossians 3:2", "Colossians 3:23", "1 Thessalonians 5:16-18", "2 Timothy 1:7", "2 Timothy 3:16",
    "Hebrews 4:16", "Hebrews 11:1", "Hebrews 12:1", "Hebrews 13:5", "Hebrews 13:8", "James 1:5", "James 1:17", "James 4:8",
    "1 Peter 5:7", "2 Peter 3:9", "1 John 1:9", "1 John 4:8", "1 John 4:18", "1 John 4:19", "Revelation 3:20", "Revelation 21:4",
};

// For the Douay-Rheims and the Vulgate: the Psalms by their Vulgate numbers (22 is "The Lord ruleth me"), and the
// deuterocanonical books. Each reference checked against bible-api.com's Douay-Rheims.
static const char* const CATHOLIC_REFS[] = {
    "Matthew 5:3", "Matthew 5:8", "Matthew 5:9", "Matthew 5:16", "Matthew 6:33", "Matthew 7:7", "Matthew 11:28", "Matthew 16:18",
    "Matthew 18:20", "Matthew 22:37-39", "Matthew 25:40", "Matthew 28:20", "Mark 10:27", "Luke 1:28", "Luke 1:37", "Luke 1:38",
    "Luke 1:46-47", "Luke 6:36", "Luke 11:9", "Luke 15:7", "Luke 23:42-43", "John 1:14", "John 3:16", "John 6:35", "John 6:51",
    "John 8:12", "John 10:11", "John 11:25", "John 13:34", "John 14:6", "John 14:27", "John 15:5", "John 15:13", "John 19:26-27",
    "John 20:29", "John 21:17", "Acts 2:42", "Acts 20:35", "Romans 5:8", "Romans 8:28", "Romans 8:31", "Romans 12:12", "Romans 15:13",
    "1 Corinthians 10:16", "1 Corinthians 11:26", "1 Corinthians 13:4-5", "1 Corinthians 13:13", "1 Corinthians 16:14",
    "2 Corinthians 5:17", "2 Corinthians 12:9", "Galatians 5:22-23", "Galatians 6:9", "Ephesians 2:8", "Ephesians 4:32",
    "Ephesians 6:10", "Philippians 4:4", "Philippians 4:6-7", "Philippians 4:13", "Colossians 3:14", "Colossians 3:23",
    "1 Thessalonians 5:16-18", "Hebrews 11:1", "Hebrews 13:8", "James 1:17", "James 2:17", "James 5:16", "1 Peter 5:7",
    "1 John 4:8", "1 John 4:16", "1 John 4:19", "Revelation 12:1", "Revelation 21:4", "Genesis 1:27", "Genesis 3:15", "Exodus 3:14",
    "Deuteronomy 6:5", "Joshua 1:9", "1 Samuel 3:10", "Isaiah 7:14", "Isaiah 9:6", "Isaiah 40:31", "Isaiah 41:10", "Isaiah 43:1",
    "Isaiah 49:15", "Isaiah 53:5", "Isaiah 55:8", "Jeremiah 1:5", "Jeremiah 29:11", "Lamentations 3:22-23", "Micah 6:8",
    "Zephaniah 3:17", "Tobit 12:8", "Tobit 4:16", "Wisdom 3:1", "Wisdom 11:25", "Sirach 2:1", "Sirach 2:6", "Sirach 6:14",
    "Baruch 3:14", "2 Maccabees 12:46", "Judith 8:17", "Psalms 22:1-4", "Psalms 26:1", "Psalms 33:9", "Psalms 45:2", "Psalms 50:12",
    "Psalms 90:11", "Psalms 102:2-3", "Psalms 117:24", "Psalms 118:105", "Psalms 120:1-2", "Psalms 138:14", "Psalms 144:18",
};

const char* fth_Bible_Reference(uint32_t day, uint8_t pick){
    const uint32_t n = sizeof(BIBLE_REFS) / sizeof(BIBLE_REFS[0]);
    return BIBLE_REFS[fth_Pick(day, pick, n, 41)];
}

const char* fth_Catholic_Reference(uint32_t day, uint8_t pick){
    const uint32_t n = sizeof(CATHOLIC_REFS) / sizeof(CATHOLIC_REFS[0]);
    return CATHOLIC_REFS[fth_Pick(day, pick, n, 43)];
}

// Reminders ----------------------------------------------------------------------------------------------------------------
bool fth_Reminder_Today(const Fth_Reminder_t& r, int weekday){
    if (!r.on) return false;
    switch (r.days){
        case 1: return weekday <= 4;
        case 2: return weekday >= 5;
        case 3: return weekday == 6;
        case 4: return weekday == 5;
        case 5: return weekday == 4;
        default: return true;
    }
}

void fth_Reminder_Clamp(Fth_Reminder_t& r){
    r.on = r.on ? 1 : 0;
    if (r.days >= FTH_DAYS_COUNT) r.days = 0;
    if (r.minute >= 1440) r.minute = 0;
    r.label[sizeof(r.label) - 1] = '\0';
}

const char* fth_Days_Name(uint8_t days){
    static const char* const N[FTH_DAYS_COUNT] = {"every day", "weekdays", "weekends", "Sundays", "Saturdays", "Fridays"};
    return days < FTH_DAYS_COUNT ? N[days] : N[0];
}

int fth_Next_Reminder(const Fth_Reminder_t* r, int count, uint32_t now, int32_t& secondsTo){
    int y, m, d, weekday;
    fth_Local_Day(now, &y, &m, &d, &weekday);
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    const int32_t secOfDay = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    int best = -1;
    int32_t bestIn = 0;
    for (int i = 0; i < count; i++){
        for (int dd = 0; dd < 3; dd++){
            if (!fth_Reminder_Today(r[i], (weekday + dd) % 7)) continue;
            const int32_t in = (int32_t)r[i].minute * 60 + dd * 86400 - secOfDay;
            if (in <= 0) continue;
            if (best < 0 || in < bestIn){ best = i; bestIn = in; }
            break;
        }
    }
    secondsTo = best < 0 ? -1 : bestIn;
    return best;
}

// Time -----------------------------------------------------------------------------------------------------------------------
void fth_Countdown(char* out, size_t size, int32_t s){
    if (s < 0) s = 0;
    const int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (h) snprintf(out, size, "%d:%02d:%02d", h, m, sec);
    else snprintf(out, size, "%d:%02d", m, sec);
}

void fth_Clock(char* out, size_t size, uint32_t t, bool hour24){
    const time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    fth_Minute_Clock(out, size, (uint16_t)(lt.tm_hour * 60 + lt.tm_min), hour24);
}

void fth_Minute_Clock(char* out, size_t size, uint16_t minute, bool hour24){
    const int h = minute / 60 % 24, m = minute % 60;
    if (hour24) snprintf(out, size, "%02d:%02d", h, m);
    else snprintf(out, size, "%d:%02d%s", h % 12 ? h % 12 : 12, m, h < 12 ? "am" : "pm");
}

uint32_t fth_Local_Day(uint32_t now, int* y, int* m, int* d, int* weekday){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (y) *y = lt.tm_year + 1900;
    if (m) *m = lt.tm_mon + 1;
    if (d) *d = lt.tm_mday;
    if (weekday) *weekday = (lt.tm_wday + 6) % 7;
    return (uint32_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

void fth_Civil_From_Days(int32_t z, int& y, int& m, int& d){
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t yy = (int32_t)yoe + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yy + (m <= 2));
}

// The church year ------------------------------------------------------------------------------------------------------------
void fth_Easter(int y, bool orthodox, int& month, int& day){
    if (orthodox){
        const int a = y % 4, b = y % 7, c = y % 19;
        const int d = (19 * c + 15) % 30, e = (2 * a + 4 * b - d + 34) % 7;
        const int jm = (d + e + 114) / 31, jd = (d + e + 114) % 31 + 1;
        // Julian to Gregorian: 13 days in 1900-2099.
        int yy, mm, dd;
        fth_Civil_From_Days(mtb_Kit_Days_From_Civil(y, jm, jd) + 13, yy, mm, dd);
        month = mm;
        day = dd;
        return;
    }
    const int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int mm = (a + 11 * h + 22 * l) / 451;
    month = (h + l - 7 * mm + 114) / 31;
    day = (h + l - 7 * mm + 114) % 31 + 1;
}

uint16_t fth_Liturgical_Color(const char* name){
    if (!name) return KIT_TEXT;
    if (!strcmp(name, "violet") || !strcmp(name, "purple")) return MTB_KIT_RGB(150, 70, 220);
    if (!strcmp(name, "rose")) return MTB_KIT_RGB(240, 120, 170);
    if (!strcmp(name, "red")) return MTB_KIT_RGB(230, 40, 40);
    if (!strcmp(name, "green")) return MTB_KIT_RGB(40, 190, 80);
    if (!strcmp(name, "black")) return MTB_KIT_RGB(110, 110, 110);
    if (!strcmp(name, "gold")) return MTB_KIT_RGB(255, 196, 0);
    return MTB_KIT_RGB(245, 245, 235);                       // white
}

struct Feast_t { int32_t day; const char* name; const char* color; };

static int32_t days_Of(int y, int m, int d){ return mtb_Kit_Days_From_Civil(y, m, d); }

// The year's great days, Western or Orthodox.
static int feasts_Of(int y, bool orthodox, Feast_t* out){
    int em, ed;
    fth_Easter(y, orthodox, em, ed);
    const int32_t E = days_Of(y, em, ed);
    int n = 0;
    auto add = [&](int32_t day, const char* name, const char* color){ out[n++] = {day, name, color}; };
    if (orthodox){
        add(days_Of(y, 1, 7), "Nativity of Christ", "white");
        add(days_Of(y, 1, 19), "Theophany", "white");
        add(E - 48, "Clean Monday", "violet");
        add(E - 7, "Palm Sunday", "green");
        add(E - 3, "Holy Thursday", "red");
        add(E - 2, "Great and Holy Friday", "black");
        add(E, "Pascha", "white");
        add(E + 39, "Ascension", "white");
        add(E + 49, "Pentecost", "green");
        add(days_Of(y, 8, 19), "Transfiguration", "white");
        add(days_Of(y, 8, 28), "Dormition", "white");
        return n;
    }
    const int32_t christmas = days_Of(y, 12, 25);
    const int cw = (int)(((christmas % 7) + 7 + 3) % 7);      // Monday 0 (1970-01-01 was a Thursday)
    const int32_t advent1 = christmas - (cw + 1) - 21;
    add(days_Of(y, 1, 1), "New Year's Day", "white");
    add(days_Of(y, 1, 6), "Epiphany", "white");
    add(E - 46, "Ash Wednesday", "violet");
    add(E - 7, "Palm Sunday", "red");
    add(E - 3, "Maundy Thursday", "white");
    add(E - 2, "Good Friday", "red");
    add(E - 1, "Holy Saturday", "violet");
    add(E, "Easter Sunday", "white");
    add(E + 1, "Easter Monday", "white");
    add(E + 39, "Ascension Day", "white");
    add(E + 49, "Pentecost", "red");
    add(E + 56, "Trinity Sunday", "white");
    add(advent1, "First Sunday of Advent", "violet");
    add(christmas - 1, "Christmas Eve", "white");
    add(christmas, "Christmas Day", "white");
    return n;
}

void fth_Church_Day(int y, int m, int d, bool orthodox, Fth_Church_Day_t& out){
    memset(&out, 0, sizeof(out));
    const int32_t today = days_Of(y, m, d);
    int em, ed;
    fth_Easter(y, orthodox, em, ed);
    const int32_t E = days_Of(y, em, ed);
    const char* name = "Ordinary Time";
    const char* color = "green";
    uint8_t season = FTH_ORDINARY;
    if (orthodox){
        if (today >= days_Of(y, 11, 28) || today <= days_Of(y, 1, 6)){ season = FTH_ADVENT; name = "Nativity Fast"; color = "red"; }
        else if (today <= days_Of(y, 1, 18)){ season = FTH_CHRISTMAS; name = "Christmas"; color = "white"; }
        else if (today >= E - 48 && today < E - 7){ season = FTH_LENT; name = "Great Lent"; color = "violet"; }
        else if (today >= E - 7 && today < E){ season = FTH_HOLY_WEEK; name = "Holy Week"; color = "black"; }
        else if (today >= E && today <= E + 49){ season = FTH_EASTER; name = "Paschal season"; color = "white"; }
        else { name = "Ordinary days"; color = "green"; }
    } else {
        const int32_t christmas = days_Of(y, 12, 25);
        const int cw = (int)(((christmas % 7) + 7 + 3) % 7);
        const int32_t advent1 = christmas - (cw + 1) - 21;
        if (today >= advent1 && today < christmas){ season = FTH_ADVENT; name = "Advent"; color = "violet"; }
        else if (today >= christmas || today <= days_Of(y, 1, 5)){ season = FTH_CHRISTMAS; name = "Christmas"; color = "white"; }
        else if (today >= E - 46 && today < E - 7){ season = FTH_LENT; name = "Lent"; color = "violet"; }
        else if (today >= E - 7 && today < E){ season = FTH_HOLY_WEEK; name = "Holy Week"; color = "red"; }
        else if (today >= E && today <= E + 49){ season = FTH_EASTER; name = "Easter"; color = "white"; }
    }
    out.season = season;
    fth_Copy(out.seasonName, sizeof(out.seasonName), name);
    Feast_t f[2][16];
    const int n0 = feasts_Of(y, orthodox, f[0]), n1 = feasts_Of(y + 1, orthodox, f[1]);
    // Today's great day sets the colour; the next one gets a countdown.
    int32_t nextDay = 0;
    for (int k = 0; k < 2; k++){
        const int n = k ? n1 : n0;
        for (int i = 0; i < n; i++){
            const Feast_t& x = f[k][i];
            if (x.day == today){ fth_Copy(out.feast, sizeof(out.feast), x.name); color = x.color; }
            if (x.day > today && (nextDay == 0 || x.day < nextDay)){ nextDay = x.day; fth_Copy(out.next, sizeof(out.next), x.name); }
        }
    }
    out.nextDays = nextDay ? (int)(nextDay - today) : -1;
    fth_Copy(out.colorName, sizeof(out.colorName), color);
    out.color = fth_Liturgical_Color(color);
}
