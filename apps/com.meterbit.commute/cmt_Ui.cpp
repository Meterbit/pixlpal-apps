#include "cmt_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint16_t K_BACK = BLACK;
static const uint16_t K_ACCENT = MTB_KIT_RGB(255, 196, 0);
static const int16_t TOP = 21, FOOT = 119;

static int16_t text_Fit(int16_t x, int16_t y, const uint8_t* font, uint16_t color, const char* text, int16_t maxWidth, uint16_t back = K_BACK){
    char a[160], b[160];
    mtb_Kit_To_Ascii(a, sizeof(a), text ? text : "");
    mtb_Kit_Fit(b, sizeof(b), font, a, maxWidth);
    return mtb_Kit_Text(x, y, font, color, b, back);
}

static void clock_Text(char* dest, size_t size, uint32_t t, bool hour24){
    const time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (hour24) snprintf(dest, size, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(dest, size, "%d:%02d %s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

static void board_Title(const Cmt_Settings_t& s, const Cmt_Board_t& b, char* dest, size_t size){
    if (b.label[0]) snprintf(dest, size, "%s", b.label);
    else if (b.kind == CMT_DRIVE_BOARD) snprintf(dest, size, "%s TO %s", cmt_Place_Name(b.from), cmt_Place_Name(b.to));
    else snprintf(dest, size, "%s", b.stopName[0] ? b.stopName : "Departures");
    (void)s;
}

uint32_t cmt_Leave_At(const Cmt_Board_t& b, const Cmt_Result_t& r, uint32_t now, const Cmt_Departure_t** which){
    if (which) *which = nullptr;
    for (int i = 0; i < r.count; i++){
        const Cmt_Departure_t& d = r.departures[i];
        if (d.cancelled) continue;
        const uint32_t leave = d.time - (uint32_t)b.walkMinutes * 60;
        if (leave + 20 < now) continue;                  // too late for this one: the next
        if (which) *which = &d;
        return leave;
    }
    return 0;
}

static void header(const Cmt_View_t& v, const char* title, const char* sub){
    int16_t x = text_Fit(4, 12, Terminal4x6, K_ACCENT, title, 150);
    if (sub && sub[0]) text_Fit(x + 6, 12, Terminal4x6, KIT_LABEL, sub, 200 - x);
    char t[16];
    clock_Text(t, sizeof(t), v.now, v.settings->hour24);
    mtb_Kit_Text_Right(253, 12, Terminal4x6, KIT_TEXT, t, K_BACK);
    mtb_Panel_Draw_HLine(19, 0, 255, KIT_LINE);
}

static void footer_Text(const char* left, uint16_t leftColor, const char* right){
    mtb_Panel_Fill_Rect(0, FOOT - 2, 255, 127, K_BACK);
    if (left) text_Fit(4, FOOT, Terminal4x6, leftColor, left, right ? 180 : 248);
    if (right) mtb_Kit_Text_Right(253, FOOT, Terminal4x6, KIT_DIM, right, K_BACK);
}

static void message(const char* title, const char* detail){
    mtb_Kit_Text_Centre(128, 44, Terminal8x12, KIT_TEXT, title, K_BACK);
    if (detail){
        char a[200], lines[3][64];
        mtb_Kit_To_Ascii(a, sizeof(a), detail);
        const int n = mtb_Kit_Wrap(a, Terminal6x8, 240, &lines[0][0], sizeof(lines[0]), 3);
        for (int i = 0; i < n; i++) mtb_Kit_Text_Centre(128, 62 + i * 10, Terminal6x8, KIT_LABEL, lines[i], K_BACK);
    }
}

static void age_Text(char* dest, size_t size, const Cmt_Result_t& r, uint32_t now){
    if (r.fetchedAt == 0 || now < r.fetchedAt){ dest[0] = '\0'; return; }
    char ago[24];
    mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - r.fetchedAt));
    mtb_Kit_Upper(ago);
    snprintf(dest, size, "UPDATED %s", ago);
}

// A stop's departures ----------------------------------------------------------------------------------------------
static void view_Departures(const Cmt_View_t& v, const Cmt_Board_t& b, const Cmt_Result_t& r){
    char title[48];
    board_Title(*v.settings, b, title, sizeof(title));
    mtb_Kit_Upper(title);
    header(v, title, b.label[0] ? b.stopName : (b.lines[0] ? b.lines : ""));
    if (r.state == CMT_FAILED && r.count == 0){ message("No departures shown", r.error); footer_Text(nullptr, 0, "TRANSITLAND"); return; }
    if (r.state != CMT_OK && r.count == 0){ message(v.online ? "Loading departures" : "Waiting for the internet", nullptr); return; }
    int16_t y = TOP + 1;
    int shown = 0;
    for (int i = 0; i < r.count && y + 11 <= FOOT - 3; i++){
        const Cmt_Departure_t& d = r.departures[i];
        if (d.time + 30 < v.now) continue;
        // The line's badge, in its own colours.
        const int16_t bw = 30;
        mtb_Panel_Fill_Rect(4, y, 4 + bw - 1, y + 9, d.color);
        char line[16];
        mtb_Kit_Fit(line, sizeof(line), Terminal6x8, d.line, bw - 4);
        mtb_Kit_Text_Centre(4 + bw / 2, y + 1, Terminal6x8, d.textColor, line, d.color);
        // When: green when live, with any delay.
        char when[16];
        cmt_Minutes_Text(when, sizeof(when), d.time, v.now, v.settings->hour24);
        int16_t right;
        if (d.cancelled) right = mtb_Kit_Text_Right(252, y + 1, Terminal6x8, KIT_RED, "CANCELLED", K_BACK);
        else {
            right = mtb_Kit_Text_Right(252, y + 1, Terminal6x8, d.realtime ? KIT_GREEN : KIT_TEXT, when, K_BACK);
            if (d.delay >= 60){
                char late[12];
                snprintf(late, sizeof(late), "+%d", (int)(d.delay / 60));
                right = mtb_Kit_Text_Right(right - 4, y + 2, Terminal4x6, KIT_ORANGE, late, K_BACK);
            }
        }
        const int16_t end = text_Fit(38, y + 1, Terminal6x8, d.cancelled ? KIT_DIM : MTB_KIT_RGB(220, 220, 220), d.headsign, right - 44);
        if (d.cancelled) mtb_Panel_Draw_HLine(y + 5, 38, end - 1, KIT_DIM);
        y += 13;
        shown++;
    }
    if (shown == 0) message("No departures soon", b.lines[0] ? "None of the chosen lines in the next 2 hours" : "None in the next 2 hours");
    // Leave in: the next departure less the walk.
    char left[48] = "";
    uint16_t lc = KIT_LABEL;
    if (b.walkMinutes){
        const Cmt_Departure_t* d;
        const uint32_t leave = cmt_Leave_At(b, r, v.now, &d);
        if (leave){
            const int32_t m = ((int32_t)(leave - v.now) + 59) / 60;
            if (m <= 0) snprintf(left, sizeof(left), "LEAVE NOW FOR THE %s", d->line);
            else snprintf(left, sizeof(left), "LEAVE IN %d MIN FOR THE %s", (int)m, d->line);
            lc = m <= 2 ? KIT_RED : m <= 5 ? KIT_GOLD : KIT_GREEN;
        }
    }
    if (!left[0]) age_Text(left, sizeof(left), r, v.now);
    footer_Text(left, lc, "TRANSITLAND");
}

// A drive ------------------------------------------------------------------------------------------------------------
static void view_Drive(const Cmt_View_t& v, const Cmt_Board_t& b, const Cmt_Result_t& r){
    char title[48];
    board_Title(*v.settings, b, title, sizeof(title));
    mtb_Kit_Upper(title);
    header(v, title, v.settings->drive == CMT_GOOGLE ? "GOOGLE" : "TOMTOM");
    if (!r.drive.ok){
        if (r.state == CMT_FAILED) message("No drive time", r.error);
        else message(v.online ? "Working out the drive" : "Waiting for the internet", nullptr);
        return;
    }
    const Cmt_Drive_Result_t& d = r.drive;
    const int minutes = (int)((d.seconds + 59) / 60);
    const int extra = d.normalSeconds && d.seconds > d.normalSeconds ? (int)((d.seconds - d.normalSeconds + 30) / 60) : 0;
    const float ratio = d.normalSeconds ? (float)d.seconds / d.normalSeconds : 1.0f;
    const uint16_t c = ratio < 1.15f ? KIT_GREEN : ratio < 1.5f ? KIT_GOLD : KIT_RED;
    char t[48];
    if (minutes >= 100) snprintf(t, sizeof(t), "%d:%02d", minutes / 60, minutes % 60);
    else snprintf(t, sizeof(t), "%d", minutes);
    mtb_Kit_Big_Text(4, TOP + 3, Terminal10x17, 2, c, t);
    const int16_t after = 4 + mtb_Kit_Big_Text_Width(Terminal10x17, 2, t) + 4;
    mtb_Kit_Text(after, TOP + 23, Terminal6x8, c, minutes >= 100 ? "H" : "MIN", K_BACK);
    // Right column: traffic, arrival, distance, roads.
    const int16_t x = 120;
    if (extra > 0) snprintf(t, sizeof(t), "+%d MIN TRAFFIC", extra);
    else snprintf(t, sizeof(t), "TRAFFIC CLEAR");
    mtb_Kit_Text(x, TOP + 2, Terminal6x8, c, t, K_BACK);
    char clock[16];
    clock_Text(clock, sizeof(clock), v.now + d.seconds, v.settings->hour24);
    snprintf(t, sizeof(t), "ARRIVE %s", clock);
    mtb_Kit_Text(x, TOP + 13, Terminal6x8, KIT_TEXT, t, K_BACK);
    if (v.settings->miles) snprintf(t, sizeof(t), "%.1f MI", d.meters / 1609.34f);
    else snprintf(t, sizeof(t), "%.1f KM", d.meters / 1000.0f);
    mtb_Kit_Text(x, TOP + 24, Terminal4x6, KIT_LABEL, t, K_BACK);
    if (d.via[0]){
        char via[64];
        snprintf(via, sizeof(via), "VIA %s", d.via);
        mtb_Kit_Upper(via);
        text_Fit(x, TOP + 32, Terminal4x6, KIT_LABEL, via, 252 - x);
    }
    // The last hours' drive times.
    if (r.historyCount >= 2){
        float h[CMT_HISTORY];
        for (int i = 0; i < r.historyCount; i++) h[i] = r.history[i];
        mtb_Kit_Text(4, TOP + 46, Terminal4x6, KIT_DIM, "TREND", K_BACK);
        mtb_Kit_Sparkline(4, TOP + 54, 248, 36, h, r.historyCount, c, mtb_Kit_Dim(c, 40));
    }
    // Leave by, for an arrival time.
    char left[64] = "";
    uint16_t lc = KIT_LABEL;
    const int leaveBy = cmt_Leave_By(b.arriveBy, d.seconds);
    const time_t tt = (time_t)v.now;
    struct tm lt;
    localtime_r(&tt, &lt);
    const int minute = lt.tm_hour * 60 + lt.tm_min;
    if (leaveBy >= 0 && minute < b.arriveBy && minute + 180 > leaveBy){   // the morning's trip, not hours ahead or past
        char a[16], l[16];
        snprintf(l, sizeof(l), "%02d:%02d", leaveBy / 60, leaveBy % 60);
        snprintf(a, sizeof(a), "%02d:%02d", b.arriveBy / 60, b.arriveBy % 60);
        if (minute <= leaveBy) snprintf(left, sizeof(left), "LEAVE BY %s TO ARRIVE %s", l, a);
        else snprintf(left, sizeof(left), "LATE: %d MIN AFTER %s", minute + (int)((d.seconds + 59) / 60) - b.arriveBy, a);
        lc = minute <= leaveBy ? K_ACCENT : KIT_RED;
    } else age_Text(left, sizeof(left), r, v.now);
    footer_Text(left, lc, nullptr);
}

// Overview --------------------------------------------------------------------------------------------------------
static void view_Overview(const Cmt_View_t& v){
    header(v, "COMMUTE", nullptr);
    if (cmt_Board_Count(*v.settings) == 0){
        message("Set up your commute", "In the Pixlpal phone app: Commute. Add your stops (Transitland) or drives (TomTom or Google) and when you travel.");
        return;
    }
    int16_t y = TOP + 1;
    for (int i = 0; i < CMT_BOARDS && y + 15 <= FOOT - 2; i++){
        const Cmt_Board_t& b = v.settings->boards[i];
        if (!b.used) continue;
        const Cmt_Result_t& r = v.results[i];
        char title[48], t[80] = "";
        board_Title(*v.settings, b, title, sizeof(title));
        uint16_t c = KIT_LABEL;
        if (b.kind == CMT_DRIVE_BOARD && r.drive.ok){
            const int m = (int)((r.drive.seconds + 59) / 60);
            const int extra = r.drive.seconds > r.drive.normalSeconds ? (int)((r.drive.seconds - r.drive.normalSeconds + 30) / 60) : 0;
            snprintf(t, sizeof(t), extra ? "%d MIN (+%d)" : "%d MIN", m, extra);
            const float ratio = r.drive.normalSeconds ? (float)r.drive.seconds / r.drive.normalSeconds : 1.0f;
            c = ratio < 1.15f ? KIT_GREEN : ratio < 1.5f ? KIT_GOLD : KIT_RED;
        } else if (b.kind == CMT_DEPARTURES_BOARD && r.count){
            size_t used = 0;
            int n = 0;
            for (int k = 0; k < r.count && n < 3 && used < sizeof(t); k++){
                const Cmt_Departure_t& d = r.departures[k];
                if (d.cancelled || d.time + 30 < v.now) continue;
                char when[16];
                cmt_Minutes_Text(when, sizeof(when), d.time, v.now, v.settings->hour24);
                used += (size_t)snprintf(t + used, sizeof(t) - used, "%s%s %s", n ? "  " : "", d.line, when);
                n++;
            }
            c = KIT_TEXT;
        } else if (r.state == CMT_FAILED) { snprintf(t, sizeof(t), "%s", r.error); c = KIT_ORANGE; }
        else snprintf(t, sizeof(t), "...");
        mtb_Panel_Fill_Rect(2, y, 3, y + 12, b.kind == CMT_DRIVE_BOARD ? KIT_CYAN : K_ACCENT);
        text_Fit(7, y, Terminal6x8, KIT_TEXT, title, 240);
        text_Fit(7, y + 8, Terminal4x6, c, t, 245);
        y += 16;
    }
    // The budgets.
    char t[64];
    const Cmt_Status_t& st = *v.status;
    snprintf(t, sizeof(t), "TRANSIT %u/%u  ROUTES %u/%u", (unsigned)st.usage.transit, (unsigned)cmt_Transit_Monthly(*v.settings),
             (unsigned)st.usage.route, (unsigned)cmt_Route_Monthly(*v.settings));
    footer_Text(t, st.transitSpent || st.routeSpent ? KIT_ORANGE : KIT_DIM, nullptr);
}

void cmt_Draw(const Cmt_View_t& v){
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, K_BACK);
    if (v.board >= 0 && v.board < CMT_BOARDS && v.settings->boards[v.board].used){
        const Cmt_Board_t& b = v.settings->boards[v.board];
        if (b.kind == CMT_DRIVE_BOARD) view_Drive(v, b, v.results[v.board]);
        else view_Departures(v, b, v.results[v.board]);
    } else view_Overview(v);
    if (v.alert) cmt_Draw_Alert(v);
}

void cmt_Draw_Alert(const Cmt_View_t& v){
    if (v.alert == nullptr) return;
    const int16_t x0 = 16, y0 = 34, x1 = 239, y1 = 100;
    mtb_Panel_Fill_Rect(x0, y0, x1, y1, K_BACK);
    const uint16_t border = v.flash ? K_ACCENT : KIT_TEXT;
    mtb_Panel_Draw_Rect(x0, y0, x1, y1, border);
    mtb_Panel_Draw_Rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, border);
    mtb_Kit_Text_Centre(128, y0 + 8, Terminal10x17, K_ACCENT, v.alert, K_BACK);
    if (v.alertDetail){
        char a[96], b[96];
        mtb_Kit_To_Ascii(a, sizeof(a), v.alertDetail);
        mtb_Kit_Fit(b, sizeof(b), Terminal6x8, a, x1 - x0 - 10);
        mtb_Kit_Text_Centre(128, y0 + 32, Terminal6x8, KIT_TEXT, b, K_BACK);
    }
    mtb_Kit_Text_Centre(128, y1 - 11, Terminal4x6, KIT_DIM, "CLICK TO DISMISS", K_BACK);
}
