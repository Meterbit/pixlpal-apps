/**
 * @file life_Engine.h
 * @brief Cellular automata on a wrapping grid of up to 256 x 128 cells, 64 cells a word, bit-parallel: the
 *        "life-like" rules (B/S: Life, HighLife, Day & Night, Seeds, Maze, Anneal), Brian's Brain (three states),
 *        and the elementary one-dimensional rules (30, 90, 110), drawn as a scroll. A row of words is also a row of a
 *        one-colour picture (bit 0 of each byte the leftmost cell). Pure C++: tested on a PC.
 */
#ifndef LIFE_ENGINE_H
#define LIFE_ENGINE_H

#include <stdint.h>

#define LIFE_W_MAX      256
#define LIFE_H_MAX      128
#define LIFE_WORDS_MAX  (LIFE_W_MAX / 64)

enum Life_Kind_e { LIFE_TOTALISTIC = 0, LIFE_BRIAN, LIFE_ELEMENTARY };

struct Life_Rule_t {
    const char* name;
    const char* code;                   // "B3/S23"
    uint8_t kind;
    uint16_t birth, survive;            // bit n: n neighbours (totalistic)
    uint8_t wolfram;                    // elementary
    uint8_t density;                    // percent alive when seeded at random (0: a small blob in the middle)
};

extern const Life_Rule_t LIFE_RULES[];
extern const int LIFE_RULE_COUNT;

struct Life_t {
    int w, h, words;                    // cells across (64, 128 or 256), down, words a row
    uint8_t rule;
    uint64_t cur[LIFE_H_MAX][LIFE_WORDS_MAX];    // alive (Brian's Brain: on)
    uint64_t prev[LIFE_H_MAX][LIFE_WORDS_MAX];   // the generation before
    uint64_t older[LIFE_H_MAX][LIFE_WORDS_MAX];  // alive for three generations and more; Brian's Brain: dying
    uint32_t generation;
    uint32_t population;
    uint32_t hash[4];                   // the last generations', to see when it has settled
    uint16_t stillFor;                  // generations it has repeated (period 1 or 2)
    uint32_t seed;
};

void life_Init(Life_t& L, int w, int h, uint8_t rule, uint32_t seed);
/** Seeds it again for its rule (random, or a blob, or for a 1-D rule one cell or a random row). */
void life_Seed(Life_t& L, uint32_t seed);
void life_Step(Life_t& L);
/** Settled: it has repeated for a while, or (nearly) died out. */
bool life_Settled(const Life_t& L);
/** Cell (x, y) of a plane. */
static inline bool life_Get(const uint64_t plane[][LIFE_WORDS_MAX], int x, int y){ return (plane[y][x >> 6] >> (x & 63)) & 1; }
static inline void life_Set(uint64_t plane[][LIFE_WORDS_MAX], int x, int y, bool on){
    const uint64_t b = (uint64_t)1 << (x & 63);
    if (on) plane[y][x >> 6] |= b; else plane[y][x >> 6] &= ~b;
}

#endif
