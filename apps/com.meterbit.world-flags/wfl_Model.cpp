#include "wfl_Model.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char* const CONTINENTS[WFL_CONTINENTS] = {"Africa", "Antarctica", "Asia", "Europe", "North America", "Oceania", "South America"};

const char* wfl_Continent_Name(uint8_t c){ return c < WFL_CONTINENTS ? CONTINENTS[c] : ""; }

void wfl_Settings_Default(Wfl_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = WFL_SETTINGS_MAGIC;
    s.mode = WFL_SLIDESHOW;
    s.source = WFL_ALL;
    s.continent = 0;
    memcpy(s.shown, "ng", 3);
    s.seconds = 20;
    s.showInfo = 1;
    s.quizSeconds = 8;
}

void wfl_Settings_Clamp(Wfl_Settings_t& s){
    if (s.mode >= WFL_MODE_COUNT) s.mode = WFL_SLIDESHOW;
    if (s.source >= WFL_SOURCE_COUNT) s.source = WFL_ALL;
    if (s.continent >= WFL_CONTINENTS) s.continent = 0;
    s.shown[2] = '\0';
    if (wfl_Find_Code(s.shown) < 0) memcpy(s.shown, "ng", 3);
    if (s.favouriteCount > WFL_FAVOURITES) s.favouriteCount = WFL_FAVOURITES;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.favouriteCount; i++){
        char code[3] = {s.favourites[i][0], s.favourites[i][1], 0};
        if (wfl_Find_Code(code) >= 0) memcpy(s.favourites[kept++], code, 3);
    }
    s.favouriteCount = kept;
    if (s.source == WFL_FAVOURITE && kept == 0) s.source = WFL_ALL;
    if (s.seconds < 3) s.seconds = 3;
    if (s.seconds > 600) s.seconds = 600;
    s.showInfo = s.showInfo ? 1 : 0;
    if (s.quizSeconds < 3) s.quizSeconds = 3;
    if (s.quizSeconds > 30) s.quizSeconds = 30;
}

//##############################################################################################################
// Where each line starts, built on first use.
static uint32_t lineStart[WFL_MAX_COUNTRIES];
static volatile unsigned lineCount = 0;

static void wfl_Index(void){
    if (lineCount) return;
    // Counted locally and published at the end: the BLE task may build it at the same time (with the same result).
    unsigned n = 0;
    const char* p = WFL_COUNTRY_LIST;
    while (*p && n < WFL_MAX_COUNTRIES){
        lineStart[n++] = (uint32_t)(p - WFL_COUNTRY_LIST);
        const char* end = strchr(p, '\n');
        if (end == nullptr) break;
        p = end + 1;
    }
    lineCount = n;
}

static void wfl_Field(const char*& p, char* dest, size_t size){
    size_t n = 0;
    while (*p && *p != '\t' && *p != '\n'){ if (n + 1 < size) dest[n++] = *p; p++; }
    dest[n] = '\0';
    if (*p == '\t') p++;
}

bool wfl_Country(unsigned index, Wfl_Country_t& c){
    wfl_Index();
    if (index >= lineCount) return false;
    const char* p = WFL_COUNTRY_LIST + lineStart[index];
    wfl_Field(p, c.code, sizeof(c.code));
    wfl_Field(p, c.name, sizeof(c.name));
    wfl_Field(p, c.capital, sizeof(c.capital));
    wfl_Field(p, c.continent, sizeof(c.continent));
    return true;
}

int wfl_Find_Code(const char* code){
    if (code == nullptr || strlen(code) != 2) return -1;
    wfl_Index();
    for (unsigned i = 0; i < lineCount; i++){
        const char* p = WFL_COUNTRY_LIST + lineStart[i];
        if (tolower((unsigned char)code[0]) == p[0] && tolower((unsigned char)code[1]) == p[1] && p[2] == '\t') return (int)i;
    }
    return -1;
}

int wfl_Find_Name(const char* name){
    if (name == nullptr) return -1;
    Wfl_Country_t c;
    for (unsigned i = 0; wfl_Country(i, c); i++) if (strcasecmp(c.name, name) == 0) return (int)i;
    return -1;
}

static bool wfl_Has_Word(const char* field, const char* word, size_t n){
    for (const char* f = field; *f; f++){
        const bool start = f == field || f[-1] == ' ' || f[-1] == '-' || f[-1] == '(';
        if (start && strncasecmp(f, word, n) == 0) return true;
    }
    return false;
}

int wfl_Search(const char* query, Wfl_Country_t* results, int max){
    if (query == nullptr || max <= 0) return 0;
    int count = 0;
    Wfl_Country_t c;
    // Names first, then capitals and codes.
    for (int pass = 0; pass < 2 && count < max; pass++){
        for (unsigned i = 0; count < max && wfl_Country(i, c); i++){
            bool all = true, any = false, byName = true;
            const char* w = query;
            while (*w && all){
                while (*w == ' ' || *w == ',') w++;
                const size_t n = strcspn(w, " ,");
                if (n == 0) break;
                any = true;
                const bool name = wfl_Has_Word(c.name, w, n);
                if (!name) byName = false;
                all = name || wfl_Has_Word(c.capital, w, n) || (n == 2 && strncasecmp(c.code, w, 2) == 0);
                w += n;
            }
            if (any && all && (pass == 0) == byName) results[count++] = c;
        }
    }
    return count;
}

int wfl_Playlist(const Wfl_Settings_t& s, uint16_t* indices, int max){
    int count = 0;
    if (s.source == WFL_FAVOURITE && s.favouriteCount){
        for (uint8_t i = 0; i < s.favouriteCount && count < max; i++){
            const int index = wfl_Find_Code(s.favourites[i]);
            if (index >= 0) indices[count++] = (uint16_t)index;
        }
        return count;
    }
    Wfl_Country_t c;
    for (unsigned i = 0; count < max && wfl_Country(i, c); i++){
        if (s.source == WFL_CONTINENT && strcmp(c.continent, wfl_Continent_Name(s.continent)) != 0) continue;
        indices[count++] = (uint16_t)i;
    }
    return count;
}

//##############################################################################################################
static uint32_t wfl_Random(uint32_t& seed){
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}

static void wfl_Shuffle(Wfl_Bag_t& bag){
    for (uint16_t i = 0; i < bag.count; i++) bag.order[i] = i;
    for (uint16_t i = bag.count; i > 1; i--){
        const uint16_t j = (uint16_t)(wfl_Random(bag.seed) % i);
        const uint16_t t = bag.order[i - 1]; bag.order[i - 1] = bag.order[j]; bag.order[j] = t;
    }
    bag.position = 0;
}

void wfl_Bag_Fill(Wfl_Bag_t& bag, uint16_t count, uint32_t seed){
    bag.count = count > WFL_MAX_COUNTRIES ? WFL_MAX_COUNTRIES : count;
    bag.seed = seed ? seed : 0x2545F491u;
    wfl_Shuffle(bag);
}

uint16_t wfl_Bag_Next(Wfl_Bag_t& bag){
    if (bag.count == 0) return 0;
    if (bag.position >= bag.count){
        const uint16_t last = bag.order[bag.count - 1];
        wfl_Shuffle(bag);
        // Not the same flag twice in a row across a reshuffle.
        if (bag.count > 1 && bag.order[0] == last){ bag.order[0] = bag.order[bag.count - 1]; bag.order[bag.count - 1] = last; }
    }
    return bag.order[bag.position++];
}
