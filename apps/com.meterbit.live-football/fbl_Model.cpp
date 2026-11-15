#include "fbl_Model.h"
#include "fbl_Config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// League IDs checked against API-Football answers of 6-7 November 2026, except the cups and tournaments not
// playing then (2, 3, 848, 1, 4, 5, 6, 9, 12, 13, 48, 66, 81, 137, 143), which are API-Football's long-standing IDs.
static const Fbl_League_t LEAGUES[] = {
    {39, "PREMIER LEAGUE", "England"},      {40, "CHAMPIONSHIP", "England"},      {45, "FA CUP", "England"},
    {48, "EFL CUP", "England"},             {140, "LA LIGA", "Spain"},             {143, "COPA DEL REY", "Spain"},
    {135, "SERIE A", "Italy"},              {137, "COPPA ITALIA", "Italy"},        {78, "BUNDESLIGA", "Germany"},
    {81, "DFB POKAL", "Germany"},           {61, "LIGUE 1", "France"},             {66, "COUPE DE FRANCE", "France"},
    {88, "EREDIVISIE", "Netherlands"},      {94, "PRIMEIRA LIGA", "Portugal"},     {144, "PRO LEAGUE", "Belgium"},
    {179, "PREMIERSHIP", "Scotland"},       {203, "SUPER LIG", "Turkey"},          {307, "SAUDI PRO LEAGUE", "Saudi Arabia"},
    {253, "MLS", "USA"},                    {262, "LIGA MX", "Mexico"},            {71, "BRASILEIRAO", "Brazil"},
    {128, "LIGA PROFESIONAL", "Argentina"}, {399, "NPFL", "Nigeria"},              {200, "BOTOLA PRO", "Morocco"},
    {2, "CHAMPIONS LEAGUE", "Europe"},      {3, "EUROPA LEAGUE", "Europe"},        {848, "CONFERENCE LEAGUE", "Europe"},
    {12, "CAF CHAMPIONS LEAGUE", "Africa"}, {13, "COPA LIBERTADORES", "South America"},
    {1, "WORLD CUP", "World"},              {4, "EURO", "Europe"},                 {6, "AFRICA CUP OF NATIONS", "Africa"},
    {9, "COPA AMERICA", "South America"},   {5, "NATIONS LEAGUE", "Europe"},
};

const Fbl_League_t* fbl_Leagues(size_t& count){
    count = sizeof(LEAGUES) / sizeof(LEAGUES[0]);
    return LEAGUES;
}

const Fbl_League_t* fbl_Find_League(uint16_t id){
    for (const Fbl_League_t& league : LEAGUES) if (league.id == id) return &league;
    return nullptr;
}

//##############################################################################################################
uint8_t fbl_Phase(const char* s){
    if (s == nullptr || s[0] == '\0') return FBL_PHASE_UPCOMING;
    static const char* const LIVE[] = {"1H", "2H", "ET", "P", "LIVE"};
    static const char* const BREAK[] = {"HT", "BT", "INT"};
    static const char* const FINISHED[] = {"FT", "AET", "PEN", "AWD", "WO"};
    static const char* const OFF[] = {"PST", "CANC", "ABD", "SUSP"};
    for (const char* c : LIVE) if (strcmp(s, c) == 0) return FBL_PHASE_LIVE;
    for (const char* c : BREAK) if (strcmp(s, c) == 0) return FBL_PHASE_BREAK;
    for (const char* c : FINISHED) if (strcmp(s, c) == 0) return FBL_PHASE_FINISHED;
    for (const char* c : OFF) if (strcmp(s, c) == 0) return FBL_PHASE_OFF;
    return FBL_PHASE_UPCOMING;      // NS, TBD
}

bool fbl_In_Play(const Fbl_Match_t& match){
    const uint8_t phase = fbl_Phase(match.status);
    return (phase == FBL_PHASE_LIVE || phase == FBL_PHASE_BREAK) && !match.awaitingFinal;
}

//##############################################################################################################
// Latin-1 Supplement and Latin Extended-A without accents, for names like "Atlético" or "Beşiktaş".
static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static const char LATIN_A[] =
    "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";

void fbl_To_Ascii(char* dest, size_t size, const char* text){
    if (size == 0) return;
    size_t used = 0;
    const uint8_t* p = (const uint8_t*)(text ? text : "");
    while (*p && used + 1 < size){
        uint32_t cp;
        int extra;
        if (*p < 0x80){ cp = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0){ cp = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0){ cp = *p & 0x0F; extra = 2; }
        else if ((*p & 0xF8) == 0xF0){ cp = *p & 0x07; extra = 3; }
        else { p++; continue; }
        bool valid = true;
        for (int i = 1; i <= extra; i++){
            if ((p[i] & 0xC0) != 0x80){ valid = false; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += valid ? extra + 1 : 1;
        if (!valid) continue;
        char c = 0;
        if (cp >= 0x20 && cp < 0x7F) c = (char)cp;
        else if (cp >= 0xC0 && cp <= 0xFF) c = LATIN1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) c = LATIN_A[cp - 0x100];
        else if (cp == 0x2019 || cp == 0x2018) c = '\'';
        else if (cp == 0x2013 || cp == 0x2014) c = '-';
        else if (cp == 0xA0) c = ' ';
        if (c) dest[used++] = c;
    }
    dest[used] = '\0';
}

void fbl_Round_Text(char* dest, size_t size, const char* round){
    char text[48];
    fbl_To_Ascii(text, sizeof(text), round);
    const char* dash = strstr(text, " - ");
    if (strncasecmp(text, "Regular Season", 14) == 0 && dash){ snprintf(dest, size, "MATCHDAY %s", dash + 3); return; }
    // "1/8-finals": a round of 16.
    if (text[0] == '1' && text[1] == '/'){
        const int part = atoi(text + 2);
        if (part > 0 && strstr(text, "final")){ snprintf(dest, size, "ROUND OF %d", part * 2); return; }
    }
    snprintf(dest, size, "%s", text);
    for (char* c = dest; *c; c++) *c = (char)toupper((unsigned char)*c);
}

const char* fbl_Short_Player(const char* name){
    if (name == nullptr) return "";
    const char* space = strrchr(name, ' ');
    return space && space[1] ? space + 1 : name;
}

int32_t fbl_Days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

//##############################################################################################################
void fbl_Match_Filter(JsonDocument& filter, bool events){
    filter.clear();
    JsonObject fixture = filter["fixture"].to<JsonObject>();
    fixture["id"] = true;
    fixture["timestamp"] = true;
    fixture["venue"]["name"] = true;
    fixture["status"]["short"] = true;
    fixture["status"]["elapsed"] = true;
    fixture["status"]["extra"] = true;
    filter["league"]["id"] = true;
    filter["league"]["season"] = true;
    filter["league"]["round"] = true;
    filter["league"]["name"] = true;
    for (const char* side : {"home", "away"}){
        filter["teams"][side]["id"] = true;
        filter["teams"][side]["name"] = true;
        filter["goals"][side] = true;
        filter["score"]["halftime"][side] = true;
        filter["score"]["penalty"][side] = true;
    }
    if (events){
        JsonObject event = filter["events"][0].to<JsonObject>();
        event["time"]["elapsed"] = true;
        event["time"]["extra"] = true;
        event["team"]["id"] = true;
        event["player"]["name"] = true;
        event["type"] = true;
        event["detail"] = true;
    }
}

static int8_t fbl_Goals(JsonVariantConst value){ return value.is<int>() ? (int8_t)value.as<int>() : (int8_t)-1; }

static bool fbl_Important(uint8_t type){ return type <= FBL_EV_MISSED_PENALTY || type == FBL_EV_RED; }

bool fbl_Parse_Match(JsonVariantConst item, Fbl_Match_t& match, uint32_t now){
    const uint32_t id = item["fixture"]["id"] | 0u;
    if (id == 0) return false;
    memset(&match, 0, sizeof(match));
    match.id = id;
    match.kickoff = item["fixture"]["timestamp"] | 0u;
    match.updated = now;
    match.leagueId = item["league"]["id"] | 0;
    match.season = item["league"]["season"] | 0;
    snprintf(match.status, sizeof(match.status), "%s", item["fixture"]["status"]["short"] | "NS");
    match.elapsed = item["fixture"]["status"]["elapsed"] | 0;
    match.extra = item["fixture"]["status"]["extra"] | 0;
    static const char* const SIDES[2] = {"home", "away"};
    for (int s = 0; s < 2; s++){
        match.team[s].id = item["teams"][SIDES[s]]["id"] | 0u;
        fbl_To_Ascii(match.team[s].name, sizeof(match.team[s].name), item["teams"][SIDES[s]]["name"] | "");
        match.goals[s] = fbl_Goals(item["goals"][SIDES[s]]);
        match.halftime[s] = fbl_Goals(item["score"]["halftime"][SIDES[s]]);
        match.penalties[s] = fbl_Goals(item["score"]["penalty"][SIDES[s]]);
    }
    char round[48];
    fbl_Round_Text(round, sizeof(round), item["league"]["round"] | "");
    snprintf(match.round, sizeof(match.round), "%.*s", (int)sizeof(match.round) - 1, round);
    fbl_To_Ascii(match.venue, sizeof(match.venue), item["fixture"]["venue"]["name"] | "");
    char league[48];
    fbl_To_Ascii(league, sizeof(league), item["league"]["name"] | "");
    for (char* c = league; *c; c++) *c = (char)toupper((unsigned char)*c);
    snprintf(match.league, sizeof(match.league), "%.*s", (int)sizeof(match.league) - 1, league);

    // Events: goals and red cards first when there are more than fit.
    for (JsonVariantConst event : item["events"].as<JsonArrayConst>()){
        const char* type = event["type"] | "";
        const char* detail = event["detail"] | "";
        Fbl_Event_t e = {};
        if (strcmp(type, "Goal") == 0){
            if (strstr(detail, "Own")) e.type = FBL_EV_OWN_GOAL;
            else if (strstr(detail, "Missed")) e.type = FBL_EV_MISSED_PENALTY;
            else if (strstr(detail, "Penalty")) e.type = FBL_EV_PENALTY;
            else e.type = FBL_EV_GOAL;
        } else if (strcmp(type, "Card") == 0) e.type = (strstr(detail, "Red") || strstr(detail, "Second")) ? FBL_EV_RED : FBL_EV_YELLOW;
        else if (strcasecmp(type, "subst") == 0) e.type = FBL_EV_SUB;
        else if (strcasecmp(type, "Var") == 0) e.type = FBL_EV_VAR;
        else e.type = FBL_EV_OTHER;
        e.minute = event["time"]["elapsed"] | 0;
        e.extra = event["time"]["extra"] | 0;
        const uint32_t teamId = event["team"]["id"] | 0u;
        e.away = (teamId != 0 && teamId == match.team[1].id) ? 1 : 0;
        char player[40];
        fbl_To_Ascii(player, sizeof(player), event["player"]["name"] | "");
        snprintf(e.player, sizeof(e.player), "%.*s", (int)sizeof(e.player) - 1, player);
        if (match.eventCount < FBL_MAX_EVENTS) match.events[match.eventCount++] = e;
        else if (fbl_Important(e.type)){
            for (int i = FBL_MAX_EVENTS - 1; i >= 0; i--) if (!fbl_Important(match.events[i].type)){ match.events[i] = e; break; }
        }
    }
    // In time order.
    for (int i = 1; i < match.eventCount; i++){
        const Fbl_Event_t e = match.events[i];
        int k = i - 1;
        while (k >= 0 && (match.events[k].minute > e.minute || (match.events[k].minute == e.minute && match.events[k].extra > e.extra))){ match.events[k + 1] = match.events[k]; k--; }
        match.events[k + 1] = e;
    }
    return true;
}

void fbl_Table_Filter(JsonDocument& filter){
    filter.clear();
    JsonObject league = filter["league"].to<JsonObject>();
    league["id"] = true;
    league["season"] = true;
    JsonObject row = league["standings"][0][0].to<JsonObject>();
    row["rank"] = true;
    row["team"]["id"] = true;
    row["team"]["name"] = true;
    row["points"] = true;
    row["goalsDiff"] = true;
    row["form"] = true;
    row["description"] = true;
    row["all"]["played"] = true;
}

bool fbl_Parse_Table(JsonVariantConst item, Fbl_Table_t& table){
    JsonArrayConst groups = item["league"]["standings"].as<JsonArrayConst>();
    if (groups.isNull() || groups.size() == 0) return false;
    table.leagueId = item["league"]["id"] | 0;
    table.season = item["league"]["season"] | 0;
    table.count = 0;
    // The first group: the league table, or group A of a cup.
    for (JsonVariantConst row : groups[0].as<JsonArrayConst>()){
        if (table.count >= FBL_TABLE_ROWS) break;
        Fbl_Row_t& r = table.rows[table.count++];
        memset(&r, 0, sizeof(r));
        r.rank = row["rank"] | 0;
        r.points = row["points"] | 0;
        r.goalDiff = row["goalsDiff"] | 0;
        r.played = row["all"]["played"] | 0;
        r.teamId = row["team"]["id"] | 0u;
        fbl_To_Ascii(r.team, sizeof(r.team), row["team"]["name"] | "");
        snprintf(r.form, sizeof(r.form), "%s", row["form"] | "");
        // API-Football's form is newest first; the screen draws newest last.
        const size_t n = strlen(r.form);
        for (size_t i = 0; i < n / 2; i++){ const char t = r.form[i]; r.form[i] = r.form[n - 1 - i]; r.form[n - 1 - i] = t; }
        const char* description = row["description"] | "";
        if (strstr(description, "Relegation")) r.zone = 3;
        else if (strstr(description, "Champions League") || strstr(description, "Promotion") || strstr(description, "Title")) r.zone = 1;
        else if (description[0]) r.zone = 2;
    }
    return table.count > 0;
}

void fbl_Scorer_Filter(JsonDocument& filter){
    filter.clear();
    filter["player"]["name"] = true;
    filter["statistics"][0]["team"]["name"] = true;
    filter["statistics"][0]["goals"]["total"] = true;
}

void fbl_Team_Filter(JsonDocument& filter){
    filter.clear();
    filter["team"]["id"] = true;
    filter["team"]["name"] = true;
    filter["team"]["country"] = true;
    filter["team"]["founded"] = true;
    filter["team"]["national"] = true;
}

bool fbl_Parse_Team(JsonVariantConst item, Fbl_Team_Found_t& team){
    memset(&team, 0, sizeof(team));
    team.id = item["team"]["id"] | 0UL;
    const char* name = item["team"]["name"];
    if (team.id == 0 || name == nullptr || name[0] == '\0') return false;
    // UTF-8 kept: the phone shows it; the panel draws the names the fixtures carry.
    snprintf(team.name, sizeof(team.name), "%s", name);
    snprintf(team.country, sizeof(team.country), "%s", item["team"]["country"] | "");
    team.founded = item["team"]["founded"] | 0;
    team.national = item["team"]["national"] | false;
    return true;
}

// 0: the name is the query; 1: starts with it; 2: the rest. Plus 3 for youth and reserve sides.
static int fbl_Team_Rank(const char* name, const char* query){
    const size_t q = strlen(query);
    int rank = strcasecmp(name, query) == 0 ? 0 : strncasecmp(name, query, q) == 0 ? 1 : 2;
    static const char* const SIDES[] = {" U17", " U18", " U19", " U20", " U21", " U23", " II", " B", " Res.", " Reserves"};
    const size_t n = strlen(name);
    for (const char* side : SIDES){
        const size_t k = strlen(side);
        if (n > k && strcasecmp(name + n - k, side) == 0) return rank + 3;
    }
    return rank;
}

void fbl_Add_Found_Team(Fbl_Team_Found_t* list, uint8_t& count, uint8_t max, const Fbl_Team_Found_t& team, const char* query){
    const int rank = fbl_Team_Rank(team.name, query);
    uint8_t at = count;
    while (at > 0 && fbl_Team_Rank(list[at - 1].name, query) > rank) at--;
    if (at >= max) return;
    const uint8_t moved = count < max ? count - at : max - 1 - at;
    memmove(&list[at + 1], &list[at], sizeof(Fbl_Team_Found_t) * moved);
    list[at] = team;
    if (count < max) count++;
}

bool fbl_Search_Query_Valid(const char* query){
    const size_t length = query ? strlen(query) : 0;
    if (length < 3 || length > 30) return false;
    for (size_t i = 0; i < length; i++){
        const unsigned char c = (unsigned char)query[i];
        if (!(isalnum(c) || strchr(" .-'&", c))) return false;
    }
    return query[0] != ' ';
}

bool fbl_Parse_Scorer(JsonVariantConst item, Fbl_Scorer_t& scorer){
    const char* name = item["player"]["name"];
    if (name == nullptr) return false;
    char text[48];
    fbl_To_Ascii(text, sizeof(text), name);
    snprintf(scorer.player, sizeof(scorer.player), "%.*s", (int)sizeof(scorer.player) - 1, text);
    fbl_To_Ascii(scorer.team, sizeof(scorer.team), item["statistics"][0]["team"]["name"] | "");
    scorer.goals = item["statistics"][0]["goals"]["total"] | 0;
    return true;
}

uint8_t fbl_Classify_Errors(JsonVariantConst errors, char* text, size_t size){
    if (size) text[0] = '\0';
    if (!errors.is<JsonObjectConst>() || errors.size() == 0) return FBL_ERR_NONE;
    uint8_t kind = FBL_ERR_OTHER;
    for (JsonPairConst pair : errors.as<JsonObjectConst>()){
        const char* key = pair.key().c_str();
        if (size && pair.value().is<const char*>()) fbl_To_Ascii(text, size, pair.value().as<const char*>());
        if (strcmp(key, "plan") == 0) kind = FBL_ERR_PLAN;
        else if (strcmp(key, "requests") == 0) kind = FBL_ERR_QUOTA;
        else if (strcmp(key, "rateLimit") == 0) kind = FBL_ERR_RATE;
        else if (strcmp(key, "token") == 0 || strcmp(key, "access") == 0) kind = FBL_ERR_KEY;
        break;
    }
    return kind;
}

//##############################################################################################################
int fbl_Find_Match(const Fbl_Data_t& data, uint32_t id){
    for (int i = 0; i < data.matchCount; i++) if (data.matches[i].id == id) return i;
    return -1;
}

bool fbl_Is_Favourite(uint32_t teamId, const Fbl_Favourites_t& favourites){
    if (teamId == 0) return false;
    for (uint8_t i = 0; i < favourites.count && i < FBL_MAX_FAVOURITES; i++) if (favourites.team[i].id == teamId) return true;
    return false;
}

bool fbl_Match_Is_Favourite(const Fbl_Match_t& match, const Fbl_Favourites_t& favourites){
    return fbl_Is_Favourite(match.team[0].id, favourites) || fbl_Is_Favourite(match.team[1].id, favourites);
}

static uint32_t fbl_Distance(uint32_t a, uint32_t b){ return a > b ? a - b : b - a; }

bool fbl_Put_Match(Fbl_Data_t& data, const Fbl_Match_t& match, uint32_t now, const Fbl_Favourites_t& favourites){
    int at = fbl_Find_Match(data, match.id);
    if (at >= 0){
        Fbl_Match_t& old = data.matches[at];
        Fbl_Match_t merged = match;
        const uint8_t oldPhase = fbl_Phase(old.status), newPhase = fbl_Phase(match.status);
        // A day list read before kick-off must not undo what the live feed has said since.
        if (newPhase == FBL_PHASE_UPCOMING && (oldPhase == FBL_PHASE_LIVE || oldPhase == FBL_PHASE_BREAK || oldPhase == FBL_PHASE_FINISHED)){
            memcpy(merged.status, old.status, sizeof(merged.status));
            merged.elapsed = old.elapsed;
            merged.extra = old.extra;
            memcpy(merged.goals, old.goals, sizeof(merged.goals));
            memcpy(merged.halftime, old.halftime, sizeof(merged.halftime));
            merged.awaitingFinal = old.awaitingFinal;
        }
        if (merged.eventCount == 0 && old.eventCount > 0){
            merged.eventCount = old.eventCount;
            memcpy(merged.events, old.events, sizeof(merged.events));
        }
        if (newPhase == FBL_PHASE_FINISHED || newPhase == FBL_PHASE_OFF) merged.awaitingFinal = 0;
        data.matches[at] = merged;
        return true;
    }
    if (data.matchCount >= FBL_MAX_MATCHES){
        // Room is made by dropping the match furthest in time from now: never one in play or a favourite's.
        int drop = -1;
        uint32_t furthest = fbl_Distance(match.kickoff, now);
        if (fbl_Match_Is_Favourite(match, favourites)) furthest = 0;
        for (int i = 0; i < data.matchCount; i++){
            const Fbl_Match_t& candidate = data.matches[i];
            if (fbl_In_Play(candidate) || fbl_Match_Is_Favourite(candidate, favourites)) continue;
            const uint32_t distance = fbl_Distance(candidate.kickoff, now);
            if (distance > furthest){ furthest = distance; drop = i; }
        }
        if (drop < 0) return false;
        data.matches[drop] = data.matches[--data.matchCount];
    }
    data.matches[data.matchCount++] = match;
    return true;
}

void fbl_Drop_Missing(Fbl_Data_t& data, uint32_t dayStart, const uint32_t* ids, size_t count){
    for (int i = data.matchCount - 1; i >= 0; i--){
        const Fbl_Match_t& match = data.matches[i];
        if (match.kickoff < dayStart || match.kickoff >= dayStart + 86400 || fbl_In_Play(match)) continue;
        bool listed = false;
        for (size_t k = 0; k < count && !listed; k++) listed = ids[k] == match.id;
        if (!listed) data.matches[i] = data.matches[--data.matchCount];
    }
}

int fbl_Mark_Left_Live(Fbl_Data_t& data, const uint32_t* liveIds, size_t count, uint32_t now){
    int left = 0;
    for (int i = 0; i < data.matchCount; i++){
        Fbl_Match_t& match = data.matches[i];
        if (!fbl_In_Play(match)) continue;
        bool live = false;
        for (size_t k = 0; k < count && !live; k++) live = liveIds[k] == match.id;
        if (live) continue;
        match.awaitingFinal = 1;
        match.updated = now;
        left++;
    }
    return left;
}

void fbl_Prune(Fbl_Data_t& data, uint32_t now, uint32_t ageSec){
    for (int i = data.matchCount - 1; i >= 0; i--){
        const Fbl_Match_t& match = data.matches[i];
        if (fbl_In_Play(match) || match.kickoff + ageSec >= now) continue;
        data.matches[i] = data.matches[--data.matchCount];
    }
}

void fbl_Sort(Fbl_Data_t& data){
    for (int i = 1; i < data.matchCount; i++){
        const Fbl_Match_t m = data.matches[i];
        int k = i - 1;
        while (k >= 0 && (data.matches[k].kickoff > m.kickoff || (data.matches[k].kickoff == m.kickoff && data.matches[k].leagueId > m.leagueId))){
            data.matches[k + 1] = data.matches[k];
            k--;
        }
        data.matches[k + 1] = m;
    }
}

uint32_t fbl_Live_Window_End(const Fbl_Data_t& data, uint32_t now){
    uint32_t end = 0;
    for (int i = 0; i < data.matchCount; i++){
        const Fbl_Match_t& match = data.matches[i];
        uint32_t until = 0;
        if (fbl_In_Play(match)) until = match.kickoff + FBL_MATCH_LENGTH_SEC > now + 900 ? match.kickoff + FBL_MATCH_LENGTH_SEC : now + 900;
        else if (fbl_Phase(match.status) == FBL_PHASE_UPCOMING && match.kickoff <= now + 60 && now < match.kickoff + FBL_MATCH_LENGTH_SEC) until = match.kickoff + FBL_MATCH_LENGTH_SEC;
        if (until > end) end = until;
    }
    return end;
}

uint32_t fbl_Plan_Live_Interval(const Fbl_Quota_t& quota, uint32_t now, uint32_t windowEnd, uint32_t floorSec){
    const uint32_t dayEnd = (now / 86400 + 1) * 86400;      // API-Football's day is UTC.
    uint32_t remaining = FBL_FREE_LIMIT;
    if (quota.seen != 0){
        remaining = (quota.seen / 86400 == now / 86400) ? quota.remaining : quota.limitDay;
        if (quota.exhausted && quota.seen / 86400 == now / 86400) remaining = 0;
    }
    const uint32_t available = remaining > FBL_RESERVE_REQUESTS ? remaining - FBL_RESERVE_REQUESTS : 0;
    if (available == 0) return dayEnd - now + 60;
    const uint32_t until = (windowEnd != 0 && windowEnd < dayEnd) ? windowEnd : dayEnd;
    const uint32_t seconds = until > now + 60 ? until - now : 60;
    uint32_t interval = seconds / available;
    if (interval < floorSec) interval = floorSec;
    if (interval > FBL_LIVE_SLOWEST_SEC) interval = FBL_LIVE_SLOWEST_SEC;
    return interval;
}
