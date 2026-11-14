// Tell the Time's model: words for times, questions by level, the family's day. No Arduino.
#include "ttm_Model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void ttm_Settings_Default(Ttm_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = TTM_SETTINGS_MAGIC;
    s.startLevel = 0xFF;
    s.kinds = (1u << TTM_KINDS) - 1;
    s.words = 1;
    memset(s.level, 0xFF, sizeof(s.level));
    ttm_Add_Event(s, "07:30 Breakfast");
    ttm_Add_Event(s, "12:30 Lunch");
    ttm_Add_Event(s, "19:30 Bedtime story");
}

void ttm_Settings_Clamp(Ttm_Settings_t& s){
    s.magic = TTM_SETTINGS_MAGIC;
    if (s.startLevel != 0xFF && s.startLevel >= TTM_LEVELS) s.startLevel = 0xFF;
    s.kinds &= (1u << TTM_KINDS) - 1;
    if (!(s.kinds & ((1u << TTM_SET) | (1u << TTM_READ)))) s.kinds |= 1u << TTM_READ;   // PASS alone has nothing to start on
    s.hour24 = s.hour24 ? 1 : 0;
    s.words = s.words ? 1 : 0;
    for (uint8_t& l : s.level) if (l != 0xFF && l >= TTM_LEVELS) l = 0xFF;
    if (s.eventCount > TTM_EVENTS) s.eventCount = TTM_EVENTS;
    for (int i = 0; i < s.eventCount; i++){
        s.events[i].label[sizeof(s.events[i].label) - 1] = '\0';
        if (s.events[i].minute >= 1440) s.events[i].minute = 0;
    }
}

uint8_t ttm_Level_For_Age(uint8_t age){
    if (age <= 5) return TTM_OCLOCK;
    if (age == 6) return TTM_HALF;
    if (age == 7) return TTM_QUARTER;
    if (age == 8) return TTM_FIVE;
    return TTM_MINUTE;
}

static int hour12(int h){ h %= 12; return h ? h : 12; }

void ttm_Words(char* d, size_t n, uint16_t minute){
    const int h = (minute / 60) % 24, m = minute % 60;
    const int now = hour12(h), next = hour12(h + 1);
    if (m == 0) snprintf(d, n, "%d O'CLOCK", now);
    else if (m == 30) snprintf(d, n, "HALF PAST %d", now);
    else if (m == 15) snprintf(d, n, "QUARTER PAST %d", now);
    else if (m == 45) snprintf(d, n, "QUARTER TO %d", next);
    else if (m % 5 == 0) snprintf(d, n, m < 30 ? "%d PAST %d" : "%d TO %d", m < 30 ? m : 60 - m, m < 30 ? now : next);
    else if (m < 30) snprintf(d, n, "%d MINUTE%s PAST %d", m, m == 1 ? "" : "S", now);
    else snprintf(d, n, "%d MINUTE%s TO %d", 60 - m, 60 - m == 1 ? "" : "S", next);
}

void ttm_Digital(char* d, size_t n, uint16_t minute, bool hour24){
    const int h = (minute / 60) % 24, m = minute % 60;
    if (hour24) snprintf(d, n, "%02d:%02d", h, m);
    else snprintf(d, n, "%d:%02d", hour12(h), m);
}

uint16_t ttm_Step(uint8_t level){
    static const uint16_t S[TTM_LEVELS] = {60, 30, 15, 5, 1, 5};
    return S[level < TTM_LEVELS ? level : 0];
}

static uint32_t next(uint32_t& r){ r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r ? r : (r = 0x9E3779B9u); }
static int range(uint32_t& r, int lo, int hi){ return lo + (int)(next(r) % (uint32_t)(hi - lo + 1)); }

static uint16_t random_Time(uint8_t level, uint32_t& r){
    const int step = level == TTM_LATER ? 5 : ttm_Step(level);
    return (uint16_t)(range(r, 0, 720 / step - 1) * step);
}

static uint16_t wrap(int m){ m %= 720; return (uint16_t)(m < 0 ? m + 720 : m); }

void ttm_Question(uint8_t level, uint8_t kinds, uint32_t& r, Ttm_Question_t& q){
    memset(&q, 0, sizeof(q));
    if (level >= TTM_LEVELS) level = TTM_OCLOCK;
    uint8_t allowed[TTM_KINDS], n = 0;
    for (uint8_t k = 0; k < TTM_KINDS; k++){
        if (!(kinds & (1u << k))) continue;
        if (k == TTM_PASS && level < TTM_LATER) continue;
        allowed[n++] = k;
    }
    if (!n){ allowed[0] = TTM_READ; n = 1; }
    q.kind = allowed[range(r, 0, n - 1)];
    char a[24];
    if (q.kind == TTM_SET){
        q.target = random_Time(level, r);
        if (!q.target) q.target = (uint16_t)(ttm_Step(level) * range(r, 1, 3));     // not where the hands start
        q.shown = 0;
        if (level >= TTM_FIVE && (next(r) & 1)) ttm_Digital(a, sizeof(a), q.target, false);
        else ttm_Words(a, sizeof(a), q.target);
        snprintf(q.prompt, sizeof(q.prompt), "SET THE CLOCK TO %s", a);
        return;
    }
    uint16_t right;
    if (q.kind == TTM_PASS){
        static const uint16_t L[] = {10, 15, 20, 30, 45, 60, 90};
        q.shown = random_Time(TTM_FIVE, r);
        q.later = L[range(r, 0, 6)];
        right = wrap(q.shown + q.later);
        if (q.later >= 60) snprintf(q.prompt, sizeof(q.prompt), "IN %s IT WILL BE", q.later == 60 ? "1 HOUR" : "1 HOUR 30 MIN");
        else snprintf(q.prompt, sizeof(q.prompt), "IN %u MINUTES IT WILL BE", q.later);
    } else {
        q.shown = random_Time(level, r);
        right = q.shown;
        snprintf(q.prompt, sizeof(q.prompt), "WHAT TIME IS IT?");
    }
    q.target = right;
    // Wrong answers children really give: an hour out, past for to, the hands swapped, half an hour out.
    const int m = right % 60, h = right / 60;
    uint16_t cand[6] = {
        wrap(right + 60), wrap(right - 60), wrap(h * 60 + (60 - m) % 60), wrap(((m / 5) % 12) * 60 + (h % 12) * 5),
        wrap(right + 30), wrap(right - (q.kind == TTM_PASS ? q.later : 30) - (q.kind == TTM_PASS ? q.later : 0))
    };
    uint16_t picks[3] = {right, 0xFFFF, 0xFFFF};
    int got = 1;
    for (int tries = 0; tries < 40 && got < 3; tries++){
        const uint16_t c = cand[range(r, 0, 5)];
        bool dup = false;
        for (int i = 0; i < got; i++) if (picks[i] == c) dup = true;
        if (!dup && c % ttm_Step(level <= TTM_QUARTER ? level : (uint8_t)TTM_FIVE) == 0) picks[got++] = c;
    }
    for (int k = 0; got < 3; k++){ const uint16_t c = wrap(right + 60 * (k + 2)); bool dup = false; for (int i = 0; i < got; i++) if (picks[i] == c) dup = true; if (!dup) picks[got++] = c; }
    // Shuffled.
    for (int i = 2; i > 0; i--){ const int j = range(r, 0, i); const uint16_t t = picks[i]; picks[i] = picks[j]; picks[j] = t; }
    const bool digital = q.kind == TTM_PASS || (level >= TTM_FIVE && (next(r) & 1));
    for (int i = 0; i < 3; i++){
        q.choices[i] = picks[i];
        if (digital) ttm_Digital(q.choiceText[i], sizeof(q.choiceText[i]), picks[i], false);
        else ttm_Words(q.choiceText[i], sizeof(q.choiceText[i]), picks[i]);
    }
}

bool ttm_Set_Right(const Ttm_Question_t& q, uint16_t set){ return wrap(set) == wrap(q.target); }

const char* ttm_Hint(const Ttm_Question_t& q, uint16_t set){
    if (q.kind != TTM_SET) return q.kind == TTM_PASS ? "COUNT ON FROM THE CLOCK" : "SHORT HAND: HOUR. LONG HAND: MINUTES";
    if (set % 60 != q.target % 60) return "THE LONG HAND SHOWS THE MINUTES";
    return "THE SHORT HAND SHOWS THE HOUR";
}

bool ttm_Add_Event(Ttm_Settings_t& s, const char* text){
    if (!text || s.eventCount >= TTM_EVENTS) return false;
    int h, m, used = 0;
    if (sscanf(text, " %d:%d%n", &h, &m, &used) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
    const char* label = text + used;
    while (*label == ' ') label++;
    if (!*label) return false;
    Ttm_Event_t e = {};
    e.minute = (uint16_t)(h * 60 + m);
    snprintf(e.label, sizeof(e.label), "%s", label);
    for (int i = 0; i < s.eventCount; i++) if (s.events[i].minute == e.minute) return false;
    int pos = s.eventCount;
    while (pos > 0 && s.events[pos - 1].minute > e.minute){ s.events[pos] = s.events[pos - 1]; pos--; }
    s.events[pos] = e;
    s.eventCount++;
    return true;
}

int ttm_Next_Event(const Ttm_Settings_t& s, uint16_t minute){
    for (int i = 0; i < s.eventCount; i++) if (s.events[i].minute > minute) return i;
    return -1;
}
