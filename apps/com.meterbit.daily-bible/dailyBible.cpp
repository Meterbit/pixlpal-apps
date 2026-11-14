/**
 * Daily Bible app, ID {13,1} (FAITH, for Protestant, Evangelical, Pentecostal, Orthodox and other Christians): a
 * verse of the day (public domain translations of the 66 books), a Bible reading plan with a streak, the church year
 * (Advent, Christmas, Lent, Holy Week, Easter; by the Western or the Orthodox calendar), and prayer reminders
 * (morning devotion, evening prayer, Sunday service, Bible study).
 *
 * The knob: click, the other page (or dismiss a reminder); long press, another verse (on the verse page) or mark
 * today's reading read (on today's page); double click, read again; turn, scroll the verse (on its page) or brightness.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "fth_Sound.h"
#include "fth_Ui.h"
#include "fth_Worker.h"
#include "dbl_Model.h"
#include "dbl_Ui.h"
#include "dbl_Mobile_Ui.h"
#include <time.h>
#include <string.h>

EXT_RAM_BSS_ATTR TaskHandle_t dailyBible_Task_H = NULL;
static void dailyBible_App_Task(void* dApplication);
static void dbl_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *dailyBible_App;
Mtb_Applications* dailyBible_App_GetInstance() {
    if (!dailyBible_App){
        dailyBible_App = new Mtb_Applications_StatusBar(dailyBible_App_Task, &dailyBible_Task_H, "Daily Bible", {13,1}, 8192);
        dailyBible_App->mtb_App_Set_Mobile_UI(DBL_UI_MANIFEST, DBL_UI_API, nullptr, dbl_Values);
    }
    return dailyBible_App;
}
MTB_REGISTER_APP(dailyBible_App, 13, 1)

EXT_RAM_BSS_ATTR static Dbl_Settings_t settings;
EXT_RAM_BSS_ATTR static Dbl_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static bool read_Settings(Dbl_Settings_t& s){
    dbl_Settings_Default(s);
    Dbl_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(DBL_NVS_KEY, &saved, sizeof(saved));
    const bool ok = saved.magic == DBL_SETTINGS_MAGIC;
    if (ok) s = saved;
    dbl_Settings_Clamp(s);
    return ok;
}

static void worker_Config(const Dbl_Settings_t& s, Fth_Worker_Config_t& c){
    memset(&c, 0, sizeof(c));
    c.verse = s.verse;
    c.reference = fth_Bible_Reference;
    fth_Copy(c.translation, sizeof(c.translation), dbl_Translation(s.translation)->id);
    c.edition = s.translation;
    fth_Copy(c.nvsKey, sizeof(c.nvsKey), DBL_VERSE_KEY);
}

static uint32_t today_Day(void){ return fth_Local_Day((uint32_t)time(nullptr), nullptr, nullptr, nullptr, nullptr); }

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Dbl_Settings_t* begin_Change(void){
    Dbl_Settings_t* next = (Dbl_Settings_t*)heap_caps_malloc(sizeof(Dbl_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void end_Change(Dbl_Settings_t* next){
    dbl_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static void set_Byte(JsonDocument& c, const char* key, long lo, long hi, uint8_t Dbl_Settings_t::* field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->*field = (uint8_t)v;
    end_Change(next);
}

static void reminder_Byte(JsonDocument& c, const char* key, long hi, uint8_t Fth_Reminder_t::* field){
    long v;
    if (!mtb_Kit_Json_Range(c[key], 0, hi, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->reminders[next->reminderSlot].*field = (uint8_t)v;
    end_Change(next);
}

static void cmd_Ack(JsonDocument&){}
static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }
static void cmd_Translation(JsonDocument& c){ set_Byte(c, "translation", 0, dbl_Translations() - 1, &Dbl_Settings_t::translation); }
static void cmd_Verse(JsonDocument& c){ set_Byte(c, "verse", 0, 1, &Dbl_Settings_t::verse); }
static void cmd_Plan(JsonDocument& c){ set_Byte(c, "plan", 0, DBL_PLANS - 1, &Dbl_Settings_t::plan); }
static void cmd_Plan_Today(JsonDocument&){
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->planStart = today_Day();
    next->lastRead = 0;
    next->streak = 0;
    end_Change(next);
}
static void cmd_Orthodox(JsonDocument& c){ set_Byte(c, "orthodox", 0, 1, &Dbl_Settings_t::orthodox); }
static void cmd_Hour24(JsonDocument& c){ set_Byte(c, "hour24", 0, 1, &Dbl_Settings_t::hour24); }
static void cmd_Chime(JsonDocument& c){ set_Byte(c, "chime", 0, 1, &Dbl_Settings_t::chime); }
static void cmd_Reminder_Slot(JsonDocument& c){ set_Byte(c, "reminderSlot", 0, DBL_REMINDERS - 1, &Dbl_Settings_t::reminderSlot); }
static void cmd_Reminder_At(JsonDocument& c){
    const int m = mtb_Kit_Parse_Clock(c["reminderAt"] | "");
    if (m < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    next->reminders[next->reminderSlot].minute = (uint16_t)m;
    end_Change(next);
}
static void cmd_Reminder_Label(JsonDocument& c){
    const char* l = c["reminderLabel"] | "";
    if (!l[0] || strlen(l) >= sizeof(Fth_Reminder_t::label)){ mtb_Ble_App_Cmd_Reject(); return; }
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fth_Copy(next->reminders[next->reminderSlot].label, sizeof(Fth_Reminder_t::label), l);
    end_Change(next);
}
static void cmd_Reminder_On(JsonDocument& c){ reminder_Byte(c, "reminderOn", 1, &Fth_Reminder_t::on); }
static void cmd_Reminder_Days(JsonDocument& c){ reminder_Byte(c, "reminderDays", FTH_DAYS_COUNT - 1, &Fth_Reminder_t::days); }
static void cmd_Another(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } fth_Worker_Another(); }
static void cmd_Read(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["read"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Dbl_Settings_t* next = begin_Change();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    dbl_Mark_Read(*next, today_Day(), v != 0);
    end_Change(next);
}

static void register_Commands(Mtb_Applications* app){
    app->mtb_App_Set_Ble_Comm_Fns(cmd_Ack, cmd_Refresh, cmd_Translation, cmd_Verse, cmd_Plan, cmd_Plan_Today, cmd_Orthodox, cmd_Hour24,
                                  cmd_Chime, cmd_Reminder_Slot, cmd_Reminder_At, cmd_Reminder_Label);
    static const bleCom_Parser_Fns_Ptr MORE[] = {cmd_Reminder_On, cmd_Reminder_Days, cmd_Another, cmd_Read};
    for (uint8_t i = 0; i < sizeof(MORE) / sizeof(MORE[0]); i++) app->mtb_App_Set_Ble_Comm_Fn(12 + i, MORE[i]);
}

// The knob ------------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------------
static void dailyBible_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    register_Commands(THIS_APP);
    THIS_APP->mtb_App_Init();

    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(DBL_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    longPressRequested = false;
    clicksRequested = 0;
    turns = 0;
    Fth_Verse_t* verse = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    Fth_Worker_Config_t config;
    worker_Config(settings, config);
    if (!verse || !fth_Worker_Start(config)){
        heap_caps_free(verse);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    appOpen = true;
    char verseError[80] = "", alert[40] = "", alertDetail[48] = "";
    uint32_t version = 0, lastSecond = 0, lastMinute = 0, phoneMs = 0, alertUntil = 0, firedUpTo = 0, knobMs = millis(), scrollMs = 0;
    uint8_t page = settings.verse ? DBL_PAGE_VERSE : DBL_PAGE_TODAY;
    int scroll = 0;
    bool redraw = true, phoneDirty = true, flash = false;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            mtb_Write_Nvs_Struct(DBL_NVS_KEY, &settings, sizeof(settings));
            worker_Config(settings, config);
            fth_Worker_Configure(config);
            if (!settings.verse) page = DBL_PAGE_TODAY;
            redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; fth_Worker_Refresh(); }
        if (fth_Worker_Copy(verse, verseError, sizeof(verseError), version)){ redraw = phoneDirty = true; }

        // The knob.
        const int t = turns;
        if (t){
            turns = 0;
            knobMs = ms;
            if (page == DBL_PAGE_VERSE){ scroll += t; if (scroll < 0) scroll = 0; scrollMs = ms; redraw = true; }
            else mtb_Brightness_Control(t > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE);
        }
        const uint8_t clicks = clicksRequested;
        if (clicks){ clicksRequested = 0; knobMs = ms; }
        if (clicks == 1){
            if (alertUntil || fth_Sound_Busy()){ alertUntil = 0; fth_Sound_Stop(); }
            else if (settings.verse){ page = page == DBL_PAGE_VERSE ? DBL_PAGE_TODAY : DBL_PAGE_VERSE; scroll = 0; }
            redraw = true;
        } else if (clicks == 2) fth_Worker_Refresh();
        if (longPressRequested){
            longPressRequested = false;
            knobMs = ms;
            if (fth_Sound_Busy()) fth_Sound_Stop();
            else if (page == DBL_PAGE_VERSE){ fth_Worker_Another(); scroll = 0; }
            else if (settings.plan != DBL_PLAN_OFF){
                const uint32_t today = today_Day();
                dbl_Mark_Read(settings, today, !dbl_Read_Today(settings, today));
                mtb_Write_Nvs_Struct(DBL_NVS_KEY, &settings, sizeof(settings));
                if (dbl_Read_Today(settings, today)) fth_Sound_Play(FTH_SOUND_SOFT);
                redraw = phoneDirty = true;
            }
        }

        // Each second: reminders, the countdown, the flashing border.
        bool flashOnly = false;
        if (now != lastSecond && now > 1700000000u){
            lastSecond = now;
            if (!firedUpTo) firedUpTo = now;                   // nothing from before the app opened
            int weekday;
            fth_Local_Day(now, nullptr, nullptr, nullptr, &weekday);
            const time_t tt = (time_t)now;
            struct tm lt;
            localtime_r(&tt, &lt);
            const uint32_t midnight = now - (uint32_t)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
            for (const Fth_Reminder_t& r : settings.reminders){
                const uint32_t at = midnight + r.minute * 60u;
                if (!fth_Reminder_Today(r, weekday) || at <= firedUpTo || at > now || now - at > 120) continue;
                fth_Copy(alert, sizeof(alert), r.label);
                fth_Minute_Clock(alertDetail, sizeof(alertDetail), r.minute, settings.hour24);
                alertUntil = ms + 60000;
                if (settings.chime && !mtb_Silent_Mode()) fth_Sound_Play(FTH_SOUND_CHIME);
                redraw = true;
            }
            firedUpTo = now;
            if (page == DBL_PAGE_TODAY) redraw = true;          // the countdown
            if (now / 60 != lastMinute){ lastMinute = now / 60; phoneDirty = true; }
            if (alertUntil){ flash = !flash; flashOnly = true; }
        }
        if (alertUntil && (int32_t)(ms - alertUntil) >= 0){ alertUntil = 0; redraw = true; }
        if (page == DBL_PAGE_VERSE && !alertUntil && ms - knobMs > 8000 && ms - scrollMs > 3000){
            const int lines = fth_Verse_Lines(*verse);
            if (lines > FTH_VERSE_FIT){ scroll = (scroll + 1) % (lines - FTH_VERSE_FIT + 1 + 2); redraw = true; }
            scrollMs = ms;
        }
        Dbl_View_t v = {&settings, verse, verseError, now, page, scroll, alertUntil ? alert : nullptr, alertDetail, flash};
        if (redraw){ redraw = false; dbl_Draw(v); }
        else if (flashOnly) dbl_Draw_Alert(v);
        if (phoneDirty && ms - phoneMs > 3000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    fth_Sound_Stop();
    appOpen = false;
    fth_Worker_Stop();
    heap_caps_free(verse);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static void dbl_Values(JsonObject values){
    Dbl_Settings_t* s = (Dbl_Settings_t*)heap_caps_malloc(sizeof(Dbl_Settings_t), MALLOC_CAP_SPIRAM);
    Fth_Verse_t* verse = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    if (!s || !verse){ heap_caps_free(s); heap_caps_free(verse); return; }
    char err[80] = "";
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *s = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
        uint32_t version = 0xFFFFFFFFu;
        fth_Worker_Copy(verse, err, sizeof(err), version);
    } else {
        read_Settings(*s);
        mtb_Read_Nvs_Struct(DBL_VERSE_KEY, verse, sizeof(Fth_Verse_t));
        verse->text[sizeof(verse->text) - 1] = '\0';
        verse->reference[sizeof(verse->reference) - 1] = '\0';
    }
    char t[200], clock[16];
    values["translation"] = s->translation;
    values["verse"] = s->verse;
    values["plan"] = s->plan;
    values["orthodox"] = s->orthodox;
    values["hour24"] = s->hour24;
    values["chime"] = s->chime;
    const Fth_Reminder_t& r = s->reminders[s->reminderSlot];
    values["reminderSlot"] = s->reminderSlot;
    fth_Minute_Clock(clock, sizeof(clock), r.minute, true);
    values["reminderAt"] = clock;
    values["reminderLabel"] = r.label;
    values["reminderOn"] = r.on;
    values["reminderDays"] = r.days;

    const uint32_t now = (uint32_t)time(nullptr);
    if (now > 1700000000u){
        int y, m, d;
        const uint32_t today = fth_Local_Day(now, &y, &m, &d, nullptr);
        values["read"] = dbl_Read_Today(*s, today) ? 1 : 0;
        JsonArray rows = values["today"].to<JsonArray>();
        Fth_Church_Day_t c;
        fth_Church_Day(y, m, d, s->orthodox, c);
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Season";
        snprintf(t, sizeof(t), "%s%s%s (%s)", c.seasonName, c.feast[0] ? ": " : "", c.feast, c.colorName);
        o["value"] = t;
        o["state"] = "info";
        if (c.nextDays > 0){
            o = rows.add<JsonObject>();
            o["label"] = "Next";
            snprintf(t, sizeof(t), "%s in %d %s", c.next, c.nextDays, c.nextDays == 1 ? "day" : "days");
            o["value"] = t;
            o["state"] = "info";
        }
        if (s->plan != DBL_PLAN_OFF){
            char reading[128];
            dbl_Reading(*s, today, y, m, d, reading, sizeof(reading));
            o = rows.add<JsonObject>();
            o["label"] = "Today's reading";
            o["value"] = reading;
            o["state"] = dbl_Read_Today(*s, today) ? "ok" : "warn";
            o = rows.add<JsonObject>();
            o["label"] = "Streak";
            const uint16_t streak = dbl_Streak(*s, today);
            snprintf(t, sizeof(t), "%u %s in a row", streak, streak == 1 ? "day" : "days");
            o["value"] = t;
            o["state"] = streak ? "ok" : "off";
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
    for (int i = 0; i < DBL_REMINDERS; i++){
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
}
