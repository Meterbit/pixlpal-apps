// The reward games.
#include "kid_Games.h"
#include "kid_Model.h"
#include <string.h>

// Star Catcher --------------------------------------------------------------------------------------------------------
static const int16_t BASKET_Y = 116, BASKET_HALF = 13;

void kid_Catch_Start(Kid_Catch_t& g){
    memset(&g, 0, sizeof(g));
    g.basket = 128;
    g.lives = 3;
}

void kid_Catch_Move(Kid_Catch_t& g, int steps){
    if (g.over) return;
    int x = g.basket + steps * 9;
    if (x < BASKET_HALF) x = BASKET_HALF;
    if (x > 255 - BASKET_HALF) x = 255 - BASKET_HALF;
    g.basket = (int16_t)x;
}

uint8_t kid_Catch_Step(Kid_Catch_t& g, uint32_t& rng){
    if (g.over) return KID_NONE;
    g.tick++;
    if (g.flash) g.flash--;
    uint8_t result = KID_NONE;
    // Faster as the score climbs: from 1 px a step to about 3.
    const int16_t speed = (int16_t)(256 + (g.score > 60 ? 60 : g.score) * 9);
    for (auto& o : g.obj){
        if (o.kind == KID_NONE) continue;
        o.y += o.vy;
        const int16_t y = (int16_t)(o.y >> 8);
        if (y >= BASKET_Y - 3 && y <= BASKET_Y + 2 && o.x >= g.basket - BASKET_HALF - 2 && o.x <= g.basket + BASKET_HALF + 2){
            if (o.kind == KID_STAR){ g.score++; result = KID_STAR; }
            else if (o.kind == KID_HEART){ if (g.lives < 5) g.lives++; result = KID_HEART; }
            else { if (g.lives) g.lives--; result = KID_ROCK; if (!g.lives) g.over = 1; }
            g.lastHit = o.kind;
            g.flash = 6;
            o.kind = KID_NONE;
        } else if (y > KID_PLAY_BOTTOM) o.kind = KID_NONE;
    }
    // A new one now and then, more often as it speeds up.
    const uint32_t every = g.score > 40 ? 12 : g.score > 15 ? 16 : 22;
    if (g.tick % every == 0){
        for (auto& o : g.obj){
            if (o.kind != KID_NONE) continue;
            const int r = kid_Random_Range(rng, 0, 99);
            o.kind = r < 68 ? KID_STAR : r < 96 ? KID_ROCK : KID_HEART;
            o.x = (int16_t)kid_Random_Range(rng, 8, 247);
            o.y = (int32_t)KID_PLAY_TOP << 8;
            o.vy = (int16_t)(speed + kid_Random_Range(rng, 0, 80));
            break;
        }
    }
    return result;
}

// Cave Glider ---------------------------------------------------------------------------------------------------------
static void new_Column(Kid_Cave_t& g, int i, uint32_t& rng){
    // The cave winds slowly and narrows from 64 px to 30 over the first 1,500 m.
    g.centre = (int16_t)(g.centre + kid_Random_Range(rng, -3, 3));
    const int narrow = (int)(g.distance / 50);
    g.gap = (int16_t)(64 - (narrow > 34 ? 34 : narrow));
    const int lo = KID_PLAY_TOP + 4 + g.gap / 2, hi = KID_PLAY_BOTTOM - 4 - g.gap / 2;
    if (g.centre < lo) g.centre = (int16_t)lo;
    if (g.centre > hi) g.centre = (int16_t)hi;
    g.top[i] = (int16_t)(g.centre - g.gap / 2);
    g.bottom[i] = (int16_t)(g.centre + g.gap / 2);
    g.gemY[i] = kid_Random_Range(rng, 0, 99) < 4 ? (int16_t)kid_Random_Range(rng, g.top[i] + 6, g.bottom[i] - 6) : 0;
}

void kid_Cave_Start(Kid_Cave_t& g, uint32_t& rng){
    memset(&g, 0, sizeof(g));
    g.centre = (KID_PLAY_TOP + KID_PLAY_BOTTOM) / 2;
    g.shipY = g.centre;
    for (int i = 0; i < KID_CAVE_COLUMNS; i++){ new_Column(g, i, rng); g.gemY[i] = 0; }
}

void kid_Cave_Move(Kid_Cave_t& g, int steps){
    if (g.over) return;
    int y = g.shipY + steps * 4;
    if (y < KID_PLAY_TOP + 2) y = KID_PLAY_TOP + 2;
    if (y > KID_PLAY_BOTTOM - 2) y = KID_PLAY_BOTTOM - 2;
    g.shipY = (int16_t)y;
}

uint8_t kid_Cave_Step(Kid_Cave_t& g, uint32_t& rng){
    if (g.over) return 0;
    g.tick++;
    // 1 px a step, 2 after 800 m.
    const int speed = g.distance > 800 ? 2 : 1;
    for (int s = 0; s < speed; s++){
        g.distance++;
        if (++g.shift >= 4){
            g.shift = 0;
            memmove(g.top, g.top + 1, sizeof(g.top[0]) * (KID_CAVE_COLUMNS - 1));
            memmove(g.bottom, g.bottom + 1, sizeof(g.bottom[0]) * (KID_CAVE_COLUMNS - 1));
            memmove(g.gemY, g.gemY + 1, sizeof(g.gemY[0]) * (KID_CAVE_COLUMNS - 1));
            new_Column(g, KID_CAVE_COLUMNS - 1, rng);
        }
    }
    // The glider is 9 x 5 at KID_SHIP_X: the columns under it.
    uint8_t result = 0;
    const int first = (KID_SHIP_X - 4 + g.shift) / 4, last = (KID_SHIP_X + 4 + g.shift) / 4;
    for (int c = first; c <= last && c < KID_CAVE_COLUMNS; c++){
        if (g.shipY - 2 <= g.top[c] || g.shipY + 2 >= g.bottom[c]){ g.over = 1; return 2; }
        if (g.gemY[c] && g.gemY[c] >= g.shipY - 5 && g.gemY[c] <= g.shipY + 5){ g.gems++; g.gemY[c] = 0; result = 1; }
    }
    return result;
}

uint32_t kid_Cave_Score(const Kid_Cave_t& g){ return g.distance / 10 + g.gems * 10u; }
