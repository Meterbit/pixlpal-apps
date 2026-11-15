/**
 * House Points: a scoreboard for up to six houses or teams. A bar for each house in its colour, its total above and
 * this week's points below its name, a crown over the leader. When points are given the house's bar flashes and a
 * "+5" rises from it, with a cheer (or a falling note when points are taken away).
 *
 * Points come from the phone (a house and an amount, or "Red 5") or the knob: turn to choose a house, click to give
 * it a point, double click to take one. The week's points start again each Monday; the term's stay until a new term.
 * NVS "hptSave". Works without the internet.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "hpt_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define HOUSES      6
#define NAME_LEN    16
#define RECENT      10
#define NVS_KEY     "hptSave"
#define SAVE_MAGIC  0x48505401u
#define FLASH_MS    1800

EXT_RAM_BSS_ATTR TaskHandle_t housePoints_Task_H = NULL;
static void housePoints_App_Task(void* dApplication);
static void hpt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* housePoints_App;
Mtb_Applications* housePoints_App_GetInstance(){
    if (!housePoints_App){
        housePoints_App = new Mtb_Applications_FullScreen(housePoints_App_Task, &housePoints_Task_H, "House Points", {0,0}, 8192);
        housePoints_App->mtb_App_Set_Mobile_UI(HPT_UI_MANIFEST, HPT_UI_API, nullptr, hpt_Values);
    }
    return housePoints_App;
}
MTB_REGISTER_APP(housePoints_App, 0, 0)       // the catalogue gives the app its number

struct Hpt_House_t { char name[NAME_LEN]; uint32_t argb; int32_t term, week; };
struct Hpt_Award_t { uint8_t house; int16_t points; uint32_t at; };

struct Hpt_Save_t {
    uint32_t magic;
    Hpt_House_t house[HOUSES];
    uint8_t n;
    int32_t weekOf;                     // the Monday-based week the week's points are for
    Hpt_Award_t recent[RECENT];         // newest first
    uint8_t recentN;
    uint8_t selected, amount, sound;
};

static EXT_RAM_BSS_ATTR Hpt_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

// The last award, for the screen to show.
static volatile int flashHouse = -1;
static volatile int flashPoints = 0;
static volatile uint32_t flashSeq = 0;

static int32_t this_Week(void){
    pxl_time_t t;
    if (!pxl_time(&t)) return 0;
    const int32_t days = mtb_Kit_Days_From_Civil(t.year, t.month, t.day);
    return (days + 3) / 7;                                                      // 1970-01-01 was a Thursday
}

static void default_Houses(Hpt_Save_t& s){
    static const char* const N[] = {"Red", "Blue", "Green", "Yellow"};
    static const uint32_t C[] = {0xFFE53935, 0xFF1E88E5, 0xFF43A047, 0xFFFDD835};
    s.n = 4;
    for (int i = 0; i < 4; i++){ snprintf(s.house[i].name, NAME_LEN, "%s", N[i]); s.house[i].argb = C[i]; }
}

static void read_Save(Hpt_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.amount = 1; s.sound = 1; default_Houses(s); }
    if (s.n > HOUSES) s.n = 0;
    if (s.recentN > RECENT) s.recentN = 0;
    if (s.amount < 1 || s.amount > 50) s.amount = 1;
    if (s.selected >= HOUSES) s.selected = 0;
    for (int i = 0; i < HOUSES; i++) s.house[i].name[NAME_LEN - 1] = '\0';
}

static uint16_t color_Of(uint32_t argb){ return MTB_KIT_RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF); }

/** Points to a house (negative takes them away). Under the lock. */
static void award(int h, int points){
    if (h < 0 || h >= save.n || !points) return;
    const int32_t wk = this_Week();
    if (wk && wk != save.weekOf){ for (int i = 0; i < save.n; i++) save.house[i].week = 0; save.weekOf = wk; }
    save.house[h].term += points;
    save.house[h].week += points;
    memmove(&save.recent[1], &save.recent[0], sizeof(save.recent[0]) * (RECENT - 1));
    save.recent[0] = {(uint8_t)h, (int16_t)points, (uint32_t)time(nullptr)};
    if (save.recentN < RECENT) save.recentN++;
    flashHouse = h; flashPoints = points; flashSeq++;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_House(JsonDocument& c){
    for (int i = 0; i < HOUSES; i++){
        char nk[8], ck[8];
        snprintf(nk, sizeof(nk), "name%d", i); snprintf(ck, sizeof(ck), "color%d", i);
        if (c[nk].isNull() && c[ck].isNull()) continue;
        portENTER_CRITICAL(&lock);
        if (!c[nk].isNull()){
            char name[NAME_LEN];
            mtb_Kit_To_Ascii(name, sizeof(name), c[nk] | "");
            char* p = name; while (*p == ' ') p++;
            size_t len = strlen(p); while (len && p[len - 1] == ' ') p[--len] = '\0';
            if (!len){                                                          // removed: the others move up
                if (i < save.n){ memmove(&save.house[i], &save.house[i + 1], sizeof(save.house[0]) * (HOUSES - 1 - i)); memset(&save.house[HOUSES - 1], 0, sizeof(save.house[0])); save.n--; }
            } else {
                const int at = i < save.n ? i : save.n;                         // a new house goes after the last
                if (at >= save.n){ memset(&save.house[at], 0, sizeof(save.house[0])); save.house[at].argb = 0xFF888888; save.n++; }
                snprintf(save.house[at].name, NAME_LEN, "%s", p);
            }
        }
        if (!c[ck].isNull() && i < save.n){
            const char* v = c[ck] | "";
            const uint32_t argb = (uint32_t)strtoul(v, nullptr, 16);
            if (argb) save.house[i].argb = argb;
        }
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Select(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["house"], 0, HOUSES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.selected = (uint8_t)v; phoneChanged = true; }
static void cmd_Amount(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["amount"], 1, 50, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.amount = (uint8_t)v; phoneChanged = true; }
static void cmd_Give(JsonDocument&){ if (save.selected >= save.n){ mtb_Ble_App_Cmd_Reject(); return; } portENTER_CRITICAL(&lock); award(save.selected, save.amount); portEXIT_CRITICAL(&lock); phoneChanged = true; }
static void cmd_Take(JsonDocument&){ if (save.selected >= save.n){ mtb_Ble_App_Cmd_Reject(); return; } portENTER_CRITICAL(&lock); award(save.selected, -(int)save.amount); portEXIT_CRITICAL(&lock); phoneChanged = true; }
static void cmd_Award(JsonDocument& c){
    // "Red 5", "blue -2", "Red Dragons +10": the last word is the points, the rest starts a house's name.
    char text[64];
    mtb_Kit_To_Ascii(text, sizeof(text), c["award"] | "");
    size_t n = strlen(text);
    while (n && text[n - 1] == ' ') text[--n] = '\0';
    char* sp = strrchr(text, ' ');
    if (!sp){ mtb_Ble_App_Cmd_Reject(); return; }
    char* e;
    const long pts = strtol(sp + 1, &e, 10);
    if (*e || pts == 0 || pts < -100 || pts > 100){ mtb_Ble_App_Cmd_Reject(); return; }
    *sp = '\0';
    const char* q = text; while (*q == ' ') q++;
    const size_t ql = strlen(q);
    int found = -1;
    for (int i = 0; i < save.n && ql; i++) if (!strncasecmp(save.house[i].name, q, ql)){ found = i; break; }
    if (found < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); award(found, (int)pts); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Term(JsonDocument&){
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < HOUSES; i++){ save.house[i].term = 0; save.house[i].week = 0; }
    save.recentN = 0;
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobDouble = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED){ if (b.count >= 2) knobDouble = true; else knobClick = true; } }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 7, 12), C_HEAD = MTB_KIT_RGB(16, 18, 30);

static void crown(int cx, int y){
    const uint16_t g = KIT_GOLD;
    mtb_Panel_Fill_Rect((int16_t)(cx - 7), (int16_t)(y + 4), (int16_t)(cx + 7), (int16_t)(y + 7), g);
    mtb_Panel_Fill_Triangle((int16_t)(cx - 7), (int16_t)(y + 4), (int16_t)(cx - 7), (int16_t)y, (int16_t)(cx - 3), (int16_t)(y + 4), g);
    mtb_Panel_Fill_Triangle((int16_t)(cx - 3), (int16_t)(y + 4), (int16_t)cx, (int16_t)(y - 1), (int16_t)(cx + 3), (int16_t)(y + 4), g);
    mtb_Panel_Fill_Triangle((int16_t)(cx + 3), (int16_t)(y + 4), (int16_t)(cx + 7), (int16_t)y, (int16_t)(cx + 7), (int16_t)(y + 4), g);
}

static void draw_Board(const Hpt_Save_t& s, int selected, bool showSel, int flash, int flashPts, uint32_t flashAge){
    mtb_Panel_Fill_Screen(C_BG);
    char t[32];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_LABEL, "HOUSE POINTS", C_HEAD);
    if (!s.n){ mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, KIT_LABEL, "ADD THE HOUSES IN THE PHONE APP", C_BG); return; }
    int lead = 0, top = 1;
    for (int i = 0; i < s.n; i++){ if (s.house[i].term > s.house[lead].term) lead = i; if (s.house[i].term > top) top = s.house[i].term; }
    bool tie = false;
    for (int i = 0; i < s.n; i++) if (i != lead && s.house[i].term == s.house[lead].term) tie = true;
    if (s.house[lead].term > 0 && !tie){
        char nm[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", s.house[lead].name); mtb_Kit_Upper(nm);
        snprintf(t, sizeof(t), "%s LEADS", nm);
        mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_GOLD, t, C_HEAD);
    }
    const int cw = W / s.n, barTop = 48, barBottom = 98;          // above the tallest bar: its total and the crown
    for (int i = 0; i < s.n; i++){
        const Hpt_House_t& h = s.house[i];
        const uint16_t c = color_Of(h.argb);
        const int x = i * cw, cx = x + cw / 2, bw = cw * 3 / 5;
        const int v = h.term > 0 ? h.term : 0;
        const int bh = 3 + (barBottom - barTop - 3) * v / top;
        const bool fl = i == flash && flashAge < FLASH_MS;
        const uint16_t bc = fl && (flashAge / 150) % 2 ? KIT_TEXT : c;
        mtb_Panel_Fill_Rect((int16_t)(cx - bw / 2), (int16_t)(barBottom - bh), (int16_t)(cx + bw / 2), (int16_t)barBottom, bc);
        snprintf(t, sizeof(t), "%ld", (long)h.term);
        const uint8_t* f = mtb_Kit_Text_Width(PT_Sans_Narrow_Bold18x22, t) <= cw - 4 ? PT_Sans_Narrow_Bold18x22 : Terminal6x8;
        const int ty = barBottom - bh - (f == Terminal6x8 ? 10 : 23);
        mtb_Kit_Text_Centre((int16_t)cx, (int16_t)(ty < 13 ? 13 : ty), f, KIT_TEXT, t, C_BG);
        if (i == lead && h.term > 0 && !tie) crown(cx, (ty < 13 ? 13 : ty) - 9 < 12 ? 12 : (ty < 13 ? 13 : ty) - 9);
        char nm[NAME_LEN + 2], fit[NAME_LEN + 2]; snprintf(nm, sizeof(nm), "%s", h.name); mtb_Kit_Upper(nm);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, nm, cw - 4);
        mtb_Kit_Text_Centre((int16_t)cx, 102, Terminal6x8, c, fit, C_BG);
        snprintf(t, sizeof(t), "%+ld WEEK", (long)h.week);
        if (cw < 50) snprintf(t, sizeof(t), "%+ld", (long)h.week);
        mtb_Kit_Text_Centre((int16_t)cx, 113, Terminal4x6, h.week > 0 ? KIT_GREEN : KIT_DIM, t, C_BG);
        if (showSel && i == selected) mtb_Panel_Draw_Rect((int16_t)(x + 1), 12, (int16_t)(x + cw - 2), 121, KIT_TEXT);
        if (fl){                                                                  // the points rising from the bar
            snprintf(t, sizeof(t), "%+d", flashPts);
            if (bh >= 24){                                                      // inside the bar, rising
                const int ry = barBottom - 20 - (int)(flashAge * (bh - 24) / FLASH_MS);
                mtb_Kit_Text_Centre((int16_t)cx, (int16_t)ry, Terminal10x17, BLACK, t, bc);
            } else mtb_Kit_Text_Centre((int16_t)cx, (int16_t)(barBottom - bh - 42 < 12 ? 12 : barBottom - bh - 42), Terminal8x12, flashPts > 0 ? KIT_GREEN : KIT_RED, t, C_BG);
        }
    }
}

static void cheer(int points){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t UP[] = {{659, 80}, {784, 80}, {1047, 200}};
    static const Snd_Note_t DOWN[] = {{392, 140}, {294, 260}};
    if (points > 0) snd_Play(UP, 3, 0.3f, false); else snd_Play(DOWN, 2, 0.25f, false);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void housePoints_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_House, cmd_Select, cmd_Amount, cmd_Give, cmd_Take, cmd_Award, cmd_Term, cmd_Sound);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    portENTER_CRITICAL(&lock);
    const int32_t wk = this_Week();
    if (wk && wk != save.weekOf){ for (int i = 0; i < save.n; i++) save.house[i].week = 0; save.weekOf = wk; }
    portEXIT_CRITICAL(&lock);
    turns = 0; knobClick = knobDouble = false; flashHouse = -1;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0, seenSeq = flashSeq, flashSince = 0, selUntil = 0;
    int selected = 0;
    bool push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        if (t && save.n){ turns -= t; selected = ((selected + t) % save.n + save.n) % save.n; selUntil = ms + 5000; lastDraw = 0; }
        else if (t) turns -= t;
        if ((knobClick || knobDouble) && save.n){
            if (selected >= save.n) selected = 0;
            portENTER_CRITICAL(&lock); award(selected, knobDouble ? -1 : 1); portEXIT_CRITICAL(&lock);
            selUntil = ms + 5000; push = saveNow = true;
        }
        knobClick = knobDouble = false;
        if (flashSeq != seenSeq){ seenSeq = flashSeq; flashSince = ms; cheer(flashPoints); lastDraw = 0; }
        const uint32_t age = ms - flashSince;
        if (ms - lastDraw >= (age < FLASH_MS ? 40u : 500u)){
            lastDraw = ms;
            Hpt_Save_t* s = &save;
            mtb_Panel_Begin_Update();
            draw_Board(*s, selected, (int32_t)(selUntil - ms) > 0, flashHouse, flashPoints, age);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void hpt_Values(JsonObject values){
    Hpt_Save_t* s = (Hpt_Save_t*)heap_caps_malloc(sizeof(Hpt_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); } else read_Save(*s);
    char t[80], key[8];
    for (int i = 0; i < HOUSES; i++){
        snprintf(key, sizeof(key), "name%d", i);
        values[key] = i < s->n ? s->house[i].name : (char*)"";
        snprintf(key, sizeof(key), "color%d", i);
        snprintf(t, sizeof(t), "0x%08lX", (unsigned long)(i < s->n ? s->house[i].argb : 0xFF888888u));
        values[key] = t;
    }
    values["house"] = s->selected;
    values["amount"] = s->amount;
    values["sound"] = s->sound;
    int order[HOUSES];
    for (int i = 0; i < s->n; i++) order[i] = i;
    for (int i = 1; i < s->n; i++) for (int j = i; j > 0 && s->house[order[j]].term > s->house[order[j - 1]].term; j--){ const int x = order[j]; order[j] = order[j - 1]; order[j - 1] = x; }
    JsonArray scores = values["scores"].to<JsonArray>();
    for (int k = 0; k < s->n; k++){
        const Hpt_House_t& h = s->house[order[k]];
        JsonObject r = scores.add<JsonObject>();
        snprintf(t, sizeof(t), "%d. %s (house %d)", k + 1, h.name, order[k] + 1);
        r["label"] = t;
        snprintf(t, sizeof(t), "%ld points, %+ld this week", (long)h.term, (long)h.week);
        r["value"] = t;
        r["state"] = k == 0 && h.term > 0 ? "ok" : "info";
    }
    JsonArray recent = values["recent"].to<JsonArray>();
    for (int k = 0; k < s->recentN; k++){
        const Hpt_Award_t& a = s->recent[k];
        if (a.house >= s->n) continue;
        JsonObject r = recent.add<JsonObject>();
        r["label"] = s->house[a.house].name;
        snprintf(t, sizeof(t), "%+d", a.points);
        r["value"] = t;
        r["state"] = a.points > 0 ? "ok" : "warn";
        if (a.at > 1700000000u){ snprintf(key, sizeof(key), "at%d", k); values[key] = a.at; r["ago"] = key; }
    }
    heap_caps_free(s);
}
