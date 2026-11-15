// Google Calendar API v3 and Google Tasks API v1, for meterbit_cal_kit. Read-only scopes: calendar.readonly and
// tasks.readonly (asked for by the phone when it links the account).
#include "gcl_Provider.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define GCL_UA          "PIXLPAL-L2/1.0 (Pixlpal Google Calendar)"
#define GCL_LIST_MAX    (64 * 1024)
#define GCL_EVENTS_MAX  (128 * 1024)

static const Mtb_Kit_OAuth_Provider_t GCL_OAUTH = {
    "https://oauth2.googleapis.com/token", "", "google", nullptr, "gcalToken", GCL_UA, nullptr     // Refreshed by the account server
};

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

static void iso_Utc(char* dest, size_t size, uint32_t t){
    const time_t tt = (time_t)t;
    struct tm u;
    gmtime_r(&tt, &u);
    strftime(dest, size, "%Y-%m-%dT%H:%M:%SZ", &u);
}

static bool parse(JsonDocument& doc, const char* body, size_t length, JsonDocument& filter, Mtb_Cal_Api_t& api){
    if (deserializeJson(doc, body, length, DeserializationOption::Filter(filter))){
        copy(api.error, sizeof(api.error), "Google's answer was not readable");
        return false;
    }
    return true;
}

static bool gcl_Account(Mtb_Cal_Api_t& api, char* dest, size_t size){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, "https://www.googleapis.com/calendar/v3/users/me/calendarList/primary?fields=id", 4096, length);
    if (body == nullptr) return false;
    JsonDocument filter;
    filter["id"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    const bool ok = parse(doc, body, length, filter, api) && doc["id"].is<const char*>();
    if (ok) copy(dest, size, doc["id"]);       // the primary calendar's id is the account's address
    heap_caps_free(body);
    return ok;
}

static int gcl_Calendars(Mtb_Cal_Api_t& api, Mtb_Cal_Calendar_t* out, int max){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, "https://www.googleapis.com/calendar/v3/users/me/calendarList?minAccessRole=freeBusyReader&maxResults=100"
                                      "&fields=items(id,summary,summaryOverride,backgroundColor,primary,selected,hidden)", GCL_LIST_MAX, length);
    if (body == nullptr) return -1;
    JsonDocument filter;
    JsonObject f = filter["items"].add<JsonObject>();
    for (const char* k : {"id", "summary", "summaryOverride", "backgroundColor", "primary", "selected", "hidden"}) f[k] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int n = -1;
    if (parse(doc, body, length, filter, api)){
        n = 0;
        int index = 0;
        // The primary calendar first, then the others in Google's order.
        for (int pass = 0; pass < 2; pass++){
            for (JsonObject c : doc["items"].as<JsonArray>()){
                if (n >= max) break;
                const bool primary = c["primary"] | false;
                if ((pass == 0) != primary || (c["hidden"] | false)) continue;
                const char* id = c["id"] | "";
                if (id[0] == '\0' || strlen(id) >= MTB_CAL_ID) continue;
                Mtb_Cal_Calendar_t& cal = out[n++];
                memset(&cal, 0, sizeof(cal));
                copy(cal.id, sizeof(cal.id), id);
                copy(cal.name, sizeof(cal.name), primary ? "My calendar" : (c["summaryOverride"] | (c["summary"] | id)));
                cal.color = mtb_Cal_Color(c["backgroundColor"] | "", index++);
                cal.primary = primary;
                cal.shown = (c["selected"] | false) || primary;
                cal.holiday = strstr(id, "#holiday@") != nullptr;
            }
        }
    }
    heap_caps_free(body);
    return n;
}

// One flag from the event: how the account answered, if it was invited.
static uint8_t response_Flags(JsonObjectConst e){
    for (JsonObjectConst a : e["attendees"].as<JsonArrayConst>()){
        if (!(a["self"] | false)) continue;
        const char* r = a["responseStatus"] | "";
        if (strcmp(r, "declined") == 0) return MTB_CAL_DECLINED;
        if (strcmp(r, "tentative") == 0 || strcmp(r, "needsAction") == 0) return MTB_CAL_TENTATIVE;
    }
    return 0;
}

static int gcl_Events(Mtb_Cal_Api_t& api, const Mtb_Cal_Calendar_t& cal, uint32_t from, uint32_t to, Mtb_Cal_Data_t& into){
    char* url = (char*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    char* id = (char*)heap_caps_malloc(MTB_CAL_ID * 3, MALLOC_CAP_SPIRAM);
    if (url == nullptr || id == nullptr){ heap_caps_free(url); heap_caps_free(id); copy(api.error, sizeof(api.error), "Out of memory"); return -1; }
    char a[24], b[24];
    iso_Utc(a, sizeof(a), from);
    iso_Utc(b, sizeof(b), to);
    mtb_Kit_Url_Encode(id, MTB_CAL_ID * 3, cal.id);
    // maxAttendees=1 leaves only the account itself among the attendees: its answer, without everyone else's.
    snprintf(url, 1024, "https://www.googleapis.com/calendar/v3/calendars/%s/events?singleEvents=true&orderBy=startTime&maxResults=100"
                        "&maxAttendees=1&timeMin=%s&timeMax=%s&fields=items(summary,location,status,transparency,eventType,start,end,"
                        "hangoutLink,conferenceData/entryPoints/entryPointType,attendees(self,responseStatus))", id, a, b);
    heap_caps_free(id);
    size_t length;
    char* body = mtb_Cal_Api_Get(api, url, GCL_EVENTS_MAX, length);
    heap_caps_free(url);
    if (body == nullptr) return -1;
    JsonDocument filter;
    JsonObject f = filter["items"].add<JsonObject>();
    for (const char* k : {"summary", "location", "status", "transparency", "eventType", "start", "end", "hangoutLink", "conferenceData"}) f[k] = true;
    f["attendees"][0]["self"] = true;
    f["attendees"][0]["responseStatus"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int added = -1;
    if (parse(doc, body, length, filter, api)){
        added = 0;
        for (JsonObjectConst e : doc["items"].as<JsonArrayConst>()){
            if (strcmp(e["status"] | "", "cancelled") == 0) continue;
            const char* type = e["eventType"] | "default";
            if (strcmp(type, "workingLocation") == 0) continue;     // "Home", "Office": not an event to show
            Mtb_Cal_Event_t ev = {};
            bool dateOnlyStart = false, dateOnlyEnd = false;
            const char* s = e["start"]["dateTime"] | (e["start"]["date"] | (const char*)nullptr);
            const char* t = e["end"]["dateTime"] | (e["end"]["date"] | (const char*)nullptr);
            if (!mtb_Cal_Parse_Time(s, ev.start, dateOnlyStart)) continue;
            if (!mtb_Cal_Parse_Time(t, ev.end, dateOnlyEnd)) ev.end = ev.start;
            ev.allDay = dateOnlyStart;
            copy(ev.title, sizeof(ev.title), e["summary"] | "");
            copy(ev.where, sizeof(ev.where), e["location"] | "");
            ev.color = cal.color;
            ev.flags = response_Flags(e);
            if (strcmp(e["transparency"] | "", "transparent") == 0) ev.flags |= MTB_CAL_FREE;
            if (e["hangoutLink"].is<const char*>() || e["conferenceData"]["entryPoints"].size() > 0) ev.flags |= MTB_CAL_ONLINE;
            if (cal.holiday) ev.flags |= MTB_CAL_HOLIDAY | MTB_CAL_FREE;
            if (strcmp(type, "birthday") == 0) ev.flags |= MTB_CAL_BIRTHDAY;
            if (strcmp(type, "outOfOffice") == 0 && ev.title[0] == '\0') copy(ev.title, sizeof(ev.title), "Out of office");
            if (strcmp(type, "focusTime") == 0 && ev.title[0] == '\0') copy(ev.title, sizeof(ev.title), "Focus time");
            ev.key = mtb_Cal_Key(ev.title, ev.start);
            if (mtb_Cal_Add_Event(into, ev)) added++;
        }
    }
    heap_caps_free(body);
    return added;
}

static int gcl_Task_Lists(Mtb_Cal_Api_t& api, Mtb_Cal_Pick_t* out, int max){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, "https://tasks.googleapis.com/tasks/v1/users/@me/lists?maxResults=100&fields=items(id,title)", GCL_LIST_MAX, length);
    if (body == nullptr) return -1;
    JsonDocument filter;
    filter["items"][0]["id"] = true;
    filter["items"][0]["title"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int n = -1;
    if (parse(doc, body, length, filter, api)){
        n = 0;
        for (JsonObjectConst l : doc["items"].as<JsonArrayConst>()){
            if (n >= max) break;
            const char* id = l["id"] | "";
            if (id[0] == '\0' || strlen(id) >= MTB_CAL_ID) continue;
            copy(out[n].id, sizeof(out[n].id), id);
            copy(out[n].name, sizeof(out[n].name), l["title"] | "Tasks");
            n++;
        }
    }
    heap_caps_free(body);
    return n;
}

static int gcl_Tasks(Mtb_Cal_Api_t& api, const char* listId, Mtb_Cal_Data_t& into){
    char* url = (char*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    char* id = (char*)heap_caps_malloc(MTB_CAL_ID * 3, MALLOC_CAP_SPIRAM);
    if (url == nullptr || id == nullptr){ heap_caps_free(url); heap_caps_free(id); copy(api.error, sizeof(api.error), "Out of memory"); return -1; }
    mtb_Kit_Url_Encode(id, MTB_CAL_ID * 3, listId && listId[0] ? listId : "@default");
    snprintf(url, 1024, "https://tasks.googleapis.com/tasks/v1/lists/%s/tasks?showCompleted=false&showHidden=false&maxResults=100"
                        "&fields=items(title,due,status,parent)", id);
    heap_caps_free(id);
    size_t length;
    char* body = mtb_Cal_Api_Get(api, url, GCL_LIST_MAX, length);
    heap_caps_free(url);
    if (body == nullptr) return -1;
    JsonDocument filter;
    for (const char* k : {"title", "due", "status", "parent"}) filter["items"][0][k] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int added = -1;
    if (parse(doc, body, length, filter, api)){
        added = 0;
        for (JsonObjectConst t : doc["items"].as<JsonArrayConst>()){
            if (strcmp(t["status"] | "", "completed") == 0 || t["parent"].is<const char*>()) continue;   // subtasks: under their task
            Mtb_Cal_Task_t task = {};
            copy(task.title, sizeof(task.title), t["title"] | "");
            task.dueDay = -1;
            // "due" is a date at midnight UTC ("2026-11-09T00:00:00.000Z"): the date, in any time zone.
            const char* due = t["due"] | (const char*)nullptr;
            int y, m, d;
            if (due && sscanf(due, "%4d-%2d-%2d", &y, &m, &d) == 3) task.dueDay = mtb_Kit_Days_From_Civil(y, m, d);
            if (mtb_Cal_Add_Task(into, task)) added++;
        }
    }
    heap_caps_free(body);
    return added;
}

// Google's public holiday calendars: "en.<country>#holiday@group.v.calendar.google.com".
static bool gcl_Holidays(const char* country, const char* name, Mtb_Cal_Calendar_t& out){
    if (country == nullptr || strlen(country) != 2) return false;
    memset(&out, 0, sizeof(out));
    snprintf(out.id, sizeof(out.id), "en.%s#holiday@group.v.calendar.google.com", country);
    snprintf(out.name, sizeof(out.name), "%s holidays", name && name[0] ? name : country);
    out.color = MTB_KIT_RGB(11, 128, 67);
    out.holiday = 1;
    return true;
}

const Mtb_Cal_Provider_t GCL_PROVIDER = {
    "Google", &GCL_OAUTH, gcl_Account, gcl_Calendars, gcl_Events, gcl_Task_Lists, gcl_Tasks, nullptr, gcl_Holidays
};

const Mtb_Cal_Country_t GCL_COUNTRIES[] = {
    {"ar", "Argentina"}, {"au", "Australia"}, {"at", "Austria"}, {"bd", "Bangladesh"}, {"be", "Belgium"}, {"br", "Brazil"},
    {"bg", "Bulgaria"}, {"cm", "Cameroon"}, {"ca", "Canada"}, {"cl", "Chile"}, {"cn", "China"}, {"co", "Colombia"},
    {"hr", "Croatia"}, {"cz", "Czechia"}, {"dk", "Denmark"}, {"eg", "Egypt"}, {"et", "Ethiopia"}, {"fi", "Finland"},
    {"fr", "France"}, {"de", "Germany"}, {"gh", "Ghana"}, {"gr", "Greece"}, {"hk", "Hong Kong"}, {"hu", "Hungary"},
    {"in", "India"}, {"id", "Indonesia"}, {"ie", "Ireland"}, {"il", "Israel"}, {"it", "Italy"}, {"ci", "Ivory Coast"},
    {"jm", "Jamaica"}, {"jp", "Japan"}, {"ke", "Kenya"}, {"my", "Malaysia"}, {"mx", "Mexico"}, {"ma", "Morocco"},
    {"nl", "Netherlands"}, {"nz", "New Zealand"}, {"ng", "Nigeria"}, {"no", "Norway"}, {"pk", "Pakistan"}, {"pe", "Peru"},
    {"ph", "Philippines"}, {"pl", "Poland"}, {"pt", "Portugal"}, {"ro", "Romania"}, {"rw", "Rwanda"}, {"sa", "Saudi Arabia"},
    {"sn", "Senegal"}, {"sg", "Singapore"}, {"za", "South Africa"}, {"kr", "South Korea"}, {"es", "Spain"}, {"se", "Sweden"},
    {"ch", "Switzerland"}, {"tw", "Taiwan"}, {"tz", "Tanzania"}, {"th", "Thailand"}, {"tr", "Turkey"}, {"ug", "Uganda"},
    {"ua", "Ukraine"}, {"ae", "United Arab Emirates"}, {"gb", "United Kingdom"}, {"us", "United States"}, {"vn", "Vietnam"},
    {"zm", "Zambia"}, {"zw", "Zimbabwe"},
};
const int GCL_COUNTRY_COUNT = sizeof(GCL_COUNTRIES) / sizeof(GCL_COUNTRIES[0]);
