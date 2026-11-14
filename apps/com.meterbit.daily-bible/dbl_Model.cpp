// Daily Bible: the model.
#include "dbl_Model.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>

void dbl_Settings_Default(Dbl_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = DBL_SETTINGS_MAGIC;
    s.verse = 1;
    s.plan = DBL_PLAN_OFF;
    s.hour24 = 1;
    s.chime = 1;
    static const struct { uint16_t m; uint8_t days; const char* l; } R[DBL_REMINDERS] = {
        {6 * 60, 0, "Morning devotion"}, {21 * 60, 0, "Evening prayer"}, {8 * 60, 3, "Sunday service"}, {19 * 60, 1, "Bible study"}};
    for (int i = 0; i < DBL_REMINDERS; i++){
        s.reminders[i].minute = R[i].m;
        s.reminders[i].days = R[i].days;
        fth_Copy(s.reminders[i].label, sizeof(s.reminders[i].label), R[i].l);
    }
}

void dbl_Settings_Clamp(Dbl_Settings_t& s){
    s.magic = DBL_SETTINGS_MAGIC;
    if (s.translation >= dbl_Translations()) s.translation = 0;
    if (s.plan >= DBL_PLANS) s.plan = DBL_PLAN_OFF;
    if (s.reminderSlot >= DBL_REMINDERS) s.reminderSlot = 0;
    s.verse = s.verse ? 1 : 0;
    s.orthodox = s.orthodox ? 1 : 0;
    s.hour24 = s.hour24 ? 1 : 0;
    s.chime = s.chime ? 1 : 0;
    for (Fth_Reminder_t& r : s.reminders) fth_Reminder_Clamp(r);
}

// Public domain translations with the 66 books (bible-api.com).
static const Fth_Edition_t TRANSLATIONS[] = {
    {"web", "English, World English Bible"}, {"kjv", "English, King James Version"}, {"bsb", "English, Berean Standard Bible"},
    {"asv", "English, American Standard Version"}, {"bbe", "English, Bible in Basic English"}, {"webbe", "English, World English Bible (British)"},
    {"oeb-us", "English, Open English Bible"}, {"darby", "English, Darby"}, {"swahili", "Swahili Bible"}, {"rv1909", "Spanish, Reina Valera 1909"},
    {"bes", "Spanish, La Biblia en Espanol Sencillo"}, {"luther1912", "German, Luther 1912"}, {"riveduta", "Italian, Riveduta"},
    {"tagalog", "Tagalog, Ang Dating Biblia"}, {"synodal", "Russian, Synodal"}, {"rccv", "Romanian, Cornilescu"},
};
int dbl_Translations(void){ return (int)(sizeof(TRANSLATIONS) / sizeof(TRANSLATIONS[0])); }
const Fth_Edition_t* dbl_Translation(int i){ return i >= 0 && i < dbl_Translations() ? &TRANSLATIONS[i] : &TRANSLATIONS[0]; }

const char* dbl_Plan_Name(uint8_t plan){
    static const char* const N[] = {"No plan", "The whole Bible in a year", "The New Testament, a chapter a day", "Proverbs and Psalms each month"};
    return plan < DBL_PLANS ? N[plan] : N[0];
}

// The 66 books and their chapters (1,189).
static const struct { const char* name; uint8_t chapters; } BOOKS[] = {
    {"Genesis", 50}, {"Exodus", 40}, {"Leviticus", 27}, {"Numbers", 36}, {"Deuteronomy", 34}, {"Joshua", 24}, {"Judges", 21}, {"Ruth", 4},
    {"1 Samuel", 31}, {"2 Samuel", 24}, {"1 Kings", 22}, {"2 Kings", 25}, {"1 Chronicles", 29}, {"2 Chronicles", 36}, {"Ezra", 10},
    {"Nehemiah", 13}, {"Esther", 10}, {"Job", 42}, {"Psalms", 150}, {"Proverbs", 31}, {"Ecclesiastes", 12}, {"Song of Songs", 8},
    {"Isaiah", 66}, {"Jeremiah", 52}, {"Lamentations", 5}, {"Ezekiel", 48}, {"Daniel", 12}, {"Hosea", 14}, {"Joel", 3}, {"Amos", 9},
    {"Obadiah", 1}, {"Jonah", 4}, {"Micah", 7}, {"Nahum", 3}, {"Habakkuk", 3}, {"Zephaniah", 3}, {"Haggai", 2}, {"Zechariah", 14},
    {"Malachi", 4}, {"Matthew", 28}, {"Mark", 16}, {"Luke", 24}, {"John", 21}, {"Acts", 28}, {"Romans", 16}, {"1 Corinthians", 16},
    {"2 Corinthians", 13}, {"Galatians", 6}, {"Ephesians", 6}, {"Philippians", 4}, {"Colossians", 4}, {"1 Thessalonians", 5},
    {"2 Thessalonians", 3}, {"1 Timothy", 6}, {"2 Timothy", 4}, {"Titus", 3}, {"Philemon", 1}, {"Hebrews", 13}, {"James", 5},
    {"1 Peter", 5}, {"2 Peter", 3}, {"1 John", 5}, {"2 John", 1}, {"3 John", 1}, {"Jude", 1}, {"Revelation", 22},
};
static const int BOOK_COUNT = sizeof(BOOKS) / sizeof(BOOKS[0]);
static const int NT_FIRST = 39;                             // Matthew

// Chapters [first, end) of the books from @p startBook, as "Genesis 50; Exodus 1-2".
static void chapters_Text(int startBook, int first, int end, char* out, size_t size){
    size_t k = 0;
    out[0] = '\0';
    int book = startBook, before = 0;
    for (; book < BOOK_COUNT && before + BOOKS[book].chapters <= first; book++) before += BOOKS[book].chapters;
    int c = first;
    while (c < end && book < BOOK_COUNT){
        const int inBook = c - before + 1;
        const int last = (end - before < BOOKS[book].chapters ? end - before : BOOKS[book].chapters);
        int n;
        if (BOOKS[book].chapters == 1) n = snprintf(out + k, size - k, "%s%s", k ? "; " : "", BOOKS[book].name);
        else if (last == inBook) n = snprintf(out + k, size - k, "%s%s %d", k ? "; " : "", BOOKS[book].name, inBook);
        else n = snprintf(out + k, size - k, "%s%s %d-%d", k ? "; " : "", BOOKS[book].name, inBook, last);
        if (n < 0 || (size_t)n >= size - k) return;
        k += (size_t)n;
        c = before + last;
        before += BOOKS[book].chapters;
        book++;
    }
}

void dbl_Reading(const Dbl_Settings_t& s, uint32_t today, int y, int m, int d, char* out, size_t size){
    out[0] = '\0';
    const uint32_t start = s.planStart ? s.planStart : (uint32_t)mtb_Kit_Days_From_Civil(y, 1, 1);
    const uint32_t day = today >= start ? today - start : 0;
    switch (s.plan){
        case DBL_PLAN_BIBLE_YEAR: {
            const int i = (int)(day % 365), total = 1189;
            chapters_Text(0, i * total / 365, (i + 1) * total / 365, out, size);
            break;
        }
        case DBL_PLAN_NT: {
            const int i = (int)(day % 260);
            int before = 0;
            for (int b = 0; b < NT_FIRST; b++) before += BOOKS[b].chapters;
            chapters_Text(0, before + i, before + i + 1, out, size);
            break;
        }
        case DBL_PLAN_PSALMS_PROVERBS:
            if (d == 31) snprintf(out, size, "Proverbs 31; Psalms 119");
            else snprintf(out, size, "Proverbs %d; Psalms %d, %d, %d, %d, %d", d, d, d + 30, d + 60, d + 90, d + 120);
            break;
        default: break;
    }
    (void)m;
}

bool dbl_Read_Today(const Dbl_Settings_t& s, uint32_t today){ return s.lastRead == today; }

uint16_t dbl_Streak(const Dbl_Settings_t& s, uint32_t today){
    if (s.lastRead == today || s.lastRead + 1 == today) return s.streak;
    return 0;
}

void dbl_Mark_Read(Dbl_Settings_t& s, uint32_t today, bool read){
    if (read){
        if (s.lastRead == today) return;
        s.streak = (uint16_t)(s.lastRead + 1 == today ? s.streak + 1 : 1);
        s.lastRead = today;
    } else if (s.lastRead == today){
        s.streak = s.streak ? (uint16_t)(s.streak - 1) : 0;
        s.lastRead = s.streak ? today - 1 : 0;
    }
}
