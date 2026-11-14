/**
 * Motorsport Live, ID {5,1} (SPORTS): Formula 1 weekends. The weekend's sessions in your time with a countdown and
 * the circuit, live timing while a session runs (with an OpenF1 sponsor account), the latest result, and both
 * championships. Tells you before sessions start, of safety cars and red flags, and the race result.
 *
 * The knob: click, the next page; turn, the next page (on live timing, the other ten cars); two clicks, read again;
 * hold, pages turn by themselves or stay.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_text_scroll.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ntf_Notify.h"
#include "snd_Sound.h"
#include "mts_App.h"
#include "mts_Mobile_Ui.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Motorsport Live)"

EXT_RAM_BSS_ATTR TaskHandle_t motorsport_Task_H = NULL;
static void motorsport_App_Task(void* dApplication);
static void mts_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *motorsport_App;
Mtb_Applications* motorsport_App_GetInstance() {
    if (!motorsport_App){
        motorsport_App = new Mtb_Applications_StatusBar(motorsport_App_Task, &motorsport_Task_H, "Motorsport Live", {5,1}, 8192);
        motorsport_App->mtb_App_Set_Mobile_UI(MTS_UI_MANIFEST, MTS_UI_API, nullptr, mts_Values);
    }
    return motorsport_App;
}
MTB_REGISTER_APP(motorsport_App, 5, 1)

EXT_RAM_BSS_ATTR static Mts_Settings_t settings;
EXT_RAM_BSS_ATTR static Mts_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, testRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

// Told once while Pixlpal runs (also across app cycling).
static uint32_t toldSession = 0, toldResult = 0, toldFlagSession = 0;
static uint8_t toldFlag = MTS_FLAG_NONE;

static bool read_Settings(Mts_Settings_t& s){
    mts_Settings_Default(s);
    Mts_Settings_t* saved = (Mts_Settings_t*)heap_caps_calloc(1, sizeof(Mts_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return false;
    mtb_Read_Nvs_Struct(MTS_NVS_KEY, saved, sizeof(*saved));
    const bool ok = saved->magic == MTS_SETTINGS_MAGIC;
    if (ok) s = *saved;
    memset(saved, 0, sizeof(*saved));
    heap_caps_free(saved);
    mts_Settings_Clamp(s);
    return ok;
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Mts_Settings_t* begin_Change(void){
    Mts_Settings_t* next = (Mts_Settings_t*)heap_caps_malloc(sizeof(Mts_Settings_t), MALLOC_CAP_SPIRAM);
    if (!next) return nullptr;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *next = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*next);
    return next;
}

static void drop_Change(Mts_Settings_t* next){
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
    mtb_Ble_App_Cmd_Reject();
}

static void end_Change(Mts_Settings_t* next){
    mts_Settings_Clamp(*next);
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        received = *next;
        settingsReceived = true;
        portEXIT_CRITICAL(&requestLock);
    } else mtb_Write_Nvs_Struct(MTS_NVS_KEY, next, sizeof(*next));
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

// 0 {"user"} | {"pass"}: the OpenF1 sponsor account; empty forgets it. The password is never sent back.
static void cmd_Account(JsonDocument& c){
    const bool user = !c["user"].isNull();
    if (!user && c["pass"].isNull()){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64];
    if (!clean(user ? (c["user"] | "") : (c["pass"] | ""), t, sizeof(t))){ memset(t, 0, sizeof(t)); mtb_Ble_App_Cmd_Reject(); return; }
    Mts_Settings_t* n = begin_Change();
    if (!n){ memset(t, 0, sizeof(t)); mtb_Ble_App_Cmd_Reject(); return; }
    if (user) snprintf(n->user, sizeof(n->user), "%s", t); else snprintf(n->pass, sizeof(n->pass), "%s", t);
    memset(t, 0, sizeof(t));
    end_Change(n);
}

// 1 {"favourite": 44 | "HAM" | 0}
static void cmd_Favourite(JsonDocument& c){
    JsonVariantConst f = c["favourite"];
    long v = -1;
    if (!mtb_Kit_Json_Range(f, 0, 99, v)){
        // A three-letter code: found among the drivers the app knows (while it runs).
        char code[8];
        if (!f.is<const char*>() || !clean(f.as<const char*>(), code, sizeof(code)) || strlen(code) != 3 || !appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
        for (char* p = code; *p; p++) *p = (char)toupper((unsigned char)*p);
        Mts_Data_t* d = (Mts_Data_t*)heap_caps_calloc(1, sizeof(Mts_Data_t), MALLOC_CAP_SPIRAM);
        if (!d){ mtb_Ble_App_Cmd_Reject(); return; }
        uint32_t version = 0xFFFFFFFFu;
        mts_Net_Copy(*d, version);
        for (int i = 0; i < d->drivers.count; i++) if (!strcmp(d->drivers.d[i].code, code)) v = d->drivers.d[i].number;
        heap_caps_free(d);
        if (v < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    }
    Mts_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->favourite = (uint8_t)v;
    end_Change(n);
}

// 2 {"p0".."p4"} | {"cycle"} | {"seconds"} | {"hour24"} | {"liveFirst"}
static void cmd_Display(JsonDocument& c){
    long v;
    Mts_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char key[4] = "p0";
    for (int i = 0; i < MTS_PAGES; i++){
        key[1] = (char)('0' + i);
        if (c[key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[key], 0, 1, v)){ drop_Change(n); return; }
        if (v) n->pages = (uint8_t)(n->pages | (1u << i)); else n->pages = (uint8_t)(n->pages & ~(1u << i));
        if (!n->pages){ drop_Change(n); return; }
        end_Change(n);
        return;
    }
    if (!c["cycle"].isNull() && mtb_Kit_Json_Range(c["cycle"], 0, 1, v)) n->cycle = (uint8_t)v;
    else if (!c["seconds"].isNull() && mtb_Kit_Json_Range(c["seconds"], 5, 120, v)) n->seconds = (uint8_t)v;
    else if (!c["hour24"].isNull() && mtb_Kit_Json_Range(c["hour24"], 0, 1, v)) n->hour24 = (uint8_t)v;
    else if (!c["liveFirst"].isNull() && mtb_Kit_Json_Range(c["liveFirst"], 0, 1, v)) n->liveFirst = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// 3 {"alertMinutes"} | {"alertRacesOnly"} | {"alertFlags"} | {"sound"} | {"phoneAlerts"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
static void cmd_Alerts(JsonDocument& c){
    long v;
    Mts_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    struct Flag_t { const char* key; uint8_t* field; };
    const Flag_t flags[] = {{"alertRacesOnly", &n->alertRacesOnly}, {"alertFlags", &n->alertFlags}, {"sound", &n->sound}, {"phoneAlerts", &n->phoneAlerts},
                            {"ntfyOn", &n->notify.on[NTF_NTFY]}, {"telegramOn", &n->notify.on[NTF_TELEGRAM]}, {"whatsappOn", &n->notify.on[NTF_WHATSAPP]}};
    for (const Flag_t& f : flags){
        if (c[f.key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[f.key], 0, 1, v)){ drop_Change(n); return; }
        *f.field = (uint8_t)v;
        end_Change(n);
        return;
    }
    if (!c["alertMinutes"].isNull() && mtb_Kit_Json_Range(c["alertMinutes"], 0, 120, v)) n->alertMinutes = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// 4 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
static void cmd_Notify_Text(JsonDocument& c){
    Mts_Settings_t* n = begin_Change();
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

// 5 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}: kept on Pixlpal, never sent back.
static void cmd_Notify_Secret(JsonDocument& c){
    Mts_Settings_t* n = begin_Change();
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

// 6 {} read again now   7 {} a test alert
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

// The knob -------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static int32_t local_Offset(uint32_t now){
    const time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    const int64_t local = (int64_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400
                        + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return (int32_t)(local - (int64_t)now);
}

static const Snd_Note_t START_SOUND[] = {{880, 150}, {0, 120}, {880, 150}, {0, 120}, {880, 150}, {0, 120}, {1760, 450}};
static const Snd_Note_t FLAG_SOUND[] = {{660, 120}, {0, 60}, {520, 220}};

static bool counts_For_Alert(const Mts_Settings_t& s, uint8_t type){
    return !s.alertRacesOnly || type == MTS_QUALIFYING || type == MTS_SPRINT_QUALI || type == MTS_SPRINT || type == MTS_RACE;
}

static void alert(const char* scroll, uint16_t color, const char* title, const char* text, bool phone, const Snd_Note_t* notes, int count){
    char ascii[128];
    mtb_Kit_To_Ascii(ascii, sizeof(ascii), scroll);
    statusBarNotif.mtb_Scroll_This_Text(ascii, color);
    if (settings.sound && notes && !mtb_Silent_Mode()) snd_Play(notes, count, 0.45f, true);
    if (phone && settings.phoneAlerts) ntf_Send(title, text, NTF_NORMAL);
}

static void motorsport_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Account, cmd_Favourite, cmd_Display, cmd_Alerts, cmd_Notify_Text, cmd_Notify_Secret, cmd_Refresh, cmd_Test);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    if (!settings.notify.ntfyTopic[0])
        snprintf(settings.notify.ntfyTopic, sizeof(settings.notify.ntfyTopic), "pixlpal-race-%08lx%04lx", (unsigned long)esp_random(),
                 (unsigned long)(esp_random() & 0xFFFF));
    mtb_Write_Nvs_Struct(MTS_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    refreshRequested = false;
    testRequested = false;
    longPressRequested = false;
    clicksRequested = 0;
    turns = 0;
    Mts_Data_t* d = (Mts_Data_t*)heap_caps_calloc(1, sizeof(Mts_Data_t), MALLOC_CAP_SPIRAM);
    if (!d || !mts_Net_Start(settings)){
        heap_caps_free(d);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    ntf_Start(settings.notify, UA, "checkered_flag");
    appOpen = true;
    uint8_t page = MTS_P_WEEKEND, half = 0;
    uint16_t tick = 0;
    uint32_t version = 0, pageSince = millis(), phoneMs = 0, halfMs = 0, tickMs = 0, lastSecond = 0, alertCheck = 0;
    bool redraw = true, phoneDirty = true, settingsDirty = false, wasLive = false;

    auto shown = [&](uint8_t p, uint32_t now) -> bool { return (settings.pages & (1u << p)) && mts_Page_Ready(*d, p, now); };
    auto step_Page = [&](int dir, uint32_t now){
        for (int i = 0; i < MTS_PAGES; i++){
            page = (uint8_t)((page + MTS_PAGES + dir) % MTS_PAGES);
            if (shown(page, now)) break;
        }
        pageSince = millis();
        half = 0;
        redraw = true;
    };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            mts_Net_Set_Settings(settings);
            ntf_Set_Config(settings.notify);
            settingsDirty = redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; mts_Net_Refresh(); }
        if (mts_Net_Copy(*d, version)){ redraw = phoneDirty = true; }

        // During a session, live timing first.
        const int li = d->schedOk ? mts_Live_Session(d->sched, now) : -1;
        const bool onTrack = li >= 0 && now + 300 >= d->sched.s[li].start && now <= d->sched.s[li].end + 600;
        if (onTrack && !wasLive && settings.liveFirst && (settings.pages & (1u << MTS_P_LIVE))){ page = MTS_P_LIVE; half = 0; pageSince = ms; redraw = true; }
        wasLive = onTrack;

        // Alerts, each told once.
        if (d->schedOk && now > 1700000000u && (!alertCheck || ms - alertCheck > 5000)){
            alertCheck = ms ? ms : 1;
            const int nx = mts_Next_Session(d->sched, now);
            if (settings.alertMinutes && nx >= 0){
                const Mts_Session_t& x = d->sched.s[nx];
                if (x.key != toldSession && counts_For_Alert(settings, x.type) && x.start - now <= settings.alertMinutes * 60u){
                    toldSession = x.key;
                    char where[48], name[24], scroll[96], text[160];
                    mtb_Kit_To_Ascii(where, sizeof(where), mts_Meeting_Name(d->sched, x.meeting));
                    mtb_Kit_To_Ascii(name, sizeof(name), x.name);
                    const uint32_t m = (x.start - now + 59) / 60;
                    snprintf(scroll, sizeof(scroll), "%s: %s IN %lu MIN", where, name, (unsigned long)m);
                    mtb_Kit_Upper(scroll);
                    snprintf(text, sizeof(text), "%s starts in %lu minutes.", name, (unsigned long)m);
                    alert(scroll, KIT_GOLD, where, text, true, START_SOUND, (int)(sizeof(START_SOUND) / sizeof(START_SOUND[0])));
                }
            }
            // Safety cars, red flags, the chequered flag.
            if (settings.alertFlags && d->liveOk && d->live.session){
                const uint8_t f = d->live.flag;
                if (d->live.session != toldFlagSession){ toldFlagSession = d->live.session; toldFlag = f; }
                else if (f != toldFlag){
                    toldFlag = f;
                    if (f == MTS_FLAG_SC || f == MTS_FLAG_VSC || f == MTS_FLAG_RED || f == MTS_FLAG_CHEQUERED)
                        alert(mts_Flag_Text(f), mts_Flag_Color(f), "Race control", mts_Flag_Text(f), f == MTS_FLAG_RED,
                              FLAG_SOUND, (int)(sizeof(FLAG_SOUND) / sizeof(FLAG_SOUND[0])));
                }
            }
            // A new race or sprint result.
            const Mts_Result_t& r = d->result;
            if (r.count >= 3 && r.session != toldResult && (r.type == MTS_RACE || r.type == MTS_SPRINT)){
                const bool first = toldResult == 0;
                toldResult = r.session;
                // Only results that come in while the app runs: not the old one found at start.
                int ri = -1;
                for (int i = 0; i < d->sched.count; i++) if (d->sched.s[i].key == r.session) ri = i;
                if (!first || (ri >= 0 && now - d->sched.s[ri].end < 3 * 3600)){
                    char who[3][8], scroll[96], title[64], text[160];
                    for (int i = 0; i < 3; i++){
                        const Mts_Driver_t* dv = mts_Driver(d->drivers, r.r[i].number);
                        snprintf(who[i], sizeof(who[i]), "%s", dv ? dv->code : "?");
                    }
                    char where[48];
                    mtb_Kit_To_Ascii(where, sizeof(where), ri >= 0 ? mts_Meeting_Name(d->sched, d->sched.s[ri].meeting) : "");
                    snprintf(scroll, sizeof(scroll), "%s: 1 %s  2 %s  3 %s", r.type == MTS_RACE ? "RACE" : "SPRINT", who[0], who[1], who[2]);
                    snprintf(title, sizeof(title), "%s %s result", where, r.type == MTS_RACE ? "race" : "sprint");
                    snprintf(text, sizeof(text), "1 %s, 2 %s, 3 %s.", who[0], who[1], who[2]);
                    alert(scroll, KIT_GREEN, title, text, true, nullptr, 0);
                    if (settings.pages & (1u << MTS_P_RESULT)){ page = MTS_P_RESULT; pageSince = ms; redraw = true; }
                }
            }
        }
        if (testRequested){
            testRequested = false;
            ntf_Send("Motorsport Live test", "A test from Pixlpal's Motorsport Live. Session and result alerts will arrive like this one.", NTF_NORMAL);
        }

        // The knob.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        const int t = turns;
        if (t) turns = 0;
        if (clicks == 1) step_Page(1, now);
        else if (clicks == 2) mts_Net_Refresh();
        if (t){
            if (page == MTS_P_LIVE && d->live.count > 10 && d->liveOk){ half ^= 1; halfMs = ms; redraw = true; }
            else step_Page(t > 0 ? 1 : -1, now);
        }
        if (longPressRequested){ longPressRequested = false; settings.cycle = settings.cycle ? 0 : 1; settingsDirty = phoneDirty = true; pageSince = ms; }
        if (!shown(page, now) && d->schedOk) step_Page(1, now);
        const bool stay = onTrack && settings.liveFirst && page == MTS_P_LIVE;
        if (settings.cycle && !stay && ms - pageSince >= settings.seconds * 1000u) step_Page(1, now);
        // Live timing shows the rest of the field every 6 seconds.
        if (page == MTS_P_LIVE && d->live.count > 10 && ms - halfMs >= 6000){ halfMs = ms; half ^= 1; redraw = true; }
        // The car round the circuit, and the countdowns.
        if (page == MTS_P_WEEKEND && d->track.n && ms - tickMs >= 120){ tickMs = ms; tick++; redraw = true; }
        if (now != lastSecond){ lastSecond = now; if (page == MTS_P_WEEKEND || page == MTS_P_LIVE) redraw = true; }

        if (redraw){
            redraw = false;
            Mts_View_t v = {page, &settings, d, now, local_Offset(now), half, tick, nullptr};
            if (now < 1700000000u) v.message = "Waiting for the clock...";
            else if (!Mtb_Applications::internetConnectStatus && !d->schedOk) v.message = "Waiting for the internet...";
            mts_Draw(v);
        }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(MTS_NVS_KEY, &settings, sizeof(settings)); }
        if (phoneDirty && ms - phoneMs > 3000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(30);
    }
    appOpen = false;
    mts_Net_Stop();
    ntf_Stop();
    if (settingsDirty) mtb_Write_Nvs_Struct(MTS_NVS_KEY, &settings, sizeof(settings));
    memset(settings.pass, 0, sizeof(settings.pass));
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

static void mts_Values(JsonObject values){
    Mts_Settings_t* s = (Mts_Settings_t*)heap_caps_malloc(sizeof(Mts_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    char t[192], a[48], b[48];
    values["user"] = s->user;
    values["pass_set"] = s->pass[0] ? 1 : 0;
    values["favourite"] = s->favourite;
    char key[4] = "p0";
    for (int i = 0; i < MTS_PAGES; i++){ key[1] = (char)('0' + i); values[key] = (s->pages >> i) & 1; }
    values["cycle"] = s->cycle;
    values["seconds"] = s->seconds;
    values["hour24"] = s->hour24;
    values["liveFirst"] = s->liveFirst;
    values["alertMinutes"] = s->alertMinutes;
    values["alertRacesOnly"] = s->alertRacesOnly;
    values["alertFlags"] = s->alertFlags;
    values["sound"] = s->sound;
    values["phoneAlerts"] = s->phoneAlerts;
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
    const bool hasAccount = s->user[0] && s->pass[0], hour24 = s->hour24;
    const uint8_t favourite = s->favourite;
    char topic[80];
    snprintf(topic, sizeof(topic), "%s%s%s", strcmp(k.ntfyServer, "ntfy.sh") ? k.ntfyServer : "", strcmp(k.ntfyServer, "ntfy.sh") ? "/" : "", k.ntfyTopic);
    const bool ntfy = s->phoneAlerts && k.on[NTF_NTFY] && k.ntfyTopic[0];
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);

    JsonArray alerts = values["alertRows"].to<JsonArray>();
    if (ntfy) row(alerts, "Subscribe in ntfy to", topic, "info");
    JsonArray rows = values["status"].to<JsonArray>();
    if (!appOpen){
        row(rows, "OpenF1", hasAccount ? "Sponsor account saved: live timing" : "Free: results and championships, no live timing", "info");
        row(rows, "Now", "Open Motorsport Live on Pixlpal to see the weekend here.", "off");
        return;
    }
    Mts_Data_t* d = (Mts_Data_t*)heap_caps_calloc(1, sizeof(Mts_Data_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    uint32_t version = 0xFFFFFFFFu;
    mts_Net_Copy(*d, version);
    const uint32_t now = (uint32_t)time(nullptr);
    const int32_t off = local_Offset(now);
    if (!d->schedOk) row(rows, "Season", d->why[0] ? d->why : "Reading...", d->why[0] ? "warn" : "info");
    else {
        const int li = mts_Live_Session(d->sched, now);
        if (li >= 0){
            const Mts_Session_t& x = d->sched.s[li];
            if (d->liveBlocked) snprintf(t, sizeof(t), "%s: %s. Live timing needs a sponsor account", mts_Meeting_Name(d->sched, x.meeting), x.name);
            else if (d->liveOk){
                if (d->live.lap) snprintf(a, sizeof(a), ", lap %u", d->live.lap); else a[0] = '\0';
                snprintf(t, sizeof(t), "%s%s%s%s", x.name, a, d->live.flag > MTS_FLAG_GREEN ? ", " : "", d->live.flag > MTS_FLAG_GREEN ? mts_Flag_Text(d->live.flag) : "");
            } else snprintf(t, sizeof(t), "%s: waiting for live timing", x.name);
            row(rows, "Live now", t, d->liveBlocked ? "warn" : "ok");
        }
        const int nx = mts_Next_Session(d->sched, now);
        if (nx >= 0){
            const Mts_Session_t& x = d->sched.s[nx];
            mts_Clock(a, sizeof(a), x.start, off, hour24);
            mts_Countdown(b, sizeof(b), x.start - now);
            snprintf(t, sizeof(t), "%s, %s at %s (in %s)", mts_Meeting_Name(d->sched, x.meeting), x.name, a, b);
            row(rows, "Next", t, "info");
        }
        if (d->result.count >= 3){
            const char* names[3];
            char codes[3][8];
            for (int i = 0; i < 3; i++){
                const Mts_Driver_t* dv = mts_Driver(d->drivers, d->result.r[i].number);
                snprintf(codes[i], sizeof(codes[i]), "%s", dv ? dv->code : "?");
                names[i] = codes[i];
            }
            const char* what = "";
            for (int i = 0; i < d->sched.count; i++) if (d->sched.s[i].key == d->result.session) what = d->sched.s[i].name;
            snprintf(t, sizeof(t), "%s: 1 %s, 2 %s, 3 %s", what, names[0], names[1], names[2]);
            row(rows, "Last result", t, "ok");
        }
        if (d->standings.drivers){
            const Mts_Driver_t* dv = mts_Driver(d->drivers, d->standings.d[0].number);
            snprintf(t, sizeof(t), "%s, %g points", dv ? dv->name : "?", (double)d->standings.d[0].points);
            if (favourite){
                for (int i = 0; i < d->standings.drivers; i++) if (d->standings.d[i].number == favourite){
                    const Mts_Driver_t* f = mts_Driver(d->drivers, favourite);
                    snprintf(a, sizeof(a), ". %s P%u, %g", f ? f->name : "Yours", d->standings.d[i].position, (double)d->standings.d[i].points);
                    strncat(t, a, sizeof(t) - strlen(t) - 1);
                }
            }
            row(rows, "Championship", t, "info");
        }
    }
    const char* plan = d->plan == MTS_PLAN_SPONSOR ? "Sponsor: live timing on" : d->plan == MTS_PLAN_REFUSED ? "Sign-in refused" : hasAccount ? "Signing in..." : "Free: no live timing";
    snprintf(t, sizeof(t), "%s. %lu requests today", d->planWhy[0] && d->plan != MTS_PLAN_SPONSOR ? d->planWhy : plan, (unsigned long)d->calls);
    row(rows, "OpenF1", t, d->plan == MTS_PLAN_REFUSED ? "error" : d->plan == MTS_PLAN_SPONSOR ? "ok" : "info");
    heap_caps_free(d);
}
