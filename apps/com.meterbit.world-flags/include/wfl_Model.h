/**
 * @file wfl_Model.h
 * @brief World Flags' model: settings, the 270 countries the phone can pick (wfl_Country_List.cpp: the phone app's
 *        former list, with capitals and continents), the search, and the playlist: every flag, one continent's, or
 *        your favourites, shuffled so each shows once before any repeats. No Arduino, so it can be tested on a PC.
 */
#ifndef WFL_MODEL_H
#define WFL_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define WFL_SETTINGS_MAGIC  0x57464C32UL    // "WFL2"
#define WFL_NVS_KEY         "worldFlagsCfg"
#define WFL_FAVOURITES      40
#define WFL_MAX_COUNTRIES   300

extern const unsigned WFL_COUNTRY_COUNT;
extern const char WFL_COUNTRY_LIST[];

enum Wfl_Mode_t : uint8_t { WFL_ONE = 0, WFL_SLIDESHOW, WFL_QUIZ, WFL_MODE_COUNT };
enum Wfl_Source_t : uint8_t { WFL_ALL = 0, WFL_CONTINENT, WFL_FAVOURITE, WFL_SOURCE_COUNT };
#define WFL_CONTINENTS 7

struct Wfl_Settings_t {
    uint32_t magic;
    uint8_t mode;                   /**< Wfl_Mode_t */
    uint8_t source;                 /**< Wfl_Source_t: what the slideshow and the quiz draw from. */
    uint8_t continent;              /**< 0-6: Africa, Antarctica, Asia, Europe, North America, Oceania, South America. */
    char shown[3];                  /**< The flag shown in "one flag" mode. */
    uint8_t favouriteCount;
    char favourites[WFL_FAVOURITES][3];
    uint16_t seconds;               /**< Per flag in the slideshow (3-600). */
    uint8_t showInfo;               /**< Name, capital and continent beside the flag. */
    uint8_t quizSeconds;            /**< The quiz shows the answer after this (3-30). */
};

struct Wfl_Country_t { char code[3]; char name[48]; char capital[32]; char continent[16]; };

void wfl_Settings_Default(Wfl_Settings_t& settings);
void wfl_Settings_Clamp(Wfl_Settings_t& settings);
const char* wfl_Continent_Name(uint8_t continent);

/** @brief The list's countries by index (0 to WFL_COUNTRY_COUNT - 1). The index is built on first use. */
bool wfl_Country(unsigned index, Wfl_Country_t& country);
/** @brief The country with flag code @p code ("ng"), or with this name ("Nigeria", any case). -1 if none. */
int wfl_Find_Code(const char* code);
int wfl_Find_Name(const char* name);
/** @brief Countries whose name, capital or code starts with (or has a word starting with) each word of @p query. */
int wfl_Search(const char* query, Wfl_Country_t* results, int max);

/** @brief The countries the settings' source covers, as list indices. Returns how many. */
int wfl_Playlist(const Wfl_Settings_t& settings, uint16_t* indices, int max);

/** @brief A shuffled order of 0..count-1 that is played through before reshuffling (no repeats until the end). */
struct Wfl_Bag_t { uint16_t order[WFL_MAX_COUNTRIES]; uint16_t count, position; uint32_t seed; };
void wfl_Bag_Fill(Wfl_Bag_t& bag, uint16_t count, uint32_t seed);
/** @brief The next index from the bag (reshuffled when used up, never starting with the last one shown). */
uint16_t wfl_Bag_Next(Wfl_Bag_t& bag);

#endif
