/**
 * Baby Countdown: the days to a baby's due date, for the whole family, showing only as much as the parents choose.
 *   - Before the week they choose (and with no due date): only the time and date, so visitors see nothing.
 *   - A plain countdown (the default): the days to go and the date, no other words.
 *   - The full board: the baby's name (or "Baby"), the days to go, the weeks and days (counted from the first day of
 *     the last period, as midwives do), the trimester, a friendly size comparison, a bar of the 40 weeks, and the next
 *     appointment.
 *   - From the due date: "any day now", without counting days late.
 *   - After "our baby has arrived": a welcome, and how many days or weeks old.
 * Stopping (one tap on the phone, with a confirmation) clears the panel and forgets everything. No sounds, no
 * nagging, no medical advice. No internet; saved on Pixlpal (NVS "bcSave").
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "esp_heap_caps.h"
#include "bc_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define W           256
#define H           128
#define NVS_KEY     "bcSave"
#define SAVE_MAGIC  0x42434401u
#define APPTS       4
#define NAME_LEN    20
#define APPT_LEN    30

EXT_RAM_BSS_ATTR TaskHandle_t babyCountdown_Task_H = NULL;
static void babyCountdown_App_Task(void* dApplication);
static void bc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* babyCountdown_App;
Mtb_Applications* babyCountdown_App_GetInstance(){
    if (!babyCountdown_App){
        babyCountdown_App = new Mtb_Applications_FullScreen(babyCountdown_App_Task, &babyCountdown_Task_H, "Baby Countdown", {0,0}, 8192);
        babyCountdown_App->mtb_App_Set_Mobile_UI(BC_UI_MANIFEST, BC_UI_API, nullptr, bc_Values);
    }
    return babyCountdown_App;
}
MTB_REGISTER_APP(babyCountdown_App, 0, 0)       // the catalogue gives the app its number

struct Bc_Appt_t { int32_t day; char text[APPT_LEN]; };

struct Bc_Save_t {
    uint32_t magic;
    int32_t due;                        // days since 1970; 0: none
    int32_t born;                       // days since 1970; 0: not yet
    char name[NAME_LEN];
    uint8_t display, showFrom;
    Bc_Appt_t appt[APPTS];
};

static EXT_RAM_BSS_ATTR Bc_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool phoneChanged = false, appOpen = false;

static void read_Save(Bc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; }
    s.name[NAME_LEN - 1] = '\0';
    for (int i = 0; i < APPTS; i++) s.appt[i].text[APPT_LEN - 1] = '\0';
    if (s.display > 1) s.display = 0;
    if (s.showFrom > 20) s.showFrom = 0;
}

static int32_t today(void){
    const time_t now = time(nullptr);
    if (now < 1700000000) return 0;
    struct tm l;
    localtime_r(&now, &l);
    return mtb_Kit_Days_From_Civil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

/** "2027-02-16..." to days since 1970; the rest after the date in @p rest. */
static bool parse_Date(const char* text, int32_t& day, const char** rest){
    int y, m, d, used = 0;
    while (*text == ' ') text++;
    if (sscanf(text, "%4d-%2d-%2d%n", &y, &m, &d, &used) != 3) return false;
    if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > mtb_Kit_Days_In_Month(y, m)) return false;
    day = mtb_Kit_Days_From_Civil(y, m, d);
    if (rest){ const char* p = text + used; while (*p == ' ' || *p == ',') p++; *rest = p; }
    return true;
}

static void date_Text(char* t, size_t n, int32_t day, bool year){
    static const char* const MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    static const char* const WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const time_t t0 = (time_t)day * 86400;
    struct tm u;
    gmtime_r(&t0, &u);
    if (year) snprintf(t, n, "%s %d %s %d", WD[u.tm_wday], u.tm_mday, MON[u.tm_mon], u.tm_year + 1900);
    else snprintf(t, n, "%s %d %s", WD[u.tm_wday], u.tm_mday, MON[u.tm_mon]);
}

// Friendly sizes by completed week (as the usual pregnancy guides compare them).
static const char* size_At(int week){
    static const char* const S[] = {
        /* 4 */ "A POPPY SEED", "A SESAME SEED", "A LENTIL", "A BLUEBERRY", "A RASPBERRY", "A CHERRY", "A STRAWBERRY",
        /* 11 */ "A LIME", "A PLUM", "A LEMON", "A PEACH", "AN APPLE", "AN AVOCADO", "A PEAR", "A SWEET PEPPER",
        /* 19 */ "A MANGO", "A BANANA", "A CARROT", "A PAPAYA", "A GRAPEFRUIT", "AN EAR OF CORN", "A CAULIFLOWER",
        /* 26 */ "A LETTUCE", "A CABBAGE", "AN AUBERGINE", "A BUTTERNUT SQUASH", "A CUCUMBER", "A COCONUT",
        /* 32 */ "A BUNCH OF BANANAS", "A PINEAPPLE", "A MELON", "A HONEYDEW MELON", "A ROMAINE LETTUCE", "A BUNCH OF CHARD",
        /* 38 */ "A LEEK", "A SMALL WATERMELON", "A PUMPKIN",
    };
    if (week < 4) return nullptr;
    if (week > 40) week = 40;
    return S[week - 4];
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Due(JsonDocument& c){
    const char* v = c["due"] | (const char*)nullptr;
    const bool lmp = !v;
    if (lmp) v = c["lmp"] | (const char*)nullptr;
    int32_t day;
    if (!v || !parse_Date(v, day, nullptr)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (lmp) day += 280;
    const int32_t now = today();
    if (now && (day < now - 30 || day > now + 300)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.due = day; save.born = 0; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Name(JsonDocument& c){
    const char* v = c["name"] | (const char*)nullptr;
    if (!v || strlen(v) >= NAME_LEN * 2){ mtb_Ble_App_Cmd_Reject(); return; }
    char clean[NAME_LEN];
    mtb_Kit_To_Ascii(clean, sizeof(clean), v);
    portENTER_CRITICAL(&lock); snprintf(save.name, NAME_LEN, "%s", clean); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Display(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["display"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.display = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Show_From(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["showFrom"], 0, 20, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.showFrom = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Appt(JsonDocument& c){
    for (int i = 0; i < APPTS; i++){
        char key[8];
        snprintf(key, sizeof(key), "appt%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        Bc_Appt_t a = {};
        if (*v){
            const char* rest = nullptr;
            if (!parse_Date(v, a.day, &rest) || !rest || !*rest){ mtb_Ble_App_Cmd_Reject(); return; }
            mtb_Kit_To_Ascii(a.text, sizeof(a.text), rest);
        }
        portENTER_CRITICAL(&lock); save.appt[i] = a; portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Born(JsonDocument&){
    if (!save.due){ mtb_Ble_App_Cmd_Reject(); return; }
    const int32_t d = today();
    portENTER_CRITICAL(&lock); save.born = d ? d : save.due; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Stop(JsonDocument&){
    portENTER_CRITICAL(&lock);
    const uint8_t display = save.display, showFrom = save.showFrom;
    memset(&save, 0, sizeof(save));
    save.magic = SAVE_MAGIC; save.display = display; save.showFrom = showFrom;
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
    if (!appOpen) mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(10, 8, 18), C_SOFT = MTB_KIT_RGB(255, 170, 200), C_MINT = MTB_KIT_RGB(120, 220, 190);

static void draw_Clock(void){
    mtb_Panel_Fill_Screen(BLACK);
    const time_t now = time(nullptr);
    struct tm l;
    localtime_r(&now, &l);
    char t[24];
    if (mtb_Clock_24h()) snprintf(t, sizeof(t), "%02d:%02d", l.tm_hour, l.tm_min);
    else snprintf(t, sizeof(t), "%d:%02d", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min);
    mtb_Kit_Text_Centre(W / 2, 30, PT_Sans_Narrow_Bold28x34, KIT_TEXT, t, BLACK);
    const int32_t d = today();
    if (d){ date_Text(t, sizeof(t), d, true); mtb_Kit_Text_Centre(W / 2, 76, Terminal8x12, KIT_LABEL, t, BLACK); }
}

static void heart(int x, int y, int r, uint16_t c){
    mtb_Panel_Fill_Circle(x - r / 2, y, r / 2 + 1, c);
    mtb_Panel_Fill_Circle(x + r / 2, y, r / 2 + 1, c);
    for (int k = 0; k <= r; k++) mtb_Panel_Draw_Line(x - r + k / 2, y + k / 2 + 1, x + r - k / 2, y + k / 2 + 1, c);
}

static void draw(const Bc_Save_t& s, uint32_t ms){
    const int32_t now = today();
    if (!s.due || !now){ draw_Clock(); return; }
    const int32_t left = s.due - now;
    const int days = 280 - left, weeks = days / 7, rest = days % 7;
    if (!s.born && s.showFrom && weeks < s.showFrom){ draw_Clock(); return; }
    char t[64], fit[64];
    char name[NAME_LEN + 4];
    snprintf(name, sizeof(name), "%s", s.name[0] ? s.name : "Baby");
    mtb_Kit_Upper(name);
    if (s.born){
        mtb_Panel_Fill_Screen(C_BG);
        for (int k = 0; k < 8; k++){
            const uint32_t seed = (uint32_t)k * 2654435761u;
            const int x = (int)(seed % W), y = (int)((H - (ms / 60 + (seed >> 9)) % (H + 20)));
            heart(x, y, 4, mtb_Kit_Dim(C_SOFT, 120));
        }
        snprintf(t, sizeof(t), "WELCOME, %s", name);
        mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, t, W - 10);
        mtb_Kit_Text_Centre(W / 2, 24, PT_Sans_Narrow_Bold18x22, C_SOFT, fit, C_BG);
        const int32_t age = now - s.born;
        if (age <= 0) snprintf(t, sizeof(t), "BORN TODAY");
        else if (age < 14) snprintf(t, sizeof(t), "%ld DAY%s OLD", (long)age, age == 1 ? "" : "S");
        else if (age < 7 * 26) snprintf(t, sizeof(t), "%ld WEEKS OLD", (long)(age / 7));
        else snprintf(t, sizeof(t), "%ld MONTHS OLD", (long)(age * 12 / 365));
        mtb_Kit_Text_Centre(W / 2, 56, Terminal8x12, KIT_TEXT, t, C_BG);
        date_Text(t, sizeof(t), s.born, true);
        mtb_Kit_Text_Centre(W / 2, 80, Terminal6x8, KIT_LABEL, t, C_BG);
        return;
    }
    mtb_Panel_Fill_Screen(C_BG);
    if (s.display == 0){
        // The plain countdown: no other words.
        if (left > 0){
            snprintf(t, sizeof(t), "%ld", (long)left);
            mtb_Kit_Text_Centre(W / 2, 20, PT_Sans_Narrow_Bold28x34, C_SOFT, t, C_BG);
            mtb_Kit_Text_Centre(W / 2, 60, Terminal8x12, KIT_TEXT, left == 1 ? "DAY TO GO" : "DAYS TO GO", C_BG);
        } else mtb_Kit_Text_Centre(W / 2, 36, PT_Sans_Narrow_Bold18x22, C_SOFT, "ANY DAY NOW", C_BG);
        date_Text(t, sizeof(t), s.due, true);
        mtb_Kit_Text_Centre(W / 2, 90, Terminal6x8, KIT_LABEL, t, C_BG);
        return;
    }
    // The full board.
    heart(12, 12, 6, C_SOFT);
    snprintf(t, sizeof(t), left > 0 ? "%s ARRIVES IN" : "%s", name);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - 30);
    mtb_Kit_Text(24, 6, Terminal8x12, KIT_TEXT, fit, C_BG);
    if (left > 0){
        snprintf(t, sizeof(t), "%ld", (long)left);
        const int16_t x = mtb_Kit_Text(8, 22, PT_Sans_Narrow_Bold28x34, C_SOFT, t, C_BG);
        mtb_Kit_Text(x + 6, 40, Terminal8x12, KIT_LABEL, left == 1 ? "DAY" : "DAYS", C_BG);
    } else mtb_Kit_Text(8, 30, PT_Sans_Narrow_Bold18x22, C_SOFT, "ANY DAY NOW", C_BG);
    // Right: the week and the trimester.
    if (weeks >= 0 && weeks <= 42){
        snprintf(t, sizeof(t), "%d WEEKS", weeks);
        mtb_Kit_Text_Right(W - 6, 24, Terminal8x12, C_MINT, t, C_BG);
        if (rest){ snprintf(t, sizeof(t), "%d DAY%s", rest, rest == 1 ? "" : "S"); mtb_Kit_Text_Right(W - 6, 38, Terminal6x8, C_MINT, t, C_BG); }
        const char* tri = weeks < 13 ? "1ST TRIMESTER" : weeks < 27 ? "2ND TRIMESTER" : "3RD TRIMESTER";
        mtb_Kit_Text_Right(W - 6, 50, Terminal4x6, KIT_LABEL, tri, C_BG);
    }
    // The size, and the 40 weeks.
    const char* size = size_At(weeks);
    if (size){
        snprintf(t, sizeof(t), "ABOUT THE SIZE OF %s", size);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 12);
        mtb_Kit_Text_Centre(W / 2, 66, Terminal6x8, KIT_TEXT, fit, C_BG);
    }
    const float f = days < 0 ? 0 : days > 280 ? 1 : days / 280.0f;
    mtb_Kit_Bar(8, 82, W - 16, 4, f, C_SOFT, MTB_KIT_RGB(40, 30, 50));
    for (int k = 1; k < 3; k++){ const int x = 8 + (W - 16) * (k == 1 ? 13 : 27) / 40; mtb_Panel_Fill_Rect(x, 80, x, 87, KIT_LABEL); }
    date_Text(t, sizeof(t), s.due, true);
    snprintf(fit, sizeof(fit), "DUE %s", t);
    mtb_Kit_Text(8, 92, Terminal4x6, KIT_DIM, fit, C_BG);
    // The next appointment.
    int next = -1;
    for (int i = 0; i < APPTS; i++) if (s.appt[i].text[0] && s.appt[i].day >= now && (next < 0 || s.appt[i].day < s.appt[next].day)) next = i;
    if (next >= 0){
        char when[24], what[APPT_LEN];
        if (s.appt[next].day == now) snprintf(when, sizeof(when), "TODAY");
        else if (s.appt[next].day == now + 1) snprintf(when, sizeof(when), "TOMORROW");
        else date_Text(when, sizeof(when), s.appt[next].day, false);
        snprintf(what, sizeof(what), "%s", s.appt[next].text); mtb_Kit_Upper(what);
        snprintf(t, sizeof(t), "NEXT: %s  %s", when, what);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 16);
        mtb_Kit_Text(8, 108, Terminal6x8, C_MINT, fit, C_BG);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void babyCountdown_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(buttonDoNothing, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Due, cmd_Name, cmd_Display, cmd_Show_From, cmd_Appt, cmd_Born, cmd_Stop);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    phoneChanged = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        Bc_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        const bool hearts = s.born && s.due;
        if (redraw || ms - lastDraw >= (hearts ? 60u : 20000u)){
            redraw = false;
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(s, ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(hearts ? 50 : 200);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void bc_Values(JsonObject values){
    Bc_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[96], d[32];
    const int32_t now = today();
    if (s.due){
        JsonObject o = rows.add<JsonObject>();
        date_Text(d, sizeof(d), s.due, true);
        if (s.born){ o["label"] = "Arrived"; date_Text(t, sizeof(t), s.born, true); o["value"] = t; o["state"] = "ok"; }
        else {
            o["label"] = "Due";
            const int32_t left = now ? s.due - now : 0;
            const int days = 280 - left;
            if (now && left > 0) snprintf(t, sizeof(t), "%s, %ld days to go (%d weeks %d days)", d, (long)left, days / 7, days % 7);
            else snprintf(t, sizeof(t), "%s, any day now", d);
            o["value"] = t; o["state"] = "info";
            if (s.showFrom && now && days / 7 < s.showFrom){
                o = rows.add<JsonObject>();
                o["label"] = "Panel";
                snprintf(t, sizeof(t), "Only the time and date until week %u", (unsigned)s.showFrom);
                o["value"] = t; o["state"] = "off";
            }
        }
    }
    values["display"] = s.display;
    values["showFrom"] = s.showFrom;
    values["name"] = s.name;
    char due[12] = "";
    if (s.due){ const time_t t0 = (time_t)s.due * 86400; struct tm u; gmtime_r(&t0, &u); snprintf(due, sizeof(due), "%04d-%02d-%02d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday); }
    values["due"] = due;
    for (int i = 0; i < APPTS; i++){
        char key[8], v[48] = "";
        snprintf(key, sizeof(key), "appt%d", i);
        if (s.appt[i].text[0]){ const time_t t0 = (time_t)s.appt[i].day * 86400; struct tm u; gmtime_r(&t0, &u); snprintf(v, sizeof(v), "%04d-%02d-%02d %s", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, s.appt[i].text); }
        values[key] = v;
    }
}
