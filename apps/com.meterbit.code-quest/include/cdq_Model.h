/**
 * @file cdq_Model.h
 * @brief Code Quest {19,4} (KIDS, ages 6-12): programming a robot to the star. A program is a row of blocks: forward,
 *        turn left, turn right; REPEAT 2-5 [ ... ]; REPEAT UNTIL THE STAR [ ... ]; IF WALL AHEAD (the next block).
 *        16 levels in four steps (moves, repeats, repeats in repeats, sensing walls), each limited in blocks so that
 *        the new idea is needed. Stars: reaching the star; no more blocks than needed; every gem. No Arduino: tested
 *        on a PC.
 */
#ifndef CDQ_MODEL_H
#define CDQ_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define CDQ_NVS_KEY         "cdqSet"
#define CDQ_SETTINGS_MAGIC  0x43445101u
#define CDQ_LEVELS          16
#define CDQ_MAP_W           21
#define CDQ_MAP_H           7
#define CDQ_PROGRAM         24
#define CDQ_TRACE           400

/** Blocks, as the characters a program is written in. */
enum Cdq_Block_t : char {
    CDQ_FWD = 'F', CDQ_LEFT = 'L', CDQ_RIGHT = 'R',
    CDQ_REP2 = '2', CDQ_REP3 = '3', CDQ_REP4 = '4', CDQ_REP5 = '5', CDQ_REP6 = '6',
    CDQ_UNTIL = 'U', CDQ_END = ']', CDQ_IF_WALL = 'w'
};
/** What a level lets the child use. */
enum Cdq_Allow_t : uint8_t { CDQ_A_MOVE = 1, CDQ_A_TURN = 2, CDQ_A_REPEAT = 4, CDQ_A_UNTIL = 8, CDQ_A_SENSE = 16 };
enum Cdq_Result_t : uint8_t { CDQ_GOAL = 0, CDQ_CRASH, CDQ_STOPPED, CDQ_TOO_LONG, CDQ_BAD_PROGRAM };

struct Cdq_Level_t {
    const char* name;
    const char* rows[CDQ_MAP_H];        /**< '#' wall, '.' floor, '*' the star, 'g' a gem, '>' '<' '^' 'v' the robot */
    uint8_t allow;                      /**< Cdq_Allow_t */
    uint8_t maxBlocks;
    uint8_t par;                        /**< the fewest blocks that reach the star */
    const char* solution;               /**< one that does (for the tests) */
    const char* tip;
};

struct Cdq_Settings_t {
    uint32_t magic;
    uint8_t stars[3][CDQ_LEVELS];       /**< per child (Kids Learning's place): 0 not done, 1-3 */
    uint8_t unlockAll;
    uint8_t speed;                      /**< 0 slow, 1 normal, 2 fast */
    char saved[3][CDQ_PROGRAM + 1];     /**< each child's program on the level they were last on */
    uint8_t savedLevel[3];
};

/** One step the robot took: where it is, facing (0 N, 1 E, 2 S, 3 W), and the block doing it. */
struct Cdq_Step_t { int8_t x, y; uint8_t dir; uint8_t block; uint8_t gem; };

struct Cdq_Run_t {
    uint8_t result;                     /**< Cdq_Result_t */
    int steps;
    Cdq_Step_t step[CDQ_TRACE];         /**< step 0 is the start */
    uint8_t gems, gemsTotal;
    uint8_t stars;
};

void cdq_Settings_Default(Cdq_Settings_t& s);
void cdq_Settings_Clamp(Cdq_Settings_t& s);
const Cdq_Level_t& cdq_Level(int i);
/** @brief The level's size and where the robot starts. */
void cdq_Level_Size(const Cdq_Level_t& l, int& w, int& h);
bool cdq_Start(const Cdq_Level_t& l, int& x, int& y, uint8_t& dir);
char cdq_Cell(const Cdq_Level_t& l, int x, int y);       /**< '#' outside the map */
/** @brief The blocks a level offers, in palette order. @return how many. */
int cdq_Palette(const Cdq_Level_t& l, char* out, int max);
/** @brief The program is complete: every REPEAT closed, every IF followed by a block. */
bool cdq_Valid(const char* program);
/** @brief Blocks counted the way the child sees them (an END counts; a REPEAT's number is its block). */
int cdq_Count(const char* program);
/** @brief Runs @p program on the level: every step, how it ended, gems and stars. */
void cdq_Run(const Cdq_Level_t& l, const char* program, Cdq_Run_t& out);
/** @brief The level that may be played: 0, and each after one with a star (all with unlockAll). */
bool cdq_Unlocked(const Cdq_Settings_t& s, uint8_t child, int level);
const char* cdq_Block_Name(char b);     /**< "FORWARD", "REPEAT 3", "IF WALL AHEAD" */

#endif
