/**
 * Bell Schedule: the school day on Pixlpal. The lesson now in large letters with a bar of the time left, what comes
 * next, a countdown to the next break (or home time), and along the bottom the whole day as a strip: lessons blue,
 * breaks green, the past dimmed, a white line at now. Before school it counts down to the first bell; after, it shows
 * tomorrow's first lesson. A chime at each bell.
 *
 * A timetable for each weekday from the phone ("08:00-08:45 Maths; ..."), and a day off when there is no school.
 * Knob: turn to see each day's timetable, click to come back to now. NVS "belSave". Works without the internet (the
 * time is Pixlpal's).
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "bel_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define PERIODS     14
#define NAME_LEN    24
#define NVS_KEY     "belSave"
#define SAVE_MAGIC  0x42454C01u

EXT_RAM_BSS_ATTR TaskHandle_t bellSchedule_Task_H = NULL;
static void bellSchedule_App_Task(void* dApplication);
static void bel_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* bellSchedule_App;
Mtb_Applications* bellSchedule_App_GetInstance(){
    if (!bellSchedule_App){
        bellSchedule_App = new Mtb_Applications_FullScreen(bellSchedule_App_Task, &bellSchedule_Task_H, "Bell Schedule", {0,0}, 8192);
        bellSchedule_App->mtb_App_Set_Mobile_UI(BEL_UI_MANIFEST, BEL_UI_API, nullptr, bel_Values);
    }
    return bellSchedule_App;
}
MTB_REGISTER_APP(bellSchedule_App, 0, 0)      // the catalogue gives the app its number

struct Bel_Period_t { uint16_t from, to; char name[NAME_LEN]; };   // minutes after midnight

struct Bel_Save_t {
    uint32_t magic;
    Bel_Period_t day[7][PERIODS];       // 0 Monday ... 6 Sunday
    uint8_t count[7];
    uint8_t chime;
    int32_t offDay;                     // the local date (days since 1970) with no school, 0 none
};

static EXT_RAM_BSS_ATTR Bel_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Bel_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.chime = 1; }
    for (int d = 0; d < 7; d++){
        if (s.count[d] > PERIODS) s.count[d] = 0;
        for (int i = 0; i < PERIODS; i++) s.day[d][i].name[NAME_LEN - 1] = '\0';
    }
}

static bool is_Break(const char* name){
    char l[NAME_LEN];
    snprintf(l, sizeof(l), "%s", name);
    for (char* c = l; *c; c++) *c = (char)tolower((unsigned char)*c);
    return strstr(l, "break") || strstr(l, "lunch") || strstr(l, "recess") || strstr(l, "snack");
}

/** "8:05", "08.05", "1:30pm": minutes after midnight; -1 if not a time. Moves @p p past it. */
static int read_Clock(const char*& p){
    while (*p == ' ') p++;
    if (!isdigit((unsigned char)*p)) return -1;
    int h = 0, m = 0;
    while (isdigit((unsigned char)*p)) h = h * 10 + (*p++ - '0');
    if (*p != ':' && *p != '.') return -1;
    p++;
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return -1;
    m = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    while (*p == ' ') p++;
    if ((p[0] == 'a' || p[0] == 'A' || p[0] == 'p' || p[0] == 'P') && (p[1] == 'm' || p[1] == 'M')){
        const bool pm = p[0] == 'p' || p[0] == 'P';
        if (h < 1 || h > 12) return -1;
        h = h % 12 + (pm ? 12 : 0);
        p += 2;
    }
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

/** A day's timetable from "08:00-08:45 Maths; 08:45-09:30 English". False if any lesson can't be read. */
static bool parse_Day(const char* text, Bel_Period_t* out, uint8_t& n){
    n = 0;
    const char* p = text;
    while (*p){
        while (*p == ' ' || *p == ';' || *p == '\n' || *p == ',') p++;
        if (!*p) break;
        if (n == PERIODS) return false;
        const int from = read_Clock(p);
        if (from < 0) return false;
        while (*p == ' ') p++;
        if (*p != '-' && *p != 't') return false;                       // "-" or "to"
        if (*p == 't'){ if (p[1] != 'o') return false; p += 2; } else p++;
        const int to = read_Clock(p);
        if (to < 0 || to <= from) return false;
        while (*p == ' ') p++;
        const char* name = p;
        while (*p && *p != ';' && *p != '\n') p++;
        size_t len = (size_t)(p - name);
        while (len && name[len - 1] == ' ') len--;
        if (!len) return false;
        char raw[64];
        snprintf(raw, sizeof(raw), "%.*s", (int)(len < 60 ? len : 60), name);
        out[n].from = (uint16_t)from;
        out[n].to = (uint16_t)to;
        mtb_Kit_To_Ascii(out[n].name, NAME_LEN, raw);
        n++;
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && out[j].from < out[j - 1].from; j--){ const Bel_Period_t t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    for (int i = 1; i < n; i++) if (out[i].from < out[i - 1].to) return false;      // overlapping lessons
    return true;
}

static void hm(char* t, size_t n, int minute){
    const int h = minute / 60, m = minute % 60;
    if (mtb_Clock_24h()) snprintf(t, n, "%02d:%02d", h, m);
    else snprintf(t, n, "%d:%02d%s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}

static void day_Text(char* t, size_t n, const Bel_Period_t* p, uint8_t count){
    t[0] = '\0';
    for (int i = 0; i < count; i++){
        char line[48];
        snprintf(line, sizeof(line), "%s%02d:%02d-%02d:%02d %s", i ? "; " : "", p[i].from / 60, p[i].from % 60, p[i].to / 60, p[i].to % 60, p[i].name);
        strlcat(t, line, n);
    }
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Day(JsonDocument& c){
    for (int d = 0; d < 7; d++){
        char key[6];
        snprintf(key, sizeof(key), "day%d", d);
        if (c[key].isNull()) continue;
        Bel_Period_t p[PERIODS];
        uint8_t n;
        if (!parse_Day(c[key] | "", p, n)){ mtb_Ble_App_Cmd_Reject(); return; }
        portENTER_CRITICAL(&lock);
        memcpy(save.day[d], p, sizeof(p[0]) * n);
        save.count[d] = n;
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Copy(JsonDocument&){
    if (!save.count[0]){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    for (int d = 1; d < 5; d++){ memcpy(save.day[d], save.day[0], sizeof(save.day[0])); save.count[d] = save.count[0]; }
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Chime(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["chime"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.chime = (uint8_t)v; phoneChanged = true; }
static int32_t today_Day(int* weekday, int* second){
    pxl_time_t t;
    pxl_time(&t);
    if (weekday) *weekday = (t.weekday + 6) % 7;                                // 0 Monday
    if (second) *second = t.hour * 3600 + t.minute * 60 + t.second;
    return mtb_Kit_Days_From_Civil(t.year, t.month, t.day);
}
static void cmd_Off(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["off"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.offDay = v ? today_Day(nullptr, nullptr) : 0;
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The day now ----------------------------------------------------------------------------------------------------------------------
enum : uint8_t { D_NONE = 0, D_BEFORE, D_LESSON, D_GAP, D_AFTER };

struct Bel_Now_t {
    uint8_t state;
    int cur;                            // D_LESSON: the lesson; D_GAP / D_BEFORE: the next
    int nextBreak;                      // the next break from now, -1 none
};

static Bel_Now_t day_Now(const Bel_Period_t* p, uint8_t n, int second){
    Bel_Now_t r = {D_NONE, -1, -1};
    if (!n) return r;
    const int minute = second / 60;
    if (minute < p[0].from){ r.state = D_BEFORE; r.cur = 0; }
    else if (minute >= p[n - 1].to) r.state = D_AFTER;
    else for (int i = 0; i < n; i++){
        if (minute >= p[i].from && minute < p[i].to){ r.state = D_LESSON; r.cur = i; break; }
        if (i + 1 < n && minute >= p[i].to && minute < p[i + 1].from){ r.state = D_GAP; r.cur = i + 1; break; }
    }
    for (int i = 0; i < n; i++) if (p[i].from > minute && is_Break(p[i].name)){ r.nextBreak = i; break; }
    return r;
}

/** The next school day after today with lessons: days ahead (1-7), -1 none. */
static int next_School_Day(const Bel_Save_t& s, int weekday){
    for (int k = 1; k <= 7; k++) if (s.count[(weekday + k) % 7]) return k;
    return -1;
}

static void span_Text(char* t, size_t n, int seconds){
    const int m = (seconds + 59) / 60;
    if (m < 60) snprintf(t, n, "%d MIN", m);
    else if (m % 60 == 0) snprintf(t, n, "%dH", m / 60);
    else snprintf(t, n, "%dH %02dM", m / 60, m % 60);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 14), C_HEAD = MTB_KIT_RGB(16, 20, 32);
static const uint16_t C_LESSON = MTB_KIT_RGB(60, 120, 255), C_BREAK = MTB_KIT_RGB(40, 200, 90);
static const char* const DAYS[] = {"MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};
static const char* const MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void big_Name(const char* name, int y, uint16_t color){
    char up[NAME_LEN + 2], fit[NAME_LEN + 2];
    snprintf(up, sizeof(up), "%s", name);
    mtb_Kit_Upper(up);
    if (mtb_Kit_Text_Width(PT_Sans_Narrow_Bold28x34, up) <= W - 8) mtb_Kit_Text(4, y, PT_Sans_Narrow_Bold28x34, color, up, C_BG);
    else { mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, up, W - 8); mtb_Kit_Text(4, y + 6, PT_Sans_Narrow_Bold18x22, color, fit, C_BG); }
}

/** The whole day as a strip: lessons blue, breaks green, the past dimmed, a white line at now. */
static void day_Strip(const Bel_Period_t* p, uint8_t n, int second, int y){
    if (!n) return;
    const int x0 = 4, x1 = W - 5, first = p[0].from, last = p[n - 1].to;
    const int span = last - first > 0 ? last - first : 1;
    mtb_Panel_Fill_Rect(x0, y, x1, y + 6, KIT_LINE);
    for (int i = 0; i < n; i++){
        const int a = x0 + (p[i].from - first) * (x1 - x0) / span, b = x0 + (p[i].to - first) * (x1 - x0) / span;
        uint16_t c = is_Break(p[i].name) ? C_BREAK : C_LESSON;
        if (second / 60 >= p[i].to) c = mtb_Kit_Dim(c, 70);
        if (b - 1 > a) mtb_Panel_Fill_Rect((int16_t)a, (int16_t)y, (int16_t)(b - 1), (int16_t)(y + 6), c);
    }
    const int mx = x0 + (second - first * 60) * (x1 - x0) / (span * 60);
    if (mx >= x0 && mx <= x1) mtb_Panel_Fill_Rect((int16_t)mx, (int16_t)(y - 2), (int16_t)(mx + 1), (int16_t)(y + 8), KIT_TEXT);
    char a[12], b[12];
    hm(a, sizeof(a), first); hm(b, sizeof(b), last);
    mtb_Kit_Text(x0, y + 9, Terminal4x6, KIT_DIM, a, C_BG);
    mtb_Kit_Text_Right(x1, y + 9, Terminal4x6, KIT_DIM, b, C_BG);
}

static void draw_Now(const Bel_Save_t& s){
    mtb_Panel_Fill_Screen(C_BG);
    int weekday, second;
    pxl_time_t pt;
    pxl_time(&pt);
    const int32_t today = today_Day(&weekday, &second);
    char t[64], a[16], b[16];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s %d %s", DAYS[weekday], pt.day, MON[(pt.month + 11) % 12]);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, t, C_HEAD);
    hm(a, sizeof(a), second / 60);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_TEXT, a, C_HEAD);

    const bool off = s.offDay == today;
    const Bel_Period_t* p = s.day[weekday];
    const uint8_t n = off ? 0 : s.count[weekday];
    const Bel_Now_t now = day_Now(p, n, second);

    bool anyDay = false;
    for (int d = 0; d < 7; d++) if (s.count[d]) anyDay = true;
    if (!anyDay){
        mtb_Kit_Text_Centre(W / 2, 46, Terminal8x12, KIT_TEXT, "NO TIMETABLE YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 64, Terminal4x6, KIT_LABEL, "WRITE THE LESSONS IN THE PHONE APP", C_BG);
        return;
    }

    if (now.state == D_NONE || now.state == D_AFTER){
        big_Name(now.state == D_AFTER ? "SCHOOL'S OUT" : off ? "NO SCHOOL TODAY" : "NO SCHOOL TODAY", 16, C_BREAK);
        const int k = next_School_Day(s, weekday);
        if (k > 0){
            const int d = (weekday + k) % 7;
            hm(a, sizeof(a), s.day[d][0].from);
            char nm[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", s.day[d][0].name); mtb_Kit_Upper(nm);
            snprintf(t, sizeof(t), "%s %s %s", k == 1 ? "TOMORROW" : DAYS[d], a, nm);
            char fit[64]; mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - 8);
            mtb_Kit_Text(4, 62, Terminal6x8, KIT_LABEL, "NEXT", C_BG);
            mtb_Kit_Text(4, 74, Terminal8x12, KIT_TEXT, fit, C_BG);
        }
        if (now.state == D_AFTER) day_Strip(p, n, second, 108);
        return;
    }

    if (now.state == D_BEFORE){
        big_Name("SCHOOL STARTS", 16, KIT_TEXT);
        hm(a, sizeof(a), p[0].from);
        span_Text(b, sizeof(b), p[0].from * 60 - second);
        snprintf(t, sizeof(t), "AT %s, IN %s", a, b);
        mtb_Kit_Text(4, 56, Terminal8x12, KIT_GOLD, t, C_BG);
        char nm[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", p[0].name); mtb_Kit_Upper(nm);
        mtb_Kit_Text(4, 76, Terminal6x8, KIT_LABEL, "FIRST", C_BG);
        mtb_Kit_Text(40, 74, Terminal8x12, KIT_TEXT, nm, C_BG);
        day_Strip(p, n, second, 108);
        return;
    }

    // A lesson, or the change between two.
    const bool lesson = now.state == D_LESSON;
    const Bel_Period_t& cur = p[now.cur];
    const bool brk = is_Break(cur.name);
    if (lesson){
        big_Name(cur.name, 14, brk ? C_BREAK : KIT_TEXT);
        const int len = (cur.to - cur.from) * 60, gone = second - cur.from * 60, left = cur.to * 60 - second;
        mtb_Kit_Bar(4, 51, W - 8, 5, (float)gone / (float)len, brk ? C_BREAK : C_LESSON, KIT_LINE);
        hm(a, sizeof(a), cur.from); hm(b, sizeof(b), cur.to);
        snprintf(t, sizeof(t), "%s-%s", a, b);
        mtb_Kit_Text(4, 61, Terminal6x8, KIT_LABEL, t, C_BG);
        char l[16]; span_Text(l, sizeof(l), left);
        snprintf(t, sizeof(t), "%s LEFT", l);
        mtb_Kit_Text_Right(W - 4, 59, Terminal8x12, left <= 300 ? KIT_GOLD : KIT_TEXT, t, C_BG);
    } else {
        big_Name("CHANGEOVER", 14, KIT_CYAN);
        span_Text(b, sizeof(b), cur.from * 60 - second);
        snprintf(t, sizeof(t), "NEXT LESSON IN %s", b);
        mtb_Kit_Text(4, 59, Terminal8x12, KIT_GOLD, t, C_BG);
    }
    const int nx = lesson ? now.cur + 1 : now.cur;
    if (nx < n){
        hm(a, sizeof(a), p[nx].from);
        char nm[NAME_LEN + 2], fit[48]; snprintf(nm, sizeof(nm), "%s", p[nx].name); mtb_Kit_Upper(nm);
        snprintf(t, sizeof(t), "%s %s", a, nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, t, W - 50);
        mtb_Kit_Text(4, 79, Terminal6x8, KIT_LABEL, "NEXT", C_BG);
        mtb_Kit_Text(34, 77, Terminal8x12, is_Break(p[nx].name) ? C_BREAK : KIT_TEXT, fit, C_BG);
    }
    // The next break, or home time.
    if (now.nextBreak >= 0 && !(lesson && brk)){
        span_Text(b, sizeof(b), p[now.nextBreak].from * 60 - second);
        char nm[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", p[now.nextBreak].name); mtb_Kit_Upper(nm);
        snprintf(t, sizeof(t), "%s IN %s", nm, b);
    } else { span_Text(b, sizeof(b), p[n - 1].to * 60 - second); snprintf(t, sizeof(t), "HOME TIME IN %s", b); }
    mtb_Kit_Text(4, 94, Terminal6x8, KIT_CYAN, t, C_BG);
    day_Strip(p, n, second, 108);
}

static void draw_Day(const Bel_Save_t& s, int d){
    mtb_Panel_Fill_Screen(C_BG);
    int weekday;
    today_Day(&weekday, nullptr);
    char t[64], a[16], b[16];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s%s", DAYS[d], d == weekday ? " (TODAY)" : "");
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, t, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 2, Terminal4x6, KIT_DIM, "TURN: DAYS  CLICK: NOW", C_HEAD);
    const uint8_t n = s.count[d];
    if (!n){ mtb_Kit_Text_Centre(W / 2, 56, Terminal8x12, KIT_LABEL, "NO SCHOOL", C_BG); return; }
    const int perCol = n > 9 ? (n + 1) / 2 : n;
    for (int i = 0; i < n; i++){
        const int col = i / perCol, row = i % perCol;
        const int x = 4 + col * 128, y = 14 + row * 12;
        hm(a, sizeof(a), s.day[d][i].from); hm(b, sizeof(b), s.day[d][i].to);
        snprintf(t, sizeof(t), "%s-%s", a, b);
        mtb_Kit_Text(x, y + 1, Terminal4x6, KIT_LABEL, t, C_BG);
        char nm[NAME_LEN + 2], fit[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", s.day[d][i].name); mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, (n > 9 ? 124 : W - 8) - 56);
        mtb_Kit_Text(x + 54, y, Terminal6x8, is_Break(s.day[d][i].name) ? C_BREAK : KIT_TEXT, fit, C_BG);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void bell_Sound(bool start){
    if (!save.chime || mtb_Silent_Mode()) return;
    static const Snd_Note_t START[] = {{784, 260}, {0, 60}, {988, 420}};
    static const Snd_Note_t END[] = {{988, 260}, {0, 60}, {784, 420}};
    snd_Play(start ? START : END, 3, 0.3f, false);
}

static void bellSchedule_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Day, cmd_Copy, cmd_Chime, cmd_Off);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, lastMinute = 0xFFFFFFFF;
    int view = -1, lastSecond = -1;                                             // -1: now, else a weekday
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t){
            turns -= t;
            int wd; today_Day(&wd, nullptr);
            int v = view < 0 ? wd : view;
            v = ((v + t) % 7 + 7) % 7;
            view = v;
            lastDraw = 0;
        }
        if (clickRequested){ clickRequested = false; view = -1; lastDraw = 0; }

        // The bells: a lesson's start or end crossed since the last look.
        int wd, second;
        const int32_t today = today_Day(&wd, &second);
        if (lastSecond >= 0 && second > lastSecond && save.offDay != today){
            for (int i = 0; i < save.count[wd]; i++){
                const int a = save.day[wd][i].from * 60, b = save.day[wd][i].to * 60;
                if (lastSecond < a && second >= a){ bell_Sound(true); break; }
                if (lastSecond < b && second >= b){
                    const bool alsoStart = i + 1 < save.count[wd] && save.day[wd][i + 1].from * 60 == b;
                    bell_Sound(alsoStart); break;
                }
            }
        }
        lastSecond = second;
        if ((uint32_t)(second / 60) != lastMinute){ lastMinute = (uint32_t)(second / 60); push = true; }

        if (ms - lastDraw >= 1000){
            lastDraw = ms;
            Bel_Save_t* s = &save;
            mtb_Panel_Begin_Update();
            if (view < 0) draw_Now(*s); else draw_Day(*s, view);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(100);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void bel_Values(JsonObject values){
    Bel_Save_t* s = (Bel_Save_t*)heap_caps_malloc(sizeof(Bel_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    char text[PERIODS * 44];
    for (int d = 0; d < 7; d++){
        char key[6];
        snprintf(key, sizeof(key), "day%d", d);
        day_Text(text, sizeof(text), s->day[d], s->count[d]);
        values[key] = text;                                                 // char*: copied
    }
    values["chime"] = s->chime;
    int wd, second;
    const int32_t today = today_Day(&wd, &second);
    values["off"] = s->offDay == today ? 1 : 0;
    JsonArray rows = values["today"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    char t[96], a[16], b[16];
    static const char* const DAY[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    r["label"] = DAY[wd];
    const uint8_t n = s->offDay == today ? 0 : s->count[wd];
    const Bel_Now_t now = day_Now(s->day[wd], n, second);
    char nm[NAME_LEN];
    switch (now.state){
        case D_LESSON:
            snprintf(nm, sizeof(nm), "%s", s->day[wd][now.cur].name);
            hm(b, sizeof(b), s->day[wd][now.cur].to);
            snprintf(t, sizeof(t), "%s until %s", nm, b); r["state"] = "ok"; break;
        case D_GAP:
            snprintf(nm, sizeof(nm), "%s", s->day[wd][now.cur].name);
            hm(a, sizeof(a), s->day[wd][now.cur].from);
            snprintf(t, sizeof(t), "Changeover: %s at %s", nm, a); r["state"] = "info"; break;
        case D_BEFORE:
            hm(a, sizeof(a), s->day[wd][0].from);
            snprintf(t, sizeof(t), "School starts at %s", a); r["state"] = "info"; break;
        case D_AFTER: snprintf(t, sizeof(t), "School's out"); r["state"] = "off"; break;
        default: snprintf(t, sizeof(t), s->offDay == today ? "No school today (day off)" : "No school today"); r["state"] = "off"; break;
    }
    r["value"] = t;
    heap_caps_free(s);
}
