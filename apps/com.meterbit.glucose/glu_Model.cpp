// Glucose Display's model: ranges, trends, the day's readings, statistics, alarms, and the sources' answers. No Arduino.
#include "glu_Model.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

ArduinoJson::Allocator* glu_Json_Allocator(void);

void glu_Settings_Default(Glu_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = GLU_SETTINGS_MAGIC;
    s.units = GLU_UNITS_AUTO;
    s.useSiteTargets = 1;
    s.urgentLow = 55;
    s.low = 70;
    s.high = 180;
    s.urgentHigh = 250;
    s.targetLow = 70;
    s.targetHigh = 180;
    s.alarmOn[GLU_URGENT_LOW] = 1;
    s.alarmOn[GLU_URGENT_LOW_SOON] = 1;
    s.alarmOn[GLU_LOW] = 1;
    s.alarmOn[GLU_FALLING_FAST] = 0;
    s.alarmOn[GLU_STALE] = 1;
    s.alarmOn[GLU_URGENT_HIGH] = 1;
    s.alarmOn[GLU_HIGH] = 0;
    s.alarmOn[GLU_RISING_FAST] = 0;
    s.staleMinutes = 20;
    s.lowSnooze = 15;
    s.highSnooze = 60;
    s.sound = 1;
    s.phoneAlerts = 1;
    s.quietHighs = 1;
    s.quietFrom = 22 * 60;
    s.quietTo = 7 * 60;
    s.nightDim = 1;
    s.graphHours = 3;
    s.hour24 = 1;
    s.notify.on[NTF_NTFY] = 1;
    snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

static uint16_t clamp16(uint16_t v, uint16_t lo, uint16_t hi){ return v < lo ? lo : v > hi ? hi : v; }
static void terminate(char* t, size_t n){ t[n - 1] = '\0'; }

void glu_Settings_Clamp(Glu_Settings_t& s){
    s.magic = GLU_SETTINGS_MAGIC;
    if (s.source >= GLU_SOURCES) s.source = GLU_NIGHTSCOUT;
    if (s.units > GLU_UNITS_AUTO) s.units = GLU_UNITS_AUTO;
    if (s.dxRegion > GLU_DX_JP) s.dxRegion = GLU_DX_US;
    if (s.lluPatient >= GLU_PATIENTS) s.lluPatient = 0;
    terminate(s.name, sizeof(s.name)); terminate(s.nsUrl, sizeof(s.nsUrl)); terminate(s.nsToken, sizeof(s.nsToken));
    terminate(s.nsSecretSha1, sizeof(s.nsSecretSha1)); terminate(s.dxUser, sizeof(s.dxUser)); terminate(s.dxPass, sizeof(s.dxPass));
    terminate(s.lluEmail, sizeof(s.lluEmail)); terminate(s.lluPass, sizeof(s.lluPass));
    s.urgentLow = clamp16(s.urgentLow, 40, 70);
    s.low = clamp16(s.low, (uint16_t)(s.urgentLow + 1), 120);
    s.urgentHigh = clamp16(s.urgentHigh, 180, 400);
    s.high = clamp16(s.high, (uint16_t)(s.low + 10), (uint16_t)(s.urgentHigh - 1));
    s.targetLow = clamp16(s.targetLow, 54, 120);
    s.targetHigh = clamp16(s.targetHigh, (uint16_t)(s.targetLow + 10), 300);
    for (uint8_t& a : s.alarmOn) a = a ? 1 : 0;
    s.alarmOn[GLU_ALARM_NONE] = 0;
    if (s.staleMinutes < 10 || s.staleMinutes > 120) s.staleMinutes = 20;
    if (s.lowSnooze < 5 || s.lowSnooze > 60) s.lowSnooze = 15;
    if (s.highSnooze < 15 || s.highSnooze > 240) s.highSnooze = 60;
    s.sound = s.sound ? 1 : 0;
    s.phoneAlerts = s.phoneAlerts ? 1 : 0;
    s.quietHighs = s.quietHighs ? 1 : 0;
    s.nightDim = s.nightDim ? 1 : 0;
    s.useSiteTargets = s.useSiteTargets ? 1 : 0;
    s.hour24 = s.hour24 ? 1 : 0;
    if (s.quietFrom >= 1440) s.quietFrom = 22 * 60;
    if (s.quietTo >= 1440) s.quietTo = 7 * 60;
    if (s.graphHours != 3 && s.graphHours != 6 && s.graphHours != 12 && s.graphHours != 24) s.graphHours = 3;
    if (!s.notify.ntfyServer[0]) snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

uint8_t glu_Units(const Glu_Settings_t& s, const Glu_Site_t* site){
    if (s.units != GLU_UNITS_AUTO) return s.units;
    return site && site->known && s.source == GLU_NIGHTSCOUT ? site->units : (uint8_t)GLU_MGDL;
}

void glu_Ranges(const Glu_Settings_t& s, const Glu_Site_t* site, uint16_t& urgentLow, uint16_t& low, uint16_t& high, uint16_t& urgentHigh){
    urgentLow = s.urgentLow; low = s.low; high = s.high; urgentHigh = s.urgentHigh;
    if (!s.useSiteTargets || !site || !site->known || s.source != GLU_NIGHTSCOUT) return;
    // Nightscout's bgLow / bgTargetBottom / bgTargetTop / bgHigh: the urgent lines and the target.
    if (site->low >= 40 && site->low <= 80) urgentLow = site->low;
    if (site->targetLow > urgentLow && site->targetLow <= 150) low = site->targetLow;
    if (site->high >= 180 && site->high <= 400) urgentHigh = site->high;
    if (site->targetHigh > low + 10 && site->targetHigh < urgentHigh) high = site->targetHigh;
}

uint8_t glu_Range(uint16_t v, uint16_t urgentLow, uint16_t low, uint16_t high, uint16_t urgentHigh){
    if (v < urgentLow) return GLU_RANGE_URGENT_LOW;
    if (v < low) return GLU_RANGE_LOW;
    if (v >= urgentHigh) return GLU_RANGE_URGENT_HIGH;
    if (v > high) return GLU_RANGE_HIGH;
    return GLU_RANGE_IN;
}

void glu_Value_Text(char* d, size_t n, uint16_t mgdl, uint8_t units){
    if (units == GLU_MMOLL) snprintf(d, n, "%.1f", mgdl / GLU_MMOL);
    else snprintf(d, n, "%u", mgdl);
}

void glu_Delta_Text(char* d, size_t n, int delta, uint8_t units){
    if (units == GLU_MMOLL){
        const float m = delta / GLU_MMOL;
        snprintf(d, n, "%c%.1f", m < -0.049f ? '-' : '+', fabsf(m));
    } else snprintf(d, n, "%c%d", delta < 0 ? '-' : '+', abs(delta));
}

const char* glu_Units_Name(uint8_t u){ return u == GLU_MMOLL ? "mmol/L" : "mg/dL"; }

const char* glu_Trend_Name(uint8_t t){
    static const char* const N[GLU_TRENDS] = {"", "rising quickly", "rising", "rising slightly", "steady", "falling slightly",
                                              "falling", "falling quickly", "trend unknown", "out of range"};
    return t < GLU_TRENDS ? N[t] : "";
}

uint8_t glu_Trend_From_Text(const char* t){
    if (!t) return GLU_TREND_NONE;
    struct Map_t { const char* text; uint8_t trend; };
    static const Map_t M[] = {{"DoubleUp", GLU_DOUBLE_UP}, {"SingleUp", GLU_SINGLE_UP}, {"FortyFiveUp", GLU_45_UP}, {"Flat", GLU_FLAT},
                              {"FortyFiveDown", GLU_45_DOWN}, {"SingleDown", GLU_SINGLE_DOWN}, {"DoubleDown", GLU_DOUBLE_DOWN},
                              {"NOT COMPUTABLE", GLU_NOT_COMPUTABLE}, {"NotComputable", GLU_NOT_COMPUTABLE},
                              {"RATE OUT OF RANGE", GLU_OUT_OF_RANGE}, {"RateOutOfRange", GLU_OUT_OF_RANGE}};
    for (const Map_t& m : M) if (!strcmp(t, m.text)) return m.trend;
    return GLU_TREND_NONE;
}

uint8_t glu_Trend_From_Libre(int a){
    static const uint8_t T[6] = {GLU_TREND_NONE, GLU_SINGLE_DOWN, GLU_45_DOWN, GLU_FLAT, GLU_45_UP, GLU_SINGLE_UP};
    return a >= 1 && a <= 5 ? T[a] : (uint8_t)GLU_TREND_NONE;
}

const char* glu_Alarm_Name(uint8_t a){
    static const char* const N[GLU_ALARMS] = {"", "Urgent low", "Urgent low soon", "Low", "Falling fast", "No readings",
                                              "Urgent high", "High", "Rising fast"};
    return a < GLU_ALARMS ? N[a] : "";
}

const char* glu_Alarm_Text(uint8_t a){
    static const char* const N[GLU_ALARMS] = {"", "URGENT LOW", "URGENT LOW SOON", "LOW", "FALLING FAST", "NO READINGS",
                                              "URGENT HIGH", "HIGH", "RISING FAST"};
    return a < GLU_ALARMS ? N[a] : "";
}

// History ----------------------------------------------------------------------------------------------------------
bool glu_History_Add(Glu_History_t& h, const Glu_Reading_t& r, uint32_t now){
    if (r.t == 0 || r.mgdl < 20 || r.mgdl > 600) return false;
    if (now && (r.t > now + 600 || r.t + 86400u < now)) return false;
    // Drop what is over a day old.
    if (now){
        int old = 0;
        while (old < h.count && h.r[old].t + 86400u < now) old++;
        if (old){ memmove(h.r, h.r + old, (size_t)(h.count - old) * sizeof(Glu_Reading_t)); h.count -= old; }
    }
    // The same reading again (within a minute): replace it.
    for (int i = h.count - 1; i >= 0 && i >= h.count - 8; i--){
        const uint32_t d = h.r[i].t > r.t ? h.r[i].t - r.t : r.t - h.r[i].t;
        if (d < 60){
            h.r[i].mgdl = r.mgdl;
            if (r.trend != GLU_TREND_NONE || h.r[i].trend == GLU_TREND_NONE) h.r[i].trend = r.trend;
            return true;
        }
    }
    if (h.count >= GLU_HISTORY){
        if (r.t <= h.r[0].t) return false;
        memmove(h.r, h.r + 1, (size_t)(h.count - 1) * sizeof(Glu_Reading_t));
        h.count--;
    }
    int pos = h.count;
    while (pos > 0 && h.r[pos - 1].t > r.t){ h.r[pos] = h.r[pos - 1]; pos--; }
    h.r[pos] = r;
    h.count++;
    return true;
}

const Glu_Reading_t* glu_Latest(const Glu_History_t& h){ return h.count ? &h.r[h.count - 1] : nullptr; }

bool glu_Delta(const Glu_History_t& h, int& delta){
    if (h.count < 2) return false;
    const Glu_Reading_t& a = h.r[h.count - 1];
    for (int i = h.count - 2; i >= 0; i--){
        const uint32_t gap = a.t - h.r[i].t;
        if (gap < 240) continue;                    // a 1-minute CGM: look further back
        if (gap > 660) return false;
        delta = (int)lroundf(((int)a.mgdl - (int)h.r[i].mgdl) * 300.0f / (float)gap);
        return true;
    }
    return false;
}

/** The slope, mg/dL a minute, of the readings in the last 15 minutes (least squares); false with fewer than 3. */
static bool slope(const Glu_History_t& h, float& perMinute){
    if (h.count < 3) return false;
    const uint32_t end = h.r[h.count - 1].t;
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = h.count - 1; i >= 0 && end - h.r[i].t <= 16 * 60; i--){
        const double x = -((double)(end - h.r[i].t)) / 60.0, y = h.r[i].mgdl;
        n++; sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    if (n < 3) return false;
    const double den = n * sxx - sx * sx;
    if (fabs(den) < 1e-6) return false;
    perMinute = (float)((n * sxy - sx * sy) / den);
    return true;
}

uint8_t glu_Trend_From_History(const Glu_History_t& h){
    float m;
    if (!slope(h, m)) return GLU_TREND_NONE;
    // Dexcom's bands: over 3 mg/dL a minute double, 2-3 single, 1-2 forty-five, else flat.
    if (m > 3) return GLU_DOUBLE_UP;
    if (m > 2) return GLU_SINGLE_UP;
    if (m > 1) return GLU_45_UP;
    if (m < -3) return GLU_DOUBLE_DOWN;
    if (m < -2) return GLU_SINGLE_DOWN;
    if (m < -1) return GLU_45_DOWN;
    return GLU_FLAT;
}

bool glu_Project(const Glu_History_t& h, int minutes, int& mgdl){
    float m;
    if (!slope(h, m)) return false;
    mgdl = (int)lroundf(h.r[h.count - 1].mgdl + m * minutes);
    return true;
}

void glu_Stats(const Glu_History_t& h, uint32_t now, int hours, uint16_t tLow, uint16_t tHigh, uint16_t urgentLow, Glu_Stats_t& o){
    memset(&o, 0, sizeof(o));
    const uint32_t from = now > (uint32_t)hours * 3600u ? now - (uint32_t)hours * 3600u : 0;
    double sum = 0, sq = 0;
    int in = 0, below = 0, above = 0, urgent = 0;
    uint32_t first = 0, last = 0;
    bool wasLow = false;
    for (int i = 0; i < h.count; i++){
        const Glu_Reading_t& r = h.r[i];
        if (r.t < from || r.t > now + 600) continue;
        if (!first) first = r.t;
        last = r.t;
        o.readings++;
        sum += r.mgdl;
        sq += (double)r.mgdl * r.mgdl;
        if (r.mgdl < tLow) below++; else if (r.mgdl > tHigh) above++; else in++;
        if (r.mgdl < urgentLow) urgent++;
        const bool low = r.mgdl < tLow;
        if (low && !wasLow) o.lows++;
        wasLow = low ? true : r.mgdl >= tLow + 5 ? false : wasLow;
    }
    if (!o.readings) return;
    o.hours = (float)(last - first) / 3600.0f;
    o.inRange = 100.0f * in / o.readings;
    o.below = 100.0f * below / o.readings;
    o.above = 100.0f * above / o.readings;
    o.urgentBelow = 100.0f * urgent / o.readings;
    o.mean = (float)(sum / o.readings);
    o.gmi = 3.31f + 0.02392f * o.mean;
    const double var = sq / o.readings - (sum / o.readings) * (sum / o.readings);
    o.cv = o.mean > 0 ? (float)(100.0 * sqrt(var > 0 ? var : 0) / o.mean) : 0;
}

// Alarms -------------------------------------------------------------------------------------------------------------
bool glu_In_Quiet(const Glu_Settings_t& s, int minute){
    if (s.quietFrom == s.quietTo) return false;
    if (s.quietFrom < s.quietTo) return minute >= s.quietFrom && minute < s.quietTo;
    return minute >= s.quietFrom || minute < s.quietTo;
}

/** A condition with hysteresis: on beyond the line, off only once back past it by @p margin. */
static bool latch(uint8_t& latched, bool beyond, bool backPast){
    if (beyond) latched = 1;
    else if (backPast) latched = 0;
    return latched;
}

uint8_t glu_Alarm_Check(const Glu_Settings_t& s, const Glu_Site_t* site, const Glu_History_t& h, uint32_t now, int minute,
                        Glu_Alarm_State_t& st){
    uint16_t uLow, low, high, uHigh;
    glu_Ranges(s, site, uLow, low, high, uHigh);
    const Glu_Reading_t* r = glu_Latest(h);
    const bool stale = !r || now > r->t + (uint32_t)s.staleMinutes * 60u;
    bool on[GLU_ALARMS] = {};
    on[GLU_STALE] = latch(st.latched[GLU_STALE], stale, !stale) && r;   // never "no readings" before the first one
    if (r && now <= r->t + 15 * 60){
        const uint16_t v = r->mgdl;
        on[GLU_URGENT_LOW] = latch(st.latched[GLU_URGENT_LOW], v < uLow, v >= uLow + 5);
        on[GLU_LOW] = latch(st.latched[GLU_LOW], v < low, v >= low + 5);
        on[GLU_URGENT_HIGH] = latch(st.latched[GLU_URGENT_HIGH], v >= uHigh, v < uHigh - 10);
        on[GLU_HIGH] = latch(st.latched[GLU_HIGH], v > high, v <= high - 10);
        int soon;
        const bool falling = glu_Project(h, 20, soon) && soon < (int)uLow && v >= uLow;
        on[GLU_URGENT_LOW_SOON] = latch(st.latched[GLU_URGENT_LOW_SOON], falling, !falling && (v >= uLow + 10 || v < uLow));
        const uint8_t trend = r->trend != GLU_TREND_NONE ? r->trend : glu_Trend_From_History(h);
        on[GLU_FALLING_FAST] = trend == GLU_DOUBLE_DOWN || trend == GLU_SINGLE_DOWN;
        on[GLU_RISING_FAST] = trend == GLU_DOUBLE_UP || trend == GLU_SINGLE_UP;
        st.latched[GLU_FALLING_FAST] = on[GLU_FALLING_FAST];
        st.latched[GLU_RISING_FAST] = on[GLU_RISING_FAST];
    } else {
        for (int a = GLU_URGENT_LOW; a < GLU_ALARMS; a++) if (a != GLU_STALE) st.latched[a] = 0;
    }
    const bool quiet = glu_In_Quiet(s, minute);
    uint8_t pick = GLU_ALARM_NONE;
    for (uint8_t a = GLU_URGENT_LOW; a < GLU_ALARMS; a++){
        if (!on[a]){ st.snoozedUntil[a] = 0; continue; }       // a new episode starts unsnoozed
        if (!s.alarmOn[a] && a != GLU_URGENT_LOW) continue;    // urgent low cannot be switched off
        if (quiet && s.quietHighs && (a == GLU_HIGH || a == GLU_RISING_FAST)) continue;
        if (pick == GLU_ALARM_NONE) pick = a;
    }
    if (pick != st.active){ st.active = pick; st.since = now; }
    if (pick != GLU_ALARM_NONE && st.snoozedUntil[pick] && now < st.snoozedUntil[pick]) return GLU_ALARM_NONE;
    return pick;
}

void glu_Alarm_Snooze(const Glu_Settings_t& s, Glu_Alarm_State_t& st, uint32_t now, int minutes){
    const uint8_t a = st.active;
    if (a == GLU_ALARM_NONE) return;
    const bool lowKind = a == GLU_URGENT_LOW || a == GLU_URGENT_LOW_SOON || a == GLU_LOW || a == GLU_FALLING_FAST;
    int m = minutes > 0 ? minutes : lowKind ? s.lowSnooze : a == GLU_STALE ? 30 : s.highSnooze;
    if ((a == GLU_URGENT_LOW || a == GLU_URGENT_LOW_SOON) && m > 15) m = 15;
    st.snoozedUntil[a] = now + (uint32_t)m * 60u;
    // A lower alarm under it would sound at once: snooze the lows together, and the highs together.
    for (uint8_t b = GLU_URGENT_LOW; b < GLU_ALARMS; b++){
        const bool bLow = b == GLU_URGENT_LOW || b == GLU_URGENT_LOW_SOON || b == GLU_LOW || b == GLU_FALLING_FAST;
        if (b != a && bLow == lowKind && b > a && b != GLU_STALE) st.snoozedUntil[b] = st.snoozedUntil[a];
    }
}

bool glu_Alarm_Send_Due(const Glu_Alarm_State_t& st, uint8_t a, uint32_t now){
    if (a == GLU_ALARM_NONE || a >= GLU_ALARMS) return false;
    const uint32_t every = a == GLU_URGENT_LOW ? 300 : 900;
    return !st.sentAt[a] || now >= st.sentAt[a] + every;
}

uint32_t glu_Next_Poll(uint32_t last, uint32_t now, uint32_t every){
    uint32_t next = now + 60;
    if (last && last <= now){
        const uint32_t due = last + every + 20;
        if (due > now) next = due;
        else if (now - last > 3 * every) next = now + 120;       // the sensor is quiet: no hurry
        else next = now + 45;                                    // late: soon again
    }
    if (next < now + 30) next = now + 30;
    if (next > now + every + 30) next = now + every + 30;
    return next;
}

// Answers ------------------------------------------------------------------------------------------------------------
int glu_Parse_Ns_Entries(const char* body, size_t length, uint32_t now, Glu_History_t& h){
    JsonDocument filter;
    filter[0]["sgv"] = true;
    filter[0]["date"] = true;
    filter[0]["direction"] = true;
    filter[0]["type"] = true;
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || !doc.is<JsonArray>()) return -1;
    int added = 0;
    for (JsonObject e : doc.as<JsonArray>()){
        const char* type = e["type"] | "sgv";
        if (strcmp(type, "sgv")) continue;
        const double ms = e["date"] | 0.0;
        const long v = e["sgv"] | 0L;
        Glu_Reading_t r = {(uint32_t)(ms / 1000.0), (uint16_t)(v > 0 && v < 1000 ? v : 0), glu_Trend_From_Text(e["direction"] | "")};
        if (glu_History_Add(h, r, now)) added++;
    }
    return added;
}

static uint16_t threshold(JsonVariantConst t, const char* camel, const char* snake){
    double v = t[camel] | -1.0;
    if (v < 0) v = t[snake] | -1.0;
    if (v <= 0) return 0;
    if (v < 35) v *= GLU_MMOL;                      // given in mmol/L
    return (uint16_t)lround(v);
}

bool glu_Parse_Ns_Status(const char* body, size_t length, Glu_Site_t& site){
    JsonDocument filter;
    filter["name"] = true;
    filter["version"] = true;
    filter["settings"]["units"] = true;
    filter["settings"]["customTitle"] = true;
    filter["settings"]["thresholds"] = true;
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || !doc.is<JsonObject>()) return false;
    JsonVariantConst st = doc["settings"];
    if (st.isNull()) return false;
    memset(&site, 0, sizeof(site));
    site.known = true;
    const char* u = st["units"] | "mg/dl";
    site.units = (u[0] == 'm' || u[0] == 'M') && (u[1] == 'm' || u[1] == 'M') ? GLU_MMOLL : GLU_MGDL;
    JsonVariantConst t = st["thresholds"];
    site.high = threshold(t, "bgHigh", "bg_high");
    site.targetHigh = threshold(t, "bgTargetTop", "bg_target_top");
    site.targetLow = threshold(t, "bgTargetBottom", "bg_target_bottom");
    site.low = threshold(t, "bgLow", "bg_low");
    snprintf(site.name, sizeof(site.name), "%s", st["customTitle"] | (doc["name"] | ""));
    snprintf(site.version, sizeof(site.version), "%s", doc["version"] | "");
    return true;
}

bool glu_Parse_Ns_Properties(const char* body, size_t length, Glu_Extra_t& x){
    JsonDocument filter;
    filter["iob"]["iob"] = true;
    filter["cob"]["cob"] = true;
    filter["upbat"]["level"] = true;
    filter["upbat"]["display"] = true;
    JsonDocument doc(glu_Json_Allocator());
    memset(&x, 0, sizeof(x));
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || !doc.is<JsonObject>()) return false;
    if (doc["iob"]["iob"].is<float>()){ x.hasIob = true; x.iob = doc["iob"]["iob"].as<float>(); }
    if (doc["cob"]["cob"].is<float>()){ x.hasCob = true; x.cob = doc["cob"]["cob"].as<float>(); }
    long level = doc["upbat"]["level"] | -1L;
    if (level < 0){ const char* d = doc["upbat"]["display"] | ""; if (isdigit((unsigned char)d[0])) level = atol(d); }
    if (level >= 0 && level <= 100){ x.hasBattery = true; x.battery = (uint8_t)level; }
    return true;
}

/** "Date(1691455258000)" or "/Date(1691455258000-0400)/" to Unix. */
static uint32_t dexcom_Time(const char* t){
    const char* p = t ? strstr(t, "Date(") : nullptr;
    if (!p) return 0;
    return (uint32_t)(strtod(p + 5, nullptr) / 1000.0);
}

int glu_Parse_Dexcom(const char* body, size_t length, uint32_t now, Glu_History_t& h, char* code, size_t codeSize){
    if (code && codeSize) code[0] = '\0';
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length)) return -1;
    if (!doc.is<JsonArray>()){
        if (code && codeSize) snprintf(code, codeSize, "%s", doc["Code"] | "unexpected answer");
        return -1;
    }
    int added = 0;
    for (JsonObject e : doc.as<JsonArray>()){
        const char* when = e["WT"] | (e["ST"] | (const char*)nullptr);
        const long v = e["Value"] | 0L;
        uint8_t trend = GLU_TREND_NONE;
        if (e["Trend"].is<const char*>()) trend = glu_Trend_From_Text(e["Trend"]);
        else { const long n = e["Trend"] | 0L; if (n > 0 && n < GLU_TRENDS) trend = (uint8_t)n; }
        Glu_Reading_t r = {dexcom_Time(when), (uint16_t)(v > 0 && v < 1000 ? v : 0), trend};
        if (glu_History_Add(h, r, now)) added++;
    }
    return added;
}

bool glu_Parse_Dexcom_Id(const char* body, size_t length, char* id, size_t size){
    if (!body || !length || size < 2) return false;
    size_t a = 0, b = length;
    while (a < b && (body[a] == '"' || isspace((unsigned char)body[a]))) a++;
    while (b > a && (body[b - 1] == '"' || isspace((unsigned char)body[b - 1]))) b--;
    if (b - a < 8 || b - a >= size) return false;
    bool zero = true;
    for (size_t i = a; i < b; i++){
        const char c = body[i];
        if (!(isxdigit((unsigned char)c) || c == '-')) return false;
        if (c != '0' && c != '-') zero = false;
    }
    if (zero) return false;                         // "00000000-0000-...": refused
    memcpy(id, body + a, b - a);
    id[b - a] = '\0';
    return true;
}

uint32_t glu_Parse_Libre_Time(const char* t){
    int mo, d, y, hh, mi, ss;
    char ampm[4] = "";
    if (!t || sscanf(t, "%d/%d/%d %d:%d:%d %2s", &mo, &d, &y, &hh, &mi, &ss, ampm) < 6) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 2000 || hh < 0 || hh > 23 || mi > 59 || ss > 60) return 0;
    if (ampm[0] == 'P' || ampm[0] == 'p'){ if (hh < 12) hh += 12; }
    else if ((ampm[0] == 'A' || ampm[0] == 'a') && hh == 12) hh = 0;
    // Days from civil (Howard Hinnant).
    const int yy = y - (mo <= 2);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = (unsigned)(yy - era * 400);
    const unsigned doy = (153u * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const long days = (long)era * 146097L + (long)doe - 719468L;
    return (uint32_t)(days * 86400L + hh * 3600L + mi * 60L + ss);
}

bool glu_Parse_Libre_Login(const char* body, size_t length, Glu_Libre_Login_t& o){
    memset(&o, 0, sizeof(o));
    o.status = -1;
    JsonDocument filter;
    filter["status"] = true;
    filter["data"]["redirect"] = true;
    filter["data"]["region"] = true;
    filter["data"]["user"]["id"] = true;
    filter["data"]["authTicket"]["token"] = true;
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || !doc.is<JsonObject>()) return false;
    o.status = doc["status"] | -1;
    JsonVariantConst d = doc["data"];
    o.redirect = d["redirect"] | false;
    const char* region = d["region"] | "";
    size_t n = 0;
    for (; region[n] && n + 1 < sizeof(o.region); n++){ if (!isalnum((unsigned char)region[n])) break; o.region[n] = (char)tolower((unsigned char)region[n]); }
    o.region[n] = '\0';
    if (region[n]) o.region[0] = '\0';              // not a plain region name: ignored
    snprintf(o.userId, sizeof(o.userId), "%s", d["user"]["id"] | "");
    const char* token = d["authTicket"]["token"] | "";
    if (strlen(token) < sizeof(o.token)) snprintf(o.token, sizeof(o.token), "%s", token);
    return o.status == 0 && (o.redirect ? o.region[0] != 0 : (o.token[0] && o.userId[0]));
}

static bool libre_Reading(JsonVariantConst m, uint32_t now, Glu_History_t& h, bool trend){
    const long v = m["ValueInMgPerDl"] | 0L;
    Glu_Reading_t r = {glu_Parse_Libre_Time(m["FactoryTimestamp"] | (const char*)nullptr), (uint16_t)(v > 0 && v < 1000 ? v : 0),
                       trend ? glu_Trend_From_Libre(m["TrendArrow"] | 0) : (uint8_t)GLU_TREND_NONE};
    return glu_History_Add(h, r, now);
}

int glu_Parse_Libre_Connections(const char* body, size_t length, uint32_t now, uint8_t pick, Glu_Libre_Patient_t* people,
                                int maxPeople, Glu_History_t& h){
    JsonDocument filter;
    filter["status"] = true;
    JsonObject f = filter["data"][0].to<JsonObject>();
    f["patientId"] = true;
    f["firstName"] = true;
    f["lastName"] = true;
    f["glucoseMeasurement"]["ValueInMgPerDl"] = true;
    f["glucoseMeasurement"]["FactoryTimestamp"] = true;
    f["glucoseMeasurement"]["TrendArrow"] = true;
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || !doc["data"].is<JsonArray>()) return -1;
    if ((doc["status"] | 0) != 0) return -1;
    int n = 0;
    for (JsonObject c : doc["data"].as<JsonArray>()){
        if (n < maxPeople && people){
            snprintf(people[n].id, sizeof(people[n].id), "%s", c["patientId"] | "");
            snprintf(people[n].name, sizeof(people[n].name), "%s %s", c["firstName"] | "", c["lastName"] | "");
        }
        if (n == pick) libre_Reading(c["glucoseMeasurement"], now, h, true);
        n++;
    }
    return n;
}

int glu_Parse_Libre_Graph(const char* body, size_t length, uint32_t now, Glu_History_t& h){
    JsonDocument filter;
    filter["status"] = true;
    filter["data"]["graphData"][0]["ValueInMgPerDl"] = true;
    filter["data"]["graphData"][0]["FactoryTimestamp"] = true;
    filter["data"]["connection"]["glucoseMeasurement"]["ValueInMgPerDl"] = true;
    filter["data"]["connection"]["glucoseMeasurement"]["FactoryTimestamp"] = true;
    filter["data"]["connection"]["glucoseMeasurement"]["TrendArrow"] = true;
    JsonDocument doc(glu_Json_Allocator());
    if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || (doc["status"] | -1) != 0) return -1;
    int added = 0;
    for (JsonVariantConst m : doc["data"]["graphData"].as<JsonArrayConst>()) if (libre_Reading(m, now, h, false)) added++;
    if (libre_Reading(doc["data"]["connection"]["glucoseMeasurement"], now, h, true)) added++;
    return added;
}

// SHA-1 and SHA-256 (FIPS 180-4), small and slow: a few short strings now and then. ---------------------------------------
static inline uint32_t rol(uint32_t x, int n){ return (x << n) | (x >> (32 - n)); }
static inline uint32_t ror(uint32_t x, int n){ return (x >> n) | (x << (32 - n)); }

/** Pads @p text into 64-byte blocks and calls @p block for each. */
template <typename F> static void blocks(const char* text, F block){
    const size_t n = strlen(text);
    const uint64_t bits = (uint64_t)n * 8;
    uint8_t b[64];
    size_t i = 0;
    for (;;){
        size_t k = 0;
        for (; k < 64 && i + k < n; k++) b[k] = (uint8_t)text[i + k];
        if (k == 64){ block(b); i += 64; continue; }
        b[k++] = 0x80;
        if (k > 56){ while (k < 64) b[k++] = 0; block(b); k = 0; }
        while (k < 56) b[k++] = 0;
        for (int j = 7; j >= 0; j--) b[k++] = (uint8_t)(bits >> (j * 8));
        block(b);
        return;
    }
}

static void hex(const uint32_t* w, int words, char* out){
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < words; i++) for (int j = 0; j < 8; j++) out[i * 8 + j] = H[(w[i] >> (28 - 4 * j)) & 15];
    out[words * 8] = '\0';
}

void glu_Sha1_Hex(const char* text, char out[41]){
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    blocks(text ? text : "", [&](const uint8_t* b){
        uint32_t w[80];
        for (int t = 0; t < 16; t++) w[t] = (uint32_t)b[t * 4] << 24 | (uint32_t)b[t * 4 + 1] << 16 | (uint32_t)b[t * 4 + 2] << 8 | b[t * 4 + 3];
        for (int t = 16; t < 80; t++) w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int t = 0; t < 80; t++){
            uint32_t f, k;
            if (t < 20){ f = (bb & c) | (~bb & d); k = 0x5A827999u; }
            else if (t < 40){ f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (t < 60){ f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[t];
            e = d; d = c; c = rol(bb, 30); bb = a; a = tmp;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    });
    hex(h, 5, out);
}

void glu_Sha256_Hex(const char* text, char out[65]){
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    blocks(text ? text : "", [&](const uint8_t* b){
        uint32_t w[64];
        for (int t = 0; t < 16; t++) w[t] = (uint32_t)b[t * 4] << 24 | (uint32_t)b[t * 4 + 1] << 16 | (uint32_t)b[t * 4 + 2] << 8 | b[t * 4 + 3];
        for (int t = 16; t < 64; t++){
            const uint32_t s0 = ror(w[t - 15], 7) ^ ror(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const uint32_t s1 = ror(w[t - 2], 17) ^ ror(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; t++){
            const uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25), ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[t] + w[t];
            const uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22), maj = (a & bb) ^ (a & c) ^ (bb & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    });
    hex(h, 8, out);
}
