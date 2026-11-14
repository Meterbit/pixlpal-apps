// Hello: a first personal app. Turn the knob to steer the ball; click to count. The count is saved.
#include "pixlpal.h"

static char inbox[PXL_INBOX_SIZE];
static int ballX = PXL_W / 2, speed = 1;
static int clicks = 0;

// No C library in an app, so a tiny number-to-text helper.
static void to_text(int n, char* out){
    char tmp[12]; int i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0){ tmp[i++] = (char)('0' + n % 10); n /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

PXL_EXPORT("pxl_inbox") char* pxl_inbox(void){ return inbox; }

PXL_EXPORT("pxl_start") void pxl_start(void){
    pxl_save_read(&clicks, sizeof(clicks));   // 0 if nothing is saved yet
    pxl_log("Hello started");
}

PXL_EXPORT("pxl_input") void pxl_input(int kind, int a, int b){
    (void)b;
    if (kind == PXL_IN_TURN){ speed += a; if (speed > 5) speed = 5; if (speed < -5) speed = -5; }
    if (kind == PXL_IN_CLICK){ clicks++; pxl_save_write(&clicks, sizeof(clicks)); }
}

PXL_EXPORT("pxl_frame") void pxl_frame(uint32_t ms, uint32_t dt){
    (void)ms; (void)dt;
    ballX += speed;
    if (ballX < 0) ballX = PXL_W - 1;
    if (ballX >= PXL_W) ballX = 0;

    pxl_fill(PXL_BLACK);
    pxl_text(PXL_W / 2, 10, PXL_FONT_LARGE, PXL_CENTRE, PXL_WHITE, PXL_BLACK, "Hello, Pixlpal!");
    pxl_text(PXL_W / 2, 30, PXL_FONT_SMALL, PXL_CENTRE, PXL_RGB(120, 120, 120), PXL_BLACK,
             "Turn: steer   Click: count");
    pxl_circle(ballX, 70, 8, PXL_RGB(255, 160, 0));

    char num[12], line[32] = "Clicks: ";
    to_text(clicks, num);
    int i = 8; for (int j = 0; num[j]; j++) line[i++] = num[j]; line[i] = 0;
    pxl_text(PXL_W / 2, 105, PXL_FONT_NORMAL, PXL_CENTRE, PXL_RGB(0, 200, 255), PXL_BLACK, line);
}

PXL_EXPORT("pxl_stop") void pxl_stop(void){ pxl_save_write(&clicks, sizeof(clicks)); }
