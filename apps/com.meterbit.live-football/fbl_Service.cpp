#include "fbl_Service.h"
#include "fbl_Config.h"
#include "fbl_Api.h"
#include "fbl_Crest.h"
#include "mtb_engine.h"
#include "mtb_github.h"
#include <FS.h>
#include <LittleFS.h>
#include <ctype.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mtb_kit_keys.h"

static const char TAG[] = "FBL_SERVICE";

enum Fbl_Feed_t : uint8_t {
    F_STATUS = 0, F_LIVE, F_TODAY, F_TOMORROW, F_YESTERDAY, F_TABLE, F_SCORERS, F_UPCOMING, F_COUNT
};
static const char* const FEED_NAME[F_COUNT] = {"Status", "Live", "Today", "Tomorrow", "Yesterday", "Table", "Scorers", "Upcoming"};

struct Fbl_Cache_Header_t { uint32_t magic; uint16_t layout; uint16_t reserved; uint32_t size; uint32_t crc; };

struct Fbl_Feed_State_t {
    uint32_t due;
    uint8_t fails;
    uint8_t last;           // Fbl_Result_t
};

// Under serviceLock.
static SemaphoreHandle_t serviceLock = NULL;
static Fbl_Data_t* published = nullptr;         // PSRAM
static uint32_t publishedVersion = 1;
static Fbl_Settings_t current;
static Fbl_Feed_State_t feeds[F_COUNT];
static uint32_t settingsGeneration = 0;
static bool started = false, scheduled = false, workerBusy = false, lowMemory = false;
static uint32_t finalDueAt = 0;                 // Today's list again, for final results.
static uint32_t lastManualRefresh = 0, lastCacheSave = 0, liveInterval = 0;
static char searchQuery[32];                    // A team search waiting for the fetch task; "" none.
static Fbl_Search_Fn searchCallback = nullptr;

#define FBL_SEARCH_BIT  (1u << F_COUNT)         // In the fetch task's mask, beside the feeds.

static void fbl_Lock(void){ xSemaphoreTake(serviceLock, portMAX_DELAY); }
static void fbl_Unlock(void){ xSemaphoreGive(serviceLock); }

//##############################################################################################################
void fbl_Settings_Default(Fbl_Settings_t& settings){
    memset(&settings, 0, sizeof(settings));
    settings.magic = FBL_SETTINGS_MAGIC;
    static const uint16_t DEFAULT_FOLLOW[] = {39, 140, 135, 78, 61, 2, 399};
    for (uint16_t id : DEFAULT_FOLLOW) settings.follow[settings.followCount++] = id;
    settings.tableLeague = 39;
    settings.goalAlerts = FBL_ALERTS_ALL;
    settings.liveMinutes = FBL_LIVE_MIN_DEFAULT;
    settings.pageSeconds = FBL_PAGE_SEC_DEFAULT;
    settings.autoCycle = 1;
    settings.quietFrom = 22 * 60;
    settings.quietTo = 7 * 60;
    settings.apiKey[0] = '\0';      // The user's own API-Football key, sent from the phone.
}

void fbl_Settings_Clamp(Fbl_Settings_t& settings){
    uint8_t kept = 0;
    const uint8_t count = settings.followCount < FBL_MAX_FOLLOW ? settings.followCount : FBL_MAX_FOLLOW;
    for (uint8_t i = 0; i < count; i++){
        const uint16_t id = settings.follow[i];
        bool repeated = false;
        for (uint8_t k = 0; k < kept; k++) if (settings.follow[k] == id) repeated = true;
        if (fbl_Find_League(id) && !repeated) settings.follow[kept++] = id;
    }
    settings.followCount = kept;
    if (settings.goalAlerts > FBL_ALERTS_ALL) settings.goalAlerts = FBL_ALERTS_ALL;
    if (settings.liveMinutes < FBL_LIVE_MIN_LOWEST) settings.liveMinutes = FBL_LIVE_MIN_LOWEST;
    if (settings.liveMinutes > FBL_LIVE_MIN_HIGHEST) settings.liveMinutes = FBL_LIVE_MIN_HIGHEST;
    if (settings.pageSeconds < FBL_PAGE_SEC_LOWEST) settings.pageSeconds = FBL_PAGE_SEC_LOWEST;
    if (settings.pageSeconds > FBL_PAGE_SEC_HIGHEST) settings.pageSeconds = FBL_PAGE_SEC_HIGHEST;
    if (!fbl_Find_League(settings.tableLeague)) settings.tableLeague = kept ? settings.follow[0] : 39;
    // Favourites: known IDs, no repeats.
    uint8_t favourites = 0;
    const uint8_t favouriteCount = settings.favourites.count < FBL_MAX_FAVOURITES ? settings.favourites.count : FBL_MAX_FAVOURITES;
    for (uint8_t i = 0; i < favouriteCount; i++){
        Fbl_Favourite_t team = settings.favourites.team[i];
        team.name[sizeof(team.name) - 1] = '\0';
        bool repeated = false;
        for (uint8_t k = 0; k < favourites; k++) if (settings.favourites.team[k].id == team.id) repeated = true;
        if (team.id != 0 && !repeated) settings.favourites.team[favourites++] = team;
    }
    for (uint8_t i = favourites; i < FBL_MAX_FAVOURITES; i++) memset(&settings.favourites.team[i], 0, sizeof(Fbl_Favourite_t));
    settings.favourites.count = favourites;
    settings.quietHours = settings.quietHours ? 1 : 0;
    if (settings.quietFrom >= 1440) settings.quietFrom = 22 * 60;
    if (settings.quietTo >= 1440) settings.quietTo = 7 * 60;
    settings.apiKey[sizeof(settings.apiKey) - 1] = '\0';
    mtb_Kit_Clear_Retired_Key(settings.apiKey, sizeof(settings.apiKey));
}

//##############################################################################################################
static bool fbl_Followed(const Fbl_Settings_t& settings, uint16_t leagueId){
    for (uint8_t i = 0; i < settings.followCount; i++) if (settings.follow[i] == leagueId) return true;
    return false;
}

static uint16_t fbl_Season_Of(const Fbl_Data_t& data, uint16_t leagueId){
    for (int i = 0; i < data.matchCount; i++) if (data.matches[i].leagueId == leagueId && data.matches[i].season) return data.matches[i].season;
    if (data.table.leagueId == leagueId) return data.table.season;
    return 0;
}

static bool fbl_Have_Key(void){ return current.apiKey[0] != '\0'; }

static uint32_t fbl_Jitter(uint32_t range){ return esp_random() % (range + 1); }

// Whether an API request may go out now: a key, quota left (the reserve is for day lists), no wait asked for.
static bool fbl_Quota_Left(uint32_t now){
    const Fbl_Quota_t& q = published->quota;
    if (q.seen == 0 || q.seen / 86400 != now / 86400) return true;
    return !q.exhausted && q.remaining > 0;
}

static bool fbl_Day_Due(uint8_t slot, uint32_t now){
    const uint32_t day = now / 86400 - 1 + slot;
    if (published->dayKey[slot] != day || published->dayFetched[slot] == 0) return true;
    const uint32_t age = now - published->dayFetched[slot];
    if (slot == 1) return age >= FBL_TODAY_REFRESH_SEC || (finalDueAt != 0 && (int32_t)(now - finalDueAt) >= 0);
    if (slot == 2) return age >= 12UL * 3600;
    // Yesterday: again only if something was left unfinished when it was read.
    for (int i = 0; i < published->matchCount; i++){
        const Fbl_Match_t& m = published->matches[i];
        if (m.kickoff / 86400 == day && (fbl_Phase(m.status) != FBL_PHASE_FINISHED && fbl_Phase(m.status) != FBL_PHASE_OFF) && age >= 3600) return true;
    }
    return false;
}

// A new UTC day: today's list becomes yesterday's, tomorrow's today's.
static void fbl_Roll_Days(uint32_t now){
    const uint32_t day = now / 86400;
    if (published->dayKey[1] == day - 1 || published->dayKey[2] == day){
        published->dayKey[0] = published->dayKey[1];
        published->dayFetched[0] = published->dayFetched[1];
        published->dayKey[1] = published->dayKey[2];
        published->dayFetched[1] = published->dayFetched[2];
        published->dayKey[2] = 0;
        published->dayFetched[2] = 0;
        publishedVersion++;
    }
}

//##############################################################################################################
static void fbl_Save_Cache(const Fbl_Data_t& data){
    static const char tempPath[] = FBL_CACHE_FILE ".tmp";
    if (!mtb_Prepare_Flash_File_Path(tempPath)) return;
    Fbl_Cache_Header_t header = {FBL_DATA_MAGIC, FBL_DATA_LAYOUT, 0, sizeof(Fbl_Data_t), esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Fbl_Data_t))};
    File file = LittleFS.open(tempPath, "w");
    if (!file) return;
    const bool complete = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header)
                       && file.write((const uint8_t*)&data, sizeof(Fbl_Data_t)) == sizeof(Fbl_Data_t);
    file.close();
    if (!complete){ LittleFS.remove(tempPath); return; }
    if (!LittleFS.rename(tempPath, FBL_CACHE_FILE)){
        LittleFS.remove(FBL_CACHE_FILE);
        LittleFS.rename(tempPath, FBL_CACHE_FILE);
    }
}

static bool fbl_Load_Cache(Fbl_Data_t& data){
    if (!LittleFS.exists(FBL_CACHE_FILE)) return false;
    File file = LittleFS.open(FBL_CACHE_FILE, "r");
    if (!file) return false;
    Fbl_Cache_Header_t header;
    const bool usable = file.size() == sizeof(header) + sizeof(Fbl_Data_t)
                     && file.read((uint8_t*)&header, sizeof(header)) == sizeof(header)
                     && header.magic == FBL_DATA_MAGIC && header.layout == FBL_DATA_LAYOUT && header.size == sizeof(Fbl_Data_t)
                     && file.read((uint8_t*)&data, sizeof(Fbl_Data_t)) == sizeof(Fbl_Data_t)
                     && esp_rom_crc32_le(0, (const uint8_t*)&data, sizeof(Fbl_Data_t)) == header.crc
                     && data.matchCount <= FBL_MAX_MATCHES && data.table.count <= FBL_TABLE_ROWS && data.scorerCount <= FBL_SCORERS;
    file.close();
    return usable;
}

//##############################################################################################################
// What one request reads into: the work copy, and which matches it kept.
struct Fbl_Parse_Context_t {
    Fbl_Data_t* data;
    const Fbl_Settings_t* settings;
    uint32_t now;
    bool live;
    uint16_t extraLeague[4];        // Leagues of the favourite's matches, outside those followed.
    uint8_t extraCount;
    uint32_t ids[FBL_MAX_MATCHES * 2];
    uint16_t idCount;
    uint8_t scorers;
};

static bool fbl_On_Match(JsonVariantConst item, void* context){
    Fbl_Parse_Context_t& c = *(Fbl_Parse_Context_t*)context;
    Fbl_Match_t* match = (Fbl_Match_t*)heap_caps_malloc(sizeof(Fbl_Match_t), MALLOC_CAP_SPIRAM);
    if (match == nullptr) return false;
    if (fbl_Parse_Match(item, *match, c.now)){
        const bool favourite = fbl_Match_Is_Favourite(*match, c.settings->favourites);
        if (fbl_Followed(*c.settings, match->leagueId) || favourite){
            if (fbl_Put_Match(*c.data, *match, c.now, c.settings->favourites) && c.idCount < sizeof(c.ids) / sizeof(c.ids[0])) c.ids[c.idCount++] = match->id;
        }
    }
    heap_caps_free(match);
    return true;
}

static bool fbl_On_Status(JsonVariantConst item, void* context){
    Fbl_Quota_t& quota = *(Fbl_Quota_t*)context;
    const char* plan = item["subscription"]["plan"] | "";
    snprintf(quota.plan, sizeof(quota.plan), "%s", plan);
    quota.paid = plan[0] && strcasecmp(plan, "Free") != 0;
    quota.limitDay = item["requests"]["limit_day"] | FBL_FREE_LIMIT;
    const uint16_t used = item["requests"]["current"] | 0;
    quota.remaining = quota.limitDay > used ? quota.limitDay - used : 0;
    return true;
}

static bool fbl_On_Table(JsonVariantConst item, void* context){
    fbl_Parse_Table(item, *(Fbl_Table_t*)context);
    return false;       // One league.
}

static bool fbl_On_Scorer(JsonVariantConst item, void* context){
    Fbl_Parse_Context_t& c = *(Fbl_Parse_Context_t*)context;
    if (c.scorers >= FBL_SCORERS) return false;
    if (fbl_Parse_Scorer(item, c.data->scorers[c.scorers])) c.scorers++;
    return true;
}

// The live query: followed leagues, and those the favourite is playing in today.
static void fbl_Live_Path(char* path, size_t size, const Fbl_Settings_t& settings, const Fbl_Data_t& data){
    size_t used = snprintf(path, size, "/fixtures?live=");
    uint16_t ids[FBL_MAX_FOLLOW + 4];
    int count = 0;
    for (uint8_t i = 0; i < settings.followCount; i++) ids[count++] = settings.follow[i];
    for (int i = 0; i < data.matchCount && count < (int)(sizeof(ids) / sizeof(ids[0])); i++){
        const Fbl_Match_t& m = data.matches[i];
        if (!fbl_In_Play(m) && fbl_Phase(m.status) != FBL_PHASE_UPCOMING) continue;
        bool listed = false;
        for (int k = 0; k < count; k++) listed = listed || ids[k] == m.leagueId;
        if (!listed && fbl_Match_Is_Favourite(m, settings.favourites)) ids[count++] = m.leagueId;
    }
    for (int i = 0; i < count && used + 8 < size; i++) used += snprintf(path + used, size - used, "%s%u", i ? "-" : "", ids[i]);
}

static void fbl_Date_Text(char* dest, size_t size, uint32_t day){
    const time_t t = (time_t)day * 86400;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(dest, size, "%Y-%m-%d", &tm);
}

struct Fbl_Search_Context_t {
    const char* query;
    Fbl_Team_Found_t found[FBL_SEARCH_RESULTS];
    uint8_t count;
};

static bool fbl_On_Team(JsonVariantConst item, void* context){
    Fbl_Search_Context_t& c = *(Fbl_Search_Context_t*)context;
    Fbl_Team_Found_t team;
    if (fbl_Parse_Team(item, team)) fbl_Add_Found_Team(c.found, c.count, FBL_SEARCH_RESULTS, team, c.query);
    return true;
}

// A team search for the phone: one request, answered through the callback whatever happens.
static void fbl_Run_Search(const char* query, const char* key){
    Fbl_Search_Context_t* context = (Fbl_Search_Context_t*)heap_caps_calloc(1, sizeof(Fbl_Search_Context_t), MALLOC_CAP_SPIRAM);
    const char* error = nullptr;
    if (context == nullptr) error = "Pixlpal is low on memory";
    else {
        context->query = query;
        char path[96];
        size_t used = snprintf(path, sizeof(path), "/teams?search=");
        for (const char* c = query; *c && used + 4 < sizeof(path); c++){
            if (isalnum((unsigned char)*c) || *c == '-' || *c == '.') path[used++] = *c;
            else used += snprintf(path + used, sizeof(path) - used, "%%%02X", (unsigned char)*c);
        }
        path[used] = '\0';
        JsonDocument filter(fbl_Json_Allocator());
        fbl_Team_Filter(filter);
        const Fbl_Response_t response = fbl_Api_Get(path, key, filter, fbl_On_Team, context, "Search");
        const uint32_t now = (uint32_t)time(nullptr);
        fbl_Lock();
        if (response.quotaSeen){
            published->quota.limitDay = response.limitDay;
            published->quota.remaining = response.remaining;
            published->quota.seen = now;
            published->quota.exhausted = 0;
        }
        if (response.result == FBL_QUOTA){ published->quota.exhausted = 1; published->quota.remaining = 0; published->quota.seen = now; }
        publishedVersion++;
        fbl_Unlock();
        switch (response.result){
            case FBL_OK: break;
            case FBL_QUOTA: error = "Today's API-Football requests are used up"; break;
            case FBL_RATE: error = "Too many requests this minute: try again shortly"; break;
            case FBL_KEY: error = "API-Football did not accept the key"; break;
            case FBL_PLAN: error = "Your plan does not include team search"; break;
            default: error = "Could not reach API-Football"; break;
        }
    }
    const Fbl_Search_Fn callback = searchCallback;
    if (callback) callback(query, context ? context->found : nullptr, context && !error ? context->count : 0, error);
    heap_caps_free(context);
}

static void fbl_Worker_Task(void* parameter){
    const uint16_t dueMask = (uint16_t)(uintptr_t)parameter;
    Fbl_Data_t* work = (Fbl_Data_t*)heap_caps_malloc(sizeof(Fbl_Data_t), MALLOC_CAP_SPIRAM);
    Fbl_Settings_t* settings = (Fbl_Settings_t*)heap_caps_malloc(sizeof(Fbl_Settings_t), MALLOC_CAP_SPIRAM);
    Fbl_Parse_Context_t* context = (Fbl_Parse_Context_t*)heap_caps_calloc(1, sizeof(Fbl_Parse_Context_t), MALLOC_CAP_SPIRAM);
    bool saveCache = false, requested = false;

    if (dueMask & FBL_SEARCH_BIT){
        char query[sizeof(searchQuery)];
        char key[FBL_KEY_LEN];
        fbl_Lock();
        memcpy(query, searchQuery, sizeof(query));
        memcpy(key, current.apiKey, sizeof(key));
        searchQuery[0] = '\0';
        fbl_Unlock();
        if (query[0]){ fbl_Run_Search(query, key); requested = true; }
        memset(key, 0, sizeof(key));
    }

    for (uint8_t f = 0; f < F_COUNT && work && settings && context; f++){
        if (!(dueMask & (1u << f))) continue;
        fbl_Lock();
        memcpy(work, published, sizeof(Fbl_Data_t));
        *settings = current;
        const uint32_t generation = settingsGeneration;
        fbl_Unlock();
        const uint32_t now = (uint32_t)time(nullptr);
        const uint32_t today = now / 86400;
        char path[160], date[16];
        JsonDocument filter(fbl_Json_Allocator());
        memset(context, 0, sizeof(*context));
        context->data = work;
        context->settings = settings;
        context->now = now;
        Fbl_Item_Fn onItem = fbl_On_Match;
        void* itemContext = context;
        uint8_t slot = 0;
        const uint16_t season = fbl_Season_Of(*work, settings->tableLeague);
        switch (f){
            case F_STATUS:
                snprintf(path, sizeof(path), "/status");
                filter["subscription"]["plan"] = true;
                filter["requests"]["current"] = true;
                filter["requests"]["limit_day"] = true;
                onItem = fbl_On_Status;
                itemContext = &work->quota;
                break;
            case F_LIVE:
                fbl_Live_Path(path, sizeof(path), *settings, *work);
                fbl_Match_Filter(filter, true);
                context->live = true;
                break;
            case F_TODAY: case F_TOMORROW: case F_YESTERDAY:
                slot = f == F_TODAY ? 1 : f == F_TOMORROW ? 2 : 0;
                fbl_Date_Text(date, sizeof(date), today - 1 + slot);
                snprintf(path, sizeof(path), "/fixtures?date=%s&timezone=UTC", date);
                fbl_Match_Filter(filter, false);
                break;
            case F_TABLE:
                snprintf(path, sizeof(path), "/standings?league=%u&season=%u", settings->tableLeague, season);
                fbl_Table_Filter(filter);
                onItem = fbl_On_Table;
                itemContext = &work->table;
                break;
            case F_SCORERS:
                snprintf(path, sizeof(path), "/players/topscorers?league=%u&season=%u", settings->tableLeague, season);
                fbl_Scorer_Filter(filter);
                onItem = fbl_On_Scorer;
                break;
            default:
                snprintf(path, sizeof(path), "/fixtures?league=%u&season=%u&next=10&timezone=UTC", settings->tableLeague, season);
                fbl_Match_Filter(filter, false);
                break;
        }
        if (requested && f != F_STATUS) vTaskDelay(pdMS_TO_TICKS(1500));       // Well inside 10 a minute.
        requested = true;
        const Fbl_Response_t response = fbl_Api_Get(path, settings->apiKey, filter, onItem, itemContext, FEED_NAME[f]);

        // Results that change the list.
        int left = 0;
        if (response.result == FBL_OK){
            if (f == F_LIVE){
                left = fbl_Mark_Left_Live(*work, context->ids, context->idCount, now);
                work->liveFetched = now;
            } else if (f >= F_TODAY && f <= F_YESTERDAY){
                fbl_Drop_Missing(*work, (today - 1 + slot) * 86400, context->ids, context->idCount);
                work->dayKey[slot] = today - 1 + slot;
                work->dayFetched[slot] = now;
                fbl_Prune(*work, now, 36UL * 3600);
            } else if (f == F_TABLE) work->table.fetched = now;
            else if (f == F_SCORERS){ work->scorerCount = context->scorers; work->scorersLeague = settings->tableLeague; work->scorersFetched = now; }
            fbl_Sort(*work);
        }

        fbl_Lock();
        Fbl_Feed_State_t& state = feeds[f];
        state.last = response.result;
        // Quota: from every answer's headers, and /status.
        if (response.quotaSeen){
            published->quota.limitDay = response.limitDay;
            published->quota.remaining = response.remaining;
            published->quota.seen = now;
            published->quota.exhausted = 0;
        }
        uint32_t wait = 0;
        const bool sameSettings = generation == settingsGeneration;
        switch (response.result){
            case FBL_OK:
                state.fails = 0;
                if (f == F_STATUS){
                    Fbl_Quota_t quota = work->quota;
                    quota.seen = now;
                    quota.exhausted = quota.remaining == 0;
                    published->quota = quota;
                    wait = FBL_STATUS_REFRESH_SEC;
                } else if (sameSettings){
                    // Publish all but the quota, which is kept up to date above.
                    const Fbl_Quota_t quota = published->quota;
                    const uint8_t locked[FBL_F_COUNT] = {published->locked[0], published->locked[1], published->locked[2]};
                    memcpy(published, work, sizeof(Fbl_Data_t));
                    published->quota = quota;
                    memcpy(published->locked, locked, sizeof(locked));
                    if (f >= F_TABLE) published->locked[f - F_TABLE] = 0;
                    if (f == F_TODAY && finalDueAt && (int32_t)(now - finalDueAt) >= 0) finalDueAt = 0;
                    if (left > 0){ finalDueAt = now + FBL_FINAL_DELAY_SEC; feeds[F_TODAY].due = finalDueAt; }
                    if (f != F_LIVE || lastCacheSave == 0 || now - lastCacheSave >= 600) saveCache = true;
                } else wait = 5;       // Settings changed meanwhile: asked again with the new ones.
                if (f == F_TODAY) wait = wait ? wait : FBL_TODAY_REFRESH_SEC;
                else if (f == F_TOMORROW || f == F_YESTERDAY) wait = wait ? wait : 12UL * 3600;
                else if (f == F_TABLE) wait = wait ? wait : FBL_TABLE_REFRESH_SEC;
                else if (f == F_SCORERS) wait = wait ? wait : FBL_SCORERS_REFRESH_SEC;
                else if (f == F_UPCOMING) wait = wait ? wait : FBL_UPCOMING_REFRESH_SEC;
                break;
            case FBL_PLAN:
                if (f >= F_TABLE){ published->locked[f - F_TABLE] = 1; wait = FBL_LOCKED_RETRY_SEC; }
                else wait = FBL_BACKOFF_MAX_SEC;
                break;
            case FBL_QUOTA:
                published->quota.exhausted = 1;
                published->quota.remaining = 0;
                published->quota.seen = now;
                wait = (today + 1) * 86400 - now + 120;         // Midnight UTC.
                break;
            case FBL_RATE: wait = 65; break;
            case FBL_KEY: wait = FBL_BACKOFF_MAX_SEC; break;
            default:
                if (state.fails < 10) state.fails++;
                wait = FBL_BACKOFF_FIRST_SEC << (state.fails - 1 < 6 ? state.fails - 1 : 6);
                if (wait > FBL_BACKOFF_MAX_SEC) wait = FBL_BACKOFF_MAX_SEC;
                break;
        }
        if (f == F_LIVE){
            liveInterval = fbl_Plan_Live_Interval(published->quota, now, fbl_Live_Window_End(*published, now), current.liveMinutes * 60UL);
            if (wait == 0) wait = liveInterval;
        }
        state.due = now + wait + (f == F_LIVE || f == F_STATUS ? 0 : fbl_Jitter(60));
        publishedVersion++;
        fbl_Unlock();
        if (response.result == FBL_OK && f != F_STATUS){
            // Crests of the matches kept, for the screens that show them.
            for (int i = 0; i < work->matchCount && i < 12; i++){
                if (!fbl_In_Play(work->matches[i]) && !fbl_Match_Is_Favourite(work->matches[i], settings->favourites)) continue;
                fbl_Crest_Want(work->matches[i].team[0].id);
                fbl_Crest_Want(work->matches[i].team[1].id);
            }
        }
    }
    if (saveCache && work){
        fbl_Lock();
        memcpy(work, published, sizeof(Fbl_Data_t));
        lastCacheSave = (uint32_t)time(nullptr);
        fbl_Unlock();
        fbl_Save_Cache(*work);
    }
    heap_caps_free(work);
    heap_caps_free(settings);
    heap_caps_free(context);
    fbl_Lock();
    workerBusy = false;
    fbl_Unlock();
    vTaskDelete(NULL);
}

//##############################################################################################################
void fbl_Service_Begin(const Fbl_Settings_t& settings){
    if (serviceLock == NULL){
        serviceLock = xSemaphoreCreateMutex();
        published = (Fbl_Data_t*)heap_caps_calloc(1, sizeof(Fbl_Data_t), MALLOC_CAP_SPIRAM);
        if (serviceLock == NULL || published == nullptr){ ESP_LOGE(TAG, "Out of memory"); return; }
    }
    if (!started){
        Fbl_Data_t* fresh = (Fbl_Data_t*)heap_caps_calloc(1, sizeof(Fbl_Data_t), MALLOC_CAP_SPIRAM);
        if (fresh == nullptr) return;
        if (!fbl_Load_Cache(*fresh)) memset(fresh, 0, sizeof(Fbl_Data_t));
        fresh->magic = FBL_DATA_MAGIC;
        fresh->layout = FBL_DATA_LAYOUT;
        fresh->size = sizeof(Fbl_Data_t);
        fbl_Lock();
        memcpy(published, fresh, sizeof(Fbl_Data_t));
        current = settings;
        fbl_Settings_Clamp(current);
        started = true;
        scheduled = false;
        publishedVersion++;
        fbl_Unlock();
        heap_caps_free(fresh);
    }
    fbl_Service_Set_Settings(settings);
}

void fbl_Service_Set_Settings(const Fbl_Settings_t& settings){
    if (!started) return;
    Fbl_Settings_t next = settings;
    fbl_Settings_Clamp(next);
    fbl_Lock();
    const uint32_t now = (uint32_t)time(nullptr);
    const bool newKey = strcmp(next.apiKey, current.apiKey) != 0;
    const bool newLeagues = next.followCount != current.followCount || memcmp(next.follow, current.follow, sizeof(next.follow)) != 0
                         || next.favourites.count != current.favourites.count
                         || memcmp(&next.favourites, &current.favourites, sizeof(next.favourites)) != 0;
    const bool newTable = next.tableLeague != current.tableLeague;
    current = next;
    if (newKey || newLeagues || newTable) settingsGeneration++;
    if (scheduled){
        if (newKey){
            memset(&published->quota, 0, sizeof(published->quota));
            memset(published->locked, 0, sizeof(published->locked));
            for (uint8_t f = 0; f < F_COUNT; f++){ feeds[f].due = now; feeds[f].fails = 0; feeds[f].last = 0; }
        }
        // Other leagues or team: the day lists hold other matches, so all three are read again.
        if (newLeagues){
            published->matchCount = 0;
            memset(published->dayKey, 0, sizeof(published->dayKey));
            memset(published->dayFetched, 0, sizeof(published->dayFetched));
            for (uint8_t f = F_TODAY; f <= F_YESTERDAY; f++) feeds[f].due = now + (f - F_TODAY) * 5;
            feeds[F_LIVE].due = now;
        }
        if (newTable){ feeds[F_TABLE].due = feeds[F_SCORERS].due = feeds[F_UPCOMING].due = now; published->table.count = 0; published->scorerCount = 0; }
        publishedVersion++;
    }
    fbl_Unlock();
}

void fbl_Service_Poll(bool online, uint32_t now){
    if (!started || now == 0) return;
    fbl_Lock();
    if (!scheduled){
        // From the cache: what is due now. Status first, always: it is free and says the plan.
        for (uint8_t f = 0; f < F_COUNT; f++){ feeds[f].due = now + (f > F_LIVE ? f : 0); feeds[f].fails = 0; feeds[f].last = 0; }
        scheduled = true;
    }
    fbl_Roll_Days(now);
    uint16_t dueMask = 0;
    if (!workerBusy && online && fbl_Have_Key()){
        const bool quotaLeft = fbl_Quota_Left(now);
        const bool paid = published->quota.paid;
        const uint32_t windowEnd = fbl_Live_Window_End(*published, now);
        if (windowEnd == 0) liveInterval = 0;
        for (uint8_t f = 0; f < F_COUNT; f++){
            if ((int32_t)(now - feeds[f].due) < 0) continue;
            if (f == F_STATUS){ dueMask |= 1u << f; continue; }
            if (!quotaLeft || feeds[F_STATUS].last == FBL_KEY || feeds[f].last == FBL_KEY) continue;
            switch (f){
                case F_LIVE:
                    if (windowEnd == 0){ feeds[f].due = now + 60; continue; }
                    break;
                case F_TODAY:     if (!fbl_Day_Due(1, now)){ feeds[f].due = now + 300; continue; } break;
                case F_TOMORROW:  if (!fbl_Day_Due(2, now)){ feeds[f].due = now + 1800; continue; } break;
                case F_YESTERDAY: if (!fbl_Day_Due(0, now)){ feeds[f].due = now + 1800; continue; } break;
                default:
                    // Paid features: not even asked for on the Free plan, nor without the league's season.
                    if (!paid || fbl_Season_Of(*published, current.tableLeague) == 0){ feeds[f].due = now + 1800; continue; }
                    break;
            }
            dueMask |= 1u << f;
        }
    }
    if (searchQuery[0] && !workerBusy && online && fbl_Have_Key()) dueMask |= FBL_SEARCH_BIT;
    if (dueMask){
        lowMemory = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < FBL_WORKER_MIN_INTERNAL_HEAP;
        if (lowMemory){ for (uint8_t f = 0; f < F_COUNT; f++) if (dueMask & (1u << f)) feeds[f].due = now + 60; dueMask = 0; }
        else workerBusy = true;
    }
    fbl_Unlock();
    if (dueMask == 0) return;
    if (xTaskCreatePinnedToCore(fbl_Worker_Task, "fblFetch", FBL_WORKER_STACK, (void*)(uintptr_t)dueMask, 1, NULL, tskNO_AFFINITY) != pdPASS){
        fbl_Lock();
        workerBusy = false;
        for (uint8_t f = 0; f < F_COUNT; f++) if (dueMask & (1u << f)) feeds[f].due = now + 60;
        fbl_Unlock();
    }
}

bool fbl_Service_Copy(Fbl_Data_t& out, uint32_t& version){
    if (!started) return false;
    fbl_Lock();
    const bool changed = version != publishedVersion;
    if (changed){ memcpy(&out, published, sizeof(Fbl_Data_t)); version = publishedVersion; }
    fbl_Unlock();
    return changed;
}

void fbl_Service_Get_Status(Fbl_Status_t& status){
    memset(&status, 0, sizeof(status));
    if (!started) return;
    fbl_Lock();
    const uint32_t now = (uint32_t)time(nullptr);
    status.busy = workerBusy;
    status.lowMemory = lowMemory;
    status.keyMissing = !fbl_Have_Key();
    for (uint8_t f = 0; f < F_COUNT; f++){
        if (feeds[f].last == FBL_KEY) status.keyRejected = true;
        if (feeds[f].last == FBL_RATE) status.rateLimited = true;
    }
    const uint8_t last = feeds[F_TODAY].last;
    status.failing = last == FBL_NET || last == FBL_PARSE || last == FBL_SERVER;
    status.quotaUsed = !fbl_Quota_Left(now);
    status.liveInterval = liveInterval;
    status.started = true;
    status.quota = published->quota;
    status.updated = published->liveFetched > published->dayFetched[1] ? published->liveFetched : published->dayFetched[1];
    for (uint8_t f = 0; f < FBL_F_COUNT; f++) if (published->locked[f]) status.locked |= 1u << f;
    fbl_Unlock();
}

bool fbl_Service_Refresh_Now(uint32_t now){
    if (!started || now == 0) return false;
    fbl_Lock();
    const bool accepted = (lastManualRefresh == 0 || now - lastManualRefresh >= FBL_MANUAL_REFRESH_GAP_SEC) && fbl_Quota_Left(now) && fbl_Have_Key();
    if (accepted){
        lastManualRefresh = now;
        feeds[F_LIVE].due = now;
        feeds[F_STATUS].due = now;
        if (published->dayFetched[1] == 0 || now - published->dayFetched[1] >= 600){ published->dayFetched[1] = 0; feeds[F_TODAY].due = now; }
    }
    fbl_Unlock();
    return accepted;
}

void fbl_Service_Set_Search_Callback(Fbl_Search_Fn fn){ searchCallback = fn; }

bool fbl_Service_Search(const char* query, uint32_t now){
    if (!started || now == 0 || !fbl_Search_Query_Valid(query)) return false;
    fbl_Lock();
    const bool accepted = searchQuery[0] == '\0' && fbl_Have_Key() && fbl_Quota_Left(now);
    if (accepted) snprintf(searchQuery, sizeof(searchQuery), "%s", query);
    fbl_Unlock();
    return accepted;
}
