/**
 * Guest Welcome: a personal welcome in a hotel room. The guest's name and room number, the guest Wi-Fi as a QR code
 * their phone's camera can scan to join (and, if wanted, the network and password written beside it), and along the
 * bottom in turn: the message of the day, checkout time and breakfast hours. From 10 pm to 6 am it dims to a quiet
 * clock (a click shows the full screen for a minute).
 *
 * Staff set it from the phone (Pixlpal Wi-Fi QR: "WIFI:T:WPA;S:...;P:...;;", Project Nayuki's encoder, MIT). Works
 * without the internet. NVS "gwlSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "esp_heap_caps.h"
#include "qrcodegen.h"
#include "gwl_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "gwlSave"
#define SAVE_MAGIC  0x47574C01u
#define QR_MAX_VER  10

EXT_RAM_BSS_ATTR TaskHandle_t guestWelcome_Task_H = NULL;
static void guestWelcome_App_Task(void* dApplication);
static void gwl_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* guestWelcome_App;
Mtb_Applications* guestWelcome_App_GetInstance(){
    if (!guestWelcome_App){
        guestWelcome_App = new Mtb_Applications_FullScreen(guestWelcome_App_Task, &guestWelcome_Task_H, "Guest Welcome", {0,0}, 8192);
        guestWelcome_App->mtb_App_Set_Mobile_UI(GWL_UI_MANIFEST, GWL_UI_API, nullptr, gwl_Values);
    }
    return guestWelcome_App;
}
MTB_REGISTER_APP(guestWelcome_App, 0, 0)      // the catalogue gives the app its number

struct Gwl_Save_t {
    uint32_t magic;
    char guest[40], room[12], hotel[40];
    char ssid[33], password[64];
    uint8_t security, showPassword, night;
    char checkout[16], breakfast[48], message[160];
};

static EXT_RAM_BSS_ATTR Gwl_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

// The QR code, made again when the Wi-Fi changes.
static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
static uint8_t qrTemp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
static bool qrOk = false;

static void read_Save(Gwl_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.showPassword = 1; s.night = 1; snprintf(s.checkout, sizeof(s.checkout), "11:00"); }
    if (s.security > 2) s.security = 0;
    s.guest[sizeof(s.guest) - 1] = s.room[sizeof(s.room) - 1] = s.hotel[sizeof(s.hotel) - 1] = '\0';
    s.ssid[sizeof(s.ssid) - 1] = s.password[sizeof(s.password) - 1] = '\0';
    s.checkout[sizeof(s.checkout) - 1] = s.breakfast[sizeof(s.breakfast) - 1] = s.message[sizeof(s.message) - 1] = '\0';
}

/** A Wi-Fi QR code's text, with \ ; , : " escaped. */
static void escape_Into(char* out, size_t n, const char* in){
    size_t k = strlen(out);
    for (; *in && k + 2 < n; in++){
        if (strchr("\\;,:\"", *in)) out[k++] = '\\';
        out[k++] = *in;
    }
    out[k] = '\0';
}
static void make_Qr(const Gwl_Save_t& s){
    qrOk = false;
    if (!s.ssid[0]) return;
    char text[256];
    snprintf(text, sizeof(text), "WIFI:T:%s;S:", s.security == 2 ? "nopass" : s.security == 1 ? "WEP" : "WPA");
    escape_Into(text, sizeof(text), s.ssid);
    strlcat(text, ";", sizeof(text));
    if (s.security != 2){ strlcat(text, "P:", sizeof(text)); escape_Into(text, sizeof(text), s.password); strlcat(text, ";", sizeof(text)); }
    strlcat(text, ";", sizeof(text));
    qrOk = qrcodegen_encodeText(text, qrTemp, qr, qrcodegen_Ecc_MEDIUM, 1, QR_MAX_VER, qrcodegen_Mask_AUTO, true);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static bool text_Into(JsonDocument& c, const char* key, char* dest, size_t size, bool ascii = true){
    const char* v = c[key] | (const char*)nullptr;
    if (!v || strlen(v) >= size + 40){ return false; }
    char tmp[200];
    if (ascii) mtb_Kit_To_Ascii(tmp, sizeof(tmp), v); else snprintf(tmp, sizeof(tmp), "%s", v);
    char* p = tmp; while (*p == ' ') p++;
    size_t len = strlen(p); while (len && p[len - 1] == ' ') p[--len] = '\0';
    if (len >= size) return false;
    portENTER_CRITICAL(&lock); snprintf(dest, size, "%s", p); portEXIT_CRITICAL(&lock);
    return true;
}
#define TEXT_CMD(fn, key, field, ascii, remakeQr) \
    static void fn(JsonDocument& c){ if (!text_Into(c, key, save.field, sizeof(save.field), ascii)){ mtb_Ble_App_Cmd_Reject(); return; } if (remakeQr) make_Qr(save); phoneChanged = true; }
TEXT_CMD(cmd_Guest, "guest", guest, true, false)
TEXT_CMD(cmd_Room, "room", room, true, false)
TEXT_CMD(cmd_Hotel, "hotel", hotel, true, false)
TEXT_CMD(cmd_Ssid, "ssid", ssid, false, true)                                 // the network's own spelling, for the code
TEXT_CMD(cmd_Password, "password", password, false, true)
TEXT_CMD(cmd_Checkout, "checkout", checkout, true, false)
TEXT_CMD(cmd_Breakfast, "breakfast", breakfast, true, false)
TEXT_CMD(cmd_Message, "message", message, true, false)
static void cmd_Security(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["security"], 0, 2, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.security = (uint8_t)v; make_Qr(save); phoneChanged = true; }
static void cmd_Show(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["showPassword"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.showPassword = (uint8_t)v; phoneChanged = true; }
static void cmd_Left(JsonDocument&){
    portENTER_CRITICAL(&lock); save.guest[0] = save.room[0] = save.message[0] = '\0'; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Night(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["night"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.night = (uint8_t)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool knobClick = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; }
static void on_Turn(rotary_encoder_rotation_t){}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 14), C_HEAD = MTB_KIT_RGB(18, 16, 10), C_GOLD = MTB_KIT_RGB(230, 185, 90);

static void clock_Text(char* t, size_t n, const pxl_time_t& pt){
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", (int)pt.hour, (int)pt.minute);
    else snprintf(t, n, "%d:%02d%s", pt.hour % 12 ? (int)(pt.hour % 12) : 12, (int)pt.minute, pt.hour < 12 ? "AM" : "PM");
}

/** The QR code, on white with a quiet edge, as large as fits in @p box: its right edge. */
static int draw_Qr(int x, int y, int box){
    const int n = qrcodegen_getSize(qr);
    const int scale = box / (n + 4);
    if (scale < 1) return x;
    const int side = (n + 4) * scale;
    mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + side - 1), (int16_t)(y + side - 1), KIT_TEXT);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (qrcodegen_getModule(qr, c, r)){
                const int px = x + (c + 2) * scale, py = y + (r + 2) * scale;
                mtb_Panel_Fill_Rect((int16_t)px, (int16_t)py, (int16_t)(px + scale - 1), (int16_t)(py + scale - 1), BLACK);
            }
    return x + side;
}

static void bottom_Line(const Gwl_Save_t& s, uint32_t ms){
    // In turn, six seconds each: the message, checkout, breakfast. A message wider than the screen scrolls.
    const char* items[3];
    char a[64], b[80];
    int n = 0;
    if (s.message[0]) items[n++] = s.message;
    if (s.checkout[0]){ snprintf(a, sizeof(a), "CHECKOUT %s", s.checkout); items[n++] = a; }
    if (s.breakfast[0]){ snprintf(b, sizeof(b), "BREAKFAST %s", s.breakfast); items[n++] = b; }
    if (!n) return;
    const uint16_t bg = C_HEAD;
    mtb_Panel_Fill_Rect(0, 116, W - 1, 127, bg);
    const int i = (int)((ms / 6000) % (uint32_t)n);
    char up[176];
    snprintf(up, sizeof(up), "%s", items[i]);
    mtb_Kit_Upper(up);
    const int w = mtb_Kit_Text_Width(Terminal6x8, up);
    if (w <= W - 8) mtb_Kit_Text_Centre(W / 2, 118, Terminal6x8, C_GOLD, up, bg);
    else {
        // Scrolls once through in its six seconds (or longer: then it holds the turn until it has).
        const int x = W - (int)((ms % 6000) * (uint32_t)(w + W) / 6000);
        mtb_Kit_Text((int16_t)x, 118, Terminal6x8, C_GOLD, up, bg);
    }
}

static void draw_Welcome(const Gwl_Save_t& s, const pxl_time_t& pt, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64], fit[64];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.hotel[0] ? s.hotel : "WELCOME");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 50);
    mtb_Kit_Text(3, 2, Terminal6x8, C_GOLD, fit, C_HEAD);
    if (pt.valid){ clock_Text(t, sizeof(t), pt); mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD); }

    int x0 = 6;
    if (qrOk) x0 = draw_Qr(4, 13, 102) + 8;
    const int width = W - x0 - 4;
    int y = 14;
    if (s.guest[0]){
        mtb_Kit_Text(x0, y, Terminal6x8, KIT_LABEL, "WELCOME,", C_BG);
        char up[48]; snprintf(up, sizeof(up), "%s", s.guest); mtb_Kit_Upper(up);
        if (mtb_Kit_Text_Width(PT_Sans_Narrow_Bold18x22, up) <= width){ mtb_Kit_Text(x0, y + 10, PT_Sans_Narrow_Bold18x22, KIT_TEXT, up, C_BG); y += 36; }
        else {                                                                  // a long name: two lines of the large letters
            char lines[2][48];
            const int n = mtb_Kit_Wrap(up, PT_Sans_Narrow_Bold18x22, width, &lines[0][0], sizeof(lines[0]), 2);
            for (int i = 0; i < n; i++) mtb_Kit_Text(x0, y + 10 + i * 22, PT_Sans_Narrow_Bold18x22, KIT_TEXT, lines[i], C_BG);
            y += 14 + n * 22;
        }
    } else { mtb_Kit_Text(x0, y + 2, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "WELCOME", C_BG); y += 30; }
    if (s.room[0]){ snprintf(t, sizeof(t), "ROOM %s", s.room); mtb_Kit_Upper(t); mtb_Kit_Text(x0, y, Terminal10x17, C_GOLD, t, C_BG); y += y > 60 ? 18 : 21; }
    if (s.ssid[0]){
        mtb_Kit_Text(x0, y, Terminal4x6, KIT_LABEL, "WI-FI", C_BG);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, s.ssid, width - 24);
        mtb_Kit_Text(x0 + 24, y - 1, Terminal6x8, KIT_TEXT, fit, C_BG);
        y += 10;
        if (s.showPassword && s.security != 2 && s.password[0]){
            mtb_Kit_Text(x0, y, Terminal4x6, KIT_LABEL, "PASS", C_BG);
            mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, s.password, width - 24);
            mtb_Kit_Text(x0 + 24, y - 1, Terminal6x8, KIT_TEXT, fit, C_BG);
            y += 10;
        }
        if (qrOk && y < 106) mtb_Kit_Text(x0, y + 1, Terminal4x6, KIT_CYAN, "SCAN THE CODE TO CONNECT", C_BG);
    }
    bottom_Line(s, ms);
}

static void draw_Night(const Gwl_Save_t& s, const pxl_time_t& pt){
    mtb_Panel_Fill_Screen(BLACK);
    char t[24];
    clock_Text(t, sizeof(t), pt);
    const uint16_t dim = MTB_KIT_RGB(70, 56, 28);
    mtb_Kit_Text_Centre(W / 2, 28, MS_Gothic31x60, dim, t, BLACK);
    if (s.room[0]){ snprintf(t, sizeof(t), "ROOM %s", s.room); mtb_Kit_Upper(t); mtb_Kit_Text_Centre(W / 2, 98, Terminal6x8, MTB_KIT_RGB(40, 32, 16), t, BLACK); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void guestWelcome_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Guest, cmd_Room, cmd_Hotel, cmd_Ssid, cmd_Password, cmd_Security, cmd_Show, cmd_Checkout, cmd_Breakfast, cmd_Message, cmd_Left, cmd_Night);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    make_Qr(save);
    knobClick = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, wakeUntil = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        if (knobClick){ knobClick = false; wakeUntil = ms + 60000; lastDraw = 0; }
        pxl_time_t pt;
        pxl_time(&pt);
        const bool night = save.night && pt.valid && (pt.hour >= 22 || pt.hour < 6) && (int32_t)(wakeUntil - ms) <= 0;
        if (ms - lastDraw >= (night ? 1000u : 60u)){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (night) draw_Night(save, pt); else draw_Welcome(save, pt, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(night ? 200 : 30);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void gwl_Values(JsonObject values){
    Gwl_Save_t* s = (Gwl_Save_t*)heap_caps_malloc(sizeof(Gwl_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    values["guest"] = s->guest;
    values["room"] = s->room;
    values["hotel"] = s->hotel;
    values["ssid"] = s->ssid;
    values["password_set"] = s->password[0] ? 1 : 0;                             // the password itself never goes back
    values["security"] = s->security;
    values["showPassword"] = s->showPassword;
    values["checkout"] = s->checkout;
    values["breakfast"] = s->breakfast;
    values["message"] = s->message;
    values["night"] = s->night;
    JsonArray rows = values["screen"].to<JsonArray>();
    char t[96];
    JsonObject g = rows.add<JsonObject>();
    g["label"] = "Guest";
    if (s->guest[0]){ snprintf(t, sizeof(t), "%s%s%s", s->guest, s->room[0] ? ", room " : "", s->room); g["value"] = t; g["state"] = "ok"; }
    else { g["value"] = "No guest: Pixlpal shows a general welcome"; g["state"] = "info"; }
    JsonObject w = rows.add<JsonObject>();
    w["label"] = "Wi-Fi";
    if (!s->ssid[0]){ w["value"] = "No network yet: give it below"; w["state"] = "warn"; }
    else if (s->security != 2 && !s->password[0]){ w["value"] = "The network has no password yet"; w["state"] = "warn"; }
    else { snprintf(t, sizeof(t), "%s, scannable code shown", s->ssid); w["value"] = t; w["state"] = "ok"; }
    heap_caps_free(s);
}
