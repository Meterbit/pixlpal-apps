// Code Quest's model: the levels (our own), checking and running programs. No Arduino.
#include "cdq_Model.h"
#include <stdio.h>
#include <string.h>

#define MOVES (CDQ_A_MOVE | CDQ_A_TURN)
#define LOOPS (MOVES | CDQ_A_REPEAT)
#define SENSE (MOVES | CDQ_A_UNTIL | CDQ_A_SENSE)

static const Cdq_Level_t LEVELS[CDQ_LEVELS] = {
    // Step 1: moving and turning.
    {"FIRST STEPS", {">...*"}, CDQ_A_MOVE, 6, 4, "FFFF", "PRESS TO ADD A BLOCK. FORWARD MOVES ONE SQUARE."},
    {"ROUND THE CORNER", {">..", "##.", "##*"}, MOVES, 8, 5, "FFRFF", "RIGHT TURNS THE ROBOT. IT STAYS ON ITS SQUARE."},
    {"UP AND BACK", {"*..", "##.", ">.."}, MOVES, 10, 8, "FFLFFLFF", "LEFT TURNS THE OTHER WAY."},
    {"STAIRS", {">.###", "#..##", "##..#", "###.*"}, MOVES, 16, 13, "FRFLFRFLFRFLF", "STEP, TURN, STEP, TURN BACK..."},
    {"A GEM ON THE WAY", {">..g", "##.#", "##.*"}, MOVES, 14, 7, "FFFRRFLFFLF", "TWO TURNS FACE THE OTHER WAY."},
    {"THE LONG WAY", {"#..g*", "#.###", ">.###"}, MOVES, 10, 8, "FLFFRFFF", "GEMS ON THE WAY ARE PICKED UP."},
    // Step 2: repeating.
    {"TOO MANY STEPS", {">.........*"}, LOOPS, 4, 4, "5FF]", "REPEAT 5 DOES WHAT IS INSIDE FIVE TIMES."},
    {"REPEATED STAIRS", {">.####", "#..###", "##..##", "###..#", "####*#"}, LOOPS, 7, 6, "4FRFL]", "FIND THE PART THAT REPEATS."},
    {"L FOR LONG", {">......", "######.", "######.", "######.", "######*"}, LOOPS, 8, 7, "6F]R4F]", "TWO REPEATS, ONE AFTER THE OTHER."},
    {"ZIGZAG", {">..####", "##.####", "##...##", "####.##", "####...", "######.", "######*"}, LOOPS, 9, 8, "3FFRFFL]", "TWO FORWARD, TURN, TWO FORWARD, TURN BACK."},
    // Step 3: repeats inside repeats.
    {"ROUND THE RING", {">..g", "###.", "*##.", "g..."}, LOOPS, 8, 7, "33F]R]F", "A REPEAT CAN GO INSIDE A REPEAT."},
    {"THE BIG SQUARE", {">.....", "#####.", "*####.", ".####.", ".####.", "......"}, LOOPS, 10, 9, "35F]R]3F]", "THREE SIDES ARE THE SAME."},
    // Step 4: sensing walls.
    {"FOLLOW THE PATH", {">.....", "#####.", "*....."}, SENSE, 6, 5, "UwRF]", "REPEAT UNTIL THE STAR, AND IF A WALL IS AHEAD, TURN."},
    {"THE SPIRAL", {">......", "######.", "....*#.", ".#####.", "......."}, SENSE, 6, 5, "UwRF]", "THE SAME PROGRAM WORKS ON ANY PATH LIKE THIS!"},
    {"LEFT THIS TIME", {"*.....", "#####.", ">....."}, SENSE, 6, 5, "UwLF]", "WHICH WAY DOES THIS PATH TURN?"},
    {"THE LEFT SPIRAL", {".......", ".#####.", "....*#.", "######.", ">......"}, SENSE, 6, 5, "UwLF]", "YOU ARE A PROGRAMMER!"},
};

void cdq_Settings_Default(Cdq_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = CDQ_SETTINGS_MAGIC;
    s.speed = 1;
}

void cdq_Settings_Clamp(Cdq_Settings_t& s){
    s.magic = CDQ_SETTINGS_MAGIC;
    for (auto& row : s.stars) for (uint8_t& v : row) if (v > 3) v = 3;
    s.unlockAll = s.unlockAll ? 1 : 0;
    if (s.speed > 2) s.speed = 1;
    for (int c = 0; c < 3; c++){
        s.saved[c][CDQ_PROGRAM] = '\0';
        if (s.savedLevel[c] >= CDQ_LEVELS) s.savedLevel[c] = 0;
    }
}

const Cdq_Level_t& cdq_Level(int i){ return LEVELS[i >= 0 && i < CDQ_LEVELS ? i : 0]; }

void cdq_Level_Size(const Cdq_Level_t& l, int& w, int& h){
    w = h = 0;
    for (int y = 0; y < CDQ_MAP_H && l.rows[y]; y++){
        const int n = (int)strlen(l.rows[y]);
        if (n > w) w = n;
        h = y + 1;
    }
}

char cdq_Cell(const Cdq_Level_t& l, int x, int y){
    if (y < 0 || y >= CDQ_MAP_H || x < 0 || !l.rows[y] || x >= (int)strlen(l.rows[y])) return '#';
    const char c = l.rows[y][x];
    return c == ' ' ? '#' : c;
}

bool cdq_Start(const Cdq_Level_t& l, int& x, int& y, uint8_t& dir){
    for (y = 0; y < CDQ_MAP_H && l.rows[y]; y++)
        for (x = 0; l.rows[y][x]; x++){
            switch (l.rows[y][x]){
                case '^': dir = 0; return true;
                case '>': dir = 1; return true;
                case 'v': dir = 2; return true;
                case '<': dir = 3; return true;
            }
        }
    x = y = 0;
    dir = 1;
    return false;
}

int cdq_Palette(const Cdq_Level_t& l, char* out, int max){
    int n = 0;
    auto add = [&](char c){ if (n < max) out[n++] = c; };
    if (l.allow & CDQ_A_MOVE) add(CDQ_FWD);
    if (l.allow & CDQ_A_TURN){ add(CDQ_LEFT); add(CDQ_RIGHT); }
    if (l.allow & CDQ_A_REPEAT){ add(CDQ_REP2); add(CDQ_REP3); add(CDQ_REP4); add(CDQ_REP5); add(CDQ_REP6); }
    if (l.allow & CDQ_A_UNTIL) add(CDQ_UNTIL);
    if (l.allow & CDQ_A_SENSE) add(CDQ_IF_WALL);
    if (l.allow & (CDQ_A_REPEAT | CDQ_A_UNTIL)) add(CDQ_END);
    return n;
}

static bool opens(char c){ return (c >= CDQ_REP2 && c <= CDQ_REP6) || c == CDQ_UNTIL; }
static bool known(char c){ return c == CDQ_FWD || c == CDQ_LEFT || c == CDQ_RIGHT || opens(c) || c == CDQ_END || c == CDQ_IF_WALL; }

/** The END closing the block opened at @p i, or -1. */
static int match(const char* p, int i){
    int depth = 0;
    for (int j = i; p[j]; j++){
        if (opens(p[j])) depth++;
        else if (p[j] == CDQ_END && --depth == 0) return j;
    }
    return -1;
}

/** The index after the block starting at @p i (-1 if it is broken). */
static int after(const char* p, int i){
    if (!p[i] || p[i] == CDQ_END) return -1;
    if (opens(p[i])){ const int e = match(p, i); return e < 0 ? -1 : e + 1; }
    if (p[i] == CDQ_IF_WALL) return after(p, i + 1);
    return i + 1;
}

bool cdq_Valid(const char* p){
    if (!p || !*p) return false;
    for (int i = 0; p[i]; i++) if (!known(p[i])) return false;
    for (int i = 0; p[i];){
        const int n = after(p, i);
        if (n < 0) return false;
        // Inside a block too: check what it holds.
        if (opens(p[i])){
            const int e = n - 1;
            for (int j = i + 1; j < e;){ const int m = after(p, j); if (m < 0 || m > e) return false; j = m; }
            if (e == i + 1) return false;       // an empty REPEAT
        }
        i = n;
    }
    return true;
}

int cdq_Count(const char* p){ return p ? (int)strlen(p) : 0; }

struct Ctx_t {
    const Cdq_Level_t* l;
    const char* p;
    int x, y;
    uint8_t dir;
    bool done;
    Cdq_Run_t* out;
    int8_t gx[16], gy[16];
    int got;
};

static void record(Ctx_t& c, uint8_t block, uint8_t gem){
    if (c.out->steps >= CDQ_TRACE){ c.out->result = CDQ_TOO_LONG; c.done = true; return; }
    c.out->step[c.out->steps++] = {(int8_t)c.x, (int8_t)c.y, c.dir, block, gem};
}

static bool wall_Ahead(const Ctx_t& c){
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    return cdq_Cell(*c.l, c.x + DX[c.dir], c.y + DY[c.dir]) == '#';
}

static int exec(Ctx_t& c, int i);

static void exec_Range(Ctx_t& c, int from, int to){
    for (int i = from; i < to && !c.done;){
        const int n = exec(c, i);
        if (n < 0) return;
        i = n;
    }
}

static int exec(Ctx_t& c, int i){
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    const char b = c.p[i];
    switch (b){
        case CDQ_FWD: {
            const int nx = c.x + DX[c.dir], ny = c.y + DY[c.dir];
            if (cdq_Cell(*c.l, nx, ny) == '#'){ record(c, (uint8_t)i, 0); c.out->result = CDQ_CRASH; c.done = true; return i + 1; }
            c.x = nx;
            c.y = ny;
            uint8_t gem = 0;
            if (cdq_Cell(*c.l, nx, ny) == 'g'){
                bool had = false;
                for (int k = 0; k < c.got; k++) if (c.gx[k] == nx && c.gy[k] == ny) had = true;
                if (!had && c.got < 16){ c.gx[c.got] = (int8_t)nx; c.gy[c.got] = (int8_t)ny; c.got++; gem = 1; }
            }
            record(c, (uint8_t)i, gem);
            if (!c.done && cdq_Cell(*c.l, nx, ny) == '*'){ c.out->result = CDQ_GOAL; c.done = true; }
            return i + 1;
        }
        case CDQ_LEFT: c.dir = (uint8_t)((c.dir + 3) % 4); record(c, (uint8_t)i, 0); return i + 1;
        case CDQ_RIGHT: c.dir = (uint8_t)((c.dir + 1) % 4); record(c, (uint8_t)i, 0); return i + 1;
        case CDQ_IF_WALL: {
            const int next = after(c.p, i + 1);
            if (wall_Ahead(c)) exec(c, i + 1);
            return next;
        }
        case CDQ_UNTIL: {
            const int e = match(c.p, i);
            while (!c.done){
                const int before = c.out->steps;
                exec_Range(c, i + 1, e);
                if (c.out->steps == before){ c.out->result = CDQ_TOO_LONG; c.done = true; }   // nothing moves: forever
            }
            return e + 1;
        }
        default:
            if (b >= CDQ_REP2 && b <= CDQ_REP6){
                const int e = match(c.p, i);
                for (int k = 0; k < b - '0' && !c.done; k++) exec_Range(c, i + 1, e);
                return e + 1;
            }
            return -1;
    }
}

void cdq_Run(const Cdq_Level_t& l, const char* program, Cdq_Run_t& out){
    memset(&out, 0, sizeof(out));
    for (int y = 0; y < CDQ_MAP_H && l.rows[y]; y++) for (int x = 0; l.rows[y][x]; x++) if (l.rows[y][x] == 'g') out.gemsTotal++;
    Ctx_t c = {};
    c.l = &l;
    c.p = program;
    c.out = &out;
    cdq_Start(l, c.x, c.y, c.dir);
    record(c, 0xFF, 0);
    if (!cdq_Valid(program)){ out.result = CDQ_BAD_PROGRAM; return; }
    out.result = CDQ_STOPPED;
    exec_Range(c, 0, (int)strlen(program));
    out.gems = (uint8_t)c.got;
    if (out.result == CDQ_GOAL){
        out.stars = 1;
        if (cdq_Count(program) <= l.par) out.stars++;
        if (out.gems >= out.gemsTotal) out.stars++;
    }
}

bool cdq_Unlocked(const Cdq_Settings_t& s, uint8_t child, int level){
    if (s.unlockAll || level <= 0) return true;
    if (level >= CDQ_LEVELS) return false;
    return s.stars[child % 3][level - 1] > 0;
}

const char* cdq_Block_Name(char b){
    switch (b){
        case CDQ_FWD: return "FORWARD";
        case CDQ_LEFT: return "TURN LEFT";
        case CDQ_RIGHT: return "TURN RIGHT";
        case CDQ_REP2: return "REPEAT 2";
        case CDQ_REP3: return "REPEAT 3";
        case CDQ_REP4: return "REPEAT 4";
        case CDQ_REP5: return "REPEAT 5";
        case CDQ_REP6: return "REPEAT 6";
        case CDQ_UNTIL: return "UNTIL THE STAR";
        case CDQ_END: return "END OF REPEAT";
        case CDQ_IF_WALL: return "IF WALL AHEAD";
        default: return "";
    }
}
