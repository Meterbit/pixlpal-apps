#include "fbl_Ui_Screens.h"
#include "fbl_Ui_Kit.h"
#include "fbl_Crest.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Live layout.
static const int16_t PITCH_Y1 = 20, PITCH_Y2 = 72;
static const int16_t PLATE_W = 58;
static const int16_t BOARD_X1 = 88, BOARD_X2 = 167, BOARD_Y1 = 24, BOARD_Y2 = 60;
static const int16_t LINE_Y = 81, LINE_X1 = 14, LINE_X2 = 241;

static const Fbl_Match_t* fbl_Match(const Fbl_View_t& view){
    if (view.data == nullptr || view.match < 0 || view.match >= view.data->matchCount) return nullptr;
    return &view.data->matches[view.match];
}

// A team's plate: crest and name, on the left or the right.
static void fbl_Plate(const Fbl_Team_t& team, int side, bool favourite){
    const int16_t x1 = side ? PANEL_RES_X - 3 - PLATE_W : 2, x2 = x1 + PLATE_W;
    mtb_Panel_Fill_Round_Rect(x1, 23, x2, 70, 4, K_PLATE);
    if (favourite) mtb_Panel_Draw_Round_Rect(x1, 23, x2, 70, 4, K_GOLD);
    fbl_Crest(team.id, team.name, x1 + (PLATE_W - FBL_CREST) / 2 + 1, 25, K_PLATE);
    char fitted[32];
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, team.name, PLATE_W - 4);
    fbl_Text_Centre((x1 + x2) / 2 + 1, 59, Terminal6x8, favourite ? K_GOLD : K_TEXT, fitted, K_PLATE);
}

static void fbl_Scoreboard(const Fbl_Match_t& match, int highlight){
    mtb_Panel_Fill_Round_Rect(BOARD_X1, BOARD_Y1, BOARD_X2, BOARD_Y2, 5, BLACK);
    mtb_Panel_Draw_Round_Rect(BOARD_X1, BOARD_Y1, BOARD_X2, BOARD_Y2, 5, K_LINE);
    const bool started = match.goals[0] >= 0;
    if (!started){
        fbl_Text_Centre(128, 36, Terminal10x16, K_LABEL, "v");
        return;
    }
    const int16_t w = (match.goals[0] >= 10 || match.goals[1] >= 10) ? 11 : 16, h = 28, t = 4;
    fbl_Segment_Number_Right(122, 28, w, h, t, match.goals[0], highlight == 0 ? K_GOLD : K_TEXT);
    mtb_Panel_Fill_Rect(125, 41, 130, 43, K_LABEL);
    fbl_Segment_Number(134, 28, w, h, t, match.goals[1], highlight == 1 ? K_GOLD : K_TEXT);
    if (match.penalties[0] >= 0){
        char text[24];
        snprintf(text, sizeof(text), "PENS %d-%d", match.penalties[0], match.penalties[1]);
        fbl_Text_Centre(128, 57 - 2, Terminal4x6, K_GOLD, text);
    }
}

// The minute in a pill under the score; the dot in it pulses while the ball is in play.
static void fbl_Minute_Pill(const Fbl_Match_t& match, uint32_t frame){
    char text[16];
    fbl_Minute_Text(text, sizeof(text), match);
    const uint16_t color = fbl_Minute_Color(match);
    const int16_t width = fbl_Text_Width(Terminal6x8, text) + (fbl_Phase(match.status) == FBL_PHASE_LIVE ? 18 : 12);
    const int16_t x1 = 128 - width / 2, x2 = x1 + width;
    for (int16_t x = x1 - 1; x <= x2 + 1; x++) mtb_Panel_Draw_VLine(x, 62, 72, fbl_Turf_At(x));
    mtb_Panel_Fill_Round_Rect(x1, 63, x2, 71, 3, color);
    if (fbl_Phase(match.status) == FBL_PHASE_LIVE){
        const bool on = (frame / 5) % 2 == 0;
        mtb_Panel_Fill_Circle(x1 + 6, 67, 2, on ? K_TEXT : color);
        fbl_Text(x1 + 12, 64, Terminal6x8, BLACK, text, color);
    } else fbl_Text_Centre(128, 64, Terminal6x8, BLACK, text, color);
}

// 0 to 90 minutes (120 with extra time): home events above the line, away below.
static void fbl_Timeline(const Fbl_Match_t& match){
    mtb_Panel_Fill_Rect(0, 73, PANEL_RES_X - 1, 89, BLACK);
    const bool extraTime = match.elapsed > 90 || strcmp(match.status, "ET") == 0 || strcmp(match.status, "BT") == 0 || strcmp(match.status, "P") == 0
                        || strcmp(match.status, "AET") == 0 || strcmp(match.status, "PEN") == 0;
    const int span = extraTime ? 120 : 90;
    auto xOf = [&](int minute){ if (minute > span) minute = span; return (int16_t)(LINE_X1 + (LINE_X2 - LINE_X1) * minute / span); };
    mtb_Panel_Draw_HLine(LINE_Y, LINE_X1, LINE_X2, K_LINE);
    const uint8_t phase = fbl_Phase(match.status);
    const int played = phase == FBL_PHASE_FINISHED ? span : match.elapsed;
    if (played > 0) mtb_Panel_Fill_Rect(LINE_X1, LINE_Y, xOf(played), LINE_Y, phase == FBL_PHASE_FINISHED ? K_LABEL : K_GREEN);
    mtb_Panel_Draw_VLine(xOf(45), LINE_Y - 2, LINE_Y + 2, K_LABEL);
    if (extraTime) mtb_Panel_Draw_VLine(xOf(90), LINE_Y - 2, LINE_Y + 2, K_LABEL);
    if (phase == FBL_PHASE_LIVE) mtb_Panel_Fill_Circle(xOf(played), LINE_Y, 1, K_TEXT);
    fbl_Text(2, LINE_Y - 2, Terminal4x6, K_DIM, "0");
    fbl_Text_Right(253, LINE_Y - 2, Terminal4x6, K_DIM, extraTime ? "120" : "90");
    for (int i = 0; i < match.eventCount; i++){
        const Fbl_Event_t& e = match.events[i];
        const int16_t x = xOf(e.minute);
        const int16_t y = e.away ? LINE_Y + 5 : LINE_Y - 5;
        switch (e.type){
            case FBL_EV_GOAL: case FBL_EV_PENALTY: fbl_Mark_Goal(x, y, K_TEXT); break;
            case FBL_EV_OWN_GOAL:                  fbl_Mark_Goal(x, y, K_RED); break;
            case FBL_EV_YELLOW:                    fbl_Mark_Card(x - 1, e.away ? LINE_Y + 3 : LINE_Y - 7, K_YELLOW); break;
            case FBL_EV_RED:                       fbl_Mark_Card(x - 1, e.away ? LINE_Y + 3 : LINE_Y - 7, K_RED); break;
            case FBL_EV_SUB:                       mtb_Panel_Draw_PixelRGB565(x, e.away ? LINE_Y + 2 : LINE_Y - 2, K_CYAN); break;
            default: break;
        }
    }
}

// "Saka 23' 67'(P)", per player, goals then red cards; home right-aligned to the centre, away from it.
static void fbl_Scorers(const Fbl_Match_t& match){
    mtb_Panel_Fill_Rect(0, 90, PANEL_RES_X - 1, 117, BLACK);
    for (int side = 0; side < 2; side++){
        int line = 0;
        bool done[FBL_MAX_EVENTS] = {};
        for (int i = 0; i < match.eventCount && line < 3; i++){
            const Fbl_Event_t& e = match.events[i];
            const bool goal = e.type == FBL_EV_GOAL || e.type == FBL_EV_PENALTY || e.type == FBL_EV_OWN_GOAL;
            if (done[i] || e.away != side || !(goal || e.type == FBL_EV_RED)) continue;
            char text[48];
            size_t used = snprintf(text, sizeof(text), "%s", fbl_Short_Player(e.player));
            for (int k = i; k < match.eventCount && used < sizeof(text) - 10; k++){
                const Fbl_Event_t& o = match.events[k];
                const bool sameKind = (o.type == FBL_EV_RED) == (e.type == FBL_EV_RED);
                if (done[k] || o.away != side || strcmp(o.player, e.player) != 0 || !sameKind) continue;
                if (o.type != FBL_EV_RED && !(o.type == FBL_EV_GOAL || o.type == FBL_EV_PENALTY || o.type == FBL_EV_OWN_GOAL)) continue;
                done[k] = true;
                if (o.extra) used += snprintf(text + used, sizeof(text) - used, " %u+%u'", o.minute, o.extra);
                else used += snprintf(text + used, sizeof(text) - used, " %u'", o.minute);
                if (o.type == FBL_EV_PENALTY) used += snprintf(text + used, sizeof(text) - used, "(P)");
                if (o.type == FBL_EV_OWN_GOAL) used += snprintf(text + used, sizeof(text) - used, "(OG)");
            }
            char fitted[48];
            fbl_Fit(fitted, sizeof(fitted), Terminal6x8, text, 108);
            const int16_t y = 92 + line * 9;
            const bool red = e.type == FBL_EV_RED;
            if (side == 0){
                fbl_Text_Right(112, y, Terminal6x8, red ? K_RED : K_TEXT, fitted);
                if (red) fbl_Mark_Card(117, y + 1, K_RED); else fbl_Mark_Goal(118, y + 3, K_TEXT);
            } else {
                if (red) fbl_Mark_Card(137, y + 1, K_RED); else fbl_Mark_Goal(138, y + 3, K_TEXT);
                fbl_Text(144, y, Terminal6x8, red ? K_RED : K_TEXT, fitted);
            }
            line++;
        }
    }
}

void fbl_Draw_Live(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match == nullptr) return;
    char right[32];
    snprintf(right, sizeof(right), "%s", match->round);
    fbl_Band(fbl_Match_League(*match), right, K_BAND);
    fbl_Pitch(PITCH_Y1, PITCH_Y2);
    for (int s = 0; s < 2; s++) fbl_Plate(match->team[s], s, view.settings && fbl_Is_Favourite(match->team[s].id, view.settings->favourites));
    fbl_Scoreboard(*match, -1);
    fbl_Minute_Pill(*match, view.frame);
    fbl_Timeline(*match);
    fbl_Scorers(*match);
    char left[40], status[24];
    fbl_Fit(left, sizeof(left), Terminal4x6, match->venue, 140);
    fbl_Upper(left);
    if (view.liveCount > 1) snprintf(status, sizeof(status), "%u/%u LIVE", (unsigned)(view.liveIndex + 1), (unsigned)view.liveCount);
    else if (view.status && view.status->liveInterval >= 60) snprintf(status, sizeof(status), "EVERY %u MIN", (unsigned)(view.status->liveInterval / 60));
    else status[0] = '\0';
    fbl_Footer(left, status, K_RED);
}

void fbl_Animate_Live(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match && fbl_Phase(match->status) == FBL_PHASE_LIVE && view.frame % 5 == 0) fbl_Minute_Pill(*match, view.frame);
}

//##############################################################################################################
// The favourite's next match: the two plates, and the time to kick-off in big digits.
static void fbl_Countdown_Digits(const Fbl_View_t& view, const Fbl_Match_t& match){
    mtb_Panel_Fill_Rect(64, 30, 191, 58, BLACK);
    const uint32_t left = match.kickoff > view.now ? match.kickoff - view.now : 0;
    const int16_t w = 10, h = 18, t = 3;
    int values[3];
    const char* labels[3];
    if (left >= 86400){ values[0] = left / 86400; values[1] = left / 3600 % 24; values[2] = left / 60 % 60; labels[0] = "DAYS"; labels[1] = "HRS"; labels[2] = "MIN"; }
    else { values[0] = left / 3600; values[1] = left / 60 % 60; values[2] = left % 60; labels[0] = "HRS"; labels[1] = "MIN"; labels[2] = "SEC"; }
    for (int g = 0; g < 3; g++){
        const int16_t cx = 84 + g * 44;
        fbl_Segment_Digit(cx - w - 2, 32, w, h, t, values[g] / 10 % 10, K_TEXT);
        fbl_Segment_Digit(cx + 2, 32, w, h, t, values[g] % 10, K_TEXT);
        fbl_Text_Centre(cx, 53, Terminal4x6, K_LABEL, labels[g]);
        if (g < 2){ mtb_Panel_Fill_Rect(cx + 21, 37, cx + 22, 38, K_GOLD); mtb_Panel_Fill_Rect(cx + 21, 44, cx + 22, 45, K_GOLD); }
    }
}

void fbl_Draw_Countdown(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match == nullptr) return;
    char title[48];
    snprintf(title, sizeof(title), "NEXT MATCH  %s", fbl_Match_League(*match));
    fbl_Band(title, match->round, K_BAND);
    for (int s = 0; s < 2; s++) fbl_Plate(match->team[s], s, view.settings && fbl_Is_Favourite(match->team[s].id, view.settings->favourites));
    fbl_Text_Centre(128, 22, Terminal4x6, K_GOLD, "KICK-OFF IN");
    fbl_Countdown_Digits(view, *match);
    char text[48];
    fbl_Kickoff_Text(text, sizeof(text), match->kickoff, view.now, true);
    fbl_Text_Centre(128, 78, Terminal8x12, K_TEXT, text);
    char fitted[48];
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, match->venue, 240);
    fbl_Text_Centre(128, 95, Terminal6x8, K_LABEL, fitted);
    fbl_Ball(128, 110, 6);
    fbl_Footer("", "", K_LABEL);
}

void fbl_Animate_Countdown(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match && view.frame % 10 == 0) fbl_Countdown_Digits(view, *match);
}

//##############################################################################################################
// GOAL! Confetti falls, the word pulses, then who scored and the new score.
void fbl_Draw_Goal(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match == nullptr) return;
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    static const uint16_t CONFETTI[] = {K_GOLD, K_RED, K_GREEN, K_CYAN, K_TEXT, FBL_RGB(255, 90, 200)};
    for (int i = 0; i < 48; i++){
        const uint32_t seed = (uint32_t)i * 2654435761u;
        const int16_t x = (int16_t)(seed % PANEL_RES_X);
        const int16_t speed = 2 + (int16_t)(seed >> 8) % 4;
        const int16_t y = 10 + (int16_t)((view.frame * speed + (seed >> 12) % 118) % 118);
        const int16_t drift = (int16_t)(2 * sinf((view.frame + i) * 0.3f));
        int16_t cx = x + drift;
        if (cx < 0) cx = 0;
        if (cx > PANEL_RES_X - 2) cx = PANEL_RES_X - 2;
        if (y > PANEL_RES_Y - 2) continue;
        mtb_Panel_Fill_Rect(cx, y, cx + 1, y + 1, CONFETTI[i % 6]);
    }
    // The word, growing in, then pulsing between white and gold.
    const int scale = view.frame < 2 ? 1 : view.frame < 4 ? 2 : 3;
    const uint16_t color = (view.frame / 3) % 2 ? K_TEXT : K_GOLD;
    const int16_t width = fbl_Big_Text_Width(Terminal10x16, scale, "GOAL!");
    fbl_Big_Text(128 - width / 2, 36 - 8 * scale, Terminal10x16, scale, color, "GOAL!");
    if (view.frame < 6) return;

    const int side = view.goalSide ? 1 : 0;
    const Fbl_Team_t& team = match->team[side];
    fbl_Crest(team.id, team.name, 6, 70, BLACK);
    char fitted[40];
    fbl_Fit(fitted, sizeof(fitted), Terminal8x12, team.name, 140);
    fbl_Text(42, 72, Terminal8x12, K_GOLD, fitted);
    // The newest goal of that side.
    for (int i = match->eventCount - 1; i >= 0; i--){
        const Fbl_Event_t& e = match->events[i];
        if (e.away != side || !(e.type == FBL_EV_GOAL || e.type == FBL_EV_PENALTY || e.type == FBL_EV_OWN_GOAL)) continue;
        char text[48];
        snprintf(text, sizeof(text), "%s %u'%s", e.player, e.minute, e.type == FBL_EV_PENALTY ? " (PEN)" : e.type == FBL_EV_OWN_GOAL ? " (OG)" : "");
        fbl_Fit(fitted, sizeof(fitted), Terminal6x8, text, 150);
        fbl_Text(42, 87, Terminal6x8, K_TEXT, fitted);
        break;
    }
    // The score, the scoring side's number in gold.
    char home[8], away[8], number[8];
    fbl_Team_Code(home, sizeof(home), match->team[0].name);
    fbl_Team_Code(away, sizeof(away), match->team[1].name);
    int16_t x = fbl_Text(42, 102, Terminal10x16, K_LABEL, home) + 8;
    snprintf(number, sizeof(number), "%d", match->goals[0] < 0 ? 0 : match->goals[0]);
    x = fbl_Text(x, 102, Terminal10x16, side == 0 ? K_GOLD : K_TEXT, number) + 4;
    x = fbl_Text(x, 102, Terminal10x16, K_LABEL, "-") + 4;
    snprintf(number, sizeof(number), "%d", match->goals[1] < 0 ? 0 : match->goals[1]);
    x = fbl_Text(x, 102, Terminal10x16, side == 1 ? K_GOLD : K_TEXT, number) + 8;
    fbl_Text(x, 102, Terminal10x16, K_LABEL, away);
    char minute[16];
    fbl_Minute_Text(minute, sizeof(minute), *match);
    fbl_Text_Right(252, 106, Terminal6x8, K_RED, minute);
}

// Red card, kick-off, full time.
void fbl_Draw_Banner(const Fbl_View_t& view){
    const Fbl_Match_t* match = fbl_Match(view);
    if (match == nullptr) return;
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    const char* word = view.banner == FBL_BANNER_RED ? "RED CARD" : view.banner == FBL_BANNER_KICKOFF ? "KICK-OFF" : "FULL TIME";
    const uint16_t color = view.banner == FBL_BANNER_RED ? K_RED : view.banner == FBL_BANNER_KICKOFF ? K_GREEN : K_TEXT;
    if (view.banner == FBL_BANNER_RED){
        // The card, raised.
        const int16_t lift = view.frame < 6 ? (int16_t)(6 - view.frame) * 6 : 0;
        mtb_Panel_Fill_Round_Rect(14, 18 + lift, 44, 62 + lift, 3, K_RED);
        mtb_Panel_Draw_Round_Rect(14, 18 + lift, 44, 62 + lift, 3, K_TEXT);
    } else fbl_Ball(29, 40, 18);
    const int16_t width = fbl_Big_Text_Width(Terminal10x16, 2, word);
    fbl_Big_Text(148 - width / 2 + 2, 22, Terminal10x16, 2, color, word);
    if (view.banner == FBL_BANNER_RED){
        for (int i = match->eventCount - 1; i >= 0; i--){
            const Fbl_Event_t& e = match->events[i];
            if (e.type != FBL_EV_RED) continue;
            char text[48], fitted[48];
            snprintf(text, sizeof(text), "%s %u'  %s", e.player, e.minute, match->team[e.away].name);
            fbl_Fit(fitted, sizeof(fitted), Terminal6x8, text, 190);
            fbl_Text_Centre(148, 60, Terminal6x8, K_TEXT, fitted);
            break;
        }
    }
    // The match.
    mtb_Panel_Draw_HLine(74, 0, PANEL_RES_X - 1, K_LINE);
    fbl_Crest(match->team[0].id, match->team[0].name, 6, 82, BLACK);
    fbl_Crest(match->team[1].id, match->team[1].name, 220, 82, BLACK);
    char fitted[32];
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, match->team[0].name, 70);
    fbl_Text(40, 88, Terminal6x8, K_TEXT, fitted);
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, match->team[1].name, 70);
    fbl_Text_Right(215, 88, Terminal6x8, K_TEXT, fitted);
    if (match->goals[0] >= 0){
        fbl_Segment_Number_Right(122, 98, 10, 18, 3, match->goals[0], K_TEXT);
        mtb_Panel_Fill_Rect(125, 106, 130, 108, K_LABEL);
        fbl_Segment_Number(134, 98, 10, 18, 3, match->goals[1], K_TEXT);
    }
    fbl_Text_Centre(128, 77, Terminal4x6, K_LABEL, fbl_Match_League(*match));
}
