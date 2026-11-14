/**
 * Calm Corner, ID {19,2} (KIDS, ages 4-10): "how do you feel?" with seven faces, a kind word, then something that
 * helps: breathing with a growing and shrinking balloon, finding things to see and hear, slow stretches, or calm
 * lights with soft notes. Then "how do you feel now?". Parents see the feelings on their phone and can be told of
 * hard ones. It never counts towards the daily limit and works at bedtime.
 *
 * The knob: turn to choose, press to choose (in "look around", press for each thing found); hold to go back
 * (on the feelings: who is here).
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ntf_Notify.h"
#include "snd_Sound.h"
#include "clm_App.h"
#include "clm_Mobile_Ui.h"
#include <time.h>
#include <string.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Calm Corner)"

EXT_RAM_BSS_ATTR TaskHandle_t calmCorner_Task_H = NULL;
static void calmCorner_App_Task(void* dApplication);
static void clm_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *calmCorner_App;
Mtb_Applications* calmCorner_App_GetInstance() {
    if (!calmCorner_App){
        calmCorner_App = new Mtb_Applications_StatusBar(calmCorner_App_Task, &calmCorner_Task_H, "Calm Corner", {19,2}, 8192);
        calmCorner_App->mtb_App_Set_Mobile_UI(CLM_UI_MANIFEST, CLM_UI_API, nullptr, clm_Values);
    }
    return calmCorner_App;
}
MTB_REGISTER_APP(calmCorner_App, 19, 2)

EXT_RAM_BSS_ATTR static Clm_Settings_t settings;
EXT_RAM_BSS_ATTR static Clm_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, testRequested = false, longPressRequested = false, clearRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static void read_Settings(Clm_Settings_t& s){
    clm_Settings_Default(s);
    Clm_Settings_t* saved = (Clm_Settings_t*)heap_caps_calloc(1, sizeof(Clm_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(CLM_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == CLM_SETTINGS_MAGIC) s = *saved;
    memset(saved, 0, sizeof(*saved));
    heap_caps_free(saved);
    clm_Settings_Clamp(s);
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------
static Clm_Settings_t* begin_Change(void){
    Clm_Settings_t* n = (Clm_Settings_t*)heap_caps_malloc(sizeof(Clm_Settings_t), MALLOC_CAP_SPIRAM);
    if (!n) return nullptr;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *n = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*n);
    return n;
}
static void drop_Change(Clm_Settings_t* n){ memset(n, 0, sizeof(*n)); heap_caps_free(n); mtb_Ble_App_Cmd_Reject(); }
static void end_Change(Clm_Settings_t* n){
    clm_Settings_Clamp(*n);
    if (appOpen){ portENTER_CRITICAL(&requestLock); received = *n; settingsReceived = true; portEXIT_CRITICAL(&requestLock); }
    else mtb_Write_Nvs_Struct(CLM_NVS_KEY, n, sizeof(*n));
    memset(n, 0, sizeof(*n));
    heap_caps_free(n);
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

// 0 {"breathStyle"} | {"breaths"} | {"tones"} | {"lightsMinutes"} | {"keepLog"}
static void cmd_Calm(JsonDocument& c){
    long v;
    Clm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["breathStyle"].isNull() && mtb_Kit_Json_Range(c["breathStyle"], 0, CLM_BREATH_STYLES - 1, v)) n->breathStyle = (uint8_t)v;
    else if (!c["breaths"].isNull() && mtb_Kit_Json_Range(c["breaths"], 3, 10, v)) n->breaths = (uint8_t)v;
    else if (!c["tones"].isNull() && mtb_Kit_Json_Range(c["tones"], 0, 1, v)) n->tones = (uint8_t)v;
    else if (!c["lightsMinutes"].isNull() && mtb_Kit_Json_Range(c["lightsMinutes"], 1, 10, v)) n->lightsMinutes = (uint8_t)v;
    else if (!c["keepLog"].isNull() && mtb_Kit_Json_Range(c["keepLog"], 0, 1, v)){ n->keepLog = (uint8_t)v; if (!v) n->logCount = n->logHead = 0; }
    else { drop_Change(n); return; }
    end_Change(n);
}

// 1 {"a0".."a6"} feelings to tell parents of | {"phoneAlerts"} | {"ntfyOn"} | {"telegramOn"} | {"whatsappOn"}
static void cmd_Alerts(JsonDocument& c){
    long v;
    Clm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char key[4] = "a0";
    for (int i = 0; i < CLM_FEELINGS; i++){
        key[1] = (char)('0' + i);
        if (c[key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[key], 0, 1, v)){ drop_Change(n); return; }
        if (v) n->alertFeelings |= (uint8_t)(1u << i); else n->alertFeelings &= (uint8_t)~(1u << i);
        end_Change(n);
        return;
    }
    struct Flag_t { const char* key; uint8_t* field; };
    const Flag_t flags[] = {{"phoneAlerts", &n->phoneAlerts}, {"ntfyOn", &n->notify.on[NTF_NTFY]}, {"telegramOn", &n->notify.on[NTF_TELEGRAM]},
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

// 2 {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
static void cmd_Notify_Text(JsonDocument& c){
    Clm_Settings_t* n = begin_Change();
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

// 3 {"ntfyToken"} | {"telegramToken"} | {"whatsappKey"}: kept on Pixlpal, never sent back.
static void cmd_Notify_Secret(JsonDocument& c){
    Clm_Settings_t* n = begin_Change();
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

// 4 {} forget the feelings   5 {} a test message
static void cmd_Clear(JsonDocument&){
    if (appOpen){ clearRequested = true; return; }
    Clm_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->logCount = n->logHead = 0;
    end_Change(n);
}

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
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = 1;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------
static const float CALM_NOTES[] = {392.0f, 440.0f, 523.25f, 587.33f, 659.25f, 784.0f};   // a pentatonic scale

static void soft(const Kk_Session_t& k, float a, float b){
    if (!k.sound || !settings.tones) return;
    const Snd_Note_t n[2] = {{a, 260}, {b, 420}};
    snd_Play(n, 2, 0.12f, false);
}

static void calmCorner_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Calm, cmd_Alerts, cmd_Notify_Text, cmd_Notify_Secret, cmd_Clear, cmd_Test);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    if (!settings.notify.ntfyTopic[0])
        snprintf(settings.notify.ntfyTopic, sizeof(settings.notify.ntfyTopic), "pixlpal-calm-%08lx%04lx", (unsigned long)esp_random(),
                 (unsigned long)(esp_random() & 0xFFFF));
    mtb_Write_Nvs_Struct(CLM_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    testRequested = false;
    longPressRequested = false;
    clearRequested = false;
    clicksRequested = 0;
    turns = 0;
    Kk_Session_t* k = (Kk_Session_t*)heap_caps_calloc(1, sizeof(Kk_Session_t), MALLOC_CAP_SPIRAM);
    if (!k){ mtb_Delete_This_App(THIS_APP); return; }
    kk_Begin(*k);
    ntf_Start(settings.notify, UA, "heart");
    appOpen = true;
    Clm_View_t v = {};
    v.s = &settings;
    v.k = k;
    v.screen = k->count > 1 ? CLM_SCR_PICK : CLM_SCR_FEEL;
    v.pick = k->child;
    v.feeling = CLM_HAPPY;
    uint32_t started = millis(), screenAt = millis(), nextNote = 0, frameMs = 0;
    uint8_t lastPhase = 0xFF, activity = 0xFF;
    bool redraw = true, dirty = false, logged = false;

    auto go = [&](uint8_t screen){ v.screen = screen; started = screenAt = millis(); redraw = true; lastPhase = 0xFF; };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            const Clm_Settings_t keep = settings;
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            // The log is this task's: kept unless switched off.
            if (settings.keepLog){ settings.logCount = keep.logCount; settings.logHead = keep.logHead; memcpy(settings.log, keep.log, sizeof(settings.log)); }
            ntf_Set_Config(settings.notify);
            dirty = redraw = true;
        }
        if (clearRequested){ clearRequested = false; settings.logCount = settings.logHead = 0; dirty = true; }
        if (testRequested){ testRequested = false; ntf_Send("Calm Corner test", "A test from Pixlpal's Calm Corner. Messages about feelings will arrive like this one.", NTF_NORMAL); }
        kk_Tick(*k, false);         // never counts towards the limit
        v.ms = ms - started;

        int t = turns;
        if (t) turns = 0;
        const bool click = clicksRequested != 0;
        clicksRequested = 0;
        const bool hold = longPressRequested;
        longPressRequested = false;
        const int dir = t > 0 ? 1 : t < 0 ? -1 : 0;

        switch (v.screen){
            case CLM_SCR_PICK:
                if (dir){ v.pick = (uint8_t)((v.pick + k->count + dir) % k->count); redraw = true; }
                if (click){ kk_Select(*k, v.pick); go(CLM_SCR_FEEL); }
                break;
            case CLM_SCR_FEEL:
            case CLM_SCR_AFTER:
                if (dir){ v.feeling = (uint8_t)((v.feeling + CLM_FEELINGS + dir) % CLM_FEELINGS); redraw = true; kk_Sound(*k, KK_CLICK); }
                if (hold){
                    if (v.screen == CLM_SCR_FEEL && k->count > 1){ v.pick = k->child; go(CLM_SCR_PICK); }
                    else go(CLM_SCR_FEEL);
                    break;
                }
                if (!click) break;
                if (v.screen == CLM_SCR_FEEL){
                    // Kept for the parents, and told if they asked.
                    if (settings.keepLog && now > 1700000000u){
                        Clm_Entry_t e = {now, k->real[k->child], v.feeling, 0xFF, 0xFF};
                        clm_Log(settings, e);
                        logged = true;
                        dirty = true;
                    }
                    if (settings.phoneAlerts && (settings.alertFeelings & (1u << v.feeling))){
                        char title[64], text[128], name[20];
                        mtb_Kit_To_Ascii(name, sizeof(name), kk_Name(*k));
                        snprintf(title, sizeof(title), "%s is feeling %s", name, clm_Feeling_Word(v.feeling));
                        snprintf(text, sizeof(text), "%s chose \"%s\" in Calm Corner just now. A hug or a chat may help.", name, clm_Feeling_Word(v.feeling));
                        ntf_Send(title, text, NTF_NORMAL);
                    }
                    v.choice = clm_Suggest(v.feeling);
                    kk_Sound(*k, KK_SOFT);
                    go(CLM_SCR_ANSWER);
                } else {
                    if (logged && settings.keepLog){
                        settings.log[(settings.logHead + CLM_LOG - 1) % CLM_LOG].after = v.feeling;
                        settings.log[(settings.logHead + CLM_LOG - 1) % CLM_LOG].activity = activity;
                        dirty = true;
                    }
                    logged = false;
                    kk_Add_Stars(*k, 1);
                    go(CLM_SCR_THANKS);
                }
                break;
            case CLM_SCR_ANSWER:
                if (dir){ v.choice = (uint8_t)((v.choice + CLM_CHOICES + dir) % CLM_CHOICES); redraw = true; kk_Sound(*k, KK_CLICK); }
                if (hold){ go(CLM_SCR_FEEL); break; }
                if (click){
                    activity = v.choice < CLM_ACTIVITIES ? v.choice : 0xFF;
                    switch (v.choice){
                        case CLM_BREATHE: go(CLM_SCR_BREATHE); break;
                        case CLM_GROUND: v.groundStep = 0; v.groundFound = 0; go(CLM_SCR_GROUND); break;
                        case CLM_STRETCH: v.stretch = 0; go(CLM_SCR_STRETCH); break;
                        case CLM_LIGHTS: go(CLM_SCR_LIGHTS); nextNote = ms; break;
                        default: kk_Add_Stars(*k, 1); go(CLM_SCR_THANKS); logged = false; break;
                    }
                }
                break;
            case CLM_SCR_BREATHE: {
                Clm_Breath_t b;
                clm_Breath_At(settings.breathStyle, kk_Age(*k), settings.breaths, v.ms, b);
                if (b.phase != lastPhase){
                    if (b.phase == CLM_IN) soft(*k, 392.0f, 523.25f);
                    else if (b.phase == CLM_OUT) soft(*k, 523.25f, 392.0f);
                    lastPhase = b.phase;
                }
                if (b.phase == CLM_DONE && ms - screenAt > 1000){
                    uint8_t p[4];
                    clm_Breath_Pattern(settings.breathStyle, kk_Age(*k), p);
                    if (v.ms > (uint32_t)(p[0] + p[1] + p[2] + p[3]) * 1000u * settings.breaths + 2500) go(CLM_SCR_AFTER);
                }
                if (hold || click) go(CLM_SCR_AFTER);
                if (ms - frameMs >= 50){ frameMs = ms; redraw = true; }
                break;
            }
            case CLM_SCR_GROUND: {
                const uint8_t need = clm_Ground_Count(v.groundStep);
                if (click && v.groundFound < need){ v.groundFound++; kk_Sound(*k, KK_CLICK); screenAt = ms; redraw = true; }
                if (v.groundFound >= need && ms - screenAt > 700){
                    if (v.groundStep + 1 >= clm_Ground_Steps(kk_Age(*k))){ kk_Sound(*k, KK_YAY); go(CLM_SCR_AFTER); }
                    else { v.groundStep++; v.groundFound = 0; screenAt = ms; redraw = true; }
                }
                if (hold) go(CLM_SCR_AFTER);
                break;
            }
            case CLM_SCR_STRETCH:
                v.stretch = (int)(v.ms / (CLM_STRETCH_SECONDS * 1000u));
                if (click){ started -= CLM_STRETCH_SECONDS * 1000u - (v.ms % (CLM_STRETCH_SECONDS * 1000u)); }   // the next one
                if (v.stretch >= CLM_STRETCHES || hold){ go(CLM_SCR_AFTER); break; }
                if (ms - frameMs >= 60){ frameMs = ms; redraw = true; }
                break;
            case CLM_SCR_LIGHTS:
                if (v.ms >= settings.lightsMinutes * 60000u || click || hold){ go(CLM_SCR_AFTER); break; }
                if (k->sound && settings.tones && ms >= nextNote){
                    const Snd_Note_t n = {CALM_NOTES[esp_random() % 6], 900};
                    snd_Play(&n, 1, 0.08f, false);
                    nextNote = ms + 3000 + esp_random() % 3000;
                }
                if (ms - frameMs >= 80){ frameMs = ms; redraw = true; }
                break;
            case CLM_SCR_THANKS:
                if (click || hold || ms - screenAt > 5000) go(CLM_SCR_FEEL);
                break;
        }

        if (redraw){ redraw = false; clm_Draw(v); }
        if (dirty){ dirty = false; mtb_Write_Nvs_Struct(CLM_NVS_KEY, &settings, sizeof(settings)); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    snd_Stop();
    kk_End(*k);
    ntf_Stop();
    mtb_Write_Nvs_Struct(CLM_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(k);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------
static void clm_Values(JsonObject values){
    Clm_Settings_t* s = begin_Change();
    if (!s) return;
    values["breathStyle"] = s->breathStyle;
    values["breaths"] = s->breaths;
    values["tones"] = s->tones;
    values["lightsMinutes"] = s->lightsMinutes;
    values["keepLog"] = s->keepLog;
    char key[4] = "a0";
    for (int i = 0; i < CLM_FEELINGS; i++){ key[1] = (char)('0' + i); values[key] = (s->alertFeelings >> i) & 1; }
    values["phoneAlerts"] = s->phoneAlerts;
    const Ntf_Config_t& n = s->notify;
    values["ntfyOn"] = n.on[NTF_NTFY];
    values["telegramOn"] = n.on[NTF_TELEGRAM];
    values["whatsappOn"] = n.on[NTF_WHATSAPP];
    values["ntfyServer"] = n.ntfyServer;
    values["ntfyTopic"] = n.ntfyTopic;
    values["telegramChat"] = n.telegramChat;
    values["whatsappPhone"] = n.whatsappPhone;
    values["ntfyToken_set"] = n.ntfyToken[0] ? 1 : 0;
    values["telegramToken_set"] = n.telegramToken[0] ? 1 : 0;
    values["whatsappKey_set"] = n.whatsappKey[0] ? 1 : 0;
    char t[160], a[48];
    snprintf(a, sizeof(a), "%s%s%s", strcmp(n.ntfyServer, "ntfy.sh") ? n.ntfyServer : "", strcmp(n.ntfyServer, "ntfy.sh") ? "/" : "", n.ntfyTopic);
    JsonArray alerts = values["alertRows"].to<JsonArray>();
    if (s->phoneAlerts && n.on[NTF_NTFY] && n.ntfyTopic[0]){ JsonObject o = alerts.add<JsonObject>(); o["label"] = "Subscribe in ntfy to"; o["value"] = a; o["state"] = "info"; }

    Kk_Session_t k;
    kk_Begin(k);
    const uint32_t now = (uint32_t)time(nullptr);
    JsonArray rows = values["status"].to<JsonArray>();
    for (int c = 0; c < k.count; c++){
        uint16_t counts[CLM_FEELINGS];
        clm_Log_Counts(*s, k.real[c], now, 7, counts);
        t[0] = '\0';
        int total = 0;
        for (int f = 0; f < CLM_FEELINGS; f++){
            if (!counts[f]) continue;
            total += counts[f];
            snprintf(a, sizeof(a), "%s%u %s", t[0] ? ", " : "", counts[f], clm_Feeling_Word(f));
            strncat(t, a, sizeof(t) - strlen(t) - 1);
        }
        JsonObject o = rows.add<JsonObject>();
        o["label"] = k.names[c];
        o["value"] = total ? t : "No check-ins this week";
        o["state"] = (counts[CLM_SAD] + counts[CLM_WORRIED] + counts[CLM_ANGRY]) * 2 > total && total ? "warn" : "info";
    }
    JsonArray log = values["logRows"].to<JsonArray>();
    Clm_Entry_t e;
    for (int i = 0; i < 12 && clm_Log_Get(*s, i, e); i++){
        const char* who = "";
        for (int c = 0; c < k.count; c++) if (k.real[c] == e.child) who = k.names[c];
        if (e.after < CLM_FEELINGS && e.activity < CLM_ACTIVITIES)
            snprintf(t, sizeof(t), "%s %s, then %s after \"%s\"", who, clm_Feeling_Word(e.feeling), clm_Feeling_Word(e.after), clm_Activity_Name(e.activity));
        else snprintf(t, sizeof(t), "%s %s", who, clm_Feeling_Word(e.feeling));
        values[String("logAt") + i] = e.at;
        JsonObject o = log.add<JsonObject>();
        o["label"] = "Felt";
        o["value"] = t;
        o["ago"] = String("logAt") + i;
        o["state"] = e.feeling >= CLM_SAD ? "warn" : "ok";
    }
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
}
