// Microsoft Graph for meterbit_cal_kit: Outlook calendars, Microsoft To Do and the inbox's unread count, for personal
// (outlook.com) and work or school accounts. The phone links with Calendars.Read, Tasks.Read, Mail.ReadBasic and
// User.Read; Pixlpal refreshes with ".default", which is whatever the user agreed to.
#include "olk_Provider.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define OLK_UA          "PIXLPAL-L2/1.0 (Pixlpal Outlook Calendar)"
#define OLK_LIST_MAX    (64 * 1024)
#define OLK_EVENTS_MAX  (128 * 1024)
#define OLK_GRAPH       "https://graph.microsoft.com/v1.0"

// A public client (the phone app's registration): no secret.
static const Mtb_Kit_OAuth_Provider_t OLK_OAUTH = {
    "https://login.microsoftonline.com/common/oauth2/v2.0/token", CONFIG_PXP_OAUTH_MICROSOFT_CLIENT_ID, nullptr,
    "https://graph.microsoft.com/.default offline_access", "olkToken", OLK_UA, nullptr
};

static const char* const UTC_HEADER[] = {"Prefer", "outlook.timezone=\"UTC\"", nullptr};

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
        copy(api.error, sizeof(api.error), "Microsoft's answer was not readable");
        return false;
    }
    return true;
}

static bool olk_Account(Mtb_Cal_Api_t& api, char* dest, size_t size){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, OLK_GRAPH "/me?$select=mail,userPrincipalName,displayName", 8192, length);
    if (body == nullptr) return false;
    JsonDocument filter;
    filter["mail"] = true;
    filter["userPrincipalName"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    bool ok = parse(doc, body, length, filter, api);
    if (ok){
        const char* who = doc["mail"] | (doc["userPrincipalName"] | (const char*)nullptr);
        ok = who != nullptr;
        if (ok) copy(dest, size, who);
    }
    heap_caps_free(body);
    return ok;
}

static int olk_Calendars(Mtb_Cal_Api_t& api, Mtb_Cal_Calendar_t* out, int max){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, OLK_GRAPH "/me/calendars?$top=50&$select=id,name,hexColor,isDefaultCalendar", OLK_LIST_MAX, length);
    if (body == nullptr) return -1;
    JsonDocument filter;
    for (const char* k : {"id", "name", "hexColor", "isDefaultCalendar"}) filter["value"][0][k] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int n = -1;
    if (parse(doc, body, length, filter, api)){
        n = 0;
        int index = 0;
        for (int pass = 0; pass < 2; pass++){
            for (JsonObjectConst c : doc["value"].as<JsonArrayConst>()){
                if (n >= max) break;
                const bool primary = c["isDefaultCalendar"] | false;
                if ((pass == 0) != primary) continue;
                const char* id = c["id"] | "";
                if (id[0] == '\0' || strlen(id) >= MTB_CAL_ID) continue;
                Mtb_Cal_Calendar_t& cal = out[n++];
                memset(&cal, 0, sizeof(cal));
                copy(cal.id, sizeof(cal.id), id);
                copy(cal.name, sizeof(cal.name), c["name"] | "Calendar");
                cal.color = mtb_Cal_Color(c["hexColor"] | "", index++);
                cal.primary = primary;
                cal.shown = 1;                  // Outlook shows every calendar the account has
                cal.holiday = strstr(cal.name, "oliday") != nullptr;
            }
        }
    }
    heap_caps_free(body);
    return n;
}

static int olk_Events(Mtb_Cal_Api_t& api, const Mtb_Cal_Calendar_t& cal, uint32_t from, uint32_t to, Mtb_Cal_Data_t& into){
    char* url = (char*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    char* id = (char*)heap_caps_malloc(MTB_CAL_ID * 3, MALLOC_CAP_SPIRAM);
    if (url == nullptr || id == nullptr){ heap_caps_free(url); heap_caps_free(id); copy(api.error, sizeof(api.error), "Out of memory"); return -1; }
    char a[24], b[24];
    iso_Utc(a, sizeof(a), from);
    iso_Utc(b, sizeof(b), to);
    mtb_Kit_Url_Encode(id, MTB_CAL_ID * 3, cal.id);
    const int n = snprintf(url, 1024, OLK_GRAPH "/me/calendars/%s/calendarView?startDateTime=%s&endDateTime=%s&$top=100&$orderby=start/dateTime"
                                      "&$select=subject,start,end,isAllDay,location,showAs,isCancelled,responseStatus,isOnlineMeeting", id, a, b);
    heap_caps_free(id);
    if (n >= 1024){ heap_caps_free(url); copy(api.error, sizeof(api.error), "Calendar id too long"); return -1; }
    size_t length;
    char* body = mtb_Cal_Api_Get(api, url, OLK_EVENTS_MAX, length, UTC_HEADER);
    heap_caps_free(url);
    if (body == nullptr) return -1;
    JsonDocument filter;
    JsonObject f = filter["value"].add<JsonObject>();
    for (const char* k : {"subject", "start", "end", "isAllDay", "showAs", "isCancelled", "responseStatus", "isOnlineMeeting"}) f[k] = true;
    f["location"]["displayName"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int added = -1;
    if (parse(doc, body, length, filter, api)){
        added = 0;
        for (JsonObjectConst e : doc["value"].as<JsonArrayConst>()){
            if (e["isCancelled"] | false) continue;
            Mtb_Cal_Event_t ev = {};
            const bool allDay = e["isAllDay"] | false;
            const char* s = e["start"]["dateTime"] | (const char*)nullptr;
            const char* t = e["end"]["dateTime"] | (const char*)nullptr;
            bool dateOnly;
            if (s == nullptr) continue;
            if (allDay){
                // Midnight to midnight of the event's own dates: the dates, in Pixlpal's time zone.
                char day[11];
                copy(day, sizeof(day), s);
                if (!mtb_Cal_Parse_Time(day, ev.start, dateOnly)) continue;
                copy(day, sizeof(day), t ? t : s);
                if (!mtb_Cal_Parse_Time(day, ev.end, dateOnly)) ev.end = ev.start + 86400;
                ev.allDay = 1;
            } else {
                if (!mtb_Cal_Parse_Time(s, ev.start, dateOnly)) continue;
                if (!t || !mtb_Cal_Parse_Time(t, ev.end, dateOnly)) ev.end = ev.start;
            }
            copy(ev.title, sizeof(ev.title), e["subject"] | "");
            copy(ev.where, sizeof(ev.where), e["location"]["displayName"] | "");
            ev.color = cal.color;
            const char* response = e["responseStatus"]["response"] | "";
            if (strcmp(response, "declined") == 0) ev.flags |= MTB_CAL_DECLINED;
            else if (strcmp(response, "tentativelyAccepted") == 0 || strcmp(response, "notResponded") == 0) ev.flags |= MTB_CAL_TENTATIVE;
            if (strcmp(e["showAs"] | "", "free") == 0) ev.flags |= MTB_CAL_FREE;
            if (e["isOnlineMeeting"] | false) ev.flags |= MTB_CAL_ONLINE;
            if (cal.holiday) ev.flags |= MTB_CAL_HOLIDAY | MTB_CAL_FREE;
            ev.key = mtb_Cal_Key(ev.title, ev.start);
            if (mtb_Cal_Add_Event(into, ev)) added++;
        }
    }
    heap_caps_free(body);
    return added;
}

// All To Do lists; @p defaultId gets the default list's ("Tasks").
static int olk_Lists(Mtb_Cal_Api_t& api, Mtb_Cal_Pick_t* out, int max, char* defaultId, size_t defaultSize){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, OLK_GRAPH "/me/todo/lists?$top=100&$select=id,displayName,wellknownListName", OLK_LIST_MAX, length);
    if (body == nullptr) return -1;
    JsonDocument filter;
    for (const char* k : {"id", "displayName", "wellknownListName"}) filter["value"][0][k] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int n = -1;
    if (parse(doc, body, length, filter, api)){
        n = 0;
        for (JsonObjectConst l : doc["value"].as<JsonArrayConst>()){
            const char* id = l["id"] | "";
            if (id[0] == '\0' || strlen(id) >= MTB_CAL_ID) continue;
            if (defaultId && strcmp(l["wellknownListName"] | "", "defaultList") == 0) copy(defaultId, defaultSize, id);
            if (out && n < max){
                copy(out[n].id, sizeof(out[n].id), id);
                copy(out[n].name, sizeof(out[n].name), l["displayName"] | "Tasks");
                n++;
            }
        }
    }
    heap_caps_free(body);
    return n;
}

static int olk_Task_Lists(Mtb_Cal_Api_t& api, Mtb_Cal_Pick_t* out, int max){ return olk_Lists(api, out, max, nullptr, 0); }

static int olk_Tasks(Mtb_Cal_Api_t& api, const char* listId, Mtb_Cal_Data_t& into){
    char* list = (char*)heap_caps_calloc(1, MTB_CAL_ID, MALLOC_CAP_SPIRAM);
    char* url = (char*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    char* id = (char*)heap_caps_malloc(MTB_CAL_ID * 3, MALLOC_CAP_SPIRAM);
    int added = -1;
    if (list && url && id){
        if (listId && listId[0]) copy(list, MTB_CAL_ID, listId);
        else if (olk_Lists(api, nullptr, 0, list, MTB_CAL_ID) < 0) list[0] = '\0';
        if (list[0] == '\0'){ if (api.error[0] == '\0') copy(api.error, sizeof(api.error), "No To Do list"); }
        else {
            mtb_Kit_Url_Encode(id, MTB_CAL_ID * 3, list);
            snprintf(url, 1024, OLK_GRAPH "/me/todo/lists/%s/tasks?$top=100&$filter=status%%20ne%%20'completed'"
                                "&$select=title,dueDateTime,importance,status", id);
            size_t length;
            char* body = mtb_Cal_Api_Get(api, url, OLK_LIST_MAX, length);
            if (body){
                JsonDocument filter;
                for (const char* k : {"title", "dueDateTime", "importance", "status"}) filter["value"][0][k] = true;
                JsonDocument doc(mtb_Kit_Json_Allocator());
                if (parse(doc, body, length, filter, api)){
                    added = 0;
                    for (JsonObjectConst t : doc["value"].as<JsonArrayConst>()){
                        if (strcmp(t["status"] | "", "completed") == 0) continue;
                        Mtb_Cal_Task_t task = {};
                        copy(task.title, sizeof(task.title), t["title"] | "");
                        task.important = strcmp(t["importance"] | "", "high") == 0;
                        task.dueDay = -1;
                        // A due date is midnight in the user's zone, given in UTC: midnight UTC is the date itself,
                        // anything else is the instant to convert.
                        const char* due = t["dueDateTime"]["dateTime"] | (const char*)nullptr;
                        int y, m, d, h = 0, mi = 0;
                        if (due && sscanf(due, "%4d-%2d-%2dT%2d:%2d", &y, &m, &d, &h, &mi) >= 3){
                            if (h == 0 && mi == 0) task.dueDay = mtb_Kit_Days_From_Civil(y, m, d);
                            else {
                                uint32_t when;
                                bool dateOnly;
                                if (mtb_Cal_Parse_Time(due, when, dateOnly)) task.dueDay = mtb_Cal_Local_Day(when);
                            }
                        }
                        if (mtb_Cal_Add_Task(into, task)) added++;
                    }
                }
                heap_caps_free(body);
            }
        }
    } else copy(api.error, sizeof(api.error), "Out of memory");
    heap_caps_free(list);
    heap_caps_free(url);
    heap_caps_free(id);
    return added;
}

static int olk_Unread(Mtb_Cal_Api_t& api){
    size_t length;
    char* body = mtb_Cal_Api_Get(api, OLK_GRAPH "/me/mailFolders/inbox?$select=unreadItemCount", 4096, length);
    if (body == nullptr) return -1;
    JsonDocument filter;
    filter["unreadItemCount"] = true;
    JsonDocument doc(mtb_Kit_Json_Allocator());
    int n = -1;
    if (parse(doc, body, length, filter, api) && doc["unreadItemCount"].is<int>()) n = doc["unreadItemCount"].as<int>();
    heap_caps_free(body);
    return n;
}

const Mtb_Cal_Provider_t OLK_PROVIDER = {
    "Microsoft", &OLK_OAUTH, olk_Account, olk_Calendars, olk_Events, olk_Task_Lists, olk_Tasks, olk_Unread, nullptr
};
