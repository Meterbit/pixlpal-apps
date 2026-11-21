/**
 * @file sb_Rules.h
 * @brief Scoreboard's scoring rules, without the app framework (tested on a computer too).
 *
 * Timed sports and "points only" just add up. Volleyball: sets to 25 (the deciding set to 15), two clear; the side
 * that wins a rally serves. Table tennis: games to 11, two clear; the serve changes every two points, every point from
 * 10-10, and the first server alternates game by game. Badminton: games to 21, two clear, capped at 30; the winner of
 * a rally serves. Tennis: 15, 30, 40, deuce and advantage; sets to 6, two clear, a tiebreak to 7 at 6-6; the serve
 * changes each game, and in a tiebreak after the first point then every two.
 */
#ifndef SB_RULES_H
#define SB_RULES_H

#include <stdint.h>

enum Sb_Sport_t : uint8_t { SB_FOOTBALL = 0, SB_BASKETBALL, SB_VOLLEYBALL, SB_TABLE_TENNIS, SB_BADMINTON, SB_TENNIS, SB_POINTS, SB_SPORTS };

#define SB_SETS_SHOWN 7

struct Sb_Match_t {
    uint16_t pts[2];                    // points in the game (or goals, baskets, the score)
    uint8_t games[2];                   // tennis: games in this set
    uint8_t sets[2];                    // sets (or games) won
    uint8_t setScore[SB_SETS_SHOWN][2]; // each finished set's score
    uint8_t setsPlayed;
    uint8_t server;                     // 0 or 1
    uint8_t firstServer;                // who served first in this game (table tennis) or tiebreak (tennis)
    uint8_t tiebreak;                   // tennis: a tiebreak is on
    uint8_t winner;                     // 0 none, 1 home, 2 away (the match is over)
};

bool sb_Racket(uint8_t sport);
/** Sets (or games) needed to win the match. */
uint8_t sb_To_Win(uint8_t bestOf);
void sb_New_Match(Sb_Match_t& m);
/** A point (or @p n of them, timed sports only) to @p side. Returns 1 if it won a game, 2 a set, 3 the match. */
int sb_Point(Sb_Match_t& m, uint8_t sport, uint8_t bestOf, uint8_t side, uint8_t n);
/** Tennis points as text ("0", "15", "30", "40", "AD", or a tiebreak's number). */
void sb_Tennis_Points(const Sb_Match_t& m, uint8_t side, char* out, int size);

#endif
