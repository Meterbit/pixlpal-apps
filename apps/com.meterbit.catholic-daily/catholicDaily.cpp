/**
 * Catholic Daily app, ID {13,2} (FAITH): today's liturgical day from the General Roman Calendar (the celebration, its
 * rank, the season and week, in the liturgical colour), the Angelus (the Regina Caeli in Eastertide) at 6:00, 12:00 and
 * 18:00 with a church bell, the Hour of Mercy at 15:00, the Rosary's mysteries of the day, a verse of the day from the
 * Douay-Rheims, and reminders (the Rosary, Sunday Mass, an examination of conscience).
 *
 * The knob: click, the next page (or dismiss a devotion and stop the bell); long press, another verse (on the verse
 * page) or stop the bell; double click, read again; turn, scroll the verse (on its page) or brightness.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include "fth_Sound.h"
#include "fth_Ui.h"
#include "fth_Net.h"
#include "fth_Worker.h"
#include "cth_Model.h"
#include "cth_Ui.h"
#include "cth_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t catholic_Task_H = NULL;
static void catholic_App_Task(void* dApplication);
static void cth_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *catholic_App;
Mtb_Applications* catholic_App_GetInstance() {
    if (!catholic_App){
        catholic_App = new Mtb_Applications_StatusBar(catholic_App_Task, &catholic_Task_H, "Catholic Daily", {13,2}, 8192);
        catholic_App->mtb_App_Set_Mobile_UI(CTH_UI_MANIFEST, CTH_UI_API, nullptr, cth_Values);
    }
    return catholic_App;
}
MTB_REGISTER_APP(catholic_App, 13, 2)

EXT_RAM_BSS_ATTR static Cth_Settings_t settings;
EXT_RAM_BSS_ATTR static Cth_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, longPressRequested = false, testRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;
// The liturgical day, written by the worker's daily job.
static portMUX_TYPE dayLock = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static Cth_Day_t liturgicalDay;
EXT_RAM_BSS_ATTR static char dayError[80];
static volatile uint8_t dayLanguage = 0;

static bool read_Settings(Cth_Settings_t& s){
    cth_Settings_Default(s);
    Cth_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(CTH_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == CTH_SETTINGS_MAGIC;
    if (ok) s = saved;
    cth_Settings_Clamp(s);
    return ok;
}

// On the worker: the day from the General Roman Calendar (calapi.inadiutorium.cz).
static bool read_Liturgical_Day(uint32_t day, int y, int m, int d){
    char path[96], url[160];
    if (!cth_Calendar_Path(dayLanguage, y, m, d, path, sizeof(path))) return false;
    snprintf(url, sizeof(url), "http://calapi.inadiutorium.cz%s", path);
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 8 * 1024, r, "PIXLPAL-L2/1.0 (Pixlpal Catholic Daily)");
    JsonDocument doc;
    Cth_Day_t x;
    const bool ok = body && r.code == 200 && !deserializeJson(doc, body, r.length) && cth_Parse_Day(doc, day, x);
    heap_caps_free(body);
    char e[80] = "";
    if (!ok){
        if (r.code == 200) fth_Copy(e, sizeof(e), "The liturgical calendar's answer was not understood");
        else fth_Http_Error(e, sizeof(e), "The liturgical calendar", r.code);
    }
    portENTER_CRITICAL(&dayLock);
    if (ok) liturgicalDay = x;
    memcpy(dayError, e, sizeof(e));
    portEXIT_CRITICAL(&dayLock);
    fth_Worker_Changed();
    return ok;
}

static void worker_Config(const Cth_Settings_t& s, Fth_Worker_Config_t& c){
    memset(&c, 0, sizeof(c));
    c.verse = s.verse;
    c.reference = fth_Catholic_Reference;
    fth_Copy(c.translation, sizeof(c.translation), cth_Translation(s.translation)->id);
    c.edition = s.translation;
    fth_Copy(c.nvsKey, sizeof(c.nvsKey), CTH_VERSE_KEY);
    c.daily = read_Liturgical_Day;
}

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Cth_Settings_t* begin_Change(void){
    Cth_Settings_t* next = (Cth_Settings_t*)heap_caps_malloc(sizeof(Cth_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void end_Change(Cth_Settings_t* next){
    cth_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static void set_Byte(JsonDocument& c, const char* key, long lo, long hi, uint8_t Cth_Settings_t::* field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cth_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)v;
    end_Change(next);
}

static void reminder_Byte(JsonDocument& c, const char* key, long hi, uint8_t Fth_Reminder_t::* field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], 0, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cth_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->reminders[next->reminderSlot].*field = (uint8_t)v;
    end_Change(next);
}

static void cmd_Ack(JsonDocument&){}
static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }
static void cmd_Verse(JsonDocument& c){ set_Byte(c, "verse", 0, 1, &Cth_Settings_t::verse); }
static void cmd_Language(JsonDocument& c){ set_Byte(c, "language", 0, 3, &Cth_Settings_t::language); }
static void cmd_Hour24(JsonDocument& c){ set_Byte(c, "hour24", 0, 1, &Cth_Settings_t::hour24); }
static void cmd_Sound(JsonDocument& c){ set_Byte(c, "sound", 0, CTH_SOUND_BELL, &Cth_Settings_t::sound); }
static void cmd_Angelus(JsonDocument& c){ set_Byte(c, "angelus", 0, 1, &Cth_Settings_t::angelus); }
static void cmd_Mercy(JsonDocument& c){ set_Byte(c, "mercy", 0, 1, &Cth_Settings_t::mercy); }
static void cmd_Rosary(JsonDocument& c){ set_Byte(c, "rosary", 0, 1, &Cth_Settings_t::rosary); }
static void cmd_Reminder_Slot(JsonDocument& c){ set_Byte(c, "reminderSlot", 0, CTH_REMINDERS - 1, &Cth_Settings_t::reminderSlot); }
static void cmd_Reminder_At(JsonDocument& c){
    const int m = mtb_Kit_Parse_Clock(c["reminderAt"] | "");
    if (m < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Cth_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->reminders[next->reminderSlot].minute = (uint16_t)m;
    end_Change(next);
}
static void cmd_Reminder_Label(JsonDocument& c){
    const char* l = c["reminderLabel"] | "";
    if (!l[0] || strlen(l) >= sizeof(Fth_Reminder_t::label)){ mtb_Ble_App_Cmd_Reject(); return; }
    Cth_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fth_Copy(next->reminders[next->reminderSlot].label, sizeof(Fth_Reminder_t::label), l);
    end_Change(next);
}
static void cmd_Reminder_On(JsonDocument& c){ reminder_Byte(c, "reminderOn", 1, &Fth_Reminder_t::on); }
static void cmd_Reminder_Days(JsonDocument& c){ reminder_Byte(c, "reminderDays", FTH_DAYS_COUNT - 1, &Fth_Reminder_t::days); }
static void cmd_Another(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } fth_Worker_Another(); }
static void cmd_Test(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } testRequested = true; }

static void register_Commands(Mtb_Applications* app){
    app->mtb_App_Set_Ble_Comm_Fns(cmd_Ack, cmd_Refresh, cmd_Verse, cmd_Language, cmd_Hour24, cmd_Sound, cmd_Angelus, cmd_Mercy,
                                  cmd_Rosary, cmd_Reminder_Slot, cmd_Reminder_At, cmd_Reminder_Label);
    static const bleCom_Parser_Fns_Ptr MORE[] = {cmd_Reminder_On, cmd_Reminder_Days, cmd_Another, cmd_Test};
    for (uint8_t i = 0; i < sizeof(MORE) / sizeof(MORE[0]); i++) app->mtb_App_Set_Ble_Comm_Fn(12 + i, MORE[i]);
}

// The knob ------------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// Devotions ------------------------------------------------------------------------------------------------------------------
struct Devotion_t { uint32_t at; char name[32]; uint8_t sound; };       // sound: Fth_Sound_t, 0 none

static int devotions_Of(const Cth_Settings_t& s, const char* season, uint32_t midnight, int weekday, Devotion_t* out, int max){
    int n = 0;
    const uint8_t bell = s.sound == CTH_SOUND_BELL ? FTH_SOUND_ANGELUS : s.sound == CTH_SOUND_CHIME ? FTH_SOUND_CHIME : 0;
    const uint8_t chime = s.sound == CTH_SOUND_SILENT ? 0 : FTH_SOUND_CHIME;
    if (s.angelus){
        static const uint16_t AT[] = {6 * 60, 12 * 60, 18 * 60};
        for (uint16_t m : AT){ if (n < max){ out[n].at = midnight + m * 60u; fth_Copy(out[n].name, sizeof(out[n].name), cth_Angelus_Name(season)); out[n++].sound = bell; } }
    }
    if (s.mercy && n < max){ out[n].at = midnight + 15 * 3600u; fth_Copy(out[n].name, sizeof(out[n].name), "The Hour of Mercy"); out[n++].sound = chime; }
    for (const Fth_Reminder_t& r : s.reminders){
        if (n >= max || !fth_Reminder_Today(r, weekday)) continue;
        out[n].at = midnight + r.minute * 60u;
        fth_Copy(out[n].name, sizeof(out[n].name), r.label);
        out[n++].sound = chime;
    }
    return n;
}

static uint32_t midnight_Of(uint32_t now){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    return now - (uint32_t)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
}

// The next devotion today or tomorrow.
static bool next_Devotion(const Cth_Settings_t& s, const char* season, uint32_t now, Cth_Next_t& out){
    Devotion_t list[8];
    bool found = false;
    for (int day = 0; day < 2 && !found; day++){
        const uint32_t midnight = midnight_Of(now) + day * 86400u;
        int weekday;
        fth_Local_Day(midnight + 3600, nullptr, nullptr, nullptr, &weekday);
        const int n = devotions_Of(s, season, midnight, weekday, list, 8);
        for (int i = 0; i < n; i++){
            if (list[i].at <= now || (found && list[i].at >= out.at)) continue;
            out.at = list[i].at;
            fth_Copy(out.name, sizeof(out.name), list[i].name);
            found = true;
        }
    }
    return found;
}

// The app -------------------------------------------------------------------------------------------------------------------
static int pages_Of(const Cth_Settings_t& s, uint8_t* pages){
    int n = 0;
    pages[n++] = CTH_PAGE_TODAY;
    if (s.rosary) pages[n++] = CTH_PAGE_ROSARY;
    if (s.verse) pages[n++] = CTH_PAGE_VERSE;
    return n;
}

static void catholic_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    register_Commands(THIS_APP);
    THIS_APP->mtb_App_Init();

    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(CTH_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    longPressRequested = false;
    testRequested = false;
    clicksRequested = 0;
    turns = 0;
    dayLanguage = settings.language;
    portENTER_CRITICAL(&dayLock);
    memset(&liturgicalDay, 0, sizeof(liturgicalDay));
    dayError[0] = '\0';
    portEXIT_CRITICAL(&dayLock);
    Fth_Verse_t* verse = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    Cth_Day_t* day = (Cth_Day_t*)heap_caps_calloc(1, sizeof(Cth_Day_t), MALLOC_CAP_SPIRAM);
    Fth_Worker_Config_t config;
    worker_Config(settings, config);
    if (!verse || !day || !fth_Worker_Start(config)){
        heap_caps_free(verse);
        heap_caps_free(day);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    appOpen = true;
    char verseError[80] = "", alert[40] = "", alertDetail[48] = "";
    uint32_t version = 0, lastSecond = 0, phoneMs = 0, alertUntil = 0, firedUpTo = 0, knobMs = millis(), scrollMs = 0;
    uint8_t pages[3];
    int pageCount = pages_Of(settings, pages), pageIndex = 0, scroll = 0;
    bool redraw = true, phoneDirty = true, flash = false, hasNext = false;
    Cth_Next_t next = {};

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            mtb_Write_Nvs_Struct(CTH_NVS_KEY, &settings, sizeof(settings));
            worker_Config(settings, config);
            fth_Worker_Configure(config);
            if (dayLanguage != settings.language){ dayLanguage = settings.language; fth_Worker_Refresh(); }
            const uint8_t was = pages[pageIndex];
            pageCount = pages_Of(settings, pages);
            pageIndex = 0;
            for (int i = 0; i < pageCount; i++) if (pages[i] == was) pageIndex = i;
            redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; fth_Worker_Refresh(); }
        if (fth_Worker_Copy(verse, verseError, sizeof(verseError), version)){ redraw = phoneDirty = true; }

        // Today: the calendar's day, else the season worked out here.
        if (now > 1700000000u){
            int y, m, d;
            const uint32_t today = fth_Local_Day(now, &y, &m, &d, nullptr);
            portENTER_CRITICAL(&dayLock);
            const bool have = liturgicalDay.day == today;
            if (have) *day = liturgicalDay;
            portEXIT_CRITICAL(&dayLock);
            if (!have && day->day != today){ cth_Fallback_Day(y, m, d, today, *day); redraw = true; }
        }

        // The knob.
        const uint8_t page = pages[pageIndex];
        const int t = turns;
        if (t){
            turns = 0;
            knobMs = ms;
            if (page == CTH_PAGE_VERSE){ scroll += t; if (scroll < 0) scroll = 0; scrollMs = ms; redraw = true; }
            else mtb_Brightness_Control(t > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE);
        }
        const uint8_t clicks = clicksRequested;
        if (clicks){ clicksRequested = 0; knobMs = ms; }
        if (clicks == 1){
            if (alertUntil || fth_Sound_Busy()){ alertUntil = 0; fth_Sound_Stop(); }
            else { pageIndex = (pageIndex + 1) % pageCount; scroll = 0; }
            redraw = true;
        } else if (clicks == 2) fth_Worker_Refresh();
        if (longPressRequested){
            longPressRequested = false;
            knobMs = ms;
            if (fth_Sound_Busy()) fth_Sound_Stop();
            else if (page == CTH_PAGE_VERSE){ fth_Worker_Another(); scroll = 0; }
            else fth_Worker_Refresh();
        }
        if (testRequested){
            testRequested = false;
            fth_Copy(alert, sizeof(alert), cth_Angelus_Name(day->season));
            fth_Copy(alertDetail, sizeof(alertDetail), "Testing the bell");
            alertUntil = ms + 30000;
            fth_Sound_Stop();
            fth_Sound_Play(settings.sound == CTH_SOUND_CHIME ? FTH_SOUND_CHIME : FTH_SOUND_ANGELUS);
            redraw = true;
        }

        // Each second: devotions, the countdown, the flashing border.
        bool flashOnly = false;
        if (now != lastSecond && now > 1700000000u){
            lastSecond = now;
            if (!firedUpTo) firedUpTo = now;                   // nothing from before the app opened
            const uint32_t midnight = midnight_Of(now);
            int weekday;
            fth_Local_Day(now, nullptr, nullptr, nullptr, &weekday);
            Devotion_t list[8];
            const int n = devotions_Of(settings, day->season, midnight, weekday, list, 8);
            for (int i = 0; i < n; i++){
                if (list[i].at <= firedUpTo || list[i].at > now || now - list[i].at > 120) continue;
                fth_Copy(alert, sizeof(alert), list[i].name);
                fth_Clock(alertDetail, sizeof(alertDetail), list[i].at, settings.hour24);
                alertUntil = ms + 90000;
                pageIndex = 0;
                if (list[i].sound && !mtb_Silent_Mode()){ fth_Sound_Stop(); fth_Sound_Play(list[i].sound); }
                redraw = true;
            }
            firedUpTo = now;
            hasNext = next_Devotion(settings, day->season, now, next);
            if (pages[pageIndex] != CTH_PAGE_VERSE) redraw = true;   // the countdown
            if (alertUntil){ flash = !flash; flashOnly = true; }
        }
        if (alertUntil && (int32_t)(ms - alertUntil) >= 0 && !fth_Sound_Busy()){ alertUntil = 0; redraw = true; }
        if (page == CTH_PAGE_VERSE && !alertUntil && ms - knobMs > 8000 && ms - scrollMs > 3000){
            const int lines = fth_Verse_Lines(*verse);
            if (lines > FTH_VERSE_FIT){ scroll = (scroll + 1) % (lines - FTH_VERSE_FIT + 1 + 2); redraw = true; }
            scrollMs = ms;
        }
        Cth_View_t v = {&settings, day, verse, verseError, hasNext ? &next : nullptr, now, pages[pageIndex], scroll,
                        alertUntil ? alert : nullptr, alertDetail, flash};
        if (redraw){ redraw = false; cth_Draw(v); }
        else if (flashOnly) cth_Draw_Alert(v);
        if (phoneDirty && ms - phoneMs > 3000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    fth_Sound_Stop();
    appOpen = false;
    fth_Worker_Stop();
    heap_caps_free(verse);
    heap_caps_free(day);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static void cth_Values(JsonObject values){
    Cth_Settings_t* s = (Cth_Settings_t*)heap_caps_malloc(sizeof(Cth_Settings_t), MALLOC_CAP_SPIRAM);
    Fth_Verse_t* verse = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    Cth_Day_t* day = (Cth_Day_t*)heap_caps_calloc(1, sizeof(Cth_Day_t), MALLOC_CAP_SPIRAM);
    if (!s || !verse || !day){ heap_caps_free(s); heap_caps_free(verse); heap_caps_free(day); return; }
    char err[80] = "", derr[80] = "";
    const uint32_t now = (uint32_t)time(nullptr);
    int y = 0, m = 0, d = 0, weekday = 0;
    const uint32_t today = now > 1700000000u ? fth_Local_Day(now, &y, &m, &d, &weekday) : 0;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *s = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
        uint32_t version = 0xFFFFFFFFu;
        fth_Worker_Copy(verse, err, sizeof(err), version);
        portENTER_CRITICAL(&dayLock);
        *day = liturgicalDay;
        memcpy(derr, dayError, sizeof(derr));
        portEXIT_CRITICAL(&dayLock);
    } else {
        read_Settings(*s);
        mtb_Read_Nvs_Struct(CTH_VERSE_KEY, verse, sizeof(Fth_Verse_t));
        verse->text[sizeof(verse->text) - 1] = '\0';
        verse->reference[sizeof(verse->reference) - 1] = '\0';
    }
    if (today && day->day != today) cth_Fallback_Day(y, m, d, today, *day);
    char t[200], clock[16];
    values["verse"] = s->verse;
    values["language"] = s->language;
    values["hour24"] = s->hour24;
    values["sound"] = s->sound;
    values["angelus"] = s->angelus;
    values["mercy"] = s->mercy;
    values["rosary"] = s->rosary;
    const Fth_Reminder_t& r = s->reminders[s->reminderSlot];
    values["reminderSlot"] = s->reminderSlot;
    fth_Minute_Clock(clock, sizeof(clock), r.minute, true);
    values["reminderAt"] = clock;
    values["reminderLabel"] = r.label;
    values["reminderOn"] = r.on;
    values["reminderDays"] = r.days;
    if (today){
        JsonArray rows = values["today"].to<JsonArray>();
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Today";
        o["value"] = day->c[0].title;
        o["state"] = "ok";
        char rank[32], season[48];
        cth_Rank_Text(day->c[0], rank, sizeof(rank));
        cth_Season_Text(*day, season, sizeof(season));
        o = rows.add<JsonObject>();
        o["label"] = "Season";
        snprintf(t, sizeof(t), "%s%s%s, %s", season, rank[0] ? ", " : "", rank, day->c[0].colour);
        o["value"] = t;
        o["state"] = "info";
        for (int i = 1; i < day->count; i++){
            o = rows.add<JsonObject>();
            o["label"] = "Also";
            o["value"] = day->c[i].title;
            o["state"] = "info";
        }
        o = rows.add<JsonObject>();
        o["label"] = "Rosary";
        const uint8_t set = cth_Rosary(weekday, day->season);
        snprintf(t, sizeof(t), "%s: %s, %s, %s, %s, %s", cth_Rosary_Name(set), cth_Mystery(set, 0), cth_Mystery(set, 1), cth_Mystery(set, 2),
                 cth_Mystery(set, 3), cth_Mystery(set, 4));
        o["value"] = t;
        o["state"] = "info";
        if (!day->fromCalendar){
            o = rows.add<JsonObject>();
            o["label"] = "Calendar";
            o["value"] = derr[0] ? derr : "The season only (open Catholic Daily on Pixlpal for the day's celebration)";
            o["state"] = "warn";
        }
    }
    JsonArray vrows = values["verses"].to<JsonArray>();
    if (s->verse && verse->text[0]){
        JsonObject o = vrows.add<JsonObject>();
        o["label"] = verse->reference;
        snprintf(t, sizeof(t), "%.190s", verse->text);
        o["value"] = t;
        o["state"] = "info";
    }
    if (err[0]){ JsonObject o = vrows.add<JsonObject>(); o["label"] = "Verse"; o["value"] = err; o["state"] = "warn"; }
    JsonArray rrows = values["reminders"].to<JsonArray>();
    for (int i = 0; i < CTH_REMINDERS; i++){
        const Fth_Reminder_t& x = s->reminders[i];
        JsonObject o = rrows.add<JsonObject>();
        snprintf(t, sizeof(t), "%d. %s", i + 1, x.label);
        o["label"] = t;
        fth_Minute_Clock(clock, sizeof(clock), x.minute, s->hour24);
        snprintf(t, sizeof(t), "%s, %s%s", clock, fth_Days_Name(x.days), x.on ? "" : " (off)");
        o["value"] = t;
        o["state"] = x.on ? "ok" : "off";
    }
    heap_caps_free(s);
    heap_caps_free(verse);
    heap_caps_free(day);
}
