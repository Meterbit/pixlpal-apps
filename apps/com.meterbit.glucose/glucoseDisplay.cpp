/**
 * Glucose Display, ID {14,2}: a continuous glucose monitor's reading, its trend and the last hours, large enough to read
 * across the room, from Nightscout, Dexcom Share or LibreLinkUp. Alarms for lows (urgent lows cannot be switched off),
 * highs, fast changes and missing readings sound on Pixlpal and go to the family's phones (ntfy, Telegram, WhatsApp).
 *
 * NOT A MEDICAL DEVICE: a second screen for the CGM app, whose own alarms stay on.
 *
 * The knob: click, snooze the alarm sounding (or, with none, change the graph's hours); turn, the screens (the reading,
 * a longer graph, the last 24 hours in numbers); two clicks, ask the source again now.
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ntf_Notify.h"
#include "snd_Sound.h"
#include "glu_App.h"
#include "glu_Mobile_Ui.h"
#include <math.h>
#include <time.h>
#include <string.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Glucose Display)"
#define SCREEN_BACK_S   60          /**< the graph and the numbers go back to the reading after a minute */

EXT_RAM_BSS_ATTR TaskHandle_t glucoseDisplay_Task_H = NULL;
static void glucoseDisplay_App_Task(void* dApplication);
static void glu_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen *glucoseDisplay_App;
Mtb_Applications* glucoseDisplay_App_GetInstance() {
    if (!glucoseDisplay_App){
        glucoseDisplay_App = new Mtb_Applications_FullScreen(glucoseDisplay_App_Task, &glucoseDisplay_Task_H, "Glucose Display", {14,2}, 8192);
        glucoseDisplay_App->mtb_App_Set_Mobile_UI(GLU_UI_MANIFEST, GLU_UI_API, nullptr, glu_Values);
    }
    return glucoseDisplay_App;
}
MTB_REGISTER_APP(glucoseDisplay_App, 14, 2)

EXT_RAM_BSS_ATTR static Glu_Settings_t settings;
EXT_RAM_BSS_ATTR static Glu_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, snoozeRequested = false, testRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;
// What the screen shows, for the phone.
static volatile uint8_t shownAlarm = GLU_ALARM_NONE;
static volatile uint32_t snoozedUntilShown = 0;

static bool read_Settings(Glu_Settings_t& s){
    glu_Settings_Default(s);
    Glu_Settings_t* saved = (Glu_Settings_t*)heap_caps_calloc(1, sizeof(Glu_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return false;
    mtb_Read_Nvs_Struct(GLU_NVS_KEY, saved, sizeof(*saved));
    const bool ok = saved->magic == GLU_SETTINGS_MAGIC;
    if (ok) s = *saved;
    memset(saved, 0, sizeof(*saved));
    heap_caps_free(saved);
    glu_Settings_Clamp(s);
    return ok;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Glu_Settings_t* begin_Change(void){
    Glu_Settings_t* next = (Glu_Settings_t*)heap_caps_malloc(sizeof(Glu_Settings_t), MALLOC_CAP_SPIRAM);
    if (!next) return nullptr;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *next = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*next);
    return next;
}

static void drop_Change(Glu_Settings_t* next){
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
    mtb_Ble_App_Cmd_Reject();
}

static void end_Change(Glu_Settings_t* next){
    glu_Settings_Clamp(*next);
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        received = *next;
        settingsReceived = true;
        portEXIT_CRITICAL(&requestLock);
    } else mtb_Write_Nvs_Struct(GLU_NVS_KEY, next, sizeof(*next));
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

/** Trimmed, no control characters, fits. */
static bool clean(const char* in, char* out, size_t size){
    while (*in == ' ') in++;
    size_t n = strlen(in);
    while (n && in[n - 1] == ' ') n--;
    if (n >= size) return false;
    for (size_t i = 0; i < n; i++) if ((unsigned char)in[i] < ' ') return false;
    memcpy(out, in, n);
    out[n] = '\0';
    return true;
}

// 0 {"source": 0 Nightscout, 1 Dexcom, 2 LibreLinkUp}
static void cmd_Source(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["source"], 0, GLU_SOURCES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->source = (uint8_t)v;
    end_Change(n);
}

// 1 {"nsUrl"} | {"dxUser"} | {"dxRegion"} | {"lluEmail"} | {"lluPatient"} | {"name"}
static void cmd_Account(JsonDocument& c){
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[100];
    long v;
    bool ok = false;
    if (!c["nsUrl"].isNull()){
        ok = clean(c["nsUrl"] | "", t, sizeof(n->nsUrl));
        // An address alone gets https://; only http(s) addresses with a host are taken.
        char url[100];
        if (ok && t[0] && strncmp(t, "http://", 7) && strncmp(t, "https://", 8)) snprintf(url, sizeof(url), "https://%s", t);
        else snprintf(url, sizeof(url), "%s", t);
        const char* host = !strncmp(url, "https://", 8) ? url + 8 : url + 7;
        ok = ok && (!url[0] || (strlen(url) < sizeof(n->nsUrl) && host[0] && host[0] != '/' && !strchr(url, ' ') && !strchr(url, '?')));
        if (ok) snprintf(n->nsUrl, sizeof(n->nsUrl), "%s", url);
    } else if (!c["dxUser"].isNull()){ ok = clean(c["dxUser"] | "", t, sizeof(n->dxUser)); if (ok) snprintf(n->dxUser, sizeof(n->dxUser), "%s", t); }
    else if (!c["lluEmail"].isNull()){ ok = clean(c["lluEmail"] | "", t, sizeof(n->lluEmail)) && (!t[0] || strchr(t, '@')); if (ok) snprintf(n->lluEmail, sizeof(n->lluEmail), "%s", t); }
    else if (!c["dxRegion"].isNull()){ ok = mtb_Kit_Json_Range(c["dxRegion"], 0, GLU_DX_JP, v); if (ok) n->dxRegion = (uint8_t)v; }
    else if (!c["lluPatient"].isNull()){ ok = mtb_Kit_Json_Range(c["lluPatient"], 0, GLU_PATIENTS - 1, v); if (ok) n->lluPatient = (uint8_t)v; }
    else if (!c["name"].isNull()){
        char ascii[sizeof(n->name)];
        ok = clean(c["name"] | "", t, sizeof(n->name));
        if (ok){ mtb_Kit_To_Ascii(ascii, sizeof(ascii), t); mtb_Kit_Upper(ascii); snprintf(n->name, sizeof(n->name), "%s", ascii); }
    }
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

// 2 {"nsToken"} | {"nsSecret"} | {"dxPass"} | {"lluPass"}: kept on Pixlpal, never sent back. The API secret is hashed.
static void cmd_Secret(JsonDocument& c){
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[80];
    bool ok = false;
    if (!c["nsToken"].isNull()){ ok = clean(c["nsToken"] | "", t, sizeof(n->nsToken)); if (ok) snprintf(n->nsToken, sizeof(n->nsToken), "%s", t); }
    else if (!c["nsSecret"].isNull()){
        ok = clean(c["nsSecret"] | "", t, sizeof(t));
        if (ok && t[0] && strlen(t) < 12) ok = false;                  // Nightscout asks for 12 characters at least
        if (ok){ if (t[0]) glu_Sha1_Hex(t, n->nsSecretSha1); else n->nsSecretSha1[0] = '\0'; }
    }
    else if (!c["dxPass"].isNull()){ ok = clean(c["dxPass"] | "", t, sizeof(n->dxPass)); if (ok) snprintf(n->dxPass, sizeof(n->dxPass), "%s", t); }
    else if (!c["lluPass"].isNull()){ ok = clean(c["lluPass"] | "", t, sizeof(n->lluPass)); if (ok) snprintf(n->lluPass, sizeof(n->lluPass), "%s", t); }
    memset(t, 0, sizeof(t));
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

// 3 {"units"} | {"useSiteTargets"} | {"graphHours"} | {"hour24"}
static void cmd_Display(JsonDocument& c){
    long v;
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["units"].isNull() && mtb_Kit_Json_Range(c["units"], 0, GLU_UNITS_AUTO, v)) n->units = (uint8_t)v;
    else if (!c["useSiteTargets"].isNull() && mtb_Kit_Json_Range(c["useSiteTargets"], 0, 1, v)) n->useSiteTargets = (uint8_t)v;
    else if (!c["graphHours"].isNull() && mtb_Kit_Json_Range(c["graphHours"], 3, 24, v) && (v == 3 || v == 6 || v == 12 || v == 24)) n->graphHours = (uint8_t)v;
    else if (!c["hour24"].isNull() && mtb_Kit_Json_Range(c["hour24"], 0, 1, v)) n->hour24 = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// 4 a line, as {"<name>Mg": 70} or {"<name>Mmol": 3.9}: urgentLow, low, high, urgentHigh, targetLow, targetHigh.
static void cmd_Range(JsonDocument& c){
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    struct Line_t { const char* name; uint16_t* field; };
    const Line_t lines[] = {{"urgentLow", &n->urgentLow}, {"low", &n->low}, {"high", &n->high}, {"urgentHigh", &n->urgentHigh},
                            {"targetLow", &n->targetLow}, {"targetHigh", &n->targetHigh}};
    for (const Line_t& l : lines){
        char mg[24], mmol[24];
        snprintf(mg, sizeof(mg), "%sMg", l.name);
        snprintf(mmol, sizeof(mmol), "%sMmol", l.name);
        float value;
        if (c[mg].is<float>()) value = c[mg].as<float>();
        else if (c[mmol].is<float>()) value = c[mmol].as<float>() * GLU_MMOL;
        else if (c[mg].is<const char*>()) value = (float)atof(c[mg] | "0");
        else continue;
        if (!(value >= 36 && value <= 450)){ drop_Change(n); return; }
        *l.field = (uint16_t)lroundf(value);
        end_Change(n);
        return;
    }
    drop_Change(n);
}

// 5 switches: alarms, sound, phone alerts and channels, the quiet hours' choices.
static void cmd_Switches(JsonDocument& c){
    long v;
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    struct Flag_t { const char* key; uint8_t* field; };
    const Flag_t flags[] = {
        {"soonLowOn", &n->alarmOn[GLU_URGENT_LOW_SOON]}, {"lowOn", &n->alarmOn[GLU_LOW]}, {"fallFastOn", &n->alarmOn[GLU_FALLING_FAST]},
        {"staleOn", &n->alarmOn[GLU_STALE]}, {"urgentHighOn", &n->alarmOn[GLU_URGENT_HIGH]}, {"highOn", &n->alarmOn[GLU_HIGH]},
        {"riseFastOn", &n->alarmOn[GLU_RISING_FAST]}, {"sound", &n->sound}, {"phoneAlerts", &n->phoneAlerts}, {"quietHighs", &n->quietHighs},
        {"nightDim", &n->nightDim}, {"ntfyOn", &n->notify.on[NTF_NTFY]}, {"telegramOn", &n->notify.on[NTF_TELEGRAM]},
        {"whatsappOn", &n->notify.on[NTF_WHATSAPP]}};
    for (const Flag_t& f : flags){
        if (c[f.key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[f.key], 0, 1, v)){ drop_Change(n); return; }
        *f.field = (uint8_t)v;
        end_Change(n);
        return;
    }
    drop_Change(n);
}

// 6 {"staleMinutes"} | {"lowSnooze"} | {"highSnooze"}
static void cmd_Numbers(JsonDocument& c){
    long v;
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["staleMinutes"].isNull() && mtb_Kit_Json_Range(c["staleMinutes"], 10, 120, v)) n->staleMinutes = (uint8_t)v;
    else if (!c["lowSnooze"].isNull() && mtb_Kit_Json_Range(c["lowSnooze"], 5, 60, v)) n->lowSnooze = (uint8_t)v;
    else if (!c["highSnooze"].isNull() && mtb_Kit_Json_Range(c["highSnooze"], 15, 240, v)) n->highSnooze = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// 7 {"from", "to"}: the quiet hours.
static void cmd_Quiet(JsonDocument& c){
    const int from = mtb_Kit_Parse_Clock(c["from"] | ""), to = mtb_Kit_Parse_Clock(c["to"] | "");
    if (from < 0 || to < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->quietFrom = (uint16_t)from;
    n->quietTo = (uint16_t)to;
    end_Change(n);
}

// 8 {} snooze the alarm sounding   9 {} ask the source again now (and try a refused login again)   12 {} a test alert
static void cmd_Snooze(JsonDocument&){ if (!appOpen || shownAlarm == GLU_ALARM_NONE){ mtb_Ble_App_Cmd_Reject(); return; } snoozeRequested = true; }
static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }
static void cmd_Test(JsonDocument&){
    bool ready = false;
    portENTER_CRITICAL(&requestLock);
    const Ntf_Config_t k = (settingsReceived ? received : settings).notify;
    portEXIT_CRITICAL(&requestLock);
    for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++) if (ntf_Ready(k, ch)) ready = true;
    if (!appOpen || !ready){ mtb_Ble_App_Cmd_Reject(); return; }
    testRequested = true;
}

// 10 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
static void cmd_Notify_Text(JsonDocument& c){
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[80];
    bool ok = false;
    Ntf_Config_t& k = n->notify;
    if (!c["ntfyServer"].isNull()){
        ok = clean(c["ntfyServer"] | "", t, sizeof(t));
        const char* h = !strncmp(t, "https://", 8) ? t + 8 : t;
        char host[80];
        snprintf(host, sizeof(host), "%s", h[0] ? h : "ntfy.sh");
        char* slash = strchr(host, '/');
        if (slash) *slash = '\0';
        ok = ok && strlen(host) < sizeof(k.ntfyServer) && ntf_Valid_Server(host);
        if (ok) snprintf(k.ntfyServer, sizeof(k.ntfyServer), "%s", host);
    }
    else if (!c["ntfyTopic"].isNull()){ ok = clean(c["ntfyTopic"] | "", t, sizeof(k.ntfyTopic)) && t[0] && ntf_Valid_Topic(t); if (ok) snprintf(k.ntfyTopic, sizeof(k.ntfyTopic), "%s", t); }
    else if (!c["telegramChat"].isNull()){ ok = clean(c["telegramChat"] | "", t, sizeof(k.telegramChat)) && ntf_Valid_Chat(t); if (ok) snprintf(k.telegramChat, sizeof(k.telegramChat), "%s", t); }
    else if (!c["whatsappPhone"].isNull()){ ok = clean(c["whatsappPhone"] | "", t, sizeof(k.whatsappPhone)) && ntf_Valid_Phone(t); if (ok) snprintf(k.whatsappPhone, sizeof(k.whatsappPhone), "%s", t); }
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

// 11 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}: kept on Pixlpal, never sent back.
static void cmd_Notify_Secret(JsonDocument& c){
    Glu_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[80];
    bool ok = false;
    Ntf_Config_t& k = n->notify;
    if (!c["ntfyToken"].isNull()){ ok = clean(c["ntfyToken"] | "", t, sizeof(k.ntfyToken)); if (ok) snprintf(k.ntfyToken, sizeof(k.ntfyToken), "%s", t); }
    else if (!c["telegramToken"].isNull()){ ok = clean(c["telegramToken"] | "", t, sizeof(k.telegramToken)) && (!t[0] || strchr(t, ':')); if (ok) snprintf(k.telegramToken, sizeof(k.telegramToken), "%s", t); }
    else if (!c["whatsappKey"].isNull()){ ok = clean(c["whatsappKey"] | "", t, sizeof(k.whatsappKey)); if (ok) snprintf(k.whatsappKey, sizeof(k.whatsappKey), "%s", t); }
    memset(t, 0, sizeof(t));
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
    else if (b.type == BUTTON_PRESSED_LONG) clicksRequested = 3;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// Sounds ---------------------------------------------------------------------------------------------------------------
static const Snd_Note_t URGENT_SOUND[] = {{1760, 120}, {0, 60}, {1760, 120}, {0, 60}, {1760, 120}, {0, 60}, {1760, 120}};
static const Snd_Note_t LOW_SOUND[] = {{1175, 220}, {0, 60}, {880, 220}, {0, 60}, {659, 320}};
static const Snd_Note_t HIGH_SOUND[] = {{659, 220}, {0, 60}, {880, 220}, {0, 60}, {1175, 320}};
static const Snd_Note_t STALE_SOUND[] = {{440, 300}, {0, 120}, {440, 300}};

static void play_Alarm(uint8_t a){
    switch (a){
        case GLU_URGENT_LOW: case GLU_URGENT_LOW_SOON: snd_Play(URGENT_SOUND, (int)(sizeof(URGENT_SOUND) / sizeof(URGENT_SOUND[0])), 0.9f, true); break;
        case GLU_LOW: case GLU_FALLING_FAST: snd_Play(LOW_SOUND, (int)(sizeof(LOW_SOUND) / sizeof(LOW_SOUND[0])), 0.7f, false); break;
        case GLU_STALE: snd_Play(STALE_SOUND, (int)(sizeof(STALE_SOUND) / sizeof(STALE_SOUND[0])), 0.5f, false); break;
        default: snd_Play(HIGH_SOUND, (int)(sizeof(HIGH_SOUND) / sizeof(HIGH_SOUND[0])), 0.6f, false); break;
    }
}

/** Seconds between sounds while an alarm is not snoozed. */
static uint32_t sound_Every(uint8_t a){
    if (a == GLU_URGENT_LOW || a == GLU_URGENT_LOW_SOON) return 8;
    if (a == GLU_LOW || a == GLU_FALLING_FAST) return 60;
    return 300;
}

// The app -------------------------------------------------------------------------------------------------------------
static void local_Now(uint32_t now, int32_t& offset, int& minute){
    const time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    const int64_t local = (int64_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400
                        + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    offset = (int32_t)(local - (int64_t)now);
    minute = lt.tm_hour * 60 + lt.tm_min;
}

/** "Ada: URGENT LOW 52 mg/dL, falling quickly" for the phones. */
static void alarm_Message(const Glu_Settings_t& s, const Glu_Data_t& d, uint8_t alarm, uint32_t now, char* title, size_t tn, char* text, size_t xn){
    const Glu_Reading_t* r = glu_Latest(d.hist);
    const uint8_t units = glu_Units(s, &d.site);
    snprintf(title, tn, "%s%s%s", s.name[0] ? s.name : "", s.name[0] ? ": " : "", glu_Alarm_Name(alarm));
    if (!r){ snprintf(text, xn, "No readings."); return; }
    char v[12];
    glu_Value_Text(v, sizeof(v), r->mgdl, units);
    const uint8_t trend = r->trend != GLU_TREND_NONE ? r->trend : glu_Trend_From_History(d.hist);
    const uint32_t age = now > r->t ? (now - r->t) / 60 : 0;
    if (alarm == GLU_STALE) snprintf(text, xn, "No new reading for %lu minutes. The last was %s %s.", (unsigned long)age, v, glu_Units_Name(units));
    else snprintf(text, xn, "%s %s%s%s (%lu min ago). Check the CGM app.", v, glu_Units_Name(units), trend != GLU_TREND_NONE ? ", " : "",
                  glu_Trend_Name(trend), (unsigned long)age);
}

static void glucoseDisplay_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Source, cmd_Account, cmd_Secret, cmd_Display, cmd_Range, cmd_Switches, cmd_Numbers, cmd_Quiet,
                                       cmd_Snooze, cmd_Refresh, cmd_Notify_Text, cmd_Notify_Secret);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(12, cmd_Test);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    if (!settings.notify.ntfyTopic[0])
        snprintf(settings.notify.ntfyTopic, sizeof(settings.notify.ntfyTopic), "pixlpal-cgm-%08lx%04lx", (unsigned long)esp_random(),
                 (unsigned long)(esp_random() & 0xFFFF));
    mtb_Write_Nvs_Struct(GLU_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    snoozeRequested = false;
    testRequested = false;
    clicksRequested = 0;
    turns = 0;
    Glu_Data_t* d = (Glu_Data_t*)heap_caps_calloc(1, sizeof(Glu_Data_t), MALLOC_CAP_SPIRAM);
    Glu_Alarm_State_t* as = (Glu_Alarm_State_t*)heap_caps_calloc(1, sizeof(Glu_Alarm_State_t), MALLOC_CAP_SPIRAM);
    if (!d || !as || !glu_Net_Start(settings)){
        heap_caps_free(d);
        heap_caps_free(as);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    ntf_Start(settings.notify, UA, "drop_of_blood");
    appOpen = true;
    uint8_t screen = GLU_SCREEN_NOW, alarm = GLU_ALARM_NONE, graphHours = settings.graphHours;
    uint32_t version = 0, screenSince = 0, lastSecond = 0, soundAt = 0, phoneMs = 0, flashMs = 0;
    int lastMinute = -1;
    bool redraw = true, phoneDirty = true, settingsDirty = false, flash = true;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            glu_Net_Set_Settings(settings);
            ntf_Set_Config(settings.notify);
            graphHours = settings.graphHours;
            settingsDirty = redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; glu_Net_Refresh(); }
        if (glu_Net_Copy(*d, version)) redraw = phoneDirty = true;
        int32_t offset = 0;
        int minute = 0;
        local_Now(now, offset, minute);

        // The knob.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        const int t = turns;
        if (t) turns = 0;
        if (clicks == 1 || clicks == 3 || snoozeRequested){
            snoozeRequested = false;
            if (alarm != GLU_ALARM_NONE){ glu_Alarm_Snooze(settings, *as, now, 0); snd_Stop(); }
            else if (clicks == 1){
                graphHours = graphHours == 3 ? 6 : graphHours == 6 ? 12 : graphHours == 12 ? 24 : 3;
                screenSince = now;
            }
            redraw = phoneDirty = true;
        } else if (clicks == 2){ glu_Net_Refresh(); }
        if (t){
            screen = (uint8_t)((screen + (t > 0 ? 1 : GLU_SCREENS - 1)) % GLU_SCREENS);
            screenSince = now;
            redraw = true;
        }
        if (screen != GLU_SCREEN_NOW && now - screenSince > SCREEN_BACK_S){ screen = GLU_SCREEN_NOW; graphHours = settings.graphHours; redraw = true; }

        // The alarms, once a second.
        if (now != lastSecond && now > 1700000000u){
            lastSecond = now;
            const uint8_t was = alarm;
            alarm = glu_Alarm_Check(settings, &d->site, d->hist, now, minute, *as);
            for (uint8_t a = GLU_URGENT_LOW; a < GLU_ALARMS; a++) if (!as->latched[a]) as->sentAt[a] = 0;   // an episode over
            if (alarm != was){
                redraw = phoneDirty = true;
                soundAt = 0;
                if (alarm != GLU_ALARM_NONE) screen = GLU_SCREEN_NOW;
            }
            if (alarm != GLU_ALARM_NONE){
                if (settings.sound && (!soundAt || now - soundAt >= sound_Every(alarm))){ soundAt = now; play_Alarm(alarm); }
                if (settings.phoneAlerts && glu_Alarm_Send_Due(*as, alarm, now)){
                    char title[64], text[200];
                    alarm_Message(settings, *d, alarm, now, title, sizeof(title), text, sizeof(text));
                    if (ntf_Send(title, text, alarm == GLU_URGENT_LOW || alarm == GLU_URGENT_LOW_SOON ? NTF_URGENT : NTF_HIGH)) as->sentAt[alarm] = now;
                }
            }
            uint32_t snoozed = 0;
            if (as->active != GLU_ALARM_NONE && as->snoozedUntil[as->active] > now) snoozed = as->snoozedUntil[as->active];
            if (snoozed != snoozedUntilShown){ snoozedUntilShown = snoozed; redraw = phoneDirty = true; }
            shownAlarm = alarm;
        }
        if (alarm != GLU_ALARM_NONE && ms - flashMs >= 500){ flashMs = ms; flash = !flash; redraw = true; }
        if (minute != lastMinute){ lastMinute = minute; redraw = true; }
        if (testRequested){
            testRequested = false;
            ntf_Send("Glucose Display test", "A test from Pixlpal's Glucose Display. Alarms will arrive like this one.", NTF_NORMAL);
        }

        if (redraw){
            redraw = false;
            const bool dim = settings.nightDim && glu_In_Quiet(settings, minute);
            Glu_View_t v = {screen, &settings, d, now, offset, alarm, alarm != GLU_ALARM_NONE && flash, snoozedUntilShown > now, dim, graphHours, nullptr};
            if (now < 1700000000u) v.message = "Waiting for the clock...";
            else if (d->state == GLU_NET_NOT_SET && !d->hist.count) v.message = d->error[0] ? d->error : "Set up a source in the Pixlpal app";
            glu_Draw(v);
        }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(GLU_NVS_KEY, &settings, sizeof(settings)); }
        if (phoneDirty && ms - phoneMs > 1500){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    shownAlarm = GLU_ALARM_NONE;
    snd_Stop();
    glu_Net_Stop();
    ntf_Stop();
    if (settingsDirty) mtb_Write_Nvs_Struct(GLU_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(d);
    heap_caps_free(as);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void row(JsonArray rows, const char* label, const char* value, const char* state){
    JsonObject o = rows.add<JsonObject>();
    o["label"] = label;
    o["value"] = value;
    if (state) o["state"] = state;
}

static void glu_Values(JsonObject values){
    Glu_Settings_t* s = (Glu_Settings_t*)heap_caps_malloc(sizeof(Glu_Settings_t), MALLOC_CAP_SPIRAM);
    Glu_Data_t* d = (Glu_Data_t*)heap_caps_calloc(1, sizeof(Glu_Data_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    uint32_t version = 0xFFFFFFFFu;
    if (appOpen) glu_Net_Copy(*d, version);
    const uint8_t units = glu_Units(*s, &d->site);
    const bool mmol = units == GLU_MMOLL;
    char t[200], a[24], b[24];
    values["source"] = s->source;
    values["units"] = s->units;
    values["mmol"] = mmol ? 1 : 0;
    values["name"] = s->name;
    values["nsUrl"] = s->nsUrl;
    values["nsToken_set"] = s->nsToken[0] ? 1 : 0;
    values["nsSecret_set"] = s->nsSecretSha1[0] ? 1 : 0;
    values["useSiteTargets"] = s->useSiteTargets;
    values["dxUser"] = s->dxUser;
    values["dxPass_set"] = s->dxPass[0] ? 1 : 0;
    values["dxRegion"] = s->dxRegion;
    values["lluEmail"] = s->lluEmail;
    values["lluPass_set"] = s->lluPass[0] ? 1 : 0;
    values["lluPatient"] = s->lluPatient;
    values["graphHours"] = s->graphHours;
    values["hour24"] = s->hour24;
    struct Line_t { const char* name; uint16_t v; };
    const Line_t lines[] = {{"urgentLow", s->urgentLow}, {"low", s->low}, {"high", s->high}, {"urgentHigh", s->urgentHigh},
                            {"targetLow", s->targetLow}, {"targetHigh", s->targetHigh}};
    for (const Line_t& l : lines){
        char k[24];
        snprintf(k, sizeof(k), "%sMg", l.name); values[k] = l.v;
        snprintf(k, sizeof(k), "%sMmol", l.name); values[k] = roundf(l.v / GLU_MMOL * 10.0f) / 10.0f;
    }
    values["soonLowOn"] = s->alarmOn[GLU_URGENT_LOW_SOON];
    values["lowOn"] = s->alarmOn[GLU_LOW];
    values["fallFastOn"] = s->alarmOn[GLU_FALLING_FAST];
    values["staleOn"] = s->alarmOn[GLU_STALE];
    values["urgentHighOn"] = s->alarmOn[GLU_URGENT_HIGH];
    values["highOn"] = s->alarmOn[GLU_HIGH];
    values["riseFastOn"] = s->alarmOn[GLU_RISING_FAST];
    values["sound"] = s->sound;
    values["phoneAlerts"] = s->phoneAlerts;
    values["quietHighs"] = s->quietHighs;
    values["nightDim"] = s->nightDim;
    values["staleMinutes"] = s->staleMinutes;
    values["lowSnooze"] = s->lowSnooze;
    values["highSnooze"] = s->highSnooze;
    mtb_Kit_Clock_Text(a, sizeof(a), s->quietFrom);
    values["from"] = a;
    mtb_Kit_Clock_Text(a, sizeof(a), s->quietTo);
    values["to"] = a;
    const Ntf_Config_t& k = s->notify;
    values["ntfyOn"] = k.on[NTF_NTFY];
    values["telegramOn"] = k.on[NTF_TELEGRAM];
    values["whatsappOn"] = k.on[NTF_WHATSAPP];
    values["ntfyServer"] = k.ntfyServer;
    values["ntfyTopic"] = k.ntfyTopic;
    values["telegramChat"] = k.telegramChat;
    values["whatsappPhone"] = k.whatsappPhone;
    values["ntfyToken_set"] = k.ntfyToken[0] ? 1 : 0;
    values["telegramToken_set"] = k.telegramToken[0] ? 1 : 0;
    values["whatsappKey_set"] = k.whatsappKey[0] ? 1 : 0;
    const uint8_t alarm = shownAlarm;
    values["alarmOn"] = appOpen && alarm != GLU_ALARM_NONE ? 1 : 0;

    JsonArray rows = values["status"].to<JsonArray>();
    if (!appOpen){
        row(rows, "Now", "Open Glucose Display on Pixlpal to see the readings here.", "off");
    } else {
        const uint32_t now = (uint32_t)time(nullptr);
        const Glu_Reading_t* r = glu_Latest(d->hist);
        uint16_t uLow, low, high, uHigh;
        glu_Ranges(*s, &d->site, uLow, low, high, uHigh);
        if (alarm != GLU_ALARM_NONE) row(rows, "Alarm", glu_Alarm_Name(alarm), alarm <= GLU_FALLING_FAST ? "error" : "warn");
        else if (snoozedUntilShown > now){
            snprintf(t, sizeof(t), "Snoozed for %lu more minutes", (unsigned long)((snoozedUntilShown - now + 59) / 60));
            row(rows, "Alarm", t, "info");
        }
        if (r){
            glu_Value_Text(a, sizeof(a), r->mgdl, units);
            const uint8_t trend = r->trend != GLU_TREND_NONE ? r->trend : glu_Trend_From_History(d->hist);
            int delta;
            b[0] = '\0';
            if (glu_Delta(d->hist, delta)){ glu_Delta_Text(b, sizeof(b), delta, units); }
            const uint32_t age = now > r->t ? now - r->t : 0;
            snprintf(t, sizeof(t), "%s %s, %s%s%s, %lu min ago", a, glu_Units_Name(units), glu_Trend_Name(trend), b[0] ? " " : "", b,
                     (unsigned long)(age / 60));
            const uint8_t range = glu_Range(r->mgdl, uLow, low, high, uHigh);
            row(rows, "Reading", t, age > (uint32_t)s->staleMinutes * 60u ? "off"
                                   : range == GLU_RANGE_IN ? "ok" : range == GLU_RANGE_URGENT_LOW || range == GLU_RANGE_LOW ? "error" : "warn");
        }
        static const char* const SOURCES[GLU_SOURCES] = {"Nightscout", "Dexcom Share", "LibreLinkUp"};
        const char* state = d->state == GLU_NET_OK ? "ok" : d->state == GLU_NET_CONNECTING ? "info" : d->state == GLU_NET_FAILED ? "warn" : "error";
        if (d->state == GLU_NET_OK){
            snprintf(t, sizeof(t), "Connected, %d readings in the last day", d->hist.count);
            if (s->source == GLU_NIGHTSCOUT && d->site.known)
                snprintf(t + strlen(t), sizeof(t) - strlen(t), ". %s%s%s", d->site.name[0] ? d->site.name : "Site", d->site.version[0] ? " " : "", d->site.version);
            if (s->source == GLU_LIBRE && d->region[0]) snprintf(t + strlen(t), sizeof(t) - strlen(t), ", region %s", d->region);
        } else snprintf(t, sizeof(t), "%s", d->state == GLU_NET_CONNECTING ? "Connecting..." : d->error);
        row(rows, SOURCES[s->source], t, state);
        if (s->source == GLU_NIGHTSCOUT && d->state == GLU_NET_OK){
            if (d->extra.hasIob || d->extra.hasCob){
                if (!d->extra.hasCob) snprintf(t, sizeof(t), "%.2f U insulin on board", d->extra.iob);
                else if (!d->extra.hasIob) snprintf(t, sizeof(t), "%.0f g carbs on board", d->extra.cob);
                else snprintf(t, sizeof(t), "%.2f U insulin, %.0f g carbs on board", d->extra.iob, d->extra.cob);
                row(rows, "Loop / AAPS", t, "info");
            }
            if (d->extra.hasBattery){ snprintf(t, sizeof(t), "%u%%", d->extra.battery); row(rows, "Uploader battery", t, d->extra.battery < 20 ? "warn" : "ok"); }
            if (s->useSiteTargets && d->site.known && (d->site.targetLow || d->site.targetHigh)){
                glu_Value_Text(a, sizeof(a), low, units);
                glu_Value_Text(b, sizeof(b), high, units);
                snprintf(t, sizeof(t), "%s to %s %s, from your site", a, b, glu_Units_Name(units));
                row(rows, "Alarm lines", t, "info");
            }
        }
        if (s->source == GLU_LIBRE && d->people){
            t[0] = '\0';
            for (int i = 0; i < d->people && i < GLU_PATIENTS; i++)
                snprintf(t + strlen(t), sizeof(t) - strlen(t), "%s%d %s%s", i ? ", " : "", i + 1, d->person[i].name, i == s->lluPatient ? " (shown)" : "");
            row(rows, "People you follow", t, "info");
        }
        Glu_Stats_t st;
        glu_Stats(d->hist, now, 24, s->targetLow, s->targetHigh, uLow, st);
        if (st.readings >= 12){
            glu_Value_Text(a, sizeof(a), (uint16_t)lroundf(st.mean), units);
            snprintf(t, sizeof(t), "%.0f%% in range, %.0f%% below, %.0f%% above. Average %s, GMI %.1f%%", st.inRange, st.below, st.above, a, st.gmi);
            row(rows, "Last 24 hours", t, st.inRange >= 70 ? "ok" : "info");
        }
    }
    char topic[80];
    snprintf(topic, sizeof(topic), "%s%s%s", strcmp(k.ntfyServer, "ntfy.sh") ? k.ntfyServer : "", strcmp(k.ntfyServer, "ntfy.sh") ? "/" : "", k.ntfyTopic);
    JsonArray alerts = values["alertRows"].to<JsonArray>();
    if (s->phoneAlerts && k.on[NTF_NTFY] && k.ntfyTopic[0]) row(alerts, "Alarms go to ntfy topic", topic, "info");
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    heap_caps_free(d);
}
