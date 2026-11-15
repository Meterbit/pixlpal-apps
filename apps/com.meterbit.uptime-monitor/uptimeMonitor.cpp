/**
 * Uptime Monitor, ID {22,0} (MAKER): up to eight websites checked every few minutes, each with whether it is up, how
 * fast it answered, a history of those times, and how often it was up since the monitor was opened. A site going
 * down shows in red with an alarm; coming back, in green. When no site answers at all, Pixlpal's own internet is the
 * likelier problem: that shows as OFFLINE rather than eight alarms.
 *
 * A worker task makes the requests (mtb_Kit_Http_Get: verified TLS, up to three redirects). Knob: turn to see one
 * site at a time (and back to all), click to check now. NVS "uptSave": the sites and the choices.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "upt_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define TAG             "UPT"
#define W               256
#define H               128
#define SITES           8
#define URL_LEN         160
#define HIST            40
#define NVS_KEY         "uptSave"
#define SAVE_MAGIC      0x55505401u
#define UA              "Pixlpal-UptimeMonitor/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t uptimeMonitor_Task_H = NULL;
static void uptimeMonitor_App_Task(void* dApplication);
static void upt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* uptimeMonitor_App;
Mtb_Applications* uptimeMonitor_App_GetInstance(){
    if (!uptimeMonitor_App){
        uptimeMonitor_App = new Mtb_Applications_FullScreen(uptimeMonitor_App_Task, &uptimeMonitor_Task_H, "Uptime Monitor", {22,0}, 8192);
        uptimeMonitor_App->mtb_App_Set_Mobile_UI(UPT_UI_MANIFEST, UPT_UI_API, nullptr, upt_Values);
    }
    return uptimeMonitor_App;
}
MTB_REGISTER_APP(uptimeMonitor_App, 22, 0)

struct Upt_Save_t {
    uint32_t magic;
    char url[SITES][URL_LEN];
    uint8_t minutes, sound;
    uint16_t slowMs;
};

enum : uint8_t { S_UNKNOWN = 0, S_UP, S_SLOW, S_DOWN };

struct Upt_Site_t {
    uint8_t state;
    int16_t code;
    uint16_t ms;                        // the last answer's time
    uint32_t checkedAt, downSince;
    uint16_t lat[HIST];                 // answer times, oldest first; 0xFFFF: down
    uint8_t histN;
    uint32_t checks, ups;
    char why[40];
};

struct Upt_Shared_t {
    Upt_Site_t site[SITES];
    bool offline;
    uint32_t roundAt;                   // the last round's time
    uint32_t version;
    volatile bool checkNow, stop, running;
    // Alerts for the app task to show: a bit a site.
    uint8_t wentDown, cameBack;
};

static EXT_RAM_BSS_ATTR Upt_Save_t save;
static EXT_RAM_BSS_ATTR Upt_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Upt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 5; s.sound = 1; s.slowMs = 2000; }
    if (s.minutes < 1 || s.minutes > 60) s.minutes = 5;
    if (s.slowMs < 500 || s.slowMs > 10000) s.slowMs = 2000;
    for (int i = 0; i < SITES; i++) s.url[i][URL_LEN - 1] = '\0';
}

/** "https://www.example.com:8443/health" -> "example.com/health" (for the screen). */
static void short_Name(const char* url, char* out, size_t size){
    const char* p = strstr(url, "://");
    p = p ? p + 3 : url;
    if (!strncmp(p, "www.", 4)) p += 4;
    snprintf(out, size, "%s", p);
    char* colon = strchr(out, ':');
    char* slash = strchr(out, '/');
    if (colon && (!slash || colon < slash)) memmove(colon, slash ? slash : colon + strlen(colon), strlen(slash ? slash : "") + 1);
    const size_t n = strlen(out);
    if (n && out[n - 1] == '/') out[n - 1] = '\0';
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Site(JsonDocument& c){
    for (int i = 0; i < SITES; i++){
        char key[8];
        snprintf(key, sizeof(key), "site%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        char url[URL_LEN];
        size_t n = strlen(v);
        while (n && v[n - 1] == ' ') n--;
        if (n == 0) url[0] = '\0';
        else {
            const bool scheme = !strncmp(v, "https://", 8) || !strncmp(v, "http://", 7);
            if (n + (scheme ? 0 : 8) >= URL_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
            snprintf(url, sizeof(url), "%s%.*s", scheme ? "" : "https://", (int)n, v);
            const char* host = strstr(url, "://") + 3;
            if (!*host || *host == '/' || !strchr(host, '.')) {
                // A name with no dot: only a local name with a port or path could be meant ("router.local" has one).
                mtb_Ble_App_Cmd_Reject(); return;
            }
            for (const char* k = url; *k; k++) if ((unsigned char)*k <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
        }
        portENTER_CRITICAL(&lock);
        snprintf(save.url[i], URL_LEN, "%s", url);
        memset(&sh.site[i], 0, sizeof(sh.site[i]));
        portEXIT_CRITICAL(&lock);
        sh.checkNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 60, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.minutes = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.sound = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    sh.checkNow = true;
}
static void cmd_Slow(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["slowMs"], 500, 10000, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.slowMs = (uint16_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static uint32_t nextRound;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    nextRound = 0;
}

static void worker_End(void){
    sh.running = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!sh.running) return;
    if (sh.stop){ worker_End(); return; }
    if (millis() - wk_StepAt < 500) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {
        const uint32_t now = (uint32_t)time(nullptr);
        if (!sh.checkNow && millis() < nextRound){ wk_WakeAt = millis() + 200; continue; }
        sh.checkNow = false;
        Upt_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        nextRound = millis() + s.minutes * 60000u;

        // One round: each site once.
        int16_t code[SITES];
        uint16_t took[SITES];
        bool asked[SITES] = {false};
        int answered = 0, tried = 0;
        for (int i = 0; i < SITES && !sh.stop; i++){
            if (!s.url[i][0]) continue;
            Mtb_Kit_Http_t r = {};
            const uint32_t t0 = millis();
            char* body = mtb_Kit_Http_Get(s.url[i], nullptr, nullptr, 0, 512, r, UA);     // a refusal keeps its reason as the code
            const uint32_t dt = millis() - t0;
            heap_caps_free(body);
            code[i] = (int16_t)(r.code < -32000 ? -1 : r.code > 32000 ? 999 : r.code);
            took[i] = (uint16_t)(dt > 60000 ? 60000 : dt);
            asked[i] = true;
            if (r.code == PXL_HTTP_NOT_ALLOWED) continue;        // not allowed (yet): no answer expected
            tried++;
            if (r.code > 0) answered++;
        }
        if (sh.stop) break;
        const bool offline = tried > 0 && answered == 0;

        portENTER_CRITICAL(&lock);
        sh.offline = offline;
        sh.roundAt = now;
        for (int i = 0; i < SITES; i++){
            if (!asked[i] || strcmp(s.url[i], save.url[i])) continue;     // changed meanwhile
            Upt_Site_t& st = sh.site[i];
            if (offline){ st.checkedAt = now; continue; }                    // no verdict without the internet
            if (code[i] == PXL_HTTP_NOT_ALLOWED){                              // its owner hasn't allowed it on the phone
                st.state = S_UNKNOWN; st.checkedAt = now;
                snprintf(st.why, sizeof(st.why), "NOT ALLOWED");
                continue;
            }
            const bool up = code[i] >= 200 && code[i] < 400;
            const uint8_t was = st.state;
            st.code = code[i];
            st.ms = took[i];
            st.checkedAt = now;
            st.checks++;
            if (up) st.ups++;
            st.state = !up ? S_DOWN : took[i] > s.slowMs ? S_SLOW : S_UP;
            if (st.state == S_DOWN && was != S_DOWN){ st.downSince = now; if (was != S_UNKNOWN) sh.wentDown |= (uint8_t)(1 << i); }
            if (st.state != S_DOWN && was == S_DOWN) sh.cameBack |= (uint8_t)(1 << i);
            if (!up){
                if (code[i] > 0) snprintf(st.why, sizeof(st.why), "HTTP %d", code[i]);
                else snprintf(st.why, sizeof(st.why), "NO ANSWER");
            } else st.why[0] = '\0';
            if (st.histN == HIST){ memmove(st.lat, st.lat + 1, sizeof(st.lat[0]) * (HIST - 1)); st.histN--; }
            st.lat[st.histN++] = up ? took[i] : 0xFFFF;
        }
        sh.version++;
        portEXIT_CRITICAL(&lock);
    } while (0);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static const uint16_t C_BG = MTB_KIT_RGB(4, 6, 10), C_TILE = MTB_KIT_RGB(16, 20, 30);
static void dot_(int x, int y, uint16_t c){ if (x >= 0 && y >= 0 && x < W && y < H) mtb_Panel_Draw_PixelRGB565((int16_t)x, (int16_t)y, c); }

static uint16_t state_Color(uint8_t s, bool offline){
    if (offline) return KIT_DIM;
    return s == S_UP ? KIT_GREEN : s == S_SLOW ? KIT_GOLD : s == S_DOWN ? KIT_RED : KIT_DIM;
}

static void fmt_Ms(char* t, size_t n, uint16_t ms){
    if (ms >= 10000) snprintf(t, n, "%us", ms / 1000);
    else if (ms >= 1000) snprintf(t, n, "%u.%us", ms / 1000, (ms % 1000) / 100);
    else snprintf(t, n, "%ums", ms);
}
static void fmt_Uptime(char* t, size_t n, const Upt_Site_t& s){
    if (!s.checks){ snprintf(t, n, "-"); return; }
    const unsigned p10 = (unsigned)((uint64_t)s.ups * 1000u / s.checks);
    if (p10 == 1000) snprintf(t, n, "100%%");
    else snprintf(t, n, "%u.%u%%", p10 / 10, p10 % 10);
}

/** Answer times from 0 (the bottom) to the slowest (the top); a down check is a red mark and breaks the line. */
static void spark(const Upt_Site_t& s, int x, int y, int w, int h, uint16_t color){
    if (s.histN < 2) return;
    uint16_t hi = 1;
    for (int i = 0; i < s.histN; i++) if (s.lat[i] != 0xFFFF && s.lat[i] > hi) hi = s.lat[i];
    int px = -1, py = 0;
    for (int i = 0; i < s.histN; i++){
        const int cx = x + i * (w - 1) / (s.histN - 1);
        if (s.lat[i] == 0xFFFF){ rect(cx, y, cx, y + h - 1, KIT_RED); px = -1; continue; }
        const int cy = y + h - 1 - (int)((uint32_t)s.lat[i] * (h - 1) / hi);
        rect(cx, cy + 1, cx, y + h - 1, mtb_Kit_Dim(color, 50));
        if (px >= 0) mtb_Panel_Draw_Line((int16_t)px, (int16_t)py, (int16_t)cx, (int16_t)cy, color);
        else dot_(cx, cy, color);
        px = cx; py = cy;
    }
}

static void header(const Upt_Shared_t& d, int used, uint32_t ms){
    rect(0, 0, W - 1, 9, MTB_KIT_RGB(14, 18, 28));
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_LABEL, "UPTIME", MTB_KIT_RGB(14, 18, 28));
    char t[40];
    int up = 0, known = 0;
    for (int i = 0; i < SITES; i++) if (save.url[i][0] && d.site[i].state != S_UNKNOWN){ known++; if (d.site[i].state != S_DOWN) up++; }
    uint16_t c = KIT_LABEL;
    if (d.offline){ snprintf(t, sizeof(t), "OFFLINE"); c = (ms / 500) % 2 ? KIT_ORANGE : KIT_DIM; }
    else if (!known) snprintf(t, sizeof(t), used ? "CHECKING..." : "");
    else { snprintf(t, sizeof(t), "%d OF %d UP", up, known); c = up == known ? KIT_GREEN : KIT_RED; }
    mtb_Kit_Text_Centre(W / 2, 1, Terminal6x8, c, t, MTB_KIT_RGB(14, 18, 28));
    const uint32_t now = (uint32_t)time(nullptr);
    if (d.roundAt && now >= d.roundAt){
        char ago[16];
        mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - d.roundAt));
        mtb_Kit_Upper(ago);
        mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, KIT_DIM, ago, MTB_KIT_RGB(14, 18, 28));
    }
}

static void tile(const Upt_Shared_t& d, int i, int x, int y, int w, int h){
    const Upt_Site_t& s = d.site[i];
    const uint16_t sc = state_Color(s.state, d.offline);
    rect(x, y, x + w - 1, y + h - 1, s.state == S_DOWN && !d.offline ? MTB_KIT_RGB(50, 10, 12) : C_TILE);
    const uint16_t bg = s.state == S_DOWN && !d.offline ? MTB_KIT_RGB(50, 10, 12) : C_TILE;
    rect(x, y, x + 2, y + h - 1, sc);
    char name[URL_LEN], fit[URL_LEN], t[24];
    short_Name(save.url[i], name, sizeof(name));
    mtb_Kit_Upper(name);
    const int sparkW = w >= 200 ? 90 : 46;
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, name, w - sparkW - 14);
    mtb_Kit_Text(x + 6, y + 3, Terminal6x8, KIT_TEXT, fit, bg);
    if (s.state == S_UNKNOWN) snprintf(t, sizeof(t), "%s", s.why[0] ? "ALLOW IT IN THE PHONE APP" : "CHECKING");
    else if (s.state == S_DOWN){
        char ago[16] = "";
        const uint32_t now = (uint32_t)time(nullptr);
        if (s.downSince && now >= s.downSince){ mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - s.downSince)); mtb_Kit_Upper(ago); }
        snprintf(t, sizeof(t), "DOWN %s", s.why);
        mtb_Kit_Text(x + 6, y + 14, Terminal4x6, KIT_RED, t, bg);
        if (ago[0] && h > 24) mtb_Kit_Text(x + 6, y + 21, Terminal4x6, KIT_LABEL, ago, bg);
        t[0] = '\0';
    } else {
        char up[12], lat[12];
        fmt_Uptime(up, sizeof(up), s);
        fmt_Ms(lat, sizeof(lat), s.ms);
        snprintf(t, sizeof(t), "%s  UP %s", lat, up);
    }
    if (t[0]) mtb_Kit_Text(x + 6, y + 14, Terminal4x6, s.state == S_SLOW ? KIT_GOLD : KIT_LABEL, t, bg);
    spark(s, x + w - sparkW - 4, y + 4, sparkW, h - 8, sc);
}

static void draw_All(const Upt_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    int used = 0, list[SITES];
    for (int i = 0; i < SITES; i++) if (save.url[i][0]) list[used++] = i;
    header(d, used, ms);
    if (!used){
        mtb_Kit_Text_Centre(W / 2, 54, Terminal8x12, KIT_TEXT, "NO WEBSITES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_LABEL, "ADD THEM IN THE PHONE APP", C_BG);
        return;
    }
    const int cols = used > 4 ? 2 : 1, rows = (used + cols - 1) / cols;
    const int tw = (W - (cols - 1) * 2) / cols, th = (H - 11 - (rows - 1) * 2) / rows;
    for (int k = 0; k < used; k++){
        const int c = k / rows, r = k % rows;
        tile(d, list[k], c * (tw + 2), 11 + r * (th + 2), tw, th > 34 ? 34 : th);
    }
}

static void draw_One(const Upt_Shared_t& d, int i){
    mtb_Panel_Fill_Screen(C_BG);
    const Upt_Site_t& s = d.site[i];
    const uint16_t sc = state_Color(s.state, d.offline);
    char name[URL_LEN], fit[URL_LEN], t[40];
    short_Name(save.url[i], name, sizeof(name));
    mtb_Kit_Upper(name);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, name, W - 8);
    mtb_Kit_Text(4, 2, Terminal6x8, KIT_TEXT, fit, C_BG);
    static const char* const NAME[] = {"CHECKING", "UP", "SLOW", "DOWN"};
    mtb_Kit_Text(4, 14, Terminal10x17, sc, d.offline ? "OFFLINE" : NAME[s.state], C_BG);
    char lat[12], up[12];
    fmt_Ms(lat, sizeof(lat), s.ms);
    fmt_Uptime(up, sizeof(up), s);
    if (s.checks){
        snprintf(t, sizeof(t), "ANSWERED IN %s", lat);
        if (s.state == S_DOWN) snprintf(t, sizeof(t), "%s", s.why);
        mtb_Kit_Text_Right(W - 4, 14, Terminal6x8, KIT_LABEL, t, C_BG);
        snprintf(t, sizeof(t), "UP %s OF %lu CHECKS", up, (unsigned long)s.checks);
        mtb_Kit_Text_Right(W - 4, 24, Terminal4x6, KIT_LABEL, t, C_BG);
    }
    rect(4, 40, W - 5, 112, C_TILE);
    spark(s, 8, 44, W - 16, 64, sc);
    if (s.histN < 2) mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_DIM, "THE HISTORY STARTS AFTER TWO CHECKS", C_TILE);
    uint16_t lo = 0xFFFF, hi = 0;
    for (int k = 0; k < s.histN; k++) if (s.lat[k] != 0xFFFF){ if (s.lat[k] < lo) lo = s.lat[k]; if (s.lat[k] > hi) hi = s.lat[k]; }
    if (hi){
        char a[12], b[12];
        fmt_Ms(a, sizeof(a), lo); fmt_Ms(b, sizeof(b), hi);
        snprintf(t, sizeof(t), "FASTEST %s  SLOWEST %s", a, b);
        mtb_Kit_Text(4, 117, Terminal4x6, KIT_DIM, t, C_BG);
    }
    mtb_Kit_Text_Right(W - 4, 117, Terminal4x6, KIT_DIM, "TURN: NEXT", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void alarm_Sound(bool down){
    static const Snd_Note_t DOWN[] = {{880, 160}, {0, 60}, {660, 160}, {0, 60}, {880, 160}, {0, 60}, {660, 300}};
    static const Snd_Note_t BACK[] = {{523, 100}, {659, 100}, {784, 200}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (down) snd_Play(DOWN, 7, 0.35f, true); else snd_Play(BACK, 3, 0.3f, false);
}

static void uptimeMonitor_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Site, cmd_Minutes, cmd_Sound, cmd_Check, cmd_Slow);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    worker_Begin();
    pxl_compat_add_background(worker_Step);
    appOpen = true;

    Upt_Shared_t* d = (Upt_Shared_t*)heap_caps_calloc(1, sizeof(Upt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, bannerUntil = 0;
    int view = -1;                      // -1 all, else a site
    char banner[48] = "";
    uint16_t bannerColor = KIT_RED;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; if (view >= 0 && !save.url[view][0]) view = -1; }
        const int t = turns;
        if (t){
            turns -= t;
            // All, then each site in turn.
            int k = view;
            for (int n = 0; n <= SITES; n++){
                k += t > 0 ? 1 : -1;
                if (k >= SITES) k = -1;
                if (k < -1) k = SITES - 1;
                if (k == -1 || save.url[k][0]) break;
            }
            view = k;
            redraw = true;
        }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; bannerUntil = ms + 1200; snprintf(banner, sizeof(banner), "CHECKING NOW"); bannerColor = KIT_CYAN; redraw = true; }

        uint8_t down = 0, back = 0;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        down = sh.wentDown; back = sh.cameBack; sh.wentDown = 0; sh.cameBack = 0;
        portEXIT_CRITICAL(&lock);
        if (down || back){
            const int i = __builtin_ctz(down ? down : back);
            char name[URL_LEN];
            short_Name(save.url[i], name, sizeof(name));
            mtb_Kit_Upper(name);
            snprintf(banner, sizeof(banner), "%.*s %s", 30, name, down ? "IS DOWN" : "IS BACK");
            bannerColor = down ? KIT_RED : KIT_GREEN;
            bannerUntil = ms + (down ? 8000 : 4000);
            alarm_Sound(down != 0);
        }

        if (redraw || ms - lastDraw >= 1000){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (view < 0) draw_All(*d, ms); else draw_One(*d, view);
            if (ms < bannerUntil){
                const bool on = bannerColor != KIT_RED || (ms / 400) % 2;
                const uint16_t bg = on ? bannerColor : MTB_KIT_RGB(40, 0, 0);
                mtb_Panel_Fill_Rect(10, 48, W - 11, 78, bg);
                char fit[48];
                mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, banner, W - 30);
                mtb_Kit_Text_Centre(W / 2, 57, Terminal8x12, on ? BLACK : bannerColor, fit, bg);
            }
            mtb_Panel_End_Update();
        }
        if (ms < bannerUntil && bannerColor == KIT_RED && ms - lastDraw >= 200) redraw = true;
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void upt_Values(JsonObject values){
    Upt_Save_t* s = (Upt_Save_t*)heap_caps_malloc(sizeof(Upt_Save_t), MALLOC_CAP_SPIRAM);
    Upt_Shared_t* d = (Upt_Shared_t*)heap_caps_malloc(sizeof(Upt_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    JsonArray rows = values["sites"].to<JsonArray>();
    char t[120];
    for (int i = 0; i < SITES; i++){
        char key[8];
        snprintf(key, sizeof(key), "site%d", i);
        values[key] = s->url[i];
        if (!s->url[i][0]) continue;
        const Upt_Site_t& st = d->site[i];
        JsonObject r = rows.add<JsonObject>();
        char name[URL_LEN];
        short_Name(s->url[i], name, sizeof(name));
        r["label"] = name;
        char lat[12], up[12];
        fmt_Ms(lat, sizeof(lat), st.ms);
        fmt_Uptime(up, sizeof(up), st);
        if (!appOpen){ r["value"] = "Checked while Uptime Monitor is open"; r["state"] = "off"; continue; }
        if (d->offline){ r["value"] = "Pixlpal is offline: no website answered"; r["state"] = "warn"; continue; }
        switch (st.state){
            case S_UP:   snprintf(t, sizeof(t), "Up, answered in %s, up %s of %lu checks", lat, up, (unsigned long)st.checks); r["state"] = "ok"; break;
            case S_SLOW: snprintf(t, sizeof(t), "Slow, answered in %s, up %s of %lu checks", lat, up, (unsigned long)st.checks); r["state"] = "warn"; break;
            case S_DOWN: snprintf(t, sizeof(t), "Down (%s), up %s of %lu checks", st.why, up, (unsigned long)st.checks); r["state"] = "error"; break;
            default:     snprintf(t, sizeof(t), "%s", st.why[0] ? "Not checked: allow this site when the phone app asks" : "Checking");
                         r["state"] = st.why[0] ? "warn" : "info"; break;
        }
        r["value"] = t;
    }
    values["minutes"] = s->minutes;
    values["sound"] = s->sound;
    values["slowMs"] = s->slowMs;
    heap_caps_free(s);
    heap_caps_free(d);
}
