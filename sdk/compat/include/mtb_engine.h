// Pixlpal compat: the firmware's app framework (meterbit_engine), so an app written for it builds as an installed
// app with few changes. The app's task becomes pxl_main: its loop's delay() hands control back to Pixlpal, the knob
// and phone commands arrive as the callbacks it registered, its settings are files (pxl_file_*), its phone page goes
// through pxl_phone_ui. One thread: no other tasks (xTaskCreate fails), critical sections do nothing.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <initializer_list>
#include <ArduinoJson.h>
#include "pixlpal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mtb_task_map.h"        // the task priorities, as the firmware's engine header brings them
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mtb_graphics.h"

typedef int esp_err_t;
#define ESP_OK    0
#define ESP_FAIL  -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102

// Time: the C library's time() is UTC; Pixlpal knows the owner's time zone.
struct tm* pxl_compat_localtime_r(const time_t* t, struct tm* out);
struct tm* pxl_compat_localtime(const time_t* t);
#define localtime_r pxl_compat_localtime_r
#define localtime pxl_compat_localtime

static inline uint32_t millis(void){ return pxl_compat_millis(); }
static inline void delay(uint32_t ms){ pxl_compat_delay(ms); }

struct Mtb_User_AppID_t { uint16_t GenApp; uint16_t SpeApp; };

typedef enum { ROT_CLOCKWISE = 0, ROT_COUNTERCLOCKWISE = 1 } rotary_encoder_rotation_t;
typedef enum { BUTTON_RELEASED = 0, BUTTON_PRESSED, BUTTON_PRESSED_LONG, BUTTON_CLICKED } button_event_type_t;
typedef struct { void* sender; button_event_type_t type; uint8_t count; uint32_t delta_ms; } button_event_t;
typedef void (*mtb_Common_ButtonFn_Ptr_t)(button_event_t);
typedef void (*encoderFn_ptr_t)(rotary_encoder_rotation_t);
void buttonDoNothing(button_event_t);
void encoderDoNothing(rotary_encoder_rotation_t);
/** The knob's default: the panel's brightness. */
void mtb_Brightness_Control(rotary_encoder_rotation_t direction);

typedef void (*bleCom_Parser_Fns_Ptr)(JsonDocument&);
typedef void (*mtb_Mobile_UI_Values_Fn_t)(JsonObject values);
#define MTB_MOBILE_VALUES_CMD 249

enum Mtb_Status_Bar_t { NO_STATUS_BAR = 0, ICONS_ONLY_STATUS_BAR, CLOCK_STATUS_BAR };
enum Mtb_Action_On_App_t { LAUNCH_PXP_APP = 1, KILL_PXP_APP, SHOW_PXP_APP_UI, IDENTIFY_PXP_APP };
enum Mtb_Do_Prev_App_t { DESTROY_PREVIOUS_APP = 0, SUSPEND_PREVIOUS_APP };

class Mtb_Applications {
public:
    Mtb_User_AppID_t appID = {0, 0};
    const char* appName = "";
    void (*appTask)(void*) = nullptr;
    Mtb_Action_On_App_t launchAction = LAUNCH_PXP_APP;
    volatile uint8_t app_is_Running = pdTRUE;
    static Mtb_Applications* currentRunningApp;
    static Mtb_Applications* previousRunningApp;
    static bool internetConnectStatus;
    // Set by the app; used by the compat layer.
    mtb_Common_ButtonFn_Ptr_t buttonFn = buttonDoNothing;
    encoderFn_ptr_t encoderFn = mtb_Brightness_Control;
    bleCom_Parser_Fns_Ptr bleAppComServiceFns[32] = {nullptr};
    const char* uiManifest = nullptr;
    const char* uiApi = nullptr;
    const char* uiData = nullptr;
    mtb_Mobile_UI_Values_Fn_t uiValues = nullptr;

    Mtb_Applications() {}
    Mtb_Applications(void (*task)(void*), TaskHandle_t*, const char* name, Mtb_User_AppID_t id, uint32_t)
        : appID(id), appName(name), appTask(task) {}
    virtual ~Mtb_Applications() {}
    void mtb_App_Set_EC11_Cb_Fns(mtb_Common_ButtonFn_Ptr_t but_Fn = buttonDoNothing, encoderFn_ptr_t enc_Fn = mtb_Brightness_Control){ buttonFn = but_Fn; encoderFn = enc_Fn; }
    void mtb_App_Set_Ble_Comm_Fns(bleCom_Parser_Fns_Ptr f0 = nullptr, bleCom_Parser_Fns_Ptr f1 = nullptr, bleCom_Parser_Fns_Ptr f2 = nullptr,
                                  bleCom_Parser_Fns_Ptr f3 = nullptr, bleCom_Parser_Fns_Ptr f4 = nullptr, bleCom_Parser_Fns_Ptr f5 = nullptr,
                                  bleCom_Parser_Fns_Ptr f6 = nullptr, bleCom_Parser_Fns_Ptr f7 = nullptr, bleCom_Parser_Fns_Ptr f8 = nullptr,
                                  bleCom_Parser_Fns_Ptr f9 = nullptr, bleCom_Parser_Fns_Ptr f10 = nullptr, bleCom_Parser_Fns_Ptr f11 = nullptr){
        bleCom_Parser_Fns_Ptr f[12] = {f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11};
        for (int i = 0; i < 12; i++) bleAppComServiceFns[i] = f[i];
    }
    void mtb_App_Set_Ble_Comm_Fn(uint8_t index, bleCom_Parser_Fns_Ptr fn){ if (index < 32) bleAppComServiceFns[index] = fn; }
    /** The firmware's list of commands that need the internet. Here nothing to do: when an installed app's request is
     *  refused because Pixlpal is offline just after a phone command, Pixlpal tells the phone itself. */
    void mtb_App_Set_Online_Cmds(std::initializer_list<uint8_t>){}
    void mtb_App_Set_Mobile_UI(const char* manifest, const char* uiApi, const char* data, mtb_Mobile_UI_Values_Fn_t valuesFn);
    /** Sends the phone page and its values (the firmware: and starts the app's display). */
    void mtb_App_Init();
    void mtb_App_Push_Mobile_Values(bool force = false);
    void mtb_App_Send_Search_Results(const char* searchId, const char* query, JsonArrayConst results, const char* error = nullptr);
};
class Mtb_Applications_FullScreen : public Mtb_Applications {
public:
    Mtb_Applications_FullScreen() {}
    Mtb_Applications_FullScreen(void (*task)(void*), TaskHandle_t* h, const char* name, Mtb_User_AppID_t id, uint32_t stack)
        : Mtb_Applications(task, h, name, id, stack) {}
};
class Mtb_Applications_StatusBar : public Mtb_Applications {
public:
    Mtb_Applications_StatusBar() {}
    /** The status bar's look is Pixlpal's (app.json's "screen": "statusbar"); the last argument is accepted. */
    Mtb_Applications_StatusBar(void (*task)(void*), TaskHandle_t* h, const char* name, Mtb_User_AppID_t id, uint32_t stack, Mtb_Status_Bar_t = CLOCK_STATUS_BAR)
        : Mtb_Applications(task, h, name, id, stack) {}
};

// The app's registration: the compat layer's pxl_start finds the one app this module holds.
struct Mtb_App_Registry_Entry {
    Mtb_User_AppID_t appID;
    Mtb_Applications* (*getRaw)();
    Mtb_Applications* (*getInstance)();
    Mtb_App_Registry_Entry* next;
};
extern Mtb_App_Registry_Entry* mtb_App_Registry_Head;
struct Mtb_App_AutoRegister {
    Mtb_App_AutoRegister(Mtb_App_Registry_Entry* entry){ entry->next = mtb_App_Registry_Head; mtb_App_Registry_Head = entry; }
};
#define MTB_REGISTER_APP(appName, genApp, speApp)                             \
    static Mtb_Applications* appName##_getRaw()      { return appName; }      \
    static Mtb_App_Registry_Entry appName##_reg_entry = {                     \
        {genApp, speApp}, appName##_getRaw, appName##_GetInstance, nullptr    \
    };                                                                        \
    static Mtb_App_AutoRegister appName##_registrar(&appName##_reg_entry);

// The task: THIS_APP is the task's parameter, THIS_APP_IS_ACTIVE false once Pixlpal closes the app.
#define THIS_APP           thisApp
#define THIS_APP_IS_ACTIVE (mtb_App_Checkpoint(thisApp))
uint8_t mtb_App_Checkpoint(Mtb_Applications* app);
/** The end of the app's task: it returns, and the app closes. */
void mtb_Delete_This_App(Mtb_Applications* app);
/** Opens another app: by its descriptor (the one before: Mtb_Applications::previousRunningApp), or its ID. */
Mtb_Applications* mtb_Launch_This_App(Mtb_Applications* app, Mtb_Action_On_App_t action = LAUNCH_PXP_APP, Mtb_Do_Prev_App_t prev = DESTROY_PREVIOUS_APP);
void mtb_Identify_App_By_ID(Mtb_User_AppID_t id, Mtb_Action_On_App_t action);

// Services Pixlpal runs itself (the time from the internet...): an app's request for them does nothing.
struct Mtb_Services { int unused; };
extern Mtb_Services* mtb_Sntp_Time_Sv;
static inline void mtb_Launch_This_Service(Mtb_Services*){}

/** Settings: each key a file of the app's (pxl_file_*). As in the firmware, reading a missing key stores the
 *  buffer as it is (the defaults) and succeeds. */
esp_err_t mtb_Read_Nvs_Struct(const char* key, void* data, size_t size);
esp_err_t mtb_Write_Nvs_Struct(const char* key, void* data, size_t size);
esp_err_t mtb_Read_Nvs_Struct_If_Saved(const char* key, void* data, size_t size);
esp_err_t mtb_Erase_Nvs_Key(const char* key);

/** Inside a phone command's handler: the command is refused (the phone shows it failed). */
void mtb_Ble_App_Cmd_Reject(void);
/** As the firmware's: here the same as mtb_Ble_App_Cmd_Reject (Pixlpal tells the phone it is offline itself). */
inline void mtb_Ble_App_Cmd_Reject_Offline(void){ mtb_Ble_App_Cmd_Reject(); }
/** A message of the app's own to its phone page (its route is the only one it can reach). Returns its length. */
#include "WString.h"
int bleApplicationComSend(const char* route, const String& message);
int bleApplicationComSend(const char* route, const char* message);
