/**
 * My First Computer, ID {19,8} (KIDS, ages 4-12): a pretend computer on the whole panel, to learn the mouse, the
 * keyboard, windows, files and staying safe online. The children are Kids Learning's; so are the daily time limit
 * and bedtime. Nothing goes online, and the only files are the child's own pictures and writing.
 *
 * A USB mouse and keyboard work it as on a real computer. The knob works too: turn to go from thing to thing, click
 * to open, double click to go back. Holding the knob asks for the parent code: with it a grown-up can leave the
 * app, lock the computer or sign the child out. App cycling never switches away from it (catalogue: cycle false).
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_usb_hid.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "kid_Model.h"
#include "mfc_App.h"
#include "mfc_Files.h"
#include "mfc_Mobile_Ui.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

#define ROUTE           "19/8"
#define PIECE           3072            // a multiple of 3: each piece is whole base64
#define FRAME_MS        33
#define SAVE_EVERY_MS   5000

EXT_RAM_BSS_ATTR TaskHandle_t myFirstComputer_Task_H = NULL;
static void myFirstComputer_App_Task(void* dApplication);
static void mfc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* myFirstComputer_App;
Mtb_Applications* myFirstComputer_App_GetInstance(){
    if (!myFirstComputer_App){
        myFirstComputer_App = new Mtb_Applications_FullScreen(myFirstComputer_App_Task, &myFirstComputer_Task_H, "My First Computer", {19,8}, 10240);
        myFirstComputer_App->mtb_App_Set_Mobile_UI(MFC_UI_MANIFEST, MFC_UI_API, nullptr, mfc_Values);
    }
    return myFirstComputer_App;
}
MTB_REGISTER_APP(myFirstComputer_App, 19, 8)

// What the app and the phone share (the phone's commands run on the BLE task) ---------------------------------------------
EXT_RAM_BSS_ATTR static Mfc_Settings_t settings;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
// Requests from the phone, done by the app task.
enum : uint32_t { R_EXTRA = 1, R_SIGN_IN = 2, R_FORGET_SECRET = 4, R_RESET = 8, R_ERASE_FILES = 16, R_LEAVE = 32, R_SEND = 64, R_DELETE = 128 };
static volatile uint32_t requests = 0;
static uint8_t requestChild = 0;
static char requestFile[24];
// Live facts for the phone's page.
static volatile int8_t nowChild = -1;
static volatile uint8_t nowProgram = MFC_PROGRAMS, nowMode = MFC_M_BOOT;

static void read_Settings(Mfc_Settings_t& s){
    mfc_Settings_Default(s);
    Mfc_Settings_t* saved = (Mfc_Settings_t*)heap_caps_calloc(1, sizeof(Mfc_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return;
    mtb_Read_Nvs_Struct(MFC_NVS_KEY, saved, sizeof(*saved));
    if (saved->magic == MFC_SETTINGS_MAGIC) s = *saved;
    heap_caps_free(saved);
    mfc_Settings_Clamp(s);
}

static void request(uint32_t r, uint8_t child){
    portENTER_CRITICAL(&lock);
    requestChild = child;
    requests |= r;
    portEXIT_CRITICAL(&lock);
}

static uint8_t editing(void){ return settings.editing % MFC_CHILDREN; }

// Phone commands (the BLE task) -------------------------------------------------------------------------------------------
static void cmd_Slot(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["childSlot"], 0, MFC_CHILDREN - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); settings.editing = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

static void cmd_Child(JsonDocument& c){
    long v;
    const uint8_t e = editing();
    if (!c["level"].isNull() && mtb_Kit_Json_Range(c["level"], 0, 3, v)){ portENTER_CRITICAL(&lock); settings.c[e].levelMode = (uint8_t)v; portEXIT_CRITICAL(&lock); }
    else if (!c["oneClick"].isNull() && mtb_Kit_Json_Range(c["oneClick"], 0, 2, v)){ portENTER_CRITICAL(&lock); settings.c[e].oneClick = (uint8_t)v; portEXIT_CRITICAL(&lock); }
    else { mtb_Ble_App_Cmd_Reject(); return; }
    phoneChanged = true;
}

static const char* const PROG_ARG[] = {"progMouse", "progKeys", "progPaint", "progWrite", "progFiles", "progCalc", "progPiano", "progWeb"};
static const uint8_t PROG_OF[] = {MFC_P_MOUSE, MFC_P_KEYS, MFC_P_PAINT, MFC_P_WRITE, MFC_P_FILES, MFC_P_CALC, MFC_P_PIANO, MFC_P_WEB};

static void cmd_Programs(JsonDocument& c){
    long v;
    for (size_t i = 0; i < sizeof(PROG_OF); i++){
        if (c[PROG_ARG[i]].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[PROG_ARG[i]], 0, 1, v)) break;
        portENTER_CRITICAL(&lock);
        uint16_t bits = (uint16_t)(1u << PROG_OF[i]);
        if (PROG_OF[i] == MFC_P_FILES) bits |= (uint16_t)(1u << MFC_P_BIN);
        if (v) settings.programs |= bits; else settings.programs &= (uint16_t)~bits;
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}

static void cmd_Code(JsonDocument& c){
    const char* code = c["code"] | "";
    if (!mfc_Code_Valid(code)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    memcpy(settings.code, code, MFC_CODE_LEN);
    settings.code[MFC_CODE_LEN] = '\0';
    settings.codeSet = 1;
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

static void cmd_Locked(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["locked"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); settings.locked = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

static void cmd_Leave(JsonDocument&){ request(R_LEAVE, 0); }

static bool file_Arg(JsonDocument& c){
    const char* f = c["file"] | "";
    // Only "NAME.png" or "NAME.txt", made of the characters our names use.
    const size_t n = strlen(f);
    if (n < 5 || n >= sizeof(requestFile)) return false;
    if (strcmp(f + n - 4, ".png") && strcmp(f + n - 4, ".txt")) return false;
    for (size_t i = 0; i + 4 < n; i++){
        const char ch = f[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    }
    portENTER_CRITICAL(&lock);
    snprintf(requestFile, sizeof(requestFile), "%s", f);
    portEXIT_CRITICAL(&lock);
    return true;
}

static void cmd_Send(JsonDocument& c){ if (!file_Arg(c) || strstr(requestFile, ".txt")){ mtb_Ble_App_Cmd_Reject(); return; } request(R_SEND, editing()); }
static void cmd_Delete(JsonDocument& c){ if (!file_Arg(c)){ mtb_Ble_App_Cmd_Reject(); return; } request(R_DELETE, editing()); }
static void cmd_Extra(JsonDocument&){ request(R_EXTRA, editing()); }
static void cmd_Sign_In(JsonDocument&){
    Kk_Session_t k;
    kk_Begin(k);
    bool used = false;
    for (int i = 0; i < k.count; i++) if (k.real[i] == editing()) used = true;
    if (!used){ mtb_Ble_App_Cmd_Reject(); return; }
    request(R_SIGN_IN, editing());
}
static void cmd_Forget(JsonDocument&){ request(R_FORGET_SECRET, editing()); }
static void cmd_Reset(JsonDocument&){ request(R_RESET, editing()); }
static void cmd_Erase(JsonDocument&){ request(R_ERASE_FILES, editing()); }

// The knob ---------------------------------------------------------------------------------------------------------------------
static volatile bool holdRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) holdRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicksRequested = (uint8_t)(b.count > 3 ? 3 : b.count);
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// Sending a picture to the phone ----------------------------------------------------------------------------------------------
struct Send_t { uint8_t* data; size_t size, offset; char file[24]; char* text; };

static bool send_Step(Send_t& s){
    if (!s.data || s.offset >= s.size) return false;
    const size_t n = s.size - s.offset < PIECE ? s.size - s.offset : PIECE;
    size_t olen = 0;
    const int k = snprintf(s.text, 160, "{\"app_command\":247,\"file\":\"%s\",\"size\":%u,\"offset\":%u,\"data\":\"", s.file, (unsigned)s.size, (unsigned)s.offset);
    if (mbedtls_base64_encode((unsigned char*)s.text + k, PIECE / 3 * 4 + 4, &olen, s.data + s.offset, n) != 0) return false;
    memcpy(s.text + k + olen, "\"}", 3);
    if (bleApplicationComSend(ROUTE, String(s.text)) <= 0) return true;    // not sent: again next time
    s.offset += n;
    return s.offset < s.size;
}

/** Kids Learning's settings: 15 more minutes today for a child (as its own page does). */
static void extra_Minutes(Kk_Session_t& k, uint8_t real){
    kk_End(k);                           // what is pending first
    Kid_Settings_t* s = (Kid_Settings_t*)heap_caps_malloc(sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    memset(s, 0, sizeof(*s));
    mtb_Read_Nvs_Struct(KID_NVS_KEY, s, sizeof(*s));
    if (s->magic != KID_SETTINGS_MAGIC) kid_Settings_Default(*s);
    kid_Settings_Clamp(*s);
    Kid_Child_t& c = s->children[real % KID_CHILDREN];
    const uint32_t now = (uint32_t)time(nullptr);
    if (now > 1700000000u) kid_New_Day(c, kk_Local_Day(now));
    c.extraSeconds = (uint16_t)(c.extraSeconds + 900 > 0xFFFF ? 0xFFFF : c.extraSeconds + 900);
    mtb_Write_Nvs_Struct(KID_NVS_KEY, s, sizeof(*s));
    heap_caps_free(s);
    const uint8_t child = k.child;
    kk_Begin(k);                         // the time left, read again
    if (child < k.count) k.child = child;
}

// The app ------------------------------------------------------------------------------------------------------------------------
static void myFirstComputer_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Slot, cmd_Child, cmd_Programs, cmd_Code, cmd_Locked, cmd_Leave, cmd_Send, cmd_Delete,
                                       cmd_Extra, cmd_Sign_In, cmd_Forget, cmd_Reset);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(12, cmd_Erase);
    THIS_APP->mtb_App_Init();
    read_Settings(settings);
    holdRequested = false;
    clicksRequested = 0;
    turns = 0;
    requests = 0;
    phoneChanged = false;

    Mfc_State_t* s = (Mfc_State_t*)heap_caps_calloc(1, sizeof(Mfc_State_t), MALLOC_CAP_SPIRAM);
    Kk_Session_t* k = (Kk_Session_t*)heap_caps_calloc(1, sizeof(Kk_Session_t), MALLOC_CAP_SPIRAM);
    uint16_t* canvas = (uint16_t*)heap_caps_malloc(MFC_CANVAS_W * MFC_CANVAS_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    Send_t send = {};
    send.text = (char*)heap_caps_malloc(160 + PIECE / 3 * 4 + 8, MALLOC_CAP_SPIRAM);
    if (!s || !k || !canvas || !send.text){
        heap_caps_free(s); heap_caps_free(k); heap_caps_free(canvas); heap_caps_free(send.text);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    kk_Begin(*k);
    s->set = &settings;
    s->k = k;
    s->canvas = canvas;
    s->ms = millis();
    mfc_Begin(*s);
    mtb_Usb_Input_Open(false);              // the mouse and keyboard are the computer's (media keys stay system-wide)
    appOpen = true;

    uint32_t lastDraw = 0, lastSecond = 0, savedMs = 0, pushedMs = 0;
    bool leaving = false, dirty = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        s->ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);

        // The phone.
        if (phoneChanged){
            phoneChanged = false;
            dirty = s->pushPhone = s->redraw = true;
            if (settings.locked && s->mode != MFC_M_LOCK) mfc_Stop(*s, 3);
            else if (!settings.locked && s->mode == MFC_M_LOCK && s->stopWhy == 3) mfc_Stop(*s, 0);
        }
        uint32_t r;
        uint8_t rc;
        char file[24];
        portENTER_CRITICAL(&lock);
        r = requests; requests = 0; rc = requestChild;
        memcpy(file, requestFile, sizeof(file));
        portEXIT_CRITICAL(&lock);
        if (r){
            if (r & R_EXTRA){ extra_Minutes(*k, rc); if (s->mode == MFC_M_LOCK && s->stopWhy == KK_TIME_UP) mfc_Stop(*s, 0); }
            if (r & R_SIGN_IN) for (uint8_t i = 0; i < k->count; i++) if (k->real[i] == rc){ s->prog = MFC_PROGRAMS; s->dlg.kind = D_NONE; mfc_Sign_In(*s, i); }
            if (r & R_FORGET_SECRET){ memset(settings.c[rc].secret, 0, sizeof(settings.c[rc].secret)); dirty = true; }
            if (r & R_RESET){ mfc_Child_Reset(settings.c[rc]); dirty = true; }
            if (r & R_ERASE_FILES){ mfc_Files_Erase_All(rc); s->files.stale = true; s->fileCount = -1; }
            if (r & R_DELETE){
                const size_t n = strlen(file);
                char name[MFC_NAME_LEN + 1];
                snprintf(name, sizeof(name), "%.*s", (int)(n - 4), file);
                const uint8_t kind = strstr(file, ".txt") ? 1 : 0;
                mfc_File_Erase(rc, kind ? MFC_F_WRITING : MFC_F_PICTURES, name, kind);
                s->files.stale = true;
                s->fileCount = -1;
            }
            if ((r & R_SEND) && !send.data){
                const size_t n = strlen(file);
                char name[MFC_NAME_LEN + 1], path[96];
                snprintf(name, sizeof(name), "%.*s", (int)(n - 4), file);
                mfc_File_Path(rc, MFC_F_PICTURES, name, 0, path, sizeof(path));
                if (mfc_File_Read(path, &send.data, &send.size, 256 * 1024)){ send.offset = 0; snprintf(send.file, sizeof(send.file), "%s", file); }
                else bleApplicationComSend(ROUTE, String("{\"app_command\":247,\"file\":\"") + file + "\",\"error\":\"That picture is not on Pixlpal any more\"}");
            }
            if (r & R_LEAVE) s->exitApp = true;
            s->pushPhone = s->redraw = true;
        }
        if (send.data && !send_Step(send)){ heap_caps_free(send.data); send.data = nullptr; }

        // The knob, then the mouse and keyboard.
        if (holdRequested){ holdRequested = false; mfc_Knob_Hold(*s); }
        const int t = turns;
        if (t){ turns -= t; mfc_Knob_Turn(*s, t > 0 ? 1 : -1); if (t > 1 || t < -1) mfc_Knob_Turn(*s, t > 0 ? 1 : -1); }
        const uint8_t clicks = clicksRequested;
        if (clicks){ clicksRequested = 0; mfc_Knob_Click(*s, clicks); }
        Mtb_Usb_Input_t in;
        int events = 0;
        while (events++ < 24 && mtb_Usb_Input_Read(in, 0)){
            switch (in.type){
                case MTB_USB_KEY_DOWN: mfc_Key(*s, in.key, in.modifiers, in.ascii); break;
                case MTB_USB_MOUSE_MOVE: mfc_Mouse_Move(*s, in.dx, in.dy); break;
                case MTB_USB_MOUSE_BUTTONS: mfc_Mouse_Buttons(*s, in.buttons); break;
                case MTB_USB_MOUSE_WHEEL: mfc_Mouse_Wheel(*s, in.wheel); break;
                default: break;
            }
        }

        // Time: the day's limit and bedtime (Kids Learning's), once a second.
        if (now != lastSecond){
            lastSecond = now;
            if (now > 1700000000u){
                const bool using_ = s->signedIn && s->mode == MFC_M_DESKTOP;
                kk_Tick(*k, using_);
                uint8_t why = settings.locked ? 3 : kk_Stop(*k, kk_Local_Minute(now));
                if (why && (s->signedIn || s->mode == MFC_M_SIGNIN || s->mode == MFC_M_SECRET || s->mode == MFC_M_LOCK)) mfc_Stop(*s, why);
                else if (!why && s->mode == MFC_M_LOCK) mfc_Stop(*s, 0);
                if (using_ && k->left != 0xFFFF && k->left > 0 && k->left <= 60) mfc_Time_Warning(*s);
                if (using_){ Mfc_Child_t& me = mfc_Me(*s); me.seconds++; if (me.seconds % 60 == 0) dirty = true; }
            } else if (settings.locked && s->mode != MFC_M_LOCK) mfc_Stop(*s, 3);
            if (s->mode == MFC_M_DESKTOP) s->redraw = true;          // the clock
        }
        mfc_Tick(*s);

        // A grown-up chose to leave: back to the app before (or the Calendar Clock).
        if (s->exitApp && !leaving){
            leaving = true;
            mfc_Stop(*s, MFC_STOP_LEAVING); // unsaved work is saved; "see you soon"
            settings.locked = 0;
            dirty = true;
            Mtb_Applications* before = Mtb_Applications::previousRunningApp;
            if (before && before != THIS_APP) mtb_Launch_This_App(before);
            else mtb_Identify_App_By_ID({0, 0}, LAUNCH_PXP_APP);
        }

        nowChild = s->signedIn ? (int8_t)s->child : -1;
        nowProgram = s->prog;
        nowMode = s->mode;
        if (s->saveSettings){ s->saveSettings = false; dirty = true; }
        if (dirty && s->ms - savedMs >= SAVE_EVERY_MS){ dirty = false; savedMs = s->ms; mtb_Write_Nvs_Struct(MFC_NVS_KEY, &settings, sizeof(settings)); }
        if (s->pushPhone && s->ms - pushedMs >= 1000){ s->pushPhone = false; pushedMs = s->ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        // The cursor blinks; the pop-up flashes.
        if (s->mode == MFC_M_DESKTOP && s->ms - lastDraw >= 250) s->redraw = true;
        if (s->redraw && s->ms - lastDraw >= FRAME_MS){
            s->redraw = false;
            lastDraw = s->ms;
            mtb_Panel_Begin_Update();
            mfc_Draw(*s);
            mtb_Panel_End_Update();
        }
        delay(5);
    }
    appOpen = false;
    mtb_Usb_Input_Close();
    if (!leaving) mfc_Stop(*s, MFC_STOP_LEAVING);   // work open is saved
    kk_End(*k);
    mtb_Write_Nvs_Struct(MFC_NVS_KEY, &settings, sizeof(settings));
    nowChild = -1;
    heap_caps_free(send.data);
    heap_caps_free(send.text);
    heap_caps_free(canvas);
    heap_caps_free(k);
    heap_caps_free(s);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ----------------------------------------------------------------------------------------------
static void mfc_Values(JsonObject values){
    Mfc_Settings_t* s = (Mfc_Settings_t*)heap_caps_malloc(sizeof(Mfc_Settings_t), MALLOC_CAP_SPIRAM);
    Kid_Settings_t* kid = (Kid_Settings_t*)heap_caps_malloc(sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    Mfc_File_t* list = (Mfc_File_t*)heap_caps_malloc(sizeof(Mfc_File_t) * MFC_LIST, MALLOC_CAP_SPIRAM);
    if (!s || !kid || !list){ heap_caps_free(s); heap_caps_free(kid); heap_caps_free(list); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = settings; portEXIT_CRITICAL(&lock); }
    else read_Settings(*s);
    memset(kid, 0, sizeof(*kid));
    mtb_Read_Nvs_Struct(KID_NVS_KEY, kid, sizeof(*kid));
    if (kid->magic != KID_SETTINGS_MAGIC) kid_Settings_Default(*kid);
    const uint32_t now = (uint32_t)time(nullptr);
    const uint32_t today = now > 1700000000u ? kk_Local_Day(now) : 0;
    const uint8_t e = s->editing % MFC_CHILDREN;
    char t[160];

    // Now.
    JsonArray nowRows = values["now"].to<JsonArray>();
    JsonObject o = nowRows.add<JsonObject>();
    o["label"] = "Pixlpal";
    if (!appOpen) o["value"] = "My First Computer is not open";
    else if (s->locked) o["value"] = "Locked by a grown-up";
    else if (nowMode == MFC_M_LOCK) o["value"] = "Locked: time is up or it's bedtime";
    else if (nowChild >= 0){
        if (nowProgram < MFC_PROGRAMS) snprintf(t, sizeof(t), "%s is using %s", kid->children[nowChild].name, mfc_Program_Name(nowProgram));
        else snprintf(t, sizeof(t), "%s is on the desktop", kid->children[nowChild].name);
        o["value"] = t;
    } else o["value"] = nowMode == MFC_M_OFF ? "Shut down" : "Waiting for someone to sign in";
    o["state"] = s->locked || nowMode == MFC_M_LOCK ? "warn" : "info";

    // The children.
    JsonArray rows = values["children"].to<JsonArray>();
    for (int i = 0; i < KID_CHILDREN; i++){
        const Kid_Child_t& c = kid->children[i];
        if (!c.used) continue;
        const Mfc_Child_t& m = s->c[i];
        const unsigned minutes = c.day == today ? c.todaySeconds / 60u : 0;
        snprintf(t, sizeof(t), "Level %u, %d of %d badges, %u min on the kids apps today%s%s", mfc_Level(m, c.age), mfc_Badge_Count(m), MFC_BADGES, minutes,
                 m.lastProgram < MFC_PROGRAMS ? ", last: " : "", m.lastProgram < MFC_PROGRAMS ? mfc_Program_Name(m.lastProgram) : "");
        char label[24];
        snprintf(label, sizeof(label), "%u. %s", (unsigned)(i + 1), c.name);
        JsonObject row = rows.add<JsonObject>();
        row["label"] = label;
        row["value"] = t;
        row["state"] = "info";
    }

    // The child the page shows.
    values["childSlot"] = e;
    values["level"] = s->c[e].levelMode;
    values["oneClick"] = s->c[e].oneClick;
    JsonArray lic = values["licence"].to<JsonArray>();
    if (!kid->children[e].used){
        JsonObject row = lic.add<JsonObject>();
        row["label"] = "Not set up";
        row["value"] = "Set this child up in Kids Learning";
        row["state"] = "warn";
    } else for (uint8_t lv = 1; lv <= 3; lv++){
        int total = 0;
        const int got = mfc_Level_Badges(s->c[e], lv, total);
        size_t n = (size_t)snprintf(t, sizeof(t), "%d of %d", got, total);
        if (got < total){
            n += (size_t)snprintf(t + n, sizeof(t) - n, ". Still to earn:");
            bool first = true;
            for (uint8_t b = 0; b < MFC_BADGES && n < sizeof(t) - 24; b++){
                if (mfc_Badge_Level(b) != lv || mfc_Has(s->c[e], b)) continue;
                n += (size_t)snprintf(t + n, sizeof(t) - n, "%s %s", first ? "" : ",", mfc_Badge_Name(b));
                first = false;
            }
        }
        JsonObject row = lic.add<JsonObject>();
        char label[16];
        snprintf(label, sizeof(label), "Level %u", lv);
        row["label"] = label;
        row["value"] = t;
        row["state"] = got == total ? "ok" : "info";
    }
    JsonArray files = values["files"].to<JsonArray>();
    for (uint8_t f = MFC_F_PICTURES; f <= MFC_F_WRITING; f++){
        const int n = mfc_Files_List(e, f, list, MFC_LIST);
        for (int i = 0; i < n; i++){
            JsonObject row = files.add<JsonObject>();
            snprintf(t, sizeof(t), "%s.%s", list[i].name, list[i].kind ? "txt" : "png");
            row["name"] = t;
            row["kind"] = list[i].kind ? "txt" : "png";
            row["size"] = list[i].size;
            row["time"] = list[i].time;
        }
    }

    for (size_t i = 0; i < sizeof(PROG_OF); i++) values[PROG_ARG[i]] = (s->programs >> PROG_OF[i]) & 1;
    values["locked"] = s->locked;
    values["codeSet"] = s->codeSet;
    values["code_set"] = s->codeSet ? true : false;     // the secret field shows "set" (the code itself never goes out)
    heap_caps_free(s);
    heap_caps_free(kid);
    heap_caps_free(list);
}
