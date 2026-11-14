// Code Quest's screens, 256 x 118 under the status bar: the levels, the map and the program, the run, the result.
#include "cdq_App.h"
#include "mtb_kit_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOP MTB_KIT_TOP
static const uint16_t FLOOR_C = MTB_KIT_RGB(30, 38, 52), WALL_C = MTB_KIT_RGB(90, 70, 50), WALL_EDGE = MTB_KIT_RGB(130, 100, 70);
static const uint16_t LOOP_C = MTB_KIT_RGB(255, 150, 30), MOVE_C = MTB_KIT_RGB(60, 160, 255), SENSE_C = MTB_KIT_RGB(190, 100, 255);
static const uint16_t ROBOT_C = MTB_KIT_RGB(80, 230, 120);
#define STRIP_Y (PANEL_RES_Y - 19)

static void clear(void){ mtb_Panel_Fill_Rect(0, TOP, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK); }

static uint16_t block_Color(char b){
    if ((b >= CDQ_REP2 && b <= CDQ_REP6) || b == CDQ_UNTIL || b == CDQ_END) return LOOP_C;
    if (b == CDQ_IF_WALL) return SENSE_C;
    if (b == CDQ_PAL_DELETE) return KIT_RED;
    if (b == CDQ_PAL_CLOSE) return KIT_LABEL;
    return MOVE_C;
}

/** A block's picture in a 12 x 14 box at (x, y). */
static void block_Icon(int16_t x, int16_t y, char b, bool lit){
    const uint16_t c = block_Color(b), bg = lit ? c : mtb_Kit_Dim(c, 70), ink = lit ? BLACK : KIT_TEXT;
    mtb_Panel_Fill_Round_Rect(x, y, (int16_t)(x + 11), (int16_t)(y + 13), 2, bg);
    const int16_t cx = (int16_t)(x + 6), cy = (int16_t)(y + 7);
    char t[3] = {0, 0, 0};
    switch (b){
        case CDQ_FWD:
            mtb_Panel_Fill_Triangle(cx, (int16_t)(y + 2), (int16_t)(cx - 4), (int16_t)(y + 7), (int16_t)(cx + 4), (int16_t)(y + 7), ink);
            mtb_Panel_Fill_Rect((int16_t)(cx - 1), (int16_t)(y + 7), (int16_t)(cx + 1), (int16_t)(y + 11), ink);
            break;
        case CDQ_LEFT:
            mtb_Panel_Fill_Rect((int16_t)(cx + 1), (int16_t)(y + 5), (int16_t)(cx + 3), (int16_t)(y + 11), ink);
            mtb_Panel_Fill_Rect((int16_t)(cx - 2), (int16_t)(y + 5), (int16_t)(cx + 3), (int16_t)(y + 7), ink);
            mtb_Panel_Fill_Triangle((int16_t)(x + 1), (int16_t)(y + 6), (int16_t)(cx - 1), (int16_t)(y + 2), (int16_t)(cx - 1), (int16_t)(y + 10), ink);
            break;
        case CDQ_RIGHT:
            mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(y + 5), (int16_t)(cx - 1), (int16_t)(y + 11), ink);
            mtb_Panel_Fill_Rect((int16_t)(cx - 3), (int16_t)(y + 5), (int16_t)(cx + 2), (int16_t)(y + 7), ink);
            mtb_Panel_Fill_Triangle((int16_t)(x + 10), (int16_t)(y + 6), (int16_t)(cx + 1), (int16_t)(y + 2), (int16_t)(cx + 1), (int16_t)(y + 10), ink);
            break;
        case CDQ_UNTIL: kk_Star(cx, cy, 4, ink); break;
        case CDQ_END: t[0] = ']'; mtb_Kit_Text_Centre(cx, (int16_t)(y + 3), Terminal6x8, ink, t, bg); break;
        case CDQ_IF_WALL: t[0] = '?'; mtb_Kit_Text_Centre(cx, (int16_t)(y + 3), Terminal6x8, ink, t, bg); break;
        case CDQ_PAL_DELETE: t[0] = '<'; mtb_Kit_Text_Centre(cx, (int16_t)(y + 3), Terminal6x8, ink, t, bg); break;
        case CDQ_PAL_CLOSE: t[0] = 'X'; mtb_Kit_Text_Centre(cx, (int16_t)(y + 3), Terminal6x8, ink, t, bg); break;
        default: t[0] = b; mtb_Kit_Text_Centre(cx, (int16_t)(y + 3), Terminal6x8, ink, t, bg); break;   // a REPEAT's number
    }
}

static void robot(int16_t x, int16_t y, int16_t cs, uint8_t dir, uint16_t c){
    const int16_t cx = (int16_t)(x + cs / 2), cy = (int16_t)(y + cs / 2), r = (int16_t)(cs / 2 - 1);
    mtb_Panel_Fill_Circle(cx, cy, r, c);
    static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    const int16_t ex = (int16_t)(cx + DX[dir] * (r - 2)), ey = (int16_t)(cy + DY[dir] * (r - 2));
    mtb_Panel_Fill_Circle(ex, ey, 2, BLACK);
}

/** The map, the robot at @p step of the run (or at the start), gems picked up so far gone. */
static void map(const Cdq_View_t& v, int16_t& mapBottom){
    const Cdq_Level_t& l = cdq_Level(v.level);
    int w, h;
    cdq_Level_Size(l, w, h);
    int cs = 12;
    if (w * cs > 252) cs = 252 / w;
    if (h * cs > 82) cs = 82 / h;
    const int16_t x0 = (int16_t)(128 - w * cs / 2), y0 = (int16_t)(TOP + 13 + (82 - h * cs) / 2);
    mapBottom = (int16_t)(y0 + h * cs);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++){
        const char c = cdq_Cell(l, x, y);
        const int16_t px = (int16_t)(x0 + x * cs), py = (int16_t)(y0 + y * cs);
        if (c == '#'){
            if (l.rows[y] && x < (int)strlen(l.rows[y]) && l.rows[y][x] == '#'){
                mtb_Panel_Fill_Rect(px, py, (int16_t)(px + cs - 1), (int16_t)(py + cs - 1), WALL_C);
                mtb_Panel_Draw_HLine(py, px, (int16_t)(px + cs - 1), WALL_EDGE);
            }
            continue;
        }
        mtb_Panel_Fill_Rect(px, py, (int16_t)(px + cs - 1), (int16_t)(py + cs - 1), FLOOR_C);
        mtb_Panel_Draw_Rect(px, py, (int16_t)(px + cs - 1), (int16_t)(py + cs - 1), MTB_KIT_RGB(40, 50, 68));
        if (c == '*') kk_Star((int16_t)(px + cs / 2), (int16_t)(py + cs / 2), (int16_t)(cs / 2 - 1), KIT_GOLD);
        else if (c == 'g'){
            bool taken = false;
            for (int i = 0; v.run && i <= v.step && i < v.run->steps; i++) if (v.run->step[i].gem && v.run->step[i].x == x && v.run->step[i].y == y) taken = true;
            if (!taken){
                const int16_t gx = (int16_t)(px + cs / 2), gy = (int16_t)(py + cs / 2), r = (int16_t)(cs / 3);
                mtb_Panel_Fill_Triangle(gx, (int16_t)(gy - r), (int16_t)(gx - r), gy, (int16_t)(gx + r), gy, KIT_CYAN);
                mtb_Panel_Fill_Triangle((int16_t)(gx - r), gy, (int16_t)(gx + r), gy, gx, (int16_t)(gy + r), MTB_KIT_RGB(40, 140, 200));
            }
        }
    }
    int rx, ry;
    uint8_t rd;
    cdq_Start(l, rx, ry, rd);
    uint16_t rc = ROBOT_C;
    if (v.run && v.step >= 0 && v.step < v.run->steps){
        const Cdq_Step_t& s = v.run->step[v.step];
        rx = s.x; ry = s.y; rd = s.dir;
        const bool crashed = v.run->result == CDQ_CRASH && v.step == v.run->steps - 1;
        if (crashed){ rc = KIT_RED; }
    }
    robot((int16_t)(x0 + rx * cs), (int16_t)(y0 + ry * cs), (int16_t)cs, rd, rc);
}

static void strip(const Cdq_View_t& v){
    const int n = (int)strlen(v.program);
    const int items = n + 2;                         // the blocks, then RUN (the caret sits in the gaps)
    // Keep the caret in view: 15 px a block.
    int first = 0;
    const int fits = 14;
    if (v.caret > fits - 3) first = v.caret - (fits - 3);
    if (first > items - fits) first = items - fits;
    if (first < 0) first = 0;
    int16_t x = 4;
    // The program runs left to right; the block running lights up.
    const int running = v.run && v.screen != CDQ_SCR_EDIT && v.step >= 0 && v.step < v.run->steps ? v.run->step[v.step].block : -1;
    for (int i = first; i <= n && x < 220; i++){
        const bool caretHere = v.screen == CDQ_SCR_EDIT && v.caret == i;
        if (caretHere) mtb_Panel_Fill_Rect(x, (int16_t)(STRIP_Y - 1), (int16_t)(x + 1), (int16_t)(STRIP_Y + 15), KIT_TEXT);
        x = (int16_t)(x + 3);
        if (i < n){ block_Icon(x, STRIP_Y, v.program[i], running == i); x = (int16_t)(x + 13); }
    }
    // RUN.
    const bool runSel = v.screen == CDQ_SCR_EDIT && v.caret == n + 1;
    mtb_Panel_Fill_Round_Rect(226, STRIP_Y, 252, (int16_t)(STRIP_Y + 13), 3, runSel ? KIT_GREEN : MTB_KIT_RGB(0, 70, 30));
    mtb_Panel_Fill_Triangle(235, (int16_t)(STRIP_Y + 3), 235, (int16_t)(STRIP_Y + 10), 243, (int16_t)(STRIP_Y + 6), runSel ? BLACK : KIT_GREEN);
}

static void palette(const Cdq_View_t& v){
    const int16_t w = (int16_t)(v.itemCount * 15 + 4), x0 = (int16_t)(128 - w / 2), y = (int16_t)(STRIP_Y - 30);
    mtb_Panel_Fill_Round_Rect(x0, y, (int16_t)(x0 + w), (int16_t)(y + 28), 4, MTB_KIT_RGB(25, 25, 35));
    mtb_Panel_Draw_Round_Rect(x0, y, (int16_t)(x0 + w), (int16_t)(y + 28), 4, KIT_LABEL);
    for (int i = 0; i < v.itemCount; i++){
        const int16_t bx = (int16_t)(x0 + 3 + i * 15);
        if (i == v.item) mtb_Panel_Draw_Rect((int16_t)(bx - 2), (int16_t)(y + 1), (int16_t)(bx + 13), (int16_t)(y + 17), KIT_TEXT);
        block_Icon(bx, (int16_t)(y + 3), v.items[i], i == v.item);
    }
    const char b = v.items[v.item];
    const char* name = b == CDQ_PAL_DELETE ? "DELETE" : b == CDQ_PAL_CLOSE ? "CLOSE" : cdq_Block_Name(b);
    mtb_Kit_Text_Centre(128, (int16_t)(y + 20), Terminal4x6, KIT_TEXT, name, MTB_KIT_RGB(25, 25, 35));
}

static void edit(const Cdq_View_t& v){
    clear();
    const Cdq_Level_t& l = cdq_Level(v.level);
    char a[48];
    snprintf(a, sizeof(a), "%d. %s", v.level + 1, l.name);
    mtb_Kit_Text(2, TOP + 1, Terminal6x8, KIT_GOLD, a);
    snprintf(a, sizeof(a), "%d/%u BLOCKS", cdq_Count(v.program), l.maxBlocks);
    mtb_Kit_Text_Right(253, TOP + 2, Terminal4x6, cdq_Count(v.program) >= l.maxBlocks ? KIT_ORANGE : KIT_LABEL, a);
    int16_t bottom;
    map(v, bottom);
    if (v.message && v.message[0]) mtb_Kit_Text_Centre(128, (int16_t)(STRIP_Y - 8), Terminal4x6, KIT_ORANGE, v.message);
    else if (!v.program[0] && !v.palette) kk_Words(l.tip, 128, (int16_t)(STRIP_Y - 14), 250, Terminal4x6, KIT_LABEL, 2);
    strip(v);
    if (v.palette) palette(v);
}

static void levels(const Cdq_View_t& v){
    clear();
    kk_Header(*v.k, TOP, "CODE QUEST", KIT_GOLD);
    const uint8_t child = v.k->real[v.k->child] % 3;
    for (int i = 0; i < CDQ_LEVELS; i++){
        const int16_t x = (int16_t)(6 + (i % 8) * 31), y = (int16_t)(TOP + 14 + (i / 8) * 40);
        const bool open = cdq_Unlocked(*v.s, child, i), sel = v.level == i;
        const uint8_t stars = v.s->stars[child][i];
        const uint16_t c = i < 6 ? MOVE_C : i < 12 ? LOOP_C : SENSE_C;
        if (sel) mtb_Panel_Fill_Round_Rect(x, y, (int16_t)(x + 27), (int16_t)(y + 34), 4, mtb_Kit_Dim(c, 90));
        mtb_Panel_Draw_Round_Rect(x, y, (int16_t)(x + 27), (int16_t)(y + 34), 4, open ? c : KIT_LINE);
        char t[4];
        snprintf(t, sizeof(t), "%d", i + 1);
        if (open) mtb_Kit_Text_Centre((int16_t)(x + 14), (int16_t)(y + 6), Terminal8x12, KIT_TEXT, t, sel ? mtb_Kit_Dim(c, 90) : BLACK);
        else { mtb_Panel_Draw_Rect((int16_t)(x + 9), (int16_t)(y + 11), (int16_t)(x + 18), (int16_t)(y + 19), KIT_DIM); mtb_Panel_Draw_Circle((int16_t)(x + 14), (int16_t)(y + 10), 3, KIT_DIM); }
        for (int s = 0; s < 3; s++) kk_Star((int16_t)(x + 6 + s * 8), (int16_t)(y + 27), 3, s < stars ? KIT_GOLD : KIT_LINE);
    }
    const Cdq_Level_t& l = cdq_Level(v.level);
    mtb_Kit_Text_Centre(128, PANEL_RES_Y - 8, Terminal4x6, KIT_LABEL, l.name);
}

static void result(const Cdq_View_t& v){
    edit(v);
    const Cdq_Run_t& r = *v.run;
    mtb_Panel_Fill_Round_Rect(30, TOP + 16, 226, TOP + 78, 6, BLACK);
    mtb_Panel_Draw_Round_Rect(30, TOP + 16, 226, TOP + 78, 6, r.result == CDQ_GOAL ? KIT_GOLD : KIT_ORANGE);
    if (r.result == CDQ_GOAL){
        mtb_Kit_Text_Centre(128, TOP + 22, Terminal10x16, KIT_GOLD, "YOU DID IT!");
        for (int s = 0; s < 3; s++) kk_Star((int16_t)(98 + s * 30), TOP + 52, 10, s < r.stars ? KIT_GOLD : KIT_LINE);
        mtb_Kit_Text_Centre(128, TOP + 68, Terminal4x6, KIT_LABEL, v.level + 1 < CDQ_LEVELS ? "PRESS FOR THE NEXT LEVEL" : "YOU FINISHED EVERY LEVEL!");
        if (v.ms < 3000) kk_Confetti(v.ms, (uint32_t)v.level, TOP);
    } else {
        const char* why = r.result == CDQ_CRASH ? "BUMP! A WALL WAS IN THE WAY." : r.result == CDQ_STOPPED ? "THE ROBOT STOPPED BEFORE THE STAR."
                        : r.result == CDQ_TOO_LONG ? "THE ROBOT GOES ROUND AND ROUND!" : "EVERY REPEAT NEEDS AN END ].";
        mtb_Kit_Text_Centre(128, TOP + 24, Terminal8x12, KIT_ORANGE, "NEARLY!");
        kk_Words(why, 128, TOP + 42, 186, Terminal6x8, KIT_TEXT, 2);
        mtb_Kit_Text_Centre(128, TOP + 68, Terminal4x6, KIT_LABEL, "PRESS TO FIX YOUR PROGRAM");
    }
}

void cdq_Draw(const Cdq_View_t& v){
    switch (v.screen){
        case CDQ_SCR_PICK: kk_Pick_Screen(*v.k, TOP, v.pick); break;
        case CDQ_SCR_LEVELS: levels(v); break;
        case CDQ_SCR_EDIT: case CDQ_SCR_RUN: edit(v); break;
        case CDQ_SCR_RESULT: result(v); break;
        case CDQ_SCR_STOP: kk_Stop_Screen(*v.k, TOP, v.stop); break;
    }
}
