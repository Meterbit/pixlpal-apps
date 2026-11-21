/**
 * Sleep Wind-Down: a bedtime routine on the bedroom's Pixlpal.
 *   - Day: the time, the bedtime and how long until it.
 *   - Wind-down (a chosen time before bed): a warm amber screen that grows dimmer towards bedtime, the minutes left,
 *     and now and then an idea for winding down; a soft chime as it begins.
 *   - Night: a dim red clock, or nothing; a click shows the time for a few seconds.
 *   - Sunrise (a chosen time before wake-up): the whole panel brightens from deep red through orange to warm white.
 *   - Morning: "good morning" for half an hour, with an optional gentle chime at wake-up.
 * Friday and Saturday nights may have their own times. Pixlpal's brightness setting is never changed: the app draws
 * dimmer colours instead (the setting is saved to flash, and is the owner's). No internet; NVS "swdSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "swd_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "swdSave"
#define SAVE_MAGIC  0x53574401u
#define MORNING_MIN 30
#define PEEK_MS     6000u

EXT_RAM_BSS_ATTR TaskHandle_t sleepWindDown_Task_H = NULL;
static void sleepWindDown_App_Task(void* dApplication);
static void swd_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* sleepWindDown_App;
Mtb_Applications* sleepWindDown_App_GetInstance(){
    if (!sleepWindDown_App){
        sleepWindDown_App = new Mtb_Applications_FullScreen(sleepWindDown_App_Task, &sleepWindDown_Task_H, "Sleep Wind-Down", {0,0}, 8192);
        sleepWindDown_App->mtb_App_Set_Mobile_UI(SWD_UI_MANIFEST, SWD_UI_API, nullptr, swd_Values);
    }
    return sleepWindDown_App;
}
MTB_REGISTER_APP(sleepWindDown_App, 0, 0)       // the catalogue gives the app its number

struct Swd_Save_t {
    uint32_t magic;
    uint16_t bed, wake, wbed, wwake;    // minutes of the day
    uint8_t weekend, windDown, sunrise, night, chime, windChime;
};

static EXT_RAM_BSS_ATTR Swd_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool phoneChanged = false, appOpen = false;

static void read_Save(Swd_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC; s.bed = 22 * 60 + 30; s.wake = 6 * 60 + 30; s.wbed = 23 * 60 + 30; s.wwake = 8 * 60;
        s.windDown = 60; s.sunrise = 20; s.windChime = 1;
    }
    if (s.bed >= 1440) s.bed = 22 * 60 + 30;
    if (s.wake >= 1440) s.wake = 6 * 60 + 30;
    if (s.wbed >= 1440) s.wbed = 23 * 60 + 30;
    if (s.wwake >= 1440) s.wwake = 8 * 60;
    if (s.windDown < 15 || s.windDown > 120) s.windDown = 60;
    if (s.sunrise > 45) s.sunrise = 20;
}

// The schedule ---------------------------------------------------------------------------------------------------------------------
// Times are minutes on one local timeline: day number * 1440 + the minute. The night "of" day X is the one that
// begins with that evening's bedtime (a bedtime after midnight, before noon, belongs to the evening before).
enum Swd_State_t : uint8_t { S_DAY = 0, S_WIND, S_NIGHT, S_SUNRISE, S_MORNING };

static int weekday_Of(int32_t day){ return (int)(((day + 4) % 7 + 7) % 7); }       // 1970-01-01 was a Thursday

static void night_Of(const Swd_Save_t& s, int32_t x, int64_t& bed, int64_t& wake){
    const int wd = weekday_Of(x);
    const bool weekendNight = s.weekend && (wd == 5 || wd == 6);                    // Friday and Saturday nights
    const uint16_t b = weekendNight ? s.wbed : s.bed, w = weekendNight ? s.wwake : s.wake;
    bed = (int64_t)x * 1440 + b + (b < 12 * 60 ? 1440 : 0);
    wake = (int64_t)(x + 1) * 1440 + w;
    while (wake <= bed) wake += 1440;
    while (wake - bed > 1440) wake -= 1440;
}

struct Swd_Now_t {
    Swd_State_t state;
    float fraction;                     // through the wind-down or the sunrise (0-1)
    int64_t bed, wake;                  // the night in question (or, in the day, the next one)
    int64_t now;                        // minutes, with the seconds below
    int seconds;
};

static bool schedule(const Swd_Save_t& s, Swd_Now_t& n){
    const time_t t = time(nullptr);
    if (t < 1700000000) return false;
    struct tm l;
    localtime_r(&t, &l);
    const int32_t day = mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    n.now = (int64_t)day * 1440 + l.tm_hour * 60 + l.tm_min;
    n.seconds = l.tm_sec;
    for (int32_t x = day - 1; x <= day + 1; x++){
        int64_t bed, wake;
        night_Of(s, x, bed, wake);
        const int64_t windFrom = bed - s.windDown, sunFrom = wake - s.sunrise;
        if (n.now >= windFrom && n.now < wake + MORNING_MIN){
            n.bed = bed; n.wake = wake;
            if (n.now < bed){ n.state = S_WIND; n.fraction = (float)(n.now - windFrom) / s.windDown; }
            else if (n.now < sunFrom){ n.state = S_NIGHT; n.fraction = 0; }
            else if (n.now < wake){ n.state = S_SUNRISE; n.fraction = s.sunrise ? (float)(n.now - sunFrom) / s.sunrise : 1; }
            else { n.state = S_MORNING; n.fraction = 0; }
            return true;
        }
        if (n.now < windFrom){ n.state = S_DAY; n.bed = bed; n.wake = wake; n.fraction = 0; return true; }
    }
    n.state = S_DAY; n.fraction = 0; n.bed = n.wake = 0;
    return true;
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Times(JsonDocument& c, const char* bk, const char* wk, uint16_t& bed, uint16_t& wake){
    const int b = mtb_Kit_Parse_Clock(c[bk] | ""), w = mtb_Kit_Parse_Clock(c[wk] | "");
    if (b < 0 || w < 0 || b >= 1440 || w >= 1440 || b == w){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); bed = (uint16_t)b; wake = (uint16_t)w; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Week(JsonDocument& c){ cmd_Times(c, "bed", "wake", save.bed, save.wake); }
static void cmd_Weekend_Times(JsonDocument& c){ cmd_Times(c, "wbed", "wwake", save.wbed, save.wwake); }
static void cmd_Byte(JsonDocument& c, const char* key, long low, long high, uint8_t& field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], low, high, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    field = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Weekend(JsonDocument& c){ cmd_Byte(c, "weekend", 0, 1, save.weekend); }
static void cmd_Wind(JsonDocument& c){ cmd_Byte(c, "windDown", 15, 120, save.windDown); }
static void cmd_Sunrise(JsonDocument& c){ cmd_Byte(c, "sunrise", 0, 45, save.sunrise); }
static void cmd_Night(JsonDocument& c){ cmd_Byte(c, "night", 0, 1, save.night); }
static void cmd_Chimes(JsonDocument& c){
    if (!c["chime"].isNull()) cmd_Byte(c, "chime", 0, 1, save.chime);
    else if (!c["windChime"].isNull()) cmd_Byte(c, "windChime", 0, 1, save.windChime);
    else mtb_Ble_App_Cmd_Reject();
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const char* const TIPS[] = {
    "SCREENS AWAY", "LIGHTS LOW", "A WARM DRINK, NO CAFFEINE", "WRITE DOWN TOMORROW'S LIST", "A FEW SLOW BREATHS",
    "A FEW PAGES OF A BOOK", "A COOL, DARK ROOM", "GENTLE STRETCHES",
};
#define TIP_COUNT (int)(sizeof(TIPS) / sizeof(TIPS[0]))

static void clock_Text(char* t, size_t n, int minuteOfDay, bool ampmSmall){
    const int h = minuteOfDay / 60 % 24, m = minuteOfDay % 60;
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", h, m);
    else snprintf(t, n, "%d:%02d%s", h % 12 ? h % 12 : 12, m, ampmSmall ? "" : (h < 12 ? " AM" : " PM"));
}

static void left_Text(char* t, size_t n, int64_t minutes){
    if (minutes >= 60) snprintf(t, n, "%ldH %02ldM", (long)(minutes / 60), (long)(minutes % 60));
    else snprintf(t, n, "%ld MIN", (long)minutes);
}

static uint16_t scaled(uint8_t r, uint8_t g, uint8_t b, float f){
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return MTB_KIT_RGB((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

static void draw(const Swd_Save_t& s, const Swd_Now_t& n, bool peek, uint32_t ms){
    char t[48], u[48];
    const int minute = (int)(((n.now % 1440) + 1440) % 1440);
    switch (n.state){
    case S_DAY: {
        const uint16_t bg = MTB_KIT_RGB(6, 8, 20);
        mtb_Panel_Fill_Screen(bg);
        clock_Text(t, sizeof(t), minute, true);
        mtb_Kit_Text_Centre(W / 2, 14, PT_Sans_Narrow_Bold28x34, KIT_TEXT, t, bg);
        if (n.bed){
            clock_Text(u, sizeof(u), (int)(n.bed % 1440), false);
            snprintf(t, sizeof(t), "BEDTIME %s", u);
            mtb_Kit_Text_Centre(W / 2, 58, Terminal8x12, MTB_KIT_RGB(170, 150, 255), t, bg);
            left_Text(u, sizeof(u), n.bed - n.now);
            snprintf(t, sizeof(t), "IN %s", u);
            mtb_Kit_Text_Centre(W / 2, 74, Terminal6x8, KIT_LABEL, t, bg);
            clock_Text(u, sizeof(u), (int)(n.wake % 1440), false);
            snprintf(t, sizeof(t), "WAKE-UP %s", u);
            mtb_Kit_Text_Centre(W / 2, 100, Terminal6x8, KIT_DIM, t, bg);
        }
        break; }
    case S_WIND: {
        // Amber, dimming from about full to a fifth as bedtime nears.
        const float f = 1.0f - 0.8f * n.fraction;
        const uint16_t bg = scaled(40, 16, 0, f), ink = scaled(255, 170, 70, f), soft = scaled(200, 110, 40, f);
        mtb_Panel_Fill_Screen(bg);
        mtb_Kit_Text_Centre(W / 2, 8, Terminal8x12, soft, "TIME TO WIND DOWN", bg);
        left_Text(u, sizeof(u), n.bed - n.now);
        snprintf(t, sizeof(t), "BED IN %s", u);
        mtb_Kit_Text_Centre(W / 2, 30, PT_Sans_Narrow_Bold18x22, ink, t, bg);
        clock_Text(u, sizeof(u), minute, false);
        mtb_Kit_Text_Centre(W / 2, 58, Terminal8x12, soft, u, bg);
        const int tip = (int)((n.now / 3) % TIP_COUNT);                 // a new idea every three minutes
        mtb_Kit_Text_Centre(W / 2, 92, Terminal6x8, ink, TIPS[tip], bg);
        mtb_Kit_Bar(40, 112, W - 80, 2, n.fraction, soft, scaled(70, 30, 0, f));
        break; }
    case S_NIGHT:
        mtb_Panel_Fill_Screen(BLACK);
        if (s.night == 0 || peek){
            clock_Text(t, sizeof(t), minute, true);
            const uint16_t c = peek ? MTB_KIT_RGB(120, 10, 0) : MTB_KIT_RGB(45, 0, 0);
            mtb_Kit_Text_Centre(W / 2, 46, PT_Sans_Narrow_Bold28x34, c, t, BLACK);
            if (peek){
                clock_Text(u, sizeof(u), (int)(n.wake % 1440), false);
                snprintf(t, sizeof(t), "WAKE-UP %s", u);
                mtb_Kit_Text_Centre(W / 2, 88, Terminal6x8, MTB_KIT_RGB(80, 10, 0), t, BLACK);
            }
        }
        break;
    case S_SUNRISE: {
        // Deep red, to orange, to warm white; the bottom of the panel a little ahead, like a horizon.
        for (int y = 0; y < H; y += 4){
            float f = n.fraction * 1.15f - 0.15f * (float)y / H + 0.15f;
            if (f > 1) f = 1;
            if (f < 0) f = 0;
            uint8_t r, g, b;
            if (f < 0.4f){ const float k = f / 0.4f; r = (uint8_t)(20 + 160 * k); g = (uint8_t)(10 * k); b = 0; }
            else if (f < 0.75f){ const float k = (f - 0.4f) / 0.35f; r = (uint8_t)(180 + 75 * k); g = (uint8_t)(10 + 110 * k); b = (uint8_t)(10 * k); }
            else { const float k = (f - 0.75f) / 0.25f; r = 255; g = (uint8_t)(120 + 100 * k); b = (uint8_t)(10 + 130 * k); }
            mtb_Panel_Fill_Rect(0, H - 1 - y - 3, W - 1, H - 1 - y, MTB_KIT_RGB(r, g, b));
        }
        if (peek){ clock_Text(t, sizeof(t), minute, true); mtb_Kit_Text_Centre(W / 2, 46, PT_Sans_Narrow_Bold28x34, BLACK, t, MTB_KIT_RGB(255, 140, 40)); }
        break; }
    case S_MORNING: {
        const uint16_t bg = MTB_KIT_RGB(255, 214, 140);
        mtb_Panel_Fill_Screen(bg);
        mtb_Kit_Text_Centre(W / 2, 16, PT_Sans_Narrow_Bold18x22, MTB_KIT_RGB(90, 40, 0), "GOOD MORNING", bg);
        clock_Text(t, sizeof(t), minute, true);
        mtb_Kit_Text_Centre(W / 2, 46, PT_Sans_Narrow_Bold28x34, MTB_KIT_RGB(60, 25, 0), t, bg);
        static const char* const DAY[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
        mtb_Kit_Text_Centre(W / 2, 92, Terminal8x12, MTB_KIT_RGB(120, 60, 10), DAY[weekday_Of((int32_t)(n.now / 1440))], bg);
        break; }
    }
    (void)ms;
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chime(bool morning){
    static const Snd_Note_t WIND[] = {{523, 300}, {392, 500}};
    static const Snd_Note_t WAKE[] = {{392, 200}, {494, 200}, {587, 200}, {784, 600}};
    if (mtb_Silent_Mode()) return;
    if (morning) snd_Play(WAKE, 4, 0.18f, false); else snd_Play(WIND, 2, 0.12f, false);
}

static void sleepWindDown_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Week, cmd_Weekend_Times, cmd_Weekend, cmd_Wind, cmd_Sunrise, cmd_Night, cmd_Chimes);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; turns = 0; phoneChanged = false;
    appOpen = true;
    Swd_State_t was = (Swd_State_t)0xFF;
    uint32_t lastDraw = 0, pushedMs = 0, peekUntil = 0;
    int lastMinute = -1;
    bool push = true, saveNow = false, redraw = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = redraw = true; }
        Swd_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        Swd_Now_t n = {};
        const bool known = schedule(s, n);
        if (clickRequested){ clickRequested = false; peekUntil = ms + PEEK_MS; redraw = true; }
        const int t = turns;
        if (t){ turns -= t; if (known && (n.state == S_DAY || n.state == S_MORNING)) for (int k = 0; k < (t > 0 ? t : -t); k++) mtb_Brightness_Control(t > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE); else { peekUntil = ms + PEEK_MS; redraw = true; } }
        if (known && n.state != was){
            // Chimes as the wind-down begins and at wake-up, only when the state is reached while open (not on opening).
            if (was != (Swd_State_t)0xFF){
                if (n.state == S_WIND && s.windChime) chime(false);
                if (n.state == S_MORNING && s.chime) chime(true);
            }
            was = n.state; push = redraw = true;
        }
        const bool peek = (int32_t)(peekUntil - ms) > 0;
        const int minuteNow = known ? (int)(n.now % 1440) : -1;
        if (minuteNow != lastMinute){ lastMinute = minuteNow; redraw = true; push = true; }
        const bool moving = known && (n.state == S_SUNRISE || n.state == S_WIND) && ms - lastDraw >= 2000u;
        if (redraw || moving || (peekUntil && !peek)){
            if (peekUntil && !peek) peekUntil = 0;
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (!known){ mtb_Panel_Fill_Screen(BLACK); mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, KIT_LABEL, "WAITING FOR THE TIME", BLACK); }
            else draw(s, n, peek, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 2000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(200);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void swd_Values(JsonObject values){
    Swd_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    Swd_Now_t n = {};
    char t[96], a[16], b[16];
    if (schedule(s, n) && n.bed){
        static const char* const WORD[] = {"Day", "Winding down", "Night", "Sunrise", "Morning"};
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Now"; o["value"] = WORD[n.state]; o["state"] = n.state == S_DAY ? "info" : "ok";
        clock_Text(a, sizeof(a), (int)(n.bed % 1440), false);
        clock_Text(b, sizeof(b), (int)(n.wake % 1440), false);
        o = rows.add<JsonObject>();
        o["label"] = n.state == S_DAY || n.state == S_WIND ? "Tonight" : "This night";
        snprintf(t, sizeof(t), "Bed %s, wake-up %s (%ld h %02ld min)", a, b, (long)((n.wake - n.bed) / 60), (long)((n.wake - n.bed) % 60));
        o["value"] = t; o["state"] = "info";
        if (!appOpen){ o = rows.add<JsonObject>(); o["label"] = "Pixlpal"; o["value"] = "Leave Sleep Wind-Down open in the bedroom"; o["state"] = "off"; }
    }
    char bed[8], wake[8], wbed[8], wwake[8];
    snprintf(bed, sizeof(bed), "%02u:%02u", s.bed / 60, s.bed % 60);
    snprintf(wake, sizeof(wake), "%02u:%02u", s.wake / 60, s.wake % 60);
    snprintf(wbed, sizeof(wbed), "%02u:%02u", s.wbed / 60, s.wbed % 60);
    snprintf(wwake, sizeof(wwake), "%02u:%02u", s.wwake / 60, s.wwake % 60);
    values["bed"] = bed; values["wake"] = wake; values["wbed"] = wbed; values["wwake"] = wwake;
    values["weekend"] = s.weekend; values["windDown"] = s.windDown; values["sunrise"] = s.sunrise;
    values["night"] = s.night; values["chime"] = s.chime; values["windChime"] = s.windChime;
}
