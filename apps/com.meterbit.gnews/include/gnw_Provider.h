/**
 * @file gnw_Provider.h
 * @brief GNews (gnews.io) for the shared news engine (mtb_kit_news.h): the app's settings, the feeds they make, and
 *        how to ask for and read each feed. No Arduino, so it can be tested on a PC.
 *
 * GNews API v4 (checked 6 November 2026):
 *   https://gnews.io/api/v4/top-headlines?category=&lang=&country=&max=   https://gnews.io/api/v4/search?q=...
 *   Header X-Api-Key. Errors: 401 key, 403 quota used ("resets at 00:00 UTC") or email not verified, 429 too many
 *   requests a second, 400 bad parameter.
 *   Free: 100 requests a day, 10 stories a request, 12-hour delay, an "information" object in every answer.
 *   Essential 1,000 a day / 25 stories, Business 5,000 / 50, Enterprise 25,000 / 100: real time, no "information".
 *   The plan shows in the answers: the information object, and how many stories a max=51 request returns.
 */
#ifndef GNW_PROVIDER_H
#define GNW_PROVIDER_H

#include "mtb_kit_news.h"

#define GNW_SETTINGS_MAGIC  0x474E5731UL    // "GNW1"
#define GNW_NVS_KEY         "gnewsCfg"
#define GNW_MAX_CATEGORIES  4
#define GNW_MAX_TOPICS      3
#define GNW_TOPIC_LEN       32

enum Gnw_View_t : uint8_t { GNW_VIEW_CARD = 0, GNW_VIEW_LIST };

struct Gnw_Settings_t {
    uint32_t magic;
    uint8_t categoryCount;
    uint8_t categories[GNW_MAX_CATEGORIES];     /**< Indices in gnw_Categories(). */
    uint8_t topicCount;
    char topics[GNW_MAX_TOPICS][GNW_TOPIC_LEN];
    char language[4];
    char country[4];
    uint8_t floorMinutes;
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t view;                               /**< Gnw_View_t */
    char apiKey[40];
};

struct Gnw_Category_t { const char* id; const char* name; uint16_t color; };
const Gnw_Category_t* gnw_Categories(size_t& count);
int gnw_Category_Index(const char* id);

void gnw_Settings_Default(Gnw_Settings_t& settings);
void gnw_Settings_Clamp(Gnw_Settings_t& settings);
/** @brief Whether a topic is acceptable: 2 to 31 letters, digits, spaces and - ' . &. */
bool gnw_Topic_Valid(const char* topic);
/** @brief The engine's configuration: categories first, then topics. */
void gnw_Config(const Gnw_Settings_t& settings, Mtb_Kit_News_Config_t& config);

/** @brief The provider (its requests and parser). */
const Mtb_Kit_News_Provider_t& gnw_Provider(void);

#endif
