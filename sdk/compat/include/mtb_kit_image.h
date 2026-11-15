// Pixlpal compat: the firmware's picture kit (mtb_kit_image.h), on Pixlpal's decoders (pxl_image, API 0.5). The same
// fitting: a PNG or JPEG scaled by whole steps, an SVG to the largest size that fits; centred on a background colour.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct Mtb_Kit_Box_t { int16_t x, y, w, h; };

bool mtb_Kit_Draw_Svg(const char* svg, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);
bool mtb_Kit_Draw_Png(const uint8_t* png, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);
bool mtb_Kit_Draw_Jpg(const uint8_t* jpg, size_t length, Mtb_Kit_Box_t box, Mtb_Kit_Box_t* drawn = nullptr);
bool mtb_Kit_Draw_Image(const uint8_t* data, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);
int mtb_Kit_Fit_Scale(int w, int h, int boxW, int boxH);
/** RGBA rows of the picture fitted into the box (at most 256 x 256): free with heap_caps_free (free). */
uint8_t* mtb_Kit_Decode_Image(const uint8_t* data, size_t length, int boxW, int boxH, int* w, int* h);
bool mtb_Kit_Image_Size(const uint8_t* data, size_t length, int* w, int* h);
