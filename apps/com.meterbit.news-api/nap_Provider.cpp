#include "nap_Provider.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NAP_HOST            "https://newsapi.org/v2"
#define NAP_PROBE_EVERY     (24UL * 3600)
#define NAP_PROBE_DAYS_BACK 40          // Beyond the Developer plan's month.

static const Nap_Category_t CATEGORIES[] = {
    {"general", "US TOP STORIES", MTB_KIT_RGB(150, 30, 30)}, {"business", "US BUSINESS", MTB_KIT_RGB(150, 100, 0)},
    {"technology", "US TECHNOLOGY", MTB_KIT_RGB(90, 40, 160)}, {"science", "US SCIENCE", MTB_KIT_RGB(0, 110, 140)},
    {"health", "US HEALTH", MTB_KIT_RGB(170, 60, 60)}, {"sports", "US SPORTS", MTB_KIT_RGB(0, 120, 40)},
    {"entertainment", "US ENTERTAINMENT", MTB_KIT_RGB(170, 30, 110)},
};
static const uint16_t TOPIC_COLORS[NAP_MAX_TOPICS] = {MTB_KIT_RGB(120, 80, 0), MTB_KIT_RGB(60, 70, 110), MTB_KIT_RGB(90, 60, 90)};

const Nap_Category_t* nap_Categories(size_t& count){ count = sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); return CATEGORIES; }

int nap_Category_Index(const char* id){
    if (id == nullptr) return -1;
    for (size_t i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); i++) if (strcasecmp(CATEGORIES[i].id, id) == 0) return (int)i;
    return -1;
}

void nap_Settings_Default(Nap_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = NAP_SETTINGS_MAGIC;
    s.categories[s.categoryCount++] = 0;
    s.categories[s.categoryCount++] = 2;
    snprintf(s.topics[s.topicCount++], NAP_TOPIC_LEN, "%s", "Nigeria");
    snprintf(s.language, sizeof(s.language), "en");
    s.floorMinutes = 15;
    s.pageSeconds = 12;
    s.autoCycle = 1;
}

bool nap_Topic_Valid(const char* topic){
    const size_t n = topic ? strlen(topic) : 0;
    if (n < 2 || n >= NAP_TOPIC_LEN) return false;
    for (size_t i = 0; i < n; i++){
        const unsigned char c = (unsigned char)topic[i];
        if (!(isalnum(c) || strchr(" -'.&", c))) return false;
    }
    return topic[0] != ' ';
}

void nap_Settings_Clamp(Nap_Settings_t& s){
    size_t total;
    nap_Categories(total);
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.categoryCount && i < NAP_MAX_CATEGORIES; i++){
        bool repeated = false;
        for (uint8_t k = 0; k < kept; k++) if (s.categories[k] == s.categories[i]) repeated = true;
        if (s.categories[i] < total && !repeated) s.categories[kept++] = s.categories[i];
    }
    s.categoryCount = kept;
    kept = 0;
    for (uint8_t i = 0; i < s.topicCount && i < NAP_MAX_TOPICS; i++){
        s.topics[i][NAP_TOPIC_LEN - 1] = '\0';
        if (nap_Topic_Valid(s.topics[i])) memmove(s.topics[kept++], s.topics[i], NAP_TOPIC_LEN);
    }
    for (uint8_t i = kept; i < NAP_MAX_TOPICS; i++) memset(s.topics[i], 0, NAP_TOPIC_LEN);
    s.topicCount = kept;
    if (s.categoryCount == 0 && s.topicCount == 0) s.categories[s.categoryCount++] = 0;
    s.language[3] = '\0';
    if (!(s.language[0] == '\0' || (strlen(s.language) == 2 && islower((unsigned char)s.language[0]) && islower((unsigned char)s.language[1])))) s.language[0] = '\0';
    if (s.floorMinutes < 5) s.floorMinutes = 5;
    if (s.floorMinutes > 180) s.floorMinutes = 180;
    if (s.pageSeconds < 5) s.pageSeconds = 5;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
    if (s.view > NAP_VIEW_LIST) s.view = NAP_VIEW_CARD;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
}

void nap_Config(const Nap_Settings_t& s, Mtb_Kit_News_Config_t& c){
    memset(&c, 0, sizeof(c));
    for (uint8_t i = 0; i < s.categoryCount && c.feedCount < KIT_NEWS_FEEDS; i++){
        Mtb_Kit_Feed_t& f = c.feeds[c.feedCount++];
        f.kind = 0;
        f.category = s.categories[i];
        f.color = CATEGORIES[s.categories[i]].color;
        snprintf(f.name, sizeof(f.name), "%s", CATEGORIES[s.categories[i]].name);
    }
    for (uint8_t i = 0; i < s.topicCount && c.feedCount < KIT_NEWS_FEEDS; i++){
        Mtb_Kit_Feed_t& f = c.feeds[c.feedCount++];
        f.kind = 1;
        f.color = TOPIC_COLORS[i];
        snprintf(f.query, sizeof(f.query), "%s", s.topics[i]);
        snprintf(f.name, sizeof(f.name), "%s", s.topics[i]);
        mtb_Kit_Upper(f.name);
    }
    memcpy(c.language, s.language, sizeof(c.language));
    c.floorMinutes = s.floorMinutes;
    snprintf(c.apiKey, sizeof(c.apiKey), "%s", s.apiKey);
}

//##############################################################################################################
static bool nap_Request(const Mtb_Kit_News_Config_t& c, uint8_t feed, const Mtb_Kit_News_Plan_t&, uint32_t now, char* url, size_t size, const char** headers){
    if (c.apiKey[0] == '\0') return false;
    if (feed == KIT_NEWS_PROBE){
        const time_t back = (time_t)(now - NAP_PROBE_DAYS_BACK * 86400UL);
        struct tm tm;
        gmtime_r(&back, &tm);
        snprintf(url, size, NAP_HOST "/everything?q=news&from=%04d-%02d-%02d&to=%04d-%02d-%02d&pageSize=1",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    } else {
        if (feed >= c.feedCount) return false;
        const Mtb_Kit_Feed_t& f = c.feeds[feed];
        if (f.kind == 0){
            size_t count;
            const Nap_Category_t* categories = nap_Categories(count);
            snprintf(url, size, NAP_HOST "/top-headlines?country=us&category=%s&pageSize=%d", categories[f.category % count].id, KIT_NEWS_PER_FEED);
        } else {
            char phrase[48], encoded[96];
            snprintf(phrase, sizeof(phrase), strchr(f.query, ' ') ? "\"%s\"" : "%s", f.query);
            mtb_Kit_Url_Encode(encoded, sizeof(encoded), phrase);
            const size_t used = snprintf(url, size, NAP_HOST "/everything?q=%s&searchIn=title,description&sortBy=publishedAt&pageSize=%d", encoded, KIT_NEWS_PER_FEED);
            if (c.language[0]) snprintf(url + used, size - used, "&language=%s", c.language);
        }
    }
    headers[0] = "X-Api-Key";
    headers[1] = c.apiKey;
    headers[2] = nullptr;
    return true;
}

static void nap_Set_Plan(Mtb_Kit_News_Plan_t& plan, const char* name, uint8_t paid, uint32_t daily, uint8_t delay, uint32_t now){
    snprintf(plan.name, sizeof(plan.name), "%s", name);
    plan.known = 1;
    plan.paid = paid;
    plan.dailyLimit = daily;
    plan.maxPerRequest = 100;
    plan.delayHours = delay;
    plan.checked = now;
}

static uint8_t nap_Parse(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_News_Config_t&, uint32_t now,
                         Mtb_Kit_Story_t* stories, uint8_t max, uint8_t& count, Mtb_Kit_News_Plan_t& plan){
    count = 0;
    const char* status = doc["status"] | "";
    const char* code = doc["code"] | "";
    const char* message = doc["message"] | "";
    if (feed == KIT_NEWS_PROBE){
        // A search 40 days back: the Developer plan refuses it, a paid plan answers.
        if (httpCode == 200 && strcmp(status, "ok") == 0){ nap_Set_Plan(plan, "Business", 1, 8000, 0, now); return KIT_NEWS_OK; }
        if (strstr(message, "too far in the past") || strstr(message, "plan") || httpCode == 426 || strcmp(code, "parameterInvalid") == 0){
            nap_Set_Plan(plan, "Developer", 0, 100, 24, now);
            return KIT_NEWS_OK;
        }
    }
    if (httpCode != 200 || strcmp(status, "ok") != 0){
        if (strcmp(code, "apiKeyExhausted") == 0) return KIT_NEWS_QUOTA;
        if (strcmp(code, "rateLimited") == 0 || httpCode == 429) return KIT_NEWS_RATE;
        if (strncmp(code, "apiKey", 6) == 0 || httpCode == 401) return KIT_NEWS_KEY;
        if (httpCode == 426) return KIT_NEWS_PLAN;
        if (httpCode == 400) return KIT_NEWS_BAD_REQUEST;
        return httpCode >= 500 ? KIT_NEWS_SERVER : httpCode > 0 ? KIT_NEWS_PARSE : KIT_NEWS_NET;
    }
    if (feed == KIT_NEWS_PROBE) return KIT_NEWS_OK;
    JsonArrayConst articles = doc["articles"];
    if (articles.isNull()) return KIT_NEWS_PARSE;
    for (JsonVariantConst a : articles){
        if (count >= max) break;
        const char* title = a["title"];
        // Withdrawn stories come back as "[Removed]".
        if (title == nullptr || title[0] == '\0' || strcmp(title, "[Removed]") == 0) continue;
        mtb_Kit_News_Fill_Story(stories[count], title, a["source"]["name"] | "", a["description"] | "", mtb_Kit_Parse_Iso_Time(a["publishedAt"]));
        count++;
    }
    return KIT_NEWS_OK;
}

const Mtb_Kit_News_Provider_t& nap_Provider(void){
    static const Mtb_Kit_News_Provider_t provider = {
        "NewsAPI", "Pixlpal-NewsAPI/2.0 (ESP32 LED display)", "/news/newsapi.bin",
        100, 6, NAP_PROBE_EVERY, 160 * 1024, nap_Request, nap_Parse, nullptr, false,
    };
    return provider;
}
