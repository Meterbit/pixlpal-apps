#include "fbl_Crest.h"
#include <string.h>

void fbl_Crest_Downscale(const uint8_t* rgba, unsigned width, unsigned height, uint16_t* rgb565, uint8_t* alpha){
    memset(rgb565, 0, sizeof(uint16_t) * FBL_CREST * FBL_CREST);
    memset(alpha, 0, FBL_CREST * FBL_CREST);
    if (rgba == nullptr || width == 0 || height == 0) return;
    // Fit the longer side, keep the aspect, centre.
    const unsigned side = width > height ? width : height;
    const unsigned outW = width * FBL_CREST / side, outH = height * FBL_CREST / side;
    const unsigned offX = (FBL_CREST - outW) / 2, offY = (FBL_CREST - outH) / 2;
    for (unsigned oy = 0; oy < outH; oy++){
        const unsigned y0 = oy * height / outH, y1 = (oy + 1) * height / outH;
        for (unsigned ox = 0; ox < outW; ox++){
            const unsigned x0 = ox * width / outW, x1 = (ox + 1) * width / outW;
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (unsigned y = y0; y < y1; y++){
                const uint8_t* p = rgba + ((size_t)y * width + x0) * 4;
                for (unsigned x = x0; x < x1; x++, p += 4){
                    r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3];
                    n++;
                }
            }
            if (n == 0) continue;
            const size_t at = (offY + oy) * FBL_CREST + offX + ox;
            alpha[at] = (uint8_t)(a / n);
            if (a == 0) continue;
            const uint32_t R = r / a, G = g / a, B = b / a;
            rgb565[at] = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
        }
    }
}
