/**
 * @file stw_Model.h
 * @brief Timers' model: a stopwatch with laps, a countdown timer, and an interval timer (work and rest rounds, for
 *        Tabata, HIIT or Pomodoro), all kept as moments on the monotonic clock (microseconds), so they run on while
 *        another app is on the panel and come back exact. No Arduino, so it can be tested on a PC.
 */
#ifndef STW_MODEL_H
#define STW_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define STW_SETTINGS_MAGIC  0x53545731UL    // "STW1"
#define STW_NVS_KEY         "stopwatchCfg"
#define STW_LAPS            99
#define STW_PREP_SECONDS    5
#define STW_TIMER_MAX       (24UL * 3600 - 1)

enum Stw_Mode_t : uint8_t { STW_MODE_STOPWATCH = 0, STW_MODE_TIMER, STW_MODE_INTERVALS, STW_MODE_COUNT };
enum Stw_State_t : uint8_t { STW_IDLE = 0, STW_RUNNING, STW_PAUSED, STW_DONE };
enum Stw_Phase_t : uint8_t { STW_PHASE_PREP = 0, STW_PHASE_WORK, STW_PHASE_REST, STW_PHASE_FINISHED };

struct Stw_Watch_t {
    uint8_t running;
    int64_t startUs;                /**< When it last started. */
    int64_t heldUs;                 /**< Time counted before that. */
    uint8_t lapCount;
    int64_t laps[STW_LAPS];         /**< The elapsed time at each lap (splits). */
};

struct Stw_Timer_t {
    uint8_t state;                  /**< Stw_State_t */
    uint32_t seconds;               /**< The length it was started with. */
    int64_t endUs;                  /**< While running. */
    int64_t leftUs;                 /**< While paused. */
    int64_t doneUs;                 /**< When it reached zero. */
};

struct Stw_Intervals_t {
    uint8_t state;                  /**< Stw_State_t */
    uint8_t phase;                  /**< Stw_Phase_t */
    uint8_t round;                  /**< 1-based. */
    int64_t endUs;                  /**< The phase's end while running. */
    int64_t leftUs;                 /**< Of the phase, while paused. */
    uint16_t work, rest;            /**< Seconds, fixed at the start. */
    uint8_t rounds;
    int64_t doneUs;
};

struct Stw_Settings_t {
    uint32_t magic;
    uint8_t mode;                   /**< Stw_Mode_t shown. */
    uint32_t timerSeconds;          /**< The countdown's length (5 s to 23:59:59). */
    uint16_t work;                  /**< Interval work, seconds (5-3600). */
    uint16_t rest;                  /**< Interval rest, seconds (0-1800). */
    uint8_t rounds;                 /**< 1-99. */
    uint8_t hundredths;             /**< The stopwatch shows hundredths. */
    uint8_t flash;                  /**< The panel flashes when a timer ends. */
    uint8_t theme;                  /**< Colour of the digits, 0-4. */
};

struct Stw_Preset_t { const char* name; uint16_t work, rest; uint8_t rounds; };

void stw_Settings_Default(Stw_Settings_t& settings);
void stw_Settings_Clamp(Stw_Settings_t& settings);
const Stw_Preset_t* stw_Presets(int& count);

// Stopwatch ---------------------------------------------------------------------------------------------------
int64_t stw_Watch_Elapsed(const Stw_Watch_t& watch, int64_t nowUs);
void stw_Watch_Toggle(Stw_Watch_t& watch, int64_t nowUs);
/** @brief A lap, while running. False when stopped or full. */
bool stw_Watch_Lap(Stw_Watch_t& watch, int64_t nowUs);
void stw_Watch_Reset(Stw_Watch_t& watch);
/** @brief The time of lap @p index (0-based), from the splits. */
int64_t stw_Watch_Lap_Time(const Stw_Watch_t& watch, int index);
/** @brief The fastest and slowest laps (-1 with fewer than two). */
void stw_Watch_Best_Worst(const Stw_Watch_t& watch, int& best, int& worst);

// Countdown ---------------------------------------------------------------------------------------------------
/** @brief Idle: starts @p seconds; running: pauses; paused: goes on; done: back to idle. */
void stw_Timer_Toggle(Stw_Timer_t& timer, uint32_t seconds, int64_t nowUs);
void stw_Timer_Reset(Stw_Timer_t& timer);
/** @brief Time left; idle: @p seconds. */
int64_t stw_Timer_Left(const Stw_Timer_t& timer, uint32_t seconds, int64_t nowUs);
/** @brief True once, when the timer reaches zero. */
bool stw_Timer_Update(Stw_Timer_t& timer, int64_t nowUs);
/** @brief The knob's step at a length: 5 s under a minute, 15 s under 5, 30 s under 10, a minute under an hour, then 5. */
uint32_t stw_Timer_Step(uint32_t seconds, bool up);

// Intervals ---------------------------------------------------------------------------------------------------
void stw_Intervals_Toggle(Stw_Intervals_t& iv, const Stw_Settings_t& settings, int64_t nowUs);
void stw_Intervals_Reset(Stw_Intervals_t& iv);
/** @brief Moves through phases that have ended. Returns the number of phase changes (0 if none). */
int stw_Intervals_Update(Stw_Intervals_t& iv, int64_t nowUs);
/** @brief Left of the phase, and of the whole workout. */
int64_t stw_Intervals_Phase_Left(const Stw_Intervals_t& iv, const Stw_Settings_t& settings, int64_t nowUs);
int64_t stw_Intervals_Total_Left(const Stw_Intervals_t& iv, const Stw_Settings_t& settings, int64_t nowUs);
/** @brief The whole workout, prep included, in seconds. */
uint32_t stw_Intervals_Length(uint16_t work, uint16_t rest, uint8_t rounds);
/** @brief The phase's length in seconds. */
uint32_t stw_Phase_Seconds(uint8_t phase, uint16_t work, uint16_t rest);

// Text --------------------------------------------------------------------------------------------------------
/** @brief "4:05.27", "1:04:05.2", "0:09": minutes and seconds, hours when needed, then @p decimals (0-2) of a second. */
void stw_Format(char* dest, size_t size, int64_t us, int decimals);
/** @brief Whole seconds for a countdown: rounded up, so "0:01" shows until the very end. */
int64_t stw_Ceil_Seconds(int64_t us);

#endif
