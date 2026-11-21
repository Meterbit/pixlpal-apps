// Scoreboard's scoring rules: see sb_Rules.h.
#include "sb_Rules.h"
#include <string.h>
#include <stdio.h>

bool sb_Racket(uint8_t sport){ return sport == SB_VOLLEYBALL || sport == SB_TABLE_TENNIS || sport == SB_BADMINTON || sport == SB_TENNIS; }

uint8_t sb_To_Win(uint8_t bestOf){ return (uint8_t)(bestOf / 2 + 1); }

void sb_New_Match(Sb_Match_t& m){ memset(&m, 0, sizeof(m)); }

static int set_Won(Sb_Match_t& m, uint8_t bestOf, uint8_t side, uint8_t a, uint8_t b){
    if (m.setsPlayed < SB_SETS_SHOWN){ m.setScore[m.setsPlayed][0] = a; m.setScore[m.setsPlayed][1] = b; }
    m.setsPlayed++;
    m.sets[side]++;
    m.pts[0] = m.pts[1] = 0;
    m.games[0] = m.games[1] = 0;
    m.tiebreak = 0;
    if (m.sets[side] >= sb_To_Win(bestOf)){ m.winner = (uint8_t)(side + 1); return 3; }
    return 2;
}

int sb_Point(Sb_Match_t& m, uint8_t sport, uint8_t bestOf, uint8_t side, uint8_t n){
    if (m.winner || side > 1) return 0;
    const uint8_t other = (uint8_t)(1 - side);
    if (!sb_Racket(sport)){ m.pts[side] = (uint16_t)(m.pts[side] + (n ? n : 1)); return 0; }
    m.pts[side]++;
    const int a = m.pts[side], b = m.pts[other];
    switch (sport){
    case SB_VOLLEYBALL: {
        m.server = side;
        const bool deciding = m.sets[0] + 1 == sb_To_Win(bestOf) && m.sets[1] + 1 == sb_To_Win(bestOf);
        const int target = deciding && bestOf > 1 ? 15 : 25;
        if (a >= target && a - b >= 2) return set_Won(m, bestOf, side, (uint8_t)m.pts[0], (uint8_t)m.pts[1]);
        return 0; }
    case SB_TABLE_TENNIS: {
        if (a >= 11 && a - b >= 2){
            const int r = set_Won(m, bestOf, side, (uint8_t)m.pts[0], (uint8_t)m.pts[1]);
            m.firstServer = (uint8_t)(1 - m.firstServer);           // the other player serves first in the next game
            m.server = m.firstServer;
            return r;
        }
        const int total = m.pts[0] + m.pts[1];
        const int changes = total >= 20 ? 10 + (total - 20) : total / 2;      // every two points; every point from 10-10
        m.server = (uint8_t)((m.firstServer + changes) % 2);
        return 0; }
    case SB_BADMINTON: {
        m.server = side;
        if ((a >= 21 && a - b >= 2) || a == 30) return set_Won(m, bestOf, side, (uint8_t)m.pts[0], (uint8_t)m.pts[1]);
        return 0; }
    case SB_TENNIS: {
        if (m.tiebreak){
            if (a >= 7 && a - b >= 2){
                m.games[side]++;
                const uint8_t next = (uint8_t)(1 - m.firstServer);  // who received first in the tiebreak serves next
                const int r = set_Won(m, bestOf, side, m.games[0], m.games[1]);
                m.server = next;
                return r;
            }
            const int total = m.pts[0] + m.pts[1];
            m.server = (uint8_t)((m.firstServer + (total + 1) / 2) % 2);
            return 0;
        }
        if (a >= 4 && a - b >= 2){
            m.games[side]++;
            m.pts[0] = m.pts[1] = 0;
            m.server = (uint8_t)(1 - m.server);
            const int ga = m.games[side], gb = m.games[other];
            if ((ga >= 6 && ga - gb >= 2)) return set_Won(m, bestOf, side, m.games[0], m.games[1]);
            if (ga == 6 && gb == 6){ m.tiebreak = 1; m.firstServer = m.server; }
            return 1;
        }
        if (a >= 4 && b >= 4 && a == b){ m.pts[0] = m.pts[1] = 3; }        // deuce again: keep the numbers small
        return 0; }
    }
    return 0;
}

void sb_Tennis_Points(const Sb_Match_t& m, uint8_t side, char* out, int size){
    if (m.tiebreak){ snprintf(out, size, "%u", (unsigned)m.pts[side]); return; }
    const int a = m.pts[side], b = m.pts[1 - side];
    if (a >= 3 && b >= 3){ snprintf(out, size, "%s", a > b ? "AD" : a == b ? "40" : "-"); return; }
    static const char* const P[] = {"0", "15", "30", "40"};
    snprintf(out, size, "%s", P[a < 4 ? a : 3]);
}
