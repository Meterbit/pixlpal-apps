#include "owm_Model.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <cmath>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define OWM_HOST            "https://api.openweathermap.org"
#define OWM_PRO_HOST        "https://pro.openweathermap.org"
#define OWM_PROBE_EVERY     (24UL * 3600)

void owm_Settings_Default(Owm_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = OWM_SETTINGS_MAGIC;
    snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
    s.latitude = 6.45407;
    s.longitude = 3.39467;
    s.air = 1;
    s.alerts = 1;
    s.oneCall = 1;
    s.pageSeconds = 10;
    s.autoCycle = 1;
}

bool owm_Key_Valid(const char* key){
    if (key == nullptr || strlen(key) != 32) return false;
    for (int i = 0; i < 32; i++) if (!isxdigit((unsigned char)key[i])) return false;
    return true;
}

void owm_Settings_Clamp(Owm_Settings_t& s){
    s.place[sizeof(s.place) - 1] = '\0';
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
    if (!(s.latitude >= -90 && s.latitude <= 90) || !(s.longitude >= -180 && s.longitude <= 180)){
        Owm_Settings_t d;
        owm_Settings_Default(d);
        memcpy(s.place, d.place, sizeof(s.place));
        s.latitude = d.latitude;
        s.longitude = d.longitude;
    }
    if (s.apiKey[0] && !owm_Key_Valid(s.apiKey)) memset(s.apiKey, 0, sizeof(s.apiKey));
    s.imperial = s.imperial ? 1 : 0;
    s.air = s.air ? 1 : 0;
    s.alerts = s.alerts ? 1 : 0;
    s.oneCall = s.oneCall ? 1 : 0;
    if (s.pageSeconds < 4) s.pageSeconds = 4;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    s.autoCycle = s.autoCycle ? 1 : 0;
}

void owm_Kit_Config(const Owm_Settings_t& s, Mtb_Kit_Wx_Config_t& config){
    memset(&config, 0, sizeof(config));
    snprintf(config.place, sizeof(config.place), "%s", s.place);
    config.latitude = s.latitude;
    config.longitude = s.longitude;
    config.imperial = s.imperial;
    config.options = (s.air ? OWM_OPTION_AIR : 0) | (s.alerts ? OWM_OPTION_ALERTS : 0) | (s.oneCall ? OWM_OPTION_ONECALL : 0);
    snprintf(config.apiKey, sizeof(config.apiKey), "%s", s.apiKey);
}

uint8_t owm_Icon_Of(int id){
    if (id >= 200 && id < 300) return KIT_WX_STORM;
    if (id >= 300 && id < 400) return KIT_WX_DRIZZLE;
    if (id == 511 || (id >= 611 && id <= 616)) return KIT_WX_SLEET;
    if (id >= 502 && id <= 504) return KIT_WX_HEAVY_RAIN;
    if (id == 522 || id == 531) return KIT_WX_HEAVY_RAIN;
    if (id >= 500 && id < 600) return KIT_WX_RAIN;
    if (id >= 600 && id < 700) return KIT_WX_SNOW;
    if (id == 771) return KIT_WX_WIND;
    if (id == 781) return KIT_WX_STORM;
    if (id >= 700 && id < 800) return KIT_WX_FOG;
    if (id == 800) return KIT_WX_CLEAR;
    if (id == 801 || id == 802) return KIT_WX_PARTLY;
    if (id == 803 || id == 804) return KIT_WX_CLOUDY;
    return KIT_WX_UNKNOWN;
}

void owm_Plan_Name(uint32_t flags, char* dest, size_t size){
    const char* fixed = (flags & OWM_HAS_HOURLY) ? "Developer or higher" : (flags & OWM_HAS_DAILY) ? "Startup" : "Free";
    snprintf(dest, size, "%s%s", fixed, (flags & OWM_HAS_ONECALL) ? " + One Call" : "");
}

bool owm_One_Call_Active(const Mtb_Kit_Wx_Config_t& c, const Mtb_Kit_Wx_Data_t& d){
    return (c.options & OWM_OPTION_ONECALL) && (d.plan.flags & OWM_HAS_ONECALL);
}

//##############################################################################################################
static size_t owm_Url(char* url, size_t size, const Mtb_Kit_Wx_Config_t& c, const char* host, const char* path, const char* extra){
    return (size_t)snprintf(url, size, "%s%s?lat=%.4f&lon=%.4f&units=%s%s&appid=%s", host, path, c.latitude, c.longitude,
                            c.imperial ? "imperial" : "metric", extra, c.apiKey);
}

bool owm_Request(const Mtb_Kit_Wx_Config_t& c, uint8_t feed, const Mtb_Kit_Wx_Data_t& d, char* url, size_t size, const char**){
    if (!c.apiKey[0]) return false;
    const bool oneCall = owm_One_Call_Active(c, d);
    const uint32_t has = d.plan.flags;
    size_t n = 0;
    switch (feed){
        case OWM_FEED_CURRENT:
            if (oneCall) return false;
            n = owm_Url(url, size, c, OWM_HOST, "/data/2.5/weather", "");
            break;
        case OWM_FEED_ONECALL:
            if (!(c.options & OWM_OPTION_ONECALL)) return false;
            n = owm_Url(url, size, c, OWM_HOST, "/data/3.0/onecall", (c.options & OWM_OPTION_ALERTS) ? "&exclude=minutely" : "&exclude=minutely,alerts");
            break;
        case OWM_FEED_FORECAST:
            // The three-hourly forecast, until something better is on the key.
            if (oneCall || (has & OWM_HAS_HOURLY)) return false;
            n = owm_Url(url, size, c, OWM_HOST, "/data/2.5/forecast", "");
            break;
        case OWM_FEED_DAILY: n = owm_Url(url, size, c, OWM_HOST, "/data/2.5/forecast/daily", "&cnt=16"); break;
        case OWM_FEED_HOURLY: n = owm_Url(url, size, c, OWM_PRO_HOST, "/data/2.5/forecast/hourly", "&cnt=48"); break;
        case OWM_FEED_AIR:
            if (!(c.options & OWM_OPTION_AIR)) return false;
            n = (size_t)snprintf(url, size, OWM_HOST "/data/2.5/air_pollution?lat=%.4f&lon=%.4f&appid=%s", c.latitude, c.longitude, c.apiKey);
            break;
        default: return false;
    }
    return n < size;
}

uint32_t owm_Every(uint8_t feed, const Mtb_Kit_Wx_Config_t& c, const Mtb_Kit_Wx_Data_t& d, uint32_t){
    const uint32_t has = d.plan.flags;
    switch (feed){
        case OWM_FEED_CURRENT: return 600;
        case OWM_FEED_ONECALL: return (has & OWM_HAS_ONECALL) ? OWM_ONECALL_EVERY : OWM_PROBE_EVERY;
        case OWM_FEED_FORECAST: return 3600;
        case OWM_FEED_DAILY: return (has & OWM_HAS_DAILY) ? 3 * 3600 : OWM_PROBE_EVERY;
        case OWM_FEED_HOURLY: return (has & OWM_HAS_HOURLY) ? 3600 : OWM_PROBE_EVERY;
        case OWM_FEED_AIR: return (c.options & OWM_OPTION_AIR) ? 3600 : 0;
        default: return 0;
    }
}

//##############################################################################################################
static float owm_Number(JsonVariantConst v){ return v.is<float>() || v.is<int>() ? v.as<float>() : NAN; }
// Metric wind comes in m/s: shown in km/h.
static float owm_Wind(JsonVariantConst v, const Mtb_Kit_Wx_Config_t& c){ const float w = owm_Number(v); return c.imperial ? w : w * 3.6f; }
static uint8_t owm_Percent(JsonVariantConst pop){ const float p = owm_Number(pop); return std::isnan(p) ? 0 : (uint8_t)lroundf(fminf(fmaxf(p, 0), 1) * 100); }

static void owm_Describe(JsonVariantConst weather, Mtb_Kit_Wx_Current_t& c){
    const int id = weather["id"] | 0;
    c.icon = owm_Icon_Of(id);
    mtb_Kit_To_Ascii(c.text, sizeof(c.text), weather["description"] | "");
    if (c.text[0]) c.text[0] = (char)toupper((unsigned char)c.text[0]);
    const char* icon = weather["icon"] | "";
    if (icon[0]) c.day = icon[strlen(icon) - 1] != 'n';
}

// Today's sun, into the day it belongs to.
static void owm_Sun(Mtb_Kit_Wx_Data_t& w, uint32_t sunrise, uint32_t sunset){
    if (!sunrise) return;
    int16_t y; uint8_t m, d;
    mtb_Kit_Wx_Date_Of(sunrise, w.utcOffset, y, m, d);
    for (uint8_t i = 0; i < w.dayCount; i++){
        if (w.days[i].year == y && w.days[i].month == m && w.days[i].mday == d){ w.days[i].sunrise = sunrise; w.days[i].sunset = sunset; return; }
    }
    if (w.dayCount == 0){
        Mtb_Kit_Wx_Day_t& day = w.days[w.dayCount++];
        memset(&day, 0, sizeof(day));
        day.year = y; day.month = m; day.mday = d;
        day.high = day.low = day.uvMax = day.precip = NAN;
        day.sunrise = sunrise; day.sunset = sunset;
    }
}

static uint8_t owm_Parse_Current(JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& cfg, Mtb_Kit_Wx_Data_t& w){
    JsonVariantConst main = doc["main"];
    if (!main.is<JsonObjectConst>()) return KIT_WX_PARSE;
    w.utcOffset = doc["timezone"] | w.utcOffset;
    Mtb_Kit_Wx_Current_t& c = w.current;
    c.time = doc["dt"] | 0u;
    c.day = 1;
    owm_Describe(doc["weather"][0], c);
    c.temp = owm_Number(main["temp"]);
    c.feels = owm_Number(main["feels_like"]);
    c.humidity = owm_Number(main["humidity"]);
    c.pressure = owm_Number(main["pressure"]);
    c.wind = owm_Wind(doc["wind"]["speed"], cfg);
    c.gust = owm_Wind(doc["wind"]["gust"], cfg);
    c.windDegrees = owm_Number(doc["wind"]["deg"]);
    c.cloud = owm_Number(doc["clouds"]["all"]);
    c.visibility = owm_Number(doc["visibility"]);
    c.uv = NAN;                     // Not in this API: the day's maximum is shown when known.
    c.rainChance = NAN;
    c.valid = !std::isnan(c.temp);
    owm_Sun(w, doc["sys"]["sunrise"] | 0u, doc["sys"]["sunset"] | 0u);
    return c.valid ? KIT_WX_OK : KIT_WX_PARSE;
}

static void owm_Hour_From(JsonVariantConst e, Mtb_Kit_Wx_Hour_t& h, bool oneCall){
    h.start = e["dt"] | 0u;
    h.temp = owm_Number(oneCall ? e["temp"] : e["main"]["temp"]);
    h.rain = owm_Percent(e["pop"]);
    h.icon = owm_Icon_Of(e["weather"][0]["id"] | 0);
    const char* pod = oneCall ? (e["weather"][0]["icon"] | "d") : (e["sys"]["pod"] | "d");
    h.day = pod[0] && pod[strlen(pod) - 1] != 'n';
}

// The 5-day / 3-hour forecast: 16 entries as hours (48 h), and days made from all of them.
static uint8_t owm_Parse_Forecast(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& w){
    JsonArrayConst list = doc["list"];
    if (list.isNull()) return KIT_WX_PARSE;
    w.utcOffset = doc["city"]["timezone"] | w.utcOffset;
    w.hourStep = 3;
    w.hourCount = 0;
    for (JsonVariantConst e : list){
        if (w.hourCount >= 16) break;
        owm_Hour_From(e, w.hours[w.hourCount++], false);
    }
    if (!(w.plan.flags & OWM_HAS_DAILY)){
        Mtb_Kit_Wx_Day_t old[KIT_WX_DAYS];
        const uint8_t oldCount = w.dayCount;
        memcpy(old, w.days, sizeof(old));
        w.dayCount = 0;
        int bestNoon[KIT_WX_DAYS];
        for (JsonVariantConst e : list){
            const uint32_t t = e["dt"] | 0u;
            int16_t y; uint8_t m, d;
            mtb_Kit_Wx_Date_Of(t, w.utcOffset, y, m, d);
            Mtb_Kit_Wx_Day_t* day = w.dayCount ? &w.days[w.dayCount - 1] : nullptr;
            if (day == nullptr || day->year != y || day->month != m || day->mday != d){
                if (w.dayCount >= KIT_WX_DAYS) break;
                day = &w.days[w.dayCount];
                memset(day, 0, sizeof(*day));
                day->year = y; day->month = m; day->mday = d;
                day->high = -1e9f; day->low = 1e9f; day->uvMax = NAN; day->precip = NAN;
                bestNoon[w.dayCount] = 99;
                for (uint8_t k = 0; k < oldCount; k++) if (old[k].year == y && old[k].month == m && old[k].mday == d){ day->sunrise = old[k].sunrise; day->sunset = old[k].sunset; }
                w.dayCount++;
            }
            const float hi = owm_Number(e["main"]["temp_max"]), lo = owm_Number(e["main"]["temp_min"]);
            if (!std::isnan(hi) && hi > day->high) day->high = hi;
            if (!std::isnan(lo) && lo < day->low) day->low = lo;
            const uint8_t rain = owm_Percent(e["pop"]);
            if (rain > day->rain) day->rain = rain;
            // The day's symbol: the one nearest midday.
            const int hour = (int)(((t + w.utcOffset) / 3600) % 24);
            const int distance = hour > 13 ? hour - 13 : 13 - hour;
            if (distance < bestNoon[w.dayCount - 1]){ bestNoon[w.dayCount - 1] = distance; day->icon = owm_Icon_Of(e["weather"][0]["id"] | 0); }
        }
        for (uint8_t i = 0; i < w.dayCount; i++){
            if (w.days[i].high < -1e8f) w.days[i].high = NAN;
            if (w.days[i].low > 1e8f) w.days[i].low = NAN;
        }
    }
    owm_Sun(w, doc["city"]["sunrise"] | 0u, doc["city"]["sunset"] | 0u);
    return w.hourCount ? KIT_WX_OK : KIT_WX_PARSE;
}

static void owm_Day_From(JsonVariantConst e, Mtb_Kit_Wx_Data_t& w, Mtb_Kit_Wx_Day_t& day){
    memset(&day, 0, sizeof(day));
    mtb_Kit_Wx_Date_Of(e["dt"] | 0u, w.utcOffset, day.year, day.month, day.mday);
    day.high = owm_Number(e["temp"]["max"]);
    day.low = owm_Number(e["temp"]["min"]);
    day.rain = owm_Percent(e["pop"]);
    day.icon = owm_Icon_Of(e["weather"][0]["id"] | 0);
    day.sunrise = e["sunrise"] | 0u;
    day.sunset = e["sunset"] | 0u;
    day.uvMax = owm_Number(e["uvi"]);
    day.precip = owm_Number(e["rain"]);
}

static uint8_t owm_Parse_Daily(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& w){
    JsonArrayConst list = doc["list"];
    if (list.isNull()) return KIT_WX_PARSE;
    w.utcOffset = doc["city"]["timezone"] | w.utcOffset;
    w.dayCount = 0;
    for (JsonVariantConst e : list){
        if (w.dayCount >= KIT_WX_DAYS) break;
        owm_Day_From(e, w, w.days[w.dayCount++]);
    }
    return w.dayCount ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint8_t owm_Parse_Hourly(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& w){
    JsonArrayConst list = doc["list"];
    if (list.isNull()) return KIT_WX_PARSE;
    w.utcOffset = doc["city"]["timezone"] | w.utcOffset;
    w.hourStep = 1;
    w.hourCount = 0;
    for (JsonVariantConst e : list){
        if (w.hourCount >= KIT_WX_HOURS) break;
        owm_Hour_From(e, w.hours[w.hourCount++], false);
    }
    return w.hourCount ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint8_t owm_Severity(const char* event){
    char lower[64];
    snprintf(lower, sizeof(lower), "%s", event);
    for (char* c = lower; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (strstr(lower, "extreme")) return KIT_WX_SEV_EXTREME;
    if (strstr(lower, "warning") || strstr(lower, "red")) return KIT_WX_SEV_SEVERE;
    if (strstr(lower, "watch") || strstr(lower, "orange") || strstr(lower, "amber")) return KIT_WX_SEV_MODERATE;
    if (strstr(lower, "advisory") || strstr(lower, "statement") || strstr(lower, "yellow")) return KIT_WX_SEV_MINOR;
    return KIT_WX_SEV_UNKNOWN;
}

static uint8_t owm_Parse_One_Call(JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& cfg, Mtb_Kit_Wx_Data_t& w){
    JsonVariantConst cur = doc["current"];
    if (!cur.is<JsonObjectConst>()) return KIT_WX_PARSE;
    w.utcOffset = doc["timezone_offset"] | w.utcOffset;
    Mtb_Kit_Wx_Current_t& c = w.current;
    c.time = cur["dt"] | 0u;
    c.day = 1;
    owm_Describe(cur["weather"][0], c);
    c.temp = owm_Number(cur["temp"]);
    c.feels = owm_Number(cur["feels_like"]);
    c.humidity = owm_Number(cur["humidity"]);
    c.pressure = owm_Number(cur["pressure"]);
    c.uv = owm_Number(cur["uvi"]);
    c.wind = owm_Wind(cur["wind_speed"], cfg);
    c.gust = owm_Wind(cur["wind_gust"], cfg);
    c.windDegrees = owm_Number(cur["wind_deg"]);
    c.cloud = owm_Number(cur["clouds"]);
    c.visibility = owm_Number(cur["visibility"]);
    c.rainChance = NAN;
    c.valid = !std::isnan(c.temp);
    // The hourly forecast of the paid fixed plans has 96 hours; One Call's 48 are used unless that one is on the key.
    if (!(w.plan.flags & OWM_HAS_HOURLY)){
        w.hourStep = 1;
        w.hourCount = 0;
        for (JsonVariantConst e : doc["hourly"].as<JsonArrayConst>()){
            if (w.hourCount >= KIT_WX_HOURS) break;
            owm_Hour_From(e, w.hours[w.hourCount++], true);
        }
    }
    // Eight days, unless the 16-day forecast is on the key.
    if (!(w.plan.flags & OWM_HAS_DAILY)){
        w.dayCount = 0;
        for (JsonVariantConst e : doc["daily"].as<JsonArrayConst>()){
            if (w.dayCount >= KIT_WX_DAYS) break;
            owm_Day_From(e, w, w.days[w.dayCount++]);
        }
    }
    owm_Sun(w, cur["sunrise"] | 0u, cur["sunset"] | 0u);
    w.alertCount = 0;
    if (cfg.options & OWM_OPTION_ALERTS){
        for (JsonVariantConst a : doc["alerts"].as<JsonArrayConst>()){
            if (w.alertCount >= KIT_WX_ALERTS) break;
            Mtb_Kit_Wx_Alert_t& alert = w.alerts[w.alertCount++];
            memset(&alert, 0, sizeof(alert));
            mtb_Kit_To_Ascii(alert.title, sizeof(alert.title), a["event"] | "Weather alert");
            mtb_Kit_To_Ascii(alert.source, sizeof(alert.source), a["sender_name"] | "");
            // No area in the answer: the start of the description says it.
            char area[160];
            mtb_Kit_To_Ascii(area, sizeof(area), a["description"] | "");
            for (char* ch = area; *ch; ch++) if (*ch == '\n' || *ch == '\r') *ch = ' ';
            const size_t length = strnlen(area, sizeof(alert.area) - 1);
            memcpy(alert.area, area, length);
            alert.area[length] = '\0';
            alert.start = a["start"] | 0u;
            alert.end = a["end"] | 0u;
            alert.severity = owm_Severity(alert.title);
        }
        // Most severe first.
        for (uint8_t i = 1; i < w.alertCount; i++)
            for (uint8_t j = i; j > 0 && w.alerts[j].severity > w.alerts[j - 1].severity; j--){ Mtb_Kit_Wx_Alert_t t = w.alerts[j]; w.alerts[j] = w.alerts[j - 1]; w.alerts[j - 1] = t; }
    }
    return c.valid ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint8_t owm_Parse_Air(JsonVariantConst doc, Mtb_Kit_Wx_Data_t& w){
    JsonVariantConst e = doc["list"][0];
    if (!e.is<JsonObjectConst>()) return KIT_WX_PARSE;
    Mtb_Kit_Wx_Air_t& a = w.air;
    a.scale = KIT_AQI_FIVE;
    a.index = (int16_t)(e["main"]["aqi"] | 0);
    a.time = e["dt"] | 0u;
    a.pm25 = owm_Number(e["components"]["pm2_5"]);
    a.pm10 = owm_Number(e["components"]["pm10"]);
    a.ozone = owm_Number(e["components"]["o3"]);
    a.no2 = owm_Number(e["components"]["no2"]);
    for (float& p : a.pollen) p = NAN;
    a.valid = a.index >= 1 && a.index <= 5;
    return a.valid ? KIT_WX_OK : KIT_WX_PARSE;
}

static uint32_t owm_Feature_Of(uint8_t feed){
    return feed == OWM_FEED_ONECALL ? OWM_HAS_ONECALL : feed == OWM_FEED_DAILY ? OWM_HAS_DAILY : feed == OWM_FEED_HOURLY ? OWM_HAS_HOURLY : 0;
}

static void owm_Update_Plan(Mtb_Kit_Wx_Plan_t& plan, uint32_t now){
    plan.known = (plan.flags & OWM_HAS_KEY) && (plan.tested & (OWM_HAS_DAILY | OWM_HAS_HOURLY)) == (OWM_HAS_DAILY | OWM_HAS_HOURLY) ? 1 : 0;
    plan.paid = (plan.flags & (OWM_HAS_DAILY | OWM_HAS_HOURLY)) ? 1 : 0;
    plan.checked = now;
    owm_Plan_Name(plan.flags, plan.name, sizeof(plan.name));
}

uint8_t owm_Parse(uint8_t feed, int httpCode, JsonVariantConst doc, const Mtb_Kit_Wx_Config_t& cfg, Mtb_Kit_Wx_Data_t& w, char* error, size_t errorSize){
    Mtb_Kit_Wx_Plan_t& plan = w.plan;
    const uint32_t feature = owm_Feature_Of(feed);
    const uint32_t now = w.current.time;
    if (httpCode != 200){
        const char* message = doc["message"] | "";
        snprintf(error, errorSize, "%s", message[0] ? message : "OpenWeather did not answer as expected");
        if (httpCode == 401 || (httpCode == 429 && feature == OWM_HAS_ONECALL)){
            if (feature){
                // Not on this key; for a probe before the key is known good, ask again soon.
                if (!(plan.flags & OWM_HAS_KEY) && !strstr(message, "One Call")) return KIT_WX_RATE;
                plan.flags &= ~feature;
                plan.tested |= feature;
                owm_Update_Plan(plan, now);
                return KIT_WX_PLAN;
            }
            plan.flags &= ~OWM_HAS_KEY;
            owm_Update_Plan(plan, now);
            return KIT_WX_KEY;
        }
        if (httpCode == 429) return KIT_WX_QUOTA;
        if (httpCode >= 500) return KIT_WX_SERVER;
        return KIT_WX_BAD;
    }
    if (doc.isNull()) return KIT_WX_PARSE;
    uint8_t result;
    switch (feed){
        case OWM_FEED_CURRENT: result = owm_Parse_Current(doc, cfg, w); break;
        case OWM_FEED_ONECALL: result = owm_Parse_One_Call(doc, cfg, w); break;
        case OWM_FEED_FORECAST: result = owm_Parse_Forecast(doc, w); break;
        case OWM_FEED_DAILY: result = owm_Parse_Daily(doc, w); break;
        case OWM_FEED_HOURLY: result = owm_Parse_Hourly(doc, w); break;
        case OWM_FEED_AIR: result = owm_Parse_Air(doc, w); break;
        default: return KIT_WX_BAD;
    }
    if (result == KIT_WX_OK){
        plan.flags |= OWM_HAS_KEY | feature;
        plan.tested |= feature;
        owm_Update_Plan(plan, w.current.time);
    }
    return result;
}

//##############################################################################################################
bool owm_Search_Request(const Mtb_Kit_Wx_Config_t& c, const char* query, char* url, size_t size){
    if (!c.apiKey[0]) return false;
    char encoded[160];
    mtb_Kit_Url_Encode(encoded, sizeof(encoded), query);
    const int n = snprintf(url, size, OWM_HOST "/geo/1.0/direct?q=%s&limit=%d&appid=%s", encoded, KIT_WX_PLACES, c.apiKey);
    return n > 0 && (size_t)n < size;
}

int owm_Parse_Places(int httpCode, JsonVariantConst doc, Mtb_Kit_Wx_Place_t* places, int max, char* error, size_t errorSize){
    if (httpCode != 200 || !doc.is<JsonArrayConst>()){
        snprintf(error, errorSize, "%s", doc["message"] | "OpenWeather could not search");
        return -1;
    }
    int count = 0;
    for (JsonVariantConst r : doc.as<JsonArrayConst>()){
        if (count >= max) break;
        const char* name = r["local_names"]["en"] | (r["name"] | "");
        if (!name[0] || !r["lat"].is<double>() || !r["lon"].is<double>()) continue;
        Mtb_Kit_Wx_Place_t& p = places[count++];
        const char* country = r["country"] | "";
        snprintf(p.name, sizeof(p.name), "%s%s%s", name, country[0] ? ", " : "", country);
        snprintf(p.detail, sizeof(p.detail), "%s", r["state"] | "");
        p.latitude = r["lat"];
        p.longitude = r["lon"];
    }
    return count;
}

const Mtb_Kit_Wx_Provider_t& owm_Provider(void){
    static const Mtb_Kit_Wx_Provider_t provider = {
        "OpenWeather", "Pixlpal-OpenWeather/2.0 (ESP32 LED display)", "/weather/openweather.bin", 96 * 1024, OWM_FEED_COUNT, false,
        owm_Request, owm_Parse, owm_Every, owm_Search_Request, owm_Parse_Places,
    };
    return provider;
}
