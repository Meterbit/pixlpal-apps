/**
 * 3D Printer Status: a printer on the owner's network, OctoPrint (GET /api/job and /api/printer, with its key) or
 * Klipper through Moonraker (GET /printer/objects/query, and the slicer's estimate from /server/files/metadata). The
 * screen: the file, how far along (a big percentage and a bar), the time left and when it will be done, the time
 * printing, the layer, the nozzle and bed temperatures against their targets; a chime when a print finishes, fails
 * or pauses. The address is a site its owner allows ("ownerNet"). Every 5 seconds while printing, 15 otherwise.
 * NVS "prnSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "prn_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "prnSave"
#define SAVE_MAGIC  0x50524E01u
#define UA          "Pixlpal-PrinterStatus/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t printerStatus_Task_H = NULL;
static void printerStatus_App_Task(void* dApplication);
static void prn_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* printerStatus_App;
Mtb_Applications* printerStatus_App_GetInstance(){
    if (!printerStatus_App){
        printerStatus_App = new Mtb_Applications_FullScreen(printerStatus_App_Task, &printerStatus_Task_H, "3D Printer Status", {0,0}, 8192);
        printerStatus_App->mtb_App_Set_Mobile_UI(PRN_UI_MANIFEST, PRN_UI_API, nullptr, prn_Values);
    }
    return printerStatus_App;
}
MTB_REGISTER_APP(printerStatus_App, 0, 0)       // the catalogue gives the app its number

struct Prn_Save_t { uint32_t magic; uint8_t kind, sound, pad[2]; char address[96]; char key[72]; char name[28]; };

enum Prn_State_e { P_UNKNOWN = 0, P_OFFLINE, P_IDLE, P_PRINTING, P_PAUSED, P_DONE, P_CANCELLED, P_ERROR };
static const char* const STATE_TEXT[] = {"", "NO ANSWER", "IDLE", "PRINTING", "PAUSED", "DONE", "CANCELLED", "ERROR"};

struct Prn_Status_t {
    uint8_t state;
    char file[64];
    float progress;                     // 0-1, -1 unknown
    int32_t printSecs, leftSecs;        // -1 unknown
    float nozzle, nozzleTarget, bed, bedTarget;
    int layer, layers;                  // 0 unknown
    char message[48];                   // the printer's own words, for an error
};

struct Prn_Shared_t {
    Prn_Status_t st;
    bool known;
    uint32_t atMs;
    char why[48];
    uint32_t version;
    volatile bool stop, running, again;
};

static EXT_RAM_BSS_ATTR Prn_Save_t save;
static EXT_RAM_BSS_ATTR Prn_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false;

static void read_Save(Prn_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.sound = 1; }
    s.kind = s.kind ? 1 : 0;
    s.sound = s.sound ? 1 : 0;
    s.address[sizeof(s.address) - 1] = s.key[sizeof(s.key) - 1] = s.name[sizeof(s.name) - 1] = '\0';
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static void changed(void){ sh.again = true; phoneChanged = saveWanted = true; }
static void cmd_Kind(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["kind"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.kind = (uint8_t)v; portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Address(JsonDocument& c){
    const char* v = c["address"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n && (v[n - 1] == '/' || v[n - 1] == ' ')) n--;
    const bool scheme = !strncasecmp(v, "http://", 7) || !strncasecmp(v, "https://", 8);
    if ((n && !scheme) || n >= sizeof(save.address) || (n && n < 10)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (size_t i = 0; i < n; i++) if (v[i] <= ' ' || v[i] == '"' || v[i] == '\\'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.address, sizeof(save.address), "%.*s", (int)n, v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Key(JsonDocument& c){
    const char* v = c["apiKey"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.key)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!(isalnum((unsigned char)*k) || *k == '-' || *k == '_' || *k == '.')){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.key, sizeof(save.key), "%s", v); portEXIT_CRITICAL(&lock);
    changed();
}
static void cmd_Name(JsonDocument& c){
    const char* v = c["name"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); mtb_Kit_To_Ascii(save.name, sizeof(save.name), v); portEXIT_CRITICAL(&lock);
    phoneChanged = saveWanted = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = saveWanted = true;
}

// The worker -------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, const char* key, JsonDocument& doc, JsonDocument* filter){
    const char* withKey[] = {"Accept", "application/json", "X-Api-Key", key, nullptr};
    const char* plain[] = {"Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, key && key[0] ? withKey : plain, nullptr, 0, 24000, r, UA);
    int code = r.code;
    if (code == 200 && body){
        const DeserializationError e = filter ? deserializeJson(doc, body, r.length, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body, r.length);
        if (e) code = -100;                 // not JSON (not PXL_HTTP_NOT_ALLOWED, -2)
    } else if (code == 409 && body){
        // OctoPrint: "Printer is not operational" when it isn't connected to the printer.
        doc.clear();
        doc["error"] = "409";
    }
    heap_caps_free(body);
    return code;
}

static void file_Name(char* out, size_t n, const char* path){
    const char* slash = strrchr(path, '/');
    char t[96];
    mtb_Kit_To_Ascii(t, sizeof(t), slash ? slash + 1 : path);
    char* dot = strrchr(t, '.');
    if (dot && (!strcasecmp(dot, ".gcode") || !strcasecmp(dot, ".bgcode") || !strcasecmp(dot, ".gco") || !strcasecmp(dot, ".g"))) *dot = '\0';
    for (char* p = t; *p; p++) if (*p == '_') *p = ' ';
    snprintf(out, n, "%s", t);
}

static int fetch_Octoprint(const Prn_Save_t& s, Prn_Status_t& o){
    char url[160];
    JsonDocument doc;
    snprintf(url, sizeof(url), "%s/api/job", s.address);
    int code = get_Json(url, s.key, doc, nullptr);
    if (code != 200) return code;
    const char* state = doc["state"] | "";
    file_Name(o.file, sizeof(o.file), doc["job"]["file"]["display"] | (doc["job"]["file"]["name"] | ""));
    o.progress = doc["progress"]["completion"].is<float>() ? doc["progress"]["completion"].as<float>() / 100.0f : -1;
    o.printSecs = doc["progress"]["printTime"].is<int>() ? doc["progress"]["printTime"].as<int32_t>() : -1;
    o.leftSecs = doc["progress"]["printTimeLeft"].is<int>() ? doc["progress"]["printTimeLeft"].as<int32_t>() : -1;
    if (!strncmp(state, "Printing", 8) || !strcmp(state, "Starting") || !strcmp(state, "Resuming") || !strcmp(state, "Finishing")) o.state = P_PRINTING;
    else if (!strncmp(state, "Paus", 4)) o.state = P_PAUSED;
    else if (!strncmp(state, "Cancel", 6)) o.state = P_CANCELLED;
    else if (!strncmp(state, "Offline", 7) || !strncmp(state, "Error", 5) || strstr(state, "rror")){ o.state = !strncmp(state, "Offline", 7) ? P_OFFLINE : P_ERROR; mtb_Kit_To_Ascii(o.message, sizeof(o.message), state); }
    else o.state = (o.progress >= 0.999f) ? P_DONE : P_IDLE;
    snprintf(url, sizeof(url), "%s/api/printer?exclude=sd,state", s.address);
    JsonDocument p, filter;
    filter["temperature"]["tool0"] = true; filter["temperature"]["bed"] = true;
    code = get_Json(url, s.key, p, &filter);
    if (code == 200){
        o.nozzle = p["temperature"]["tool0"]["actual"] | NAN; o.nozzleTarget = p["temperature"]["tool0"]["target"] | 0.0f;
        o.bed = p["temperature"]["bed"]["actual"] | NAN; o.bedTarget = p["temperature"]["bed"]["target"] | 0.0f;
    } else if (code == 409){ o.nozzle = o.bed = NAN; if (o.state != P_ERROR) o.state = P_OFFLINE; snprintf(o.message, sizeof(o.message), "OctoPrint is not connected to it"); }
    else return code;
    return 200;
}

static int fetch_Moonraker(const Prn_Save_t& s, Prn_Status_t& o, char* lastFile, size_t lastSize, int32_t& estimate){
    char url[200];
    JsonDocument doc;
    snprintf(url, sizeof(url), "%s/printer/objects/query?print_stats&display_status&virtual_sdcard&extruder&heater_bed&webhooks", s.address);
    const int code = get_Json(url, s.key, doc, nullptr);
    if (code != 200) return code;
    JsonVariantConst st = doc["result"]["status"];
    const char* state = st["print_stats"]["state"] | "";
    const char* path = st["print_stats"]["filename"] | "";
    file_Name(o.file, sizeof(o.file), path);
    float progress = st["virtual_sdcard"]["progress"] | -1.0f;
    if (st["display_status"]["progress"].is<float>() && st["display_status"]["progress"].as<float>() > 0) progress = st["display_status"]["progress"].as<float>();
    o.progress = progress;
    o.printSecs = st["print_stats"]["print_duration"].is<float>() ? (int32_t)st["print_stats"]["print_duration"].as<float>() : -1;
    o.layer = st["print_stats"]["info"]["current_layer"] | 0;
    o.layers = st["print_stats"]["info"]["total_layer"] | 0;
    o.nozzle = st["extruder"]["temperature"] | NAN; o.nozzleTarget = st["extruder"]["target"] | 0.0f;
    o.bed = st["heater_bed"]["temperature"] | NAN; o.bedTarget = st["heater_bed"]["target"] | 0.0f;
    const char* klippy = st["webhooks"]["state"] | "ready";
    if (strcmp(klippy, "ready")){ o.state = P_ERROR; mtb_Kit_To_Ascii(o.message, sizeof(o.message), st["webhooks"]["state_message"] | klippy); }
    else if (!strcmp(state, "printing")) o.state = P_PRINTING;
    else if (!strcmp(state, "paused")) o.state = P_PAUSED;
    else if (!strcmp(state, "complete")) o.state = P_DONE;
    else if (!strcmp(state, "cancelled")) o.state = P_CANCELLED;
    else if (!strcmp(state, "error")){ o.state = P_ERROR; mtb_Kit_To_Ascii(o.message, sizeof(o.message), st["print_stats"]["message"] | "error"); }
    else o.state = P_IDLE;
    // The slicer's estimate for the file, once a file.
    if (path[0] && strcmp(path, lastFile)){
        snprintf(lastFile, lastSize, "%s", path);
        estimate = -1;
        char enc[128];
        mtb_Kit_Url_Encode(enc, sizeof(enc), path);
        snprintf(url, sizeof(url), "%s/server/files/metadata?filename=%s", s.address, enc);
        JsonDocument m, filter;
        filter["result"]["estimated_time"] = true;
        if (get_Json(url, s.key, m, &filter) == 200 && m["result"]["estimated_time"].is<float>()) estimate = (int32_t)m["result"]["estimated_time"].as<float>();
    }
    // Time left: the slicer's estimate scaled by how the print is going, or from the progress alone.
    o.leftSecs = -1;
    if (o.state == P_PRINTING || o.state == P_PAUSED){
        if (o.progress > 0.02f && o.printSecs > 0){
            const float fromProgress = o.printSecs / o.progress - o.printSecs;
            if (estimate > 0){
                const float fromSlicer = estimate * (1 - o.progress);
                o.leftSecs = (int32_t)(o.progress < 0.3f ? fromSlicer * (1 - o.progress / 0.3f) + fromProgress * (o.progress / 0.3f) : fromProgress);
            } else o.leftSecs = (int32_t)fromProgress;
        } else if (estimate > 0) o.leftSecs = estimate - (o.printSecs > 0 ? o.printSecs : 0);
        if (o.leftSecs < 0) o.leftSecs = 0;
    }
    return 200;
}

static void worker(void*){
    char lastFile[96] = "";
    int32_t estimate = -1;
    uint32_t nextMs = 0;
    while (!sh.stop){
        if (sh.again){ sh.again = false; nextMs = 0; lastFile[0] = '\0'; }
        if (nextMs && (int32_t)(millis() - nextMs) < 0){ vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        Prn_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        if (!s.address[0]){ nextMs = millis() + 1000; vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        Prn_Status_t o = {};
        o.progress = -1; o.printSecs = o.leftSecs = -1; o.nozzle = o.bed = NAN;
        const int code = s.kind ? fetch_Moonraker(s, o, lastFile, sizeof(lastFile), estimate) : fetch_Octoprint(s, o);
        portENTER_CRITICAL(&lock);
        if (code == 200){ sh.st = o; sh.known = true; sh.atMs = millis(); sh.why[0] = '\0'; }
        else {
            if (code == PXL_HTTP_NOT_ALLOWED) snprintf(sh.why, sizeof(sh.why), "ALLOW THE ADDRESS IN THE PHONE APP");
            else if (code == 401 || code == 403) snprintf(sh.why, sizeof(sh.why), s.kind ? "MOONRAKER WANTS AN API KEY" : "OCTOPRINT REFUSED THE KEY");
            else if (code == 404) snprintf(sh.why, sizeof(sh.why), s.kind ? "NO MOONRAKER AT THAT ADDRESS" : "NO OCTOPRINT AT THAT ADDRESS");
            else if (code == -100) snprintf(sh.why, sizeof(sh.why), "THAT ADDRESS DID NOT ANSWER AS A PRINTER");
            else if (code <= 0) snprintf(sh.why, sizeof(sh.why), "NO ANSWER FROM THE PRINTER");
            else snprintf(sh.why, sizeof(sh.why), "THE PRINTER: HTTP %d", code);
            if (sh.known && code <= 0 && code != PXL_HTTP_NOT_ALLOWED) sh.st.state = P_OFFLINE;
        }
        sh.version++;
        portEXIT_CRITICAL(&lock);
        nextMs = millis() + (code == 200 && (o.state == P_PRINTING || o.state == P_PAUSED) ? 5000u : 15000u);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(8, 8, 10), C_BAR = MTB_KIT_RGB(24, 24, 30);

static uint16_t state_Color(uint8_t s){
    switch (s){
        case P_PRINTING: return KIT_CYAN;
        case P_PAUSED: return KIT_GOLD;
        case P_DONE: return KIT_GREEN;
        case P_ERROR: return KIT_RED;
        case P_CANCELLED: return KIT_ORANGE;
        default: return KIT_LABEL;
    }
}

static void duration(char* t, size_t n, int32_t s){
    if (s < 0){ snprintf(t, n, "--"); return; }
    if (s >= 3600) snprintf(t, n, "%ld:%02ld:%02ld", (long)(s / 3600), (long)(s / 60 % 60), (long)(s % 60));
    else snprintf(t, n, "%ld:%02ld", (long)(s / 60), (long)(s % 60));
}

static void temperature(int x, int y, const char* label, float now, float target){
    char t[24];
    mtb_Kit_Text(x, y, Terminal4x6, KIT_LABEL, label, C_BG);
    if (isnan(now)){ mtb_Kit_Text(x, y + 8, Terminal8x12, KIT_DIM, "--", C_BG); return; }
    const bool hot = now >= 50, heating = target > 0 && now < target - 3;
    const uint16_t c = target <= 0 ? (hot ? KIT_ORANGE : KIT_LABEL) : heating ? KIT_GOLD : KIT_RED;
    snprintf(t, sizeof(t), "%d", (int)lroundf(now));
    const int x2 = mtb_Kit_Text(x, y + 8, Terminal8x12, c, t, C_BG);
    if (target > 0){ snprintf(t, sizeof(t), "/%d", (int)lroundf(target)); mtb_Kit_Text(x2 + 1, y + 13, Terminal4x6, KIT_LABEL, t, C_BG); }
    else mtb_Kit_Text(x2 + 1, y + 13, Terminal4x6, KIT_DIM, "OFF", C_BG);
    // A small bar, how near the target.
    const float f = target > 0 ? now / target : now / 250.0f;
    mtb_Kit_Bar(x, y + 22, 54, 2, f > 1 ? 1 : f < 0 ? 0 : f, c);
}

static void draw(const Prn_Save_t& s, const Prn_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[96], f[96];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_BAR);
    snprintf(t, sizeof(t), "%s", s.name[0] ? s.name : (s.kind ? "KLIPPER" : "OCTOPRINT"));
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, 160);
    mtb_Kit_Text(3, 1, Terminal6x8, KIT_TEXT, f, C_BAR);
    if (!s.address[0]){
        mtb_Kit_Text_Centre(W / 2, 48, Terminal6x8, KIT_LABEL, "ADD YOUR PRINTER'S ADDRESS", C_BG);
        mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, KIT_LABEL, "IN THE PHONE APP", C_BG);
        return;
    }
    if (!d.known){
        mtb_Kit_Text_Centre(W / 2, 56, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "ASKING THE PRINTER...", C_BG);
        return;
    }
    const Prn_Status_t& p = d.st;
    const uint16_t sc = state_Color(p.state);
    const char* state = STATE_TEXT[p.state < 8 ? p.state : 0];
    const int sw = mtb_Kit_Text_Width(Terminal6x8, state);
    const bool blink = (p.state == P_PAUSED || p.state == P_ERROR) && (ms / 500) % 2;
    mtb_Panel_Fill_Rect((int16_t)(W - sw - 6), 0, W - 1, 9, blink ? C_BAR : sc);
    mtb_Kit_Text_Right(W - 3, 1, Terminal6x8, blink ? sc : C_BG, state, blink ? C_BAR : sc);
    // The file.
    if (p.file[0]){ mtb_Kit_Fit(f, sizeof(f), Terminal6x8, p.file, W - 8); mtb_Kit_Text(3, 13, Terminal6x8, KIT_LABEL, f, C_BG); }
    const bool active = p.state == P_PRINTING || p.state == P_PAUSED;
    if (active || p.state == P_DONE || p.state == P_CANCELLED){
        // How far along.
        const float pr = p.progress < 0 ? 0 : p.progress > 1 ? 1 : p.progress;
        if (p.state == P_DONE) mtb_Kit_Text(3, 25, PT_Sans_Narrow_Bold28x34, KIT_GREEN, "DONE", C_BG);
        else {
            snprintf(t, sizeof(t), p.progress < 0 ? "--%%" : "%d%%", (int)(pr * 100));
            mtb_Kit_Text(3, 25, PT_Sans_Narrow_Bold28x34, sc, t, C_BG);
        }
        mtb_Kit_Bar(3, 62, W - 6, 6, p.state == P_DONE ? 1.0f : pr, sc, KIT_LINE);
        // The times, on the right.
        const int x = 128;
        if (active){
            mtb_Kit_Text(x, 24, Terminal4x6, KIT_LABEL, "LEFT", C_BG);
            duration(t, sizeof(t), p.leftSecs);
            mtb_Kit_Text(x, 31, Terminal8x12, KIT_TEXT, t, C_BG);
            if (p.leftSecs >= 0 && p.state == P_PRINTING){
                const time_t done = time(nullptr) + p.leftSecs;
                struct tm l;
                localtime_r(&done, &l);
                if (mtb_Clock_24h()) snprintf(t, sizeof(t), "DONE AT %02d:%02d", l.tm_hour, l.tm_min);
                else snprintf(t, sizeof(t), "DONE AT %d:%02d %s", (l.tm_hour + 11) % 12 + 1, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
                mtb_Kit_Text(x, 46, Terminal6x8, KIT_GOLD, t, C_BG);
            }
        }
        mtb_Kit_Text(x + 76, 24, Terminal4x6, KIT_LABEL, active ? "PRINTING" : "TOOK", C_BG);
        duration(t, sizeof(t), p.printSecs);
        mtb_Kit_Text(x + 76, active ? 33 : 31, active ? Terminal6x8 : Terminal8x12, KIT_LABEL, t, C_BG);
    } else if (p.state == P_ERROR || p.state == P_OFFLINE){
        mtb_Kit_Text(3, 28, PT_Sans_Narrow_Bold18x22, sc, p.state == P_ERROR ? "PRINTER ERROR" : "NOT CONNECTED", C_BG);
        const char* m = p.message[0] ? p.message : d.why;
        if (m[0]){ snprintf(t, sizeof(t), "%s", m); mtb_Kit_Upper(t); mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, W - 8); mtb_Kit_Text(3, 54, Terminal6x8, KIT_LABEL, f, C_BG); }
    } else {
        mtb_Kit_Text(3, 28, PT_Sans_Narrow_Bold18x22, KIT_LABEL, "READY", C_BG);
        mtb_Kit_Text(3, 54, Terminal6x8, KIT_DIM, "NOTHING PRINTING", C_BG);
    }
    // The bottom: temperatures and the layer.
    mtb_Panel_Draw_HLine(76, 0, W - 1, KIT_LINE);
    temperature(6, 82, "NOZZLE C", p.nozzle, p.nozzleTarget);
    temperature(76, 82, "BED C", p.bed, p.bedTarget);
    if (p.layers > 0 || p.layer > 0){
        mtb_Kit_Text(150, 82, Terminal4x6, KIT_LABEL, "LAYER", C_BG);
        snprintf(t, sizeof(t), "%d", p.layer);
        const int x2 = mtb_Kit_Text(150, 90, Terminal8x12, KIT_TEXT, t, C_BG);
        if (p.layers > 0){ snprintf(t, sizeof(t), "/%d", p.layers); mtb_Kit_Text(x2 + 1, 95, Terminal4x6, KIT_LABEL, t, C_BG); }
    }
    if (d.why[0]){ mtb_Kit_Fit(f, sizeof(f), Terminal4x6, d.why, W - 8); mtb_Kit_Text(3, H - 8, Terminal4x6, KIT_ORANGE, f, C_BG); }
    else if (ms - d.atMs > 60000u){ mtb_Kit_Text(3, H - 8, Terminal4x6, KIT_ORANGE, "NOT HEARD FROM IT FOR OVER A MINUTE", C_BG); }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void printerStatus_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(buttonDoNothing, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Kind, cmd_Address, cmd_Key, cmd_Name, cmd_Sound);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    phoneChanged = saveWanted = false;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "prnNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Prn_Shared_t* d = (Prn_Shared_t*)heap_caps_calloc(1, sizeof(Prn_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0;
    uint8_t lastState = P_UNKNOWN;
    bool redraw = true, push = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        // A chime when a print ends, fails or pauses (not when the app first sees it so).
        if (d->known && d->st.state != lastState){
            const uint8_t now = d->st.state;
            if (save.sound && (lastState == P_PRINTING || lastState == P_PAUSED)){
                static const Snd_Note_t DONE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 300}};
                static const Snd_Note_t STOP[] = {{659, 200}, {523, 200}, {392, 400}};
                static const Snd_Note_t PAUSE[] = {{880, 150}, {0, 100}, {880, 150}};
                if (now == P_DONE) snd_Play(DONE, 4, 0.3f, false);
                else if (now == P_ERROR || now == P_CANCELLED) snd_Play(STOP, 3, 0.3f, false);
                else if (now == P_PAUSED) snd_Play(PAUSE, 3, 0.3f, false);
            }
            lastState = now;
        }
        if (redraw || ms - lastDraw >= 500u){
            redraw = false;
            lastDraw = ms;
            Prn_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            draw(s, *d, ms);
        }
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if ((push && ms - pushedMs >= 3000) || ms - pushedMs >= 30000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void prn_Values(JsonObject values){
    Prn_Save_t s;
    Prn_Shared_t* d = (Prn_Shared_t*)heap_caps_calloc(1, sizeof(Prn_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; memcpy(d, &sh, sizeof(sh)); portEXIT_CRITICAL(&lock); } else read_Save(s);
    char t[128], a[24], b[24];
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject o = rows.add<JsonObject>();
    o["label"] = s.kind ? "Klipper" : "OctoPrint";
    o["value"] = s.address[0] ? s.address : "No address yet";
    o["state"] = s.address[0] ? "info" : "warn";
    if (!s.kind && !s.key[0]){ o = rows.add<JsonObject>(); o["label"] = "API key"; o["value"] = "None yet: OctoPrint needs one"; o["state"] = "warn"; }
    if (appOpen && s.address[0]){
        o = rows.add<JsonObject>();
        o["label"] = "Now";
        if (!d->known){ o["value"] = d->why[0] ? d->why : "Asking the printer"; o["state"] = d->why[0] ? "warn" : "info"; }
        else {
            const Prn_Status_t& p = d->st;
            if (p.state == P_PRINTING || p.state == P_PAUSED){
                duration(a, sizeof(a), p.leftSecs);
                snprintf(t, sizeof(t), "%s %d%%, %s left%s%s", p.state == P_PAUSED ? "Paused at" : "Printing,", (int)((p.progress < 0 ? 0 : p.progress) * 100), a,
                         p.file[0] ? ": " : "", p.file);
            } else if (p.state == P_ERROR || p.state == P_OFFLINE) snprintf(t, sizeof(t), "%s%s%s", p.state == P_ERROR ? "Error" : "Not connected", p.message[0] ? ": " : "", p.message);
            else snprintf(t, sizeof(t), "%s%s%s", p.state == P_DONE ? "Done" : p.state == P_CANCELLED ? "Cancelled" : "Idle", p.file[0] ? ": " : "", p.file);
            o["value"] = t;
            o["state"] = p.state == P_PRINTING || p.state == P_DONE ? "ok" : p.state == P_ERROR || p.state == P_OFFLINE ? "error" : p.state == P_PAUSED ? "warn" : "info";
            if (!isnan(p.nozzle)){
                o = rows.add<JsonObject>();
                o["label"] = "Temperatures";
                snprintf(a, sizeof(a), "%d C", (int)lroundf(p.nozzle));
                snprintf(b, sizeof(b), "%d C", isnan(p.bed) ? 0 : (int)lroundf(p.bed));
                snprintf(t, sizeof(t), "Nozzle %s, bed %s", a, b);
                o["value"] = t; o["state"] = "info";
            }
        }
    }
    values["kind"] = s.kind;
    values["address"] = s.address;
    values["name"] = s.name;
    values["apiKey_set"] = s.key[0] ? 1 : 0;
    values["sound"] = s.sound;
    heap_caps_free(d);
}
