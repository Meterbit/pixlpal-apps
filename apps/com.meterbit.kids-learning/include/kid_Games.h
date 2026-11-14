/**
 * @file kid_Games.h
 * @brief The reward games (original, made for the knob), played in the area below the status bar and a score line
 *        (y 20-127, 256 wide). Both run at 25 steps a second. Tested on a PC.
 *
 *  Star Catcher: turn the knob to slide a basket; catch the falling stars (and the odd heart, a life back), dodge the
 *  rocks. Faster as the score climbs; three lives.
 *  Cave Glider: turn the knob to steer a glider up and down through a winding cave that narrows as it goes; collect
 *  the gems on the way.
 */
#ifndef KID_GAMES_H
#define KID_GAMES_H

#include <stdint.h>

#define KID_PLAY_TOP    20
#define KID_PLAY_BOTTOM 127
#define KID_CATCH_OBJECTS 10
#define KID_CAVE_COLUMNS 66             /**< 4 px each, a little more than the screen */

enum Kid_Thing_t : uint8_t { KID_NONE = 0, KID_STAR, KID_ROCK, KID_HEART };

struct Kid_Catch_t {
    int16_t basket;                     /**< Centre x */
    struct { int16_t x; int32_t y; int16_t vy; uint8_t kind; } obj[KID_CATCH_OBJECTS];   /**< y and vy in 1/256 px */
    uint16_t score;
    uint8_t lives, over;
    uint32_t tick;
    uint8_t flash;                      /**< Steps left to show a catch or a hit */
    uint8_t lastHit;                    /**< What was caught last (for a sound) */
};
void kid_Catch_Start(Kid_Catch_t& g);
void kid_Catch_Move(Kid_Catch_t& g, int steps);
/** @brief One step. @return what happened: KID_NONE, KID_STAR caught, KID_ROCK hit, KID_HEART caught. */
uint8_t kid_Catch_Step(Kid_Catch_t& g, uint32_t& rng);

struct Kid_Cave_t {
    int16_t shipY;                      /**< Centre y */
    int16_t top[KID_CAVE_COLUMNS], bottom[KID_CAVE_COLUMNS];   /**< Wall edges, column 0 at the left */
    int16_t gemY[KID_CAVE_COLUMNS];     /**< A gem's y in the column, 0 none */
    uint8_t shift;                      /**< 0-3 px scrolled into column 0 */
    int16_t centre, gap;
    uint32_t distance;
    uint16_t gems;
    uint8_t over;
    uint32_t tick;
};
#define KID_SHIP_X 44
void kid_Cave_Start(Kid_Cave_t& g, uint32_t& rng);
void kid_Cave_Move(Kid_Cave_t& g, int steps);
/** @brief One step. @return 1 a gem collected, 2 crashed, 0 otherwise. */
uint8_t kid_Cave_Step(Kid_Cave_t& g, uint32_t& rng);
/** @brief The score: distance in metres plus 10 a gem. */
uint32_t kid_Cave_Score(const Kid_Cave_t& g);

#endif
