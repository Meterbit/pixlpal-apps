/**
 * @file mtb_kit_image.h
 * @brief Pictures fitted into a box on the panel: an SVG (flags, logos) rasterised to the largest size that fits,
 *        and a PNG scaled by whole steps (pixel art stays sharp: 128x64 art fills 256x128 at twice its size).
 *        Both keep the picture's shape and centre it on a background colour. Drawing goes through
 *        mtb_Panel_Draw_FrameRGB888(), so wrap calls in mtb_Panel_Begin_Update()/End_Update() as usual.
 */
#ifndef MTB_KIT_IMAGE_H
#define MTB_KIT_IMAGE_H

#include <stdint.h>
#include <stddef.h>

struct Mtb_Kit_Box_t { int16_t x, y, w, h; };

/**
 * @brief Draws SVG text into @p box. The text is parsed in a copy, so @p svg is left as it was.
 * @param drawn  If given, receives where the picture went (inside the box).
 * @return False if the SVG cannot be parsed or memory is short.
 */
bool mtb_Kit_Draw_Svg(const char* svg, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);

/**
 * @brief Draws PNG bytes into @p box: enlarged by the largest whole factor that fits, or reduced by the smallest
 *        whole divisor (averaging) when larger than the box. Transparent parts show @p background.
 * @return False if the PNG cannot be decoded.
 */
bool mtb_Kit_Draw_Png(const uint8_t* png, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);

/** @brief The whole-number scale for a w x h picture in a box: >1 enlarges, <-1 reduces by that divisor. */
/** @brief Draws a baseline JPEG (the hardware decoder) into @p box, fitted and centred. */
bool mtb_Kit_Draw_Jpg(const uint8_t* jpg, size_t length, Mtb_Kit_Box_t box, Mtb_Kit_Box_t* drawn = nullptr);
/** @brief PNG, JPEG or SVG, told apart by their first bytes. */
bool mtb_Kit_Draw_Image(const uint8_t* data, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn = nullptr);
int mtb_Kit_Fit_Scale(int w, int h, int boxW, int boxH);

#endif
