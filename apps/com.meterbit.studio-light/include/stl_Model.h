/**
 * @file stl_Model.h
 * @brief Studio Light's model: settings, and the colour of every pixel at any moment for each mode, so the screen
 *        is one function of time: solid, split, gradient, a cycle of colours (cut or faded), white light at a colour
 *        temperature, and effects (breathing, candle, rainbow, aurora, and a sunrise wake light that brightens over
 *        minutes). No Arduino, so it can be tested on a PC.
 */
#ifndef STL_MODEL_H
#define STL_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define STL_SETTINGS_MAGIC  0x53544C32UL    // "STL2"
#define STL_NVS_KEY         "studioLightCfg"
#define STL_COLORS          5

enum Stl_Mode_t : uint8_t { STL_SOLID = 0, STL_SPLIT, STL_CYCLE, STL_GRADIENT, STL_WHITE, STL_EFFECT, STL_MODE_COUNT };
enum Stl_Effect_t : uint8_t { STL_BREATHE = 0, STL_CANDLE, STL_RAINBOW, STL_AURORA, STL_SUNRISE, STL_EFFECT_COUNT };

struct Stl_Settings_t {
    uint32_t magic;
    uint8_t mode;                   /**< Stl_Mode_t */
    uint8_t power;                  /**< Off: the panel is dark. */
    uint32_t colors[STL_COLORS];    /**< 0xRRGGBB. */
    uint16_t cycleSeconds;          /**< Each colour of the cycle (1-600). */
    uint8_t fade;                   /**< The cycle fades between colours. */
    uint16_t kelvin;                /**< White light, 1800-10000 K. */
    uint8_t intensity;              /**< 1-100 per cent of the colours. */
    uint8_t effect;                 /**< Stl_Effect_t */
    uint8_t sunriseMinutes;         /**< The sunrise's length (1-60). */
};

void stl_Settings_Default(Stl_Settings_t& settings);
void stl_Settings_Clamp(Stl_Settings_t& settings);
const char* stl_Mode_Name(uint8_t mode);
const char* stl_Effect_Name(uint8_t effect);

/** @brief The colour of white light at @p kelvin, as 0xRRGGBB (Tanner Helland's fit to the blackbody curve). */
uint32_t stl_Kelvin_Rgb(uint16_t kelvin);
/** @brief 0xRRGGBB a fraction @p t of the way from @p a to @p b. */
uint32_t stl_Mix(uint32_t a, uint32_t b, float t);
/** @brief @p color at @p percent brightness. */
uint32_t stl_Scale(uint32_t color, uint8_t percent);

/** @brief True if the picture changes with time in this mode (so it is redrawn every frame). */
bool stl_Animated(const Stl_Settings_t& settings);
/**
 * @brief Fills @p rgb (w x h x 3) with the picture @p ms milliseconds after the mode started, intensity applied.
 */
void stl_Render(const Stl_Settings_t& settings, uint32_t ms, uint8_t* rgb, int w, int h);

#endif
