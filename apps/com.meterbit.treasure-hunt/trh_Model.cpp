// Treasure Hunt's model: clues, codes and the ready-made hunts. No Arduino.
#include "trh_Model.h"
#include <stdio.h>
#include <string.h>

struct Preset_Clue_t { const char* text; const char* hint; };

// The ready-made hunts (our own words). Each clue says where the next card is.
static const Preset_Clue_t HOME[] = {
    {"I keep food cold, day and night. Look in me or on me!", "In the kitchen. I hum."},
    {"You sit on me to read or watch. Look under my cushions.", "Soft, in the living room"},
    {"Where do your shoes sleep at night?", "By the door"},
    {"Look where you wash your hands and brush your teeth.", "Water and soap live here"},
    {"I am full of stories. Look between my books.", "A shelf of pages"},
    {"Your head rests on me every night.", "On your bed"},
    {"I tell everyone the time. Look near me.", "Tick tock!"},
    {"Dirty clothes wait here for a wash.", "A basket or a machine"},
};
static const Preset_Clue_t GARDEN[] = {
    {"Find where the flowers grow tall.", "Colours and petals"},
    {"Look under a pot. Gently!", "Plants live in me"},
    {"Find the biggest tree you can see.", "Look up, then down at its roots"},
    {"Where does the water come out for the plants?", "A tap or a hose"},
    {"People come in and out through me.", "The gate or the door"},
    {"Look under a chair where grown-ups rest outside.", "Garden furniture"},
};
static const Preset_Clue_t PARTY[] = {
    {"Find the table with the yummy food.", "Where the snacks are"},
    {"Look where the music comes from.", "A speaker or a radio"},
    {"Find something that floats and pops!", "A balloon"},
    {"Look near the door where guests came in.", "The front door"},
    {"Find a place with lots of cushions.", "Somewhere comfy"},
    {"The last card is near the birthday cake!", "Candles!"},
};
static const struct { const Preset_Clue_t* clues; int count; const char* name; const char* prize; } PRESETS[TRH_PRESETS] = {
    {HOME, 8, "At home", "Look under your pillow!"},
    {GARDEN, 6, "In the garden", "Ask a grown-up for your prize!"},
    {PARTY, 6, "A party", "Everyone gets a prize!"},
};

static uint32_t next(uint32_t& r){ r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r ? r : (r = 0x2545F491u); }

void trh_Settings_Default(Trh_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = TRH_SETTINGS_MAGIC;
    s.useCodes = 1;
    s.showTimer = 1;
    s.seed = 0x1234567u;
    trh_Preset(s, 0);
}

void trh_Settings_Clamp(Trh_Settings_t& s){
    s.magic = TRH_SETTINGS_MAGIC;
    if (s.count > TRH_CLUES) s.count = TRH_CLUES;
    for (int i = 0; i < TRH_CLUES; i++){
        s.clues[i].text[TRH_TEXT - 1] = '\0';
        s.clues[i].hint[TRH_HINT - 1] = '\0';
        if (s.clues[i].code < 100 || s.clues[i].code > 999) s.clues[i].code = (uint16_t)(100 + (i * 137 + 211) % 900);
    }
    s.useCodes = s.useCodes ? 1 : 0;
    s.showTimer = s.showTimer ? 1 : 0;
    s.prize[sizeof(s.prize) - 1] = '\0';
    if (!s.seed) s.seed = 0x1234567u;
}

static void trim(char* t){
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ')) t[--n] = '\0';
    size_t k = 0;
    while (t[k] == ' ') k++;
    if (k) memmove(t, t + k, strlen(t + k) + 1);
}

static uint16_t fresh_Code(const Trh_Settings_t& s, uint32_t& r, int upTo){
    for (;;){
        const uint16_t c = (uint16_t)(100 + next(r) % 900);
        bool used = false;
        for (int i = 0; i < upTo; i++) if (s.clues[i].code == c) used = true;
        if (!used) return c;
    }
}

bool trh_Add_Clue(Trh_Settings_t& s, const char* text){
    if (!text || s.count >= TRH_CLUES) return false;
    char t[TRH_TEXT + TRH_HINT + 4];
    snprintf(t, sizeof(t), "%s", text);
    for (char* p = t; *p; p++) if ((unsigned char)*p < ' ') *p = ' ';
    char* bar = strchr(t, '|');
    char hint[TRH_HINT] = "";
    if (bar){ *bar = '\0'; snprintf(hint, sizeof(hint), "%s", bar + 1); trim(hint); }
    trim(t);
    if (strlen(t) < 3 || strlen(t) >= TRH_TEXT) return false;
    Trh_Clue_t& c = s.clues[s.count];
    snprintf(c.text, sizeof(c.text), "%s", t);
    snprintf(c.hint, sizeof(c.hint), "%s", hint);
    c.code = fresh_Code(s, s.seed, s.count);
    s.count++;
    return true;
}

void trh_New_Codes(Trh_Settings_t& s){
    for (int i = 0; i < s.count; i++) s.clues[i].code = 0;
    for (int i = 0; i < s.count; i++) s.clues[i].code = fresh_Code(s, s.seed, i);
}

bool trh_Preset(Trh_Settings_t& s, uint8_t which){
    if (which >= TRH_PRESETS) return false;
    s.count = 0;
    for (int i = 0; i < PRESETS[which].count && i < TRH_CLUES; i++){
        Trh_Clue_t& c = s.clues[s.count++];
        snprintf(c.text, sizeof(c.text), "%s", PRESETS[which].clues[i].text);
        snprintf(c.hint, sizeof(c.hint), "%s", PRESETS[which].clues[i].hint);
        c.code = 0;
    }
    trh_New_Codes(s);
    snprintf(s.prize, sizeof(s.prize), "%s", PRESETS[which].prize);
    return true;
}

const char* trh_Preset_Name(uint8_t which){ return which < TRH_PRESETS ? PRESETS[which].name : ""; }

void trh_Time_Text(char* d, size_t n, uint32_t s){
    if (s >= 3600) snprintf(d, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else snprintf(d, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
