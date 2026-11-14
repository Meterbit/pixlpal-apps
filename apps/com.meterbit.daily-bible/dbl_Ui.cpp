// Daily Bible's screens.
#include "dbl_Ui.h"
#include "fth_Ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint16_t K_ACCENT = MTB_KIT_RGB(255, 196, 0);
static const uint16_t K_SKY = MTB_KIT_RGB(90, 170, 255);

static void page_Today(const Dbl_View_t& v){
    const Dbl_Settings_t& s = *v.settings;
    int y, m, d, weekday;
    const uint32_t today = fth_Local_Day(v.now, &y, &m, &d, &weekday);
    const time_t tt = (time_t)v.now;
    struct tm lt;
    localtime_r(&tt, &lt);
    char t[96], date[40];
    strftime(date, sizeof(date), "%A %d %B", &lt);
    Fth_Church_Day_t c;
    fth_Church_Day(y, m, d, s.orthodox, c);
    fth_Header(date, K_ACCENT, c.seasonName);
    // The season's colour down the left.
    mtb_Panel_Fill_Rect(0, 21, 3, 117, c.color);
    // Today's great day, or the season.
    fth_Text_Fit(8, 23, Terminal8x12, c.feast[0] ? c.color : KIT_TEXT, c.feast[0] ? c.feast : c.seasonName, 244);
    if (c.nextDays > 0){
        if (c.nextDays == 1) snprintf(t, sizeof(t), "%s tomorrow", c.next);
        else snprintf(t, sizeof(t), "%s in %d days", c.next, c.nextDays);
        fth_Text_Fit(8, 38, Terminal6x8, KIT_LABEL, t, 244);
    }
    mtb_Panel_Draw_HLine(51, 8, 255, KIT_LINE);
    // The reading plan.
    int16_t y0 = 55;
    if (s.plan != DBL_PLAN_OFF){
        mtb_Kit_Text(8, y0, Terminal4x6, KIT_DIM, "TODAY'S READING", BLACK);
        char reading[128], lines[2][48];
        dbl_Reading(s, today, y, m, d, reading, sizeof(reading));
        const int n = mtb_Kit_Wrap(reading, Terminal6x8, 244, &lines[0][0], sizeof(lines[0]), 2);
        for (int i = 0; i < n; i++) mtb_Kit_Text(8, y0 + 8 + i * 10, Terminal6x8, K_SKY, lines[i], BLACK);
        const uint16_t streak = dbl_Streak(s, today);
        if (dbl_Read_Today(s, today)) snprintf(t, sizeof(t), "READ TODAY.  %u %s IN A ROW", streak, streak == 1 ? "DAY" : "DAYS");
        else if (streak) snprintf(t, sizeof(t), "LONG PRESS WHEN READ (%u %s SO FAR)", streak, streak == 1 ? "DAY" : "DAYS");
        else snprintf(t, sizeof(t), "LONG PRESS WHEN READ");
        mtb_Kit_Text(8, y0 + 30, Terminal4x6, dbl_Read_Today(s, today) ? KIT_GREEN : KIT_DIM, t, BLACK);
        y0 += 40;
    }
    // The next reminder.
    int32_t in;
    const int r = fth_Next_Reminder(s.reminders, DBL_REMINDERS, v.now, in);
    if (r >= 0){
        char clock[12], cd[16];
        fth_Minute_Clock(clock, sizeof(clock), s.reminders[r].minute, s.hour24);
        fth_Countdown(cd, sizeof(cd), in);
        if (s.plan == DBL_PLAN_OFF){
            mtb_Kit_Text(8, y0, Terminal4x6, KIT_DIM, "NEXT", BLACK);
            fth_Text_Fit(8, y0 + 9, Terminal8x12, K_ACCENT, s.reminders[r].label, 160);
            mtb_Kit_Text(8, y0 + 25, Terminal6x8, KIT_TEXT, clock, BLACK);
            const int16_t w = mtb_Kit_Big_Text_Width(Terminal8x12, 2, cd);
            mtb_Kit_Big_Text(252 - w, y0 + 9, Terminal8x12, 2, KIT_TEXT, cd);
        } else {
            snprintf(t, sizeof(t), "%s %s", s.reminders[r].label, clock);
            fth_Text_Fit(8, FTH_FOOT - 2, Terminal6x8, K_ACCENT, t, 170);
            mtb_Kit_Text_Right(253, FTH_FOOT - 2, Terminal6x8, KIT_TEXT, cd, BLACK);
        }
    }
}

void dbl_Draw(const Dbl_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, BLACK);
    if (v.page == DBL_PAGE_VERSE) fth_Draw_Verse(*v.verse, "Verse of the day", K_ACCENT, v.scroll, v.verseError);
    else page_Today(v);
    if (v.alert) dbl_Draw_Alert(v);
    mtb_Panel_End_Update();
}

void dbl_Draw_Alert(const Dbl_View_t& v){ fth_Draw_Alert(v.alert, v.alertDetail, v.flash, K_ACCENT); }
