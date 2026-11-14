/**
 * @file pixlpal.h
 * @brief The Pixlpal App API for WebAssembly apps, draft version 0.2. An app includes this file only; it reaches
 *        Pixlpal through nothing else.
 *
 * The screen is 256 x 128 pixels. Colours are RGB565 (PXL_RGB(r, g, b) makes one). Coordinates outside the screen
 * are clipped.
 *
 * What Pixlpal calls in the app (export the ones you use with PXL_EXPORT):
 *   void pxl_start(void)                       once, after loading. Set the phone page here (pxl_phone_ui and
 *                                              pxl_phone_values): Pixlpal may start an app only to get its page for
 *                                              the phone, and close it again. Don't draw here.
 *   void pxl_frame(uint32_t ms, uint32_t dt)   every 20 ms: ms since start-up, dt since the last frame. Draw the whole
 *                                              screen here; it is shown when pxl_frame returns.
 *   void pxl_input(int kind, int a, int b)     the knob, a USB keyboard or mouse (PXL_IN_...)
 *   int  pxl_command(int command, int length)  a command from the app's phone page: its JSON (length bytes) is in
 *                                              the buffer pxl_inbox() returns. Return 0 if it was used.
 *   char* pxl_inbox(void)                      a PXL_INBOX_SIZE byte buffer of the app's, for pxl_command
 *   void pxl_stop(void)                        before the app closes: save what needs saving
 *
 * Each call into the app may run about 20 million WebAssembly instructions (roughly a second on Pixlpal). One that
 * runs longer, such as an endless loop, is stopped: Pixlpal closes the app and shows why.
 *
 * There is no C library: no printf, malloc, strlen or memcpy. Write the small helpers you need (see the examples).
 *
 * Build and pack (the WASI SDK, github.com/WebAssembly/wasi-sdk/releases):
 *   python3 tools/build.py path/to/app_folder        the folder holds app.json and the app's .c files
 */
#ifndef PIXLPAL_H
#define PIXLPAL_H

#include <stdint.h>

#define PXL_W 256
#define PXL_H 128

#define PXL_IMPORT(name) __attribute__((import_module("pixlpal"), import_name(name)))
#define PXL_EXPORT(name) __attribute__((export_name(name)))

#define PXL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define PXL_BLACK  0x0000
#define PXL_WHITE  0xFFFF

/* Fonts and alignment for pxl_text. */
enum { PXL_FONT_SMALL = 0 /* 4x6 */, PXL_FONT_NORMAL = 1 /* 6x8 */, PXL_FONT_LARGE = 2 /* 8x12 */ };
enum { PXL_LEFT = 0, PXL_CENTRE = 1, PXL_RIGHT = 2 };

/* pxl_input kinds. */
enum {
    PXL_IN_TURN = 1,        /* the knob: a = +1 clockwise, -1 back */
    PXL_IN_CLICK = 2,       /* the knob's button, a short press */
    PXL_IN_HOLD = 3,        /* the knob's button, held */
    PXL_IN_KEY_DOWN = 4,    /* a USB keyboard: a = the HID key code, b = the character (0 if none) */
    PXL_IN_KEY_UP = 5,      /* a = the HID key code */
    PXL_IN_MOUSE_MOVE = 6,  /* a = dx, b = dy */
    PXL_IN_MOUSE_WHEEL = 7, /* a = + away from the user */
    PXL_IN_MOUSE_BUTTONS = 8 /* a: bit 0 left, 1 right, 2 middle */
};

#define PXL_INBOX_SIZE 512
#define PXL_SAVE_MAX 1024

/* Drawing. */
PXL_IMPORT("fill")      void pxl_fill(int color);
PXL_IMPORT("rect")      void pxl_rect(int x, int y, int w, int h, int color);           /* filled */
PXL_IMPORT("frame")     void pxl_frame_rect(int x, int y, int w, int h, int color);     /* outline */
PXL_IMPORT("pixel")     void pxl_pixel(int x, int y, int color);
PXL_IMPORT("circle")    void pxl_circle(int x, int y, int r, int color);                /* filled */
PXL_IMPORT("triangle")  void pxl_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color);
/** Text (at most 64 characters) on a background colour. x is the left edge, the centre or the right edge.
 *  Returns the text's left edge. */
PXL_IMPORT("text")      int  pxl_text(int x, int y, int font, int align, int color, int background, const char* text);
PXL_IMPORT("text_width") int pxl_text_width(int font, const char* text);

/* Time, chance. */
PXL_IMPORT("millis")    uint32_t pxl_millis(void);
PXL_IMPORT("random")    uint32_t pxl_random(void);

/* Sound: notes are pairs {frequency Hz (0: a rest), milliseconds}, at most 40. Nothing plays in silent mode. */
typedef struct { int32_t hz, ms; } pxl_note_t;
PXL_IMPORT("sound")     void pxl_sound_raw(const void* notes, int bytes);
PXL_IMPORT("sound_stop") void pxl_sound_stop(void);
static inline void pxl_sound(const pxl_note_t* notes, int count){ pxl_sound_raw(notes, count * (int)sizeof(pxl_note_t)); }

/* The app's saved data: one block of up to PXL_SAVE_MAX bytes, kept across restarts and updates, and removed with
 * the app. pxl_save_read fills data only if a block of exactly that many bytes was saved: it returns bytes, or 0
 * (nothing saved yet, or saved with another size: keep your defaults). pxl_save_write returns bytes, or 0 if it
 * failed. Saving writes flash: save when something changes, or in pxl_stop, not every frame. */
PXL_IMPORT("save_read")  int pxl_save_read(void* data, int bytes);
PXL_IMPORT("save_write") int pxl_save_write(const void* data, int bytes);

/* The phone page. pxl_phone_ui (in pxl_start): the page's manifest and widgets, as JSON (the same format as the
 * built-in apps). pxl_phone_values: the values the page shows, as a JSON object; sent when it changes. */
PXL_IMPORT("phone_ui")     void pxl_phone_ui(const char* manifest, const char* widgets);
PXL_IMPORT("phone_values") void pxl_phone_values(const char* json);

/* Reading a command's JSON. */
PXL_IMPORT("json_int")  int pxl_json_int(const char* json, const char* key, int fallback);
/** Copies a string value (0-terminated, cut to fit). Returns its length, or -1 if there is none. */
PXL_IMPORT("json_str")  int pxl_json_str(const char* json, const char* key, char* out, int outSize);

/* A line in Pixlpal's log (the serial monitor). */
PXL_IMPORT("log")       void pxl_log(const char* text);

/* Colour helpers. */
static inline uint16_t pxl_blend(uint16_t a, uint16_t b, float t){
    if (t <= 0) return a;
    if (t >= 1) return b;
    const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (int)((br - ar) * t + ((br - ar) * t >= 0 ? 0.5f : -0.5f));
    const int g = ag + (int)((bg - ag) * t + ((bg - ag) * t >= 0 ? 0.5f : -0.5f));
    const int bl = ab + (int)((bb - ab) * t + ((bb - ab) * t >= 0 ? 0.5f : -0.5f));
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
static inline uint16_t pxl_dim(uint16_t c, uint8_t level){
    const int r = ((c >> 11) & 31) * level / 255, g = ((c >> 5) & 63) * level / 255, b = (c & 31) * level / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

#endif
