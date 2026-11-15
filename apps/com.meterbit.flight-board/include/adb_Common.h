/**
 * @file adb_Common.h
 * @brief AeroDataBox (aerodatabox.p.rapidapi.com) for Flight Board and My Flight: the request (the owner's RapidAPI
 *        key), the times it gives ("2026-11-15 10:00Z", "2026-11-15 11:00+01:00"), and a reader that lets a long JSON
 *        parse wait its turn (an installed app's task must not run more than about two seconds without waiting).
 *
 * The same file is in both apps' folders.
 */
#ifndef ADB_COMMON_H
#define ADB_COMMON_H

#include "mtb_kit_http.h"
#include "mtb_kit_ui.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifndef ADB_BASE
#define ADB_BASE "https://aerodatabox.p.rapidapi.com"
#endif
#define ADB_HOST "aerodatabox.p.rapidapi.com"

extern "C" void pxl_compat_breathe(void);

/** GET an AeroDataBox path ("/flights/..."): the body (to free with heap_caps_free), or nullptr. */
static inline char* adb_Get(const char* path, const char* key, size_t maxBytes, Mtb_Kit_Http_t& r, const char* ua){
    char url[320];
    snprintf(url, sizeof(url), "%s%s", ADB_BASE, path);
    const char* headers[] = {"X-RapidAPI-Key", key, "X-RapidAPI-Host", ADB_HOST, "Accept", "application/json", nullptr};
    return mtb_Kit_Http_Get(url, headers, nullptr, 0, maxBytes, r, ua);
}

/** Why a request failed, for the owner. */
static inline void adb_Why(int code, char* t, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(t, n, "NOT ALLOWED");
    else if (code <= 0) snprintf(t, n, "NO ANSWER");
    else if (code == 401 || code == 403) snprintf(t, n, "KEY REFUSED: CHECK IT OR YOUR SUBSCRIPTION");
    else if (code == 429) snprintf(t, n, "MONTHLY LIMIT REACHED");
    else if (code == 204) snprintf(t, n, "NOTHING FOUND");
    else if (code == 400) snprintf(t, n, "NOT FOUND: CHECK THE CODE");
    else snprintf(t, n, "HTTP %d", code);
}

static inline int64_t adb_Days(int y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/** An AeroDataBox time: its Unix seconds (0 if unreadable); @p hhmm gets the local clock as given ("11:00"). */
struct Adb_Time_t { int64_t utc; int localMinute; int y, m, d; bool ok; };
static inline Adb_Time_t adb_Time(const char* s){
    Adb_Time_t t = {0, -1, 0, 0, 0, false};
    int y, mo, d, h, mi, n = 0;
    if (!s || sscanf(s, "%4d-%2d-%2d%*1[ T]%2d:%2d%n", &y, &mo, &d, &h, &mi, &n) < 5 || !n) return t;
    const char* p = s + n;
    if (*p == ':') p += 3;                                                      // seconds
    while (*p == '.' || (*p >= '0' && *p <= '9')) p++;                          // fractions
    int off = 0;
    if (*p == '+' || *p == '-'){ int oh = 0, om = 0; sscanf(p + 1, "%2d:%2d", &oh, &om); off = (oh * 60 + om) * (*p == '-' ? -1 : 1); }
    t.utc = adb_Days(y, mo, d) * 86400 + h * 3600 + mi * 60 - (int64_t)off * 60;
    t.localMinute = h * 60 + mi;
    t.y = y; t.m = mo; t.d = d;
    t.ok = true;
    return t;
}

/** A reader for deserializeJson over a body in memory that lets the parse wait its turn every 2 KB. */
struct Adb_Reader_t {
    const char* p;
    const char* end;
    size_t sinceBreath = 0;
    int read(){
        if (p >= end) return -1;
        if (++sinceBreath >= 2048){ sinceBreath = 0; pxl_compat_breathe(); }
        return (uint8_t)*p++;
    }
    size_t readBytes(char* out, size_t n){
        size_t k = 0;
        while (k < n && p < end){ out[k++] = *p++; if (++sinceBreath >= 2048){ sinceBreath = 0; pxl_compat_breathe(); } }
        return k;
    }
};

#endif
