#include "rss_Model.h"
#include "mtb_kit_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Each feed read and checked on 6 November 2026. CNN's old feeds (rss.cnn.com) no longer answer, Reuters closed its
// feeds in 2020, and Google News is left out: its terms do not allow it to be shown this way.
static const Rss_Preset_t PRESETS[] = {
    {"BBC News", "https://feeds.bbci.co.uk/news/rss.xml", "top uk world headlines"},
    {"BBC World", "https://feeds.bbci.co.uk/news/world/rss.xml", "international uk"},
    {"BBC Technology", "https://feeds.bbci.co.uk/news/technology/rss.xml", "tech science uk"},
    {"BBC Sport", "https://feeds.bbci.co.uk/sport/rss.xml", "sports uk"},
    {"BBC Football", "https://feeds.bbci.co.uk/sport/football/rss.xml", "soccer premier league sport uk"},
    {"Al Jazeera", "https://www.aljazeera.com/xml/rss/all.xml", "world international middle east africa"},
    {"NPR News", "https://feeds.npr.org/1001/rss.xml", "top us america headlines"},
    {"Guardian World", "https://www.theguardian.com/world/rss", "international uk"},
    {"New York Times", "https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml", "nyt top us america"},
    {"Sky News World", "https://feeds.skynews.com/feeds/rss/world.xml", "international uk"},
    {"ESPN", "https://www.espn.com/espn/rss/news", "sports us america"},
    {"TechCrunch", "https://techcrunch.com/feed/", "tech startups business"},
    {"Ars Technica", "https://feeds.arstechnica.com/arstechnica/index", "tech science"},
    {"The Verge", "https://www.theverge.com/rss/index.xml", "tech gadgets"},
    {"Wired", "https://www.wired.com/feed/rss", "tech science culture"},
    {"Engadget", "https://www.engadget.com/rss.xml", "tech gadgets"},
    {"Hacker News", "https://hnrss.org/frontpage", "tech programming startups"},
    {"CNBC", "https://www.cnbc.com/id/100003114/device/rss/rss.html", "business finance markets top us"},
    {"Bloomberg Markets", "https://feeds.bloomberg.com/markets/news.rss", "business finance markets"},
    {"ScienceDaily", "https://www.sciencedaily.com/rss/all.xml", "science research health"},
    {"NASA", "https://www.nasa.gov/news-release/feed/", "space science"},
    {"Punch", "https://punchng.com/feed/", "nigeria africa"},
    {"Premium Times", "https://www.premiumtimesng.com/feed", "nigeria africa"},
    {"Vanguard", "https://www.vanguardngr.com/feed/", "nigeria africa"},
};

static const uint16_t FEED_COLORS[RSS_FEEDS] = {
    MTB_KIT_RGB(170, 20, 30), MTB_KIT_RGB(20, 90, 170), MTB_KIT_RGB(20, 130, 70), MTB_KIT_RGB(150, 70, 10),
    MTB_KIT_RGB(110, 40, 150), MTB_KIT_RGB(10, 120, 130), MTB_KIT_RGB(120, 110, 20),
};

const Rss_Preset_t* rss_Presets(int& count){
    count = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
    return PRESETS;
}

static void rss_Copy(char* dest, size_t size, const char* text){
    if (size == 0) return;
    snprintf(dest, size, "%s", text ? text : "");
}

//##############################################################################################################
void rss_Settings_Default(Rss_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = RSS_SETTINGS_MAGIC;
    static const int DEFAULTS[] = {0, 5, 6, 2};
    for (int i : DEFAULTS){
        rss_Copy(s.feeds[s.feedCount].name, RSS_NAME_LEN, PRESETS[i].name);
        rss_Copy(s.feeds[s.feedCount].url, RSS_URL_LEN, PRESETS[i].url);
        s.feedCount++;
    }
    s.refreshMinutes = 15;
    s.storySeconds = 12;
    s.view = RSS_VIEW_CARD;
    s.autoCycle = 1;
    s.summaries = 1;
    s.maxAgeHours = 0;
}

void rss_Settings_Clamp(Rss_Settings_t& s){
    if (s.feedCount > RSS_FEEDS) s.feedCount = RSS_FEEDS;
    // Feeds that cannot be read are dropped.
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s.feedCount; i++){
        Rss_Feed_t f = s.feeds[i];
        f.name[RSS_NAME_LEN - 1] = '\0';
        f.url[RSS_URL_LEN - 1] = '\0';
        if (!rss_Url_Valid(f.url)) continue;
        if (f.name[0] == '\0') rss_Name_From_Url(f.name, sizeof(f.name), f.url);
        s.feeds[kept++] = f;
    }
    for (uint8_t i = kept; i < RSS_FEEDS; i++) memset(&s.feeds[i], 0, sizeof(s.feeds[i]));
    s.feedCount = kept;
    if (s.refreshMinutes < 10) s.refreshMinutes = 10;
    if (s.refreshMinutes > 180) s.refreshMinutes = 180;
    if (s.storySeconds < 5) s.storySeconds = 5;
    if (s.storySeconds > 60) s.storySeconds = 60;
    if (s.view > RSS_VIEW_LIST) s.view = RSS_VIEW_CARD;
    s.autoCycle = s.autoCycle ? 1 : 0;
    s.summaries = s.summaries ? 1 : 0;
    if (s.maxAgeHours > 168) s.maxAgeHours = 168;
    s.muted[RSS_MUTED_LEN - 1] = '\0';
}

bool rss_Url_Valid(const char* url){
    if (url == nullptr) return false;
    const size_t n = strlen(url);
    const char* host = nullptr;
    if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else if (strncmp(url, "http://", 7) == 0) host = url + 7;
    if (host == nullptr || n >= RSS_URL_LEN) return false;
    for (size_t i = 0; i < n; i++){
        const unsigned char c = (unsigned char)url[i];
        if (c <= ' ' || c >= 127 || c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\') return false;
    }
    const size_t hostLength = strcspn(host, "/?#:");
    if (hostLength < 3) return false;
    const char* dot = (const char*)memchr(host, '.', hostLength);
    return dot != nullptr && dot != host && dot != host + hostLength - 1;
}

void rss_Name_From_Url(char* dest, size_t size, const char* url){
    const char* host = strstr(url ? url : "", "://");
    host = host ? host + 3 : (url ? url : "");
    static const char* const PREFIXES[] = {"www.", "feeds.", "feed.", "rss.", "news.", "m."};
    for (const char* p : PREFIXES){
        const size_t n = strlen(p);
        if (strncasecmp(host, p, n) == 0 && strchr(host + n, '.')){ host += n; break; }
    }
    size_t n = strcspn(host, "./:?#");
    if (n == 0){ rss_Copy(dest, size, "My Feed"); return; }
    if (n >= size) n = size - 1;
    for (size_t i = 0; i < n; i++) dest[i] = i == 0 ? (char)toupper((unsigned char)host[i]) : (char)tolower((unsigned char)host[i]);
    dest[n] = '\0';
}

// Case-blind search for a word.
static const char* rss_Find(const char* haystack, const char* needle, size_t needleLength){
    if (needleLength == 0) return nullptr;
    for (const char* h = haystack; *h; h++) if (strncasecmp(h, needle, needleLength) == 0) return h;
    return nullptr;
}

int rss_Search(const char* query, Rss_Result_t* results, int max){
    if (query == nullptr || max <= 0) return 0;
    while (*query == ' ') query++;
    int count = 0;
    if (strncasecmp(query, "http://", 7) == 0 || strncasecmp(query, "https://", 8) == 0){
        char url[RSS_URL_LEN];
        rss_Copy(url, sizeof(url), query);
        for (size_t n = strlen(url); n && url[n - 1] == ' '; n--) url[n - 1] = '\0';
        if (!rss_Url_Valid(url)) return 0;
        // A preset at that address keeps its name.
        int presets;
        rss_Presets(presets);
        for (int i = 0; i < presets; i++){
            if (strcmp(PRESETS[i].url, url) == 0){
                rss_Copy(results[0].name, RSS_NAME_LEN, PRESETS[i].name);
                rss_Copy(results[0].url, RSS_URL_LEN, url);
                rss_Copy(results[0].detail, sizeof(results[0].detail), "Ready-made feed");
                return 1;
            }
        }
        rss_Name_From_Url(results[0].name, RSS_NAME_LEN, url);
        rss_Copy(results[0].url, RSS_URL_LEN, url);
        rss_Copy(results[0].detail, sizeof(results[0].detail), "Your own feed: checked when first read");
        return 1;
    }
    int presets;
    rss_Presets(presets);
    for (int i = 0; i < presets && count < max; i++){
        // Every word of the query, in the name or the tags.
        bool all = true, any = false;
        const char* w = query;
        while (*w && all){
            while (*w == ' ' || *w == ',') w++;
            const size_t n = strcspn(w, " ,");
            if (n == 0) break;
            any = true;
            all = rss_Find(PRESETS[i].name, w, n) || rss_Find(PRESETS[i].tags, w, n);
            w += n;
        }
        if (!any || !all) continue;
        rss_Copy(results[count].name, RSS_NAME_LEN, PRESETS[i].name);
        rss_Copy(results[count].url, RSS_URL_LEN, PRESETS[i].url);
        const char* host = strstr(PRESETS[i].url, "://") + 3;
        snprintf(results[count].detail, sizeof(results[count].detail), "%.*s", (int)strcspn(host, "/"), host);
        count++;
    }
    return count;
}

void rss_Kit_Config(const Rss_Settings_t& s, Mtb_Kit_News_Config_t& config){
    memset(&config, 0, sizeof(config));
    config.feedCount = s.feedCount;
    for (uint8_t i = 0; i < s.feedCount; i++){
        Mtb_Kit_Feed_t& f = config.feeds[i];
        f.kind = 0;
        f.color = FEED_COLORS[i % RSS_FEEDS];
        mtb_Kit_To_Ascii(f.name, sizeof(f.name), s.feeds[i].name);
        mtb_Kit_Upper(f.name);
        // The address is the app's; its hash makes a new address a new feed to the engine.
        snprintf(f.query, sizeof(f.query), "u%08x", (unsigned)mtb_Kit_News_Hash(s.feeds[i].url));
    }
    config.floorMinutes = s.refreshMinutes;
}

//##############################################################################################################
// Feed text to plain text. Two stages: the XML layer (CDATA, entities), then the HTML that descriptions carry (tags,
// entities again).
struct Rss_Text_t {
    char* dest;
    size_t size, used;
    bool space, inTag;
    char entity[12];
    int entityLength;              // -1: not in an entity
};

static void rss_Emit_Byte(Rss_Text_t& t, char c){
    if (t.used + 1 >= t.size) return;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r'){
        if (t.used > 0) t.space = true;
        return;
    }
    if (t.space && t.used + 2 < t.size){ t.dest[t.used++] = ' '; }
    t.space = false;
    t.dest[t.used++] = c;
}

static void rss_Emit_Code(Rss_Text_t& t, unsigned long code){
    // As UTF-8; the panel's ASCII comes later.
    if (code == 0xA0) code = ' ';
    if (code < 0x80) rss_Emit_Byte(t, (char)code);
    else if (code < 0x800){ rss_Emit_Byte(t, (char)(0xC0 | (code >> 6))); rss_Emit_Byte(t, (char)(0x80 | (code & 0x3F))); }
    else if (code < 0x10000){
        rss_Emit_Byte(t, (char)(0xE0 | (code >> 12))); rss_Emit_Byte(t, (char)(0x80 | ((code >> 6) & 0x3F))); rss_Emit_Byte(t, (char)(0x80 | (code & 0x3F)));
    } else if (code < 0x110000){
        rss_Emit_Byte(t, (char)(0xF0 | (code >> 18))); rss_Emit_Byte(t, (char)(0x80 | ((code >> 12) & 0x3F)));
        rss_Emit_Byte(t, (char)(0x80 | ((code >> 6) & 0x3F))); rss_Emit_Byte(t, (char)(0x80 | (code & 0x3F)));
    }
}

// The code point of an entity's name ("amp", "#8217", "#x2019"); 0 if unknown.
static unsigned long rss_Entity(const char* name){
    if (name[0] == '#'){
        char* end = nullptr;
        const unsigned long code = (name[1] == 'x' || name[1] == 'X') ? strtoul(name + 2, &end, 16) : strtoul(name + 1, &end, 10);
        return (end && *end == '\0') ? code : 0;
    }
    static const struct { const char* name; uint16_t code; } NAMED[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}, {"rsquo", 0x2019}, {"lsquo", 0x2018},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"hellip", 0x2026}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"copy", 0xA9},
        {"reg", 0xAE}, {"trade", 0x2122}, {"euro", 0x20AC}, {"pound", 0xA3}, {"eacute", 0xE9}, {"egrave", 0xE8}, {"aacute", 0xE1},
        {"agrave", 0xE0}, {"oacute", 0xF3}, {"iacute", 0xED}, {"uacute", 0xFA}, {"ntilde", 0xF1}, {"ccedil", 0xE7}, {"ouml", 0xF6},
        {"uuml", 0xFC}, {"auml", 0xE4}, {"szlig", 0xDF}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"middot", 0xB7}, {"bull", 0x2022},
    };
    for (const auto& e : NAMED) if (strcmp(name, e.name) == 0) return e.code;
    return 0;
}

// Stage two: HTML.
static void rss_Html(Rss_Text_t& t, char c){
    if (t.entityLength >= 0){
        if (c == ';'){
            t.entity[t.entityLength] = '\0';
            t.entityLength = -1;
            const unsigned long code = rss_Entity(t.entity);
            if (code) rss_Emit_Code(t, code);
            return;
        }
        if (t.entityLength < (int)sizeof(t.entity) - 1 && (isalnum((unsigned char)c) || c == '#')){ t.entity[t.entityLength++] = c; return; }
        // Not an entity after all: what was held is text.
        rss_Emit_Byte(t, '&');
        for (int i = 0; i < t.entityLength; i++) rss_Emit_Byte(t, t.entity[i]);
        t.entityLength = -1;
    }
    if (t.inTag){ if (c == '>'){ t.inTag = false; t.space = t.used > 0; } return; }
    if (c == '<'){ t.inTag = true; return; }
    if (c == '&'){ t.entityLength = 0; return; }
    rss_Emit_Byte(t, c);
}

void rss_Plain_Text(char* dest, size_t size, const char* text, size_t length){
    if (size == 0) return;
    Rss_Text_t t = {dest, size, 0, false, false, {0}, -1};
    const char* p = text;
    const char* end = text + length;
    while (p < end && t.used + 1 < t.size){
        if (*p == '<' && end - p >= 9 && memcmp(p, "<![CDATA[", 9) == 0){
            // CDATA: its text is HTML as it stands.
            const char* q = p + 9;
            while (q < end && !(end - q >= 3 && memcmp(q, "]]>", 3) == 0)) rss_Html(t, *q++);
            p = q + 3;
            continue;
        }
        if (*p == '&'){
            // An XML entity, decoded, and the result read as HTML ("&lt;p&gt;" is a tag).
            const char* semi = (const char*)memchr(p, ';', (size_t)(end - p) < 12 ? (size_t)(end - p) : 12);
            if (semi){
                char name[12];
                const size_t n = (size_t)(semi - p - 1);
                memcpy(name, p + 1, n);
                name[n] = '\0';
                const unsigned long code = rss_Entity(name);
                if (code){
                    if (code < 0x80) rss_Html(t, (char)code);
                    else rss_Emit_Code(t, code);
                    p = semi + 1;
                    continue;
                }
            }
        }
        rss_Html(t, *p++);
    }
    while (t.used && t.dest[t.used - 1] == ' ') t.used--;
    t.dest[t.used < size ? t.used : size - 1] = '\0';
}

//##############################################################################################################
uint32_t rss_Parse_Date(const char* text){
    if (text == nullptr) return 0;
    while (*text == ' ' || *text == '\n' || *text == '\t' || *text == '\r') text++;
    if (isdigit((unsigned char)text[0]) && isdigit((unsigned char)text[3]) && text[4] == '-') return mtb_Kit_Parse_Iso_Time(text);
    // RFC 822: [Day,] DD Mon YYYY HH:MM[:SS] zone
    const char* p = text;
    const char* comma = strchr(p, ',');
    if (comma && comma - p < 12) p = comma + 1;
    int day = 0, year = 0, h = 0, m = 0, s = 0;
    char month[8] = "", zone[8] = "";
    int fields = sscanf(p, " %d %7s %d %d:%d:%d %7s", &day, month, &year, &h, &m, &s, zone);
    if (fields < 6){
        // No seconds.
        s = 0;
        zone[0] = '\0';
        fields = sscanf(p, " %d %7s %d %d:%d %7s", &day, month, &year, &h, &m, zone);
        if (fields < 5) return 0;
    }
    static const char* const MONTHS[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    int mo = 0;
    for (int i = 0; i < 12; i++) if (strncasecmp(month, MONTHS[i], 3) == 0) mo = i + 1;
    if (year < 100) year += 2000;
    if (mo == 0 || day < 1 || day > 31 || year < 1970 || h > 23 || m > 59 || s > 60) return 0;
    int32_t offset = 0;
    if (zone[0] == '+' || zone[0] == '-'){
        int hh = 0, mm = 0;
        if (strchr(zone, ':')) sscanf(zone + 1, "%2d:%2d", &hh, &mm);
        else { const int v = atoi(zone + 1); hh = v / 100; mm = v % 100; }
        offset = (hh * 3600 + mm * 60) * (zone[0] == '+' ? 1 : -1);
    } else {
        static const struct { const char* name; int8_t hours; } ZONES[] = {
            {"EST", -5}, {"EDT", -4}, {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7},
            {"BST", 1}, {"CET", 1}, {"CEST", 2}, {"WAT", 1},
        };
        for (const auto& z : ZONES) if (strcasecmp(zone, z.name) == 0) offset = z.hours * 3600;
    }
    const int64_t unix = (int64_t)mtb_Kit_Days_From_Civil(year, mo, day) * 86400 + h * 3600 + m * 60 + s - offset;
    return unix > 0 ? (uint32_t)unix : 0;
}

//##############################################################################################################
// The next "<name" that is that element (followed by '>', '/', or a space) in [p, end).
static const char* rss_Open(const char* p, const char* end, const char* name){
    const size_t n = strlen(name);
    while (p && p < end){
        const char* lt = (const char*)memchr(p, '<', (size_t)(end - p));
        if (lt == nullptr) return nullptr;
        if ((size_t)(end - lt) > n + 1 && strncmp(lt + 1, name, n) == 0){
            const char c = lt[n + 1];
            if (c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r') return lt;
        }
        p = lt + 1;
    }
    return nullptr;
}

// The text of the first element @p name in [p, end): its content's bounds. False if absent or empty.
static bool rss_Element(const char* p, const char* end, const char* name, const char*& from, const char*& to){
    const char* open = rss_Open(p, end, name);
    if (open == nullptr) return false;
    const char* gt = (const char*)memchr(open, '>', (size_t)(end - open));
    if (gt == nullptr || gt[-1] == '/') return false;       // <link href="..."/>: nothing inside.
    char close[24];
    snprintf(close, sizeof(close), "</%s>", name);
    const size_t n = strlen(close);
    for (const char* q = gt + 1; q + n <= end; q++){
        if (*q == '<' && memcmp(q, close, n) == 0){ from = gt + 1; to = q; return to > from; }
    }
    return false;
}

static bool rss_Text_Of(const char* p, const char* end, const char* const* names, char* dest, size_t size){
    for (const char* const* n = names; *n; n++){
        const char *from, *to;
        if (rss_Element(p, end, *n, from, to)){
            rss_Plain_Text(dest, size, from, (size_t)(to - from));
            if (dest[0]) return true;
        }
    }
    dest[0] = '\0';
    return false;
}

int rss_Parse(const char* text, size_t length, uint8_t feed, const char* fallbackSource, Mtb_Kit_Story_t* stories, int max,
              char* channel, size_t channelSize){
    if (text == nullptr) return -1;
    const char* end = text + length;
    const bool atom = rss_Open(text, end, "feed") != nullptr;
    if (!atom && !rss_Open(text, end, "rss") && !rss_Open(text, end, "rdf:RDF") && !rss_Open(text, end, "channel")) return -1;
    const char* itemName = atom ? "entry" : "item";
    char itemClose[10];
    snprintf(itemClose, sizeof(itemClose), "</%s>", itemName);
    const size_t closeLength = strlen(itemClose);

    // The feed's own title: the first title before the first item.
    const char* first = rss_Open(text, end, itemName);
    char source[KIT_NEWS_SOURCE * 2] = "";
    static const char* const TITLE[] = {"title", nullptr};
    rss_Text_Of(text, first ? first : end, TITLE, source, sizeof(source));
    if (channel && channelSize) snprintf(channel, channelSize, "%s", source);
    if (fallbackSource && fallbackSource[0]) snprintf(source, sizeof(source), "%s", fallbackSource);

    static const char* const SUMMARY[] = {"description", "summary", "content", "media:description", nullptr};
    static const char* const DATES[] = {"pubDate", "published", "updated", "dc:date", nullptr};
    static const char* const SOURCES[] = {"source", nullptr};
    char* title = (char*)malloc(KIT_NEWS_TITLE * 2);
    char* summary = (char*)malloc(KIT_NEWS_SUMMARY * 2);
    if (title == nullptr || summary == nullptr){ free(title); free(summary); return 0; }
    int count = 0;
    const char* p = first;
    while (p && count < max){
        // The item's end; an item cut off by a truncated answer is left out.
        const char* close = nullptr;
        for (const char* q = p; q + closeLength <= end; q++) if (*q == '<' && memcmp(q, itemClose, closeLength) == 0){ close = q; break; }
        if (close == nullptr) break;
        char date[48], itemSource[KIT_NEWS_SOURCE * 2];
        rss_Text_Of(p, close, TITLE, title, KIT_NEWS_TITLE * 2);
        rss_Text_Of(p, close, SUMMARY, summary, KIT_NEWS_SUMMARY * 2);
        rss_Text_Of(p, close, DATES, date, sizeof(date));
        const bool hasSource = rss_Text_Of(p, close, SOURCES, itemSource, sizeof(itemSource));
        // A title-less item (allowed by RSS 2.0) is named by its summary.
        const char* headline = title[0] ? title : summary;
        if (headline[0]){
            mtb_Kit_News_Fill_Story(stories[count], headline, hasSource ? itemSource : source, title[0] ? summary : "", rss_Parse_Date(date));
            stories[count].feed = feed;
            if (stories[count].title[0]) count++;
        }
        p = rss_Open(close, end, itemName);
    }
    free(title);
    free(summary);
    return count;
}

//##############################################################################################################
bool rss_Visible(const Mtb_Kit_Story_t& story, const Rss_Settings_t& s, uint32_t now){
    if (s.maxAgeHours && story.published && now > story.published && now - story.published > s.maxAgeHours * 3600UL) return false;
    const char* w = s.muted;
    while (*w){
        while (*w == ',' || *w == ' ') w++;
        size_t n = strcspn(w, ",");
        size_t trimmed = n;
        while (trimmed && w[trimmed - 1] == ' ') trimmed--;
        if (trimmed >= 2 && rss_Find(story.title, w, trimmed)) return false;
        w += n;
    }
    return true;
}
