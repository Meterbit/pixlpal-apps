// Cellular automata, bit-parallel (life_Engine.h).
#include "life_Engine.h"
#include <string.h>

static constexpr uint16_t life_Bits(int a = -1, int b = -1, int c = -1, int d = -1, int e = -1, int f = -1){
    return (uint16_t)((a >= 0 ? 1 << a : 0) | (b >= 0 ? 1 << b : 0) | (c >= 0 ? 1 << c : 0) | (d >= 0 ? 1 << d : 0) | (e >= 0 ? 1 << e : 0) | (f >= 0 ? 1 << f : 0));
}

const Life_Rule_t LIFE_RULES[] = {
    {"GAME OF LIFE", "B3/S23", LIFE_TOTALISTIC, life_Bits(3), life_Bits(2, 3), 0, 30},
    {"HIGHLIFE", "B36/S23", LIFE_TOTALISTIC, life_Bits(3, 6), life_Bits(2, 3), 0, 30},
    {"DAY & NIGHT", "B3678/S34678", LIFE_TOTALISTIC, life_Bits(3, 6, 7, 8), life_Bits(3, 4, 6, 7, 8), 0, 50},
    {"SEEDS", "B2/S", LIFE_TOTALISTIC, life_Bits(2), 0, 0, 0},
    {"MAZE", "B3/S12345", LIFE_TOTALISTIC, life_Bits(3), life_Bits(1, 2, 3, 4, 5), 0, 0},
    {"ANNEAL", "B4678/S35678", LIFE_TOTALISTIC, life_Bits(4, 6, 7, 8), life_Bits(3, 5, 6, 7, 8), 0, 50},
    {"BRIAN'S BRAIN", "B2/S/3", LIFE_BRIAN, life_Bits(2), 0, 0, 25},
    {"RULE 30", "ELEMENTARY", LIFE_ELEMENTARY, 0, 0, 30, 0},
    {"RULE 110", "ELEMENTARY", LIFE_ELEMENTARY, 0, 0, 110, 50},
    {"RULE 90", "ELEMENTARY", LIFE_ELEMENTARY, 0, 0, 90, 0},
};
const int LIFE_RULE_COUNT = (int)(sizeof(LIFE_RULES) / sizeof(LIFE_RULES[0]));

static uint32_t rnd(uint32_t& s){ s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static uint64_t rnd64(uint32_t& s){ return ((uint64_t)rnd(s) << 32) | rnd(s); }

static uint64_t row_Mask(const Life_t& L){ return L.w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << L.w) - 1); }

static void clear(Life_t& L){
    memset(L.cur, 0, sizeof(L.cur)); memset(L.prev, 0, sizeof(L.prev)); memset(L.older, 0, sizeof(L.older));
    L.generation = 0; L.stillFor = 0;
    memset(L.hash, 0, sizeof(L.hash));
}

static uint32_t count(const Life_t& L){
    uint32_t n = 0;
    for (int y = 0; y < L.h; y++) for (int w = 0; w < L.words; w++) n += (uint32_t)__builtin_popcountll(L.cur[y][w]);
    return n;
}

void life_Init(Life_t& L, int w, int h, uint8_t rule, uint32_t seed){
    L.w = w > LIFE_W_MAX ? LIFE_W_MAX : w < 64 ? 64 : w;
    L.h = h > LIFE_H_MAX ? LIFE_H_MAX : h < 8 ? 8 : h;
    L.words = L.w / 64;
    L.rule = rule < LIFE_RULE_COUNT ? rule : 0;
    life_Seed(L, seed);
}

void life_Seed(Life_t& L, uint32_t seed){
    clear(L);
    L.seed = seed ? seed : 0x9E3779B9u;
    const Life_Rule_t& r = LIFE_RULES[L.rule];
    if (r.kind == LIFE_ELEMENTARY){
        // A scroll: the new row comes in at the bottom. One cell in the middle, or a random row.
        if (r.density) for (int w = 0; w < L.words; w++) L.cur[L.h - 1][w] = rnd64(L.seed);
        else life_Set(L.cur, L.w / 2, L.h - 1, true);
    } else if (r.density){
        for (int y = 0; y < L.h; y++)
            for (int x = 0; x < L.w; x++) if ((int)(rnd(L.seed) % 100) < r.density) life_Set(L.cur, x, y, true);
    } else {
        // A small random blob in the middle, to grow from.
        const int bw = L.w / 16 + 2, bh = L.h / 16 + 2;
        for (int y = L.h / 2 - bh / 2; y < L.h / 2 + bh / 2; y++)
            for (int x = L.w / 2 - bw / 2; x < L.w / 2 + bw / 2; x++) if (rnd(L.seed) % 100 < 40) life_Set(L.cur, x, y, true);
    }
    L.population = count(L);
}

// The row of a plane shifted by one cell, wrapping around the grid: west (each cell gets its left neighbour) or east.
static void shift_Row(const Life_t& L, const uint64_t* in, uint64_t* west, uint64_t* east){
    const int n = L.words;
    const uint64_t mask = row_Mask(L);
    for (int w = 0; w < n; w++){
        const uint64_t lo = in[(w + n - 1) % n], hi = in[(w + 1) % n];
        if (L.w >= 64){
            west[w] = (in[w] << 1) | (lo >> 63);
            east[w] = (in[w] >> 1) | (hi << 63);
        } else {
            west[w] = ((in[w] << 1) | (in[w] >> (L.w - 1))) & mask;
            east[w] = ((in[w] >> 1) | (in[w] << (L.w - 1))) & mask;
        }
    }
}

// Adds a plane of ones to a 4-bit count, bit-parallel.
static inline void add(uint64_t& b0, uint64_t& b1, uint64_t& b2, uint64_t& b3, uint64_t x){
    uint64_t c = b0 & x; b0 ^= x;
    uint64_t d = b1 & c; b1 ^= c;
    uint64_t e = b2 & d; b2 ^= d;
    b3 |= e;
}

static inline uint64_t equals(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3, int n){
    return ((n & 1) ? b0 : ~b0) & ((n & 2) ? b1 : ~b1) & ((n & 4) ? b2 : ~b2) & ((n & 8) ? b3 : ~b3);
}

static uint32_t fnv(const Life_t& L){
    uint32_t h = 2166136261u;
    for (int y = 0; y < L.h; y++) for (int w = 0; w < L.words; w++){
        const uint64_t v = L.cur[y][w];
        h = (h ^ (uint32_t)v) * 16777619u;
        h = (h ^ (uint32_t)(v >> 32)) * 16777619u;
    }
    return h;
}

static void step_Elementary(Life_t& L){
    const uint8_t rule = LIFE_RULES[L.rule].wolfram;
    memcpy(L.prev, L.cur, sizeof(L.cur));
    memmove(&L.cur[0][0], &L.cur[1][0], sizeof(L.cur[0]) * (size_t)(L.h - 1));
    uint64_t west[LIFE_WORDS_MAX], east[LIFE_WORDS_MAX];
    const uint64_t* c = L.prev[L.h - 1];
    shift_Row(L, c, west, east);
    for (int w = 0; w < L.words; w++){
        // A cell's left neighbour is in "west", its right one in "east": the eight patterns, as Wolfram numbers them.
        const uint64_t l = west[w], m = c[w], r = east[w];
        uint64_t out = 0;
        for (int p = 0; p < 8; p++){
            if (!((rule >> p) & 1)) continue;
            out |= ((p & 4) ? l : ~l) & ((p & 2) ? m : ~m) & ((p & 1) ? r : ~r);
        }
        L.cur[L.h - 1][w] = out & row_Mask(L);
    }
}

void life_Step(Life_t& L){
    const Life_Rule_t& rule = LIFE_RULES[L.rule];
    L.generation++;
    if (rule.kind == LIFE_ELEMENTARY){ step_Elementary(L); L.population = count(L); return; }
    static uint64_t next[LIFE_H_MAX][LIFE_WORDS_MAX], nextOlder[LIFE_H_MAX][LIFE_WORDS_MAX];
    const int H = L.h;
    for (int y = 0; y < H; y++){
        const int up = (y + H - 1) % H, down = (y + 1) % H;
        // The rows above, at and below, shifted west and east: the eight neighbours.
        uint64_t wU[LIFE_WORDS_MAX], eU[LIFE_WORDS_MAX], wM[LIFE_WORDS_MAX], eM[LIFE_WORDS_MAX], wD[LIFE_WORDS_MAX], eD[LIFE_WORDS_MAX];
        shift_Row(L, L.cur[up], wU, eU);
        shift_Row(L, L.cur[y], wM, eM);
        shift_Row(L, L.cur[down], wD, eD);
        for (int w = 0; w < L.words; w++){
            uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
            add(b0, b1, b2, b3, wU[w]); add(b0, b1, b2, b3, L.cur[up][w]); add(b0, b1, b2, b3, eU[w]);
            add(b0, b1, b2, b3, wM[w]);                                    add(b0, b1, b2, b3, eM[w]);
            add(b0, b1, b2, b3, wD[w]); add(b0, b1, b2, b3, L.cur[down][w]); add(b0, b1, b2, b3, eD[w]);
            const uint64_t me = L.cur[y][w];
            uint64_t born = 0, stay = 0;
            for (int n = 0; n <= 8; n++){
                const uint64_t e = ((rule.birth | rule.survive) >> n) & 1 ? equals(b0, b1, b2, b3, n) : 0;
                if ((rule.birth >> n) & 1) born |= e;
                if ((rule.survive >> n) & 1) stay |= e;
            }
            if (rule.kind == LIFE_BRIAN){
                // On -> dying -> off; off -> on with exactly two on neighbours (dying ones don't count).
                const uint64_t dying = L.older[y][w];
                next[y][w] = ~me & ~dying & born;
                nextOlder[y][w] = me;
            } else {
                const uint64_t n = (~me & born) | (me & stay);
                next[y][w] = n;
                nextOlder[y][w] = n & me & L.prev[y][w];     // alive three generations running
            }
        }
    }
    memcpy(L.prev, L.cur, sizeof(L.cur));
    memcpy(L.cur, next, sizeof(L.cur));
    memcpy(L.older, nextOlder, sizeof(L.older));
    L.population = count(L);
    // Settled: the same as one or two generations ago (Brian's Brain never stands still, but it can die out).
    const uint32_t h = fnv(L);
    if (h == L.hash[0] || h == L.hash[1]){ if (L.stillFor < 60000) L.stillFor++; } else L.stillFor = 0;
    L.hash[3] = L.hash[2]; L.hash[2] = L.hash[1]; L.hash[1] = L.hash[0]; L.hash[0] = h;
}

bool life_Settled(const Life_t& L){
    if (LIFE_RULES[L.rule].kind == LIFE_ELEMENTARY) return L.population == 0 && L.generation > (uint32_t)L.h;
    const uint32_t cells = (uint32_t)(L.w * L.h);
    return L.stillFor >= 40 || L.population < cells / 400 + 2;
}
