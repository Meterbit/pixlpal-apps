#include "stl_Model.h"
#include <math.h>
#include <string.h>

void stl_Settings_Default(Stl_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = STL_SETTINGS_MAGIC;
    s.mode = STL_WHITE;
    s.power = 1;
    const uint32_t colors[STL_COLORS] = {0xFFFFFF, 0x8000FF, 0x00FF40, 0xFFD000, 0x0040FF};
    memcpy(s.colors, colors, sizeof(colors));
    s.cycleSeconds = 10;
    s.fade = 1;
    s.kelvin = 5600;
    s.intensity = 100;
    s.effect = STL_BREATHE;
    s.sunriseMinutes = 15;
}

void stl_Settings_Clamp(Stl_Settings_t& s){
    if (s.mode >= STL_MODE_COUNT) s.mode = STL_WHITE;
    s.power = s.power ? 1 : 0;
    for (uint32_t& c : s.colors) c &= 0xFFFFFF;
    if (s.cycleSeconds < 1) s.cycleSeconds = 1;
    if (s.cycleSeconds > 600) s.cycleSeconds = 600;
    s.fade = s.fade ? 1 : 0;
    if (s.kelvin < 1800) s.kelvin = 1800;
    if (s.kelvin > 10000) s.kelvin = 10000;
    if (s.intensity < 1) s.intensity = 1;
    if (s.intensity > 100) s.intensity = 100;
    if (s.effect >= STL_EFFECT_COUNT) s.effect = STL_BREATHE;
    if (s.sunriseMinutes < 1) s.sunriseMinutes = 1;
    if (s.sunriseMinutes > 60) s.sunriseMinutes = 60;
}

const char* stl_Mode_Name(uint8_t m){
    static const char* const NAMES[STL_MODE_COUNT] = {"SOLID", "SPLIT", "CYCLE", "GRADIENT", "WHITE", "EFFECT"};
    return m < STL_MODE_COUNT ? NAMES[m] : "";
}

const char* stl_Effect_Name(uint8_t e){
    static const char* const NAMES[STL_EFFECT_COUNT] = {"BREATHE", "CANDLE", "RAINBOW", "AURORA", "SUNRISE"};
    return e < STL_EFFECT_COUNT ? NAMES[e] : "";
}

static uint8_t stl_Clip(float v){ return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)lroundf(v); }

uint32_t stl_Kelvin_Rgb(uint16_t kelvin){
    const float t = kelvin / 100.0f;
    float r, g, b;
    if (t <= 66){ r = 255; g = 99.4708025861f * logf(t) - 161.1195681661f; }
    else { r = 329.698727446f * powf(t - 60, -0.1332047592f); g = 288.1221695283f * powf(t - 60, -0.0755148492f); }
    if (t >= 66) b = 255;
    else if (t <= 19) b = 0;
    else b = 138.5177312231f * logf(t - 10) - 305.0447927307f;
    return ((uint32_t)stl_Clip(r) << 16) | ((uint32_t)stl_Clip(g) << 8) | stl_Clip(b);
}

uint32_t stl_Mix(uint32_t a, uint32_t b, float t){
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const float ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const float br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return ((uint32_t)stl_Clip(ar + (br - ar) * t) << 16) | ((uint32_t)stl_Clip(ag + (bg - ag) * t) << 8) | stl_Clip(ab + (bb - ab) * t);
}

uint32_t stl_Scale(uint32_t c, uint8_t percent){
    return stl_Mix(0, c, percent / 100.0f);
}

bool stl_Animated(const Stl_Settings_t& s){
    return s.power && (s.mode == STL_CYCLE || s.mode == STL_EFFECT);
}

static uint32_t stl_Hue(float h){            // 0-1 around the colour wheel, full brightness.
    h -= floorf(h);
    const float x = h * 6;
    const int i = (int)x;
    const float f = x - i;
    const uint8_t up = stl_Clip(f * 255), down = stl_Clip((1 - f) * 255);
    switch (i % 6){
        case 0: return 0xFF0000 | (up << 8);
        case 1: return ((uint32_t)down << 16) | 0x00FF00;
        case 2: return 0x00FF00 | up;
        case 3: return ((uint32_t)down << 8) | 0x0000FF;
        case 4: return ((uint32_t)up << 16) | 0x0000FF;
        default: return 0xFF0000 | down;
    }
}

static inline void stl_Put(uint8_t* p, uint32_t c){ p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; }

// A small, repeatable noise for the candle.
static float stl_Noise(uint32_t n){
    n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7FFFFFFF) / 1073741824.0f;
}

static float stl_Smooth_Noise(float t){
    const uint32_t i = (uint32_t)floorf(t);
    const float f = t - floorf(t), u = f * f * (3 - 2 * f);
    return stl_Noise(i) * (1 - u) + stl_Noise(i + 1) * u;
}

void stl_Render(const Stl_Settings_t& s, uint32_t ms, uint8_t* rgb, int w, int h){
    const size_t pixels = (size_t)w * h;
    if (!s.power){ memset(rgb, 0, pixels * 3); return; }
    const uint8_t level = s.intensity;
    auto fill = [&](uint32_t c){
        c = stl_Scale(c, level);
        for (size_t i = 0; i < pixels; i++) stl_Put(rgb + i * 3, c);
    };
    // Columns that differ only across x: one colour per column, then copied down.
    auto columns = [&](auto colorAt){
        for (int x = 0; x < w; x++) stl_Put(rgb + x * 3, stl_Scale(colorAt(x), level));
        for (int y = 1; y < h; y++) memcpy(rgb + (size_t)y * w * 3, rgb, (size_t)w * 3);
    };
    const float seconds = ms / 1000.0f;
    switch (s.mode){
        case STL_SOLID: fill(s.colors[0]); break;
        case STL_SPLIT: columns([&](int x){ return x < w / 2 ? s.colors[0] : s.colors[1]; }); break;
        case STL_GRADIENT: columns([&](int x){ return stl_Mix(s.colors[0], s.colors[1], (float)x / (w - 1)); }); break;
        case STL_WHITE: fill(stl_Kelvin_Rgb(s.kelvin)); break;
        case STL_CYCLE: {
            const uint32_t period = s.cycleSeconds * 1000u;
            const uint32_t step = ms / period, into = ms % period;
            const uint32_t a = s.colors[step % STL_COLORS], b = s.colors[(step + 1) % STL_COLORS];
            // With fade: hold, then blend over the last third of each colour's time.
            const float blend = s.fade ? (into > period * 2 / 3 ? (into - period * 2 / 3) / (period / 3.0f) : 0) : 0;
            fill(stl_Mix(a, b, blend));
            break;
        }
        default: switch (s.effect){
            case STL_BREATHE: {
                const float b = 0.12f + 0.88f * (0.5f - 0.5f * cosf(seconds * 2 * (float)M_PI / 6));    // A breath every 6 s.
                fill(stl_Mix(0, s.colors[0], b));
                break;
            }
            case STL_CANDLE: {
                const float flicker = 0.78f + 0.12f * stl_Smooth_Noise(seconds * 7) + 0.06f * stl_Smooth_Noise(seconds * 19 + 100);
                fill(stl_Mix(0, stl_Kelvin_Rgb(1900), flicker));
                break;
            }
            case STL_RAINBOW: columns([&](int x){ return stl_Hue(seconds / 20 + (float)x / w); }); break;
            case STL_AURORA: {
                // A band of light per column (its height and colour drift), its brightness from a lookup by distance.
                static float falloff[128];
                static bool ready = false;
                if (!ready){ for (int i = 0; i < 128; i++) falloff[i] = expf(-powf(i / 32.0f, 2)); ready = true; }
                for (int x = 0; x < w; x++){
                    const float wave = sinf(x * 0.035f + seconds * 0.6f) + 0.5f * sinf(x * 0.09f - seconds * 0.9f);
                    const float centre = h * (0.45f + 0.12f * wave), spread = h * 0.22f;
                    const uint32_t c = stl_Mix(0x00FF80, 0x8020FF, 0.5f + 0.5f * sinf(x * 0.02f - seconds * 0.4f));
                    for (int y = 0; y < h; y++){
                        int i = (int)(fabsf(y - centre) / spread * 32);
                        if (i > 127) i = 127;
                        stl_Put(rgb + ((size_t)y * w + x) * 3, stl_Scale(stl_Mix(0x000010, c, falloff[i]), level));
                    }
                }
                break;
            }
            default: {
                // Sunrise: from a dim deep red to warm white over the set minutes, then stays.
                const float t = fminf(1.0f, seconds / (s.sunriseMinutes * 60.0f));
                const uint32_t color = t < 0.5f ? stl_Mix(0x200000, stl_Kelvin_Rgb(1900), t * 2) : stl_Mix(stl_Kelvin_Rgb(1900), stl_Kelvin_Rgb(4500), t * 2 - 1);
                fill(stl_Mix(0, color, 0.05f + 0.95f * t));
                break;
            }
        }
    }
}
