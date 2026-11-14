// Pixlpal compat: the firmware's app framework on the Pixlpal App API (see include/mtb_engine.h).
//
// The module holds one app, registered with MTB_REGISTER_APP. pxl_start makes its descriptor (which sets its phone
// page), pxl_main runs its task function, whose loop's delay() is pxl_yield. The knob, USB input and phone commands
// arrive (inside pxl_yield) as the callbacks the app registered.
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_usb_hid.h"
#include "snd_Sound.h"
#include <ctype.h>

// The framework's globals --------------------------------------------------------------------------------------------
Mtb_App_Registry_Entry* mtb_App_Registry_Head = nullptr;
Mtb_Applications* Mtb_Applications::currentRunningApp = nullptr;
Mtb_Applications* Mtb_Applications::previousRunningApp = nullptr;
bool Mtb_Applications::internetConnectStatus = true;
static Mtb_Services sntpService;
Mtb_Services* mtb_Sntp_Time_Sv = &sntpService;
uint16_t panelBrightness = 128;

static Mtb_Applications* theApp = nullptr;
static bool active = true;            // false once pxl_yield said the app is closing
static uint32_t lastYieldMs = 0;
static bool commandRejected = false;

// Time and the loop ------------------------------------------------------------------------------------------------------
extern "C" uint32_t pxl_compat_millis(void){ return pxl_millis(); }

static void (*background[8])(void);
static bool inBackground = false;
struct Pending_Task_t { void (*fn)(void*); void* parameter; };
static Pending_Task_t tasks[8];
static int taskCount = 0;
extern "C" void pxl_compat_add_background(void (*step)(void)){
    for (auto& b : background) if (b == step) return;
    for (auto& b : background) if (!b){ b = step; return; }
}
extern "C" void pxl_compat_remove_background(void (*step)(void)){
    for (auto& b : background) if (b == step) b = nullptr;
}

extern "C" void pxl_compat_delay(uint32_t ms){
    if (!active){ pxl_yield((int)ms); return; }            // closing: Pixlpal still lets a short wait pass, a while
    active = pxl_yield((int)ms) != 0;
    lastYieldMs = pxl_millis();
    // The workers' steps and the tasks the app started: here, between the app's frames, one at a time (a step or task
    // may itself wait, which comes back here without starting another).
    if (active && !inBackground){
        inBackground = true;
        for (auto& b : background) if (b && active) b();
        while (active && taskCount > 0){
            const Pending_Task_t t = tasks[0];
            taskCount--;
            memmove(tasks, tasks + 1, sizeof(tasks[0]) * (size_t)taskCount);
            t.fn(t.parameter);
        }
        inBackground = false;
    }
}

BaseType_t xTaskCreatePinnedToCore(void (*task)(void*), const char*, uint32_t, void* parameter, UBaseType_t, TaskHandle_t* handle, BaseType_t){
    if (handle) *handle = nullptr;
    if (!task || taskCount >= 8) return pdFAIL;
    tasks[taskCount].fn = task;
    tasks[taskCount].parameter = parameter;
    taskCount++;
    if (handle) *handle = (TaskHandle_t)task;
    return pdPASS;
}

// THIS_APP_IS_ACTIVE: a loop that checks it often enough but forgets to wait still lets Pixlpal show its frames and
// pass input on (and gets a fresh instruction budget).
uint8_t mtb_App_Checkpoint(Mtb_Applications*){
    if (active && pxl_millis() - lastYieldMs > 50) pxl_compat_delay(0);
    return active ? pdTRUE : pdFALSE;
}

void mtb_Delete_This_App(Mtb_Applications*){ active = false; }

struct tm* pxl_compat_localtime_r(const time_t* t, struct tm* out){
    pxl_time_t now;
    pxl_time(&now);
    const time_t local = *t + (time_t)now.utc_offset_minutes * 60;
    return gmtime_r(&local, out);
}
struct tm* pxl_compat_localtime(const time_t* t){
    static struct tm result;
    return pxl_compat_localtime_r(t, &result);
}

// Apps and services ------------------------------------------------------------------------------------------------------
Mtb_Applications* mtb_Launch_This_App(Mtb_Applications* app, Mtb_Action_On_App_t, Mtb_Do_Prev_App_t){
    if (app && app != theApp) pxl_open_previous_app();      // the only other descriptor an app has: the one before
    return app;
}
void mtb_Identify_App_By_ID(Mtb_User_AppID_t id, Mtb_Action_On_App_t action){
    if (action == LAUNCH_PXP_APP) pxl_open_app(id.GenApp, id.SpeApp);
}

void buttonDoNothing(button_event_t){}
void encoderDoNothing(rotary_encoder_rotation_t){}
void mtb_Brightness_Control(rotary_encoder_rotation_t d){
    pxl_brightness_step(d == ROT_CLOCKWISE ? 1 : -1);
    panelBrightness = (uint16_t)(pxl_brightness() * 255 / 100);
}
void mtb_Panel_Set_Brightness(uint8_t level){
    pxl_set_brightness((level * 100 + 127) / 255);
    panelBrightness = level;
}

// The phone ---------------------------------------------------------------------------------------------------------------
void Mtb_Applications::mtb_App_Set_Mobile_UI(const char* manifest, const char* api, const char* data, mtb_Mobile_UI_Values_Fn_t valuesFn){
    uiManifest = manifest;
    uiApi = api;
    uiData = data;
    uiValues = valuesFn;
}

static void send_Page(Mtb_Applications* a){
    if (a && a->uiManifest && a->uiApi) pxl_phone_ui(a->uiManifest, a->uiApi);
    if (a && a->uiData) pxl_phone_data(a->uiData);
}

void Mtb_Applications::mtb_App_Push_Mobile_Values(bool){
    if (!uiValues) return;
    JsonDocument doc;
    uiValues(doc.to<JsonObject>());
    const size_t n = measureJson(doc);
    char* text = (char*)malloc(n + 1);
    if (!text) return;
    serializeJson(doc, text, n + 1);
    pxl_phone_values(text);
    free(text);
}

void Mtb_Applications::mtb_App_Init(){
    send_Page(this);
    mtb_App_Push_Mobile_Values(true);
}

void Mtb_Applications::mtb_App_Send_Search_Results(const char* searchId, const char* query, JsonArrayConst results, const char* error){
    const size_t n = measureJson(results);
    char* text = (char*)malloc(n + 1);
    if (!text) return;
    serializeJson(results, text, n + 1);
    pxl_phone_search(searchId ? searchId : "", query ? query : "", text, error ? error : "");
    free(text);
}

void mtb_Ble_App_Cmd_Reject(void){ commandRejected = true; }

// Settings: a file per key ("nvs.<key>", the key's odd characters made '_'). The keys app.json lists in "sharedNvs"
// are in the folder of its "dataGroup" (the developer's apps share them: the kids' profiles), "group/nvs.<key>".
#ifndef PXL_COMPAT_SHARED_NVS
#define PXL_COMPAT_SHARED_NVS ""
#endif
static bool shared_Key(const char* key){
    const char* list = PXL_COMPAT_SHARED_NVS;
    const size_t n = strlen(key);
    for (const char* p = list; *p;){
        const char* e = strchr(p, ',');
        const size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && strncmp(p, key, n) == 0) return true;
        p = e ? e + 1 : p + l;
    }
    return false;
}
static void nvs_Name(const char* key, char* out, size_t size){
    if (!key) key = "";
    const bool shared = shared_Key(key);
    snprintf(out, size, "%snvs.%.22s", shared ? "group/" : "", key);
    for (char* c = out + (shared ? 10 : 4); *c; c++) if (!(isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.')) *c = '_';
}

esp_err_t mtb_Read_Nvs_Struct_If_Saved(const char* key, void* data, size_t size){
    char name[40];
    nvs_Name(key, name, sizeof(name));
    uint8_t* tmp = (uint8_t*)malloc(size + 1);
    if (!tmp) return ESP_FAIL;
    const int n = pxl_file_read(name, tmp, (int)size + 1);
    const bool ok = n == (int)size;               // saved with another size: not this struct
    if (ok) memcpy(data, tmp, size);
    free(tmp);
    return ok ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t mtb_Read_Nvs_Struct(const char* key, void* data, size_t size){
    if (mtb_Read_Nvs_Struct_If_Saved(key, data, size) == ESP_OK) return ESP_OK;
    return mtb_Write_Nvs_Struct(key, data, size);   // as the firmware: missing, so the defaults are stored
}

esp_err_t mtb_Write_Nvs_Struct(const char* key, void* data, size_t size){
    char name[40];
    nvs_Name(key, name, sizeof(name));
    return pxl_file_write(name, data, (int)size) == (int)size ? ESP_OK : ESP_FAIL;
}

esp_err_t mtb_Erase_Nvs_Key(const char* key){
    char name[40];
    nvs_Name(key, name, sizeof(name));
    pxl_file_remove(name);
    return ESP_OK;
}

// Sound ---------------------------------------------------------------------------------------------------------------------
bool snd_Play(const Snd_Note_t* notes, int count, float, bool){
    if (!notes || count <= 0) return false;
    pxl_note_t n[SND_NOTES_MAX];
    if (count > SND_NOTES_MAX) count = SND_NOTES_MAX;
    for (int i = 0; i < count; i++){ n[i].hz = (int32_t)(notes[i].f + 0.5f); n[i].ms = notes[i].ms; }
    pxl_sound(n, count);
    return !pxl_silent();
}
bool snd_Say(const char* text, const char* language, const Snd_Note_t* notes, int count, float level, bool square){
    if (notes && count > 0) snd_Play(notes, count, level, square);
    return text && text[0] && pxl_say(text, language ? language : "en");
}
void snd_Stop(void){ pxl_sound_stop(); }
bool snd_Busy(void){ return pxl_sound_busy() != 0; }

// A USB keyboard and mouse ----------------------------------------------------------------------------------------------
static Mtb_Usb_Input_t usbQueue[32];
static int usbHead = 0, usbCount = 0;
bool mtb_Usb_Input_Open(bool){ usbHead = usbCount = 0; return true; }
void mtb_Usb_Input_Close(void){ usbHead = usbCount = 0; }
bool mtb_Usb_Input_Read(Mtb_Usb_Input_t& out, uint32_t timeoutMs){
    const uint32_t start = pxl_millis();
    while (usbCount == 0 && timeoutMs > 0 && active && pxl_millis() - start < timeoutMs) pxl_compat_delay(5);
    if (usbCount == 0) return false;
    out = usbQueue[usbHead];
    usbHead = (usbHead + 1) % 32;
    usbCount--;
    return true;
}
bool mtb_Usb_Keyboard_Connected(void){ return (pxl_usb_state() & 1) != 0; }
bool mtb_Usb_Mouse_Connected(void){ return (pxl_usb_state() & 2) != 0; }
static void usb_Push(const Mtb_Usb_Input_t& in){
    if (usbCount == 32){ usbHead = (usbHead + 1) % 32; usbCount--; }     // the oldest goes
    usbQueue[(usbHead + usbCount) % 32] = in;
    usbCount++;
}

// The panel ----------------------------------------------------------------------------------------------------------------
static inline int lo(int a, int b){ return a < b ? a : b; }
static inline int hi(int a, int b){ return a > b ? a : b; }
void mtb_Panel_Fill_Screen(uint16_t c){ pxl_fill(c); }
void mtb_Panel_Draw_PixelRGB565(int16_t x, int16_t y, uint16_t c){ pxl_pixel(x, y, c); }
void mtb_Panel_Draw_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c){ pxl_line(x1, y1, x2, y2, c); }
void mtb_Panel_Draw_HLine(int16_t y, int16_t x1, int16_t x2, uint16_t c){ pxl_rect(lo(x1, x2), y, hi(x1, x2) - lo(x1, x2) + 1, 1, c); }
void mtb_Panel_Draw_VLine(int16_t x, int16_t y1, int16_t y2, uint16_t c){ pxl_rect(x, lo(y1, y2), 1, hi(y1, y2) - lo(y1, y2) + 1, c); }
void mtb_Panel_Draw_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c){
    pxl_frame_rect(lo(x1, x2), lo(y1, y2), hi(x1, x2) - lo(x1, x2) + 1, hi(y1, y2) - lo(y1, y2) + 1, c);
}
void mtb_Panel_Fill_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c){
    pxl_rect(lo(x1, x2), lo(y1, y2), hi(x1, x2) - lo(x1, x2) + 1, hi(y1, y2) - lo(y1, y2) + 1, c);
}
void mtb_Panel_Fill_Circle(int16_t x0, int16_t y0, int16_t r, uint16_t c){ pxl_circle(x0, y0, r, c); }
// An outline circle (midpoint): one call per row pair, as horizontal runs of the circle's edge.
void mtb_Panel_Draw_Circle(int16_t x0, int16_t y0, int16_t r, uint16_t c){
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y){
        pxl_pixel(x0 + x, y0 + y, c); pxl_pixel(x0 - x, y0 + y, c); pxl_pixel(x0 + x, y0 - y, c); pxl_pixel(x0 - x, y0 - y, c);
        pxl_pixel(x0 + y, y0 + x, c); pxl_pixel(x0 - y, y0 + x, c); pxl_pixel(x0 + y, y0 - x, c); pxl_pixel(x0 - y, y0 - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
void mtb_Panel_Draw_Triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c){
    pxl_line(x0, y0, x1, y1, c); pxl_line(x1, y1, x2, y2, c); pxl_line(x2, y2, x0, y0, c);
}
void mtb_Panel_Fill_Triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c){ pxl_triangle(x0, y0, x1, y1, x2, y2, c); }
void mtb_Panel_Fill_Round_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t r, uint16_t c){
    pxl_round_rect(lo(x1, x2), lo(y1, y2), hi(x1, x2) - lo(x1, x2) + 1, hi(y1, y2) - lo(y1, y2) + 1, r, c);
}
void mtb_Panel_Draw_Round_Rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t r, uint16_t c){
    const int l = lo(x1, x2), t = lo(y1, y2), rr = hi(x1, x2), b = hi(y1, y2);
    if (r * 2 > rr - l) r = (rr - l) / 2;
    if (r * 2 > b - t) r = (b - t) / 2;
    if (r < 0) r = 0;
    mtb_Panel_Draw_HLine(t, l + r, rr - r, c);
    mtb_Panel_Draw_HLine(b, l + r, rr - r, c);
    mtb_Panel_Draw_VLine(l, t + r, b - r, c);
    mtb_Panel_Draw_VLine(rr, t + r, b - r, c);
    int x = r, y = 0, err = 1 - r;
    while (x >= y){
        pxl_pixel(rr - r + x, b - r + y, c); pxl_pixel(rr - r + y, b - r + x, c);
        pxl_pixel(l + r - x, b - r + y, c); pxl_pixel(l + r - y, b - r + x, c);
        pxl_pixel(rr - r + x, t + r - y, c); pxl_pixel(rr - r + y, t + r - x, c);
        pxl_pixel(l + r - x, t + r - y, c); pxl_pixel(l + r - y, t + r - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
void mtb_Panel_Draw_FrameRGB565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* data){
    if (!data || w <= 0 || h <= 0) return;
    if (((uintptr_t)data & 1) == 0){ pxl_bitmap(x, y, w, h, data); return; }
    uint16_t* row = (uint16_t*)malloc((size_t)w * 2);                // pxl_bitmap needs an even address
    if (!row) return;
    for (int j = 0; j < h; j++){ memcpy(row, (const uint8_t*)data + (size_t)j * w * 2, (size_t)w * 2); pxl_bitmap(x, y + j, w, 1, row); }
    free(row);
}
void mtb_Panel_Draw_FrameRGB888(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* data){
    if (!data || w <= 0 || h <= 0) return;
    uint16_t* row = (uint16_t*)malloc((size_t)w * 2);
    if (!row) return;
    for (int j = 0; j < h; j++){
        const uint8_t* p = data + (size_t)j * w * 3;
        for (int i = 0; i < w; i++) row[i] = (uint16_t)(((p[i * 3] & 0xF8) << 8) | ((p[i * 3 + 1] & 0xFC) << 3) | (p[i * 3 + 2] >> 3));
        pxl_bitmap(x, y + j, w, 1, row);
    }
    free(row);
}
// One-colour pictures: pxl_bitmap_mono takes rows of (w + 7) / 8 bytes from bit 0; anything else is repacked.
static void mono(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits, uint32_t stride, uint32_t srcX, uint16_t color, int background){
    if (!bits || w <= 0 || h <= 0) return;
    const uint32_t need = ((uint32_t)w + 7) / 8;
    if (srcX == 0 && stride == need){ pxl_bitmap_mono(x, y, w, h, bits, color, background); return; }
    uint8_t* packed = (uint8_t*)calloc((size_t)need * h, 1);
    if (!packed) return;
    for (int r = 0; r < h; r++)
        for (int i = 0; i < w; i++){
            const uint32_t sx = srcX + (uint32_t)i;
            if ((bits[r * stride + (sx >> 3)] >> (sx & 7)) & 1) packed[r * need + (i >> 3)] |= (uint8_t)(1u << (i & 7));
        }
    pxl_bitmap_mono(x, y, w, h, packed, color, background);
    free(packed);
}
void mtb_Panel_Draw_Mono(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits, uint32_t stride, uint32_t srcX, uint16_t color, uint16_t bg){ mono(x, y, w, h, bits, stride, srcX, color, bg); }
void mtb_Panel_Draw_Mono_Fg(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits, uint32_t stride, uint32_t srcX, uint16_t color){ mono(x, y, w, h, bits, stride, srcX, color, PXL_CLEAR); }

// What Pixlpal calls ------------------------------------------------------------------------------------------------------
static char inbox[PXL_INBOX_SIZE];
PXL_EXPORT("pxl_inbox") char* pxl_inbox(void){ return inbox; }

// The app's descriptor (and so its phone page), even when Pixlpal starts it only to get the page.
PXL_EXPORT("pxl_start") void pxl_start(void){
    panelBrightness = (uint16_t)(pxl_brightness() * 255 / 100);
    for (Mtb_App_Registry_Entry* e = mtb_App_Registry_Head; e && !theApp; e = e->next) theApp = e->getInstance();
    Mtb_Applications::currentRunningApp = theApp;
    if (theApp){ send_Page(theApp); theApp->mtb_App_Push_Mobile_Values(true); }
}

PXL_EXPORT("pxl_main") void pxl_main(void){
    if (!theApp || !theApp->appTask) return;
    active = true;
    lastYieldMs = pxl_millis();
    theApp->appTask(theApp);
}

PXL_EXPORT("pxl_input") void pxl_input(int kind, int a, int b){
    if (!theApp) return;
    if (kind == PXL_IN_TURN){ if (theApp->encoderFn) theApp->encoderFn(a > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE); }
    else if (kind == PXL_IN_CLICK || kind == PXL_IN_HOLD){
        button_event_t e = {};
        e.type = kind == PXL_IN_CLICK ? BUTTON_CLICKED : BUTTON_PRESSED_LONG;
        e.count = (uint8_t)(kind == PXL_IN_CLICK ? (a > 0 ? a : 1) : 0);
        if (theApp->buttonFn) theApp->buttonFn(e);
    } else if (kind >= PXL_IN_KEY_DOWN && kind <= PXL_IN_MOUSE_BUTTONS){
        Mtb_Usb_Input_t in = {};
        if (kind == PXL_IN_KEY_DOWN){ in.type = MTB_USB_KEY_DOWN; in.key = (uint8_t)a; in.ascii = (char)b; }
        else if (kind == PXL_IN_KEY_UP){ in.type = MTB_USB_KEY_UP; in.key = (uint8_t)a; }
        else if (kind == PXL_IN_MOUSE_MOVE){ in.type = MTB_USB_MOUSE_MOVE; in.dx = (int16_t)a; in.dy = (int16_t)b; }
        else if (kind == PXL_IN_MOUSE_WHEEL){ in.type = MTB_USB_MOUSE_WHEEL; in.wheel = (int16_t)a; }
        else { in.type = MTB_USB_MOUSE_BUTTONS; in.buttons = (uint8_t)a; }
        usb_Push(in);
    }
}

PXL_EXPORT("pxl_command") int pxl_command(int command, int length){
    if (!theApp || command < 0 || command >= 32 || !theApp->bleAppComServiceFns[command]) return -1;
    JsonDocument doc;
    if (deserializeJson(doc, inbox, (size_t)length)) return -1;
    commandRejected = false;
    theApp->bleAppComServiceFns[command](doc);
    return commandRejected ? -1 : 0;
}

// Messages to the phone page --------------------------------------------------------------------------------------------
int bleApplicationComSend(const char*, const char* message){
    if (!message) return 0;
    pxl_phone_send(message);
    return (int)strlen(message);
}
int bleApplicationComSend(const char* route, const String& message){ return bleApplicationComSend(route, message.c_str()); }

// base64 ------------------------------------------------------------------------------------------------------------------
#include "mbedtls/base64.h"
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
extern "C" int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen){
    const size_t need = ((slen + 2) / 3) * 4;
    *olen = need + 1;
    if (!dst || dlen < need + 1) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    size_t o = 0;
    for (size_t i = 0; i < slen; i += 3){
        const uint32_t v = (uint32_t)src[i] << 16 | (i + 1 < slen ? (uint32_t)src[i + 1] << 8 : 0) | (i + 2 < slen ? src[i + 2] : 0);
        dst[o++] = (unsigned char)B64[(v >> 18) & 63];
        dst[o++] = (unsigned char)B64[(v >> 12) & 63];
        dst[o++] = i + 1 < slen ? (unsigned char)B64[(v >> 6) & 63] : '=';
        dst[o++] = i + 2 < slen ? (unsigned char)B64[v & 63] : '=';
    }
    dst[o] = 0;
    *olen = o;
    return 0;
}
extern "C" int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen){
    size_t o = 0;
    uint32_t v = 0;
    int bits = 0;
    for (size_t i = 0; i < slen; i++){
        const unsigned char c = src[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        const char* p = strchr(B64, c);
        if (!p || !c) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        v = (v << 6) | (uint32_t)(p - B64);
        bits += 6;
        if (bits >= 8){
            bits -= 8;
            if (dst && o < dlen) dst[o] = (unsigned char)(v >> bits);
            o++;
        }
    }
    *olen = o;
    return (!dst || o > dlen) ? MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL : 0;
}

// HTTP: one request at a time, waited for (pxl_http_done arrives inside pxl_yield) ----------------------------------------
#include "mtb_kit_http.h"
static volatile int httpId = 0, httpStatus = 0, httpLength = 0;
PXL_EXPORT("pxl_http_done") void pxl_http_done(int id, int status, int length){
    if (id == httpId){ httpStatus = status; httpLength = length; httpId = -id; }
}
static char* http_Wait(int id, size_t maxBytes, char* buffer, Mtb_Kit_Http_t& result){
    memset(&result, 0, sizeof(result));
    if (id <= 0){ free(buffer); result.code = -1; return nullptr; }
    httpId = id;
    const uint32_t start = pxl_millis();
    while (httpId == id && active && pxl_millis() - start < 30000) pxl_compat_delay(20);
    if (httpId != -id){ httpId = 0; free(buffer); result.code = -11; return nullptr; }       // closed, or no answer
    httpId = 0;
    result.code = httpStatus == PXL_HTTP_TOO_BIG ? 200 : httpStatus;
    result.truncated = httpStatus == PXL_HTTP_TOO_BIG;
    result.length = httpLength > 0 ? (size_t)httpLength : 0;
    if (httpStatus < 0 && httpStatus != PXL_HTTP_TOO_BIG){ free(buffer); result.length = 0; return nullptr; }
    if (result.length > maxBytes) result.length = maxBytes;
    buffer[result.length] = 0;
    return buffer;
}
static int headers_Text(const char* const* h, char* out, size_t size){
    size_t n = 0;
    out[0] = 0;
    for (; h && h[0] && h[1]; h += 2){
        const int w = snprintf(out + n, size - n, "%s%s: %s", n ? "\n" : "", h[0], h[1]);
        if (w < 0 || (size_t)w >= size - n) return -1;
        n += (size_t)w;
    }
    return 0;
}
char* mtb_Kit_Http_Get(const char* url, const char* const* requestHeaders, const char* const*, size_t, size_t maxBytes,
                       Mtb_Kit_Http_t& result, const char*){
    if (maxBytes > 65535) maxBytes = 65535;
    char headers[512];
    char* buffer = (char*)malloc(maxBytes + 1);
    if (!buffer || headers_Text(requestHeaders, headers, sizeof(headers)) < 0){ free(buffer); memset(&result, 0, sizeof(result)); result.code = -1; return nullptr; }
    return http_Wait(pxl_http_get(url, headers, buffer, (int)maxBytes + 1), maxBytes, buffer, result);
}
char* mtb_Kit_Http_Send(const char* method, const char* url, const char* const* requestHeaders, const void* body, size_t length,
                        const char* const* collect, size_t count, size_t maxBytes, Mtb_Kit_Http_t& result, const char* agent){
    if (!method || strcmp(method, "GET") == 0) return mtb_Kit_Http_Get(url, requestHeaders, collect, count, maxBytes, result, agent);
    memset(&result, 0, sizeof(result));
    if (strcmp(method, "POST") != 0){ result.code = -1; return nullptr; }            // the API has GET and POST
    const size_t room = maxBytes ? maxBytes : 1;
    char headers[512];
    char* buffer = (char*)malloc(room + 1);
    if (!buffer || headers_Text(requestHeaders, headers, sizeof(headers)) < 0){ free(buffer); result.code = -1; return nullptr; }
    char* r = http_Wait(pxl_http_post(url, headers, body, (int)length, buffer, (int)room + 1), room, buffer, result);
    if (maxBytes == 0 && r){ free(r); return nullptr; }       // the status was all that was wanted
    return r;
}
ArduinoJson::Allocator* mtb_Kit_Json_Allocator(void){ return ArduinoJson::detail::DefaultAllocator::instance(); }

// FreeRTOS queues, within the one thread ------------------------------------------------------------------------------------
struct PxlQueue { uint8_t* data; UBaseType_t length, size, head, count; };
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize){
    PxlQueue* q = (PxlQueue*)calloc(1, sizeof(PxlQueue));
    if (!q) return nullptr;
    q->data = (uint8_t*)calloc(length ? length : 1, itemSize ? itemSize : 1);
    if (!q->data){ free(q); return nullptr; }
    q->length = length ? length : 1; q->size = itemSize;
    return q;
}
static BaseType_t queue_Put(QueueHandle_t q, const void* item, bool front){
    if (!q || q->count >= q->length) return pdFALSE;
    if (front){ q->head = (q->head + q->length - 1) % q->length; memcpy(q->data + q->head * q->size, item, q->size); }
    else memcpy(q->data + ((q->head + q->count) % q->length) * q->size, item, q->size);
    q->count++;
    return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t){ return queue_Put(q, item, false); }
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t){ return queue_Put(q, item, false); }
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t){ return queue_Put(q, item, true); }
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item){ if (q){ q->count = 0; q->head = 0; } return queue_Put(q, item, false); }
static BaseType_t queue_Get(QueueHandle_t q, void* item, TickType_t wait, bool take){
    if (!q) return pdFALSE;
    const uint32_t start = pxl_millis();
    while (q->count == 0 && wait > 0 && active && (wait == portMAX_DELAY || pxl_millis() - start < wait)) pxl_compat_delay(wait < 20 ? wait : 20);
    if (q->count == 0) return pdFALSE;
    memcpy(item, q->data + q->head * q->size, q->size);
    if (take){ q->head = (q->head + 1) % q->length; q->count--; }
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait){ return queue_Get(q, item, wait, true); }
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait){ return queue_Get(q, item, wait, false); }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q){ return q ? q->count : 0; }
BaseType_t xQueueReset(QueueHandle_t q){ if (q){ q->head = 0; q->count = 0; } return pdTRUE; }
void vQueueDelete(QueueHandle_t q){ if (q){ free(q->data); free(q); } }

// Arduino's file system on the app's files ----------------------------------------------------------------------------------
#include "FS.h"
#include "LittleFS.h"
fs::FS LittleFS;
void pxl_compat_file_name(const char* path, char* out, size_t size){
    if (!path) path = "";
    if (strncmp(path, "/littlefs", 9) == 0) path += 9;
    while (*path == '/') path++;
    char flat[128];
    size_t n = 0;
    for (const char* c = path; *c && n < sizeof(flat) - 1; c++)
        flat[n++] = (isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == '_') ? *c : '_';
    flat[n] = 0;
    if (n <= 31 && n < size){ memcpy(out, flat, n + 1); if (out[0] == '.') out[0] = '_'; return; }
    uint32_t h = 2166136261u;                                        // too long: its start and a hash
    for (size_t i = 0; i < n; i++){ h ^= (uint8_t)flat[i]; h *= 16777619u; }
    snprintf(out, size, "%.22s_%08x", flat, (unsigned)h);
}
struct fs::File::Data { char name[32]; char mode; uint8_t* buf; size_t length, capacity, at; bool dirty; int refs; };
fs::File::File(const char* path, char mode){
    d_ = (Data*)calloc(1, sizeof(Data));
    if (!d_) return;
    d_->refs = 1;
    d_->mode = mode;
    pxl_compat_file_name(path, d_->name, sizeof(d_->name));
    if (mode == 'r' || mode == 'a'){
        uint8_t* b = (uint8_t*)malloc(64 * 1024 + 1);
        const int n = b ? pxl_file_read(d_->name, b, 64 * 1024 + 1) : -1;
        if (n < 0 && mode == 'r'){ free(b); free(d_); d_ = nullptr; return; }
        d_->buf = b; d_->length = n > 0 ? (size_t)n : 0; d_->capacity = 64 * 1024 + 1;
        if (mode == 'a') d_->at = d_->length;
    }
    if (mode == 'w') d_->dirty = true;                               // an empty file, even if nothing is written
}
fs::File::File(const File& o) : d_(o.d_){ if (d_) d_->refs++; }
fs::File& fs::File::operator=(const File& o){ if (this != &o){ close(); d_ = o.d_; if (d_) d_->refs++; } return *this; }
fs::File::~File(){ close(); }
fs::File::operator bool() const { return d_ != nullptr; }
size_t fs::File::size() const { return d_ ? d_->length : 0; }
int fs::File::available() const { return d_ ? (int)(d_->length - d_->at) : 0; }
size_t fs::File::position() const { return d_ ? d_->at : 0; }
bool fs::File::seek(size_t pos){ if (!d_ || pos > d_->length) return false; d_->at = pos; return true; }
const char* fs::File::name() const { return d_ ? d_->name : ""; }
size_t fs::File::read(uint8_t* b, size_t n){
    if (!d_ || !d_->buf) return 0;
    if (n > d_->length - d_->at) n = d_->length - d_->at;
    memcpy(b, d_->buf + d_->at, n);
    d_->at += n;
    return n;
}
int fs::File::read(){ uint8_t c; return read(&c, 1) == 1 ? c : -1; }
String fs::File::readString(){
    if (!d_ || !d_->buf) return String();
    String s;
    s.concat((const char*)d_->buf + d_->at, d_->length - d_->at);
    d_->at = d_->length;
    return s;
}
size_t fs::File::write(const uint8_t* b, size_t n){
    if (!d_ || d_->mode == 'r') return 0;
    if (d_->at + n > d_->capacity){
        size_t cap = d_->capacity ? d_->capacity * 2 : 1024;
        while (cap < d_->at + n) cap *= 2;
        if (cap > 64 * 1024) cap = 64 * 1024;
        if (d_->at + n > cap) return 0;                               // a file is 64 KB at most
        uint8_t* nb = (uint8_t*)realloc(d_->buf, cap);
        if (!nb) return 0;
        d_->buf = nb; d_->capacity = cap;
    }
    memcpy(d_->buf + d_->at, b, n);
    d_->at += n;
    if (d_->at > d_->length) d_->length = d_->at;
    d_->dirty = true;
    return n;
}
void fs::File::close(){
    if (!d_) return;
    if (--d_->refs == 0){
        if (d_->dirty && d_->mode != 'r') pxl_file_write(d_->name, d_->buf ? d_->buf : (const uint8_t*)"", (int)d_->length);
        free(d_->buf);
        free(d_);
    }
    d_ = nullptr;
}
fs::File fs::FS::open(const char* path, const char* mode, bool){ return File(path, mode && mode[0] ? mode[0] : 'r'); }
bool fs::FS::exists(const char* path){ char n[32]; pxl_compat_file_name(path, n, sizeof(n)); uint8_t b; return pxl_file_read(n, &b, 1) >= 0; }
bool fs::FS::remove(const char* path){ char n[32]; pxl_compat_file_name(path, n, sizeof(n)); return pxl_file_remove(n) == 0; }
bool fs::FS::rename(const char* from, const char* to){
    File a(from, 'r');
    if (!a) return false;
    File b(to, 'w');
    uint8_t* buf = (uint8_t*)malloc(a.size() + 1);
    if (!buf) return false;
    const size_t n = a.read(buf, a.size());
    const bool ok = b.write(buf, n) == n;
    free(buf);
    a.close();
    b.close();
    return ok && remove(from);
}
size_t fs::FS::usedBytes(){
    char* list = (char*)malloc(8192);
    if (!list) return 0;
    size_t used = 0;
    if (pxl_file_list("", list, 8192) > 0)
        for (char* t = strchr(list, '\t'); t; t = strchr(t + 1, '\n') ? strchr(strchr(t + 1, '\n'), '\t') : nullptr) used += strtoul(t + 1, nullptr, 10);
    free(list);
    return used;
}

// Arduino's HTTPClient on the compat HTTP helper ------------------------------------------------------------------------------
#include "HTTPClient.h"
bool HTTPClient::begin(const char* url){ end(); snprintf(url_, sizeof(url_), "%s", url ? url : ""); headerCount_ = 0; return url_[0] != 0; }
void HTTPClient::addHeader(const char* name, const char* value){
    if (headerCount_ >= 4 || !name || !value) return;                // the API takes 4 headers
    snprintf(headers_[2 * headerCount_], sizeof(headers_[0]), "%s", name);
    snprintf(headers_[2 * headerCount_ + 1], sizeof(headers_[0]), "%s", value);
    headerCount_++;
}
int HTTPClient::sendRequest(const char* method, const uint8_t* body, size_t length){
    const char* h[9] = {nullptr};
    for (int i = 0; i < headerCount_ * 2; i++) h[i] = headers_[i];
    Mtb_Kit_Http_t r;
    free(body_);
    body_ = mtb_Kit_Http_Send(method, url_, headerCount_ ? h : nullptr, body, length, nullptr, 0, 64 * 1024 - 1, r, "Pixlpal");
    stream_.data = body_; stream_.length = body_ ? r.length : 0; stream_.at = 0;
    return r.code;
}
int HTTPClient::GET(){ return sendRequest("GET", nullptr, 0); }
int HTTPClient::POST(const uint8_t* body, size_t length){ return sendRequest("POST", body, length); }
void HTTPClient::end(){ free(body_); body_ = nullptr; stream_ = Pxl_Body_Stream(); }
#include "mtb_text_scroll.h"
Mtb_Status_Notice_t statusBarNotif;

uint8_t mtb_Decode_Jpg_RGB888(const uint8_t*, size_t, uint8_t** rgb, uint16_t*, uint16_t*, uint16_t*){ if (rgb) *rgb = nullptr; return 1; }

// Tickers (Mtb_ScrollText_t): each active one moves on at the app's waits ----------------------------------------------------
#include "mtb_kit_ui.h"
static Mtb_ScrollText_t* tickers[4];
static void tickers_Step(void){ for (auto t : tickers) if (t) t->step(); }
Mtb_ScrollText_t::Mtb_ScrollText_t(uint16_t x, uint16_t y, uint16_t w, const uint8_t* f, uint16_t c, uint16_t sp, uint16_t passes)
    : xPos(x), yPos(y), width(w), height(f ? f[6] : 8), color(c), speed(sp ? sp : 20), pass(passes ? passes : 1), font(f) {}
Mtb_ScrollText_t::~Mtb_ScrollText_t(){
    for (auto& t : tickers) if (t == this) t = nullptr;
    free(bits_);
}
void Mtb_ScrollText_t::mtb_Scroll_This_Text(const char* text){ mtb_Scroll_This_Text(text, color); }
void Mtb_ScrollText_t::mtb_Scroll_This_Text(const char* text, uint16_t c){
    free(bits_); bits_ = nullptr;
    if (!text || !font) return;
    const int tw = mtb_Kit_Text_Width(font, text);
    length_ = width + tw + width;                         // blank, the text, blank: it comes in and goes out
    stride_ = (length_ + 7) / 8;
    bits_ = (uint8_t*)calloc((size_t)stride_ * height, 1);
    if (!bits_) return;
    pxl_compat_glyphs(bits_, (int16_t)stride_, (int16_t)length_, (int16_t)height, font, text, width);
    now_ = c;
    offset_ = 0;
    left_ = pass;
    lastMs_ = pxl_millis();
    bool listed = false;
    for (auto t : tickers) listed |= t == this;
    for (auto& t : tickers) if (!listed && !t){ t = this; listed = true; }
    pxl_compat_add_background(tickers_Step);
}
bool Mtb_ScrollText_t::mtb_Scroll_Active(uint8_t check_or_stop){
    if (check_or_stop == STOP_SCROLL){
        free(bits_); bits_ = nullptr; left_ = 0;
        pxl_rect(xPos, yPos, width, height, backgroundColor);
    }
    return bits_ != nullptr && left_ > 0;
}
void Mtb_ScrollText_t::step(){
    if (!bits_ || left_ <= 0) return;
    const uint32_t now = pxl_millis();
    const int moves = (int)((now - lastMs_) / speed);
    if (moves <= 0) return;
    lastMs_ += (uint32_t)moves * speed;
    offset_ += moves;
    if (offset_ > length_ - width){
        if (--left_ <= 0){ free(bits_); bits_ = nullptr; pxl_rect(xPos, yPos, width, height, backgroundColor); return; }
        offset_ = 0;
    }
    mtb_Panel_Draw_Mono((int16_t)xPos, (int16_t)yPos, (int16_t)width, (int16_t)height, bits_, (uint32_t)stride_, (uint32_t)offset_, now_, backgroundColor);
}

// The fixed text widget: its background behind the text (and behind what the text before covered), then the text.
uint16_t Mtb_FixedText_t::mtb_Write_String(const char* text){
    if (!text || !font) return 0;
    const int16_t w = mtb_Kit_Text_Width(font, text);
    const int16_t cover = w > lastWidth_ ? w : lastWidth_;
    if (cover > 0) pxl_rect(x, y, cover, font[6], backgroundColor);
    mtb_Kit_Text(x, y, font, color, text, backgroundColor);
    lastWidth_ = w;
    return (uint16_t)w;
}
uint16_t Mtb_FixedText_t::mtb_Clear_String(){
    if (lastWidth_ > 0) pxl_rect(x, y, lastWidth_, font[6], backgroundColor);
    lastWidth_ = 0;
    return 0;
}
