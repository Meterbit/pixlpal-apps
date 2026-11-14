#include "gwx_Model.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_icons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

void gwx_Settings_Default(Gwx_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = GWX_SETTINGS_MAGIC;
    snprintf(s.place, sizeof(s.place), "Lagos, Nigeria");
    s.latitude = 6.5244;
    s.longitude = 3.3792;
    s.alerts = 1;
    s.pageSeconds = 10;
    s.autoCycle = 1;
    s.monthlyBudget = 9000;
}

void gwx_Settings_Clamp(Gwx_Settings_t& s){
    s.place[sizeof(s.place) - 1] = '\0';
    if (!(s.latitude >= -90 && s.latitude <= 90) || !(s.longitude >= -180 && s.longitude <= 180)){ s.latitude = 6.5244; s.longitude = 3.3792; }
    s.imperial = s.imperial ? 1 : 0;
    s.alerts = s.alerts ? 1 : 0;
    s.autoCycle = s.autoCycle ? 1 : 0;
    if (s.pageSeconds < 4) s.pageSeconds = 4;
    if (s.pageSeconds > 60) s.pageSeconds = 60;
    if (s.monthlyBudget < 1000) s.monthlyBudget = 1000;
    if (s.monthlyBudget > 200000) s.monthlyBudget = 200000;
    s.apiKey[sizeof(s.apiKey) - 1] = '\0';
}

//##############################################################################################################
uint8_t gwx_Icon_Of(const char* type){
    if (type == nullptr || type[0] == '\0') return KIT_WX_UNKNOWN;
    auto has = [&](const char* part){ return strstr(type, part) != nullptr; };
    if (has("THUNDER")) return KIT_WX_STORM;
    if (has("HAIL")) return KIT_WX_HAIL;
    if (has("SLEET") || has("RAIN_AND_SNOW") || has("RAINSNOW") || has("SNOWRAIN")) return KIT_WX_SLEET;
    if (has("SNOW")) return KIT_WX_SNOW;
    if (has("HEAVY_RAIN") || has("RAINSTORM") || has("MODERATE_TO_HEAVY") || has("PERIODICALLY_HEAVY")) return KIT_WX_HEAVY_RAIN;
    if (has("LIGHT_RAIN") || has("DRIZZLE") || has("CHANCE_OF_SHOWERS") || has("SCATTERED_SHOWERS")) return KIT_WX_DRIZZLE;
    if (has("RAIN") || has("SHOWER")) return KIT_WX_RAIN;
    if (has("FOG") || has("HAZE") || has("MIST")) return KIT_WX_FOG;
    if (has("WIND")) return KIT_WX_WIND;
    if (has("VERY_HOT")) return KIT_WX_HOT;
    if (strcmp(type, "PARTLY_CLOUDY") == 0 || strcmp(type, "MOSTLY_CLEAR") == 0) return KIT_WX_PARTLY;
    if (has("CLOUD")) return KIT_WX_CLOUDY;
    if (has("CLEAR") || has("SUNNY")) return KIT_WX_CLEAR;
    return KIT_WX_UNKNOWN;
}

void gwx_Cardinal(char* dest, size_t size, const char* cardinal){
    // "NORTH_NORTHWEST" -> "NNW": the first letter of each part, "NORTHWEST" counting as two.
    size_t used = 0;
    const char* p = cardinal ? cardinal : "";
    while (*p && used + 2 < size){
        if (strncmp(p, "NORTH", 5) == 0){ dest[used++] = 'N'; p += 5; }
        else if (strncmp(p, "SOUTH", 5) == 0){ dest[used++] = 'S'; p += 5; }
        else if (strncmp(p, "EAST", 4) == 0){ dest[used++] = 'E'; p += 4; }
        else if (strncmp(p, "WEST", 4) == 0){ dest[used++] = 'W'; p += 4; }
        else p++;
    }
    dest[used] = '\0';
}

//##############################################################################################################
uint8_t gwx_Error_Of(JsonVariantConst doc, int httpCode, char* text, size_t size){
    if (size) text[0] = '\0';
    JsonVariantConst error = doc["error"];
    if (httpCode == 200 && error.isNull()) return GWX_OK;
    const char* status = error["status"] | "";
    char ascii[160];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), error["message"] | "");
    if (size) snprintf(text, size, "%s", ascii);
    if (strcmp(status, "PERMISSION_DENIED") == 0 || strcmp(status, "UNAUTHENTICATED") == 0 || httpCode == 403 || httpCode == 401) return GWX_ERR_DENIED;
    if (strcmp(status, "RESOURCE_EXHAUSTED") == 0 || httpCode == 429) return GWX_ERR_QUOTA;
    if (strcmp(status, "INVALID_ARGUMENT") == 0 || strcmp(status, "NOT_FOUND") == 0 || httpCode == 400 || httpCode == 404) return GWX_ERR_ARGUMENT;
    if (httpCode >= 500) return GWX_ERR_SERVER;
    return httpCode > 0 ? GWX_ERR_PARSE : GWX_ERR_NET;
}

uint8_t gwx_Parse_Current(JsonVariantConst d, Gwx_Current_t& c){
    if (d["temperature"]["degrees"].isNull()) return GWX_ERR_PARSE;
    memset(&c, 0, sizeof(c));
    c.valid = 1;
    c.time = mtb_Kit_Parse_Iso_Time(d["currentTime"]);
    c.day = d["isDaytime"] | true;
    c.icon = gwx_Icon_Of(d["weatherCondition"]["type"]);
    mtb_Kit_To_Ascii(c.text, sizeof(c.text), d["weatherCondition"]["description"]["text"] | "");
    c.temp = d["temperature"]["degrees"] | 0.0f;
    c.feels = d["feelsLikeTemperature"]["degrees"] | c.temp;
    c.humidity = d["relativeHumidity"] | -1.0f;
    c.uv = d["uvIndex"] | -1.0f;
    c.wind = d["wind"]["speed"]["value"] | -1.0f;
    c.gust = d["wind"]["gust"]["value"] | -1.0f;
    c.windDegrees = d["wind"]["direction"]["degrees"] | -1.0f;
    gwx_Cardinal(c.windFrom, sizeof(c.windFrom), d["wind"]["direction"]["cardinal"] | "");
    c.pressure = d["airPressure"]["meanSeaLevelMillibars"] | -1.0f;
    c.rainChance = d["precipitation"]["probability"]["percent"] | -1.0f;
    c.cloud = d["cloudCover"] | -1.0f;
    c.high = d["currentConditionsHistory"]["maxTemperature"]["degrees"] | NAN;
    c.low = d["currentConditionsHistory"]["minTemperature"]["degrees"] | NAN;
    return GWX_OK;
}

static int32_t gwx_Offset_Seconds(const char* text){
    // "-28800s"
    return text ? (int32_t)strtol(text, nullptr, 10) : 0;
}

uint8_t gwx_Parse_Hours(JsonVariantConst d, Gwx_Data_t& data){
    JsonArrayConst hours = d["forecastHours"];
    if (hours.isNull()) return GWX_ERR_PARSE;
    uint8_t n = 0;
    for (JsonVariantConst h : hours){
        if (n >= GWX_HOURS) break;
        Gwx_Hour_t& out = data.hours[n];
        out.start = mtb_Kit_Parse_Iso_Time(h["interval"]["startTime"]);
        if (out.start == 0) continue;
        out.temp = h["temperature"]["degrees"] | 0.0f;
        out.rain = (uint8_t)(h["precipitation"]["probability"]["percent"] | 0);
        out.icon = gwx_Icon_Of(h["weatherCondition"]["type"]);
        out.day = h["isDaytime"] | true;
        if (n == 0) data.utcOffset = gwx_Offset_Seconds(h["displayDateTime"]["utcOffset"]);
        n++;
    }
    data.hourCount = n;
    return n ? GWX_OK : GWX_ERR_PARSE;
}

static uint8_t gwx_Moon_Index(const char* phase){
    static const char* const PHASES[8] = {"NEW_MOON", "WAXING_CRESCENT", "FIRST_QUARTER", "WAXING_GIBBOUS", "FULL_MOON",
                                          "WANING_GIBBOUS", "LAST_QUARTER", "WANING_CRESCENT"};
    for (uint8_t i = 0; i < 8; i++) if (phase && strcmp(phase, PHASES[i]) == 0) return i;
    return 0xFF;
}

uint8_t gwx_Parse_Days(JsonVariantConst d, Gwx_Data_t& data){
    JsonArrayConst days = d["forecastDays"];
    if (days.isNull()) return GWX_ERR_PARSE;
    uint8_t n = 0;
    for (JsonVariantConst day : days){
        if (n >= GWX_DAYS) break;
        Gwx_Day_t& out = data.days[n];
        memset(&out, 0, sizeof(out));
        out.year = day["displayDate"]["year"] | 0;
        out.month = day["displayDate"]["month"] | 0;
        out.mday = day["displayDate"]["day"] | 0;
        if (out.year == 0) continue;
        out.high = day["maxTemperature"]["degrees"] | NAN;
        out.low = day["minTemperature"]["degrees"] | NAN;
        // Daytime, or the full day (Japan and South Korea have no day parts).
        JsonVariantConst part = day["daytimeForecast"].isNull() ? day["fullDayForecast"] : day["daytimeForecast"];
        out.rain = (uint8_t)(part["precipitation"]["probability"]["percent"] | 0);
        out.icon = gwx_Icon_Of(part["weatherCondition"]["type"]);
        out.sunrise = mtb_Kit_Parse_Iso_Time(day["sunEvents"]["sunriseTime"]);
        out.sunset = mtb_Kit_Parse_Iso_Time(day["sunEvents"]["sunsetTime"]);
        out.moonPhase = gwx_Moon_Index(day["moonEvents"]["moonPhase"]);
        n++;
    }
    data.dayCount = n;
    return n ? GWX_OK : GWX_ERR_PARSE;
}

uint8_t gwx_Parse_Alerts(JsonVariantConst d, Gwx_Data_t& data){
    if (!d["error"].isNull()) return GWX_ERR_PARSE;
    uint8_t n = 0;
    // No alerts in force: only "regionCode" comes back.
    for (JsonVariantConst a : d["weatherAlerts"].as<JsonArrayConst>()){
        if (n >= GWX_ALERTS) break;
        Gwx_Alert_t& out = data.alerts[n];
        memset(&out, 0, sizeof(out));
        const char* severity = a["severity"] | "";
        out.severity = strcmp(severity, "EXTREME") == 0 ? GWX_SEV_EXTREME : strcmp(severity, "SEVERE") == 0 ? GWX_SEV_SEVERE
                     : strcmp(severity, "MODERATE") == 0 ? GWX_SEV_MODERATE : strcmp(severity, "MINOR") == 0 ? GWX_SEV_MINOR : GWX_SEV_UNKNOWN;
        out.start = mtb_Kit_Parse_Iso_Time(a["startTime"]);
        out.end = mtb_Kit_Parse_Iso_Time(a["expirationTime"]);
        mtb_Kit_To_Ascii(out.title, sizeof(out.title), a["alertTitle"]["text"] | (a["eventType"] | "Weather alert"));
        mtb_Kit_To_Ascii(out.area, sizeof(out.area), a["areaName"] | "");
        mtb_Kit_To_Ascii(out.source, sizeof(out.source), a["dataSource"]["name"] | (a["dataSource"]["publisher"] | ""));
        n++;
    }
    // Most severe first.
    for (uint8_t i = 1; i < n; i++){
        Gwx_Alert_t key = data.alerts[i];
        int j = i - 1;
        while (j >= 0 && data.alerts[j].severity < key.severity){ data.alerts[j + 1] = data.alerts[j]; j--; }
        data.alerts[j + 1] = key;
    }
    data.alertCount = n;
    return GWX_OK;
}

int gwx_Parse_Places(JsonVariantConst d, Gwx_Place_t* places, int max, char* text, size_t size){
    const char* status = d["status"] | "";
    if (size) text[0] = '\0';
    if (strcmp(status, "ZERO_RESULTS") == 0) return 0;
    if (strcmp(status, "OK") != 0){
        const char* message = d["error_message"] | "";
        if (size) snprintf(text, size, "%s%s%s", status[0] ? status : "No answer", message[0] ? ": " : "", message);
        return -1;
    }
    int n = 0;
    for (JsonVariantConst r : d["results"].as<JsonArrayConst>()){
        if (n >= max) break;
        Gwx_Place_t& p = places[n];
        memset(&p, 0, sizeof(p));
        p.latitude = r["geometry"]["location"]["lat"] | NAN;
        p.longitude = r["geometry"]["location"]["lng"] | NAN;
        if (!(p.latitude >= -90 && p.latitude <= 90) || !(p.longitude >= -180 && p.longitude <= 180)) continue;
        // The name: the locality (or the first component) and the country, as the phone shows it.
        const char *locality = nullptr, *admin = nullptr, *country = nullptr, *first = nullptr;
        for (JsonVariantConst c : r["address_components"].as<JsonArrayConst>()){
            const char* name = c["long_name"] | "";
            if (first == nullptr) first = name;
            for (JsonVariantConst t : c["types"].as<JsonArrayConst>()){
                const char* type = t | "";
                if (!locality && strcmp(type, "locality") == 0) locality = name;
                if (!admin && strcmp(type, "administrative_area_level_1") == 0) admin = name;
                if (!country && strcmp(type, "country") == 0) country = name;
            }
        }
        const char* main = locality ? locality : first ? first : "";
        if (country && strcmp(country, main) != 0) snprintf(p.name, sizeof(p.name), "%s, %s", main, country);
        else snprintf(p.name, sizeof(p.name), "%s", main);
        if (admin && strcmp(admin, main) != 0) snprintf(p.detail, sizeof(p.detail), "%s", admin);
        else snprintf(p.detail, sizeof(p.detail), "%.4f, %.4f", p.latitude, p.longitude);
        if (p.name[0] == '\0') continue;
        n++;
    }
    return n;
}

//##############################################################################################################
uint32_t gwx_Interval(uint32_t budget, uint32_t used, uint32_t secondsLeft, bool alerts){
    if (used >= budget) return 0;
    const uint32_t left = budget - used;
    // Per current-conditions read: 1, plus 1/4 for hours, 1/12 for days, 1/2 for alerts.
    const float perRead = 1.0f + 0.25f + 1.0f / 12 + (alerts ? 0.5f : 0);
    float interval = secondsLeft * perRead / left;
    if (interval < 600) interval = 600;
    if (interval > 3 * 3600) interval = 3 * 3600;
    return (uint32_t)interval;
}

uint32_t gwx_Month_Of(uint32_t t, uint32_t* secondsLeft){
    // Civil date from days since 1970.
    const int32_t z = (int32_t)(t / 86400) + 719468;
    const int32_t era = z / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) y++;
    if (secondsLeft){
        const int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
        const int64_t next = (int64_t)mtb_Kit_Days_From_Civil(ny, nm, 1) * 86400;
        *secondsLeft = next > (int64_t)t ? (uint32_t)(next - t) : 0;
    }
    return (uint32_t)(y * 100 + m);
}
