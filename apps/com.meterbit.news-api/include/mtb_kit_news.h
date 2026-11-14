/**
 * @file mtb_kit_news.h
 * @brief A news engine shared by the headline apps: feeds (categories or topics), stories, a daily request budget
 *        spread over the day, the plan, back-off, a flash cache, and the screens (a story card and a headline list).
 *        Each app supplies a provider: how to ask for a feed, and how to read the answer.
 *
 * Three layers:
 *   model   stories, pacing and merging, no Arduino (mtb_kit_news_model.cpp): tested on a PC
 *   engine  the fetch task and cache (mtb_kit_news_engine.cpp)
 *   screens mtb_Kit_News_Draw_Card(), mtb_Kit_News_Draw_List() (mtb_kit_news_ui.cpp)
 */
#ifndef MTB_KIT_NEWS_H
#define MTB_KIT_NEWS_H

#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

#define KIT_NEWS_FEEDS          7
#define KIT_NEWS_PER_FEED       12
#define KIT_NEWS_STORIES        (KIT_NEWS_FEEDS * KIT_NEWS_PER_FEED)
#define KIT_NEWS_TITLE          160
#define KIT_NEWS_SUMMARY        240
#define KIT_NEWS_SOURCE         28
#define KIT_NEWS_KEY_LEN        72
#define KIT_NEWS_PROBE          0xFF    /**< The "feed" of a plan probe request. */

enum Mtb_Kit_News_Result_t : uint8_t {
    KIT_NEWS_OK = 0, KIT_NEWS_NET, KIT_NEWS_PARSE, KIT_NEWS_KEY, KIT_NEWS_QUOTA, KIT_NEWS_RATE, KIT_NEWS_PLAN,
    KIT_NEWS_BAD_REQUEST, KIT_NEWS_SERVER
};

struct Mtb_Kit_Story_t {
    uint32_t published;             /**< Unix time. */
    uint32_t hash;                  /**< Of the title: the same story in a later answer is not "new". */
    uint8_t feed;
    uint8_t fresh;                  /**< Arrived in the latest answer and was not there before. */
    char source[KIT_NEWS_SOURCE];
    char title[KIT_NEWS_TITLE];     /**< ASCII. */
    char summary[KIT_NEWS_SUMMARY]; /**< ASCII; may be empty. */
};

/** @brief A feed: a category or a search, as the provider understands @p kind and @p query. */
struct Mtb_Kit_Feed_t {
    uint8_t kind;                   /**< The provider's: e.g. 0 a category, 1 a search. */
    uint8_t category;               /**< The provider's category index, for kind 0. */
    uint16_t color;                 /**< The feed's band colour. */
    char name[20];                  /**< As the band shows it: "TECHNOLOGY", "ARSENAL". */
    char query[40];                 /**< Search words, for a search. */
};

struct Mtb_Kit_News_Config_t {
    uint8_t feedCount;
    Mtb_Kit_Feed_t feeds[KIT_NEWS_FEEDS];
    char language[4];               /**< "en", or "" for any. */
    char country[4];                /**< "ng", or "" for any. */
    uint16_t floorMinutes;          /**< No feed more often than this. */
    char apiKey[KIT_NEWS_KEY_LEN];
};

struct Mtb_Kit_News_Plan_t {
    uint8_t known;                  /**< The provider has told us. */
    uint8_t paid;
    uint8_t delayHours;             /**< How old the newest stories are on this plan (0: real time). */
    uint8_t maxPerRequest;          /**< Stories one request may return. */
    uint32_t dailyLimit;            /**< Requests a day. */
    uint32_t checked;               /**< When the plan was last learned. */
    char name[20];
};

struct Mtb_Kit_News_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    uint8_t storyCount;
    Mtb_Kit_Story_t stories[KIT_NEWS_STORIES];      /**< By feed, newest first within a feed. */
    uint32_t feedFetched[KIT_NEWS_FEEDS];
    uint8_t feedResult[KIT_NEWS_FEEDS];
    uint32_t configHash;            /**< The feeds the stories belong to: a change empties the cache. */
    uint32_t usedDay;               /**< UTC day of usedToday. */
    uint16_t usedToday;             /**< Requests this device made today (the providers send no counts). */
    uint8_t quotaUsed;              /**< The provider said today's requests are used up. */
    Mtb_Kit_News_Plan_t plan;
};

struct Mtb_Kit_News_Status_t {
    bool started;
    bool busy;
    bool keyMissing;
    bool keyRejected;
    bool quotaUsed;
    bool rateLimited;
    bool failing;
    uint32_t feedInterval;          /**< Seconds between reads of each feed at the current pace. */
    uint32_t lastFetch;
};

/** @brief What a provider does. All functions run on the engine's fetch task. */
struct Mtb_Kit_News_Provider_t {
    const char* label;              /**< For the log: "GNews". */
    const char* userAgent;
    const char* cacheFile;          /**< LittleFS path. */
    uint32_t defaultDailyLimit;     /**< Assumed until the plan is known: the free plan's. */
    uint16_t reserve;               /**< Requests kept back each day for "refresh now". */
    uint32_t probeEverySec;         /**< 0: no separate plan probe; the feeds' answers tell. */
    size_t maxBody;                 /**< Largest answer read. */
    /**
     * @brief The request for a feed (or KIT_NEWS_PROBE): the URL, and up to two header name/value pairs (ending in
     *        nullptr). False if this feed cannot be asked for.
     */
    bool (*request)(const Mtb_Kit_News_Config_t& config, uint8_t feed, const Mtb_Kit_News_Plan_t& plan, uint32_t now,
                    char* url, size_t urlSize, const char** headers);
    /**
     * @brief Reads an answer (parsed JSON; null on a non-JSON body) into @p stories (at most @p max) and may update
     *        @p plan. Returns the result.
     */
    uint8_t (*parse)(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_News_Config_t& config, uint32_t now,
                     Mtb_Kit_Story_t* stories, uint8_t max, uint8_t& count, Mtb_Kit_News_Plan_t& plan);
    /**
     * @brief Optional, for answers that are not JSON (RSS and Atom): reads the raw body (NUL-terminated; null with no
     *        body) instead of parse(). @p truncated: the body was cut at maxBody.
     */
    uint8_t (*parseText)(uint8_t feed, int httpCode, const char* body, size_t length, bool truncated, const Mtb_Kit_News_Config_t& config,
                         uint32_t now, Mtb_Kit_Story_t* stories, uint8_t max, uint8_t& count, Mtb_Kit_News_Plan_t& plan);
    bool keyless;                   /**< The feeds need no key: an empty apiKey is not "key missing". */
};

/** @brief The engine of one app: create one static instance and leave it to the functions below. */
struct Mtb_Kit_News_t {
    const Mtb_Kit_News_Provider_t* provider;
    void* lock;                     /**< SemaphoreHandle_t */
    Mtb_Kit_News_Data_t* published;
    Mtb_Kit_News_Config_t config;
    uint32_t version;
    uint32_t generation;
    uint32_t due[KIT_NEWS_FEEDS];
    uint32_t probeDue;
    uint32_t lastManual;
    uint32_t lastFetch;
    uint32_t interval;
    uint8_t fails;
    uint8_t lastResult;
    uint32_t lastSave;              /**< When the cache was last written. */
    bool started;
    bool busy;
};

// Model ------------------------------------------------------------------------------------------------------
/** @brief FNV-1a of a text, for story identity and config identity. */
uint32_t mtb_Kit_News_Hash(const char* text, uint32_t seed = 2166136261u);
uint32_t mtb_Kit_News_Config_Hash(const Mtb_Kit_News_Config_t& config);

/**
 * @brief Seconds between reads of each feed so what is left of today's requests (less @p reserve) lasts until
 *        midnight UTC, never faster than @p floorSec, never slower than 6 hours. 0 if nothing is left today.
 */
uint32_t mtb_Kit_News_Interval(uint32_t dailyLimit, uint32_t usedToday, uint8_t feedCount, uint32_t now, uint32_t floorSec, uint16_t reserve);

/**
 * @brief Replaces a feed's stories with @p count new ones (newest first after sorting), marking those whose title
 *        was not there before as fresh (unless the feed had none: a first load is not news). Other feeds are kept.
 */
void mtb_Kit_News_Merge(Mtb_Kit_News_Data_t& data, uint8_t feed, const Mtb_Kit_Story_t* stories, uint8_t count);

/** @brief Clears a story then fills it from provider text (UTF-8 in, ASCII out); the title's " - Source" tail is cut. */
void mtb_Kit_News_Fill_Story(Mtb_Kit_Story_t& story, const char* title, const char* source, const char* summary, uint32_t published);

/** @brief Indices of the stories in showing order: feed by feed, newest first. Returns how many. */
int mtb_Kit_News_Order(const Mtb_Kit_News_Data_t& data, uint8_t* order, int max);

// Engine -----------------------------------------------------------------------------------------------------
void mtb_Kit_News_Begin(Mtb_Kit_News_t& engine, const Mtb_Kit_News_Provider_t& provider, const Mtb_Kit_News_Config_t& config);
void mtb_Kit_News_Set_Config(Mtb_Kit_News_t& engine, const Mtb_Kit_News_Config_t& config);
void mtb_Kit_News_Poll(Mtb_Kit_News_t& engine, bool online, uint32_t now);
bool mtb_Kit_News_Copy(Mtb_Kit_News_t& engine, Mtb_Kit_News_Data_t& out, uint32_t& version);
void mtb_Kit_News_Get_Status(Mtb_Kit_News_t& engine, Mtb_Kit_News_Status_t& status);
/** @brief The plan and today's use, without copying the stories. */
void mtb_Kit_News_Get_Plan(Mtb_Kit_News_t& engine, Mtb_Kit_News_Plan_t& plan, uint16_t& usedToday);
/** @brief Reads every feed now. False within two minutes of the last time, or with today's requests used. */
bool mtb_Kit_News_Refresh(Mtb_Kit_News_t& engine, uint32_t now);

// Screens ----------------------------------------------------------------------------------------------------
struct Mtb_Kit_News_View_t {
    const Mtb_Kit_News_Data_t* data;
    const Mtb_Kit_News_Config_t* config;
    const uint8_t* order;           /**< From mtb_Kit_News_Order(). */
    int orderCount;
    int current;                    /**< Index in order. */
    uint32_t now;
    float progress;                 /**< 0-1: how long until the next story, for the bar. */
    bool details;                   /**< The card shows the summary instead of the headline. */
    bool paused;
    const char* credit;             /**< "GNEWS", shown at the bottom right. */
};

/** @brief One story: the feed's band, the source and age, the headline as large as it fits, a progress bar. */
void mtb_Kit_News_Draw_Card(const Mtb_Kit_News_View_t& view);
/** @brief Six headlines, one line each, the current one lit. */
void mtb_Kit_News_Draw_List(const Mtb_Kit_News_View_t& view);
/** @brief A full-screen message (no key, offline...). */
void mtb_Kit_News_Draw_Message(const char* title, const char* detail, const char* credit);

#endif
