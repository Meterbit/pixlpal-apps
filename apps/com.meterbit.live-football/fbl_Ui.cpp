#include "fbl_Ui_Screens.h"
#include "fbl_Ui_Kit.h"
#include <stdio.h>
#include <string.h>

static const int16_t BOARD_Y = 21, ROW_H = 10, ROWS = 9;

void fbl_Band(const char* left, const char* right, uint16_t background){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, 19, background);
    char fitted[48];
    const int16_t rightX = right && right[0] ? fbl_Text_Right(253, 12, Terminal4x6, K_TEXT, right, background) : 254;
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, left, rightX - 8);
    fbl_Text(3, 11, Terminal6x8, K_TEXT, fitted, background);
}

void fbl_Footer(const char* left, const char* right, uint16_t rightColor){
    mtb_Panel_Fill_Rect(0, 118, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (left && left[0]) fbl_Text(3, 120, Terminal4x6, K_DIM, left);
    if (right && right[0]) fbl_Text_Right(253, 120, Terminal4x6, rightColor, right);
}

static uint32_t fbl_Local_Day(uint32_t t){
    const time_t when = (time_t)t;
    struct tm tm;
    localtime_r(&when, &tm);
    return (uint32_t)fbl_Days_From_Civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

void fbl_Kickoff_Text(char* dest, size_t size, uint32_t kickoff, uint32_t now, bool withDay){
    const time_t when = (time_t)kickoff;
    struct tm tm;
    localtime_r(&when, &tm);
    const int32_t days = (int32_t)fbl_Local_Day(kickoff) - (int32_t)fbl_Local_Day(now);
    if (!withDay || days == 0) strftime(dest, size, withDay ? "TODAY %H:%M" : "%H:%M", &tm);
    else if (days == 1) strftime(dest, size, "TOMORROW %H:%M", &tm);
    else if (days > 1 && days < 7) strftime(dest, size, "%a %H:%M", &tm);
    else strftime(dest, size, "%a %d %b %H:%M", &tm);
    fbl_Upper(dest);
}

const char* fbl_League_Name(uint16_t leagueId){
    const Fbl_League_t* league = fbl_Find_League(leagueId);
    return league ? league->name : "FOOTBALL";
}

void fbl_Minute_Text(char* dest, size_t size, const Fbl_Match_t& match){
    const uint8_t phase = fbl_Phase(match.status);
    if (match.awaitingFinal){ snprintf(dest, size, "FT"); return; }
    if (phase == FBL_PHASE_LIVE){
        if (strcmp(match.status, "P") == 0) snprintf(dest, size, "PENS");
        else if (match.extra) snprintf(dest, size, "%u+%u'", match.elapsed, match.extra);
        else snprintf(dest, size, "%u'", match.elapsed);
        return;
    }
    if (phase == FBL_PHASE_BREAK || phase == FBL_PHASE_FINISHED || phase == FBL_PHASE_OFF){ snprintf(dest, size, "%s", match.status); return; }
    snprintf(dest, size, "--");
}

const char* fbl_Match_League(const Fbl_Match_t& match){
    const Fbl_League_t* league = fbl_Find_League(match.leagueId);
    return league ? league->name : match.league[0] ? match.league : "FOOTBALL";
}

uint16_t fbl_Minute_Color(const Fbl_Match_t& match){
    switch (fbl_Phase(match.status)){
        case FBL_PHASE_LIVE:  return match.awaitingFinal ? K_LABEL : K_RED;
        case FBL_PHASE_BREAK: return K_GOLD;
        case FBL_PHASE_OFF:   return K_DIM;
        default:              return K_LABEL;
    }
}

//##############################################################################################################
// A board's matches: by league in the order followed (other leagues of the favourite last), then by kick-off.
void fbl_Ui_Board_List(const Fbl_View_t& view, uint8_t board, Fbl_Board_List_t& list){
    list.count = 0;
    if (view.data == nullptr) return;
    const int32_t today = (int32_t)fbl_Local_Day(view.now);
    const Fbl_Data_t& d = *view.data;
    auto rank = [&](uint16_t leagueId){
        if (view.settings) for (uint8_t i = 0; i < view.settings->followCount; i++) if (view.settings->follow[i] == leagueId) return (int)i;
        return 100 + leagueId;
    };
    for (int i = 0; i < d.matchCount; i++){
        const int32_t day = (int32_t)fbl_Local_Day(d.matches[i].kickoff) - today;
        const bool wanted = board == FBL_BOARD_AHEAD ? day > 1 : day == (int32_t)board - 1;
        if (wanted) list.index[list.count++] = (uint8_t)i;
    }
    for (int i = 1; i < list.count; i++){
        const uint8_t v = list.index[i];
        int k = i - 1;
        while (k >= 0){
            const Fbl_Match_t& a = d.matches[list.index[k]];
            const Fbl_Match_t& b = d.matches[v];
            const bool after = board == FBL_BOARD_AHEAD ? a.kickoff > b.kickoff
                             : (rank(a.leagueId) > rank(b.leagueId) || (rank(a.leagueId) == rank(b.leagueId) && a.kickoff > b.kickoff));
            if (!after) break;
            list.index[k + 1] = list.index[k];
            k--;
        }
        list.index[k + 1] = v;
    }
}

// The lines of a board: a league heading before each league's first match on a page.
struct Fbl_Line_t { uint8_t heading; uint8_t index; };

static int fbl_Board_Lines(const Fbl_View_t& view, const Fbl_Board_List_t& list, Fbl_Line_t* lines, int maxLines){
    int count = 0;
    uint16_t lastLeague = 0xFFFF;
    for (int i = 0; i < list.count && count + 2 <= maxLines; i++){
        const Fbl_Match_t& m = view.data->matches[list.index[i]];
        // A heading for each league; never alone at the foot of a page.
        if (m.leagueId != lastLeague){
            if (count % ROWS == ROWS - 1) lines[count++] = {2, 0};
            lines[count++] = {1, list.index[i]};
            lastLeague = m.leagueId;
        }
        lines[count++] = {0, list.index[i]};
    }
    return count;
}

uint8_t fbl_Ui_Board_Pages(const Fbl_View_t& view, uint8_t board){
    Fbl_Board_List_t list;
    fbl_Ui_Board_List(view, board, list);
    if (list.count == 0) return 0;
    Fbl_Line_t lines[FBL_MAX_MATCHES * 2 + 16];
    const int count = fbl_Board_Lines(view, list, lines, sizeof(lines) / sizeof(lines[0]));
    return (uint8_t)((count + ROWS - 1) / ROWS);
}

static void fbl_Board_Row(const Fbl_View_t& view, const Fbl_Match_t& m, int16_t y){
    const bool favourite = view.settings && fbl_Match_Is_Favourite(m, view.settings->favourites);
    const uint16_t background = favourite ? K_FAV_BG : BLACK;
    if (favourite) mtb_Panel_Fill_Rect(0, y - 1, PANEL_RES_X - 1, y + 8, K_FAV_BG);
    const uint8_t phase = fbl_Phase(m.status);
    char text[24], fitted[32];
    if (phase == FBL_PHASE_UPCOMING) fbl_Kickoff_Text(text, sizeof(text), m.kickoff, view.now, false);
    else fbl_Minute_Text(text, sizeof(text), m);
    const uint16_t timeColor = phase == FBL_PHASE_UPCOMING ? K_LABEL : fbl_Minute_Color(m);
    fbl_Text(3, y, Terminal6x8, timeColor, text, background);
    if (fbl_In_Play(m)) mtb_Panel_Fill_Circle(36, y + 3, 1, K_RED);
    const uint16_t nameColor = favourite ? K_GOLD : K_TEXT;
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, m.team[0].name, 72);
    fbl_Text_Right(113, y, Terminal6x8, nameColor, fitted, background);
    if (m.goals[0] >= 0){
        snprintf(text, sizeof(text), "%d-%d", m.goals[0], m.goals[1]);
        const uint16_t box = fbl_In_Play(m) ? FBL_RGB(90, 0, 0) : FBL_RGB(36, 36, 36);
        mtb_Panel_Fill_Rect(117, y - 1, 139, y + 7, box);
        fbl_Text_Centre(128, y, Terminal6x8, K_TEXT, text, box);
    } else fbl_Text_Centre(128, y, Terminal6x8, K_DIM, "v", background);
    fbl_Fit(fitted, sizeof(fitted), Terminal6x8, m.team[1].name, 72);
    const int16_t after = fbl_Text(143, y, Terminal6x8, nameColor, fitted, background);
    if (m.penalties[0] >= 0 && after < 236){
        snprintf(text, sizeof(text), "P%d-%d", m.penalties[0], m.penalties[1]);
        fbl_Text_Right(253, y + 1, Terminal4x6, K_GOLD, text, background);
    }
}

static void fbl_Draw_Board(const Fbl_View_t& view){
    static const char* const TITLE[] = {"RESULTS  YESTERDAY", "TODAY", "TOMORROW", "COMING UP"};
    Fbl_Board_List_t list;
    fbl_Ui_Board_List(view, view.board, list);
    char title[48], right[24];
    if (view.board == FBL_BOARD_TODAY && view.clockValid){
        const time_t now = (time_t)view.now;
        struct tm tm;
        localtime_r(&now, &tm);
        char date[24];
        strftime(date, sizeof(date), "%a %d %b", &tm);
        fbl_Upper(date);
        snprintf(title, sizeof(title), "TODAY  %s", date);
    } else snprintf(title, sizeof(title), "%s", TITLE[view.board & 3]);
    int live = 0;
    for (int i = 0; i < list.count; i++) if (fbl_In_Play(view.data->matches[list.index[i]])) live++;
    Fbl_Line_t lines[FBL_MAX_MATCHES * 2 + 16];
    const int count = fbl_Board_Lines(view, list, lines, sizeof(lines) / sizeof(lines[0]));
    const int pages = (count + ROWS - 1) / ROWS;
    if (live) snprintf(right, sizeof(right), "%d LIVE", live);
    else if (pages > 1) snprintf(right, sizeof(right), "%u/%d", (unsigned)(view.page % pages + 1), pages);
    else right[0] = '\0';
    fbl_Band(title, right, K_BAND);
    const int first = (view.page % (pages ? pages : 1)) * ROWS;
    for (int i = first; i < count && i < first + ROWS; i++){
        const int16_t y = BOARD_Y + (i - first) * ROW_H + 1;
        const Fbl_Match_t& m = view.data->matches[lines[i].index];
        if (lines[i].heading == 2) continue;
        if (lines[i].heading == 1){
            char league[40];
            snprintf(league, sizeof(league), "%s%s", fbl_Match_League(m), m.round[0] ? "  " : "");
            const int16_t end = fbl_Text(3, y + 2, Terminal4x6, K_GREEN, league);
            fbl_Text(end, y + 2, Terminal4x6, K_DIM, m.round);
            mtb_Panel_Draw_HLine(y + 8, end + fbl_Text_Width(Terminal4x6, m.round) + 4, 253, K_LINE);
            continue;
        }
        fbl_Board_Row(view, m, y);
    }
    // Plan and quota, bottom left.
    char left[72] = "";
    const Fbl_Quota_t& q = view.data->quota;
    if (q.seen) snprintf(left, sizeof(left), "%s PLAN  %u/%u REQUESTS LEFT TODAY", q.plan[0] ? q.plan : "API", (unsigned)q.remaining, (unsigned)q.limitDay);
    fbl_Upper(left);
    char updated[24] = "";
    const uint32_t fetched = view.data->dayFetched[view.board <= FBL_BOARD_TOMORROW ? view.board : 1];
    if (fetched){
        const time_t t = (time_t)fetched;
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(updated, sizeof(updated), "UPDATED %H:%M", &tm);
    }
    fbl_Footer(left, updated, K_DIM);
}

//##############################################################################################################
uint8_t fbl_Ui_Table_Pages(const Fbl_View_t& view){
    return view.data ? (uint8_t)((view.data->table.count + 7) / 8) : 0;
}

static void fbl_Draw_Table(const Fbl_View_t& view){
    const Fbl_Table_t& table = view.data->table;
    const int pages = (table.count + 7) / 8;
    char right[16];
    snprintf(right, sizeof(right), "TABLE %u/%d", (unsigned)(view.page % (pages ? pages : 1) + 1), pages);
    fbl_Band(fbl_League_Name(table.leagueId), right, K_BAND);
    fbl_Text(3, 22, Terminal4x6, K_DIM, "#");
    fbl_Text(20, 22, Terminal4x6, K_DIM, "TEAM");
    fbl_Text_Right(150, 22, Terminal4x6, K_DIM, "P");
    fbl_Text_Right(176, 22, Terminal4x6, K_DIM, "GD");
    fbl_Text_Right(202, 22, Terminal4x6, K_DIM, "PTS");
    fbl_Text(212, 22, Terminal4x6, K_DIM, "FORM");
    const int first = (view.page % (pages ? pages : 1)) * 8;
    for (int i = first; i < table.count && i < first + 8; i++){
        const Fbl_Row_t& r = table.rows[i];
        const int16_t y = 30 + (i - first) * 11;
        const bool favourite = view.settings && fbl_Is_Favourite(r.teamId, view.settings->favourites);
        const uint16_t background = favourite ? K_FAV_BG : BLACK;
        if (favourite) mtb_Panel_Fill_Rect(0, y - 1, PANEL_RES_X - 1, y + 8, K_FAV_BG);
        static const uint16_t ZONE[4] = {BLACK, FBL_RGB(0, 120, 255), FBL_RGB(255, 140, 0), FBL_RGB(220, 30, 30)};
        if (r.zone) mtb_Panel_Fill_Rect(0, y - 1, 1, y + 7, ZONE[r.zone & 3]);
        char text[32];
        snprintf(text, sizeof(text), "%u", r.rank);
        fbl_Text_Right(14, y, Terminal6x8, K_LABEL, text, background);
        fbl_Fit(text, sizeof(text), Terminal6x8, r.team, 112);
        fbl_Text(20, y, Terminal6x8, favourite ? K_GOLD : K_TEXT, text, background);
        snprintf(text, sizeof(text), "%u", r.played);
        fbl_Text_Right(150, y, Terminal6x8, K_LABEL, text, background);
        snprintf(text, sizeof(text), "%+d", r.goalDiff);
        fbl_Text_Right(176, y, Terminal6x8, K_LABEL, text, background);
        snprintf(text, sizeof(text), "%u", r.points);
        fbl_Text_Right(202, y, Terminal6x8, K_TEXT, text, background);
        // Form: newest last.
        for (int f = 0; r.form[f] && f < 5; f++){
            const uint16_t c = r.form[f] == 'W' ? K_GREEN : r.form[f] == 'L' ? K_RED : K_LABEL;
            mtb_Panel_Fill_Rect(212 + f * 8, y + 1, 217 + f * 8, y + 6, c);
        }
    }
    fbl_Footer("", "", K_DIM);
}

static void fbl_Draw_Scorers(const Fbl_View_t& view){
    const Fbl_Data_t& d = *view.data;
    fbl_Band(fbl_League_Name(d.scorersLeague), "TOP SCORERS", K_BAND);
    const int top = d.scorerCount ? d.scorers[0].goals : 1;
    for (int i = 0; i < d.scorerCount && i < 8; i++){
        const Fbl_Scorer_t& s = d.scorers[i];
        const int16_t y = 23 + i * 12;
        char text[32];
        snprintf(text, sizeof(text), "%d", i + 1);
        fbl_Text_Right(12, y, Terminal6x8, K_LABEL, text);
        fbl_Fit(text, sizeof(text), Terminal6x8, s.player, 96);
        fbl_Text(18, y, Terminal6x8, i == 0 ? K_GOLD : K_TEXT, text);
        fbl_Fit(text, sizeof(text), Terminal4x6, s.team, 70);
        fbl_Upper(text);
        fbl_Text(118, y + 1, Terminal4x6, K_DIM, text);
        const int16_t bar = (int16_t)(40 * s.goals / (top ? top : 1));
        mtb_Panel_Fill_Rect(194, y + 1, 194 + bar, y + 6, i == 0 ? K_GOLD : K_GREEN);
        snprintf(text, sizeof(text), "%u", s.goals);
        fbl_Text_Right(253, y, Terminal6x8, K_TEXT, text);
    }
    fbl_Footer("", "", K_DIM);
}

static void fbl_Draw_Message(const Fbl_View_t& view){
    fbl_Band("LIVE FOOTBALL", "", K_BAND);
    fbl_Pitch(20, 117);
    mtb_Panel_Fill_Round_Rect(78, 26, 250, 112, 6, K_PLATE);
    fbl_Ball(40, 68, 24);
    fbl_Text(86, 32, Terminal8x12, K_GOLD, view.message ? view.message : "", K_PLATE);
    // Word wrap.
    const char* rest = view.detail ? view.detail : "";
    char line[48];
    for (int16_t y = 50; *rest && y < 108; y += 11){
        size_t take = 0, lastSpace = 0;
        while (rest[take] && take + 1 < sizeof(line)){
            memcpy(line, rest, take + 1);
            line[take + 1] = '\0';
            if (fbl_Text_Width(Terminal6x8, line) > 158) break;
            if (rest[take] == ' ') lastSpace = take;
            take++;
        }
        if (rest[take] && lastSpace) take = lastSpace;
        memcpy(line, rest, take);
        line[take] = '\0';
        fbl_Text(86, y, Terminal6x8, K_TEXT, line, K_PLATE);
        rest += take;
        while (*rest == ' ') rest++;
    }
    fbl_Footer("DATA: API-FOOTBALL (API-SPORTS.IO)", "", K_DIM);
}

//##############################################################################################################
void fbl_Ui_Draw(const Fbl_View_t& view){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    if (view.data == nullptr) return;
    switch (view.screen){
        case FBL_SCREEN_LIVE:      fbl_Draw_Live(view); break;
        case FBL_SCREEN_BOARD:     fbl_Draw_Board(view); break;
        case FBL_SCREEN_COUNTDOWN: fbl_Draw_Countdown(view); break;
        case FBL_SCREEN_TABLE:     fbl_Draw_Table(view); break;
        case FBL_SCREEN_SCORERS:   fbl_Draw_Scorers(view); break;
        case FBL_SCREEN_GOAL:      fbl_Draw_Goal(view); break;
        case FBL_SCREEN_BANNER:    fbl_Draw_Banner(view); break;
        default:                   fbl_Draw_Message(view); break;
    }
}

void fbl_Ui_Animate(const Fbl_View_t& view){
    if (view.data == nullptr) return;
    switch (view.screen){
        case FBL_SCREEN_LIVE:      fbl_Animate_Live(view); break;
        case FBL_SCREEN_COUNTDOWN: fbl_Animate_Countdown(view); break;
        case FBL_SCREEN_GOAL:      fbl_Draw_Goal(view); break;
        case FBL_SCREEN_BANNER:    if (view.frame < 8) fbl_Draw_Banner(view); break;
        default: break;
    }
}
