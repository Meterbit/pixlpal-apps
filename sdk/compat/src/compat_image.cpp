// Pixlpal compat: pictures (mtb_kit_image.h, mtb_Draw_Local_*) and the GitHub file download (mtb_github.h).
// The picture kit's functions are weak: an app that carries its own copy of the kit (as World Clock does, from before
// API 0.5) keeps it.
#define WEAK __attribute__((weak))
#include "mtb_kit_image.h"
#include "mtb_graphics.h"
#include "mtb_github.h"
#include "mtb_kit_http.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>
#include "pixlpal.h"

static bool draw(const void* d, size_t n, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){
    if (!d || n == 0 || box.w <= 0 || box.h <= 0) return false;
    const int r = pxl_image(box.x, box.y, box.w, box.h, d, (int)n, background);
    if (!r) return false;
    if (drawn){
        const int16_t w = (int16_t)pxl_image_w(r), h = (int16_t)pxl_image_h(r);
        *drawn = {(int16_t)(box.x + (box.w - w) / 2), (int16_t)(box.y + (box.h - h) / 2), w, h};
    }
    return true;
}

WEAK bool mtb_Kit_Draw_Svg(const char* svg, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){ return draw(svg, length, box, background, drawn); }
WEAK bool mtb_Kit_Draw_Png(const uint8_t* png, size_t length, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){ return draw(png, length, box, background, drawn); }
WEAK bool mtb_Kit_Draw_Jpg(const uint8_t* jpg, size_t length, Mtb_Kit_Box_t box, Mtb_Kit_Box_t* drawn){ return draw(jpg, length, box, 0, drawn); }
WEAK bool mtb_Kit_Draw_Image(const uint8_t* d, size_t n, Mtb_Kit_Box_t box, uint16_t background, Mtb_Kit_Box_t* drawn){ return draw(d, n, box, background, drawn); }

WEAK int mtb_Kit_Fit_Scale(int w, int h, int boxW, int boxH){
    if (w <= 0 || h <= 0 || boxW <= 0 || boxH <= 0) return 1;
    if (w <= boxW && h <= boxH){
        const int sx = boxW / w, sy = boxH / h;
        return sx < sy ? sx : sy;
    }
    int d = 2;
    while (w / d > boxW || h / d > boxH) d++;
    return -d;
}

WEAK uint8_t* mtb_Kit_Decode_Image(const uint8_t* d, size_t n, int boxW, int boxH, int* w, int* h){
    if (boxW <= 0 || boxH <= 0 || boxW > 256 || boxH > 256) return nullptr;
    const size_t bytes = (size_t)boxW * boxH * 4;
    uint8_t* out = (uint8_t*)malloc(bytes);
    if (!out) return nullptr;
    const int r = pxl_image_pixels(d, (int)n, boxW, boxH, out, (int)bytes);
    if (!r){ free(out); return nullptr; }
    if (w) *w = pxl_image_w(r);
    if (h) *h = pxl_image_h(r);
    return out;
}

WEAK bool mtb_Kit_Image_Size(const uint8_t* d, size_t n, int* w, int* h){
    const int r = pxl_image_size(d, (int)n);
    if (!r) return false;
    if (w) *w = pxl_image_w(r);
    if (h) *h = pxl_image_h(r);
    return true;
}

// A picture file drawn at (xAxis, yAxis) at its own size (scale 2, 3...: enlarged). 0 when drawn, as the firmware's.
static BaseType_t draw_File(const Mtb_LocalImage_t& image){
    File f = LittleFS.open(image.imagePath, "r");
    if (!f) return 1;
    const size_t n = f.size();
    uint8_t* d = n ? (uint8_t*)malloc(n) : nullptr;
    const bool read = d && f.read(d, n) == n;
    f.close();
    int w = 0, h = 0;
    const int s = image.scale > 1 ? image.scale : 1;
    const bool ok = read && mtb_Kit_Image_Size(d, n, &w, &h) &&
                    draw(d, n, {(int16_t)image.xAxis, (int16_t)image.yAxis, (int16_t)(w * s), (int16_t)(h * s)}, 0, nullptr);
    free(d);
    return ok ? 0 : 1;
}
BaseType_t mtb_Draw_Local_Png(const Mtb_LocalImage_t& image){ return draw_File(image); }
BaseType_t mtb_Draw_Local_Svg(const Mtb_LocalImage_t& image){ return draw_File(image); }
BaseType_t mtb_Draw_Local_Jpg(const Mtb_LocalImage_t& image){ return draw_File(image); }

// raw.githubusercontent.com (app.json's "net" lists it): public files, no token.
static char* github_Get(const String& path, const String& owner, const String& repo, size_t& length){
    char url[256];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/%s/HEAD/%s", owner.c_str(), repo.c_str(), path.c_str());
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 60 * 1024, r, "Pixlpal");
    if (!body || r.code != 200 || r.truncated || r.length == 0){ free(body); return nullptr; }
    length = r.length;
    return body;
}

bool mtb_Download_Github_File_To_SPIFFS(const String& bucketPath, const String& flashPath, const String owner, const String repo, const String){
    size_t n = 0;
    char* body = github_Get(bucketPath, owner, repo, n);
    if (!body) return false;
    File f = LittleFS.open(flashPath.c_str(), "w");
    const bool ok = f && f.write((const uint8_t*)body, n) == n;
    f.close();
    free(body);
    if (!ok) LittleFS.remove(flashPath.c_str());
    return ok;
}

bool mtb_Download_Github_File_To_PSRAM(const String& bucketPath, uint8_t** outBuffer, size_t* outSize, const String owner, const String repo, const String){
    size_t n = 0;
    char* body = github_Get(bucketPath, owner, repo, n);
    if (!body) return false;
    *outBuffer = (uint8_t*)body;
    *outSize = n;
    return true;
}
