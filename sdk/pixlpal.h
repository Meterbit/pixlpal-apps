/**
 * @file pixlpal.h
 * @brief The Pixlpal App API for WebAssembly apps, draft version 0.4. An app includes this file only; it reaches
 *        Pixlpal through nothing else. Set "api" in app.json to the version whose additions it uses (marked "0.3",
 *        "0.4" below); apps written for an older version run unchanged.
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
 *   void pxl_http_done(int id, int status, int length)   (0.3) an internet request's answer is in its buffer
 *   void pxl_main(void)                        (0.4) instead of pxl_frame: the app as one loop that calls
 *                                              pxl_yield(ms) to wait (its "delay"), and returns when pxl_yield
 *                                              returns 0. Input and commands arrive inside pxl_yield.
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

#ifdef __cplusplus
extern "C" {
#endif

#define PXL_W 256
#define PXL_H 128

#define PXL_IMPORT(name) __attribute__((import_module("pixlpal"), import_name(name)))
#define PXL_EXPORT(name) __attribute__((export_name(name)))

#define PXL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define PXL_BLACK  0x0000
#define PXL_WHITE  0xFFFF
#define PXL_CLEAR  (-1)        /* as a background: none, draw on what is there */

/* Fonts and alignment for pxl_text. */
enum { PXL_FONT_SMALL = 0 /* 4x6 */, PXL_FONT_NORMAL = 1 /* 6x8 */, PXL_FONT_LARGE = 2 /* 8x12 */ };
enum { PXL_LEFT = 0, PXL_CENTRE = 1, PXL_RIGHT = 2 };

/* pxl_input kinds. */
enum {
    PXL_IN_TURN = 1,        /* the knob: a = +1 clockwise, -1 back */
    PXL_IN_CLICK = 2,       /* the knob's button, a short press; a = how many in a row (1, or 2 for a double click) */
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
/** Text (at most 64 characters). x is the left edge, the centre or the right edge. background: a colour for the box
 *  behind the text, or PXL_CLEAR (-1) to draw on what is there. Returns the text's left edge. */
PXL_IMPORT("text")      int  pxl_text(int x, int y, int font, int align, int color, int background, const char* text);
PXL_IMPORT("text_width") int pxl_text_width(int font, const char* text);

/* More drawing (0.3). */
PXL_IMPORT("line")       void pxl_line(int x0, int y0, int x1, int y1, int color);
PXL_IMPORT("round_rect") void pxl_round_rect(int x, int y, int w, int h, int radius, int color);   /* filled */
/** A picture: w * h RGB565 pixels, row by row, at an even address (a uint16_t array). */
PXL_IMPORT("bitmap")     void pxl_bitmap_raw(int x, int y, int w, int h, const void* pixels, int bytes);
static inline void pxl_bitmap(int x, int y, int w, int h, const uint16_t* pixels){ pxl_bitmap_raw(x, y, w, h, pixels, w * h * 2); }
/** A one-colour picture: rows of (w + 7) / 8 bytes, bit 0 of each byte the leftmost pixel. background: a colour, or
 *  PXL_CLEAR to leave the screen as it is where a bit is 0. */
PXL_IMPORT("bitmap_mono") void pxl_bitmap_mono_raw(int x, int y, int w, int h, const void* bits, int bytes, int color, int background);
static inline void pxl_bitmap_mono(int x, int y, int w, int h, const uint8_t* bits, int color, int background){
    pxl_bitmap_mono_raw(x, y, w, h, bits, ((w + 7) / 8) * h, color, background);
}

/* Time, chance. */
PXL_IMPORT("millis")    uint32_t pxl_millis(void);
PXL_IMPORT("random")    uint32_t pxl_random(void);

/* The time of day (0.3), in the owner's time zone. valid is 0 until Pixlpal has had the time from the internet. */
typedef struct {
    int32_t year, month, day;             /* 2026, 1-12, 1-31 */
    int32_t hour, minute, second;         /* 0-23 */
    int32_t weekday, yearday;             /* 0 = Sunday; 0-365 */
    int32_t utc_offset_minutes;           /* the time zone, daylight saving included */
    uint32_t unix;                        /* seconds since 1970, UTC */
    int32_t valid;
} pxl_time_t;
PXL_IMPORT("time")      int pxl_time_raw(void* out, int bytes);
static inline int pxl_time(pxl_time_t* t){ return pxl_time_raw(t, (int)sizeof(pxl_time_t)); }   /* returns valid */
/** 1 if the owner chose a 24-hour clock, 0 for 12-hour. */
PXL_IMPORT("clock_24h") int pxl_clock_24h(void);

/* Sound: notes are pairs {frequency Hz (0: a rest), milliseconds}, at most 40. Nothing plays in silent mode. */
typedef struct { int32_t hz, ms; } pxl_note_t;
PXL_IMPORT("sound")     void pxl_sound_raw(const void* notes, int bytes);
PXL_IMPORT("sound_stop") void pxl_sound_stop(void);
static inline void pxl_sound(const pxl_note_t* notes, int count){ pxl_sound_raw(notes, count * (int)sizeof(pxl_note_t)); }
/** (0.3) Speaks text (at most 200 characters) in a language ("en", "fr", "es"...), through Google's text to speech:
 *  it needs the internet, and the text goes to Google. At most once every 2 s. Returns 1 if it started. */
PXL_IMPORT("say")       int pxl_say(const char* text, const char* language);
/** (0.3) 1 while Pixlpal is in Silent Mode (sounds and speech are not played then). */
PXL_IMPORT("silent")    int pxl_silent(void);

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

/* Reading JSON: a phone command, or an internet answer. key is a name, or (0.3) a path through objects and arrays:
 * "current.temperature_2m", "list.0.name". Each call reads the whole text, so read only the values you need. */
PXL_IMPORT("json_int")  int pxl_json_int(const char* json, const char* key, int fallback);
/** Copies a string value (0-terminated, cut to fit). Returns its length, or -1 if there is none. */
PXL_IMPORT("json_str")  int pxl_json_str(const char* json, const char* key, char* out, int outSize);
/** (0.3) A number with decimals. */
PXL_IMPORT("json_float") float pxl_json_float(const char* json, const char* key, float fallback);
/** (0.3) How many entries the array (or object) at key has, -1 if there is none. key "" is the whole document. */
PXL_IMPORT("json_count") int pxl_json_count(const char* json, const char* key);

/* Loops, other apps, the device (0.4). */
/** For an app written as a loop (pxl_main): shows the frame drawn since the last call, passes on input and phone
 *  commands (pxl_input, pxl_command and pxl_http_done run from in here), waits ms, and returns 1; or 0 when the app is
 *  closing, and pxl_main should return. Each loop round may run about 20 million instructions. */
PXL_IMPORT("yield")       int pxl_yield(int ms);
/** 1 while a sound (or speech) is playing. */
PXL_IMPORT("sound_busy")  int pxl_sound_busy(void);
/** Opens another app by its number ("19/8" is 19, 8); this one closes. */
PXL_IMPORT("open_app")    void pxl_open_app(int group, int number);
/** Back to the app that was open before this one (the Calendar Clock if none); this one closes. */
PXL_IMPORT("open_previous_app") void pxl_open_previous_app(void);
/** Bit 0: a USB keyboard is connected; bit 1: a USB mouse. */
PXL_IMPORT("usb_state")   int pxl_usb_state(void);
/** The panel's brightness, the owner's setting, 1-100 %. Setting it keeps it, as turning the knob does. */
PXL_IMPORT("brightness")  int pxl_brightness(void);
PXL_IMPORT("set_brightness") void pxl_set_brightness(int percent);
/** What the knob does in most apps: one step brighter (direction > 0) or dimmer. */
PXL_IMPORT("brightness_step") void pxl_brightness_step(int direction);

/* Files (0.4): the app's own, kept across restarts and updates, removed with the app. Names of letters, digits, '_',
 * '-' and '.' (not first), at most 31 characters. A file up to 64 KB; all of an app's files, 256 KB. A catalogue app
 * with "dataGroup" in its app.json also has its group's folder, shared with its developer's other apps in that group:
 * names starting "group/".
 * pxl_file_read returns the bytes read (at most bytes), or -1 if there is no such file. pxl_file_write replaces the
 * file and returns bytes, or -1 (too big, or no room). Writing wears the flash: write when something changes. */
PXL_IMPORT("file_read")   int pxl_file_read(const char* name, void* data, int bytes);
PXL_IMPORT("file_write")  int pxl_file_write(const char* name, const void* data, int bytes);
PXL_IMPORT("file_remove") int pxl_file_remove(const char* name);
/** The app's files: folder "" (its own) or "group/" (its group's), as lines of "name<TAB>bytes<TAB>time\n" (time: the
 *  last write, Unix seconds). Returns the bytes written to out, or -1. */
PXL_IMPORT("file_list")   int pxl_file_list(const char* folder, char* out, int size);

/** A searchPicker's results on the phone page: resultsJson is [{"name": "Shown", "value": ...}, ...]; error "" if
 *  none. searchId and query are the ones the search command carried. */
/** The phone page's datasets, which its list widgets name: {"datasets": {"name": [{"name": "Shown", "value": ...}]}}. */
PXL_IMPORT("phone_data")  void pxl_phone_data(const char* json);
/** A notice scrolled along Pixlpal's status bar (120 characters, once a second at most). */
PXL_IMPORT("status_text") void pxl_status_text(const char* text, int color);
/** A message of the app's own to its phone page (a JSON object, up to 4 KB), such as a file in pieces (247). */
PXL_IMPORT("phone_send")  void pxl_phone_send(const char* json);
PXL_IMPORT("phone_search") void pxl_phone_search(const char* searchId, const char* query, const char* resultsJson, const char* error);

/* The internet (0.3). An app reaches only the hosts its app.json lists in "net" (["api.example.com",
 * "*.example.org"]), over https. Owners see that list in the App Store, and Meterbit reviews it.
 *
 * pxl_http_get starts a request and returns at once: its number (1 and up), or a reason it didn't start (PXL_HTTP_...).
 * Pixlpal fetches the answer while the app keeps drawing, puts it in buffer, and then calls the app's
 * pxl_http_done(id, status, length). status is the HTTP status (200...), PXL_HTTP_FAILED (no answer) or
 * PXL_HTTP_TOO_BIG (the start of the answer is in buffer). If there is room, a 0 follows the answer, so text can be
 * read as a string. Keep buffer untouched until pxl_http_done. A buffer outside the app's memory stops the app, as
 * any bad pointer does.
 *
 * headers: "" or lines of "Name: value" (at most 4), such as "X-Api-Key: abc". One request at a time, at most 30 a
 * minute, answers of up to 64 KB, a POST body of up to 8 KB. An API key the owner typed on the phone page can go in a
 * header or the URL: it only ever reaches the hosts the app lists. */
enum { PXL_HTTP_BUSY = -1, PXL_HTTP_NOT_ALLOWED = -2, PXL_HTTP_BAD = -3, PXL_HTTP_OFFLINE = -4, PXL_HTTP_TOO_OFTEN = -5 };
enum { PXL_HTTP_FAILED = -1, PXL_HTTP_TOO_BIG = -2 };
PXL_IMPORT("http_request") int pxl_http_request_raw(int method, const char* url, const char* headers, const void* body,
                                                    int bodyBytes, void* buffer, int bufferSize);
static inline int pxl_http_get(const char* url, const char* headers, void* buffer, int bufferSize){
    return pxl_http_request_raw(0, url, headers, 0, 0, buffer, bufferSize);
}
static inline int pxl_http_post(const char* url, const char* headers, const void* body, int bodyBytes, void* buffer, int bufferSize){
    return pxl_http_request_raw(1, url, headers, body, bodyBytes, buffer, bufferSize);
}

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

#ifdef __cplusplus
}
#endif

#endif
