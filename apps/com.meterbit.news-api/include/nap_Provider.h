/**
 * @file nap_Provider.h
 * @brief NewsAPI.org for the shared news engine (mtb_kit_news.h): the app's settings, the feeds they make, and
 *        how to ask for and read each feed. No Arduino, so it can be tested on a PC.
 *
 * NewsAPI v2 (checked 6 November 2026):
 *   /v2/top-headlines?country=us&category=&pageSize=   (the docs list "us" as the only country)
 *   /v2/everything?q=&language=&sortBy=publishedAt&pageSize=&from=
 *   Header X-Api-Key. Errors {"status":"error","code","message"}: 401 apiKey..., 429 rateLimited or
 *   apiKeyExhausted, 400 parameterInvalid; a search further back than the plan allows is refused.
 *   Developer (free): 100 requests a day, stories 24 hours late, a month of search, development use only.
 *   Business $449/month: 250,000 requests a month, real time, 5 years. Advanced: 2,000,000.
 *   The plan is probed once a day: a search 40 days back is refused on the Developer plan.
 */
#ifndef NAP_PROVIDER_H
#define NAP_PROVIDER_H

#include "mtb_kit_news.h"

#define NAP_SETTINGS_MAGIC  0x4E415031UL    // "NAP1"
#define NAP_NVS_KEY         "newsApiCfg"
#define NAP_MAX_CATEGORIES  4
#define NAP_MAX_TOPICS      3
#define NAP_TOPIC_LEN       32

enum Nap_View_t : uint8_t { NAP_VIEW_CARD = 0, NAP_VIEW_LIST };

struct Nap_Settings_t {
    uint32_t magic;
    uint8_t categoryCount;
    uint8_t categories[NAP_MAX_CATEGORIES];     /**< Indices in nap_Categories(). */
    uint8_t topicCount;
    char topics[NAP_MAX_TOPICS][NAP_TOPIC_LEN];
    char language[4];                           /**< For topics; "" any. */
    uint8_t floorMinutes;
    uint8_t pageSeconds;
    uint8_t autoCycle;
    uint8_t view;                               /**< Nap_View_t */
    char apiKey[40];
};

struct Nap_Category_t { const char* id; const char* name; uint16_t color; };
const Nap_Category_t* nap_Categories(size_t& count);
int nap_Category_Index(const char* id);

void nap_Settings_Default(Nap_Settings_t& settings);
void nap_Settings_Clamp(Nap_Settings_t& settings);
bool nap_Topic_Valid(const char* topic);
void nap_Config(const Nap_Settings_t& settings, Mtb_Kit_News_Config_t& config);
const Mtb_Kit_News_Provider_t& nap_Provider(void);

#endif
