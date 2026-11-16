/**
 * Scam Alerts: fraud-awareness tips for a bank branch, in turn on the screen, each a picture, a headline and what to
 * know: never share your PIN or one-time code, there is no "safe account", calls and links can be fake... Ten of its
 * own, and up to six of the bank's ("Headline | what to know"). An urgent alert about a scam going round now comes up
 * in red after each tip. Along the bottom, a line the bank chooses ("Report fraud: 0800 123 456").
 *
 * The knob: turn to go through the tips (the screen holds there half a minute). NVS "scaSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_image.h"
#include "esp_heap_caps.h"
#include "sca_Tips.h"
#include "sca_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define W           256
#define H           128
#define OWN         6
#define NVS_KEY     "scaSave"
#define SAVE_MAGIC  0x53434102u

EXT_RAM_BSS_ATTR TaskHandle_t scamAlerts_Task_H = NULL;
static void scamAlerts_App_Task(void* dApplication);
static void sca_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* scamAlerts_App;
Mtb_Applications* scamAlerts_App_GetInstance(){
    if (!scamAlerts_App){
        scamAlerts_App = new Mtb_Applications_FullScreen(scamAlerts_App_Task, &scamAlerts_Task_H, "Scam Alerts", {0,0}, 8192);
        scamAlerts_App->mtb_App_Set_Mobile_UI(SCA_UI_MANIFEST, SCA_UI_API, nullptr, sca_Values);
    }
    return scamAlerts_App;
}
MTB_REGISTER_APP(scamAlerts_App, 0, 0)        // the catalogue gives the app its number

struct Sca_Own_t { char headline[48]; char body[140]; };

struct Sca_Save_t {
    uint32_t magic;
    char bank[36];
    Sca_Own_t own[OWN];
    uint8_t ownN, builtin, seconds;
    char alert[240];
    char contact[64];
};

static EXT_RAM_BSS_ATTR Sca_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile int shownSlide = 0;

static void read_Save(Sca_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.builtin = 1; s.seconds = 12;
        snprintf(s.contact, sizeof(s.contact), "Not sure? Hang up and call us on the number on your card.");
    }
    if (s.ownN > OWN) s.ownN = 0;
    if (s.seconds < 6 || s.seconds > 60) s.seconds = 12;
    if (!s.ownN) s.builtin = 1;
    s.bank[sizeof(s.bank) - 1] = s.alert[sizeof(s.alert) - 1] = s.contact[sizeof(s.contact) - 1] = '\0';
    for (int i = 0; i < OWN; i++){ s.own[i].headline[sizeof(s.own[i].headline) - 1] = s.own[i].body[sizeof(s.own[i].body) - 1] = '\0'; }
}

static void trim(char*& p){
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = '\0';
}

// The slides: the bank's tips, then Scam Alerts' own; each followed by the alert while there is one -------------------------------
static int tip_Count(const Sca_Save_t& s){ return s.ownN + (s.builtin ? SCA_TIP_COUNT : 0); }
static int slide_Count(const Sca_Save_t& s){ return tip_Count(s) * (s.alert[0] ? 2 : 1); }

/** Slide @p i: the tip it is (or follows), and whether it is the alert. */
static void slide_Is(const Sca_Save_t& s, int i, int& tip, bool& alert){
    if (s.alert[0]){ tip = i / 2; alert = i % 2; } else { tip = i; alert = false; }
}

static void tip_Text(const Sca_Save_t& s, int tip, const char*& headline, const char*& body, int& icon){
    if (tip < s.ownN){ headline = s.own[tip].headline; body = s.own[tip].body; icon = I_SHIELD; return; }
    const Sca_Tip_t& t = SCA_TIPS[(tip - s.ownN) % SCA_TIP_COUNT];
    headline = t.headline; body = t.body; icon = t.icon;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Bank(JsonDocument& c){
    const char* v = c["bank"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.bank, sizeof(save.bank), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Tip(JsonDocument& c){
    for (int i = 0; i < OWN; i++){
        char key[8]; snprintf(key, sizeof(key), "tip%d", i);
        if (c[key].isNull()) continue;
        char t[256]; mtb_Kit_To_Ascii(t, sizeof(t), c[key] | "");
        char* p = t; trim(p);
        Sca_Own_t o = {};
        if (*p){
            char* bar = strchr(p, '|');
            if (!bar){ mtb_Ble_App_Cmd_Reject(); return; }
            *bar = '\0';
            char* h = p; trim(h);
            char* b = bar + 1; trim(b);
            if (!*h || !*b || strlen(h) >= sizeof(o.headline) || strlen(b) >= sizeof(o.body)){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(o.headline, sizeof(o.headline), "%s", h);
            snprintf(o.body, sizeof(o.body), "%s", b);
        }
        portENTER_CRITICAL(&lock);
        if (!*p){
            if (i < save.ownN){ memmove(&save.own[i], &save.own[i + 1], sizeof(save.own[0]) * (OWN - 1 - i)); memset(&save.own[OWN - 1], 0, sizeof(save.own[0])); save.ownN--; }
            if (!save.ownN) save.builtin = 1;
        } else {
            const int at = i < save.ownN ? i : save.ownN;
            save.own[at] = o;
            if (at >= save.ownN) save.ownN++;
        }
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Builtin(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["builtin"], 0, 1, v) || (!v && !save.ownN)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.builtin = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Alert(JsonDocument& c){
    const char* v = c["alert"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[512]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    if (strlen(p) >= sizeof(save.alert)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.alert, sizeof(save.alert), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Seconds(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seconds"], 6, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.seconds = (uint8_t)v; phoneChanged = true; }
static void cmd_Contact(JsonDocument& c){
    const char* v = c["contact"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[128]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.contact, sizeof(save.contact), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static void on_Button(button_event_t){}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 14, 24), C_HEAD = MTB_KIT_RGB(0, 70, 70), C_TEAL = MTB_KIT_RGB(0, 190, 170);
static const uint16_t C_RED = MTB_KIT_RGB(70, 0, 6), C_RED_HEAD = MTB_KIT_RGB(200, 20, 30);

/** Wraps @p text to @p width in the largest of the fonts that fits @p maxLines; the lines, and the font chosen. */
static int wrap_Fit(const char* text, int width, int maxLines, const uint8_t* const* fonts, int nFonts, char* lines, size_t lineSize, const uint8_t*& font){
    for (int f = 0; f < nFonts; f++){
        font = fonts[f];
        const int n = mtb_Kit_Wrap(text, font, (int16_t)width, lines, lineSize, 16);
        if (n <= maxLines || f == nFonts - 1) return n < maxLines ? n : maxLines;
    }
    return 0;
}

static void footer(const Sca_Save_t& s, int slide, int slides, uint16_t band, uint16_t fg){
    char t[96], fit[96];
    mtb_Panel_Fill_Rect(0, H - 9, W - 1, H - 1, band);
    char n[16]; snprintf(n, sizeof(n), "%d/%d", slide + 1, slides);
    const int nw = mtb_Kit_Text_Width(Terminal4x6, n);
    mtb_Kit_Text_Right(W - 3, H - 7, Terminal4x6, fg, n, band);
    if (s.contact[0]){
        snprintf(t, sizeof(t), "%s", s.contact); mtb_Kit_Upper(t);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, (int16_t)(W - 12 - nw));
        mtb_Kit_Text(3, H - 7, Terminal4x6, fg, fit, band);
    }
}

static void draw_Tip(const Sca_Save_t& s, int tip, int slide, int slides){
    mtb_Panel_Fill_Screen(C_BG);
    char t[160], fit[96];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.bank[0] ? s.bank : "STAY SAFE");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 80);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, fit, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_GOLD, "BEAT THE SCAMMERS", C_HEAD);
    const char* headline; const char* body; int icon;
    tip_Text(s, tip, headline, body, icon);
    const char* svg = SCA_ICONS[icon];
    mtb_Kit_Draw_Svg(svg, strlen(svg), Mtb_Kit_Box_t{6, 24, 46, 46}, C_BG);
    const int x = 60, width = W - x - 4;
    static char lines[16 * 64];
    snprintf(t, sizeof(t), "%s", headline); mtb_Kit_Upper(t);
    static const uint8_t* const HEAD_FONTS[] = {Terminal8x12, Terminal6x8};
    const uint8_t* hf;
    int hn = wrap_Fit(t, width, 2, HEAD_FONTS, 2, lines, 64, hf);
    if (hf == Terminal6x8) hn = mtb_Kit_Wrap(t, hf, (int16_t)width, lines, 64, 3);
    const int hl = hf == Terminal8x12 ? 13 : 9;
    int y = 16;
    for (int i = 0; i < hn; i++, y += hl) mtb_Kit_Text(x, (int16_t)y, hf, KIT_GOLD, lines + i * 64, C_BG);
    y += 4;
    mtb_Panel_Draw_HLine((int16_t)(y - 3), x, (int16_t)(x + 30), C_TEAL);
    const int room = (H - 11 - y) / 10;
    const int bn = mtb_Kit_Wrap(body, Terminal6x8, (int16_t)width, lines, 64, room);
    for (int i = 0; i < bn && i < room; i++, y += 10) mtb_Kit_Text(x, (int16_t)y, Terminal6x8, KIT_TEXT, lines + i * 64, C_BG);
    footer(s, slide, slides, C_HEAD, KIT_TEXT);
}

static void draw_Alert(const Sca_Save_t& s, int slide, int slides, uint32_t ms){
    mtb_Panel_Fill_Screen(C_RED);
    const bool on = (ms / 500) % 2;
    const uint16_t band = on ? C_RED_HEAD : KIT_GOLD, ink = on ? KIT_TEXT : BLACK;
    mtb_Panel_Fill_Rect(0, 0, W - 1, 25, band);
    const char* svg = SCA_ICONS[I_WARN];
    const uint16_t back = MTB_KIT_RGB(30, 0, 4);                              // the yellow sign shows on either band
    mtb_Panel_Fill_Rect(4, 1, 29, 24, back);
    mtb_Kit_Draw_Svg(svg, strlen(svg), Mtb_Kit_Box_t{6, 2, 22, 22}, back);
    mtb_Kit_Text(34, 2, PT_Sans_Narrow_Bold18x22, ink, "SCAM ALERT", band);
    char t[96];
    snprintf(t, sizeof(t), "%s", s.bank[0] ? s.bank : "");
    mtb_Kit_Upper(t);
    char fit[64]; mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, 100);
    if (fit[0]) mtb_Kit_Text_Right(W - 6, 10, Terminal4x6, ink, fit, band);
    static char lines[16 * 64];
    char up[sizeof(s.alert)]; snprintf(up, sizeof(up), "%s", s.alert); mtb_Kit_Upper(up);
    static const uint8_t* const ALERT_FONTS[] = {Terminal8x12, Terminal6x8};
    const uint8_t* f;
    int n = wrap_Fit(up, W - 16, 6, ALERT_FONTS, 2, lines, 64, f);
    const int lh = f == Terminal8x12 ? 14 : 10;
    if (f == Terminal6x8) n = mtb_Kit_Wrap(up, f, W - 16, lines, 64, 9);
    const int top = 30, area = H - 10 - top;
    int y = top + (area - n * lh) / 2;
    if (y < top) y = top;
    for (int i = 0; i < n; i++, y += lh) mtb_Kit_Text_Centre(W / 2, (int16_t)y, f, KIT_TEXT, lines + i * 64, C_RED);
    footer(s, slide, slides, MTB_KIT_RGB(110, 0, 10), KIT_TEXT);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void scamAlerts_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Bank, cmd_Tip, cmd_Builtin, cmd_Alert, cmd_Seconds, cmd_Contact);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = false;
    appOpen = true;
    Sca_Save_t* s = (Sca_Save_t*)heap_caps_malloc(sizeof(Sca_Save_t), MALLOC_CAP_SPIRAM);
    if (!s){ appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t lastDraw = 0, pushedMs = 0, slideAt = millis(), handUntil = 0;
    int slide = 0, drawn = -1;
    bool push = true, saveNow = false, redraw = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = redraw = true; }
        portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock);
        const int slides = slide_Count(*s);
        const int t = turns;
        if (t){ turns -= t; slide += t; slideAt = ms; handUntil = ms + 30000; }
        int tip; bool alert;
        slide = ((slide % slides) + slides) % slides;
        slide_Is(*s, slide, tip, alert);
        const uint32_t hold = alert ? (s->seconds * 1000u * 2 / 3) : s->seconds * 1000u;
        if ((int32_t)(handUntil - ms) <= 0 && ms - slideAt >= hold){ slideAt = ms; slide = (slide + 1) % slides; }
        slide_Is(*s, slide, tip, alert);
        if (slide != drawn) redraw = true;
        if (redraw || (alert && ms - lastDraw >= 250) || ms - lastDraw >= 5000){
            redraw = false; drawn = slide; lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (alert) draw_Alert(*s, slide, slides, ms); else draw_Tip(*s, tip, slide, slides);
            mtb_Panel_End_Update();
            if (shownSlide != slide){ shownSlide = slide; push = true; }
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    heap_caps_free(s);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void sca_Values(JsonObject values){
    Sca_Save_t* s = (Sca_Save_t*)heap_caps_malloc(sizeof(Sca_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    char t[200], key[8];
    values["bank"] = s->bank;
    for (int i = 0; i < OWN; i++){
        snprintf(key, sizeof(key), "tip%d", i);
        if (i < s->ownN){ snprintf(t, sizeof(t), "%s | %s", s->own[i].headline, s->own[i].body); values[key] = t; } else values[key] = "";
    }
    values["builtin"] = s->builtin;
    values["alert"] = s->alert;
    values["seconds"] = s->seconds;
    values["contact"] = s->contact;
    JsonArray rows = values["shown"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = "Tips";
    snprintf(t, sizeof(t), "%d in turn (%u of your own)", tip_Count(*s), (unsigned)s->ownN);
    r["value"] = t; r["state"] = "ok";
    r = rows.add<JsonObject>();
    r["label"] = "Urgent alert";
    if (s->alert[0]){ r["value"] = s->alert; r["state"] = "warn"; } else { r["value"] = "None"; r["state"] = "off"; }
    if (appOpen){
        const int slides = slide_Count(*s);
        int tip; bool alert;
        slide_Is(*s, shownSlide % (slides ? slides : 1), tip, alert);
        const char* h; const char* b; int icon;
        tip_Text(*s, tip, h, b, icon);
        r = rows.add<JsonObject>();
        r["label"] = "Now";
        if (alert) r["value"] = "The urgent alert";
        else { snprintf(t, sizeof(t), "%s", h); r["value"] = t; }
        r["state"] = "info";
    }
    heap_caps_free(s);
}
