// Motorsport Live's worker, on OpenF1. What it reads, and how often:
//   The season's sessions and meetings: at start and every 12 hours (two requests; the next season's too in November
//   and December).
//   The weekend's drivers: when the weekend or the live session changes, and every 30 minutes.
//   The circuit: once per weekend (4 requests: last year's race at the circuit, its winner, one lap, its positions).
//   The last finished session's result, and the championships after a race or sprint: when they come out.
//   Live timing (sponsor accounts, while a session runs): positions and gaps every 5 s, race control every 8 s, laps
//   every 10 s, tyres and pit stops every 30 s, weather every 60 s: about 40 requests a minute, under the 60 allowed.
// Requests are spaced to the plan's limit: 2.1 s apart on the free plan (30 a minute), 1.05 s with a sponsor account.
#include "mts_App.h"
#include "mtb_engine.h"
#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_body_reader.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char TAG[] = "MTS_NET";
#define UA "PIXLPAL-L2/1.0 (Pixlpal Motorsport Live)"
#define API "https://api.openf1.org/v1/"
#define TOKEN_MAX 2048

struct Net_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh;
    bool changed;
    Mts_Settings_t* settings;
    Mts_Data_t* data;
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Net_t N;
static bool made = false;

static void lock(void){ xSemaphoreTake(N.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(N.lock); }

// Requests -------------------------------------------------------------------------------------------------------------
static char* token = nullptr;           // PSRAM, the sponsor account's bearer token
static uint32_t tokenUntil = 0, lastRequestMs = 0, callDay = 0, calls = 0, backOffUntil = 0;
static uint8_t plan = MTS_PLAN_FREE;

static void pace(void){
    const uint32_t gap = plan == MTS_PLAN_SPONSOR ? 1050 : 2100;
    while (millis() - lastRequestMs < gap && !N.stop) vTaskDelay(pdMS_TO_TICKS(50));
    lastRequestMs = millis();
    const uint32_t day = (uint32_t)time(nullptr) / 86400;
    if (day != callDay){ callDay = day; calls = 0; }
    calls++;
}

/** The sponsor account's token, fetched when there is none or it is about to run out. False: free plan or refused. */
static bool sign_In(const Mts_Settings_t& s, char* why, size_t whySize){
    if (!s.user[0] || !s.pass[0]){ plan = MTS_PLAN_FREE; token[0] = '\0'; return false; }
    const uint32_t now = (uint32_t)time(nullptr);
    if (token[0] && now + 120 < tokenUntil) return true;
    char* body = (char*)heap_caps_malloc(512, MALLOC_CAP_SPIRAM);
    if (!body) return false;
    char u[200], p[200];
    mtb_Kit_Url_Encode(u, sizeof(u), s.user);
    mtb_Kit_Url_Encode(p, sizeof(p), s.pass);
    const int n = snprintf(body, 512, "username=%s&password=%s", u, p);
    memset(p, 0, sizeof(p));
    static const char* const headers[] = {"Content-Type", "application/x-www-form-urlencoded", "Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r;
    pace();
    char* answer = n > 0 && n < 512 ? mtb_Kit_Http_Send("POST", "https://api.openf1.org/token", headers, body, (size_t)n, nullptr, 0, 8192, r, UA) : nullptr;
    memset(body, 0, 512);
    heap_caps_free(body);
    bool ok = false;
    if (answer && r.code == 200){
        JsonDocument doc(mtb_Kit_Json_Allocator());
        if (!deserializeJson(doc, answer, r.length)){
            const char* t = doc["access_token"] | "";
            const long life = doc["expires_in"].is<const char*>() ? atol(doc["expires_in"] | "3600") : (long)(doc["expires_in"] | 3600);
            if (t[0] && strlen(t) + 8 < TOKEN_MAX){
                snprintf(token, TOKEN_MAX, "Bearer %s", t);
                tokenUntil = now + (uint32_t)(life > 60 ? life : 3600);
                plan = MTS_PLAN_SPONSOR;
                ok = true;
            }
        }
    }
    if (!ok){
        token[0] = '\0';
        if (r.code == 401 || r.code == 403){ plan = MTS_PLAN_REFUSED; snprintf(why, whySize, "OpenF1 refused the username or password"); }
        else { plan = MTS_PLAN_FREE; snprintf(why, whySize, "OpenF1 sign-in did not answer (%d): free data meanwhile", r.code); }
    }
    if (answer){ memset(answer, 0, r.length); heap_caps_free(answer); }
    return ok;
}

/**
 * GET api.openf1.org/v1/<path>, read as it streams through @p filter into @p doc. 404 is OpenF1's "no rows": an empty
 * array and 200. @return the HTTP status (negative: no connection).
 */
static int of1_Get(const char* path, void (*makeFilter)(JsonDocument&), JsonDocument& doc){
    if ((uint32_t)time(nullptr) < backOffUntil) return 429;
    pace();
    char url[320];
    snprintf(url, sizeof(url), API "%s", path);
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.useHTTP10(true);
    http.setReuse(false);
    http.setUserAgent(UA);
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    doc.clear();
    if (!http.begin(client, url)) return -1;
    if (token && token[0]) http.addHeader("Authorization", token);
    const int code = http.GET();
    if (code == 200){
        JsonDocument filter;
        makeFilter(filter);
        Mtb_Kit_Body_Reader_t reader(http.getStream(), 15000, 4u * 1024u * 1024u);
        const DeserializationError e = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
        if (e){ ESP_LOGW(TAG, "%s: %s", path, e.c_str()); http.end(); doc.clear(); return -2; }
    }
    http.end();
    if (code == 404){ doc.clear(); doc.to<JsonArray>(); return 200; }
    if (code == 429) backOffUntil = (uint32_t)time(nullptr) + 30;
    if (code != 200) ESP_LOGW(TAG, "%s: HTTP %d", path, code);
    return code;
}

static void filter_Meeting(JsonDocument& f){ mts_Filter_Meetings(f); }
static void filter_Race(JsonDocument& f){ JsonObject o = f.add<JsonObject>(); o["session_key"] = true; o["date_end"] = true; o["is_cancelled"] = true; }
static void filter_Winner(JsonDocument& f){ JsonObject o = f.add<JsonObject>(); o["driver_number"] = true; o["number_of_laps"] = true; }
static void filter_Lap(JsonDocument& f){ JsonObject o = f.add<JsonObject>(); o["date_start"] = true; o["lap_duration"] = true; }
static void filter_Location(JsonDocument& f){ JsonObject o = f.add<JsonObject>(); o["x"] = true; o["y"] = true; }

// The worker -----------------------------------------------------------------------------------------------------------
struct Due_t { uint32_t sched, drivers, track, result, standings, position, interval, control, laps, stints, pits, weather; };

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 250 ms. Its state lives in these statics between steps.
static Mts_Settings_t* s;
static Mts_Schedule_t* sched;
static Mts_Drivers_t* drivers;
static Mts_Track_t* track;
static Mts_Result_t* result;
static Mts_Standings_t* standings;
static Mts_Live_t* live;
static JsonDocument doc;
static Due_t due;
static uint32_t weekend;
static uint32_t trackCircuit;
static uint32_t liveKey;
static uint32_t driversFor;
static uint32_t driversWant;
static char path[256];
static char why[96];
static char a[32];
static char b[32];
static bool schedOk;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    s = (Mts_Settings_t*)heap_caps_malloc(sizeof(Mts_Settings_t), MALLOC_CAP_SPIRAM);
    sched = (Mts_Schedule_t*)heap_caps_calloc(1, sizeof(Mts_Schedule_t), MALLOC_CAP_SPIRAM);
    drivers = (Mts_Drivers_t*)heap_caps_calloc(1, sizeof(Mts_Drivers_t), MALLOC_CAP_SPIRAM);
    track = (Mts_Track_t*)heap_caps_calloc(1, sizeof(Mts_Track_t), MALLOC_CAP_SPIRAM);
    result = (Mts_Result_t*)heap_caps_calloc(1, sizeof(Mts_Result_t), MALLOC_CAP_SPIRAM);
    standings = (Mts_Standings_t*)heap_caps_calloc(1, sizeof(Mts_Standings_t), MALLOC_CAP_SPIRAM);
    live = (Mts_Live_t*)heap_caps_calloc(1, sizeof(Mts_Live_t), MALLOC_CAP_SPIRAM);
    token = (char*)heap_caps_calloc(1, TOKEN_MAX, MALLOC_CAP_SPIRAM);
    if (!s || !sched || !drivers || !track || !result || !standings || !live || !token) N.stop = true;
    doc.clear();
    due = Due_t();
    weekend = 0;
    trackCircuit = 0;
    liveKey = 0;
    driversFor = 0;
    driversWant = 0;
    memset(path, 0, sizeof(path));
    memset(why, 0, sizeof(why));
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    schedOk = false;
    tokenUntil = 0;
    plan = MTS_PLAN_FREE;
}

static void worker_End(void){
    heap_caps_free(s);
    heap_caps_free(sched);
    heap_caps_free(drivers);
    heap_caps_free(track);
    heap_caps_free(result);
    heap_caps_free(standings);
    heap_caps_free(live);
    if (token){ memset(token, 0, TOKEN_MAX); heap_caps_free(token); token = nullptr; }
    N.running = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!N.running) return;
    if (N.stop){ worker_End(); return; }
    if (millis() - wk_StepAt < 250) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000u || !Mtb_Applications::internetConnectStatus) continue;
        lock();
        *s = *N.settings;
        const bool refresh = N.refresh, changed = N.changed;
        N.refresh = N.changed = false;
        unlock();
        if (changed){ token[0] = '\0'; tokenUntil = 0; }
        if (refresh){ due.sched = due.drivers = due.result = due.standings = 0; }
        why[0] = '\0';

        // The account.
        if (s->user[0] && s->pass[0] && (!token[0] || now + 120 >= tokenUntil) && plan != MTS_PLAN_REFUSED){
            sign_In(*s, why, sizeof(why));
            lock();
            N.data->plan = plan;
            snprintf(N.data->planWhy, sizeof(N.data->planWhy), "%s", why);
            N.version++;
            unlock();
        } else if (!s->user[0] || !s->pass[0]){ plan = MTS_PLAN_FREE; token[0] = '\0'; }
        if (changed && plan == MTS_PLAN_REFUSED) plan = MTS_PLAN_FREE;     // new details: try again

        // The season.
        if (now >= due.sched){
            const time_t tt = (time_t)now;
            struct tm u;
            gmtime_r(&tt, &u);
            const int year = u.tm_year + 1900;
            memset(sched, 0, sizeof(*sched));
            bool ok = true;
            for (int y = year; y <= year + (u.tm_mon >= 10 ? 1 : 0); y++){
                snprintf(path, sizeof(path), "sessions?year=%d", y);
                int code = of1_Get(path, mts_Filter_Sessions, doc);
                if (code == 200) mts_Parse_Sessions(doc.as<JsonVariantConst>(), *sched); else if (y == year) ok = false;
                snprintf(path, sizeof(path), "meetings?year=%d", y);
                code = of1_Get(path, filter_Meeting, doc);
                if (code == 200) mts_Parse_Meetings(doc.as<JsonVariantConst>(), *sched);
            }
            sched->year = (uint16_t)year;
            schedOk = ok && sched->count > 0;
            due.sched = now + (schedOk ? 12 * 3600 : 300);
            lock();
            if (schedOk || !N.data->schedOk){ N.data->sched = *sched; N.data->schedOk = schedOk; }
            if (!schedOk) snprintf(N.data->why, sizeof(N.data->why), "OpenF1 did not answer: trying again in 5 minutes");
            else N.data->why[0] = '\0';
            N.data->calls = calls;
            N.version++;
            unlock();
            continue;
        }
        if (!schedOk) continue;

        // The weekend, and the session live now.
        const uint32_t wk = mts_Weekend(*sched, now);
        if (wk != weekend){
            weekend = wk;
            due.drivers = due.track = 0;
            lock();
            N.data->weekend = wk;
            N.version++;
            unlock();
        }
        const int li = mts_Live_Session(*sched, now);
        const Mts_Session_t* ls = li >= 0 ? &sched->s[li] : nullptr;
        const bool onTrack = ls && now + 300 >= ls->start && now <= ls->end + 900;

        // The drivers: of the live session, else the weekend's latest, else the last race.
        uint32_t want = 0;
        if (ls) want = ls->key;
        else {
            for (int i = 0; i < sched->count; i++) if (sched->s[i].meeting == weekend && sched->s[i].start <= now) want = sched->s[i].key;
            if (!want){ const int r = mts_Last_Session(*sched, now, MTS_RACE); if (r >= 0) want = sched->s[r].key; }
        }
        if (want != driversWant){ driversWant = want; due.drivers = 0; }
        if (want && now >= due.drivers){
            snprintf(path, sizeof(path), "drivers?session_key=%lu", (unsigned long)want);
            int code = of1_Get(path, mts_Filter_Drivers, doc);
            uint32_t from = want;
            if (code == 200 && doc.as<JsonArrayConst>().size() == 0 && !driversFor){
                // Not out yet (before a weekend's first session): the last race's grid meanwhile.
                const int r = mts_Last_Session(*sched, now, MTS_RACE);
                if (r >= 0 && sched->s[r].key != want){
                    from = sched->s[r].key;
                    snprintf(path, sizeof(path), "drivers?session_key=%lu", (unsigned long)from);
                    code = of1_Get(path, mts_Filter_Drivers, doc);
                }
            }
            if (code == 200 && doc.as<JsonArrayConst>().size() > 0){
                mts_Parse_Drivers(doc.as<JsonVariantConst>(), from, *drivers);
                driversFor = from;
                lock();
                N.data->drivers = *drivers;
                N.version++;
                unlock();
            }
            due.drivers = now + (driversFor == want ? 1800 : code == 200 ? 300 : 120);
            continue;
        }

        // The circuit, from one lap of the last race held there.
        uint16_t circuit = 0;
        for (int i = 0; i < sched->count; i++) if (sched->s[i].meeting == weekend){ circuit = sched->s[i].circuitKey; break; }
        if (circuit && circuit != trackCircuit && now >= due.track && !onTrack){
            due.track = now + 1800;
            bool ok = false;
            Mts_Track_t t = {};
            t.circuitKey = circuit;
            snprintf(path, sizeof(path), "sessions?circuit_key=%u&session_type=Race", (unsigned)circuit);
            uint32_t race = 0;
            if (of1_Get(path, filter_Race, doc) == 200){
                for (JsonObjectConst r : doc.as<JsonArrayConst>()){
                    const uint32_t end = (uint32_t)(mts_Parse_Time_Ms(r["date_end"] | "") / 1000u);
                    if (end && end + MTS_LIVE_AFTER < now && !(r["is_cancelled"] | false)) race = r["session_key"] | race;
                }
            }
            int winner = 0;
            if (race){
                snprintf(path, sizeof(path), "session_result?session_key=%lu&position=1", (unsigned long)race);
                if (of1_Get(path, filter_Winner, doc) == 200 && doc.size()){
                    winner = doc[0]["driver_number"] | 0;
                    t.laps = (uint16_t)(doc[0]["number_of_laps"] | 0);
                }
            }
            if (winner){
                snprintf(path, sizeof(path), "laps?session_key=%lu&driver_number=%d&lap_number=3", (unsigned long)race, winner);
                if (of1_Get(path, filter_Lap, doc) == 200 && doc.size()){
                    const uint64_t start = mts_Parse_Time_Ms(doc[0]["date_start"] | "");
                    const float dur = doc[0]["lap_duration"] | 0.0f;
                    if (start && dur > 30 && dur < 300){
                        mts_Iso_Ms(a, sizeof(a), start);
                        mts_Iso_Ms(b, sizeof(b), start + (uint64_t)(dur * 1000.0f));
                        snprintf(path, sizeof(path), "location?session_key=%lu&driver_number=%d&date>%s&date<%s", (unsigned long)race, winner, a, b);
                        if (of1_Get(path, filter_Location, doc) == 200) ok = mts_Parse_Track(doc.as<JsonVariantConst>(), t) > 20;
                    }
                }
            }
            if (ok || race == 0){
                trackCircuit = circuit;      // a new circuit has no race yet: nothing to draw, don't ask again
                *track = t;
                lock();
                N.data->track = t;
                N.version++;
                unlock();
            }
            continue;
        }

        // Live timing.
        if (onTrack){
            const bool sponsor = plan == MTS_PLAN_SPONSOR;
            if (!sponsor){
                lock();
                if (!N.data->liveBlocked){ N.data->liveBlocked = true; N.data->liveOk = false; N.version++; }
                unlock();
            } else {
                if (liveKey != ls->key){
                    liveKey = ls->key;
                    mts_Live_Reset(*live, ls->key, ls->type, *drivers);
                    live->laps = (ls->type == MTS_RACE && track->circuitKey == ls->circuitKey) ? track->laps : 0;
                    due.position = due.interval = due.control = due.laps = due.stints = due.pits = due.weather = 0;
                }
                bool got = false, failed = false;
                const unsigned long k = (unsigned long)ls->key;
                // "&date>2026-11-08T18:00:00.123": rows after what was read. With no mark, @p backSeconds back from now
                // (busy feeds), or nothing (the whole session so far).
                char since[48];
                auto mark = [&](const char* field, uint64_t m, uint32_t backSeconds) -> const char* {
                    since[0] = '\0';
                    if (!m && !backSeconds) return since;
                    char t[32];
                    mts_Iso_Ms(t, sizeof(t), m ? m : (uint64_t)(now - backSeconds) * 1000u);
                    snprintf(since, sizeof(since), "&%s>%s", field, t);
                    return since;
                };
                int code = 0;
                if (now >= due.position){
                    snprintf(path, sizeof(path), "position?session_key=%lu%s", k, mark("date", live->markPosition, 0));
                    if ((code = of1_Get(path, mts_Filter_Position, doc)) == 200){ mts_Live_Positions(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.position = now + 5;
                } else if (now >= due.interval && (ls->type == MTS_RACE || ls->type == MTS_SPRINT)){
                    snprintf(path, sizeof(path), "intervals?session_key=%lu%s", k, mark("date", live->markInterval, 30));
                    if ((code = of1_Get(path, mts_Filter_Interval, doc)) == 200){ mts_Live_Intervals(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.interval = now + 5;
                } else if (now >= due.control){
                    snprintf(path, sizeof(path), "race_control?session_key=%lu%s", k, mark("date", live->markControl, 0));
                    if ((code = of1_Get(path, mts_Filter_Control, doc)) == 200){ mts_Live_Control(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.control = now + 8;
                } else if (now >= due.laps){
                    snprintf(path, sizeof(path), "laps?session_key=%lu%s", k, mark("date_start", live->markLap, 0));
                    if ((code = of1_Get(path, mts_Filter_Laps, doc)) == 200){ mts_Live_Laps(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.laps = now + 10;
                } else if (now >= due.stints){
                    snprintf(path, sizeof(path), "stints?session_key=%lu", k);
                    if ((code = of1_Get(path, mts_Filter_Stints, doc)) == 200){ mts_Live_Stints(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.stints = now + 30;
                } else if (now >= due.pits){
                    snprintf(path, sizeof(path), "pit?session_key=%lu%s", k, mark("date", live->markPit, 0));
                    if ((code = of1_Get(path, mts_Filter_Pit, doc)) == 200){ mts_Live_Pits(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.pits = now + 30;
                } else if (now >= due.weather){
                    snprintf(path, sizeof(path), "weather?session_key=%lu%s", k, mark("date", live->markWeather, 180));
                    if ((code = of1_Get(path, mts_Filter_Weather, doc)) == 200){ mts_Live_Weather(doc.as<JsonVariantConst>(), *live); got = true; } else failed = true;
                    due.weather = now + 60;
                }
                if (failed && (code == 401 || code == 403)){ token[0] = '\0'; tokenUntil = 0; }     // signed out: sign in again
                if (got || failed){
                    lock();
                    N.data->live = *live;
                    N.data->liveOk = got || N.data->liveOk;
                    N.data->liveBlocked = false;
                    N.data->calls = calls;
                    N.version++;
                    unlock();
                    continue;
                }
            }
        } else if (liveKey){
            liveKey = 0;
            due.result = due.standings = now + 60;
            lock();
            N.data->liveOk = N.data->liveBlocked = false;
            N.version++;
            unlock();
        }

        // The last finished session's result (free accounts: once it is no longer live).
        const int last = mts_Last_Session(*sched, now, 0xFF);
        if (last >= 0 && now >= due.result){
            const Mts_Session_t& x = sched->s[last];
            const bool readable = plan == MTS_PLAN_SPONSOR || now > x.end + MTS_LIVE_AFTER;
            if (readable && result->session != x.key){
                snprintf(path, sizeof(path), "session_result?session_key=%lu", (unsigned long)x.key);
                const int code = of1_Get(path, mts_Filter_Result, doc);
                if (code == 200 && doc.as<JsonArrayConst>().size() > 0){
                    mts_Parse_Result(doc.as<JsonVariantConst>(), x.key, x.type, *result);
                    lock();
                    N.data->result = *result;
                    N.version++;
                    unlock();
                    due.result = now + 600;
                } else due.result = now + (code == 200 ? 300 : 120);   // not out yet
                continue;
            }
            due.result = now + 120;
        }
        // The championships after the last race or sprint.
        if (now >= due.standings){
            int r = -1;
            for (int i = 0; i < sched->count; i++){
                const Mts_Session_t& x = sched->s[i];
                if (!x.cancelled && (x.type == MTS_RACE || x.type == MTS_SPRINT) && x.end <= now) r = i;
            }
            due.standings = now + 600;
            if (r >= 0 && standings->session != sched->s[r].key && (plan == MTS_PLAN_SPONSOR || now > sched->s[r].end + MTS_LIVE_AFTER)){
                const unsigned long k = (unsigned long)sched->s[r].key;
                Mts_Standings_t st = {};
                snprintf(path, sizeof(path), "championship_drivers?session_key=%lu", k);
                int code = of1_Get(path, [](JsonDocument& f){
                    JsonObject o = f.add<JsonObject>();
                    for (const char* key : {"session_key", "driver_number", "position_current", "position_start", "points_current", "points_start"}) o[key] = true;
                }, doc);
                const bool okD = code == 200 && mts_Parse_Standings_Drivers(doc.as<JsonVariantConst>(), st) > 0;
                snprintf(path, sizeof(path), "championship_teams?session_key=%lu", k);
                code = of1_Get(path, [](JsonDocument& f){
                    JsonObject o = f.add<JsonObject>();
                    for (const char* key : {"team_name", "position_current", "position_start", "points_current", "points_start"}) o[key] = true;
                }, doc);
                const bool okT = code == 200 && mts_Parse_Standings_Teams(doc.as<JsonVariantConst>(), st) > 0;
                if (okD){
                    st.session = (uint32_t)k;
                    *standings = st;
                    lock();
                    N.data->standings = st;
                    N.version++;
                    unlock();
                } else due.standings = now + 300;
                (void)okT;
                continue;
            }
            // The season's first race not run yet: last year's final table is not shown (a new grid).
        }
        lock();
        if (N.data->calls != calls){ N.data->calls = calls; N.version++; }
        unlock();
    } while (0);
}

bool mts_Net_Start(const Mts_Settings_t& settings){
    if (!made){
        N.lock = xSemaphoreCreateMutexStatic(&N.lockBuffer);
        N.settings = (Mts_Settings_t*)heap_caps_calloc(1, sizeof(Mts_Settings_t), MALLOC_CAP_SPIRAM);
        N.data = (Mts_Data_t*)heap_caps_calloc(1, sizeof(Mts_Data_t), MALLOC_CAP_SPIRAM);
        if (!N.settings || !N.data) return false;
        made = true;
    }
    if (N.running) return true;
    lock();
    *N.settings = settings;
    memset(N.data, 0, sizeof(Mts_Data_t));
    N.version++;
    unlock();
    N.stop = false;
    N.refresh = false;
    N.changed = false;
    N.running = true;
    worker_Begin();
    if (N.running) pxl_compat_add_background(worker_Step);
    return true;
}

void mts_Net_Stop(void){
    if (!N.running) return;
    N.stop = true;
    for (int i = 0; i < 600 && N.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (made){
        lock();
        memset(N.settings->user, 0, sizeof(N.settings->user));
        memset(N.settings->pass, 0, sizeof(N.settings->pass));
        unlock();
    }
}

void mts_Net_Set_Settings(const Mts_Settings_t& settings){
    if (!made) return;
    lock();
    const bool changed = strcmp(N.settings->user, settings.user) != 0 || strcmp(N.settings->pass, settings.pass) != 0;
    *N.settings = settings;
    if (changed) N.changed = true;
    unlock();
}

void mts_Net_Refresh(void){ N.refresh = true; }

bool mts_Net_Copy(Mts_Data_t& d, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != N.version;
    if (changed){ d = *N.data; version = N.version; }
    unlock();
    return changed;
}
