/**
 * Commute & Departures app, ID {10,0}: live departures from your stops (Transitland: thousands of transit agencies,
 * live times where they publish them) and drive times with traffic between your places (TomTom or Google), with a
 * chime when it is time to leave.
 *
 * Up to six boards, each a stop's departures (chosen lines, the walk to the stop) or a drive between two places (an
 * arrive-by time gives a leave-by time). Each board is fetched while it is active (its days and hours), paced so
 * each provider's free month lasts; the departures of the next two hours count down on Pixlpal between fetches.
 *
 * The knob: click, the next board (or dismiss an alert); double click, refresh now; long press, the overview;
 * turn, brightness.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_audio_hw.h"
#include "esp_heap_caps.h"
#include "cmt_App.h"
#include "cmt_Ui.h"
#include "cmt_Mobile_Ui.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

static const char TAG[] = "COMMUTE";

EXT_RAM_BSS_ATTR TaskHandle_t commute_Task_H = NULL;
static void commute_App_Task(void* dApplication);
static void cmt_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *commute_App;
Mtb_Applications* commute_App_GetInstance() {
    if (!commute_App){
        commute_App = new Mtb_Applications_StatusBar(commute_App_Task, &commute_Task_H, "Commute", {10,0}, 8192);
        commute_App->mtb_App_Set_Mobile_UI(CMT_UI_MANIFEST, CMT_UI_API, nullptr, cmt_Values);
    }
    return commute_App;
}
MTB_REGISTER_APP(commute_App, 10, 0)

EXT_RAM_BSS_ATTR static Cmt_Settings_t settings;
EXT_RAM_BSS_ATTR static Cmt_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile uint8_t placeSlot = 0;          // the place the phone's address search sets

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

static bool read_Settings(Cmt_Settings_t& s){
    cmt_Settings_Default(s);
    Cmt_Settings_t* saved = (Cmt_Settings_t*)heap_caps_calloc(1, sizeof(Cmt_Settings_t), MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (saved){
        mtb_Read_Nvs_Struct(CMT_NVS_KEY, saved, sizeof(*saved));
        ok = saved->magic == CMT_SETTINGS_MAGIC;
        if (ok) s = *saved;
        heap_caps_free(saved);
    }
    cmt_Settings_Clamp(s);
    return ok;
}

// Two short notes, then one lower: time to go.
static void chime(void){
    if (!mtb_Audio_Pcm_Begin()) return;
    const int rate = 48000, frames = 480;
    int32_t* buffer = (int32_t*)heap_caps_malloc(frames * 2 * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    if (buffer){
        static const float NOTES[] = {1046.5f, 1046.5f, 784.0f};
        const int noteFrames = rate * 12 / 100, fade = rate / 200;
        for (float f : NOTES){
            for (int done = 0; done < noteFrames; done += frames){
                for (int i = 0; i < frames; i++){
                    const int n = done + i;
                    float env = n < fade ? (float)n / fade : n > noteFrames - fade * 6 ? (float)(noteFrames - n) / (fade * 6) : 1.0f;
                    if (env < 0) env = 0;
                    const float v = sinf(2.0f * (float)M_PI * f * n / rate) * env * 0.24f;
                    buffer[2 * i] = buffer[2 * i + 1] = (int32_t)(v * 2147483647.0f);
                }
                mtb_Audio_Pcm_Write(buffer, frames, 200);
            }
            memset(buffer, 0, frames * 2 * sizeof(int32_t));
            for (int k = 0; k < 3; k++) mtb_Audio_Pcm_Write(buffer, frames, 200);
        }
        for (int k = 0; k < 8; k++) mtb_Audio_Pcm_Write(buffer, frames, 200);
        heap_caps_free(buffer);
    }
    mtb_Audio_Pcm_End();
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------
static Cmt_Settings_t* begin_Change(void){
    Cmt_Settings_t* next = (Cmt_Settings_t*)heap_caps_malloc(sizeof(Cmt_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void end_Change(Cmt_Settings_t* next){
    cmt_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static bool number(JsonDocument& c, const char* key, long lo, long hi, long& v){
    return mtb_Kit_Json_Range(c[key], lo, hi, v);
}

static void set_Byte(JsonDocument& c, const char* key, long lo, long hi, uint8_t Cmt_Settings_t::* field){
    long v;
    if (!number(c, key, lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)v;
    end_Change(next);
}

// The board the phone edits (it is in use from its first setting).
static void set_Board(JsonDocument& c, const char* key, long lo, long hi, uint8_t Cmt_Board_t::* field){
    long v;
    if (!number(c, key, lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Board_t& b = next->boards[next->editing];
    if (!b.used){ b.used = 1; b.to = 1; b.arriveBy = 0xFFFF; }
    b.*field = (uint8_t)v;
    end_Change(next);
}

static void set_Key(JsonDocument& c, const char* key, char* Cmt_Settings_t::* unused, size_t offset){
    (void)unused;
    const char* v = c[key] | "";
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\n' || v[n - 1] == '\r')) n--;
    if (n >= CMT_KEY_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (size_t i = 0; i < n; i++) if (v[i] <= ' ' || v[i] > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    char* dest = (char*)next + offset;
    memcpy(dest, v, n);
    dest[n] = '\0';
    end_Change(next);
}

static void cmd_Ack(JsonDocument&){}
static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }
static void cmd_Transit_Key(JsonDocument& c){ set_Key(c, "transitKey", nullptr, offsetof(Cmt_Settings_t, transitKey)); }
static void cmd_Plan(JsonDocument& c){ set_Byte(c, "plan", 0, CMT_PLAN_PAYG, &Cmt_Settings_t::plan); }
static void cmd_Payg_Cap(JsonDocument& c){
    long v;
    if (!number(c, "paygCap", 1000, 60000, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->paygCap = (uint16_t)v;
    end_Change(next);
}
static void cmd_Drive(JsonDocument& c){ set_Byte(c, "drive", 0, CMT_GOOGLE, &Cmt_Settings_t::drive); }
static void cmd_TomTom_Key(JsonDocument& c){ set_Key(c, "tomtomKey", nullptr, offsetof(Cmt_Settings_t, tomtomKey)); }
static void cmd_Google_Key(JsonDocument& c){ set_Key(c, "googleKey", nullptr, offsetof(Cmt_Settings_t, googleKey)); }
static void cmd_Google_Cap(JsonDocument& c){
    long v;
    if (!number(c, "googleCap", 0, 60000, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->googleCap = (uint16_t)v;
    end_Change(next);
}

static void cmd_Place_Slot(JsonDocument& c){
    long v;
    if (!number(c, "placeSlot", 0, CMT_PLACES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    placeSlot = (uint8_t)v;
}

static void search(JsonDocument& c, const char* id, size_t minLength){
    const char* q = c["query"] | "";
    if (strlen(q) < minLength || strlen(q) > 60 || !cmt_Net_Search(id, q)) mtb_Ble_App_Cmd_Reject();
}
static void cmd_Search_Address(JsonDocument& c){ search(c, "address", 3); }

// {"address": {name: "12 Allen Avenue, Ikeja", value: "6.601800,3.351500"}}
static void cmd_Address(JsonDocument& c){
    const char* value = c["address"]["value"] | "";
    double lat, lon;
    if (sscanf(value, "%lf,%lf", &lat, &lon) != 2 || lat < -90 || lat > 90 || lon < -180 || lon > 180){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Place_t& p = next->places[placeSlot];
    p.set = 1;
    p.latE6 = (int32_t)lround(lat * 1e6);
    p.lonE6 = (int32_t)lround(lon * 1e6);
    copy(p.address, sizeof(p.address), c["address"]["name"] | "");
    end_Change(next);
}

static void cmd_Editing(JsonDocument& c){ set_Byte(c, "editing", 0, CMT_BOARDS - 1, &Cmt_Settings_t::editing); }
static void cmd_Kind(JsonDocument& c){ set_Board(c, "kind", 0, CMT_DRIVE_BOARD, &Cmt_Board_t::kind); }

static void cmd_Label(JsonDocument& c){
    const char* v = c["label"] | "";
    if (strlen(v) >= sizeof(Cmt_Board_t::label)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Board_t& b = next->boards[next->editing];
    if (!b.used){ b.used = 1; b.to = 1; b.arriveBy = 0xFFFF; }
    copy(b.label, sizeof(b.label), v);
    end_Change(next);
}

static void cmd_Search_Stop(JsonDocument& c){ search(c, "stop", 2); }

static void cmd_Stop(JsonDocument& c){
    const char* key = c["stop"]["value"] | "";
    if (key[0] == '\0' || strlen(key) >= sizeof(Cmt_Board_t::stopKey)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Board_t& b = next->boards[next->editing];
    if (!b.used){ b.used = 1; b.to = 1; b.arriveBy = 0xFFFF; }
    b.kind = CMT_DEPARTURES_BOARD;
    if (strcmp(b.stopKey, key) != 0) b.lines[0] = '\0';         // another stop: its own lines
    copy(b.stopKey, sizeof(b.stopKey), key);
    copy(b.stopName, sizeof(b.stopName), c["stop"]["name"] | "Stop");
    end_Change(next);
}

static void cmd_Search_Lines(JsonDocument& c){ search(c, "lines", 0); }

static void cmd_Lines(JsonDocument& c){
    if (!c["lines"].is<JsonArray>()){ mtb_Ble_App_Cmd_Reject(); return; }
    char list[48] = "";
    size_t used = 0;
    for (JsonVariant v : c["lines"].as<JsonArray>()){
        const char* line = v["value"] | "";
        if (!line[0]) continue;
        const int n = snprintf(list + used, sizeof(list) - used, "%s%s", used ? "," : "", line);
        if (n < 0 || used + n >= sizeof(list)){ mtb_Ble_App_Cmd_Reject(); return; }
        used += n;
    }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    copy(next->boards[next->editing].lines, sizeof(Cmt_Board_t::lines), list);
    end_Change(next);
}

static void cmd_Walk(JsonDocument& c){ set_Board(c, "walk", 0, 60, &Cmt_Board_t::walkMinutes); }
static void cmd_From(JsonDocument& c){ set_Board(c, "from", 0, CMT_PLACES - 1, &Cmt_Board_t::from); }
static void cmd_To(JsonDocument& c){ set_Board(c, "to", 0, CMT_PLACES - 1, &Cmt_Board_t::to); }
static void cmd_Miles(JsonDocument& c){ set_Byte(c, "miles", 0, 1, &Cmt_Settings_t::miles); }

static void cmd_Arrive_By(JsonDocument& c){
    const int m = mtb_Kit_Parse_Clock(c["arriveBy"] | "");
    if (m < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Board_t& b = next->boards[next->editing];
    if (!b.used){ b.used = 1; b.to = 1; }
    b.arriveBy = (uint16_t)m;
    end_Change(next);
}

static void cmd_Days(JsonDocument& c){ set_Board(c, "days", 0, CMT_DAYS_WEEKENDS, &Cmt_Board_t::days); }

static void cmd_Window(JsonDocument& c){
    const int a = mtb_Kit_Parse_Clock(c["windowFrom"] | ""), z = mtb_Kit_Parse_Clock(c["windowTo"] | "");
    if (a < 0 || z < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Board_t& b = next->boards[next->editing];
    if (!b.used){ b.used = 1; b.to = 1; b.arriveBy = 0xFFFF; }
    b.fromMinute = (uint16_t)a;
    b.toMinute = (uint16_t)z;
    end_Change(next);
}

static void cmd_Alert(JsonDocument& c){ set_Board(c, "alert", 0, 1, &Cmt_Board_t::alert); }

static void cmd_Remove(JsonDocument&){
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    memset(&next->boards[next->editing], 0, sizeof(Cmt_Board_t));
    end_Change(next);
}

static void cmd_Hour24(JsonDocument& c){ set_Byte(c, "hour24", 0, 1, &Cmt_Settings_t::hour24); }
static void cmd_Chime(JsonDocument& c){ set_Byte(c, "chime", 0, 1, &Cmt_Settings_t::chime); }
static void cmd_Alert_Minutes(JsonDocument& c){ set_Byte(c, "alertMinutes", 0, 30, &Cmt_Settings_t::alertMinutes); }

static void cmd_Avoid(JsonDocument& c){
    long v;
    if (!number(c, "avoid", 0, 3, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cmt_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->avoidTolls = (v & 1) ? 1 : 0;
    next->avoidHighways = (v & 2) ? 1 : 0;
    end_Change(next);
}

static void register_Commands(Mtb_Applications* app){
    app->mtb_App_Set_Ble_Comm_Fns(cmd_Ack, cmd_Refresh, cmd_Transit_Key, cmd_Plan, cmd_Payg_Cap, cmd_Drive, cmd_TomTom_Key, cmd_Google_Key,
                                  cmd_Google_Cap, cmd_Place_Slot, cmd_Search_Address, cmd_Address);
    static const bleCom_Parser_Fns_Ptr MORE[] = {cmd_Editing, cmd_Kind, cmd_Label, cmd_Search_Stop, cmd_Stop, cmd_Search_Lines, cmd_Lines,
                                                 cmd_Walk, cmd_From, cmd_To, cmd_Miles, cmd_Arrive_By, cmd_Days, cmd_Window, cmd_Alert,
                                                 cmd_Remove, cmd_Hour24, cmd_Chime, cmd_Alert_Minutes, cmd_Avoid};
    for (uint8_t i = 0; i < sizeof(MORE) / sizeof(MORE[0]); i++) app->mtb_App_Set_Ble_Comm_Fn(12 + i, MORE[i]);
}

// The knob ------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}

// The board after @p board (-1: the overview), in use ones only, then the overview again.
static int next_Board(const Cmt_Settings_t& s, int board){
    for (int i = board + 1; i < CMT_BOARDS; i++) if (s.boards[i].used) return i;
    return -1;
}

// The app ---------------------------------------------------------------------------------------------------------
static void commute_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, mtb_Brightness_Control);
    register_Commands(THIS_APP);
    THIS_APP->mtb_App_Init();

    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(CMT_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    longPressRequested = false;
    clicksRequested = 0;
    Cmt_Result_t* results = (Cmt_Result_t*)heap_caps_calloc(CMT_BOARDS, sizeof(Cmt_Result_t), MALLOC_CAP_SPIRAM);
    if (results == nullptr || !cmt_Net_Start(settings)){
        heap_caps_free(results);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    appOpen = true;
    Cmt_Status_t status = {};
    uint32_t version = 0, lastSecond = 0, phoneMs = 0, alertUntil = 0;
    int board = next_Board(settings, -1);              // the first board, or the overview if none
    bool redraw = true, phoneDirty = true, flash = false, wasOnline = Mtb_Applications::internetConnectStatus;
    char alert[32] = "", alertDetail[80] = "";
    uint32_t alerted[CMT_BOARDS] = {};                 // the departure (or day) last alerted for, per board

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        const bool online = Mtb_Applications::internetConnectStatus;
        if (online != wasOnline){ wasOnline = online; redraw = true; }
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            mtb_Write_Nvs_Struct(CMT_NVS_KEY, &settings, sizeof(settings));
            cmt_Net_Set_Settings(settings);
            if (board >= 0 && !settings.boards[board].used) board = -1;
            if (board < 0 && cmt_Board_Count(settings) == 1) board = next_Board(settings, -1);
            redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; cmt_Net_Refresh(); }
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        if (clicks == 1 && alertUntil){ alertUntil = 0; redraw = true; }
        else if (clicks == 1){ board = next_Board(settings, board); redraw = true; }
        if (clicks == 2) cmt_Net_Refresh();
        if (longPressRequested){ longPressRequested = false; board = -1; redraw = true; }

        if (cmt_Net_Copy(results, status, version)) redraw = phoneDirty = true;

        // Each second: countdowns, and whether it is time to leave.
        bool flashOnly = false;
        if (now != lastSecond && now > 1700000000u){
            lastSecond = now;
            if (now % 15 == 0) redraw = true;
            const time_t tt = (time_t)now;
            struct tm lt;
            localtime_r(&tt, &lt);
            const int weekday = (lt.tm_wday + 6) % 7, minute = lt.tm_hour * 60 + lt.tm_min;
            for (int i = 0; i < CMT_BOARDS && !alertUntil; i++){
                const Cmt_Board_t& b = settings.boards[i];
                if (!b.used || !b.alert || !cmt_Active(b, weekday, minute)) continue;
                const uint32_t lead = (uint32_t)settings.alertMinutes * 60;
                if (b.kind == CMT_DEPARTURES_BOARD){
                    const Cmt_Departure_t* d;
                    const uint32_t leave = cmt_Leave_At(b, results[i], now, &d);
                    if (!leave || leave > now + lead || alerted[i] == d->time) continue;
                    alerted[i] = d->time;
                    const int m = leave > now ? (int)((leave - now + 59) / 60) : 0;
                    if (m) snprintf(alert, sizeof(alert), "LEAVE IN %d MIN", m); else snprintf(alert, sizeof(alert), "LEAVE NOW");
                    char when[16];
                    cmt_Minutes_Text(when, sizeof(when), d->time, now, settings.hour24);
                    snprintf(alertDetail, sizeof(alertDetail), "%s to %.30s, %s", d->line, d->headsign, when);
                } else {
                    const int leaveBy = results[i].drive.ok ? cmt_Leave_By(b.arriveBy, results[i].drive.seconds) : -1;
                    const uint32_t day = now / 86400;
                    if (leaveBy < 0 || alerted[i] == day) continue;
                    const int until = leaveBy - minute;
                    if (until < 0 || until > (int)settings.alertMinutes) continue;
                    alerted[i] = day;
                    if (until) snprintf(alert, sizeof(alert), "LEAVE IN %d MIN", until); else snprintf(alert, sizeof(alert), "LEAVE NOW");
                    snprintf(alertDetail, sizeof(alertDetail), "%s to %s: %d min with traffic", cmt_Place_Name(b.from), cmt_Place_Name(b.to),
                             (int)((results[i].drive.seconds + 59) / 60));
                }
                alertUntil = ms + 90000;
                board = i;
                redraw = true;
                if (settings.chime && !mtb_Silent_Mode()){
                    Cmt_View_t view = {&settings, results, &status, now, board, online, alert, alertDetail, true};
                    cmt_Draw(view);
                    chime();
                    redraw = false;
                }
            }
            if (alertUntil){ flash = !flash; flashOnly = true; }
        }
        if (alertUntil && (int32_t)(ms - alertUntil) >= 0){ alertUntil = 0; redraw = true; }
        Cmt_View_t view = {&settings, results, &status, now, board, online, alertUntil ? alert : nullptr, alertDetail, flash};
        if (redraw){ redraw = false; cmt_Draw(view); }
        else if (flashOnly) cmt_Draw_Alert(view);
        if (phoneDirty && ms - phoneMs > 3000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    cmt_Net_Stop();
    heap_caps_free(results);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------
static void clock_Value(JsonObject v, const char* key, uint16_t minute){
    char t[8];
    snprintf(t, sizeof(t), "%02u:%02u", (unsigned)(minute / 60 % 24), (unsigned)(minute % 60));
    v[key] = t;
}

static void cmt_Values(JsonObject values){
    Cmt_Settings_t* s = (Cmt_Settings_t*)heap_caps_malloc(sizeof(Cmt_Settings_t), MALLOC_CAP_SPIRAM);
    Cmt_Result_t* results = (Cmt_Result_t*)heap_caps_calloc(CMT_BOARDS, sizeof(Cmt_Result_t), MALLOC_CAP_SPIRAM);
    if (s == nullptr || results == nullptr){ heap_caps_free(s); heap_caps_free(results); return; }
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *s = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*s);
    Cmt_Status_t st = {};
    uint32_t version = 0xFFFFFFFFu;
    const bool live = appOpen && cmt_Net_Copy(results, st, version);
    if (!live) mtb_Read_Nvs_Struct(CMT_USAGE_KEY, &st.usage, sizeof(st.usage));
    char t[160];

    // Keys and plans (keys never leave Pixlpal: only whether they are set).
    values["transitKey_set"] = s->transitKey[0] ? 1 : 0;
    values["tomtomKey_set"] = s->tomtomKey[0] ? 1 : 0;
    values["googleKey_set"] = s->googleKey[0] ? 1 : 0;
    values["plan"] = s->plan;
    values["paygCap"] = s->paygCap;
    values["drive"] = s->drive;
    values["googleCap"] = s->googleCap;
    values["hour24"] = s->hour24;
    values["chime"] = s->chime;
    values["alertMinutes"] = s->alertMinutes;
    values["miles"] = s->miles;
    values["avoid"] = (s->avoidTolls ? 1 : 0) | (s->avoidHighways ? 2 : 0);
    values["placeSlot"] = placeSlot;
    JsonArray usage = values["usage"].to<JsonArray>();
    {
        JsonObject r = usage.add<JsonObject>();
        r["label"] = "Transitland";
        snprintf(t, sizeof(t), "%u of %u requests this month%s", (unsigned)st.usage.transit, (unsigned)cmt_Transit_Monthly(*s),
                 st.transitSpent ? " (used up)" : "");
        r["value"] = t;
        r["state"] = st.transitSpent ? "error" : st.usage.transit > cmt_Transit_Monthly(*s) * 8 / 10 ? "warn" : "ok";
        r = usage.add<JsonObject>();
        r["label"] = s->drive == CMT_GOOGLE ? "Google Routes" : "TomTom";
        snprintf(t, sizeof(t), "%u of %u routes, %u address lookups", (unsigned)st.usage.route, (unsigned)cmt_Route_Monthly(*s), (unsigned)st.usage.geocode);
        r["value"] = t;
        r["state"] = st.routeSpent ? "error" : "ok";
        if (live){
            r = usage.add<JsonObject>();
            r["label"] = "Refresh";
            snprintf(t, sizeof(t), "Departures every %u s, drives every %u s (while active)", (unsigned)st.nextTransit, (unsigned)st.nextRoute);
            r["value"] = t;
            r["state"] = "info";
        }
    }
    // Places.
    JsonArray places = values["places"].to<JsonArray>();
    for (int i = 0; i < CMT_PLACES; i++){
        JsonObject r = places.add<JsonObject>();
        r["label"] = cmt_Place_Name(i);
        r["value"] = s->places[i].set ? s->places[i].address : "Not set";
        r["state"] = s->places[i].set ? "ok" : "off";
    }
    // Boards, and the one being edited.
    JsonArray boards = values["boards"].to<JsonArray>();
    for (int i = 0; i < CMT_BOARDS; i++){
        const Cmt_Board_t& b = s->boards[i];
        if (!b.used) continue;
        JsonObject r = boards.add<JsonObject>();
        snprintf(t, sizeof(t), "Board %d", i + 1);
        r["label"] = t;
        const Cmt_Result_t& x = results[i];
        if (b.kind == CMT_DRIVE_BOARD){
            if (live && x.drive.ok) snprintf(t, sizeof(t), "%s to %s: %u min with traffic", cmt_Place_Name(b.from), cmt_Place_Name(b.to), (unsigned)((x.drive.seconds + 59) / 60));
            else snprintf(t, sizeof(t), "%s to %s%s%s", cmt_Place_Name(b.from), cmt_Place_Name(b.to), x.state == CMT_FAILED ? ": " : "", x.state == CMT_FAILED ? x.error : "");
        } else {
            if (live && x.count){
                const uint32_t now = (uint32_t)time(nullptr);
                char when[16];
                cmt_Minutes_Text(when, sizeof(when), x.departures[0].time, now, s->hour24);
                snprintf(t, sizeof(t), "%s: %s to %s, %s", b.stopName, x.departures[0].line, x.departures[0].headsign, when);
            } else snprintf(t, sizeof(t), "%s%s%s", b.stopName[0] ? b.stopName : "No stop yet", x.state == CMT_FAILED ? ": " : "", x.state == CMT_FAILED ? x.error : "");
        }
        r["value"] = t;
        r["state"] = !live ? "info" : x.state == CMT_FAILED ? "error" : x.state == CMT_OK ? "ok" : "info";
    }
    const Cmt_Board_t& e = s->boards[s->editing];
    values["editing"] = s->editing;
    values["kind"] = e.kind;
    values["label"] = e.label;
    if (e.stopKey[0]){ JsonObject o = values["stop"].to<JsonObject>(); o["name"] = e.stopName; o["value"] = e.stopKey; }
    JsonArray lines = values["lines"].to<JsonArray>();
    if (e.lines[0]){
        char list[48];
        copy(list, sizeof(list), e.lines);
        for (char* tok = strtok(list, ","); tok; tok = strtok(nullptr, ",")){ JsonObject o = lines.add<JsonObject>(); o["name"] = tok; o["value"] = tok; }
    }
    values["walk"] = e.walkMinutes;
    values["from"] = e.from;
    values["to"] = e.to;
    clock_Value(values, "arriveBy", e.arriveBy < 1440 ? e.arriveBy : 8 * 60 + 30);
    values["days"] = e.days;
    clock_Value(values, "windowFrom", e.fromMinute);
    clock_Value(values, "windowTo", e.toMinute);
    values["alert"] = e.alert;
    snprintf(t, sizeof(t), e.used ? "Board %d is in use" : "Board %d is empty: set it below", s->editing + 1);
    values["editingNote"] = t;
    heap_caps_free(results);
    heap_caps_free(s);
}
