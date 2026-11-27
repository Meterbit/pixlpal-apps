// Pixlpal compat: the status bar's notice line (statusBarNotif), as Pixlpal's own (pxl_status_text). Mtb_ScrollText_t,
// a ticker of the app's own, is in mtb_scroll_text.h.
#pragma once
#include <stdint.h>
#include "pixlpal.h"
#include "WString.h"
struct Mtb_Status_Notice_t {
    void mtb_Scroll_This_Text(const char* text, uint16_t color = 0xFFFF){ pxl_status_text(text, color); }
    void mtb_Scroll_This_Text(const String& text, uint16_t color = 0xFFFF){ pxl_status_text(text.c_str(), color); }
};
extern Mtb_Status_Notice_t statusBarNotif;

#define CHECK_SCROLL  0
#define STOP_SCROLL   1
/** Pixlpal compat: a ticker of the app's own, scrolling text right to left across a strip of the screen, one pixel
 *  every `speed` ms, `pass` times. In an app whose "api" is 0.7 or newer, Pixlpal scrolls it (pxl_ticker), evenly,
 *  whatever the app is doing, as the firmware's apps' tickers are. Otherwise (or with a font Pixlpal hasn't, or none
 *  of its scrolling free) it is drawn here, at the app's waits, so it moves only as often as the app waits. */
class Mtb_ScrollText_t {
public:
    uint16_t xPos, yPos, width, height, color, backgroundColor = 0, speed, pass;
    const uint8_t* font;
    Mtb_ScrollText_t(uint16_t x, uint16_t y, uint16_t w, const uint8_t* f, uint16_t c = 0xFFFF, uint16_t sp = 20, uint16_t passes = 1);
    ~Mtb_ScrollText_t();
    void mtb_Scroll_This_Text(const char* text);
    void mtb_Scroll_This_Text(const char* text, uint16_t c);
    void mtb_Scroll_This_Text(const String& text, uint16_t c){ mtb_Scroll_This_Text(text.c_str(), c); }
    bool mtb_Scroll_Active(uint8_t check_or_stop = CHECK_SCROLL);
    void step();
private:
    uint8_t* bits_ = nullptr;
    int stride_ = 0, length_ = 0, offset_ = 0, left_ = 0;
    uint32_t lastMs_ = 0;
    uint16_t now_ = 0xFFFF;
#if defined(PXL_APP_API) && PXL_APP_API >= 7
    bool host_ = false;               // Pixlpal scrolls it (pxl_ticker), not this
#endif
};
