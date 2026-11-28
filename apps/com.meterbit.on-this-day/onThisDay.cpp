/**
 * On This Day: the day's anniversaries from Wikipedia's front page ("Selected anniversaries", otd_Wiki.cpp). One event
 * at a time: its year large, how long ago, and what happened; then the next. Along the bottom, scrolled by Pixlpal
 * (App API 0.7), who was born and who died on the date, and its observances. Fetched once a day (at midnight in
 * Pixlpal's time zone) and kept, so the day stays offline. Knob: turn for the event, click for the next.
 * NVS "otdSave" (settings), "otdDay" (the day kept).
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "mtb_text_scroll.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "otd_Wiki.h"
#include "otd_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "otdSave"
#define NVS_DAY     "otdDay"
#define SAVE_MAGIC  0x4F544401u
#define DAY_MAGIC   0x4F544402u
#define UA          "Pixlpal-OnThisDay/1.0 (https://github.com/Meterbit/pixlpal-apps)"
#define TICKER_Y    120
#define TOP_H       TICKER_Y            // the app draws rows 0 to 119; Pixlpal scrolls the ticker below

static const char* const MONTH[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September",
                                      "October", "November", "December"};

EXT_RAM_BSS_ATTR TaskHandle_t onThisDay_Task_H = NULL;
static void onThisDay_App_Task(void* dApplication);
static void otd_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* onThisDay_App;
Mtb_Applications* onThisDay_App_GetInstance(){
    if (!onThisDay_App){
        onThisDay_App = new Mtb_Applications_FullScreen(onThisDay_App_Task, &onThisDay_Task_H, "On This Day", {0,0}, 8192);
        onThisDay_App->mtb_App_Set_Mobile_UI(OTD_UI_MANIFEST, OTD_UI_API, nullptr, otd_Values);
    }
    return onThisDay_App;
}
MTB_REGISTER_APP(onThisDay_App, 0, 0)       // the catalogue gives the app its number

struct Otd_Save_t { uint32_t magic; uint8_t seconds, ticker, speak, pad; };
struct Otd_Kept_t { uint32_t magic; uint8_t month, day; uint8_t pad[2]; Otd_Day_t d; };

struct Otd_Shared_t {
    Otd_Kept_t kept;                    // the day shown (month 0: none yet)
    char why[44];
    uint32_t version;
    volatile bool stop, running;
};

static EXT_RAM_BSS_ATTR Otd_Save_t save;
static EXT_RAM_BSS_ATTR Otd_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false, nextWanted = false;

static void read_Save(Otd_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.seconds = 20; s.ticker = 1; }
    if (s.seconds < 5 || s.seconds > 120) s.seconds = 20;
    s.ticker = s.ticker ? 1 : 0;
    s.speak = s.speak ? 1 : 0;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static void cmd_Seconds(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["seconds"], 5, 120, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.seconds = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Ticker(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["ticker"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.ticker = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Speak(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["speak"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.speak = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Next(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } nextWanted = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) turns += 1; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static void ascii_Day(Otd_Day_t& d){
    char t[OTD_TEXT];
    mtb_Kit_To_Ascii(t, sizeof(t), d.observances); memcpy(d.observances, t, sizeof(d.observances));
    for (int i = 0; i < d.events; i++){
        mtb_Kit_To_Ascii(t, sizeof(t), d.event[i].text); memcpy(d.event[i].text, t, sizeof(d.event[i].text));
        mtb_Kit_To_Ascii(t, OTD_YEAR, d.event[i].year); memcpy(d.event[i].year, t, OTD_YEAR);
    }
    for (int i = 0; i < d.people; i++){
        Otd_Person_t& p = d.person[i];
        mtb_Kit_To_Ascii(t, sizeof(p.name), p.name); memcpy(p.name, t, sizeof(p.name));
        mtb_Kit_To_Ascii(t, sizeof(p.about), p.about); memcpy(p.about, t, sizeof(p.about));
        mtb_Kit_To_Ascii(t, OTD_YEAR, p.year); memcpy(p.year, t, OTD_YEAR);
    }
}

static int fetch_Day(int month, int day, Otd_Day_t& out){
    char url[200];
    snprintf(url, sizeof(url), "https://en.wikipedia.org/w/api.php?action=parse&page=Wikipedia:Selected_anniversaries/%s_%d"
             "&prop=wikitext&format=json&formatversion=2&maxlag=5", MONTH[month - 1], day);
    const char* headers[] = {"Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 60000, r, UA);
    int code = r.code;
    if (code == 200 && body){
        JsonDocument doc(mtb_Kit_Json_Allocator());
        JsonDocument filter;
        filter["parse"]["wikitext"] = true;
        if (r.truncated || deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) code = -2;
        else {
            const char* text = doc["parse"]["wikitext"] | (const char*)nullptr;
            if (!text || !otd_Parse(text, out)) code = -3;
            else ascii_Day(out);
        }
    }
    heap_caps_free(body);
    return code;
}

static void worker(void*){
    static Otd_Day_t fresh;
    uint32_t tryAt = 0;
    while (!sh.stop){
        const time_t now = time(nullptr);
        if (now > 1700000000){
            struct tm l;
            localtime_r(&now, &l);
            const uint8_t month = (uint8_t)(l.tm_mon + 1), day = (uint8_t)l.tm_mday;
            const bool have = sh.kept.month == month && sh.kept.day == day;
            if (!have && (!tryAt || millis() - tryAt >= 60000u)){
                tryAt = millis() ? millis() : 1;
                const int code = fetch_Day(month, day, fresh);
                portENTER_CRITICAL(&lock);
                if (code == 200){
                    sh.kept.magic = DAY_MAGIC; sh.kept.month = month; sh.kept.day = day;
                    memcpy(&sh.kept.d, &fresh, sizeof(fresh));
                    sh.why[0] = '\0';
                    tryAt = 0;
                } else if (code == -3) snprintf(sh.why, sizeof(sh.why), "WIKIPEDIA HAS NO LIST FOR TODAY YET");
                else if (code <= 0 && code != -2) snprintf(sh.why, sizeof(sh.why), "NO ANSWER FROM WIKIPEDIA");
                else if (code == -2) snprintf(sh.why, sizeof(sh.why), "WIKIPEDIA'S ANSWER WAS NOT READABLE");
                else snprintf(sh.why, sizeof(sh.why), "WIKIPEDIA: HTTP %d", code);
                sh.version++;
                portEXIT_CRITICAL(&lock);
                if (code == 200) mtb_Write_Nvs_Struct(NVS_DAY, &sh.kept, sizeof(sh.kept));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(14, 10, 6), C_HEAD = MTB_KIT_RGB(40, 26, 10), C_YEAR = MTB_KIT_RGB(255, 190, 90);
static const uint16_t C_PAPER = MTB_KIT_RGB(236, 226, 206);

static void header(const Otd_Kept_t& k, int index, int count, time_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_HEAD);
    char t[48];
    struct tm l;
    localtime_r(&now, &l);
    const int month = k.month ? k.month : l.tm_mon + 1, day = k.month ? k.day : l.tm_mday;
    snprintf(t, sizeof(t), "ON THIS DAY  %d %s", day, MONTH[month - 1]);
    mtb_Kit_Upper(t);
    mtb_Kit_Text(3, 1, Terminal6x8, C_YEAR, t, C_HEAD);
    // A dot an event, the one shown bright.
    for (int i = 0; i < count; i++){
        const int x = W - 4 - (count - 1 - i) * 6;
        mtb_Panel_Fill_Rect((int16_t)(x - 1), 3, (int16_t)(x + 1), 5, i == index ? C_YEAR : MTB_KIT_RGB(90, 70, 40));
    }
}

/** Years before now of a year like "1161", "44 BC", "AD 79"; -1 if it isn't one. */
static int years_Ago(const char* y, int thisYear){
    int n = 0, digits = 0;
    for (const char* p = y; *p; p++) if (*p >= '0' && *p <= '9'){ n = n * 10 + (*p - '0'); digits++; } else if (digits) break;
    if (!digits) return -1;
    if (strstr(y, "BC")) return thisYear + n - 1;      // no year 0
    return thisYear - n;
}

static void draw_Event(const Otd_Kept_t& k, int index, time_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, TOP_H - 1, C_BG);
    const int count = k.d.events + (k.d.people ? 1 : 0);
    header(k, index, count, now);
    char t[64];
    if (index >= k.d.events){
        // The births and deaths, as a list.
        mtb_Kit_Text(4, 14, Terminal6x8, C_YEAR, "BORN AND DIED", C_BG);
        int y = 26;
        for (int i = 0; i < k.d.people && y < TOP_H - 8; i++){
            const Otd_Person_t& p = k.d.person[i];
            snprintf(t, sizeof(t), "%s %s", p.died ? "D." : "B.", p.year);
            mtb_Kit_Text(4, y, Terminal6x8, p.died ? KIT_LABEL : KIT_GREEN, t, C_BG);
            char line[96], f[64];
            snprintf(line, sizeof(line), "%s%s%s", p.name, p.about[0] ? ", " : "", p.about);
            mtb_Kit_Fit(f, sizeof(f), Terminal6x8, line, W - 60);
            mtb_Kit_Text(56, y, Terminal6x8, C_PAPER, f, C_BG);
            y += 11;
        }
        return;
    }
    const Otd_Event_t& e = k.d.event[index];
    struct tm l;
    localtime_r(&now, &l);
    const int x = mtb_Kit_Text(4, 13, PT_Sans_Narrow_Bold28x34, C_YEAR, e.year, C_BG);
    const int ago = years_Ago(e.year, l.tm_year + 1900);
    if (ago > 0){
        snprintf(t, sizeof(t), "%d YEAR%s AGO", ago, ago == 1 ? "" : "S");
        mtb_Kit_Text(x + 8, 33, Terminal6x8, KIT_LABEL, t, C_BG);
    }
    // What happened, wrapped; the part before a colon (the war, the campaign) on its own line in colour.
    const char* text = e.text;
    const char* colon = strstr(text, ": ");
    int y = 50;
    if (colon && colon - text < 40){
        char lead[44];
        snprintf(lead, sizeof(lead), "%.*s", (int)(colon - text), text);
        mtb_Kit_Upper(lead);
        mtb_Kit_Text(4, y, Terminal4x6, KIT_CYAN, lead, C_BG);
        y += 8;
        text = colon + 2;
    }
    const int maxLines = (TOP_H - 2 - y) / 9;
    char lines[8][64];
    const int n = mtb_Kit_Wrap(text, Terminal6x8, W - 8, &lines[0][0], sizeof(lines[0]), maxLines < 8 ? maxLines : 8);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, y + i * 9, Terminal6x8, C_PAPER, lines[i], C_BG);
}

static void draw_Waiting(const Otd_Shared_t& d, time_t now){
    mtb_Panel_Fill_Rect(0, 0, W - 1, TOP_H - 1, C_BG);
    header(d.kept, -1, 0, now);
    const char* what = now <= 1700000000 ? "WAITING FOR THE TIME..." : d.why[0] ? d.why : "ASKING WIKIPEDIA...";
    mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, what, C_BG);
}

// The ticker's text: who was born and who died, and the observances.
static void ticker_Text(const Otd_Kept_t& k, char* t, size_t n){
    size_t used = 0;
    t[0] = '\0';
    if (k.d.observances[0]) used += snprintf(t + used, n - used, "TODAY: %s     ", k.d.observances);
    for (int i = 0; i < k.d.people && used < n; i++){
        const Otd_Person_t& p = k.d.person[i];
        used += snprintf(t + used, n - used, "%s %s (%s)%s%s     ", p.died ? "DIED:" : "BORN:", p.name, p.year, p.about[0] ? ", " : "", p.about);
    }
    if (used >= n) used = n - 1;
    t[used] = '\0';
}

static void speak_Event(const Otd_Kept_t& k, int index){
    if (index >= k.d.events) return;
    char t[200];
    const Otd_Event_t& e = k.d.event[index];
    snprintf(t, sizeof(t), "In %s. %s", e.year, e.text);
    snd_Say(t, "en", nullptr, 0, 0.3f, false);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void onThisDay_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Seconds, cmd_Ticker, cmd_Speak, cmd_Next);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; phoneChanged = saveWanted = nextWanted = false;
    memset(&sh, 0, sizeof(sh));
    mtb_Read_Nvs_Struct_If_Saved(NVS_DAY, &sh.kept, sizeof(sh.kept));
    if (sh.kept.magic != DAY_MAGIC || sh.kept.month < 1 || sh.kept.month > 12 || sh.kept.d.events < 0 || sh.kept.d.events > OTD_EVENTS
        || sh.kept.d.people < 0 || sh.kept.d.people > OTD_PEOPLE) memset(&sh.kept, 0, sizeof(sh.kept));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "otdNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Otd_Shared_t* d = (Otd_Shared_t*)heap_caps_calloc(1, sizeof(Otd_Shared_t), MALLOC_CAP_SPIRAM);
    char* tickerText = (char*)heap_caps_calloc(1, 1000, MALLOC_CAP_SPIRAM);
    if (!d || !tickerText){ sh.stop = true; appOpen = false; heap_caps_free(d); heap_caps_free(tickerText); mtb_Delete_This_App(THIS_APP); return; }
    Mtb_ScrollText_t ticker(0, TICKER_Y, W, Terminal6x8, MTB_KIT_RGB(255, 200, 120), 25, 1);
    ticker.backgroundColor = C_BG;
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, shownMs = millis();
    uint8_t shownDay = 0;
    int index = 0;
    bool redraw = true, push = true, speakNow = false, tickerOn = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const time_t now = time(nullptr);
        if (phoneChanged){ phoneChanged = false; redraw = push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        const int count = d->kept.d.events + (d->kept.d.people ? 1 : 0);
        if (d->kept.month && d->kept.day != shownDay){ shownDay = d->kept.day; index = 0; shownMs = ms; redraw = true; speakNow = save.speak; tickerOn = false; }
        int step = turns;
        if (step) turns -= step;
        if (nextWanted){ nextWanted = false; step += 1; }
        if (!step && count && ms - shownMs >= save.seconds * 1000u) step = 1;
        if (step && count){ index = ((index + step) % count + count) % count; shownMs = ms; redraw = true; speakNow = save.speak; push = true; }
        if (redraw){
            redraw = false;
            if (count) draw_Event(d->kept, index < count ? index : 0, now); else draw_Waiting(*d, now);
        }
        if (speakNow){ speakNow = false; if (count) speak_Event(d->kept, index); }
        // The ticker: started again when it has run through, so it keeps going.
        if (save.ticker && count && (d->kept.d.people || d->kept.d.observances[0])){
            if (!tickerOn || !ticker.mtb_Scroll_Active()){
                ticker_Text(d->kept, tickerText, 1000);
                ticker.mtb_Scroll_This_Text(tickerText);
                tickerOn = true;
            }
        } else if (tickerOn){ ticker.mtb_Scroll_Active(STOP_SCROLL); tickerOn = false; mtb_Panel_Fill_Rect(0, TICKER_Y, W - 1, H - 1, C_BG); }
        else mtb_Panel_Fill_Rect(0, TICKER_Y, W - 1, H - 1, C_BG);
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 3000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    ticker.mtb_Scroll_Active(STOP_SCROLL);
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    heap_caps_free(tickerText);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void otd_Values(JsonObject values){
    Otd_Save_t s;
    Otd_Kept_t* k = (Otd_Kept_t*)heap_caps_calloc(1, sizeof(Otd_Kept_t), MALLOC_CAP_SPIRAM);
    if (!k) return;
    char why[44] = "";
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(k, &sh.kept, sizeof(*k)); memcpy(why, sh.why, sizeof(why)); portEXIT_CRITICAL(&lock); }
    else { read_Save(s); mtb_Read_Nvs_Struct_If_Saved(NVS_DAY, k, sizeof(*k)); if (k->magic != DAY_MAGIC || k->d.events > OTD_EVENTS || k->d.people > OTD_PEOPLE) memset(k, 0, sizeof(*k)); }
    JsonArray rows = values["events"].to<JsonArray>();
    if (!k->month){
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Today";
        o["value"] = why[0] ? why : "Not fetched yet";
        o["state"] = why[0] ? "warn" : "off";
    } else {
        char t[48];
        if (k->d.observances[0]){
            JsonObject o = rows.add<JsonObject>();
            o["label"] = "Today"; o["value"] = k->d.observances; o["state"] = "ok";
        }
        for (int i = 0; i < k->d.events && i < OTD_EVENTS; i++){
            JsonObject o = rows.add<JsonObject>();
            o["label"] = k->d.event[i].year; o["value"] = k->d.event[i].text; o["state"] = "info";
        }
        for (int i = 0; i < k->d.people && i < OTD_PEOPLE; i++){
            const Otd_Person_t& p = k->d.person[i];
            JsonObject o = rows.add<JsonObject>();
            snprintf(t, sizeof(t), "%s %s", p.died ? "Died" : "Born", p.year);
            o["label"] = t;
            char line[110];
            snprintf(line, sizeof(line), "%s%s%s", p.name, p.about[0] ? ", " : "", p.about);
            o["value"] = line; o["state"] = "off";
        }
    }
    values["seconds"] = s.seconds;
    values["ticker"] = s.ticker;
    values["speak"] = s.speak;
    heap_caps_free(k);
}
