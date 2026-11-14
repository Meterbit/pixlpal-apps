#include "mtb_kit_image.h"
#include "mtb_graphics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "lodepng.h"
// The SVG library's code is compiled once, in meterbit_graphics (the project defines these for every file).
#undef NANOSVG_IMPLEMENTATION
#undef NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

static void* kit_Alloc(size_t size){
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(size);
}

static void kit_Free(void* p){ heap_caps_free(p); }

int mtb_Kit_Fit_Scale(int w, int h, int boxW, int boxH){
    if (w <= 0 || h <= 0 || boxW <= 0 || boxH <= 0) return 1;
    if (w <= boxW && h <= boxH){
        const int sx = boxW / w, sy = boxH / h;
        return sx < sy ? sx : sy;
    }
    int d = 2;
    while (w / d > boxW || h / d > boxH) d++;
    return -d;
}

// RGB888 rows of @p w x @p h, drawn at (x, y).
static void kit_Blit(const uint8_t* rgb, int16_t x, int16_t y, int16_t w, int16_t h){
    mtb_Panel_Draw_FrameRGB888(x, y, w, h, rgb);
}

static void kit_Background(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b){
    r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    b = (uint8_t)((c & 0x1F) * 255 / 31);
}

bool mtb_Kit_Draw_Svg(const char* svg, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){
    if (svg == nullptr || length == 0 || box.w <= 0 || box.h <= 0) return false;
    char* copy = (char*)kit_Alloc(length + 1);
    if (copy == nullptr) return false;
    memcpy(copy, svg, length);
    copy[length] = '\0';
    NSVGimage* image = nsvgParse(copy, "px", 96.0f);
    kit_Free(copy);
    if (image == nullptr || image->width <= 0 || image->height <= 0){ if (image) nsvgDelete(image); return false; }
    const float scale = fminf(box.w / image->width, box.h / image->height);
    const int w = (int)lroundf(image->width * scale), h = (int)lroundf(image->height * scale);
    uint8_t* rgba = (uint8_t*)kit_Alloc((size_t)w * h * 4);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    bool ok = false;
    if (rgba && rast && w > 0 && h > 0){
        memset(rgba, 0, (size_t)w * h * 4);
        nsvgRasterize(rast, image, 0, 0, scale, rgba, w, h, w * 4);
        // RGBA over the background, in place, to RGB.
        uint8_t br, bg, bb;
        kit_Background(background, br, bg, bb);
        uint8_t* out = rgba;
        for (int i = 0; i < w * h; i++){
            const uint8_t* p = rgba + i * 4;
            const unsigned a = p[3];
            const uint8_t r = (uint8_t)((p[0] * a + br * (255 - a)) / 255);
            const uint8_t g = (uint8_t)((p[1] * a + bg * (255 - a)) / 255);
            const uint8_t b = (uint8_t)((p[2] * a + bb * (255 - a)) / 255);
            out[i * 3] = r; out[i * 3 + 1] = g; out[i * 3 + 2] = b;
        }
        const int16_t x = box.x + (box.w - w) / 2, y = box.y + (box.h - h) / 2;
        kit_Blit(rgba, x, y, (int16_t)w, (int16_t)h);
        if (drawn) *drawn = {x, y, (int16_t)w, (int16_t)h};
        ok = true;
    }
    if (rast) nsvgDeleteRasterizer(rast);
    kit_Free(rgba);
    nsvgDelete(image);
    return ok;
}

bool mtb_Kit_Draw_Png(const uint8_t* png, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){
    if (png == nullptr || length == 0 || box.w <= 0 || box.h <= 0) return false;
    unsigned char* image = nullptr;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&image, &w, &h, png, length) != 0 || image == nullptr) return false;
    const int s = mtb_Kit_Fit_Scale((int)w, (int)h, box.w, box.h);
    const int outW = s > 0 ? (int)w * s : (int)w / -s, outH = s > 0 ? (int)h * s : (int)h / -s;
    uint8_t* rgb = (uint8_t*)kit_Alloc((size_t)outW * outH * 3);
    bool ok = false;
    if (rgb && outW > 0 && outH > 0){
        uint8_t br, bg, bb;
        kit_Background(background, br, bg, bb);
        for (int y = 0; y < outH; y++){
            for (int x = 0; x < outW; x++){
                unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
                if (s > 0){
                    const uint8_t* p = image + ((size_t)(y / s) * w + (x / s)) * 4;
                    r = p[0]; g = p[1]; b = p[2]; a = p[3]; n = 1;
                } else {
                    // Average the block (premultiplied by alpha).
                    const int d = -s;
                    for (int dy = 0; dy < d; dy++) for (int dx = 0; dx < d; dx++){
                        const uint8_t* p = image + ((size_t)(y * d + dy) * w + (x * d + dx)) * 4;
                        r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]; n++;
                    }
                    if (a){ r /= a; g /= a; b /= a; }
                    a /= n;
                    n = 1;
                }
                uint8_t* o = rgb + ((size_t)y * outW + x) * 3;
                o[0] = (uint8_t)((r * a + br * (255 - a)) / 255);
                o[1] = (uint8_t)((g * a + bg * (255 - a)) / 255);
                o[2] = (uint8_t)((b * a + bb * (255 - a)) / 255);
            }
        }
        const int16_t x = box.x + (box.w - outW) / 2, y = box.y + (box.h - outH) / 2;
        kit_Blit(rgb, x, y, (int16_t)outW, (int16_t)outH);
        if (drawn) *drawn = {x, y, (int16_t)outW, (int16_t)outH};
        ok = true;
    }
    kit_Free(rgb);
    heap_caps_free(image);     // lodepng's buffer: PSRAM on the device (lodepng_psram.cpp).
    return ok;
}

bool mtb_Kit_Draw_Jpg(const uint8_t* jpg, size_t length, Mtb_Kit_Box_t box, Mtb_Kit_Box_t* drawn){
    if (jpg == nullptr || length < 4 || box.w <= 0 || box.h <= 0) return false;
    uint8_t* image = nullptr;
    uint16_t w = 0, h = 0, stride = 0;
    if (mtb_Decode_Jpg_RGB888(jpg, length, &image, &w, &h, &stride) != 0 || image == nullptr) return false;
    const int s = mtb_Kit_Fit_Scale((int)w, (int)h, box.w, box.h);
    const int outW = s > 0 ? (int)w * s : (int)w / -s, outH = s > 0 ? (int)h * s : (int)h / -s;
    uint8_t* rgb = (uint8_t*)kit_Alloc((size_t)outW * outH * 3);
    bool ok = false;
    if (rgb && outW > 0 && outH > 0){
        for (int y = 0; y < outH; y++){
            for (int x = 0; x < outW; x++){
                unsigned r = 0, g = 0, b = 0, n = 0;
                if (s > 0){
                    const uint8_t* p = image + ((size_t)(y / s) * stride + (x / s)) * 3;
                    r = p[0]; g = p[1]; b = p[2]; n = 1;
                } else {
                    const int d = -s;
                    for (int dy = 0; dy < d; dy++) for (int dx = 0; dx < d; dx++){
                        const uint8_t* p = image + ((size_t)(y * d + dy) * stride + (x * d + dx)) * 3;
                        r += p[0]; g += p[1]; b += p[2]; n++;
                    }
                }
                uint8_t* o = rgb + ((size_t)y * outW + x) * 3;
                o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n);
            }
        }
        const int16_t x = box.x + (box.w - outW) / 2, y = box.y + (box.h - outH) / 2;
        kit_Blit(rgb, x, y, (int16_t)outW, (int16_t)outH);
        if (drawn) *drawn = {x, y, (int16_t)outW, (int16_t)outH};
        ok = true;
    }
    kit_Free(rgb);
    heap_caps_free(image);
    return ok;
}

bool mtb_Kit_Draw_Image(const uint8_t* d, size_t n, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){
    if (d == nullptr || n < 8) return false;
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return mtb_Kit_Draw_Png(d, n, box, background, drawn);
    if (d[0] == 0xFF && d[1] == 0xD8) return mtb_Kit_Draw_Jpg(d, n, box, drawn);
    // SVG: text with "<svg" near the start.
    const size_t look = n < 512 ? n : 512;
    for (size_t i = 0; i + 4 <= look; i++) if (memcmp(d + i, "<svg", 4) == 0) return mtb_Kit_Draw_Svg((const char*)d, n, box, background, drawn);
    return false;
}
