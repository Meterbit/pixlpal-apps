// Catholic Daily's screens.
#include "cth_Ui.h"
#include "fth_Ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint16_t K_GOLD = MTB_KIT_RGB(255, 196, 0);
static const uint16_t K_MARIAN = MTB_KIT_RGB(110, 160, 255);

static void date_Text(uint32_t now, char* out, size_t size){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    strftime(out, size, "%A %d %B", &lt);
}

static void footer_Next(const Cth_View_t& v){
    if (!v.next || v.next->at <= v.now) return;
    char clock[12], cd[16], t[64];
    fth_Clock(clock, sizeof(clock), v.next->at, v.settings->hour24);
    fth_Countdown(cd, sizeof(cd), (int32_t)(v.next->at - v.now));
    snprintf(t, sizeof(t), "%s %s", v.next->name, clock);
    fth_Text_Fit(8, FTH_FOOT - 2, Terminal6x8, K_GOLD, t, 170);
    mtb_Kit_Text_Right(253, FTH_FOOT - 2, Terminal6x8, KIT_TEXT, cd, BLACK);
}

static void page_Today(const Cth_View_t& v){
    char date[40], season[48], t[96];
    date_Text(v.now, date, sizeof(date));
    const Cth_Day_t& d = *v.day;
    cth_Season_Text(d, season, sizeof(season));
    fth_Header(date, K_GOLD, season);
    if (!d.count){ fth_Message("Today", "Reading today's liturgical day..."); return; }
    const uint16_t colour = cth_Colour(d);
    mtb_Panel_Fill_Rect(0, 21, 3, 117, colour);
    // The day's celebration, two lines at most.
    char ascii[96], lines[2][48];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), d.c[0].title);
    const int n = mtb_Kit_Wrap(ascii, Terminal8x12, 244, &lines[0][0], sizeof(lines[0]), 2);
    for (int i = 0; i < n; i++) mtb_Kit_Text(8, 23 + i * 14, Terminal8x12, KIT_TEXT, lines[i], BLACK);
    int16_t y = 23 + n * 14 + 2;
    char rank[32];
    cth_Rank_Text(d.c[0], rank, sizeof(rank));
    if (rank[0]){ mtb_Kit_Text(8, y, Terminal4x6, colour == fth_Liturgical_Color("white") ? K_GOLD : colour, rank, BLACK); y += 9; }
    // Optional memorials and the like.
    if (d.count > 1){
        snprintf(t, sizeof(t), "Also: %s%s", d.c[1].title, d.count > 2 ? " and more" : "");
        fth_Text_Fit(8, y, Terminal6x8, KIT_LABEL, t, 244);
        y += 10;
    }
    if (!d.fromCalendar) fth_Text_Fit(8, y, Terminal4x6, KIT_DIM, "THE SEASON, WORKED OUT ON PIXLPAL (THE CALENDAR IS NOT REACHABLE)", 244);
    // The colour of the day, and the next great day.
    char word[12];
    mtb_Kit_To_Ascii(word, sizeof(word), d.count ? d.c[0].colour : "");
    mtb_Kit_Upper(word);
    snprintf(t, sizeof(t), "LITURGICAL COLOUR: %s", word);
    mtb_Kit_Text(8, 80, Terminal4x6, KIT_DIM, t, BLACK);
    int yy, mm, dd;
    fth_Local_Day(v.now, &yy, &mm, &dd, nullptr);
    Fth_Church_Day_t c;
    fth_Church_Day(yy, mm, dd, false, c);
    if (c.nextDays > 0){
        if (c.nextDays == 1) snprintf(t, sizeof(t), "%s TOMORROW", c.next);
        else snprintf(t, sizeof(t), "%s IN %d DAYS", c.next, c.nextDays);
        mtb_Kit_Upper(t);
        fth_Text_Fit(8, 88, Terminal4x6, KIT_LABEL, t, 244);
    }
    // The Rosary of the day.
    if (v.settings->rosary){
        int weekday;
        fth_Local_Day(v.now, nullptr, nullptr, nullptr, &weekday);
        snprintf(t, sizeof(t), "ROSARY: %s", cth_Rosary_Name(cth_Rosary(weekday, d.season)));
        mtb_Kit_Upper(t);
        fth_Text_Fit(8, 98, Terminal4x6, K_MARIAN, t, 244);
    }
    mtb_Panel_Draw_HLine(FTH_FOOT - 6, 8, 255, KIT_LINE);
    footer_Next(v);
}

static void page_Rosary(const Cth_View_t& v){
    int weekday;
    fth_Local_Day(v.now, nullptr, nullptr, nullptr, &weekday);
    const uint8_t set = cth_Rosary(weekday, v.day->season);
    char date[40];
    date_Text(v.now, date, sizeof(date));
    fth_Header(cth_Rosary_Name(set), K_MARIAN, date);
    for (int i = 0; i < 5; i++){
        char n[4];
        snprintf(n, sizeof(n), "%d", i + 1);
        mtb_Kit_Text(8, 25 + i * 15, Terminal8x12, K_MARIAN, n, BLACK);
        fth_Text_Fit(24, 27 + i * 15, Terminal6x8, KIT_TEXT, cth_Mystery(set, i), 228);
    }
    footer_Next(v);
}

void cth_Draw(const Cth_View_t& v){
    mtb_Panel_Begin_Update();
    mtb_Panel_Fill_Rect(0, MTB_KIT_TOP, 255, 127, BLACK);
    switch (v.page){
        case CTH_PAGE_ROSARY: page_Rosary(v); break;
        case CTH_PAGE_VERSE: fth_Draw_Verse(*v.verse, "Verse of the day", K_GOLD, v.scroll, v.verseError); break;
        default: page_Today(v); break;
    }
    if (v.alert) cth_Draw_Alert(v);
    mtb_Panel_End_Update();
}

void cth_Draw_Alert(const Cth_View_t& v){ fth_Draw_Alert(v.alert, v.alertDetail, v.flash, K_GOLD); }
