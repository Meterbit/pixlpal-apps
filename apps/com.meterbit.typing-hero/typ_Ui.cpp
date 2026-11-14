// Typing Hero's screens, 256 x 118 under the status bar.
#include "typ_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define TOP MTB_KIT_TOP

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

/** The keyboard, each key in its finger's colour, @p next lit up. */
static void keyboard(int16_t x0, int16_t y0, char next){
    static const char* const ROWS[4] = {"1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};
    int nr = -1, nc = -1;
    typ_Key_Place(next, nr, nc);
    for (int r = 0; r < 4; r++){
        for (int c = 0; ROWS[r][c]; c++){
            const int16_t x = (int16_t)(x0 + r * 4 + c * 13), y = (int16_t)(y0 + r * 12);
            const uint16_t fc = typ_Finger_Color(typ_Finger(ROWS[r][c]));
            const bool lit = r == nr && c == nc;
            mtb_Panel_Fill_Rect(x, y, (int16_t)(x + 11), (int16_t)(y + 10), lit ? fc : mtb_Kit_Dim(fc, 55));
            char t[2] = {(char)toupper((unsigned char)ROWS[r][c]), 0};
            mtb_Kit_Text((int16_t)(x + 4), (int16_t)(y + 2), Terminal4x6, lit ? BLACK : mtb_Kit_Dim(fc, 200), t, lit ? fc : mtb_Kit_Dim(fc, 55));
        }
    }
    // The home row's bumps, and the space bar.
    mtb_Panel_Draw_HLine((int16_t)(y0 + 24 + 9), (int16_t)(x0 + 8 + 3 * 13 + 3), (int16_t)(x0 + 8 + 3 * 13 + 7), BLACK);
    mtb_Panel_Draw_HLine((int16_t)(y0 + 24 + 9), (int16_t)(x0 + 8 + 6 * 13 + 3), (int16_t)(x0 + 8 + 6 * 13 + 7), BLACK);
    const bool space = next == ' ';
    mtb_Panel_Fill_Rect((int16_t)(x0 + 30), (int16_t)(y0 + 48), (int16_t)(x0 + 100), (int16_t)(y0 + 54), space ? typ_Finger_Color(TYP_THUMB) : mtb_Kit_Dim(typ_Finger_Color(TYP_THUMB), 60));
}

static void menu(const Typ_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "TYPING HERO", KIT_GOLD);
    static const char* const N[TYP_MENU] = {"LESSONS", "FALLING LETTERS", "WORD RACE"};
    static const char* const D[TYP_MENU] = {"LEARN EVERY KEY", "ZAP THEM BEFORE THEY LAND", "60 SECONDS: HOW FAST?"};
    for (int i = 0; i < TYP_MENU; i++){
        const int16_t y = (int16_t)(TOP + 14 + i * 32);
        const bool sel = v.menu == i;
        const uint16_t c = i == 0 ? KIT_CYAN : i == 1 ? KIT_ORANGE : KIT_GREEN;
        if (sel) mtb_Panel_Fill_Round_Rect(20, y, 236, (int16_t)(y + 28), 6, mtb_Kit_Dim(c, 60));
        mtb_Panel_Draw_Round_Rect(20, y, 236, (int16_t)(y + 28), 6, sel ? c : mtb_Kit_Dim(c, 100));
        mtb_Kit_Text(30, (int16_t)(y + 4), Terminal8x12, sel ? KIT_TEXT : KIT_LABEL, N[i], sel ? mtb_Kit_Dim(c, 60) : BLACK);
        mtb_Kit_Text(30, (int16_t)(y + 18), Terminal4x6, c, D[i], sel ? mtb_Kit_Dim(c, 60) : BLACK);
    }
    char a[32];
    if (v.s->bestWpm[v.k->real[v.k->child] % 3]){ snprintf(a, sizeof(a), "BEST %u WPM", v.s->bestWpm[v.k->real[v.k->child] % 3]); mtb_Kit_Text_Right(230, TOP + 92, Terminal4x6, KIT_GREEN, a); }
}

static void lessons(const Typ_View_t& v){
    clear();
    mtb_Kit_Text(4, TOP + 1, Terminal6x8, KIT_GOLD, "LESSONS");
    // A window of six around the one chosen.
    int first = v.lesson - 2;
    if (first > TYP_LESSONS - 6) first = TYP_LESSONS - 6;
    if (first < 0) first = 0;
    for (int i = first; i < first + 6 && i < TYP_LESSONS; i++){
        const int16_t y = (int16_t)(TOP + 12 + (i - first) * 17);
        const bool open = i < v.open, sel = i == v.lesson;
        if (sel) mtb_Panel_Fill_Round_Rect(2, y, 253, (int16_t)(y + 15), 4, MTB_KIT_RGB(20, 60, 90));
        char a[48];
        snprintf(a, sizeof(a), "%2d  %s", i + 1, typ_Lesson(i).name);
        mtb_Kit_Text(8, (int16_t)(y + 4), Terminal6x8, open ? (sel ? KIT_TEXT : KIT_LABEL) : KIT_DIM, a, sel ? MTB_KIT_RGB(20, 60, 90) : BLACK);
        if (!open) mtb_Kit_Text_Right(248, (int16_t)(y + 5), Terminal4x6, KIT_DIM, "LOCKED", sel ? MTB_KIT_RGB(20, 60, 90) : BLACK);
        else if (i + 1 < v.open) kk_Star(242, (int16_t)(y + 7), 4, KIT_GOLD);
    }
}

static void lesson(const Typ_View_t& v){
    clear();
    const Typ_Lesson_t& l = typ_Lesson(v.lesson);
    char a[48];
    mtb_Kit_Text(4, TOP + 1, Terminal6x8, KIT_GOLD, l.name);
    snprintf(a, sizeof(a), "LINE %u/%u  %d WPM  %d%%", v.lineNo + 1, v.lines, v.wpm, v.accuracy);
    mtb_Kit_Text_Right(253, TOP + 2, Terminal4x6, KIT_LABEL, a);
    // The line: typed in green, the next character boxed, the rest in white.
    const int n = (int)strlen(v.line);
    const int16_t x0 = (int16_t)(128 - n * 4);
    for (int i = 0; i < n; i++){
        const int16_t x = (int16_t)(x0 + i * 8), y = TOP + 16;
        char t[2] = {v.line[i], 0};
        if (i < v.at) mtb_Kit_Text(x, y, Terminal8x12, KIT_GREEN, t);
        else if (i == v.at){
            const uint16_t bg = v.missed ? KIT_RED : KIT_CYAN;
            mtb_Panel_Fill_Rect((int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(x + 7), (int16_t)(y + 12), bg);
            if (t[0] == ' ') mtb_Panel_Draw_HLine((int16_t)(y + 10), (int16_t)(x + 1), (int16_t)(x + 5), BLACK);
            else mtb_Kit_Text(x, y, Terminal8x12, BLACK, t, bg);
        } else mtb_Kit_Text(x, y, Terminal8x12, KIT_TEXT, t);
    }
    if (v.at < n){
        const char next = v.line[v.at];
        const uint8_t f = typ_Finger(next);
        snprintf(a, sizeof(a), "%s%s", typ_Finger_Name(f), isupper((unsigned char)next) ? " + SHIFT" : "");
        mtb_Kit_Text_Centre(128, TOP + 33, Terminal4x6, typ_Finger_Color(f), a);
        if (v.s->keyboardMap) keyboard(60, TOP + 42, next);
        else {
            char t[2] = {next == ' ' ? '_' : next, 0};
            mtb_Kit_Big_Text((int16_t)(128 - mtb_Kit_Big_Text_Width(Terminal10x16, 3, t) / 2), TOP + 46, Terminal10x16, 3, typ_Finger_Color(f), t);
        }
    }
}

static void done(const Typ_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "", KIT_GOLD);
    mtb_Kit_Text_Centre(128, TOP + 14, Terminal10x16, KIT_GOLD, v.title ? v.title : "");
    for (int i = 0; i < 3; i++) kk_Star((int16_t)(98 + i * 30), TOP + 48, 10, i < v.stars ? KIT_GOLD : KIT_LINE);
    char a[48];
    snprintf(a, sizeof(a), "%d WORDS A MINUTE   %d%% RIGHT", v.wpm, v.accuracy);
    mtb_Kit_Text_Centre(128, TOP + 66, Terminal6x8, KIT_TEXT, a);
    if (v.note) kk_Words(v.note, 128, TOP + 80, 240, Terminal6x8, KIT_GREEN, 2);
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 8, Terminal4x6, KIT_DIM, "PRESS ENTER OR THE KNOB");
    if (v.stars == 3 && v.ms < 2500) kk_Confetti(v.ms, 3, TOP);
}

static void fall(const Typ_View_t& v){
    clear();
    const Typ_Fall_t& f = *v.fall;
    char a[24];
    snprintf(a, sizeof(a), "SCORE %u", f.score);
    mtb_Kit_Text(4, TOP + 1, Terminal6x8, KIT_GOLD, a);
    for (int i = 0; i < 3; i++){
        const int16_t x = (int16_t)(240 - i * 12);
        const uint16_t c = i < f.lives ? KIT_RED : KIT_LINE;
        mtb_Panel_Fill_Circle((int16_t)(x - 2), TOP + 4, 2, c);
        mtb_Panel_Fill_Circle((int16_t)(x + 2), TOP + 4, 2, c);
        mtb_Panel_Fill_Triangle((int16_t)(x - 4), TOP + 5, (int16_t)(x + 4), TOP + 5, x, TOP + 9, c);
    }
    const int16_t ground = PANEL_RES_Y - 4;
    mtb_Panel_Draw_HLine(ground, 0, 255, MTB_KIT_RGB(60, 120, 60));
    for (int i = 0; i < TYP_DROPS; i++){
        const Typ_Drop_t& d = f.d[i];
        if (!d.live || d.y < 0) continue;              // still above the playing field
        const int16_t x = (int16_t)d.x, y = (int16_t)(TOP + 12 + d.y);
        const uint16_t c = typ_Finger_Color(typ_Finger(d.c));
        mtb_Panel_Fill_Round_Rect(x, y, (int16_t)(x + 13), (int16_t)(y + 14), 3, mtb_Kit_Dim(c, 90));
        char t[2] = {d.c, 0};
        mtb_Kit_Text((int16_t)(x + 3), (int16_t)(y + 2), Terminal8x12, KIT_TEXT, t, mtb_Kit_Dim(c, 90));
    }
    if (f.over){
        mtb_Panel_Fill_Round_Rect(48, TOP + 30, 208, TOP + 80, 6, BLACK);
        mtb_Panel_Draw_Round_Rect(48, TOP + 30, 208, TOP + 80, 6, KIT_ORANGE);
        mtb_Kit_Text_Centre(128, TOP + 36, Terminal8x12, KIT_ORANGE, "GAME OVER");
        snprintf(a, sizeof(a), "YOU ZAPPED %u!", f.score);
        mtb_Kit_Text_Centre(128, TOP + 54, Terminal6x8, KIT_TEXT, a);
        mtb_Kit_Text_Centre(128, TOP + 68, Terminal4x6, KIT_LABEL, "ENTER TO PLAY AGAIN");
    }
}

static void race(const Typ_View_t& v){
    clear();
    char a[32];
    snprintf(a, sizeof(a), "%lu", (unsigned long)v.secondsLeft);
    mtb_Kit_Text(4, TOP + 1, Terminal6x8, v.secondsLeft <= 10 ? KIT_RED : KIT_GOLD, a);
    snprintf(a, sizeof(a), "%u WORDS  %d WPM", v.words, v.wpm);
    mtb_Kit_Text_Right(253, TOP + 1, Terminal6x8, KIT_LABEL, a);
    mtb_Kit_Bar(4, TOP + 12, 248, 2, v.secondsLeft / 60.0f, KIT_GOLD, KIT_LINE);
    // The word, letters typed right in green and wrong in red.
    const char* w = v.word ? v.word : "";
    const int n = (int)strlen(w), tn = (int)strlen(v.typed ? v.typed : "");
    const int16_t width = mtb_Kit_Big_Text_Width(Terminal8x12, 2, w);
    int16_t x = (int16_t)(128 - width / 2);
    for (int i = 0; i < n; i++){
        char t[2] = {w[i], 0};
        const uint16_t c = i < tn ? (v.typed[i] == w[i] ? KIT_GREEN : KIT_RED) : KIT_TEXT;
        mtb_Kit_Big_Text(x, TOP + 26, Terminal8x12, 2, c, t);
        x = (int16_t)(x + mtb_Kit_Big_Text_Width(Terminal8x12, 2, t));
    }
    if (tn > n) mtb_Kit_Text_Centre(128, TOP + 54, Terminal6x8, KIT_RED, "TOO MANY LETTERS: BACKSPACE");
    if (v.nextWords) mtb_Kit_Text_Centre(128, TOP + 70, Terminal6x8, KIT_DIM, v.nextWords);
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 9, Terminal4x6, KIT_LABEL, "TYPE THE WORD, THEN SPACE");
}

void typ_Draw(const Typ_View_t& v){
    switch (v.screen){
        case TYP_SCR_PICK: kk_Pick_Screen(*v.k, TOP, v.pick); break;
        case TYP_SCR_NO_KEYBOARD:
            clear();
            keyboard(62, TOP + 8, 'f');
            kk_Words("PLUG A USB KEYBOARD INTO PIXLPAL TO PLAY", 128, TOP + 72, 240, Terminal8x12, KIT_TEXT, 2);
            break;
        case TYP_SCR_LESSONS: lessons(v); break;
        case TYP_SCR_LESSON: lesson(v); break;
        case TYP_SCR_DONE: done(v); break;
        case TYP_SCR_FALL: fall(v); break;
        case TYP_SCR_RACE: race(v); break;
        case TYP_SCR_STOP: kk_Stop_Screen(*v.k, TOP, v.stop); break;
        default: menu(v); break;
    }
}
