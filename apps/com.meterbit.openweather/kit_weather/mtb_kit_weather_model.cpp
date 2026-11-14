#include "mtb_kit_weather.h"
#include "mtb_kit_ui.h"
#include <cmath>
#include <math.h>
#include <string.h>

void mtb_Kit_Wx_Clear(Mtb_Kit_Wx_Data_t& d){
    const Mtb_Kit_Wx_Plan_t plan = d.plan;
    const uint32_t usedDay = d.usedDay, usedToday = d.usedToday, usedMonth = d.usedMonth, usedCalls = d.usedCalls, keyHash = d.keyHash;
    memset(&d, 0, sizeof(d));
    Mtb_Kit_Wx_Current_t& c = d.current;
    c.temp = c.feels = c.humidity = c.uv = c.wind = c.gust = c.windDegrees = c.pressure = c.rainChance = c.cloud = c.visibility = NAN;
    d.air.pm25 = d.air.pm10 = d.air.ozone = d.air.no2 = NAN;
    for (float& p : d.air.pollen) p = NAN;
    d.hourStep = 1;
    // What was learned about the key and the calls made are not the weather: they stay.
    d.plan = plan;
    d.keyHash = keyHash;
    d.usedDay = usedDay; d.usedToday = usedToday; d.usedMonth = usedMonth; d.usedCalls = usedCalls;
}

uint32_t mtb_Kit_Wx_Key_Hash(const char* key){
    uint32_t hash = 2166136261u;
    for (const char* c = key ? key : ""; *c; c++){ hash ^= (uint8_t)*c; hash *= 16777619u; }
    return hash;
}

uint32_t mtb_Kit_Wx_Month_Of(uint32_t unixTime){
    int16_t y; uint8_t m, d;
    mtb_Kit_Wx_Date_Of(unixTime, 0, y, m, d);
    return (uint32_t)y * 100 + m;
}

void mtb_Kit_Wx_Date_Of(uint32_t unixTime, int32_t offset, int16_t& year, uint8_t& month, uint8_t& mday){
    // Civil date from days (Howard Hinnant's algorithm).
    int32_t z = (int32_t)(((int64_t)unixTime + offset) / 86400) + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    mday = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    year = (int16_t)((int32_t)yoe + era * 400 + (month <= 2));
}

const char* mtb_Kit_Wx_Cardinal(float degrees){
    static const char* const POINTS[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    if (std::isnan(degrees)) return "";
    float d = fmodf(degrees, 360.0f);
    if (d < 0) d += 360.0f;
    return POINTS[(int)((d + 11.25f) / 22.5f) % 16];
}

int mtb_Kit_Aqi_Band(uint8_t scale, int index){
    if (index < 0) return -1;
    switch (scale){
        case KIT_AQI_EU: return index <= 20 ? 0 : index <= 40 ? 1 : index <= 60 ? 2 : index <= 80 ? 3 : index <= 100 ? 4 : 5;
        case KIT_AQI_FIVE: return index <= 1 ? 0 : index >= 5 ? 4 : index - 1;
        default: return index <= 50 ? 0 : index <= 100 ? 1 : index <= 150 ? 2 : index <= 200 ? 3 : index <= 300 ? 4 : 5;
    }
}

const char* mtb_Kit_Aqi_Name(uint8_t scale, int index){
    const int band = mtb_Kit_Aqi_Band(scale, index);
    if (band < 0) return "UNKNOWN";
    static const char* const US[6] = {"GOOD", "MODERATE", "UNHEALTHY FOR SOME", "UNHEALTHY", "VERY UNHEALTHY", "HAZARDOUS"};
    static const char* const EU[6] = {"GOOD", "FAIR", "MODERATE", "POOR", "VERY POOR", "EXTREMELY POOR"};
    return scale == KIT_AQI_US ? US[band] : EU[band];
}

uint16_t mtb_Kit_Aqi_Color(uint8_t scale, int index){
    static const uint16_t COLORS[6] = {MTB_KIT_RGB(40, 200, 80), MTB_KIT_RGB(230, 210, 0), MTB_KIT_RGB(255, 130, 0),
                                       MTB_KIT_RGB(235, 30, 30), MTB_KIT_RGB(160, 50, 200), MTB_KIT_RGB(140, 20, 40)};
    const int band = mtb_Kit_Aqi_Band(scale, index);
    return band < 0 ? KIT_DIM : COLORS[band];
}
