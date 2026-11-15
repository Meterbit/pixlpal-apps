/**
 * @file rss_Model.h
 * @brief RSS Reader's model: settings, the catalogue of ready-made feeds, the feed reader (RSS 2.0, RSS 1.0/RDF and
 *        Atom, read without building a tree, so a cut-off answer still gives its whole items), the date formats feeds
 *        use, and the muted-words filter. No Arduino, so it can be tested on a PC.
 */
#ifndef RSS_MODEL_H
#define RSS_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "mtb_kit_news.h"

#define RSS_SETTINGS_MAGIC  0x52535332UL    // "RSS2"
#define RSS_NVS_KEY         "rssCfg"
#define RSS_FEEDS           KIT_NEWS_FEEDS
#define RSS_URL_LEN         160
#define RSS_NAME_LEN        20
#define RSS_MUTED_LEN       64
#define RSS_RESULTS         12

struct Rss_Feed_t {
    char name[RSS_NAME_LEN];        /**< "BBC News" (ASCII). */
    char url[RSS_URL_LEN];
};

enum Rss_View_t : uint8_t { RSS_VIEW_CARD = 0, RSS_VIEW_LIST };

struct Rss_Settings_t {
    uint32_t magic;
    uint8_t feedCount;
    Rss_Feed_t feeds[RSS_FEEDS];
    uint8_t refreshMinutes;         /**< Each feed is read this often (10-180). */
    uint8_t storySeconds;           /**< Time on each story (5-60). */
    uint8_t view;                   /**< Rss_View_t */
    uint8_t autoCycle;              /**< Stories move on by themselves. */
    uint8_t summaries;              /**< The card turns to the story's summary halfway through its time. */
    uint8_t maxAgeHours;            /**< Stories older than this are not shown; 0: any age. */
    char muted[RSS_MUTED_LEN];      /**< Comma-separated words: stories whose headline has one are not shown. */
};

/** @brief A ready-made feed. */
struct Rss_Preset_t { const char* name; const char* url; const char* tags; };

/** @brief A search answer for the phone: a preset, or the address the user typed. */
struct Rss_Result_t { char name[RSS_NAME_LEN]; char url[RSS_URL_LEN]; char detail[40]; };

void rss_Settings_Default(Rss_Settings_t& settings);
void rss_Settings_Clamp(Rss_Settings_t& settings);

const Rss_Preset_t* rss_Presets(int& count);
/**
 * @brief The presets whose name or tags hold every word of @p query (any case); or, for an http(s) address, that
 *        address as the user's own feed, named after its site. Returns how many.
 */
int rss_Search(const char* query, Rss_Result_t* results, int max);
/** @brief A short name for a feed address: "www.example.co.uk/feed" -> "Example". */
void rss_Name_From_Url(char* dest, size_t size, const char* url);
/** @brief The address can be read: http(s)://, a host, no spaces or quotes, short enough. */
bool rss_Url_Valid(const char* url);

/** @brief The engine's view of the settings: one feed per entry, named in capitals, with a colour each. */
void rss_Kit_Config(const Rss_Settings_t& settings, Mtb_Kit_News_Config_t& config);

/**
 * @brief Reads a feed into @p stories (newest first is not needed: the engine sorts). Items without a usable title are
 *        skipped. @p channel gets the feed's own title when given room.
 * @return The number read, or -1 if the text is not a feed at all.
 */
int rss_Parse(const char* text, size_t length, uint8_t feed, const char* fallbackSource, Mtb_Kit_Story_t* stories, int max,
              char* channel = nullptr, size_t channelSize = 0);

/** @brief Unix time of an RSS date ("Thu, 06 Nov 2026 10:00:00 GMT", "+0100", "EST"...) or an ISO 8601 one. 0 if neither. */
uint32_t rss_Parse_Date(const char* text);

/** @brief Feed text to plain text: CDATA unwrapped, HTML tags removed, entities decoded (to UTF-8), spaces collapsed. */
void rss_Plain_Text(char* dest, size_t size, const char* text, size_t length);

/** @brief The story may be shown: not older than maxAgeHours, and no muted word in its headline. */
bool rss_Visible(const Mtb_Kit_Story_t& story, const Rss_Settings_t& settings, uint32_t now);

#endif
