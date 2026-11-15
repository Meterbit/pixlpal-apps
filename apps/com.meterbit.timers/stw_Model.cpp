#include "stw_Model.h"
#include <stdio.h>
#include <string.h>

#define US 1000000LL

static const Stw_Preset_t PRESETS[] = {
    {"Tabata", 20, 10, 8},
    {"HIIT 40/20", 40, 20, 10},
    {"EMOM 10 min", 60, 0, 10},
    {"Pomodoro", 25 * 60, 5 * 60, 4},
    {"Plank 1 min x 3", 60, 30, 3},
};

const Stw_Preset_t* stw_Presets(int& count){
    count = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
    return PRESETS;
}

void stw_Settings_Default(Stw_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = STW_SETTINGS_MAGIC;
    s.mode = STW_MODE_STOPWATCH;
    s.timerSeconds = 5 * 60;
    s.work = PRESETS[0].work;
    s.rest = PRESETS[0].rest;
    s.rounds = PRESETS[0].rounds;
    s.hundredths = 1;
    s.flash = 1;
    s.theme = 0;
}

void stw_Settings_Clamp(Stw_Settings_t& s){
    if (s.mode >= STW_MODE_COUNT) s.mode = STW_MODE_STOPWATCH;
    if (s.timerSeconds < 5) s.timerSeconds = 5;
    if (s.timerSeconds > STW_TIMER_MAX) s.timerSeconds = STW_TIMER_MAX;
    if (s.work < 5) s.work = 5;
    if (s.work > 3600) s.work = 3600;
    if (s.rest > 1800) s.rest = 1800;
    if (s.rounds < 1) s.rounds = 1;
    if (s.rounds > 99) s.rounds = 99;
    s.hundredths = s.hundredths ? 1 : 0;
    s.flash = s.flash ? 1 : 0;
    if (s.theme > 4) s.theme = 0;
}

//##############################################################################################################
int64_t stw_Watch_Elapsed(const Stw_Watch_t& w, int64_t now){
    return w.heldUs + (w.running && now > w.startUs ? now - w.startUs : 0);
}

void stw_Watch_Toggle(Stw_Watch_t& w, int64_t now){
    if (w.running){ w.heldUs = stw_Watch_Elapsed(w, now); w.running = 0; }
    else { w.startUs = now; w.running = 1; }
}

bool stw_Watch_Lap(Stw_Watch_t& w, int64_t now){
    if (!w.running || w.lapCount >= STW_LAPS) return false;
    const int64_t split = stw_Watch_Elapsed(w, now);
    if (w.lapCount && split <= w.laps[w.lapCount - 1]) return false;
    w.laps[w.lapCount++] = split;
    return true;
}

void stw_Watch_Reset(Stw_Watch_t& w){ memset(&w, 0, sizeof(w)); }

int64_t stw_Watch_Lap_Time(const Stw_Watch_t& w, int i){
    if (i < 0 || i >= w.lapCount) return 0;
    return w.laps[i] - (i ? w.laps[i - 1] : 0);
}

void stw_Watch_Best_Worst(const Stw_Watch_t& w, int& best, int& worst){
    best = worst = -1;
    if (w.lapCount < 2) return;
    best = worst = 0;
    for (int i = 1; i < w.lapCount; i++){
        const int64_t t = stw_Watch_Lap_Time(w, i);
        if (t < stw_Watch_Lap_Time(w, best)) best = i;
        if (t > stw_Watch_Lap_Time(w, worst)) worst = i;
    }
}

//##############################################################################################################
void stw_Timer_Toggle(Stw_Timer_t& t, uint32_t seconds, int64_t now){
    switch (t.state){
        case STW_IDLE: t.seconds = seconds; t.endUs = now + (int64_t)seconds * US; t.state = STW_RUNNING; break;
        case STW_RUNNING: t.leftUs = t.endUs > now ? t.endUs - now : 0; t.state = STW_PAUSED; break;
        case STW_PAUSED: t.endUs = now + t.leftUs; t.state = STW_RUNNING; break;
        default: stw_Timer_Reset(t); break;
    }
}

void stw_Timer_Reset(Stw_Timer_t& t){ memset(&t, 0, sizeof(t)); }

int64_t stw_Timer_Left(const Stw_Timer_t& t, uint32_t seconds, int64_t now){
    switch (t.state){
        case STW_RUNNING: return t.endUs > now ? t.endUs - now : 0;
        case STW_PAUSED: return t.leftUs;
        case STW_DONE: return 0;
        default: return (int64_t)seconds * US;
    }
}

bool stw_Timer_Update(Stw_Timer_t& t, int64_t now){
    if (t.state != STW_RUNNING || now < t.endUs) return false;
    t.state = STW_DONE;
    t.doneUs = t.endUs;
    return true;
}

uint32_t stw_Timer_Step(uint32_t s, bool up){
    // The step of the band the length is in (going up) or comes from (going down).
    const uint32_t at = up ? s : (s > 0 ? s - 1 : 0);
    if (at < 60) return 5;
    if (at < 300) return 15;
    if (at < 600) return 30;
    if (at < 3600) return 60;
    return 300;
}

//##############################################################################################################
uint32_t stw_Phase_Seconds(uint8_t phase, uint16_t work, uint16_t rest){
    return phase == STW_PHASE_PREP ? STW_PREP_SECONDS : phase == STW_PHASE_WORK ? work : phase == STW_PHASE_REST ? rest : 0;
}

uint32_t stw_Intervals_Length(uint16_t work, uint16_t rest, uint8_t rounds){
    return STW_PREP_SECONDS + (uint32_t)work * rounds + (uint32_t)rest * (rounds > 0 ? rounds - 1 : 0);
}

void stw_Intervals_Toggle(Stw_Intervals_t& iv, const Stw_Settings_t& s, int64_t now){
    switch (iv.state){
        case STW_IDLE:
            memset(&iv, 0, sizeof(iv));
            iv.work = s.work; iv.rest = s.rest; iv.rounds = s.rounds;
            iv.phase = STW_PHASE_PREP;
            iv.round = 1;
            iv.endUs = now + STW_PREP_SECONDS * US;
            iv.state = STW_RUNNING;
            break;
        case STW_RUNNING: iv.leftUs = iv.endUs > now ? iv.endUs - now : 0; iv.state = STW_PAUSED; break;
        case STW_PAUSED: iv.endUs = now + iv.leftUs; iv.state = STW_RUNNING; break;
        default: stw_Intervals_Reset(iv); break;
    }
}

void stw_Intervals_Reset(Stw_Intervals_t& iv){ memset(&iv, 0, sizeof(iv)); }

int stw_Intervals_Update(Stw_Intervals_t& iv, int64_t now){
    int changes = 0;
    // Every phase that has ended, even many (the app was away).
    while (iv.state == STW_RUNNING && now >= iv.endUs && changes < 250){
        changes++;
        if (iv.phase == STW_PHASE_PREP){ iv.phase = STW_PHASE_WORK; iv.endUs += (int64_t)iv.work * US; continue; }
        if (iv.phase == STW_PHASE_WORK){
            if (iv.round >= iv.rounds){ iv.phase = STW_PHASE_FINISHED; iv.state = STW_DONE; iv.doneUs = iv.endUs; break; }
            if (iv.rest > 0){ iv.phase = STW_PHASE_REST; iv.endUs += (int64_t)iv.rest * US; continue; }
            iv.round++;
            iv.endUs += (int64_t)iv.work * US;
            continue;
        }
        if (iv.phase == STW_PHASE_REST){ iv.phase = STW_PHASE_WORK; iv.round++; iv.endUs += (int64_t)iv.work * US; continue; }
        break;
    }
    return changes;
}

int64_t stw_Intervals_Phase_Left(const Stw_Intervals_t& iv, const Stw_Settings_t& s, int64_t now){
    switch (iv.state){
        case STW_RUNNING: return iv.endUs > now ? iv.endUs - now : 0;
        case STW_PAUSED: return iv.leftUs;
        case STW_DONE: return 0;
        default: (void)s; return (int64_t)STW_PREP_SECONDS * US;
    }
}

int64_t stw_Intervals_Total_Left(const Stw_Intervals_t& iv, const Stw_Settings_t& s, int64_t now){
    if (iv.state == STW_IDLE) return (int64_t)stw_Intervals_Length(s.work, s.rest, s.rounds) * US;
    if (iv.state == STW_DONE) return 0;
    int64_t left = stw_Intervals_Phase_Left(iv, s, now);
    // The phases after this one.
    const int roundsAfter = iv.rounds - iv.round;
    if (iv.phase == STW_PHASE_PREP) left += (int64_t)stw_Intervals_Length(iv.work, iv.rest, iv.rounds) * US - STW_PREP_SECONDS * US;
    else if (iv.phase == STW_PHASE_WORK) left += (int64_t)roundsAfter * (iv.work + iv.rest) * US;
    else if (iv.phase == STW_PHASE_REST) left += (int64_t)iv.work * US + (int64_t)(roundsAfter - 1) * (iv.work + iv.rest) * US;
    return left;
}

//##############################################################################################################
int64_t stw_Ceil_Seconds(int64_t us){ return us <= 0 ? 0 : (us + US - 1) / US; }

void stw_Format(char* dest, size_t size, int64_t us, int decimals){
    if (us < 0) us = 0;
    const int64_t totalSeconds = us / US;
    const int h = (int)(totalSeconds / 3600), m = (int)(totalSeconds / 60 % 60), s = (int)(totalSeconds % 60);
    char tail[8] = "";
    if (decimals == 1) snprintf(tail, sizeof(tail), ".%d", (int)(us % US / 100000));
    else if (decimals >= 2) snprintf(tail, sizeof(tail), ".%02d", (int)(us % US / 10000));
    if (h) snprintf(dest, size, "%d:%02d:%02d%s", h, m, s, tail);
    else snprintf(dest, size, "%d:%02d%s", m, s, tail);
}
