#include "cmt_Model.h"
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    while (k > 0 && s[k] && ((unsigned char)s[k] & 0xC0) == 0x80) k--;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

const char* cmt_Place_Name(int i){
    static const char* const NAMES[CMT_PLACES] = {"Home", "Work", "School", "Other"};
    return i >= 0 && i < CMT_PLACES ? NAMES[i] : "Place";
}

void cmt_Settings_Default(Cmt_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = CMT_SETTINGS_MAGIC;
    s.plan = CMT_PLAN_EXPLORER;
    s.paygCap = 10000;
    s.googleCap = 5000;
    s.hour24 = 1;
    s.chime = 1;
    s.alertMinutes = 5;
}

void cmt_Settings_Clamp(Cmt_Settings_t& s){
    if (s.drive > CMT_GOOGLE) s.drive = CMT_TOMTOM;
    if (s.plan > CMT_PLAN_PAYG) s.plan = CMT_PLAN_EXPLORER;
    if (s.paygCap < 1000) s.paygCap = 1000;
    if (s.googleCap > 60000) s.googleCap = 60000;
    s.hour24 = s.hour24 ? 1 : 0;
    s.chime = s.chime ? 1 : 0;
    s.miles = s.miles ? 1 : 0;
    s.avoidTolls = s.avoidTolls ? 1 : 0;
    s.avoidHighways = s.avoidHighways ? 1 : 0;
    if (s.alertMinutes > 30) s.alertMinutes = 30;
    if (s.editing >= CMT_BOARDS) s.editing = 0;
    for (Cmt_Place_t& p : s.places){
        p.set = p.set ? 1 : 0;
        p.address[sizeof(p.address) - 1] = '\0';
        if (p.latE6 < -90000000 || p.latE6 > 90000000 || p.lonE6 < -180000000 || p.lonE6 > 180000000) p.set = 0;
    }
    for (Cmt_Board_t& b : s.boards){
        b.used = b.used ? 1 : 0;
        if (b.kind > CMT_DRIVE_BOARD) b.kind = CMT_DEPARTURES_BOARD;
        b.label[sizeof(b.label) - 1] = b.stopKey[sizeof(b.stopKey) - 1] = b.stopName[sizeof(b.stopName) - 1] = b.lines[sizeof(b.lines) - 1] = '\0';
        if (b.walkMinutes > 60) b.walkMinutes = 60;
        if (b.from >= CMT_PLACES) b.from = 0;
        if (b.to >= CMT_PLACES) b.to = 1;
        if (b.arriveBy >= 1440) b.arriveBy = 0xFFFF;
        if (b.days > CMT_DAYS_WEEKENDS) b.days = CMT_DAYS_ALL;
        if (b.fromMinute >= 1440) b.fromMinute = 0;
        if (b.toMinute >= 1440) b.toMinute = 0;
        b.alert = b.alert ? 1 : 0;
    }
    s.transitKey[CMT_KEY_LEN - 1] = s.tomtomKey[CMT_KEY_LEN - 1] = s.googleKey[CMT_KEY_LEN - 1] = '\0';
}

int cmt_Board_Count(const Cmt_Settings_t& s){
    int n = 0;
    for (const Cmt_Board_t& b : s.boards) if (b.used) n++;
    return n;
}

// Active times ----------------------------------------------------------------------------------------------------
static bool day_Ok(uint8_t days, int weekday){
    if (days == CMT_DAYS_WEEKDAYS) return weekday < 5;
    if (days == CMT_DAYS_WEEKENDS) return weekday >= 5;
    return true;
}

bool cmt_Active(const Cmt_Board_t& b, int weekday, int minute){
    if (!b.used || !day_Ok(b.days, weekday)) return false;
    if (b.fromMinute == b.toMinute) return true;
    if (b.fromMinute < b.toMinute) return minute >= b.fromMinute && minute < b.toMinute;
    return minute >= b.fromMinute || minute < b.toMinute;
}

static int days_In(int y, int m){
    static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[(m - 1) % 12];
}

// Minutes of [start, 1440) inside the window.
static int window_Minutes(const Cmt_Board_t& b, int start){
    int n = 0;
    for (int m = start; m < 1440; m++){
        bool in;
        if (b.fromMinute == b.toMinute) in = true;
        else if (b.fromMinute < b.toMinute) in = m >= b.fromMinute && m < b.toMinute;
        else in = m >= b.fromMinute || m < b.toMinute;
        if (in) n++;
    }
    return n;
}

uint32_t cmt_Active_Seconds_Left(const Cmt_Board_t& b, int year, int month, int day, int weekday, int minute){
    if (!b.used) return 0;
    uint32_t seconds = 0;
    const int last = days_In(year, month);
    for (int d = day, w = weekday; d <= last; d++, w = (w + 1) % 7){
        if (!day_Ok(b.days, w)) continue;
        seconds += (uint32_t)window_Minutes(b, d == day ? minute : 0) * 60;
    }
    return seconds;
}

// Budgets ---------------------------------------------------------------------------------------------------------
uint32_t cmt_Transit_Monthly(const Cmt_Settings_t& s){
    // A little is kept back for searches from the phone.
    if (s.plan == CMT_PLAN_HOBBYIST) return 50000 - 1000;
    if (s.plan == CMT_PLAN_PAYG) return s.paygCap > 200 ? s.paygCap - 200u : s.paygCap;
    return 5000 - 300;
}

uint32_t cmt_Route_Monthly(const Cmt_Settings_t& s){
    if (s.drive == CMT_GOOGLE) return s.googleCap;
    return 20000 - 500;                 // TomTom Routing's free month
}

uint32_t cmt_Interval(uint32_t left, uint32_t activeSecondsLeft, int consumers, uint32_t floorSec){
    if (left == 0) return 0;
    if (consumers < 1) consumers = 1;
    const uint64_t perRequest = (uint64_t)activeSecondsLeft * consumers / left;
    if (perRequest < floorSec) return floorSec;
    return perRequest > 6 * 3600 ? 6 * 3600 : (uint32_t)perRequest;
}

void cmt_Usage_Roll(Cmt_Usage_t& u, uint32_t yyyymm){
    if (u.month != yyyymm){ memset(&u, 0, sizeof(u)); u.month = yyyymm; }
}

// Times ------------------------------------------------------------------------------------------------------------
static int64_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// "2026-11-08T08:31:00Z" or with an offset.
static uint32_t iso_Time(const char* t){
    if (t == nullptr) return 0;
    int y, mo, d, h, mi, s = 0;
    if (sscanf(t, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) < 5) return 0;
    const char* z = t + 16;
    if (*z == ':') z += 3;
    while (*z == '.' || isdigit((unsigned char)*z)) z++;
    int32_t offset = 0;
    if (*z == '+' || *z == '-'){
        int oh = 0, om = 0;
        sscanf(z + 1, "%2d:%2d", &oh, &om);
        offset = (oh * 3600 + om * 60) * (*z == '+' ? 1 : -1);
    }
    const int64_t u = days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - offset;
    return u > 0 ? (uint32_t)u : 0;
}

uint16_t cmt_Color(const char* hex, uint16_t fallback){
    if (hex == nullptr) return fallback;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return fallback;
    for (int i = 0; i < 6; i++) if (!isxdigit((unsigned char)hex[i])) return fallback;
    const unsigned long v = strtoul(hex, nullptr, 16);
    return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

// Departures ---------------------------------------------------------------------------------------------------
static bool line_Wanted(const char* lines, const char* line){
    if (lines == nullptr || lines[0] == '\0') return true;
    char list[64];
    copy(list, sizeof(list), lines);
    for (char* t = strtok(list, ","); t; t = strtok(nullptr, ",")){
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n - 1] == ' ') t[--n] = '\0';
        if (strcasecmp(t, line) == 0) return true;
    }
    return false;
}

static void line_Name(JsonObjectConst route, char* dest, size_t size){
    const char* s = route["route_short_name"] | "";
    if (s[0]) copy(dest, size, s);
    else copy(dest, size, route["route_long_name"] | "?");
}

static void add_Departures(JsonArrayConst list, const char* lines, uint32_t now, Cmt_Departure_t* out, int& n, int max){
    for (JsonObjectConst d : list){
        JsonObjectConst route = d["trip"]["route"];
        char line[8];
        line_Name(route, line, sizeof(line));
        if (!line_Wanted(lines, line)) continue;
        const uint32_t scheduled = iso_Time(d["departure"]["scheduled_utc"] | (const char*)nullptr);
        const uint32_t estimated = iso_Time(d["departure"]["estimated_utc"] | (const char*)nullptr);
        const uint32_t t = estimated ? estimated : scheduled;
        if (t == 0 || t + 30 < now) continue;           // gone (a little grace for the doors)
        Cmt_Departure_t x = {};
        x.time = t;
        x.realtime = estimated != 0;
        x.delay = estimated && scheduled ? (int32_t)(estimated - scheduled) : 0;
        const char* rel = d["schedule_relationship"] | "";
        const char* relTrip = d["trip"]["schedule_relationship"] | "";
        x.cancelled = strcmp(rel, "CANCELED") == 0 || strcmp(relTrip, "CANCELED") == 0 || strcmp(rel, "SKIPPED") == 0;
        x.color = cmt_Color(route["route_color"] | "", RGB(70, 110, 200));
        x.textColor = cmt_Color(route["route_text_color"] | "", RGB(255, 255, 255));
        copy(x.line, sizeof(x.line), line);
        const char* head = d["trip"]["trip_headsign"] | "";
        if (!head[0]) head = d["stop_headsign"] | "";
        if (!head[0]) head = route["route_long_name"] | "";
        copy(x.headsign, sizeof(x.headsign), head);
        // Soonest first; when full, a later one gives way.
        int at = n;
        while (at > 0 && out[at - 1].time > x.time) at--;
        if (at >= max) continue;
        if (n >= max) n = max - 1;
        memmove(&out[at + 1], &out[at], sizeof(out[0]) * (n - at));
        out[at] = x;
        n++;
    }
}

static void departures_Filter(JsonDocument& f){
    JsonObject d = f["stops"][0]["departures"][0].to<JsonObject>();
    d["departure"]["scheduled_utc"] = true;
    d["departure"]["estimated_utc"] = true;
    d["schedule_relationship"] = true;
    d["stop_headsign"] = true;
    d["trip"]["trip_headsign"] = true;
    d["trip"]["schedule_relationship"] = true;
    for (const char* k : {"route_short_name", "route_long_name", "route_color", "route_text_color"}) d["trip"]["route"][k] = true;
    f["stops"][0]["children"][0]["departures"] = f["stops"][0]["departures"];
}

int cmt_Parse_Departures(const char* json, size_t length, const char* lines, uint32_t now, Cmt_Departure_t* out, int max){
    JsonDocument filter;
    departures_Filter(filter);
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return -1;
    int n = 0;
    for (JsonObjectConst stop : doc["stops"].as<JsonArrayConst>()){
        add_Departures(stop["departures"].as<JsonArrayConst>(), lines, now, out, n, max);
        for (JsonObjectConst child : stop["children"].as<JsonArrayConst>()) add_Departures(child["departures"].as<JsonArrayConst>(), lines, now, out, n, max);
    }
    return n;
}

int cmt_Parse_Lines(const char* json, size_t length, char (*lines)[64], int max){
    JsonDocument filter;
    departures_Filter(filter);
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return -1;
    int n = 0;
    auto take = [&](JsonArrayConst list){
        for (JsonObjectConst d : list){
            char line[8];
            line_Name(d["trip"]["route"], line, sizeof(line));
            bool seen = false;
            for (int i = 0; i < n; i++) if (strncmp(lines[i], line, strlen(line)) == 0 && lines[i][strlen(line)] == ' ') seen = true;
            if (seen || n >= max) continue;
            const char* head = d["trip"]["trip_headsign"] | (d["stop_headsign"] | "");
            snprintf(lines[n++], 64, "%s %s%.50s", line, head[0] ? "to " : "", head);
        }
    };
    for (JsonObjectConst stop : doc["stops"].as<JsonArrayConst>()){
        take(stop["departures"].as<JsonArrayConst>());
        for (JsonObjectConst child : stop["children"].as<JsonArrayConst>()) take(child["departures"].as<JsonArrayConst>());
    }
    return n;
}

int cmt_Parse_Stops(const char* json, size_t length, Cmt_Stop_t* out, int max){
    JsonDocument filter;
    JsonObject f = filter["stops"][0].to<JsonObject>();
    for (const char* k : {"onestop_id", "stop_id", "stop_name", "stop_code", "location_type"}) f[k] = true;
    f["feed_version"]["feed"]["onestop_id"] = true;
    f["place"]["adm1_name"] = true;
    f["place"]["adm0_name"] = true;
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return -1;
    int n = 0;
    for (JsonObjectConst s : doc["stops"].as<JsonArrayConst>()){
        if (n >= max) break;
        const int type = s["location_type"] | 0;
        if (type != 0 && type != 1) continue;          // stops and stations, not entrances
        Cmt_Stop_t& x = out[n];
        memset(&x, 0, sizeof(x));
        const char* onestop = s["onestop_id"] | "";
        if (onestop[0]) copy(x.key, sizeof(x.key), onestop);
        else snprintf(x.key, sizeof(x.key), "%s:%s", s["feed_version"]["feed"]["onestop_id"] | "", s["stop_id"] | "");
        copy(x.name, sizeof(x.name), s["stop_name"] | "Stop");
        const char* area = s["place"]["adm1_name"] | (s["place"]["adm0_name"] | "");
        const char* code = s["stop_code"] | "";
        snprintf(x.detail, sizeof(x.detail), "%s%s%s%s%.30s", type == 1 ? "Station" : "Stop", code[0] ? " " : "", code, area[0] ? ", " : "", area);
        if (x.key[0] && strcmp(x.key, ":") != 0) n++;
    }
    return n;
}

// Drives -----------------------------------------------------------------------------------------------------------
bool cmt_Parse_TomTom_Route(const char* json, size_t length, Cmt_Drive_Result_t& r){
    memset(&r, 0, sizeof(r));
    JsonDocument filter;
    JsonObject s = filter["routes"][0]["summary"].to<JsonObject>();
    for (const char* k : {"lengthInMeters", "travelTimeInSeconds", "noTrafficTravelTimeInSeconds", "historicTrafficTravelTimeInSeconds", "trafficDelayInSeconds"}) s[k] = true;
    filter["routes"][0]["sections"][0]["sectionType"] = true;
    filter["routes"][0]["sections"][0]["streetName"]["text"] = true;
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
    JsonObjectConst sum = doc["routes"][0]["summary"];
    if (!sum["travelTimeInSeconds"].is<long>()) return false;
    r.seconds = sum["travelTimeInSeconds"] | 0u;
    r.meters = sum["lengthInMeters"] | 0u;
    if (sum["noTrafficTravelTimeInSeconds"].is<long>()) r.normalSeconds = sum["noTrafficTravelTimeInSeconds"] | 0u;
    else if (sum["trafficDelayInSeconds"].is<long>()) r.normalSeconds = r.seconds - (uint32_t)(sum["trafficDelayInSeconds"] | 0);
    else r.normalSeconds = r.seconds;
    // Main roads: the first two named important stretches.
    size_t used = 0;
    int names = 0;
    for (JsonObjectConst sec : doc["routes"][0]["sections"].as<JsonArrayConst>()){
        if (strcmp(sec["sectionType"] | "", "IMPORTANT_ROAD_STRETCH") != 0) continue;
        const char* name = sec["streetName"]["text"] | "";
        if (!name[0] || strstr(r.via, name)) continue;
        used += (size_t)snprintf(r.via + used, sizeof(r.via) - used, "%s%s", used ? ", " : "", name);
        if (++names >= 2 || used >= sizeof(r.via)) break;
    }
    r.ok = r.seconds > 0;
    return r.ok;
}

static uint32_t google_Seconds(const char* d){ return d ? (uint32_t)strtoul(d, nullptr, 10) : 0; }   // "1234s"

bool cmt_Parse_Google_Route(const char* json, size_t length, Cmt_Drive_Result_t& r){
    memset(&r, 0, sizeof(r));
    JsonDocument filter;
    for (const char* k : {"duration", "staticDuration", "distanceMeters", "description"}) filter["routes"][0][k] = true;
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
    JsonObjectConst route = doc["routes"][0];
    r.seconds = google_Seconds(route["duration"] | (const char*)nullptr);
    r.normalSeconds = google_Seconds(route["staticDuration"] | (const char*)nullptr);
    if (r.normalSeconds == 0) r.normalSeconds = r.seconds;
    r.meters = route["distanceMeters"] | 0u;
    copy(r.via, sizeof(r.via), route["description"] | "");
    r.ok = r.seconds > 0;
    return r.ok;
}

int cmt_Parse_TomTom_Geocode(const char* json, size_t length, Cmt_Address_t* out, int max){
    JsonDocument filter;
    filter["results"][0]["address"]["freeformAddress"] = true;
    filter["results"][0]["poi"]["name"] = true;
    filter["results"][0]["position"] = true;
    JsonDocument doc;
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return -1;
    int n = 0;
    for (JsonObjectConst x : doc["results"].as<JsonArrayConst>()){
        if (n >= max) break;
        if (!x["position"]["lat"].is<float>()) continue;
        Cmt_Address_t& a = out[n++];
        const char* poi = x["poi"]["name"] | "";
        if (poi[0]) snprintf(a.text, sizeof(a.text), "%.30s, %.39s", poi, x["address"]["freeformAddress"] | "");
        else copy(a.text, sizeof(a.text), x["address"]["freeformAddress"] | "");
        a.latE6 = (int32_t)lround((x["position"]["lat"] | 0.0) * 1e6);
        a.lonE6 = (int32_t)lround((x["position"]["lon"] | 0.0) * 1e6);
    }
    return n;
}

int cmt_Parse_Google_Geocode(const char* json, size_t length, Cmt_Address_t* out, int max, char* status, size_t ss){
    JsonDocument filter;
    filter["status"] = true;
    filter["error_message"] = true;
    filter["results"][0]["formatted_address"] = true;
    filter["results"][0]["geometry"]["location"] = true;
    JsonDocument doc;
    if (status && ss) status[0] = '\0';
    if (json == nullptr || deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return -1;
    if (status) copy(status, ss, doc["status"] | "");
    if (strcmp(doc["status"] | "", "OK") != 0 && strcmp(doc["status"] | "", "ZERO_RESULTS") != 0) return -1;
    int n = 0;
    for (JsonObjectConst x : doc["results"].as<JsonArrayConst>()){
        if (n >= max) break;
        Cmt_Address_t& a = out[n++];
        copy(a.text, sizeof(a.text), x["formatted_address"] | "");
        a.latE6 = (int32_t)lround((x["geometry"]["location"]["lat"] | 0.0) * 1e6);
        a.lonE6 = (int32_t)lround((x["geometry"]["location"]["lng"] | 0.0) * 1e6);
    }
    return n;
}

// Labels ------------------------------------------------------------------------------------------------------------
void cmt_Minutes_Text(char* dest, size_t size, uint32_t t, uint32_t now, bool hour24){
    const int32_t s = (int32_t)(t - now);
    if (s < 45) { copy(dest, size, "NOW"); return; }
    const int m = (s + 30) / 60;
    if (m < 60) { snprintf(dest, size, "%d MIN", m); return; }
    if (m < 120) { snprintf(dest, size, "1 H %02d", m - 60); return; }
    const time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (hour24) snprintf(dest, size, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(dest, size, "%d:%02d%s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "A" : "P");
}

int cmt_Leave_By(uint16_t arriveBy, uint32_t driveSeconds){
    if (arriveBy >= 1440) return -1;
    int m = (int)arriveBy - (int)((driveSeconds + 59) / 60);
    while (m < 0) m += 1440;
    return m;
}
