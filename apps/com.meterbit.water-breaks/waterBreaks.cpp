/**
 * Water & Breaks: a desk companion. Glasses of water against a daily goal (a knob click for each), a glass that fills
 * on the panel, the last seven days and the days in a row the goal was met; and, within the owner's working hours,
 * reminders: stand up and stretch every so often, 20-20-20 eye breaks (look 20 feet away for 20 seconds, counted on
 * the panel), and a glass of water after a while without one. A reminder fills the panel, sounds softly, and goes by
 * itself after a minute (an eye break after its 20 seconds); a click says done.
 *
 * No internet. Everything is saved on Pixlpal (NVS "wbSave"): the settings, today's glasses and the last week.
 * Knob: click, a glass (or done, during a reminder); double click, take the last glass back; turn, brightness.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "wb_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "wbSave"
#define SAVE_MAGIC  0x57424B01u
#define DAYS        7
#define REMIND_MS   60000u          // a reminder goes by itself after this
#define EYE_SECS    20

EXT_RAM_BSS_ATTR TaskHandle_t waterBreaks_Task_H = NULL;
static void waterBreaks_App_Task(void* dApplication);
static void wb_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* waterBreaks_App;
Mtb_Applications* waterBreaks_App_GetInstance(){
    if (!waterBreaks_App){
        waterBreaks_App = new Mtb_Applications_FullScreen(waterBreaks_App_Task, &waterBreaks_Task_H, "Water & Breaks", {0,0}, 8192);
        waterBreaks_App->mtb_App_Set_Mobile_UI(WB_UI_MANIFEST, WB_UI_API, nullptr, wb_Values);
    }
    return waterBreaks_App;
}
MTB_REGISTER_APP(waterBreaks_App, 0, 0)       // the catalogue gives the app its number

struct Wb_Save_t {
    uint32_t magic;
    uint8_t goal, days, eyes, sound;
    uint16_t ml, from, to;              // from, to: minutes of the day
    uint16_t stand, water;              // minutes; 0 none
    int32_t today;                      // the day (days since 1970, local) the count is for
    uint8_t count;
    uint8_t history[DAYS];              // the days before today, most recent first
    uint16_t streak;                    // days in a row before today with the goal met
    uint32_t lastGlass;                 // Unix time of the last glass
};

static EXT_RAM_BSS_ATTR Wb_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool phoneChanged = false, appOpen = false;
static volatile int glassRequested = 0;     // +1, -1 from the phone or the knob

static void defaults(Wb_Save_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = SAVE_MAGIC; s.goal = 8; s.ml = 250; s.from = 9 * 60; s.to = 17 * 60; s.days = 1;
    s.stand = 45; s.eyes = 1; s.water = 90; s.sound = 1;
}

static void read_Save(Wb_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC) defaults(s);
    if (s.goal < 4 || s.goal > 16) s.goal = 8;
    if (s.ml < 100 || s.ml > 500) s.ml = 250;
    if (s.from >= 1440) s.from = 9 * 60;
    if (s.to >= 1440) s.to = 17 * 60;
    if (s.days > 2) s.days = 1;
    if (s.stand > 120) s.stand = 45;
    if (s.water > 240) s.water = 90;
}

// The local day and minute.
static bool local_Now(int32_t& day, int& minute, int& weekday){
    const time_t now = time(nullptr);
    if (now < 1700000000) return false;
    struct tm l;
    localtime_r(&now, &l);
    day = mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    minute = l.tm_hour * 60 + l.tm_min;
    weekday = l.tm_wday;
    return true;
}

static bool working_Now(const Wb_Save_t& s){
    int32_t day; int minute, wd;
    if (!local_Now(day, minute, wd)) return false;
    if (s.days == 1 && (wd == 0 || wd == 6)) return false;
    if (s.days == 2 && wd == 0) return false;
    if (s.from == s.to) return true;
    return s.from < s.to ? minute >= s.from && minute < s.to : minute >= s.from || minute < s.to;     // overnight shifts too
}

/** A new day: today's count joins the history. */
static bool roll_Day(Wb_Save_t& s){
    int32_t day; int minute, wd;
    if (!local_Now(day, minute, wd) || day == s.today) return false;
    if (s.today && day > s.today){
        int32_t gap = day - s.today;
        // The streak: today's count met the goal, and no whole day was skipped.
        if (s.count >= s.goal && gap == 1) s.streak++;
        else s.streak = 0;
        for (int32_t k = 0; k < gap && k < DAYS; k++){
            memmove(s.history + 1, s.history, DAYS - 1);
            s.history[0] = k == 0 ? s.count : 0;
        }
    }
    s.today = day;
    s.count = 0;
    return true;
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Glass(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["glass"], -1, 1, v) || v == 0){ mtb_Ble_App_Cmd_Reject(); return; }
    if (v < 0 && save.count == 0 && glassRequested <= 0){ mtb_Ble_App_Cmd_Reject(); return; }
    if (appOpen) glassRequested += (int)v;
    else {                                                          // not open: counted straight into the save
        portENTER_CRITICAL(&lock);
        roll_Day(save);
        if (v > 0 && save.count < 99){ save.count++; save.lastGlass = (uint32_t)time(nullptr); }
        else if (v < 0 && save.count) save.count--;
        portEXIT_CRITICAL(&lock);
        mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    }
    phoneChanged = true;
}
static void cmd_Byte(JsonDocument& c, const char* key, long low, long high, uint8_t& field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], low, high, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    field = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Goal(JsonDocument& c){ cmd_Byte(c, "goal", 4, 16, save.goal); }
static void cmd_Hours(JsonDocument& c){
    const int from = mtb_Kit_Parse_Clock(c["from"] | ""), to = mtb_Kit_Parse_Clock(c["to"] | "");
    if (from < 0 || to < 0 || from >= 1440 || to >= 1440){ mtb_Ble_App_Cmd_Reject(); return; }
    save.from = (uint16_t)from; save.to = (uint16_t)to;
    phoneChanged = true;
}
static void cmd_Days(JsonDocument& c){ cmd_Byte(c, "days", 0, 2, save.days); }
static void cmd_Stand(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["stand"], 0, 120, v) || (v && v < 15)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.stand = (uint16_t)v;
    phoneChanged = true;
}
static void cmd_Eyes(JsonDocument& c){ cmd_Byte(c, "eyes", 0, 1, save.eyes); }
static void cmd_Water(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["water"], 0, 240, v) || (v && v < 30)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.water = (uint16_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){ cmd_Byte(c, "sound", 0, 1, save.sound); }
static void cmd_Ml(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["ml"], 100, 500, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.ml = (uint16_t)v;
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile uint8_t clicks = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicks = (uint8_t)b.count; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(4, 10, 18), C_WATER = MTB_KIT_RGB(40, 150, 255), C_GLASS = MTB_KIT_RGB(150, 190, 220);
static const uint16_t C_STAND = MTB_KIT_RGB(255, 160, 40), C_EYES = MTB_KIT_RGB(120, 220, 140), C_PANEL = MTB_KIT_RGB(14, 22, 34);

enum Remind_t : uint8_t { R_NONE = 0, R_STAND, R_EYES, R_WATER };

static void litres(char* t, size_t n, uint16_t ml){
    if (ml < 1000) snprintf(t, n, "%u ML", (unsigned)ml);
    else snprintf(t, n, "%u.%02u L", (unsigned)(ml / 1000), (unsigned)(ml % 1000 / 10));
}

// A glass, filled to @p f (0-1), with a wave on the water.
static void glass(int x, int y, int w, int h, float f, uint32_t ms){
    if (f > 1) f = 1;
    const int inset = 6;                        // the glass narrows towards the bottom
    const int water = (int)((h - 4) * f);
    for (int k = 0; k < water; k++){
        const int yy = y + h - 3 - k;
        const int in = 2 + inset * (h - (yy - y)) / h;
        int top = 0;
        if (k == water - 1) top = ((ms / 120 + yy) % 6 < 3) ? 1 : 0;
        mtb_Panel_Fill_Rect(x + in, yy - top, x + w - 1 - in, yy, k > water - 3 ? MTB_KIT_RGB(120, 200, 255) : C_WATER);
    }
    mtb_Panel_Draw_Line(x, y, x + inset, y + h - 1, C_GLASS);
    mtb_Panel_Draw_Line(x + w - 1, y, x + w - 1 - inset, y + h - 1, C_GLASS);
    mtb_Panel_Draw_Line(x + inset, y + h - 1, x + w - 1 - inset, y + h - 1, C_GLASS);
}

static void until_Text(char* t, size_t n, const char* what, uint32_t dueMs, uint32_t ms){
    const int32_t left = (int32_t)(dueMs - ms);
    if (left <= 0) snprintf(t, n, "%s NOW", what);
    else snprintf(t, n, "%s IN %ld MIN", what, (long)((left + 59999) / 60000));
}

static void draw_Main(const Wb_Save_t& s, bool working, uint32_t standDue, uint32_t eyesDue, uint32_t ms, uint32_t plusSince){
    mtb_Panel_Fill_Screen(C_BG);
    char t[64];
    const float f = (float)s.count / s.goal;
    glass(8, 14, 54, 100, f, ms);
    if (plusSince && ms - plusSince < 1200){
        const int rise = (int)((ms - plusSince) / 40);
        mtb_Kit_Text_Centre(35, 40 - rise, Terminal8x12, KIT_TEXT, "+1", C_BG);
    }
    // The count.
    snprintf(t, sizeof(t), "%u", (unsigned)s.count);
    const int16_t x = mtb_Kit_Text(74, 10, PT_Sans_Narrow_Bold28x34, s.count >= s.goal ? KIT_GREEN : KIT_TEXT, t, C_BG);
    snprintf(t, sizeof(t), "OF %u", (unsigned)s.goal);
    mtb_Kit_Text(x + 4, 20, Terminal8x12, KIT_LABEL, t, C_BG);
    char l[16];
    litres(l, sizeof(l), (uint16_t)(s.count * s.ml));
    snprintf(t, sizeof(t), "%s  %s", s.count >= s.goal ? "GOAL MET!" : "GLASSES TODAY", l);
    mtb_Kit_Text(74, 46, Terminal6x8, s.count >= s.goal ? KIT_GREEN : KIT_LABEL, t, C_BG);
    // Breaks.
    if (!working) mtb_Kit_Text(74, 62, Terminal6x8, KIT_DIM, "BREAKS: OUTSIDE WORKING HOURS", C_BG);
    else {
        int y = 62;
        if (s.stand){ until_Text(t, sizeof(t), "STAND UP", standDue, ms); mtb_Kit_Text(74, y, Terminal6x8, C_STAND, t, C_BG); y += 11; }
        if (s.eyes){ until_Text(t, sizeof(t), "EYE BREAK", eyesDue, ms); mtb_Kit_Text(74, y, Terminal6x8, C_EYES, t, C_BG); y += 11; }
        if (!s.stand && !s.eyes) mtb_Kit_Text(74, y, Terminal6x8, KIT_DIM, "NO BREAKS SET", C_BG);
    }
    // The last week: a bar a day, oldest on the left, today on the right.
    const int bx = 74, by = 92, bh = 24, bw = 18;
    for (int k = 0; k <= DAYS; k++){
        const uint8_t v = k == DAYS ? s.count : s.history[DAYS - 1 - k];
        const int x0 = bx + k * (bw + 4);
        const int hh = v >= s.goal ? bh : bh * v / s.goal;
        mtb_Panel_Fill_Rect(x0, by, x0 + bw - 1, by + bh - 1, C_PANEL);
        if (hh) mtb_Panel_Fill_Rect(x0, by + bh - hh, x0 + bw - 1, by + bh - 1, v >= s.goal ? KIT_GREEN : (k == DAYS ? C_WATER : MTB_KIT_RGB(30, 90, 160)));
    }
    mtb_Kit_Text(bx, by + bh + 2, Terminal4x6, KIT_DIM, "LAST 7 DAYS", C_BG);
    if (s.streak){ snprintf(t, sizeof(t), "%u DAYS IN A ROW", (unsigned)s.streak); mtb_Kit_Text_Right(W - 4, by + bh + 2, Terminal4x6, KIT_GREEN, t, C_BG); }
    else mtb_Kit_Text_Right(W - 4, by + bh + 2, Terminal4x6, KIT_DIM, "TODAY", C_BG);
}

static void draw_Reminder(Remind_t r, uint32_t since, uint32_t ms){
    const uint16_t c = r == R_STAND ? C_STAND : r == R_EYES ? C_EYES : C_WATER;
    const uint16_t bg = mtb_Kit_Dim(c, 40);
    mtb_Panel_Fill_Screen(bg);
    mtb_Panel_Fill_Rect(0, 0, W - 1, 3, c);
    mtb_Panel_Fill_Rect(0, H - 4, W - 1, H - 1, c);
    if (r == R_STAND){
        mtb_Kit_Text_Centre(W / 2, 18, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "STAND UP AND STRETCH", bg);
        mtb_Kit_Text_Centre(W / 2, 50, Terminal6x8, KIT_TEXT, "ROLL YOUR SHOULDERS, REACH UP,", bg);
        mtb_Kit_Text_Centre(W / 2, 62, Terminal6x8, KIT_TEXT, "WALK A FEW STEPS", bg);
    } else if (r == R_EYES){
        const int32_t left = EYE_SECS - (int32_t)((ms - since) / 1000);
        mtb_Kit_Text_Centre(W / 2, 12, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "EYE BREAK", bg);
        mtb_Kit_Text_Centre(W / 2, 38, Terminal6x8, KIT_TEXT, "LOOK AT SOMETHING 20 FEET (6 M) AWAY", bg);
        char t[8];
        snprintf(t, sizeof(t), "%ld", (long)(left > 0 ? left : 0));
        mtb_Kit_Text_Centre(W / 2, 52, PT_Sans_Narrow_Bold28x34, c, t, bg);
        mtb_Kit_Bar(48, 92, W - 96, 4, (float)(ms - since) / (EYE_SECS * 1000.0f), c, mtb_Kit_Dim(c, 90));
    } else {
        mtb_Kit_Text_Centre(W / 2, 18, PT_Sans_Narrow_Bold18x22, KIT_TEXT, "A GLASS OF WATER?", bg);
        mtb_Kit_Text_Centre(W / 2, 50, Terminal6x8, KIT_TEXT, "CLICK THE KNOB WHEN YOU'VE HAD ONE", bg);
    }
    if (r != R_EYES) mtb_Kit_Text_Centre(W / 2, 106, Terminal4x6, KIT_LABEL, "CLICK: DONE", bg);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chime(Remind_t r){
    static const Snd_Note_t STAND[] = {{523, 120}, {659, 120}, {784, 200}};
    static const Snd_Note_t EYES[] = {{880, 100}, {0, 60}, {880, 100}};
    static const Snd_Note_t WATER[] = {{698, 90}, {880, 160}};
    static const Snd_Note_t DONE[] = {{1047, 100}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (r == R_STAND) snd_Play(STAND, 3, 0.25f, false);
    else if (r == R_EYES) snd_Play(EYES, 3, 0.2f, false);
    else if (r == R_WATER) snd_Play(WATER, 2, 0.25f, false);
    else snd_Play(DONE, 1, 0.2f, false);
}

static void waterBreaks_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Glass, cmd_Goal, cmd_Hours, cmd_Days, cmd_Stand, cmd_Eyes, cmd_Water, cmd_Sound, cmd_Ml);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clicks = 0; glassRequested = 0; phoneChanged = false;
    appOpen = true;
    bool saveNow = roll_Day(save), redraw = true, push = true, wasWorking = false;
    uint32_t ms0 = millis();
    uint32_t standDue = ms0 + save.stand * 60000u, eyesDue = ms0 + 20u * 60000u, lastDraw = 0, pushedMs = 0, plusSince = 0, lastRoll = 0;
    Remind_t remind = R_NONE;
    uint32_t remindSince = 0, waterAskedFor = 0;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (ms - lastRoll >= 10000u){ lastRoll = ms; portENTER_CRITICAL(&lock); if (roll_Day(save)){ saveNow = push = redraw = true; } portEXIT_CRITICAL(&lock); }
        const bool working = working_Now(save);
        if (working && !wasWorking){ standDue = ms + save.stand * 60000u; eyesDue = ms + 20u * 60000u; }   // the day's first break comes after a while
        wasWorking = working;
        // The knob.
        const uint8_t c = clicks;
        if (c){
            clicks = 0;
            if (remind != R_NONE && c == 1){
                if (remind == R_WATER) glassRequested++;
                if (remind == R_STAND) standDue = ms + save.stand * 60000u;
                if (remind == R_EYES) eyesDue = ms + 20u * 60000u;
                remind = R_NONE; redraw = true;
            } else if (c == 1) glassRequested++;
            else if (c == 2 && save.count) glassRequested--;
        }
        // Glasses.
        const int g = glassRequested;
        if (g){
            glassRequested -= g;
            portENTER_CRITICAL(&lock);
            if (g > 0){ save.count = (uint8_t)(save.count + g > 99 ? 99 : save.count + g); save.lastGlass = (uint32_t)time(nullptr); }
            else save.count = (uint8_t)(save.count + g < 0 ? 0 : save.count + g);
            portEXIT_CRITICAL(&lock);
            if (g > 0){ plusSince = ms ? ms : 1; chime(R_NONE); }
            if (remind == R_WATER){ remind = R_NONE; }
            saveNow = push = redraw = true;
        }
        // Reminders, within working hours, one at a time.
        if (working && remind == R_NONE){
            const uint32_t now = (uint32_t)time(nullptr);
            Remind_t next = R_NONE;
            if (save.eyes && (int32_t)(ms - eyesDue) >= 0) next = R_EYES;
            else if (save.stand && (int32_t)(ms - standDue) >= 0) next = R_STAND;
            else if (save.water && now > 1700000000u && save.count < save.goal){
                const uint32_t since = save.lastGlass && save.lastGlass + 86400u > now ? save.lastGlass : 0;
                const uint32_t start = since ? since : now - (uint32_t)(ms - ms0) / 1000u;     // no glass yet: since the app opened
                if (now - start >= save.water * 60u && waterAskedFor != start){ next = R_WATER; waterAskedFor = start; }
            }
            if (next != R_NONE){ remind = next; remindSince = ms; chime(next); redraw = true; }
        }
        if (remind != R_NONE){
            const uint32_t limit = remind == R_EYES ? EYE_SECS * 1000u : REMIND_MS;
            if (ms - remindSince >= limit){
                if (remind == R_EYES){ eyesDue = ms + 20u * 60000u; chime(R_NONE); }
                if (remind == R_STAND) standDue = ms + save.stand * 60000u;
                remind = R_NONE; redraw = true;
            }
            if (!working && remind != R_NONE){ remind = R_NONE; redraw = true; }
        }
        const bool moving = remind != R_NONE || (plusSince && ms - plusSince < 1300) || ms - lastDraw >= 1000u;
        if (redraw || moving){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            if (remind != R_NONE) draw_Reminder(remind, remindSince, ms);
            else draw_Main(save, working, standDue, eyesDue, ms, plusSince);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(remind != R_NONE || plusSince ? 50 : 100);
        if (plusSince && ms - plusSince >= 1300) plusSince = 0;
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void wb_Values(JsonObject values){
    Wb_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else { read_Save(s); roll_Day(s); }
    JsonArray rows = values["today"].to<JsonArray>();
    char t[80], l[16];
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Water";
    litres(l, sizeof(l), (uint16_t)(s.count * s.ml));
    snprintf(t, sizeof(t), "%u of %u glasses (%s)", (unsigned)s.count, (unsigned)s.goal, l);
    for (char* k = t; *k; k++) if (*k == 'M' && k[1] == 'L') { k[0] = 'm'; k[1] = 'l'; }
    o["value"] = t;
    o["state"] = s.count >= s.goal ? "ok" : "info";
    int met = 0;
    for (int k = 0; k < DAYS; k++) if (s.history[k] >= s.goal) met++;
    o = rows.add<JsonObject>();
    o["label"] = "Last 7 days";
    snprintf(t, sizeof(t), "Goal met on %d%s", met, s.streak ? "" : "");
    if (s.streak) snprintf(t + strlen(t), sizeof(t) - strlen(t), ", %u days in a row", (unsigned)s.streak);
    o["value"] = t;
    o["state"] = "info";
    o = rows.add<JsonObject>();
    o["label"] = "Breaks";
    if (!s.stand && !s.eyes) snprintf(t, sizeof(t), "None set");
    else snprintf(t, sizeof(t), "%s%s%s", s.stand ? "Stand up and stretch" : "", s.stand && s.eyes ? ", " : "", s.eyes ? "20-20-20 eye breaks" : "");
    o["value"] = t;
    o["state"] = working_Now(s) ? "ok" : "off";
    char from[12], to[12];
    snprintf(from, sizeof(from), "%02u:%02u", (unsigned)(s.from / 60), (unsigned)(s.from % 60));
    snprintf(to, sizeof(to), "%02u:%02u", (unsigned)(s.to / 60), (unsigned)(s.to % 60));
    values["goal"] = s.goal; values["ml"] = s.ml; values["water"] = s.water; values["stand"] = s.stand; values["eyes"] = s.eyes;
    values["from"] = from; values["to"] = to; values["days"] = s.days; values["sound"] = s.sound;
}
