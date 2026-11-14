#include "mtb_kit_news.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

uint32_t mtb_Kit_News_Hash(const char* text, uint32_t hash){
    for (const unsigned char* c = (const unsigned char*)(text ? text : ""); *c; c++){ hash ^= (uint8_t)tolower(*c); hash *= 16777619u; }
    return hash;
}

uint32_t mtb_Kit_News_Config_Hash(const Mtb_Kit_News_Config_t& config){
    uint32_t hash = mtb_Kit_News_Hash(config.language);
    hash = mtb_Kit_News_Hash(config.country, hash ^ 0x9E37u);
    for (uint8_t i = 0; i < config.feedCount && i < KIT_NEWS_FEEDS; i++){
        const Mtb_Kit_Feed_t& f = config.feeds[i];
        char key[64];
        snprintf(key, sizeof(key), "%u/%u/%s", f.kind, f.category, f.query);
        hash = mtb_Kit_News_Hash(key, hash ^ (i + 1));
    }
    return hash;
}

uint32_t mtb_Kit_News_Interval(uint32_t dailyLimit, uint32_t usedToday, uint8_t feedCount, uint32_t now, uint32_t floorSec, uint16_t reserve){
    if (feedCount == 0) return 0;
    const uint32_t budget = dailyLimit > reserve ? dailyLimit - reserve : 0;
    if (usedToday >= budget) return 0;
    const uint32_t left = budget - usedToday;
    const uint32_t untilMidnight = 86400 - now % 86400;
    // Each feed gets left / feedCount requests over what is left of the day.
    uint32_t interval = (uint32_t)((uint64_t)untilMidnight * feedCount / left);
    if (interval < floorSec) interval = floorSec;
    if (interval > 6UL * 3600) interval = 6UL * 3600;
    return interval;
}

void mtb_Kit_News_Fill_Story(Mtb_Kit_Story_t& story, const char* title, const char* source, const char* summary, uint32_t published){
    memset(&story, 0, sizeof(story));
    mtb_Kit_To_Ascii(story.title, sizeof(story.title), title);
    mtb_Kit_To_Ascii(story.source, sizeof(story.source), source);
    mtb_Kit_To_Ascii(story.summary, sizeof(story.summary), summary);
    // "Headline - CNN": the source is shown on its own.
    const size_t n = strlen(story.title), s = strlen(story.source);
    if (s > 0 && n > s + 3 && strcasecmp(story.title + n - s, story.source) == 0 && story.title[n - s - 2] == '-' && story.title[n - s - 3] == ' '){
        story.title[n - s - 3] = '\0';
    }
    // A summary that only repeats the headline says nothing.
    if (strncasecmp(story.summary, story.title, strlen(story.title)) == 0 && strlen(story.summary) <= strlen(story.title) + 3) story.summary[0] = '\0';
    story.published = published;
    story.hash = mtb_Kit_News_Hash(story.title);
}

void mtb_Kit_News_Merge(Mtb_Kit_News_Data_t& data, uint8_t feed, const Mtb_Kit_Story_t* incoming, uint8_t count){
    if (count > KIT_NEWS_PER_FEED) count = KIT_NEWS_PER_FEED;
    // The feed's stories before: for "fresh".
    uint32_t before[KIT_NEWS_PER_FEED];
    int beforeCount = 0;
    for (int i = 0; i < data.storyCount; i++) if (data.stories[i].feed == feed && beforeCount < KIT_NEWS_PER_FEED) before[beforeCount++] = data.stories[i].hash;
    // Remove them.
    int kept = 0;
    for (int i = 0; i < data.storyCount; i++) if (data.stories[i].feed != feed) data.stories[kept++] = data.stories[i];
    // Add the new ones, newest first, without repeats, where there is room. (Off the stack: 5 KB.)
    Mtb_Kit_Story_t* sorted = (Mtb_Kit_Story_t*)malloc(sizeof(Mtb_Kit_Story_t) * KIT_NEWS_PER_FEED);
    if (sorted == nullptr){ data.storyCount = (uint8_t)kept; return; }
    int n = 0;
    for (uint8_t i = 0; i < count; i++){
        if (incoming[i].title[0] == '\0') continue;
        bool repeated = false;
        for (int k = 0; k < n; k++) if (sorted[k].hash == incoming[i].hash) repeated = true;
        if (repeated) continue;
        int at = n++;
        while (at > 0 && sorted[at - 1].published < incoming[i].published){ sorted[at] = sorted[at - 1]; at--; }
        sorted[at] = incoming[i];
    }
    for (int i = 0; i < n && kept < KIT_NEWS_STORIES; i++){
        Mtb_Kit_Story_t story = sorted[i];
        story.feed = feed;
        bool seen = false;
        for (int k = 0; k < beforeCount; k++) if (before[k] == story.hash) seen = true;
        story.fresh = beforeCount > 0 && !seen;
        data.stories[kept++] = story;
    }
    free(sorted);
    data.storyCount = (uint8_t)kept;
}

int mtb_Kit_News_Order(const Mtb_Kit_News_Data_t& data, uint8_t* order, int max){
    int n = 0;
    for (uint8_t feed = 0; feed < KIT_NEWS_FEEDS; feed++){
        for (int i = 0; i < data.storyCount && n < max; i++) if (data.stories[i].feed == feed) order[n++] = (uint8_t)i;
    }
    return n;
}
