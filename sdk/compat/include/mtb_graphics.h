// Pixlpal compat: the panel, as the firmware's apps draw on it. Shapes go to Pixlpal (pxl_rect, pxl_line...), text is
// drawn in the app with the firmware's own fonts (mtb_fonts.h) and sent as one-colour bitmaps.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_heap_caps.h"
#include "mtb_colors.h"
#include "mtb_fonts.h"

#define PANEL_RES_X 256
#define PANEL_RES_Y 128

void mtb_Panel_Fill_Screen(uint16_t color);
void mtb_Panel_Draw_PixelRGB565(int16_t x, int16_t y, uint16_t color);
void mtb_Panel_Draw_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void mtb_Panel_Draw_HLine(int16_t y, int16_t x1, int16_t x2, uint16_t color);
void mtb_Panel_Draw_VLine(int16_t x, int16_t y1, int16_t y2, uint16_t color);
void mtb_Panel_Draw_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void mtb_Panel_Fill_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void mtb_Panel_Draw_Circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void mtb_Panel_Fill_Circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void mtb_Panel_Draw_Triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void mtb_Panel_Fill_Triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void mtb_Panel_Draw_Round_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t radius, uint16_t color);
void mtb_Panel_Fill_Round_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t radius, uint16_t color);
void mtb_Panel_Draw_FrameRGB565(int16_t x, int16_t y, int16_t width, int16_t height, const uint16_t* data);
void mtb_Panel_Draw_FrameRGB888(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t* data);
void mtb_Panel_Draw_Mono(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t* bits, uint32_t strideBytes, uint32_t srcX, uint16_t color, uint16_t backgroundColor);
void mtb_Panel_Draw_Mono_Fg(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t* bits, uint32_t strideBytes, uint32_t srcX, uint16_t color);
static inline void mtb_Panel_Begin_Update(void){}      // Pixlpal shows the frame at each pxl_yield
static inline void mtb_Panel_End_Update(void){}
/** Pixlpal compat: the firmware's fixed text widget, as the apps use it: text at (x, y) on its background colour. */
class Mtb_FixedText_t {
public:
    int16_t x, y;
    const uint8_t* font;
    uint16_t color, backgroundColor;
    Mtb_FixedText_t(int16_t x1, int16_t y1, const uint8_t* f = Terminal6x8, uint16_t c = OLIVE_GREEN, uint16_t bg = BLACK)
        : x(x1), y(y1), font(f), color(c), backgroundColor(bg) {}
    virtual ~Mtb_FixedText_t() {}
    uint16_t mtb_Write_String(const char* text);
    uint16_t mtb_Write_Colored_String(const char* text, uint16_t c){ color = c; return mtb_Write_String(text); }
    uint16_t mtb_Write_Colored_String(const char* text, uint16_t c, uint16_t bg){ color = c; backgroundColor = bg; return mtb_Write_String(text); }
    uint16_t mtb_Clear_String();
private:
    int16_t lastWidth_ = 0;
};
/** A picture file in the app's files, drawn at (xAxis, yAxis) at its own size, by Pixlpal (pxl_image). */
struct Mtb_LocalImage_t {
    char     imagePath[50] = {0};
    uint16_t xAxis = 0;
    uint16_t yAxis = 0;
    int8_t   scale = 1;
};
#include "freertos/FreeRTOS.h"
BaseType_t mtb_Draw_Local_Png(const Mtb_LocalImage_t& image);     /**< 0 when drawn */
BaseType_t mtb_Draw_Local_Svg(const Mtb_LocalImage_t& image);
BaseType_t mtb_Draw_Local_Jpg(const Mtb_LocalImage_t& image);
/** Raw JPEG pixels are the firmware's (the chip's decoder): this returns 1 (failed). To draw a JPEG, use
 *  mtb_Kit_Draw_Jpg or pxl_image, which Pixlpal decodes. */
uint8_t mtb_Decode_Jpg_RGB888(const uint8_t* jpg, size_t jpgSize, uint8_t** rgb, uint16_t* width, uint16_t* height, uint16_t* stride);
/** The panel's brightness, 0-255 (the owner's setting: it is kept). */
void mtb_Panel_Set_Brightness(uint8_t brightness);
extern uint16_t panelBrightness;
