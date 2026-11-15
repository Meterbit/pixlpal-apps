// Catholic Daily: the model.
#include "cth_Model.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

void cth_Settings_Default(Cth_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = CTH_SETTINGS_MAGIC;
    s.verse = 1;
    s.hour24 = 1;
    s.sound = CTH_SOUND_BELL;
    s.angelus = 1;
    s.rosary = 1;
    static const struct { uint16_t m; uint8_t days; const char* l; } R[CTH_REMINDERS] = {
        {20 * 60, 0, "The Rosary"}, {9 * 60, 3, "Sunday Mass"}, {21 * 60 + 30, 0, "Examination of conscience"}};
    for (int i = 0; i < CTH_REMINDERS; i++){
        s.reminders[i].minute = R[i].m;
        s.reminders[i].days = R[i].days;
        fth_Copy(s.reminders[i].label, sizeof(s.reminders[i].label), R[i].l);
    }
}

void cth_Settings_Clamp(Cth_Settings_t& s){
    s.magic = CTH_SETTINGS_MAGIC;
    if (s.translation >= cth_Translations()) s.translation = 0;
    if (s.language > 3) s.language = 0;
    if (s.sound > CTH_SOUND_BELL) s.sound = CTH_SOUND_BELL;
    if (s.reminderSlot >= CTH_REMINDERS) s.reminderSlot = 0;
    s.verse = s.verse ? 1 : 0;
    s.hour24 = s.hour24 ? 1 : 0;
    s.angelus = s.angelus ? 1 : 0;
    s.mercy = s.mercy ? 1 : 0;
    s.rosary = s.rosary ? 1 : 0;
    for (Fth_Reminder_t& r : s.reminders) fth_Reminder_Clamp(r);
}

// The public domain Catholic Bible with the deuterocanonical books (bible-api.com), Psalms numbered as the Vulgate.
// (bible-api.com's Clementine Vulgate lacks whole books, Matthew among them, so it is not offered.)
static const Fth_Edition_t TRANSLATIONS[] = { {"dra", "English, Douay-Rheims"} };
int cth_Translations(void){ return (int)(sizeof(TRANSLATIONS) / sizeof(TRANSLATIONS[0])); }
const Fth_Edition_t* cth_Translation(int i){ return i >= 0 && i < cth_Translations() ? &TRANSLATIONS[i] : &TRANSLATIONS[0]; }

size_t cth_Calendar_Path(uint8_t language, int y, int m, int d, char* out, size_t size){
    static const char* const L[] = {"en/calendars/default", "fr/calendars/general-fr", "it/calendars/general-it", "la/calendars/general-la"};
    const int n = snprintf(out, size, "/api/v0/%s/%d/%d/%d", L[language <= 3 ? language : 0], y, m, d);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

bool cth_Parse_Day(JsonDocument& doc, uint32_t day, Cth_Day_t& out){
    memset(&out, 0, sizeof(out));
    if (!doc["celebrations"].is<JsonArray>()) return false;
    fth_Copy(out.season, sizeof(out.season), doc["season"] | "");
    out.week = (uint8_t)(doc["season_week"] | 0);
    for (JsonObjectConst c : doc["celebrations"].as<JsonArrayConst>()){
        if (out.count >= CTH_CELEBRATIONS) break;
        Cth_Celebration_t& x = out.c[out.count++];
        fth_Copy(x.title, sizeof(x.title), c["title"] | "");
        fth_Copy(x.colour, sizeof(x.colour), c["colour"] | "white");
        fth_Copy(x.rank, sizeof(x.rank), c["rank"] | "");
        x.rankNum = c["rank_num"] | 9.0f;
    }
    if (!out.count) return false;
    out.day = day;
    out.fromCalendar = true;
    return true;
}

void cth_Fallback_Day(int y, int m, int d, uint32_t day, Cth_Day_t& out){
    memset(&out, 0, sizeof(out));
    Fth_Church_Day_t c;
    fth_Church_Day(y, m, d, false, c);
    static const char* const S[] = {"advent", "christmas", "ordinary", "lent", "lent", "easter"};
    fth_Copy(out.season, sizeof(out.season), c.season < FTH_SEASONS ? S[c.season] : "ordinary");
    out.day = day;
    out.count = 1;
    fth_Copy(out.c[0].title, sizeof(out.c[0].title), c.feast[0] ? c.feast : c.seasonName);
    fth_Copy(out.c[0].colour, sizeof(out.c[0].colour), c.colorName);
    out.c[0].rankNum = 9;
}

static void ordinal(char* out, size_t size, int n){
    const char* suffix = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    snprintf(out, size, "%d%s", n, suffix);
}

void cth_Season_Text(const Cth_Day_t& d, char* out, size_t size){
    char n[8];
    ordinal(n, sizeof(n), d.week);
    const char* s = d.season;
    if (!strcmp(s, "ordinary")) { if (d.week) snprintf(out, size, "%s week in Ordinary Time", n); else snprintf(out, size, "Ordinary Time"); }
    else if (!strcmp(s, "advent")) { if (d.week) snprintf(out, size, "%s week of Advent", n); else snprintf(out, size, "Advent"); }
    else if (!strcmp(s, "lent")) { if (d.week) snprintf(out, size, "%s week of Lent", n); else snprintf(out, size, "Lent"); }
    else if (!strcmp(s, "easter")) { if (d.week) snprintf(out, size, "%s week of Easter", n); else snprintf(out, size, "Easter"); }
    else if (!strcmp(s, "christmas")) snprintf(out, size, "Christmas Time");
    else if (s[0]){ snprintf(out, size, "%s", s); out[0] = (char)toupper((unsigned char)out[0]); }
    else out[0] = '\0';
}

void cth_Rank_Text(const Cth_Celebration_t& c, char* out, size_t size){
    out[0] = '\0';
    if (!c.rank[0]) return;
    // English ranks the panel names; the weekday and Sunday ranks say nothing more than the title.
    if (!strcasecmp(c.rank, "ferial") || !strcasecmp(c.rank, "Sunday") || !strcasecmp(c.rank, "Primary liturgical days") ||
        !strcasecmp(c.rank, "privileged ferial") || !strcasecmp(c.rank, "ferial privileged")) return;
    if (!strcasecmp(c.rank, "Easter triduum")){ fth_Copy(out, size, "EASTER TRIDUUM"); return; }
    mtb_Kit_To_Ascii(out, size, c.rank);
    mtb_Kit_Upper(out);
}

uint16_t cth_Colour(const Cth_Day_t& d){ return fth_Liturgical_Color(d.count ? d.c[0].colour : "green"); }

uint8_t cth_Rosary(int weekday, const char* season){
    if (weekday == 6){
        if (season && (!strcmp(season, "advent") || !strcmp(season, "christmas"))) return CTH_JOYFUL;
        if (season && !strcmp(season, "lent")) return CTH_SORROWFUL;
        return CTH_GLORIOUS;
    }
    static const uint8_t WEEK[6] = {CTH_JOYFUL, CTH_SORROWFUL, CTH_GLORIOUS, CTH_LUMINOUS, CTH_SORROWFUL, CTH_JOYFUL};
    return weekday >= 0 && weekday < 6 ? WEEK[weekday] : CTH_GLORIOUS;
}

const char* cth_Rosary_Name(uint8_t set){
    static const char* const N[] = {"The Joyful Mysteries", "The Sorrowful Mysteries", "The Glorious Mysteries", "The Luminous Mysteries"};
    return set < 4 ? N[set] : N[2];
}

const char* cth_Mystery(uint8_t set, int i){
    static const char* const M[4][5] = {
        {"The Annunciation", "The Visitation", "The Nativity of Our Lord", "The Presentation in the Temple", "The Finding in the Temple"},
        {"The Agony in the Garden", "The Scourging at the Pillar", "The Crowning with Thorns", "The Carrying of the Cross", "The Crucifixion"},
        {"The Resurrection", "The Ascension", "The Descent of the Holy Spirit", "The Assumption of Mary", "The Coronation of Mary"},
        {"The Baptism of the Lord", "The Wedding at Cana", "The Proclamation of the Kingdom", "The Transfiguration", "The Institution of the Eucharist"},
    };
    return set < 4 && i >= 0 && i < 5 ? M[set][i] : "";
}

const char* cth_Angelus_Name(const char* season){
    return season && !strcmp(season, "easter") ? "Regina Caeli" : "The Angelus";
}
