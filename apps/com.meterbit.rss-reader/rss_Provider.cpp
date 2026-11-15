#include "rss_Provider.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RSS_USER_AGENT  "Pixlpal-RSS/2.0 (ESP32 LED display)"
#define RSS_MAX_BODY    (320 * 1024)        // The largest feeds (NASA's) are about 260 KB.

static portMUX_TYPE feedLock = portMUX_INITIALIZER_UNLOCKED;
static Rss_Feed_t feeds[RSS_FEEDS];
static uint8_t feedCount = 0;

void rss_Provider_Set_Feeds(const Rss_Settings_t& s){
    portENTER_CRITICAL(&feedLock);
    memcpy(feeds, s.feeds, sizeof(feeds));
    feedCount = s.feedCount;
    portEXIT_CRITICAL(&feedLock);
}

static bool rss_Feed_Of(uint8_t feed, Rss_Feed_t& out){
    portENTER_CRITICAL(&feedLock);
    const bool ok = feed < feedCount;
    if (ok) out = feeds[feed];
    portEXIT_CRITICAL(&feedLock);
    return ok;
}

static bool rss_Request(const Mtb_Kit_News_Config_t&, uint8_t feed, const Mtb_Kit_News_Plan_t&, uint32_t, char* url, size_t urlSize, const char** headers){
    Rss_Feed_t f;
    if (!rss_Feed_Of(feed, f)) return false;
    snprintf(url, urlSize, "%s", f.url);
    static const char* const ACCEPT[] = {"Accept", "application/rss+xml, application/atom+xml, application/xml;q=0.9, text/xml;q=0.8, */*;q=0.5", nullptr};
    for (int i = 0; ACCEPT[i]; i++) headers[i] = ACCEPT[i];
    return true;
}

static uint8_t rss_Read(uint8_t feed, int httpCode, const char* body, size_t length, bool, const Mtb_Kit_News_Config_t&, uint32_t,
                        Mtb_Kit_Story_t* stories, uint8_t max, uint8_t& count, Mtb_Kit_News_Plan_t& plan){
    count = 0;
    // No plans here: every feed is free.
    if (!plan.known){ plan.known = 1; snprintf(plan.name, sizeof(plan.name), "Free feeds"); }
    if (httpCode == 429) return KIT_NEWS_RATE;
    if (httpCode >= 500) return KIT_NEWS_SERVER;
    if (httpCode >= 300) return KIT_NEWS_BAD_REQUEST;      // Gone, moved too often, or refused: retried in 12 hours.
    if (httpCode != 200 || body == nullptr) return KIT_NEWS_NET;
    Rss_Feed_t f;
    if (!rss_Feed_Of(feed, f)) return KIT_NEWS_BAD_REQUEST;
    const int n = rss_Parse(body, length, feed, f.name, stories, max);
    if (n < 0) return KIT_NEWS_PARSE;                   // A web page, not a feed.
    count = (uint8_t)n;
    return KIT_NEWS_OK;
}

const Mtb_Kit_News_Provider_t& rss_Provider(void){
    static const Mtb_Kit_News_Provider_t provider = {
        "RSS", RSS_USER_AGENT, "/news/rss.bin",
        5000, 0, 0, RSS_MAX_BODY, rss_Request, nullptr, rss_Read, true,
    };
    return provider;
}
