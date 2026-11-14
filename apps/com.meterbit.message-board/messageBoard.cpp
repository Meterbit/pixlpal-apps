/**
 * Message Board, ID {22,1} (MAKER / NOTIFICATIONS): messages sent to an ntfy topic, shown on Pixlpal with a chime.
 * Polls GET <server>/<topic>/json?poll=1&since=<last id> (ntfy's documented poll mode: one JSON object a line).
 * Priority 4 shows orange, 5 flashes red with an alarm. Keeps the last 8 messages (NVS "mbdSave"), so they are
 * there after a restart. The access token, if any, stays on Pixlpal.
 *
 * Knob: turn for older and newer messages, click to mark them read, hold to clear them.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "mbd_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define TAG             "MBD"
#define W               256
#define H               128
#define MSGS            8
#define TITLE_LEN       48
#define BODY_LEN        200
#define NVS_KEY         "mbdSave"
#define SAVE_MAGIC      0x4D424401u
#define UA              "Pixlpal-MessageBoard/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t messageBoard_Task_H = NULL;
static void messageBoard_App_Task(void* dApplication);
static void mbd_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* messageBoard_App;
Mtb_Applications* messageBoard_App_GetInstance(){
    if (!messageBoard_App){
        messageBoard_App = new Mtb_Applications_FullScreen(messageBoard_App_Task, &messageBoard_Task_H, "Message Board", {22,1}, 8192);
        messageBoard_App->mtb_App_Set_Mobile_UI(MBD_UI_MANIFEST, MBD_UI_API, nullptr, mbd_Values);
    }
    return messageBoard_App;
}
MTB_REGISTER_APP(messageBoard_App, 22, 1)

struct Mbd_Msg_t {
    uint32_t time;
    uint8_t priority, read;
    uint8_t pad[2];
    char title[TITLE_LEN];
    char body[BODY_LEN];
};
struct Mbd_Save_t {
    uint32_t magic;
    char server[64];
    char topic[68];
    char token[72];
    char lastId[24];                    // the newest message seen: the next poll asks for what came after
    uint16_t pollSec;
    uint8_t sound, count;               // messages[0] is the newest
    Mbd_Msg_t msg[MSGS];
};

static EXT_RAM_BSS_ATTR Mbd_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, pollNow = false, workerStop = false, workerRunning = false;
static volatile uint32_t arrived = 0;   // messages the worker added (counted up; the app compares)
static volatile uint8_t arrivedTop = 0; // the highest priority among them
static volatile int lastCode = 0;       // the last poll's answer, for the phone
static volatile uint32_t lastPollAt = 0;

static bool topic_Valid(const char* t){
    const size_t n = strlen(t);
    if (n == 0 || n > 64) return false;
    for (size_t i = 0; i < n; i++){
        const char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static void random_Topic(char* out, size_t size){
    static const char A[] = "abcdefghijkmnpqrstuvwxyz23456789";
    char r[11];
    for (int i = 0; i < 10; i++) r[i] = A[esp_random() % (sizeof(A) - 1)];
    r[10] = '\0';
    snprintf(out, size, "pixlpal-%s", r);
}

static void read_Save(Mbd_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s));
        s.magic = SAVE_MAGIC;
        snprintf(s.server, sizeof(s.server), "https://ntfy.sh");
        random_Topic(s.topic, sizeof(s.topic));
        s.pollSec = 30;
        s.sound = 1;
    }
    s.server[sizeof(s.server) - 1] = s.topic[sizeof(s.topic) - 1] = s.token[sizeof(s.token) - 1] = s.lastId[sizeof(s.lastId) - 1] = '\0';
    if (!topic_Valid(s.topic)) random_Topic(s.topic, sizeof(s.topic));
    if (strncmp(s.server, "https://", 8)) snprintf(s.server, sizeof(s.server), "https://ntfy.sh");
    if (s.pollSec < 10 || s.pollSec > 300) s.pollSec = 30;
    if (s.count > MSGS) s.count = MSGS;
}

static void write_Save(void){
    Mbd_Save_t* copy = (Mbd_Save_t*)heap_caps_malloc(sizeof(Mbd_Save_t), MALLOC_CAP_SPIRAM);
    if (!copy) return;
    portENTER_CRITICAL(&lock); *copy = save; portEXIT_CRITICAL(&lock);
    mtb_Write_Nvs_Struct(NVS_KEY, copy, sizeof(*copy));
    heap_caps_free(copy);
}

// Phone commands (the BLE task) ------------------------------------------------------------------------------------------------
static void cmd_Topic(JsonDocument& c){
    const char* t = c["topic"] | "";
    while (*t == ' ') t++;
    char v[68];
    snprintf(v, sizeof(v), "%s", t);
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ') v[--n] = '\0';
    if (!topic_Valid(v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    if (strcmp(save.topic, v)){ snprintf(save.topic, sizeof(save.topic), "%s", v); save.lastId[0] = '\0'; }
    portEXIT_CRITICAL(&lock);
    pollNow = true;
    phoneChanged = true;
}
static void cmd_Server(JsonDocument& c){
    const char* t = c["server"] | "";
    while (*t == ' ') t++;
    char v[64];
    if (!*t) snprintf(v, sizeof(v), "https://ntfy.sh");
    else if (strncmp(t, "https://", 8) || strlen(t) >= sizeof(v) || strlen(t) < 12){ mtb_Ble_App_Cmd_Reject(); return; }
    else snprintf(v, sizeof(v), "%s", t);
    size_t n = strlen(v);
    while (n && (v[n - 1] == '/' || v[n - 1] == ' ')) v[--n] = '\0';
    for (size_t k = 0; k < n; k++) if ((unsigned char)v[k] <= ' '){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    if (strcmp(save.server, v)){ snprintf(save.server, sizeof(save.server), "%s", v); save.lastId[0] = '\0'; }
    portEXIT_CRITICAL(&lock);
    pollNow = true;
    phoneChanged = true;
}
static void cmd_Token(JsonDocument& c){
    const char* t = c["token"] | "";
    while (*t == ' ') t++;
    if (strlen(t) >= sizeof(save.token)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = t; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.token, sizeof(save.token), "%s", t); portEXIT_CRITICAL(&lock);
    pollNow = true;
    phoneChanged = true;
}
static void cmd_Settings(JsonDocument& c){
    long v;
    if (!c["pollSec"].isNull() && mtb_Kit_Json_Range(c["pollSec"], 10, 300, v)){ portENTER_CRITICAL(&lock); save.pollSec = (uint16_t)v; portEXIT_CRITICAL(&lock); }
    else if (!c["sound"].isNull() && mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ portENTER_CRITICAL(&lock); save.sound = (uint8_t)v; portEXIT_CRITICAL(&lock); }
    else { mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}
static void cmd_New_Topic(JsonDocument&){
    char t[68];
    random_Topic(t, sizeof(t));
    portENTER_CRITICAL(&lock); snprintf(save.topic, sizeof(save.topic), "%s", t); save.lastId[0] = '\0'; portEXIT_CRITICAL(&lock);
    pollNow = true;
    phoneChanged = true;
}
static void cmd_Clear(JsonDocument&){
    portENTER_CRITICAL(&lock); save.count = 0; memset(save.msg, 0, sizeof(save.msg)); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob ------------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false, holdRequested = false;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) holdRequested = true;
    else if (b.type == BUTTON_CLICKED) clickRequested = true;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
static void add_Message(JsonVariantConst m){
    Mbd_Msg_t n = {};
    n.time = m["time"] | 0u;
    n.priority = (uint8_t)(m["priority"] | 3);
    if (n.priority < 1 || n.priority > 5) n.priority = 3;
    mtb_Kit_To_Ascii(n.title, sizeof(n.title), m["title"] | "");
    mtb_Kit_To_Ascii(n.body, sizeof(n.body), m["message"] | "");
    // Tags such as "warning" or "rotating_light" are emoji on phones; here they only raise the colour a little.
    JsonArrayConst tags = m["tags"];
    for (JsonVariantConst t : tags){
        const char* s = t | "";
        if (n.priority < 4 && (!strcmp(s, "warning") || !strcmp(s, "rotating_light") || !strcmp(s, "fire"))) n.priority = 4;
    }
    if (!n.body[0] && !n.title[0]) return;
    portENTER_CRITICAL(&lock);
    memmove(&save.msg[1], &save.msg[0], sizeof(Mbd_Msg_t) * (MSGS - 1));
    save.msg[0] = n;
    if (save.count < MSGS) save.count++;
    const char* id = m["id"] | "";
    snprintf(save.lastId, sizeof(save.lastId), "%s", id);
    if (n.priority > arrivedTop) arrivedTop = n.priority;
    arrived = arrived + 1;
    portEXIT_CRITICAL(&lock);
}

// An installed app has one thread: the worker is a step the compat layer runs at the app's waits
// (pxl_compat_add_background), about every 500 ms. Its state lives in these statics between steps.
static uint32_t next;
static int failures;
static uint32_t wk_StepAt = 0, wk_WakeAt = 0;
static void worker_Step(void);

static void worker_Begin(void){
    wk_StepAt = 0; wk_WakeAt = 0;
    next = 0;
    failures = 0;
}

static void worker_End(void){
    workerRunning = false;
    pxl_compat_remove_background(worker_Step);
}

static void worker_Step(void){
    if (!workerRunning) return;
    if (workerStop){ worker_End(); return; }
    if (millis() - wk_StepAt < 500) return;
    wk_StepAt = millis();
    if ((int32_t)(millis() - wk_WakeAt) < 0) return;
    do {
        if (!pollNow && millis() < next){ wk_WakeAt = millis() + 200; continue; }
        pollNow = false;
        char server[64], topic[68], token[72], since[24];
        uint16_t every;
        portENTER_CRITICAL(&lock);
        memcpy(server, save.server, sizeof(server)); memcpy(topic, save.topic, sizeof(topic));
        memcpy(token, save.token, sizeof(token)); memcpy(since, save.lastId, sizeof(since));
        every = save.pollSec;
        portEXIT_CRITICAL(&lock);
        char url[200];
        // The first poll of a topic: what came in the last 12 hours; after: what came after the newest seen.
        snprintf(url, sizeof(url), "%s/%s/json?poll=1&since=%s", server, topic, since[0] ? since : "12h");
        char auth[90];
        const char* headers[3] = {nullptr, nullptr, nullptr};
        if (token[0]){ snprintf(auth, sizeof(auth), "Bearer %s", token); headers[0] = "Authorization"; headers[1] = auth; }
        Mtb_Kit_Http_t r = {};
        char* body = mtb_Kit_Http_Get(url, token[0] ? headers : nullptr, nullptr, 0, 64 * 1024, r, UA);
        lastCode = r.code;
        lastPollAt = (uint32_t)time(nullptr);
        if (body && r.code == 200){
            failures = 0;
            // One JSON object a line, oldest first.
            char* line = body;
            while (line && *line && !workerStop){
                char* end = strchr(line, '\n');
                if (end) *end = '\0';
                if (*line == '{'){
                    JsonDocument doc(mtb_Kit_Json_Allocator());
                    if (!deserializeJson(doc, line)){
                        const char* ev = doc["event"] | "message";
                        if (!strcmp(ev, "message")) add_Message(doc.as<JsonVariantConst>());
                    }
                }
                line = end ? end + 1 : nullptr;
            }
        } else if (r.code != 200) failures++;
        heap_caps_free(body);
        // Failing: wait longer each time, up to 5 minutes.
        uint32_t wait = every * 1000u;
        if (failures) { wait <<= (failures > 4 ? 4 : failures); if (wait > 300000u) wait = 300000u; }
        next = millis() + wait;
    } while (0);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 6, 12), C_BAR = MTB_KIT_RGB(18, 18, 30);
static uint16_t prio_Color(uint8_t p){ return p >= 5 ? KIT_RED : p == 4 ? KIT_ORANGE : p <= 2 ? KIT_DIM : KIT_CYAN; }

static void draw(const Mbd_Save_t& v, int sel, uint32_t ms, bool alarm){
    mtb_Panel_Fill_Screen(C_BG);
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_BAR);
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_LABEL, "MESSAGES", C_BAR);
    char t[96];
    int unread = 0;
    for (int i = 0; i < v.count; i++) if (!v.msg[i].read) unread++;
    if (unread){ snprintf(t, sizeof(t), "%d NEW", unread); mtb_Kit_Text_Right(W - 2, 1, Terminal6x8, KIT_GOLD, t, C_BAR); }
    else {
        char fit[72];
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, v.topic, 150);
        mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, KIT_DIM, fit, C_BAR);
    }
    if (lastCode != 200 && lastCode != 0){
        const char* why = lastCode == 401 || lastCode == 403 ? "NOT ALLOWED: TOKEN?" : lastCode == 429 ? "TOO MANY ASKS" : lastCode < 0 ? "OFFLINE" : "SERVER PROBLEM";
        mtb_Kit_Text_Centre(W / 2, 2, Terminal4x6, KIT_ORANGE, why, C_BAR);
    }
    if (!v.count){
        mtb_Kit_Text_Centre(W / 2, 30, Terminal8x12, KIT_TEXT, "NO MESSAGES YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 52, Terminal4x6, KIT_LABEL, "SEND ONE TO THIS TOPIC:", C_BG);
        const char* host = strstr(v.server, "://");
        snprintf(t, sizeof(t), "%s/%s", host ? host + 3 : v.server, v.topic);
        char fit[96];
        mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 8);
        mtb_Kit_Text_Centre(W / 2, 62, Terminal6x8, KIT_CYAN, fit, C_BG);
        mtb_Kit_Text_Centre(W / 2, 80, Terminal4x6, KIT_DIM, "THE PHONE APP SHOWS HOW", C_BG);
        return;
    }
    const Mbd_Msg_t& m = v.msg[sel];
    const uint16_t pc = prio_Color(m.priority);
    const bool flash = alarm && (ms / 300) % 2;
    mtb_Panel_Fill_Rect(0, 11, 3, H - 1, flash ? WHITE : pc);
    if (m.priority >= 5 && !m.read) mtb_Panel_Draw_Rect(0, 10, W - 1, H - 1, flash ? WHITE : KIT_RED);
    int y = 13;
    if (m.title[0]){
        char fit[TITLE_LEN];
        mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, m.title, W - 12);
        mtb_Kit_Text(7, y, Terminal8x12, m.read ? KIT_TEXT : WHITE, fit, C_BG);
        y += 15;
    }
    // The text, wrapped; the last line ends in "..." if it does not fit.
    static char lines[10][64];
    const int maxLines = (H - 10 - y) / 10;
    const int n = mtb_Kit_Wrap(m.body, Terminal6x8, W - 12, &lines[0][0], sizeof(lines[0]), maxLines > 10 ? 10 : maxLines);
    for (int i = 0; i < n; i++){ mtb_Kit_Text(7, y, Terminal6x8, m.read ? KIT_LABEL : KIT_TEXT, lines[i], C_BG); y += 10; }
    // Where it is in the list, and when.
    snprintf(t, sizeof(t), "%d OF %d", sel + 1, v.count);
    mtb_Kit_Text(7, H - 7, Terminal4x6, KIT_DIM, t, C_BG);
    const uint32_t now = (uint32_t)time(nullptr);
    if (m.time && now >= m.time){
        char ago[20];
        mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(now - m.time));
        mtb_Kit_Upper(ago);
        mtb_Kit_Text_Right(W - 3, H - 7, Terminal4x6, KIT_DIM, ago, C_BG);
    }
    if (!m.read) mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_GOLD, "NEW - CLICK WHEN READ", C_BG);
}

// The app ----------------------------------------------------------------------------------------------------------------------------
static void messageBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Topic, cmd_Server, cmd_Token, cmd_Settings, cmd_New_Topic, cmd_Clear);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    write_Save();                       // a first topic made now is kept
    turns = 0; clickRequested = false; holdRequested = false; phoneChanged = false;
    workerStop = false; pollNow = true; lastCode = 0;
    workerRunning = true;
    worker_Begin();
    pxl_compat_add_background(worker_Step);
    Mbd_Save_t* view = (Mbd_Save_t*)heap_caps_malloc(sizeof(Mbd_Save_t), MALLOC_CAP_SPIRAM);   // a copy to draw from
    if (!view){ workerStop = true; mtb_Delete_This_App(THIS_APP); return; }
    appOpen = true;

    uint32_t seen = arrived, lastDraw = 0, pushedMs = 0, alarmUntil = 0, savedMs = 0;
    int sel = 0;
    bool redraw = true, push = true, dirty = false;
    int lastShownCode = 0;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = dirty = true; if (sel >= save.count) sel = 0; }
        if (arrived != seen){
            seen = arrived;
            const uint8_t top = arrivedTop;
            arrivedTop = 0;
            sel = 0;
            redraw = push = dirty = true;
            if (save.sound && !mtb_Silent_Mode()){
                static const Snd_Note_t NEW[] = {{988, 80}, {1319, 160}};
                static const Snd_Note_t URGENT[] = {{1319, 120}, {0, 60}, {1319, 120}, {0, 60}, {1319, 120}, {0, 200}, {1319, 120}, {0, 60}, {1319, 120}, {0, 60}, {1319, 240}};
                if (top >= 5) snd_Play(URGENT, 11, 0.4f, true); else snd_Play(NEW, 2, 0.3f, false);
            }
            if (top >= 5) alarmUntil = ms + 30000;
        }
        const int t = turns;
        if (t){ turns -= t; if (save.count){ sel = (sel + (t > 0 ? 1 : -1) + save.count) % save.count; } redraw = true; }
        if (clickRequested){
            clickRequested = false;
            portENTER_CRITICAL(&lock); for (int i = 0; i < save.count; i++) save.msg[i].read = 1; portEXIT_CRITICAL(&lock);
            alarmUntil = 0;
            redraw = push = dirty = true;
        }
        if (holdRequested){
            holdRequested = false;
            portENTER_CRITICAL(&lock); save.count = 0; memset(save.msg, 0, sizeof(save.msg)); portEXIT_CRITICAL(&lock);
            sel = 0; alarmUntil = 0;
            redraw = push = dirty = true;
        }
        if (lastCode != lastShownCode){ lastShownCode = lastCode; redraw = push = true; }
        const bool alarm = ms < alarmUntil;
        if (redraw || ms - lastDraw >= (alarm ? 150u : 30000u)){
            redraw = false;
            lastDraw = ms;
            portENTER_CRITICAL(&lock); *view = save; portEXIT_CRITICAL(&lock);
            if (sel >= view->count) sel = 0;
            mtb_Panel_Begin_Update();
            draw(*view, sel, ms, alarm);
            mtb_Panel_End_Update();
        }
        if (dirty && ms - savedMs >= 3000){ dirty = false; savedMs = ms; write_Save(); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    workerStop = true;
    for (int i = 0; i < 400 && workerRunning; i++) vTaskDelay(pdMS_TO_TICKS(50));
    write_Save();
    heap_caps_free(view);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ----------------------------------------------------------------------------------------------
static void mbd_Values(JsonObject values){
    Mbd_Save_t* s = (Mbd_Save_t*)heap_caps_malloc(sizeof(Mbd_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(*s);
    char t[200];
    JsonArray now = values["now"].to<JsonArray>();
    JsonObject o = now.add<JsonObject>();
    o["label"] = "Following";
    snprintf(t, sizeof(t), "%s/%s", s->server, s->topic);
    o["value"] = t;
    o["state"] = "info";
    JsonObject p = now.add<JsonObject>();
    p["label"] = "Last look";
    const int code = lastCode;
    if (!appOpen){ p["value"] = "Messages arrive while Message Board is open"; p["state"] = "off"; }
    else if (code == 200){ p["value"] = "Fine"; p["state"] = "ok"; if (lastPollAt) p["ago"] = "lastPollAt"; }
    else if (code == 0){ p["value"] = "Looking..."; p["state"] = "info"; }
    else if (code == 401 || code == 403){ p["value"] = "Not allowed: this topic needs an access token (or the token is wrong)"; p["state"] = "error"; }
    else if (code == 429){ p["value"] = "The server says too many requests: look less often"; p["state"] = "warn"; }
    else if (code < 0){ p["value"] = "No answer: is Pixlpal online, and is the server right?"; p["state"] = "error"; }
    else { snprintf(t, sizeof(t), "The server answered %d", code); p["value"] = t; p["state"] = "error"; }
    values["lastPollAt"] = (uint32_t)lastPollAt;

    JsonArray how = values["howto"].to<JsonArray>();
    JsonObject h = how.add<JsonObject>();
    h["label"] = "From a computer";
    snprintf(t, sizeof(t), "curl -d \"Hello Pixlpal\" %s/%s", s->server, s->topic);
    h["value"] = t;
    h["state"] = "info";
    JsonObject h2 = how.add<JsonObject>();
    h2["label"] = "Urgent, with a title";
    snprintf(t, sizeof(t), "curl -H \"Title: Alarm\" -H \"Priority: 5\" -d \"The freezer is warm\" %s/%s", s->server, s->topic);
    h2["value"] = t;
    h2["state"] = "warn";

    JsonArray msgs = values["messages"].to<JsonArray>();
    const uint32_t nowS = (uint32_t)time(nullptr);
    for (int i = 0; i < s->count; i++){
        const Mbd_Msg_t& m = s->msg[i];
        JsonObject r = msgs.add<JsonObject>();
        char ago[20] = "";
        if (m.time && nowS >= m.time) mtb_Kit_Ago(ago, sizeof(ago), (int32_t)(nowS - m.time));
        snprintf(t, sizeof(t), "%s%s%s", m.title[0] ? m.title : "Message", ago[0] ? ", " : "", ago);
        r["label"] = t;
        r["value"] = m.body;
        r["state"] = m.priority >= 5 ? "error" : m.priority == 4 ? "warn" : m.read ? "off" : "info";
    }
    values["topic"] = s->topic;
    values["server"] = s->server;
    values["token_set"] = s->token[0] != '\0';
    values["pollSec"] = s->pollSec;
    values["sound"] = s->sound;
    heap_caps_free(s);
}
