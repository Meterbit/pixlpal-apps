#include "stw_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>

#define US 1000000LL

static const uint16_t K_TAB = MTB_KIT_RGB(25, 30, 45);
static const uint16_t K_WORK = MTB_KIT_RGB(20, 160, 60);
static const uint16_t K_REST = MTB_KIT_RGB(30, 90, 200);
static const uint16_t K_PREP = MTB_KIT_RGB(200, 140, 0);
static const uint16_t K_DONE = MTB_KIT_RGB(120, 50, 170);
static const uint16_t K_ALARM = MTB_KIT_RGB(200, 20, 20);

uint16_t stw_Theme_Color(uint8_t theme){
    static const uint16_t COLORS[5] = {MTB_KIT_RGB(80, 220, 255), MTB_KIT_RGB(60, 230, 110), MTB_KIT_RGB(255, 180, 30),
                                       MTB_KIT_RGB(240, 240, 240), MTB_KIT_RGB(255, 60, 60)};
    return COLORS[theme < 5 ? theme : 0];
}

// Segment digits for "12:34" or "1:02:03", @p h tall, centred on @p cx. Returns the right edge.
static int16_t stw_Width(const char* text, int16_t h){
    const int16_t w = h * 55 / 100, gap = h / 10, colon = h / 6;
    int16_t total = 0;
    for (const char* c = text; *c; c++) total += (*c == ':' ? colon : w) + gap;
    return total - gap;
}

static int16_t stw_Digits(int16_t cx, int16_t y, int16_t h, const char* text, uint16_t color){
    const int16_t w = h * 55 / 100, gap = h / 10, colon = h / 6, t = h / 9 > 2 ? h / 9 : 2;
    int16_t x = cx - stw_Width(text, h) / 2;
    const uint16_t ghost = mtb_Kit_Dim(color, 22);
    for (const char* c = text; *c; c++){
        if (*c == ':'){
            const int16_t s = t, mx = x + (colon - s) / 2;
            mtb_Panel_Fill_Rect(mx, y + h * 3 / 10, mx + s - 1, y + h * 3 / 10 + s - 1, color);
            mtb_Panel_Fill_Rect(mx, y + h * 7 / 10 - s, mx + s - 1, y + h * 7 / 10 - 1, color);
            x += colon + gap;
        } else {
            mtb_Kit_Segment_Digit(x, y, w, h, t, *c - '0', color, ghost);
            x += w + gap;
        }
    }
    return x - gap;
}

// The time as large as fits in @p maxWidth.
static int16_t stw_Fit_Digits(int16_t cx, int16_t y, int16_t maxHeight, int16_t maxWidth, const char* text, uint16_t color){
    int16_t h = maxHeight;
    while (h > 16 && stw_Width(text, h) > maxWidth) h -= 2;
    return stw_Digits(cx, y + (maxHeight - h) / 2, h, text, color);
}

static void stw_Tabs(uint8_t mode, uint16_t accent){
    static const char* const NAMES[STW_MODE_COUNT] = {"STOPWATCH", "TIMER", "INTERVALS"};
    mtb_Panel_Fill_Rect(0, 10, 255, 20, K_TAB);
    for (int i = 0; i < STW_MODE_COUNT; i++){
        const int16_t x0 = i * 86, x1 = i == STW_MODE_COUNT - 1 ? 255 : x0 + 84;
        const bool on = i == mode;
        if (on) mtb_Panel_Fill_Rect(x0, 10, x1, 20, accent);
        mtb_Kit_Text_Centre((x0 + x1) / 2, 12, Terminal6x8, on ? BLACK : KIT_LABEL, NAMES[i], on ? accent : K_TAB);
    }
}

static void stw_Hint(const char* text){
    char fitted[96];
    mtb_Kit_Fit(fitted, sizeof(fitted), Terminal4x6, text, 248);
    mtb_Kit_Text(4, 121, Terminal4x6, KIT_DIM, fitted);
}

// A timer running in another mode, at the bottom right above the hint.
static void stw_Elsewhere(const Stw_View_t& v){
    char text[32] = "", time[16];
    uint16_t color = KIT_LABEL;
    const uint8_t mode = v.settings->mode;
    if (mode != STW_MODE_TIMER && (v.timer->state == STW_RUNNING || v.timer->state == STW_DONE)){
        if (v.timer->state == STW_DONE){ snprintf(text, sizeof(text), "TIMER DONE"); color = KIT_RED; }
        else { stw_Format(time, sizeof(time), stw_Ceil_Seconds(stw_Timer_Left(*v.timer, v.settings->timerSeconds, v.nowUs)) * US, 0); snprintf(text, sizeof(text), "TIMER %s", time); color = KIT_GOLD; }
    } else if (mode != STW_MODE_INTERVALS && v.intervals->state == STW_RUNNING){
        stw_Format(time, sizeof(time), stw_Ceil_Seconds(stw_Intervals_Total_Left(*v.intervals, *v.settings, v.nowUs)) * US, 0);
        snprintf(text, sizeof(text), "INTERVALS %s", time);
        color = v.intervals->phase == STW_PHASE_REST ? K_REST : K_WORK;
    } else if (mode != STW_MODE_STOPWATCH && v.watch->running){
        stw_Format(time, sizeof(time), stw_Watch_Elapsed(*v.watch, v.nowUs), 0);
        snprintf(text, sizeof(text), "STOPWATCH %s", time);
        color = KIT_CYAN;
    }
    if (text[0]) mtb_Kit_Text_Right(252, 111, Terminal4x6, color, text);
}

//##############################################################################################################
static void stw_Draw_Watch(const Stw_View_t& v, uint16_t accent){
    const Stw_Watch_t& w = *v.watch;
    const int64_t elapsed = stw_Watch_Elapsed(w, v.nowUs);
    char text[24];
    stw_Format(text, sizeof(text), elapsed, 0);
    const bool hours = elapsed >= 3600 * US;
    const int16_t right = stw_Fit_Digits(84, 26, 46, hours ? 164 : 128, text, w.running || elapsed ? accent : mtb_Kit_Dim(accent, 110));
    // Hundredths (tenths past an hour) beside the seconds.
    if (v.settings->hundredths || hours){
        char tail[8];
        if (hours) snprintf(tail, sizeof(tail), ".%d", (int)(elapsed % US / 100000));
        else snprintf(tail, sizeof(tail), ".%02d", (int)(elapsed % US / 10000));
        mtb_Kit_Text(hours ? 136 : right + 3, hours ? 76 : 55, Terminal10x17, mtb_Kit_Dim(accent, 190), tail);
    }
    const char* state = w.running ? "RUNNING" : elapsed ? "STOPPED" : "READY";
    const int16_t x = mtb_Kit_Text(4, 96, Terminal6x8, w.running ? KIT_GREEN : elapsed ? KIT_ORANGE : KIT_LABEL, state);
    if (w.lapCount && elapsed){
        char lap[32], t[16];
        stw_Format(t, sizeof(t), elapsed - w.laps[w.lapCount - 1], 1);
        snprintf(lap, sizeof(lap), "LAP %d  %s", w.lapCount + 1, t);
        mtb_Kit_Text(x + 10, 96, Terminal6x8, KIT_TEXT, lap);
    }
    // The laps, newest first.
    mtb_Panel_Draw_VLine(174, 23, 106, KIT_LINE);
    mtb_Kit_Text(180, 24, Terminal4x6, KIT_LABEL, w.lapCount ? "LAPS" : "NO LAPS YET");
    int best, worst;
    stw_Watch_Best_Worst(w, best, worst);
    for (int r = 0; r < 7 && r < w.lapCount; r++){
        const int i = w.lapCount - 1 - r;
        char row[24], t[16];
        stw_Format(t, sizeof(t), stw_Watch_Lap_Time(w, i), 1);
        snprintf(row, sizeof(row), "%2d %s", i + 1, t);
        mtb_Kit_Text(180, 32 + r * 11, Terminal6x8, i == best ? KIT_GREEN : i == worst ? KIT_RED : KIT_TEXT, row);
    }
    stw_Elsewhere(v);
    stw_Hint(w.running ? "CLICK: STOP    DOUBLE CLICK: LAP    HOLD: NEXT TIMER" : elapsed ? "CLICK: GO ON    DOUBLE CLICK: RESET    HOLD: NEXT TIMER"
                                                                               : "CLICK: START    HOLD: NEXT TIMER");
}

static void stw_Draw_Timer(const Stw_View_t& v, uint16_t accent){
    const Stw_Timer_t& t = *v.timer;
    const uint32_t length = t.state == STW_IDLE ? v.settings->timerSeconds : t.seconds;
    char text[24];
    if (t.state == STW_DONE){
        // At zero: the panel flashes (if allowed) and counts the time over.
        const bool bright = v.flash && v.settings->flash;
        const uint16_t bg = bright ? K_ALARM : BLACK;
        mtb_Panel_Fill_Rect(0, 22, 255, 119, bg);
        mtb_Kit_Big_Text(128 - mtb_Kit_Big_Text_Width(Terminal10x17, 2, "TIME'S UP") / 2, 30, Terminal10x17, 2, bright ? KIT_TEXT : K_ALARM, "TIME'S UP");
        char over[32];
        stw_Format(text, sizeof(text), v.nowUs > t.doneUs ? v.nowUs - t.doneUs : 0, 0);
        snprintf(over, sizeof(over), "+%s", text);
        mtb_Kit_Text_Centre(128, 72, Terminal10x17, bright ? KIT_TEXT : KIT_GOLD, over, bg);
        stw_Format(text, sizeof(text), (int64_t)length * US, 0);
        char line[40];
        snprintf(line, sizeof(line), "%s TIMER ENDED", text);
        mtb_Kit_Text_Centre(128, 94, Terminal6x8, bright ? KIT_TEXT : KIT_LABEL, line, bg);
        stw_Hint("CLICK: STOP THE ALARM");
        return;
    }
    const int64_t left = stw_Timer_Left(t, v.settings->timerSeconds, v.nowUs);
    const int64_t shown = stw_Ceil_Seconds(left);
    stw_Format(text, sizeof(text), shown * US, 0);
    const uint16_t color = t.state == STW_RUNNING && shown <= 10 ? KIT_GOLD : t.state == STW_PAUSED ? mtb_Kit_Dim(accent, 140) : accent;
    stw_Fit_Digits(86, 26, 50, 164, text, color);
    const float fraction = length ? (float)left / ((float)length * US) : 0;
    mtb_Kit_Bar(4, 84, 164, 4, fraction, t.state == STW_RUNNING ? accent : KIT_DIM);
    const char* state = t.state == STW_RUNNING ? "RUNNING" : t.state == STW_PAUSED ? "PAUSED" : "TURN THE KNOB TO SET";
    mtb_Kit_Text(4, 96, Terminal6x8, t.state == STW_RUNNING ? KIT_GREEN : t.state == STW_PAUSED ? KIT_ORANGE : KIT_LABEL, state);
    mtb_Panel_Draw_VLine(174, 23, 106, KIT_LINE);
    mtb_Kit_Text(180, 26, Terminal4x6, KIT_LABEL, "LENGTH");
    stw_Format(text, sizeof(text), (int64_t)length * US, 0);
    mtb_Kit_Text(180, 34, Terminal8x12, KIT_TEXT, text);
    if (t.state == STW_RUNNING && v.endsAt){
        mtb_Kit_Text(180, 54, Terminal4x6, KIT_LABEL, "ENDS AT");
        mtb_Kit_Text(180, 62, Terminal8x12, KIT_GOLD, v.endsAt);
    }
    stw_Elsewhere(v);
    stw_Hint(t.state == STW_RUNNING ? "CLICK: PAUSE    DOUBLE CLICK: RESET    HOLD: NEXT TIMER"
           : t.state == STW_PAUSED ? "CLICK: GO ON    DOUBLE CLICK: RESET    HOLD: NEXT TIMER" : "TURN: SET    CLICK: START    HOLD: NEXT TIMER");
}

static void stw_Draw_Intervals(const Stw_View_t& v){
    const Stw_Intervals_t& iv = *v.intervals;
    const Stw_Settings_t& s = *v.settings;
    const bool idle = iv.state == STW_IDLE;
    const uint16_t work = idle ? s.work : iv.work, rest = idle ? s.rest : iv.rest;
    const uint8_t rounds = idle ? s.rounds : iv.rounds;
    static const char* const PHASES[] = {"GET READY", "WORK", "REST", "FINISHED"};
    const uint8_t phase = idle ? (uint8_t)STW_PHASE_PREP : iv.phase;
    uint16_t band = idle ? K_TAB : phase == STW_PHASE_WORK ? K_WORK : phase == STW_PHASE_REST ? K_REST : phase == STW_PHASE_PREP ? K_PREP : K_DONE;
    uint16_t ink = KIT_TEXT;
    if (v.flash && !idle){ ink = band; band = KIT_TEXT; }          // A phase has just changed.
    mtb_Panel_Fill_Rect(0, 22, 255, 39, band);
    mtb_Kit_Text(4, 23, Terminal10x17, ink, idle ? "READY" : PHASES[phase < 4 ? phase : 0], band);
    char text[32], t2[16];
    if (iv.state == STW_PAUSED) mtb_Kit_Text_Centre(128, 27, Terminal6x8, ink, "PAUSED", band);
    snprintf(text, sizeof(text), "ROUND %d / %d", idle ? 1 : iv.round, rounds);
    mtb_Kit_Text_Right(252, 27, Terminal6x8, ink, text, band);

    const int64_t phaseLeft = idle ? (int64_t)work * US : stw_Intervals_Phase_Left(iv, s, v.nowUs);
    const int64_t shown = stw_Ceil_Seconds(phaseLeft);
    stw_Format(text, sizeof(text), shown * US, 0);
    const uint16_t color = iv.state == STW_DONE ? K_DONE : !idle && shown <= 3 && iv.state == STW_RUNNING ? KIT_GOLD
                         : phase == STW_PHASE_REST && !idle ? MTB_KIT_RGB(110, 170, 255) : MTB_KIT_RGB(120, 240, 150);
    stw_Fit_Digits(86, 44, 46, 164, text, color);
    const uint32_t phaseLength = stw_Phase_Seconds(phase, work, rest);
    mtb_Kit_Bar(4, 96, 164, 3, idle || !phaseLength ? 0 : (float)phaseLeft / ((float)phaseLength * US), band == KIT_TEXT ? ink : band);

    mtb_Panel_Draw_VLine(174, 42, 106, KIT_LINE);
    mtb_Kit_Text(180, 44, Terminal4x6, KIT_LABEL, idle ? "WORKOUT" : "TIME LEFT");
    stw_Format(text, sizeof(text), stw_Ceil_Seconds(stw_Intervals_Total_Left(iv, s, v.nowUs)) * US, 0);
    mtb_Kit_Text(180, 52, Terminal8x12, KIT_TEXT, text);
    stw_Format(text, sizeof(text), (int64_t)work * US, 0);
    stw_Format(t2, sizeof(t2), (int64_t)rest * US, 0);
    char line[40];
    snprintf(line, sizeof(line), "WORK %s", text);
    mtb_Kit_Text(180, 70, Terminal6x8, K_WORK, line);
    snprintf(line, sizeof(line), rest ? "REST %s" : "NO REST", t2);
    mtb_Kit_Text(180, 81, Terminal6x8, MTB_KIT_RGB(90, 150, 255), line);
    if (!idle && iv.state != STW_DONE){
        const char* next = phase == STW_PHASE_PREP || phase == STW_PHASE_REST ? "WORK" : iv.round >= iv.rounds ? "DONE" : rest ? "REST" : "WORK";
        snprintf(line, sizeof(line), "NEXT: %s", next);
        mtb_Kit_Text(180, 93, Terminal4x6, KIT_LABEL, line);
    }
    stw_Elsewhere(v);
    stw_Hint(idle ? "TURN: ROUNDS    CLICK: START    HOLD: NEXT TIMER" : iv.state == STW_DONE ? "CLICK: CLEAR    HOLD: NEXT TIMER"
                  : iv.state == STW_RUNNING ? "CLICK: PAUSE    DOUBLE CLICK: RESET    HOLD: NEXT TIMER" : "CLICK: GO ON    DOUBLE CLICK: RESET    HOLD: NEXT TIMER");
}

void stw_Draw(const Stw_View_t& v){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    const uint16_t accent = stw_Theme_Color(v.settings->theme);
    stw_Tabs(v.settings->mode, accent);
    switch (v.settings->mode){
        case STW_MODE_TIMER: stw_Draw_Timer(v, accent); break;
        case STW_MODE_INTERVALS: stw_Draw_Intervals(v); break;
        default: stw_Draw_Watch(v, accent); break;
    }
}
