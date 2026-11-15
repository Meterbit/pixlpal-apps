/**
 * Busy Light / On Air, ID {2,2}: a sign for the door of a home office or a studio. FREE, IN A MEETING, ON A CALL,
 * ON AIR, DO NOT DISTURB, AWAY... from your Google or Microsoft calendar, Microsoft Teams, working hours, or set
 * from the phone for a while. Someone at the door can press the knob to knock: you hear about it on your phone and
 * answer from there ("Come in", "One minute"...), and the answer shows on the sign.
 *
 * The knob: press, knock; turn or hold, the day's meetings; two clicks, read the calendar again.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_kit_oauth.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ntf_Notify.h"
#include "snd_Sound.h"
#include "gcl_Provider.h"
#include "olk_Provider.h"
#include "bsy_App.h"
#include "bsy_Mobile_Ui.h"
#include <math.h>
#include <time.h>
#include <string.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Busy Light)"
#define KNOCK_SHOWN_S   180         /**< the knock screen, if nobody answers */
#define REPLY_SHOWN_S   45
#define KNOCK_GAP_S     20          /**< one knock per 20 seconds */

EXT_RAM_BSS_ATTR TaskHandle_t busyLight_Task_H = NULL;
static void busyLight_App_Task(void* dApplication);
static void bsy_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen *busyLight_App;
Mtb_Applications* busyLight_App_GetInstance() {
    if (!busyLight_App){
        busyLight_App = new Mtb_Applications_FullScreen(busyLight_App_Task, &busyLight_Task_H, "Busy Light", {2,2}, 8192);
        busyLight_App->mtb_App_Set_Mobile_UI(BSY_UI_MANIFEST, BSY_UI_API, nullptr, bsy_Values);
    }
    return busyLight_App;
}
MTB_REGISTER_APP(busyLight_App, 2, 2)

/** The accounts' stored tokens: the same as Google Calendar's and Outlook Calendar's, so one link serves all three. */
static const char* token_Key(uint8_t p){ return (p == BSY_GOOGLE ? GCL_PROVIDER : OLK_PROVIDER).oauth->nvsKey; }

EXT_RAM_BSS_ATTR static Bsy_Settings_t settings;
EXT_RAM_BSS_ATTR static Bsy_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, syncRequested = false, testRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

// A knock and its answer (the BLE task writes the answer).
static volatile uint32_t knockedAt = 0;
static volatile bool knockPending = false, replyArrived = false;
static volatile uint8_t replyCode = 0;
EXT_RAM_BSS_ATTR static char replyText[BSY_TEXT + 8];
// What the sign says now, for the phone.
EXT_RAM_BSS_ATTR static Bsy_State_t shownState;
EXT_RAM_BSS_ATTR static char shownDetail[64];

static bool read_Settings(Bsy_Settings_t& s){
    bsy_Settings_Default(s);
    Bsy_Settings_t* saved = (Bsy_Settings_t*)heap_caps_calloc(1, sizeof(Bsy_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return false;
    mtb_Read_Nvs_Struct(BSY_NVS_KEY, saved, sizeof(*saved));
    const bool ok = saved->magic == BSY_SETTINGS_MAGIC;
    if (ok) s = *saved;
    memset(saved, 0, sizeof(*saved));
    heap_caps_free(saved);
    bsy_Settings_Clamp(s);
    return ok;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Bsy_Settings_t* begin_Change(void){
    Bsy_Settings_t* next = (Bsy_Settings_t*)heap_caps_malloc(sizeof(Bsy_Settings_t), MALLOC_CAP_SPIRAM);
    if (!next) return nullptr;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *next = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*next);
    return next;
}

static void drop_Change(Bsy_Settings_t* next){
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
    mtb_Ble_App_Cmd_Reject();
}

static void end_Change(Bsy_Settings_t* next){
    bsy_Settings_Clamp(*next);
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        received = *next;
        settingsReceived = true;
        portEXIT_CRITICAL(&requestLock);
    } else mtb_Write_Nvs_Struct(BSY_NVS_KEY, next, sizeof(*next));
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

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

// 0 / 2 {"refreshToken"}: link Google / Microsoft (shared with the calendar apps).   1 / 3: unlink.
static void link(JsonDocument& c, uint8_t p){
    const char* token = c["refreshToken"] | (const char*)nullptr;
    if (!token || !mtb_Kit_OAuth_Store(token_Key(p), token)){ mtb_Ble_App_Cmd_Reject(); return; }
    if (appOpen) syncRequested = true;
}
static void unlink(uint8_t p){
    mtb_Kit_OAuth_Forget(token_Key(p));
    Bsy_Settings_t* n = begin_Change();
    if (n){ n->account[p][0] = '\0'; end_Change(n); }
    if (appOpen) syncRequested = true;
}
static void cmd_Link_Google(JsonDocument& c){ link(c, BSY_GOOGLE); }
static void cmd_Unlink_Google(JsonDocument&){ unlink(BSY_GOOGLE); }
static void cmd_Link_Microsoft(JsonDocument& c){ link(c, BSY_MICROSOFT); }
static void cmd_Unlink_Microsoft(JsonDocument&){ unlink(BSY_MICROSOFT); }

// 4 {"sign", "minutes"?}: a status from the phone (0: back to automatic).
static void cmd_Status(JsonDocument& c){
    long v, m;
    if (!mtb_Kit_Json_Range(c["sign"], BSY_AUTO, BSY_STATUSES - 1, v) || v == BSY_OFF_WORK || v == BSY_OFFLINE){ mtb_Ble_App_Cmd_Reject(); return; }
    Bsy_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    const long minutes = !c["minutes"].isNull() && mtb_Kit_Json_Range(c["minutes"], 0, 720, m) ? m : n->manualMinutes;
    n->manual = (uint8_t)v;
    const uint32_t now = (uint32_t)time(nullptr);
    n->manualUntil = (v == BSY_AUTO || minutes == 0 || now < 1700000000u) ? 0 : now + (uint32_t)minutes * 60u;
    end_Change(n);
}

// 5 {"manualMinutes"}
static void cmd_Duration(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["manualMinutes"], 0, 240, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Bsy_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->manualMinutes = (uint8_t)v;
    end_Change(n);
}

// 6 {"customText"} and/or {"customColor": "0xAARRGGBB"}: your own words (shown with status 13).
static void cmd_Custom(JsonDocument& c){
    Bsy_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    bool any = false;
    if (!c["customText"].isNull()){
        char t[BSY_TEXT], ascii[BSY_TEXT];
        if (!clean(c["customText"] | "", t, sizeof(t)) || !t[0]){ drop_Change(n); return; }
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), t);
        mtb_Kit_Upper(ascii);
        snprintf(n->customText, sizeof(n->customText), "%s", ascii);
        any = true;
    }
    if (!c["customColor"].isNull()){
        uint16_t color;
        if (!mtb_Kit_Parse_Color(c["customColor"] | "", color)){ drop_Change(n); return; }
        n->customColor = color;
        any = true;
    }
    if (!any){ drop_Change(n); return; }
    end_Change(n);
}

// 7 {"reply": 0-3} | {"replyText"}: the answer to a knock.
static void cmd_Reply(JsonDocument& c){
    long v;
    if (!c["reply"].isNull() && mtb_Kit_Json_Range(c["reply"], 0, BSY_REPLIES - 1, v)) replyCode = (uint8_t)v;
    else if (!c["replyText"].isNull()){
        char t[BSY_TEXT], ascii[BSY_TEXT];
        if (!clean(c["replyText"] | "", t, sizeof(t)) || !t[0]){ mtb_Ble_App_Cmd_Reject(); return; }
        mtb_Kit_To_Ascii(ascii, sizeof(ascii), t);
        mtb_Kit_Upper(ascii);
        portENTER_CRITICAL(&requestLock);
        snprintf(replyText, sizeof(replyText), "%s", ascii);
        portEXIT_CRITICAL(&requestLock);
        replyCode = BSY_REPLIES;
    } else { mtb_Ble_App_Cmd_Reject(); return; }
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    replyArrived = true;
}

// 8 {"useGoogle"} | {"useMicrosoft"} | {"usePresence"} | {"allCalendars"} | {"tentativeBusy"}
// 9 {"showTitle"} | {"showNext"} | {"hour24"} | {"pulse"} | {"offHours"} | {"label"}
// 11 {"knock"} | {"knockSound"} | {"phoneKnock"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
static void cmd_Switches(JsonDocument& c){
    long v;
    Bsy_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    struct Flag_t { const char* key; uint8_t* field; };
    const Flag_t flags[] = {
        {"useGoogle", &n->useCalendar[BSY_GOOGLE]}, {"useMicrosoft", &n->useCalendar[BSY_MICROSOFT]}, {"usePresence", &n->usePresence},
        {"allCalendars", &n->allCalendars}, {"tentativeBusy", &n->tentativeBusy}, {"showTitle", &n->showTitle}, {"showNext", &n->showNext},
        {"hour24", &n->hour24}, {"pulse", &n->pulse}, {"knock", &n->knock}, {"knockSound", &n->knockSound}, {"phoneKnock", &n->phoneKnock},
        {"ntfyOn", &n->notify.on[NTF_NTFY]}, {"telegramOn", &n->notify.on[NTF_TELEGRAM]}, {"whatsappOn", &n->notify.on[NTF_WHATSAPP]}};
    for (const Flag_t& f : flags){
        if (c[f.key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[f.key], 0, 1, v)){ drop_Change(n); return; }
        *f.field = (uint8_t)v;
        end_Change(n);
        return;
    }
    if (!c["offHours"].isNull() && mtb_Kit_Json_Range(c["offHours"], 0, BSY_OFF_DIM, v)) n->offHours = (uint8_t)v;
    else if (!c["label"].isNull()){
        char t[BSY_TEXT];
        if (!clean(c["label"] | "", t, sizeof(t))){ drop_Change(n); return; }
        snprintf(n->label, sizeof(n->label), "%s", t);
    } else { drop_Change(n); return; }
    end_Change(n);
}

// 10 {"from", "to"} working hours | {"d0".."d6"} working days (Monday first)
static void cmd_Hours(JsonDocument& c){
    long v;
    Bsy_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["from"].isNull() || !c["to"].isNull()){
        const int from = mtb_Kit_Parse_Clock(c["from"] | ""), to = mtb_Kit_Parse_Clock(c["to"] | "");
        if (from < 0 || to < 0 || from == to){ drop_Change(n); return; }
        n->workFrom = (uint16_t)from;
        n->workTo = (uint16_t)to;
        end_Change(n);
        return;
    }
    char key[4] = "d0";
    for (int i = 0; i < 7; i++){
        key[1] = (char)('0' + i);
        if (c[key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[key], 0, 1, v)){ drop_Change(n); return; }
        if (v) n->workDays = (uint8_t)(n->workDays | (1u << i)); else n->workDays = (uint8_t)(n->workDays & ~(1u << i));
        end_Change(n);
        return;
    }
    drop_Change(n);
}

// 12 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
static void cmd_Notify_Text(JsonDocument& c){
    Bsy_Settings_t* n = begin_Change();
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

// 13 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}: kept on Pixlpal, never sent back.
static void cmd_Notify_Secret(JsonDocument& c){
    Bsy_Settings_t* n = begin_Change();
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

// 14 {} read again now   15 {} a test knock to the phones
static void cmd_Sync(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } syncRequested = true; }

static void cmd_Test(JsonDocument&){
    bool ready = false;
    portENTER_CRITICAL(&requestLock);
    const Ntf_Config_t k = (settingsReceived ? received : settings).notify;
    portEXIT_CRITICAL(&requestLock);
    for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++) if (ntf_Ready(k, ch)) ready = true;
    if (!appOpen || !ready){ mtb_Ble_App_Cmd_Reject(); return; }
    testRequested = true;
}

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static void local_Now(uint32_t now, int32_t& offset, int& weekday, int& minute){
    const time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    const int64_t local = (int64_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400
                        + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    offset = (int32_t)(local - (int64_t)now);
    weekday = (lt.tm_wday + 6) % 7;
    minute = lt.tm_hour * 60 + lt.tm_min;
}

static const Snd_Note_t KNOCK_SOUND[] = {{196, 90}, {0, 70}, {196, 90}, {0, 260}, {196, 90}, {0, 70}, {196, 90}};
static const Snd_Note_t REPLY_SOUND[] = {{784, 120}, {0, 30}, {1047, 220}};

static void busyLight_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Link_Google, cmd_Unlink_Google, cmd_Link_Microsoft, cmd_Unlink_Microsoft, cmd_Status,
                                       cmd_Duration, cmd_Custom, cmd_Reply, cmd_Switches, cmd_Switches, cmd_Hours, cmd_Switches);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(12, cmd_Notify_Text);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(13, cmd_Notify_Secret);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(14, cmd_Sync);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(15, cmd_Test);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    if (!settings.notify.ntfyTopic[0])
        snprintf(settings.notify.ntfyTopic, sizeof(settings.notify.ntfyTopic), "pixlpal-door-%08lx%04lx", (unsigned long)esp_random(),
                 (unsigned long)(esp_random() & 0xFFFF));
    mtb_Write_Nvs_Struct(BSY_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    syncRequested = false;
    testRequested = false;
    longPressRequested = false;
    replyArrived = false;
    knockPending = false;
    knockedAt = 0;
    clicksRequested = 0;
    turns = 0;
    Bsy_Data_t* d = (Bsy_Data_t*)heap_caps_calloc(1, sizeof(Bsy_Data_t), MALLOC_CAP_SPIRAM);
    if (!d || !bsy_Net_Start(settings)){
        heap_caps_free(d);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    ntf_Start(settings.notify, UA, "door");
    appOpen = true;
    uint8_t screen = BSY_SCREEN_SIGN, reply = 0;
    char ownReply[BSY_TEXT + 8] = "";
    uint32_t version = 0, screenSince = 0, lastMinute = 0, phoneMs = 0, glowMs = 0;
    uint8_t lastStatus = 0xFF;
    bool redraw = true, phoneDirty = true, settingsDirty = false;
    Bsy_State_t st = {};

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            bsy_Net_Set_Settings(settings);
            ntf_Set_Config(settings.notify);
            settingsDirty = redraw = phoneDirty = true;
        }
        if (syncRequested){ syncRequested = false; bsy_Net_Sync(); }
        if (bsy_Net_Copy(*d, version)){
            redraw = phoneDirty = true;
            for (int p = 0; p < BSY_PROVIDERS; p++){
                if (d->link[p].account[0] && strcmp(d->link[p].account, settings.account[p])){
                    snprintf(settings.account[p], sizeof(settings.account[p]), "%s", d->link[p].account);
                    settingsDirty = true;
                }
            }
        }
        // A phone status runs out.
        if (settings.manual != BSY_AUTO && settings.manualUntil && now >= settings.manualUntil && now > 1700000000u){
            settings.manual = BSY_AUTO;
            settings.manualUntil = 0;
            bsy_Net_Set_Settings(settings);
            settingsDirty = redraw = phoneDirty = true;
        }

        // What the sign says.
        int32_t offset = 0;
        int weekday = 0, minute = 0;
        local_Now(now, offset, weekday, minute);
        bsy_Decide(settings, now, weekday, minute, &d->cal, &d->presence, st);
        if (st.status != lastStatus){ lastStatus = st.status; redraw = phoneDirty = true; }
        {
            char detail[64];
            bsy_Detail(detail, sizeof(detail), st, settings, now, offset, weekday, minute);
            portENTER_CRITICAL(&requestLock);
            shownState = st;
            snprintf(shownDetail, sizeof(shownDetail), "%s", detail);
            portEXIT_CRITICAL(&requestLock);
        }

        // The knob: a knock, or the day.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        const int t = turns;
        if (t) turns = 0;
        if (clicks == 1){
            if (screen == BSY_SCREEN_TODAY){ screen = BSY_SCREEN_SIGN; redraw = true; }
            else if (settings.knock && (!knockedAt || now - knockedAt >= KNOCK_GAP_S)){
                knockedAt = now;
                knockPending = true;
                screen = BSY_SCREEN_KNOCKED;
                screenSince = now;
                redraw = phoneDirty = true;
                if (settings.knockSound && !mtb_Silent_Mode()) snd_Play(KNOCK_SOUND, (int)(sizeof(KNOCK_SOUND) / sizeof(KNOCK_SOUND[0])), 0.5f, false);
                if (settings.phoneKnock){
                    char title[64], text[160];
                    snprintf(title, sizeof(title), "Knock knock%s%s", settings.label[0] ? ": " : "", settings.label);
                    snprintf(text, sizeof(text), "Someone is at your door. The sign says: %s. Answer from the Pixlpal app.",
                             bsy_Status_Name(st.status));
                    ntf_Send(title, text, NTF_HIGH);
                }
            }
        } else if (clicks == 2) bsy_Net_Sync();
        if (t || longPressRequested){
            longPressRequested = false;
            screen = screen == BSY_SCREEN_TODAY ? BSY_SCREEN_SIGN : BSY_SCREEN_TODAY;
            screenSince = now;
            redraw = true;
        }
        if (replyArrived){
            replyArrived = false;
            reply = replyCode;
            portENTER_CRITICAL(&requestLock);
            snprintf(ownReply, sizeof(ownReply), "%s", replyText);
            portEXIT_CRITICAL(&requestLock);
            knockPending = false;
            screen = BSY_SCREEN_REPLY;
            screenSince = now;
            redraw = phoneDirty = true;
            snd_Play(REPLY_SOUND, (int)(sizeof(REPLY_SOUND) / sizeof(REPLY_SOUND[0])), 0.5f, false);
        }
        if (testRequested){
            testRequested = false;
            ntf_Send("Busy Light test", "A test from Pixlpal's Busy Light. Knocks will arrive like this one.", NTF_NORMAL);
        }
        // Screens time out back to the sign.
        if ((screen == BSY_SCREEN_KNOCKED && now - screenSince >= KNOCK_SHOWN_S) || (screen == BSY_SCREEN_REPLY && now - screenSince >= REPLY_SHOWN_S)
            || (screen == BSY_SCREEN_TODAY && now - screenSince >= 15)){
            screen = BSY_SCREEN_SIGN;
            redraw = true;
        }
        if (knockPending && now - knockedAt > 600){ knockPending = false; phoneDirty = true; }
        if (minute != (int)lastMinute){ lastMinute = (uint32_t)minute; redraw = true; }

        // ON AIR glows; the knock screen breathes too.
        const bool glows = (screen == BSY_SCREEN_SIGN && settings.pulse && (st.status == BSY_ON_AIR || st.status == BSY_PRESENTING))
                        || screen == BSY_SCREEN_KNOCKED;
        uint8_t glow = 255;
        if (glows){
            glow = (uint8_t)(150 + 105 * (0.5f + 0.5f * sinf((float)(ms % 2400) / 2400.0f * 6.2831853f)));
            if (ms - glowMs >= 80){ glowMs = ms; redraw = true; }
        }

        if (redraw){
            redraw = false;
            Bsy_View_t v = {screen, &settings, &st, &d->cal, now, offset, weekday, minute, glow, reply, ownReply, knockedAt, nullptr};
            if (now < 1700000000u) v.message = "Waiting for the clock...";
            bsy_Draw(v);
        }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(BSY_NVS_KEY, &settings, sizeof(settings)); }
        if (phoneDirty && ms - phoneMs > 1500){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    knockPending = false;
    bsy_Net_Stop();
    ntf_Stop();
    if (settingsDirty) mtb_Write_Nvs_Struct(BSY_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(d);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void row(JsonArray rows, const char* label, const char* value, const char* state){
    JsonObject o = rows.add<JsonObject>();
    o["label"] = label;
    o["value"] = value;
    if (state) o["state"] = state;
}

static void bsy_Values(JsonObject values){
    Bsy_Settings_t* s = (Bsy_Settings_t*)heap_caps_malloc(sizeof(Bsy_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    char t[160], a[24];
    const bool gLinked = mtb_Kit_OAuth_Is_Stored(token_Key(BSY_GOOGLE)), mLinked = mtb_Kit_OAuth_Is_Stored(token_Key(BSY_MICROSOFT));
    values["gLinked"] = gLinked ? 1 : 0;
    values["gAccount"] = gLinked ? s->account[BSY_GOOGLE] : "";
    values["mLinked"] = mLinked ? 1 : 0;
    values["mAccount"] = mLinked ? s->account[BSY_MICROSOFT] : "";
    values["useGoogle"] = s->useCalendar[BSY_GOOGLE];
    values["useMicrosoft"] = s->useCalendar[BSY_MICROSOFT];
    values["usePresence"] = s->usePresence;
    values["allCalendars"] = s->allCalendars;
    values["tentativeBusy"] = s->tentativeBusy;
    values["showTitle"] = s->showTitle;
    values["showNext"] = s->showNext;
    values["hour24"] = s->hour24;
    values["pulse"] = s->pulse;
    values["offHours"] = s->offHours;
    values["label"] = s->label;
    values["knock"] = s->knock;
    values["knockSound"] = s->knockSound;
    values["phoneKnock"] = s->phoneKnock;
    values["manualMinutes"] = s->manualMinutes;
    values["sign"] = s->manual;
    values["customText"] = s->customText;
    mtb_Kit_Clock_Text(a, sizeof(a), s->workFrom);
    values["from"] = a;
    mtb_Kit_Clock_Text(a, sizeof(a), s->workTo == 1440 ? 0 : s->workTo);
    values["to"] = a;
    char key[4] = "d0";
    for (int i = 0; i < 7; i++){ key[1] = (char)('0' + i); values[key] = (s->workDays >> i) & 1; }
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
    char topic[80];
    snprintf(topic, sizeof(topic), "%s%s%s", strcmp(k.ntfyServer, "ntfy.sh") ? k.ntfyServer : "", strcmp(k.ntfyServer, "ntfy.sh") ? "/" : "", k.ntfyTopic);
    const bool ntfy = s->phoneKnock && k.on[NTF_NTFY] && k.ntfyTopic[0];
    const bool usePresence = s->usePresence, useG = s->useCalendar[BSY_GOOGLE], useM = s->useCalendar[BSY_MICROSOFT];
    const uint8_t manual = s->manual;
    const uint32_t manualUntil = s->manualUntil;
    const uint16_t customColor = s->customColor;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);

    // The knock.
    values["knockPending"] = knockPending ? 1 : 0;
    values["knockAt"] = knockedAt;
    JsonArray knocks = values["knockRows"].to<JsonArray>();
    if (knockPending){
        JsonObject o = knocks.add<JsonObject>();
        o["label"] = "Someone knocked";
        o["value"] = "Answer below: the sign shows it";
        o["state"] = "warn";
        o["ago"] = "knockAt";
    }
    if (ntfy) row(knocks, "Knocks also go to ntfy topic", topic, "info");

    JsonArray rows = values["status"].to<JsonArray>();
    if (!appOpen){
        if (manual != BSY_AUTO) row(rows, "Set from the phone", bsy_Status_Name(manual), "info");
        row(rows, "Now", "Open Busy Light on Pixlpal to show the sign.", "off");
        return;
    }
    Bsy_State_t st;
    char detail[64];
    portENTER_CRITICAL(&requestLock);
    st = shownState;
    snprintf(detail, sizeof(detail), "%s", shownDetail);
    portEXIT_CRITICAL(&requestLock);
    const char* from = st.from == BSY_FROM_PHONE ? "set from the phone" : st.from == BSY_FROM_TEAMS ? "from Teams"
                     : st.from == BSY_FROM_CALENDAR ? "from the calendar" : "from working hours";
    snprintf(t, sizeof(t), "%s (%s)%s%s", st.status == BSY_CUSTOM ? "Your own text" : bsy_Status_Name(st.status), from, detail[0] ? ". " : "", detail);
    const int sev = bsy_Severity(st.status);
    row(rows, "The sign says", t, sev >= 4 ? "error" : sev >= 2 ? "warn" : "ok");
    (void)customColor;
    if (manual != BSY_AUTO){
        if (manualUntil){
            const uint32_t now = (uint32_t)time(nullptr);
            bsy_Span(a, sizeof(a), manualUntil > now ? manualUntil - now : 0);
            snprintf(t, sizeof(t), "%s for %s more", bsy_Status_Name(manual), a);
        } else snprintf(t, sizeof(t), "%s until you change it", bsy_Status_Name(manual));
        row(rows, "Set from the phone", t, "info");
    }
    Bsy_Data_t* d = (Bsy_Data_t*)heap_caps_calloc(1, sizeof(Bsy_Data_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    uint32_t version = 0xFFFFFFFFu;
    bsy_Net_Copy(*d, version);
    static const char* const NAMES[BSY_PROVIDERS] = {"Google Calendar", "Outlook Calendar"};
    const bool use[BSY_PROVIDERS] = {useG, useM};
    const bool linked[BSY_PROVIDERS] = {gLinked, mLinked};
    for (int p = 0; p < BSY_PROVIDERS; p++){
        if (!linked[p] || !use[p]) continue;
        const Bsy_Link_t& l = d->link[p];
        if (l.state == MTB_OAUTH_RELINK) row(rows, NAMES[p], "The link stopped working: link the account again", "error");
        else if (l.error[0]) row(rows, NAMES[p], l.error, "warn");
        else if (d->syncedAt){
            int count = 0;
            for (int i = 0; i < d->cal.count; i++) if (d->cal.e[i].provider == p) count++;
            snprintf(t, sizeof(t), "%u calendar%s, %d event%s today and tomorrow", l.calendars, l.calendars == 1 ? "" : "s", count, count == 1 ? "" : "s");
            row(rows, NAMES[p], t, "ok");
        } else row(rows, NAMES[p], "Reading...", "info");
    }
    if (usePresence && mLinked){
        if (d->presenceOk == 1 && d->presence.known){
            snprintf(t, sizeof(t), "%s, %s", d->presence.availability, d->presence.activity);
            row(rows, "Teams", t, "ok");
        } else if (d->presenceOk == 0 || d->presenceOk == -1) row(rows, "Teams", d->presenceWhy, d->presenceOk == 0 ? "off" : "warn");
    }
    if (!gLinked && !mLinked) row(rows, "Calendars", "None linked: the sign follows working hours and the phone", "info");
    heap_caps_free(d);
}
