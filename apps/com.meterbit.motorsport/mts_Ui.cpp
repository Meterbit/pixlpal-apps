// Motorsport Live's screens, 256 x 118 under the status bar: the weekend, live timing, the result, the championships.
#include "mts_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TOP MTB_KIT_TOP
static const uint16_t FAV_BG = MTB_KIT_RGB(70, 52, 0);

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

static void fit_Text(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t bg = BLACK){
    char ascii[128], fitted[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    mtb_Kit_Fit(fitted, sizeof(fitted), font, ascii, maxWidth);
    mtb_Kit_Text(x, y, font, color, fitted, bg);
}

static void upper(char* dest, size_t size, const char* text){
    mtb_Kit_To_Ascii(dest, size, text);
    mtb_Kit_Upper(dest);
}

static void message(const char* head, const char* text){
    clear();
    mtb_Kit_Text_Centre(128, TOP + 22, Terminal8x12, KIT_GOLD, head);
    char ascii[256], lines[6][64];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), text);
    const int n = mtb_Kit_Wrap(ascii, Terminal6x8, 240, &lines[0][0], sizeof(lines[0]), 6);
    for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, TOP + 44 + i * 11, Terminal6x8, KIT_TEXT, lines[i]);
}

static int year_Of(uint32_t t){
    const time_t tt = (time_t)t;
    struct tm u;
    gmtime_r(&tt, &u);
    return u.tm_year + 1900;
}

static const char* const DAYS[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};

static void day_Time(char* dest, size_t size, uint32_t t, int32_t off, bool hour24){
    const int64_t local = (int64_t)t + off;
    const int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    const int wd = (int)(((days + 3) % 7 + 7) % 7);     // 1970-01-01 was a Thursday
    char c[12];
    mts_Clock(c, sizeof(c), t, off, hour24);
    snprintf(dest, size, "%s %s", DAYS[wd], c);
}

static const char* driver_Code(const Mts_Data_t& d, uint8_t number, char* buf, size_t size){
    const Mts_Driver_t* x = mts_Driver(d.drivers, number);
    if (x && x->code[0]) return x->code;
    snprintf(buf, size, "#%u", number);
    return buf;
}

static uint16_t driver_Color(const Mts_Data_t& d, uint8_t number){
    const Mts_Driver_t* x = mts_Driver(d.drivers, number);
    return x ? x->color : KIT_DIM;
}

// The weekend -------------------------------------------------------------------------------------------------------
static void draw_Track(const Mts_Track_t& t, int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t tick){
    if (t.n < 10) return;
    int16_t maxX = 0, maxY = 0;
    for (int i = 0; i < t.n; i++){ if (t.x[i] > maxX) maxX = t.x[i]; if (t.y[i] > maxY) maxY = t.y[i]; }
    const float k = fminf((float)(w - 4) / (maxX ? maxX : 1), (float)(h - 4) / (maxY ? maxY : 1));
    const int16_t ox = (int16_t)(x0 + (w - maxX * k) / 2), oy = (int16_t)(y0 + (h - maxY * k) / 2);
    auto px = [&](int i){ return (int16_t)lroundf(ox + t.x[i] * k); };
    auto py = [&](int i){ return (int16_t)lroundf(oy + t.y[i] * k); };
    for (int i = 0; i < t.n; i++){
        const int j = (i + 1) % t.n;
        mtb_Panel_Draw_Line(px(i), py(i), px(j), py(j), MTB_KIT_RGB(120, 120, 130));
    }
    // A car going round, and the start.
    const int c = tick % t.n;
    mtb_Panel_Fill_Rect(px(0) - 1, py(0) - 1, px(0) + 1, py(0) + 1, KIT_TEXT);
    mtb_Panel_Fill_Rect(px(c) - 1, py(c) - 1, px(c) + 1, py(c) + 1, KIT_RED);
}

static void page_Weekend(const Mts_View_t& v){
    const Mts_Data_t& d = *v.d;
    const Mts_Schedule_t& sc = d.sched;
    const bool h24 = v.s->hour24;
    char a[64], b[48];
    clear();
    upper(a, sizeof(a), mts_Meeting_Name(sc, d.weekend));
    // The round: the weekend's place among its year's race weekends.
    int round = 0, rounds = 0, year = -1;
    uint32_t lastMeeting = 0;
    for (int i = 0; i < sc.count; i++) if (sc.s[i].meeting == d.weekend){ year = year_Of(sc.s[i].start); break; }
    for (int i = 0; i < sc.count; i++){
        const Mts_Session_t& x = sc.s[i];
        if (x.type != MTS_RACE || x.cancelled || x.meeting == lastMeeting || year_Of(x.start) != year) continue;
        lastMeeting = x.meeting;
        rounds++;
        if (x.meeting == d.weekend) round = rounds;
    }
    fit_Text(2, TOP + 2, Terminal6x8, KIT_GOLD, a, 200);
    if (round){ snprintf(b, sizeof(b), "R%d", round); mtb_Kit_Text_Right(253, TOP + 2, Terminal6x8, KIT_LABEL, b); }

    // The weekend's sessions, in this device's time.
    int y = TOP + 13, next = -1;
    const Mts_Session_t* first = nullptr;
    for (int i = 0; i < sc.count; i++){
        const Mts_Session_t& x = sc.s[i];
        if (x.meeting != d.weekend) continue;
        if (!first) first = &x;
        const bool live = v.now + 60 >= x.start && v.now <= x.end;
        const bool done = v.now > x.end;
        if (!done && !live && next < 0) next = i;
        day_Time(a, sizeof(a), x.start, v.utcOffset, h24);
        upper(b, sizeof(b), x.name);
        const uint16_t c = x.cancelled ? KIT_DIM : live ? KIT_RED : done ? KIT_DIM : (next == i ? KIT_TEXT : KIT_LABEL);
        mtb_Kit_Text(2, y, Terminal4x6, c, a);
        fit_Text(48, y, Terminal4x6, c, x.cancelled ? "CANCELLED" : b, 100);
        if (live) mtb_Kit_Text(130, y, Terminal4x6, KIT_RED, "LIVE");
        y += 8;
        if (y > TOP + 62) break;
    }
    if (first){ upper(a, sizeof(a), first->circuit); upper(b, sizeof(b), first->country); snprintf(a + strlen(a), sizeof(a) - strlen(a), ", %s", b); }
    if (first) fit_Text(2, TOP + 70, Terminal4x6, KIT_LABEL, a, 150);
    draw_Track(d.track, 152, TOP + 11, 102, 70, v.tick);
    if (d.track.laps){ snprintf(b, sizeof(b), "%u LAPS", d.track.laps); mtb_Kit_Text_Right(253, TOP + 76, Terminal4x6, KIT_DIM, b); }

    // The countdown.
    const int li = mts_Live_Session(sc, v.now);
    const Mts_Session_t* ls = li >= 0 ? &sc.s[li] : nullptr;
    if (ls && v.now + 60 >= ls->start && v.now <= ls->end){
        upper(b, sizeof(b), ls->name);
        snprintf(a, sizeof(a), "%s LIVE", b);
        mtb_Kit_Text(2, TOP + 86, Terminal8x12, KIT_RED, a);
        mts_Countdown(b, sizeof(b), ls->end - v.now);
        snprintf(a, sizeof(a), "%s TO GO", b);
        mtb_Kit_Text(2, TOP + 102, Terminal6x8, KIT_TEXT, a);
    } else if (next >= 0){
        const Mts_Session_t& x = sc.s[next];
        upper(b, sizeof(b), x.name);
        snprintf(a, sizeof(a), "%s IN", b);
        fit_Text(2, TOP + 84, Terminal6x8, KIT_LABEL, a, 250);
        mts_Countdown(b, sizeof(b), x.start - v.now);
        mtb_Kit_Text(2, TOP + 95, Terminal10x16, x.type == MTS_RACE ? KIT_GOLD : KIT_TEXT, b);
    } else {
        const int nx = mts_Next_Session(sc, v.now);
        if (nx >= 0){
            upper(b, sizeof(b), mts_Meeting_Name(sc, sc.s[nx].meeting));
            snprintf(a, sizeof(a), "NEXT: %s", b);
            fit_Text(2, TOP + 86, Terminal6x8, KIT_LABEL, a, 250);
            mts_Countdown(b, sizeof(b), sc.s[nx].start - v.now);
            mtb_Kit_Text(2, TOP + 98, Terminal8x12, KIT_TEXT, b);
        } else mtb_Kit_Text(2, TOP + 90, Terminal8x12, KIT_LABEL, "SEASON OVER");
    }
}

// Live timing -------------------------------------------------------------------------------------------------------
static void page_Live(const Mts_View_t& v){
    const Mts_Data_t& d = *v.d;
    const Mts_Live_t& l = d.live;
    const int li = mts_Live_Session(d.sched, v.now);
    char a[64], b[40], c[16];
    if (li < 0){ message("NO SESSION ON", "Live timing shows here while a session runs."); return; }
    const Mts_Session_t& ls = d.sched.s[li];
    if (d.liveBlocked || !d.liveOk){
        upper(b, sizeof(b), ls.name);
        if (v.now < ls.start){ mts_Countdown(c, sizeof(c), ls.start - v.now); snprintf(a, sizeof(a), "%s IN %s", b, c); }
        else if (v.now <= ls.end){ mts_Countdown(c, sizeof(c), ls.end - v.now); snprintf(a, sizeof(a), "%s: %s TO GO", b, c); }
        else snprintf(a, sizeof(a), "%s: FINISHED", b);
        message(a, d.liveBlocked ? "Live timing needs an OpenF1 sponsor account (add it from the phone). The result shows here about 30 minutes after the flag."
                                 : "Waiting for live timing...");
        return;
    }
    clear();
    const bool race = l.type == MTS_RACE || l.type == MTS_SPRINT;
    // The header: the session, the flag, the lap or the clock.
    upper(b, sizeof(b), ls.name);
    int16_t x = mtb_Kit_Text(2, TOP + 1, Terminal6x8, KIT_TEXT, b);
    if (l.flag != MTS_FLAG_NONE && l.flag != MTS_FLAG_GREEN){
        const char* f = mts_Flag_Text(l.flag);
        const int16_t w = mtb_Kit_Text_Width(Terminal4x6, f);
        mtb_Panel_Fill_Rect(x + 4, TOP, x + 8 + w, TOP + 8, mts_Flag_Color(l.flag));
        mtb_Kit_Text(x + 6, TOP + 2, Terminal4x6, BLACK, f, mts_Flag_Color(l.flag));
    }
    if (race){
        if (l.laps) snprintf(a, sizeof(a), "LAP %u/%u", l.lap, l.laps); else snprintf(a, sizeof(a), "LAP %u", l.lap);
    } else if (v.now < ls.end){ mts_Countdown(c, sizeof(c), ls.end - v.now); snprintf(a, sizeof(a), "%s LEFT", c); }
    else snprintf(a, sizeof(a), "FINISHED");
    mtb_Kit_Text_Right(253, TOP + 1, Terminal6x8, KIT_GOLD, a);

    // Ten cars at a time.
    uint8_t order[MTS_DRIVERS];
    const int n = mts_Live_Order(l, order, MTS_DRIVERS);
    const int from = v.half && n > 10 ? 10 : 0;
    const float leaderBest = n ? l.car[order[0]].best : NAN;
    for (int r = 0; r < 10 && from + r < n; r++){
        const Mts_Car_t& k = l.car[order[from + r]];
        const int16_t y = (int16_t)(TOP + 12 + r * 10);
        const bool fav = v.s->favourite && k.number == v.s->favourite;
        if (fav) mtb_Panel_Fill_Rect(0, y - 1, 255, y + 8, FAV_BG);
        const uint16_t bg = fav ? FAV_BG : BLACK;
        snprintf(a, sizeof(a), "%2d", k.position ? k.position : from + r + 1);
        mtb_Kit_Text(1, y, Terminal6x8, KIT_TEXT, a, bg);
        mtb_Panel_Fill_Rect(15, y, 16, y + 7, driver_Color(d, k.number));
        mtb_Kit_Text(19, y, Terminal6x8, KIT_TEXT, driver_Code(d, k.number, c, sizeof(c)), bg);
        if (race){
            // The gap to the car ahead, and to the leader.
            if (k.position == 1) snprintf(a, sizeof(a), "LEADER");
            else if (k.lapsDown) mts_Gap_Text(a, sizeof(a), NAN, k.lapsDown, false);
            else mts_Gap_Text(a, sizeof(a), k.interval, 0, false);
            mtb_Kit_Text_Right(104, y, Terminal6x8, KIT_TEXT, a, bg);
            if (k.position > 1 && !k.lapsDown){ mts_Gap_Text(a, sizeof(a), k.gap, 0, false); mtb_Kit_Text_Right(158, y + 1, Terminal4x6, KIT_LABEL, a, bg); }
        } else {
            mts_Lap_Time(a, sizeof(a), k.best);
            mtb_Kit_Text_Right(104, y, Terminal6x8, KIT_TEXT, a, bg);
            if (r + from > 0 && !isnan(k.best) && !isnan(leaderBest)){ snprintf(a, sizeof(a), "+%.3f", (double)(k.best - leaderBest)); mtb_Kit_Text_Right(158, y + 1, Terminal4x6, KIT_LABEL, a, bg); }
        }
        if (l.fastest == k.number) mtb_Kit_Text(162, y + 1, Terminal4x6, MTB_KIT_RGB(190, 60, 255), "FL", bg);
        // Tyres and stops.
        if (k.compound){
            snprintf(a, sizeof(a), "%c", mts_Compound_Letter(k.compound));
            mtb_Panel_Draw_Circle(184, y + 3, 4, mts_Compound_Color(k.compound));
            mtb_Kit_Text(182, y, Terminal4x6, KIT_TEXT, a, bg);
            snprintf(a, sizeof(a), "%u", k.tyreLaps);
            mtb_Kit_Text(192, y + 1, Terminal4x6, KIT_LABEL, a, bg);
        }
        if (race && k.pits){ snprintf(a, sizeof(a), "%u STOP%s", k.pits, k.pits > 1 ? "S" : ""); mtb_Kit_Text_Right(253, y + 1, Terminal4x6, KIT_LABEL, a, bg); }
        else if (!race && k.lap){ snprintf(a, sizeof(a), "%u LAPS", k.lap); mtb_Kit_Text_Right(253, y + 1, Terminal4x6, KIT_LABEL, a, bg); }
    }
    if (n > 10){ snprintf(a, sizeof(a), "%d-%d", from + 1, from + 10 < n ? from + 10 : n); mtb_Kit_Text_Right(253, PANEL_RES_Y - 7, Terminal4x6, KIT_DIM, a); }
}

// The result ---------------------------------------------------------------------------------------------------------
static void page_Result(const Mts_View_t& v){
    const Mts_Data_t& d = *v.d;
    const Mts_Result_t& r = d.result;
    char a[64], b[48], c[16];
    if (!r.count){ message("RESULTS", "The last session's result shows here once it is out."); return; }
    clear();
    const Mts_Session_t* x = nullptr;
    for (int i = 0; i < d.sched.count; i++) if (d.sched.s[i].key == r.session) x = &d.sched.s[i];
    if (x){
        upper(b, sizeof(b), mts_Meeting_Name(d.sched, x->meeting));
        char* gp = strstr(b, " GRAND PRIX");
        if (gp) memcpy(gp, " GP", 4);
        upper(c, sizeof(c), x->name);
        snprintf(a, sizeof(a), "%s: %s", b, c);
    } else snprintf(a, sizeof(a), "RESULT");
    fit_Text(2, TOP + 1, Terminal6x8, KIT_GOLD, a, 252);
    const bool race = r.type == MTS_RACE || r.type == MTS_SPRINT;
    for (int i = 0; i < r.count && i < 20; i++){
        const Mts_Result_Row_t& k = r.r[i];
        const int16_t x0 = i < 10 ? 0 : 128, y = (int16_t)(TOP + 12 + (i % 10) * 10);
        const bool fav = v.s->favourite && k.number == v.s->favourite;
        const uint16_t bg = fav ? FAV_BG : BLACK;
        if (fav) mtb_Panel_Fill_Rect(x0, y - 1, x0 + 126, y + 8, FAV_BG);
        if (k.position) snprintf(a, sizeof(a), "%2d", k.position); else snprintf(a, sizeof(a), "--");
        mtb_Kit_Text(x0 + 1, y, Terminal6x8, KIT_TEXT, a, bg);
        mtb_Panel_Fill_Rect(x0 + 15, y, x0 + 16, y + 7, driver_Color(d, k.number));
        mtb_Kit_Text(x0 + 19, y, Terminal6x8, KIT_TEXT, driver_Code(d, k.number, c, sizeof(c)), bg);
        if (k.dsq) snprintf(a, sizeof(a), "DSQ");
        else if (k.dns) snprintf(a, sizeof(a), "DNS");
        else if (k.dnf || !k.position) snprintf(a, sizeof(a), "DNF");
        else if (k.position == 1){ if (race) snprintf(a, sizeof(a), "WINNER"); else mts_Lap_Time(a, sizeof(a), k.best); }
        else if (race) mts_Gap_Text(a, sizeof(a), k.gap, k.lapsDown, false);
        else if (!isnan(k.gap)) snprintf(a, sizeof(a), "+%.3f", (double)k.gap);
        else mts_Lap_Time(a, sizeof(a), k.best);
        mtb_Kit_Text_Right(x0 + (race && k.points > 0 ? 104 : 124), y + 1, Terminal4x6, k.position == 1 ? KIT_GOLD : KIT_LABEL, a, bg);
        if (race && k.points > 0){ snprintf(a, sizeof(a), "%g", (double)k.points); mtb_Kit_Text_Right(x0 + 124, y + 1, Terminal4x6, KIT_GREEN, a, bg); }
    }
}

// The championships --------------------------------------------------------------------------------------------------
static void page_Drivers(const Mts_View_t& v){
    const Mts_Data_t& d = *v.d;
    const Mts_Standings_t& s = d.standings;
    char a[48], c[16];
    if (!s.drivers){ message("DRIVERS", "The championship shows here after the season's first race."); return; }
    clear();
    mtb_Kit_Text(2, TOP + 1, Terminal6x8, KIT_GOLD, "DRIVERS' CHAMPIONSHIP");
    for (int i = 0; i < s.drivers && i < 20; i++){
        const auto& k = s.d[i];
        const int16_t x0 = i < 10 ? 0 : 128, y = (int16_t)(TOP + 12 + (i % 10) * 10);
        const bool fav = v.s->favourite && k.number == v.s->favourite;
        const uint16_t bg = fav ? FAV_BG : BLACK;
        if (fav) mtb_Panel_Fill_Rect(x0, y - 1, x0 + 126, y + 8, FAV_BG);
        snprintf(a, sizeof(a), "%2d", k.position);
        mtb_Kit_Text(x0 + 1, y, Terminal6x8, KIT_TEXT, a, bg);
        mtb_Panel_Fill_Rect(x0 + 15, y, x0 + 16, y + 7, driver_Color(d, k.number));
        mtb_Kit_Text(x0 + 19, y, Terminal6x8, KIT_TEXT, driver_Code(d, k.number, c, sizeof(c)), bg);
        snprintf(a, sizeof(a), "%g", (double)k.points);
        mtb_Kit_Text_Right(x0 + 94, y, Terminal6x8, KIT_TEXT, a, bg);
        if (k.gained > 0){ snprintf(a, sizeof(a), "+%g", (double)k.gained); mtb_Kit_Text_Right(x0 + 124, y + 1, Terminal4x6, KIT_GREEN, a, bg); }
        if (k.before && k.before != k.position) mtb_Kit_Arrow(x0 + 100, y + 4, 5, k.before > k.position, k.before > k.position ? KIT_GREEN : KIT_RED);
    }
}

static void page_Teams(const Mts_View_t& v){
    const Mts_Data_t& d = *v.d;
    const Mts_Standings_t& s = d.standings;
    char a[48], b[32];
    if (!s.teams){ message("TEAMS", "The championship shows here after the season's first race."); return; }
    clear();
    mtb_Kit_Text(2, TOP + 1, Terminal6x8, KIT_GOLD, "TEAMS' CHAMPIONSHIP");
    const float top = s.t[0].points > 0 ? s.t[0].points : 1;
    for (int i = 0; i < s.teams && i < 11; i++){
        const auto& k = s.t[i];
        const int16_t y = (int16_t)(TOP + 11 + i * 9);
        const uint16_t color = mts_Color_Of_Team(d.drivers, k.team);
        snprintf(a, sizeof(a), "%2d", k.position);
        mtb_Kit_Text(1, y, Terminal6x8, KIT_TEXT, a);
        upper(b, sizeof(b), k.team);
        fit_Text(17, y, Terminal6x8, KIT_TEXT, b, 92);
        mtb_Kit_Bar(112, y + 2, 78, 4, k.points / top, color, KIT_LINE);
        snprintf(a, sizeof(a), "%g", (double)k.points);
        mtb_Kit_Text_Right(222, y, Terminal6x8, KIT_TEXT, a);
        if (k.gained > 0){ snprintf(a, sizeof(a), "+%g", (double)k.gained); mtb_Kit_Text_Right(253, y + 1, Terminal4x6, KIT_GREEN, a); }
    }
}

bool mts_Page_Ready(const Mts_Data_t& d, uint8_t page, uint32_t now){
    switch (page){
        case MTS_P_WEEKEND: return d.schedOk;
        case MTS_P_LIVE: return mts_Live_Session(d.sched, now) >= 0;
        case MTS_P_RESULT: return d.result.count > 0;
        case MTS_P_DRIVERS: return d.standings.drivers > 0;
        case MTS_P_TEAMS: return d.standings.teams > 0;
        default: return false;
    }
}

void mts_Draw(const Mts_View_t& v){
    if (v.message){ message("MOTORSPORT LIVE", v.message); return; }
    switch (v.page){
        case MTS_P_LIVE: page_Live(v); break;
        case MTS_P_RESULT: page_Result(v); break;
        case MTS_P_DRIVERS: page_Drivers(v); break;
        case MTS_P_TEAMS: page_Teams(v); break;
        default:
            if (!v.d->schedOk) message("MOTORSPORT LIVE", v.d->why[0] ? v.d->why : "Reading the season...");
            else page_Weekend(v);
            break;
    }
}
