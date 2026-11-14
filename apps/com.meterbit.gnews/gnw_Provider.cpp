#include "gnw_Provider.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define GNW_HOST            "https://gnews.io/api/v4"
#define GNW_PLAN_CHECK_SEC  (24UL * 3600)
#define GNW_PROBE_MAX       51          // Above Essential's 25 and Business's 50: what comes back says the plan.

static const Gnw_Category_t CATEGORIES[] = {
    {"general", "TOP STORIES", MTB_KIT_RGB(150, 30, 30)},   {"world", "WORLD", MTB_KIT_RGB(20, 90, 150)},
    {"nation", "NATION", MTB_KIT_RGB(0, 110, 80)},           {"business", "BUSINESS", MTB_KIT_RGB(150, 100, 0)},
    {"technology", "TECHNOLOGY", MTB_KIT_RGB(90, 40, 160)},  {"entertainment", "ENTERTAINMENT", MTB_KIT_RGB(170, 30, 110)},
    {"sports", "SPORTS", MTB_KIT_RGB(0, 120, 40)},           {"science", "SCIENCE", MTB_KIT_RGB(0, 110, 140)},
    {"health", "HEALTH", MTB_KIT_RGB(170, 60, 60)},
};
static const uint16_t TOPIC_COLORS[GNW_MAX_TOPICS] = {MTB_KIT_RGB(120, 80, 0), MTB_KIT_RGB(60, 70, 110), MTB_KIT_RGB(90, 60, 90)};

const Gnw_Category_t* gnw_Categories(size_t& count){ count = sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); return CATEGORIES; }

int gnw_Category_Index(const char* id){
    if (id == nullptr) return -1;
    for (size_t i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); i++) if (strcasecmp(CATEGORIES[i].id, id) == 0) return (int)i;
    return -1;
}

void gnw_Settings_Default(Gnw_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = GNW_SETTINGS_MAGIC;
    s.categories[s.categoryCount++] = 0;        // Top stories
    s.categories[s.categoryCount++] = 4;        // Technology
    s.categories[s.categoryCount++] = 6;        // Sports
    snprintf(s.language, sizeof(s.language), "en");
    s.floorMinutes = 15;
    s.pageSeconds = 12;
    s.autoCycle = 1;
}

bool gnw_Topic_Valid(const char* topic){
    const size_t n = topic ? strlen(topic) : 0;
    if (n < 2 || n >= GNW_TOPIC_LEN) return false;
    for (size_t i = 0; i < n; i++){
        const unsigned char c = (unsigned char)topic[i];
        if (!(isalnum(c) || strchr(" -'.&", c))) return false;
    }
    return topic[0] != ' ';
}

static bool gnw_Code_Valid(const char* code){
    return code[0] == '\0' || (strlen(code) == 2 && islower((unsigned char)code[0]) && islower((unsigned char)code[1]));
}

void gnw_Settings_Clamp(Gnw_Settings_t& s){
    size_t categoryTotal;
    gnw_Categories(categoryTotal);
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.categoryCount && i < GNW_MAX_CATEGORIES; i++){
        bool repeated = false;
        for (uint8_t k = 0; k < kept; k++) if (s.categories[k] == s.categories[i]) repeated = true;
        if (s.categories[i] < categoryTotal && !repeated) s.categories[kept++] = s.categories[i];
    }
    s.categoryCount = kept;
    kept = 0;
    for (uint8_t i = 0; i < s.topicCount && i < GNW_MAX_TOPICS; i++){
        s.topics[i][GNW_TOPIC_LEN - 1] = '\0';
        if (gnw_Topic_Valid(s.topics[i])) memmove(s.topics[kept++], s.topics[i], GNW_TOPIC_LEN);
    }
    for (uint8_t i = kept; i < GNW_MAX_TOPICS; i++) memset(s.topics[i], 0, GNW_TOPIC_LEN);
    s.topicCount = kept;
    if (s.categoryCount == 0 && s.topicCount == 0) s.categories[s.categoryCount++] = 0;
    s.language[3] = s.country[3] = '\0';
    if (!gnw_Code_Valid(s.language)) s.language[0] = '\0';
    if (!gnw_Code_Valid(s.country)) s.country[0] = '\0';
    if (s.floorMinutes < 5) s.floorMinutes = 5;
    if (s.floorMinutes > 180) s.floorMinutes = 180;
    if (s.pageSeconds < 5) s.pageSeconds = 5;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
    if (s.view > GNW_VIEW_LIST) s.view = GNW_VIEW_CARD;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
}

void gnw_Config(const Gnw_Settings_t& s, Mtb_Kit_News_Config_t& c){
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
    memcpy(c.country, s.country, sizeof(c.country));
    c.floorMinutes = s.floorMinutes;
    snprintf(c.apiKey, sizeof(c.apiKey), "%s", s.apiKey);
}

//##############################################################################################################
static bool gnw_Needs_Plan_Check(const Mtb_Kit_News_Plan_t& plan, uint32_t now){
    return !plan.known || now - plan.checked > GNW_PLAN_CHECK_SEC;
}

static int gnw_Asked_Max(const Mtb_Kit_News_Plan_t& plan, uint32_t now){
    if (gnw_Needs_Plan_Check(plan, now)) return GNW_PROBE_MAX;
    return plan.paid ? KIT_NEWS_PER_FEED : 10;
}

static bool gnw_Request(const Mtb_Kit_News_Config_t& c, uint8_t feed, const Mtb_Kit_News_Plan_t& plan, uint32_t now, char* url, size_t size, const char** headers){
    if (feed >= c.feedCount || c.apiKey[0] == '\0') return false;
    const Mtb_Kit_Feed_t& f = c.feeds[feed];
    size_t used;
    if (f.kind == 0){
        size_t count;
        const Gnw_Category_t* categories = gnw_Categories(count);
        used = snprintf(url, size, GNW_HOST "/top-headlines?category=%s", categories[f.category % count].id);
    } else {
        // Several words as one phrase: "real madrid", not real OR madrid.
        char phrase[48], encoded[96];
        snprintf(phrase, sizeof(phrase), strchr(f.query, ' ') ? "\"%s\"" : "%s", f.query);
        mtb_Kit_Url_Encode(encoded, sizeof(encoded), phrase);
        used = snprintf(url, size, GNW_HOST "/search?q=%s&in=title,description&sortby=publishedAt", encoded);
    }
    if (c.language[0]) used += snprintf(url + used, size - used, "&lang=%s", c.language);
    if (c.country[0]) used += snprintf(url + used, size - used, "&country=%s", c.country);
    snprintf(url + used, size - used, "&max=%d&truncate=content&nullable=description,image", gnw_Asked_Max(plan, now));
    headers[0] = "X-Api-Key";
    headers[1] = c.apiKey;
    headers[2] = nullptr;
    return true;
}

static void gnw_Set_Plan(Mtb_Kit_News_Plan_t& plan, const char* name, uint8_t paid, uint32_t daily, uint8_t max, uint8_t delay, uint32_t now){
    snprintf(plan.name, sizeof(plan.name), "%s", name);
    plan.known = 1;
    plan.paid = paid;
    plan.dailyLimit = daily;
    plan.maxPerRequest = max;
    plan.delayHours = delay;
    plan.checked = now;
}

static uint8_t gnw_Parse(uint8_t, int httpCode, JsonVariantConst doc, const Mtb_Kit_News_Config_t&, uint32_t now,
                         Mtb_Kit_Story_t* stories, uint8_t max, uint8_t& count, Mtb_Kit_News_Plan_t& plan){
    count = 0;
    if (httpCode != 200){
        // The message says which 403 it is.
        char text[160] = "";
        JsonVariantConst errors = doc["errors"];
        if (errors.is<JsonArrayConst>()) snprintf(text, sizeof(text), "%s", (const char*)(errors[0] | ""));
        else if (errors.is<JsonObjectConst>()) for (JsonPairConst p : errors.as<JsonObjectConst>()){ snprintf(text, sizeof(text), "%s", (const char*)(p.value() | "")); break; }
        if (httpCode == 401) return KIT_NEWS_KEY;
        if (httpCode == 403) return strstr(text, "quota") || strstr(text, "requests") ? KIT_NEWS_QUOTA : KIT_NEWS_KEY;
        if (httpCode == 429) return KIT_NEWS_RATE;
        if (httpCode == 400) return strstr(text, "API key") ? KIT_NEWS_KEY : KIT_NEWS_BAD_REQUEST;
        return httpCode >= 500 ? KIT_NEWS_SERVER : KIT_NEWS_NET;
    }
    JsonArrayConst articles = doc["articles"];
    if (articles.isNull()) return KIT_NEWS_PARSE;
    const int asked = gnw_Asked_Max(plan, now);
    const uint32_t total = doc["totalArticles"] | 0UL;
    const int returned = (int)articles.size();
    // The plan: the Free plan's answers carry "information"; a paid plan's size shows in a max=51 answer.
    if (!doc["information"].isNull()) gnw_Set_Plan(plan, "Free", 0, 100, 10, 12, now);
    else if (asked == GNW_PROBE_MAX){
        if (returned >= GNW_PROBE_MAX) gnw_Set_Plan(plan, "Enterprise", 1, 25000, 100, 0, now);
        else if (returned > 25) gnw_Set_Plan(plan, "Business", 1, 5000, 50, 0, now);
        else if ((int)total > returned) gnw_Set_Plan(plan, "Essential", 1, 1000, 25, 0, now);
        else if (!plan.known || !plan.paid) gnw_Set_Plan(plan, "Paid", 1, 1000, 25, 0, now);       // Too few stories to tell.
        else plan.checked = now;
    }
    for (JsonVariantConst a : articles){
        if (count >= max) break;
        const char* title = a["title"];
        if (title == nullptr || title[0] == '\0') continue;
        mtb_Kit_News_Fill_Story(stories[count], title, a["source"]["name"] | "", a["description"] | "", mtb_Kit_Parse_Iso_Time(a["publishedAt"]));
        count++;
    }
    return KIT_NEWS_OK;
}

const Mtb_Kit_News_Provider_t& gnw_Provider(void){
    static const Mtb_Kit_News_Provider_t provider = {
        "GNews", "Pixlpal-GNews/2.0 (ESP32 LED display)", "/news/gnews.bin",
        100, 6, 0, 160 * 1024, gnw_Request, gnw_Parse, nullptr, false,
    };
    return provider;
}
