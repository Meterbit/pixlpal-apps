/**
 * Timers app, ID {0,4}: a stopwatch with laps, a countdown timer, and an interval timer (Tabata, HIIT, EMOM,
 * Pomodoro, or your own work and rest), worked from the knob or the phone. They keep running while another app is on
 * the panel, and a finished countdown is still waiting, with the time over, when the app comes back.
 *
 * The knob: click starts and stops; double click laps (stopwatch) or resets; long press moves to the next timer;
 * turning sets the countdown's length or the rounds while they are not running.
 *
 * Model: stw_Model.cpp. Screens: stw_Ui.cpp.
 */
#include "mtb_engine.h"
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "stw_Model.h"
#include "stw_Ui.h"
#include "stw_Mobile_Ui.h"

#define STW_SAVE_DELAY_MS       2000        // Knob turns are saved once they stop.
#define STW_ALARM_FLASH_MS      30000
#define STW_PHASE_FLASH_MS      600
#define STW_CLOCK_VALID_AFTER   1700000000L

EXT_RAM_BSS_ATTR TaskHandle_t realStopwatch_Task_H = NULL;
void realStopwatch_App_Task(void *);
static void stw_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *stopWatch_App;
Mtb_Applications* stopWatch_App_GetInstance() {
    if (!stopWatch_App){
        stopWatch_App = new Mtb_Applications_StatusBar(realStopwatch_App_Task, &realStopwatch_Task_H, "Timers", {0,4}, 6144);
        stopWatch_App->mtb_App_Set_Mobile_UI(STW_UI_MANIFEST, STW_UI_API, nullptr, stw_Mobile_Ui_Values);
    }
    return stopWatch_App;
}
MTB_REGISTER_APP(stopWatch_App, 0, 4)

// The timers outlive the app task, so they run on while another app shows. All under stateLock.
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static Stw_Settings_t stwSettings;
EXT_RAM_BSS_ATTR static Stw_Watch_t stwWatch;
EXT_RAM_BSS_ATTR static Stw_Timer_t stwTimer;
EXT_RAM_BSS_ATTR static Stw_Intervals_t stwIntervals;
static bool settingsLoaded = false;
static volatile bool settingsDirty = false;
static volatile uint32_t changedMs = 0;
static volatile bool redrawRequested = false;

static volatile uint8_t clicksRequested = 0;
static volatile int8_t turnsRequested = 0;
static volatile bool longPressRequested = false;

static void stw_Button(button_event_t button);
static void stw_Knob(rotary_encoder_rotation_t direction);
static void stw_Ble_Mode(JsonDocument& command);
static void stw_Ble_Watch_Toggle(JsonDocument& command);
static void stw_Ble_Watch_Lap(JsonDocument& command);
static void stw_Ble_Watch_Reset(JsonDocument& command);
static void stw_Ble_Timer_Length(JsonDocument& command);
static void stw_Ble_Timer_Toggle(JsonDocument& command);
static void stw_Ble_Timer_Reset(JsonDocument& command);
static void stw_Ble_Intervals_Set(JsonDocument& command);
static void stw_Ble_Intervals_Toggle(JsonDocument& command);
static void stw_Ble_Intervals_Reset(JsonDocument& command);
static void stw_Ble_Display(JsonDocument& command);

//##############################################################################################################
static int64_t stw_Now_Us(void){ return esp_timer_get_time(); }

static void stw_Load_Settings(void){
    if (settingsLoaded) return;
    Stw_Settings_t saved;
    memset(&saved, 0, sizeof(saved));
    mtb_Read_Nvs_Struct(STW_NVS_KEY, &saved, sizeof(saved));
    Stw_Settings_t s;
    stw_Settings_Default(s);
    if (saved.magic == STW_SETTINGS_MAGIC) s = saved;
    stw_Settings_Clamp(s);
    portENTER_CRITICAL(&stateLock);
    stwSettings = s;
    settingsLoaded = true;
    portEXIT_CRITICAL(&stateLock);
    if (saved.magic != STW_SETTINGS_MAGIC) mtb_Write_Nvs_Struct(STW_NVS_KEY, &s, sizeof(s));
}

// After any change: redraw now, save the settings a little later, tell the phone.
static void stw_Changed(bool settings){
    if (settings){ settingsDirty = true; changedMs = millis(); }
    redrawRequested = true;
}

static void stw_Ends_At(char* dest, size_t size, int64_t leftUs){
    const time_t now = time(nullptr);
    if (now < STW_CLOCK_VALID_AFTER){ dest[0] = '\0'; return; }
    const time_t end = now + (time_t)((leftUs + 999999) / 1000000);
    struct tm local;
    localtime_r(&end, &local);
    snprintf(dest, size, "%02d:%02d", local.tm_hour, local.tm_min);
}

//##############################################################################################################
void realStopwatch_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(stw_Button, stw_Knob);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(stw_Ble_Mode, stw_Ble_Watch_Toggle, stw_Ble_Watch_Lap, stw_Ble_Watch_Reset, stw_Ble_Timer_Length,
                                       stw_Ble_Timer_Toggle, stw_Ble_Timer_Reset, stw_Ble_Intervals_Set, stw_Ble_Intervals_Toggle,
                                       stw_Ble_Intervals_Reset, stw_Ble_Display);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    stw_Load_Settings();
    clicksRequested = 0;
    turnsRequested = 0;
    longPressRequested = false;

    Stw_Settings_t settings;
    Stw_Watch_t* watch = (Stw_Watch_t*)heap_caps_malloc(sizeof(Stw_Watch_t), MALLOC_CAP_SPIRAM);
    if (watch == nullptr){ mtb_Delete_This_App(THIS_APP); return; }
    Stw_Timer_t timer;
    Stw_Intervals_t intervals;
    uint32_t drawnMs = 0, pushMs = 0, phaseFlashMs = 0, alarmMs = 0;
    uint8_t lastTimerState = 0xFF, lastPhase = 0xFF;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const int64_t now = stw_Now_Us();
        bool changed = false;

        // The knob.
        const int8_t turns = turnsRequested;
        const uint8_t clicks = clicksRequested;
        const bool longPress = longPressRequested;
        if (turns) turnsRequested = 0;
        if (clicks) clicksRequested = 0;
        if (longPress) longPressRequested = false;
        portENTER_CRITICAL(&stateLock);
        if (longPress){ stwSettings.mode = (stwSettings.mode + 1) % STW_MODE_COUNT; settingsDirty = true; changedMs = millis(); changed = true; }
        if (clicks == 1 || clicks == 2){
            switch (stwSettings.mode){
                case STW_MODE_STOPWATCH:
                    if (clicks == 1) stw_Watch_Toggle(stwWatch, now);
                    else if (stwWatch.running) stw_Watch_Lap(stwWatch, now);
                    else stw_Watch_Reset(stwWatch);
                    break;
                case STW_MODE_TIMER:
                    if (clicks == 1) stw_Timer_Toggle(stwTimer, stwSettings.timerSeconds, now);
                    else stw_Timer_Reset(stwTimer);
                    break;
                default:
                    if (clicks == 1) stw_Intervals_Toggle(stwIntervals, stwSettings, now);
                    else stw_Intervals_Reset(stwIntervals);
                    break;
            }
            changed = true;
        }
        bool brightness = false;
        if (turns){
            if (stwSettings.mode == STW_MODE_TIMER && stwTimer.state == STW_IDLE){
                for (int i = 0; i < (turns > 0 ? turns : -turns); i++){
                    const bool up = turns > 0;
                    const uint32_t step = stw_Timer_Step(stwSettings.timerSeconds, up);
                    uint32_t next = up ? stwSettings.timerSeconds + step : (stwSettings.timerSeconds > step ? stwSettings.timerSeconds - step : 5);
                    next = next / step * step;          // Onto the step.
                    stwSettings.timerSeconds = next < 5 ? 5 : next > STW_TIMER_MAX ? STW_TIMER_MAX : next;
                }
                settingsDirty = true; changedMs = millis(); changed = true;
            } else if (stwSettings.mode == STW_MODE_INTERVALS && stwIntervals.state == STW_IDLE){
                int rounds = stwSettings.rounds + turns;
                stwSettings.rounds = (uint8_t)(rounds < 1 ? 1 : rounds > 99 ? 99 : rounds);
                settingsDirty = true; changedMs = millis(); changed = true;
            } else brightness = true;
        }
        // The timers' own changes.
        const bool timerDone = stw_Timer_Update(stwTimer, now);
        const int phaseChanges = stw_Intervals_Update(stwIntervals, now);
        settings = stwSettings;
        memcpy(watch, &stwWatch, sizeof(*watch));
        timer = stwTimer;
        intervals = stwIntervals;
        const bool save = settingsDirty && millis() - changedMs >= STW_SAVE_DELAY_MS;
        if (save) settingsDirty = false;
        if (redrawRequested){ redrawRequested = false; changed = true; }
        portEXIT_CRITICAL(&stateLock);
        if (brightness) for (int i = 0; i < (turns > 0 ? turns : -turns); i++) mtb_Brightness_Control(turns > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE);
        if (save) mtb_Write_Nvs_Struct(STW_NVS_KEY, &settings, sizeof(settings));

        if (timerDone || (timer.state == STW_DONE && lastTimerState != STW_DONE)){ alarmMs = millis(); settings.mode = STW_MODE_TIMER; portENTER_CRITICAL(&stateLock); stwSettings.mode = STW_MODE_TIMER; portEXIT_CRITICAL(&stateLock); }
        if (phaseChanges || (intervals.state == STW_RUNNING && intervals.phase != lastPhase)) phaseFlashMs = millis();
        if (timer.state != lastTimerState || intervals.phase != lastPhase) changed = true;
        lastTimerState = timer.state;
        lastPhase = intervals.phase;

        // Frames: smooth while something counts, the alarm flashing, otherwise once a second.
        const bool running = watch->running || timer.state == STW_RUNNING || intervals.state == STW_RUNNING;
        const bool alarm = timer.state == STW_DONE && millis() - alarmMs < STW_ALARM_FLASH_MS;
        const uint32_t frameMs = settings.mode == STW_MODE_STOPWATCH && watch->running && settings.hundredths ? 40 : running || alarm ? 100 : 1000;
        if (changed || millis() - drawnMs >= frameMs){
            drawnMs = millis();
            char endsAt[8];
            stw_Ends_At(endsAt, sizeof(endsAt), stw_Timer_Left(timer, settings.timerSeconds, now));
            Stw_View_t view = {};
            view.settings = &settings;
            view.watch = watch;
            view.timer = &timer;
            view.intervals = &intervals;
            view.nowUs = now;
            view.endsAt = endsAt[0] ? endsAt : nullptr;
            view.flash = settings.mode == STW_MODE_TIMER ? alarm && (millis() / 400) % 2 == 0 : millis() - phaseFlashMs < STW_PHASE_FLASH_MS;
            mtb_Panel_Begin_Update();
            stw_Draw(view);
            mtb_Panel_End_Update();
        }
        if (changed || millis() - pushMs >= (running ? 2000 : 30000)){ pushMs = millis(); THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(10);
    }
    // Unsaved knob changes are kept.
    if (settingsDirty){
        portENTER_CRITICAL(&stateLock);
        settings = stwSettings;
        settingsDirty = false;
        portEXIT_CRITICAL(&stateLock);
        mtb_Write_Nvs_Struct(STW_NVS_KEY, &settings, sizeof(settings));
    }
    heap_caps_free(watch);
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
static void stw_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 2) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void stw_Knob(rotary_encoder_rotation_t direction){
    const int8_t turns = turnsRequested;
    if (direction == ROT_CLOCKWISE && turns < 30) turnsRequested = turns + 1;
    else if (direction == ROT_COUNTERCLOCKWISE && turns > -30) turnsRequested = turns - 1;
}

static void stw_State_Text(char* dest, size_t size, const char* state, int64_t us, int decimals){
    char t[24];
    stw_Format(t, sizeof(t), us, decimals);
    snprintf(dest, size, "%s, %s", t, state);
}

// What the phone shows. Runs on the BLE parser task; the times are as of now.
static void stw_Mobile_Ui_Values(JsonObject values){
    stw_Load_Settings();
    Stw_Watch_t* watch = (Stw_Watch_t*)heap_caps_malloc(sizeof(Stw_Watch_t), MALLOC_CAP_SPIRAM);
    if (watch == nullptr) return;
    const int64_t now = stw_Now_Us();
    portENTER_CRITICAL(&stateLock);
    const Stw_Settings_t s = stwSettings;
    memcpy(watch, &stwWatch, sizeof(*watch));
    const Stw_Timer_t timer = stwTimer;
    const Stw_Intervals_t iv = stwIntervals;
    portEXIT_CRITICAL(&stateLock);
    values["mode"] = s.mode;
    values["timer"] = s.timerSeconds;
    values["timerMinutes"] = (s.timerSeconds + 30) / 60;
    values["work"] = s.work;
    values["rest"] = s.rest;
    values["rounds"] = s.rounds;
    values["hundredths"] = s.hundredths;
    values["flash"] = s.flash;
    values["theme"] = s.theme;
    char text[64];
    const int64_t elapsed = stw_Watch_Elapsed(*watch, now);
    stw_State_Text(text, sizeof(text), watch->running ? "running" : elapsed ? "stopped" : "ready", elapsed, 1);
    values["swText"] = text;
    values["swState"] = watch->running ? "ok" : elapsed ? "warn" : "off";
    values["running"] = watch->running;
    if (watch->lapCount){
        char laps[160] = "";
        int best, worst;
        stw_Watch_Best_Worst(*watch, best, worst);
        for (int r = 0; r < 6 && r < watch->lapCount; r++){
            const int i = watch->lapCount - 1 - r;
            char t[16];
            stw_Format(t, sizeof(t), stw_Watch_Lap_Time(*watch, i), 2);
            const size_t used = strlen(laps);
            snprintf(laps + used, sizeof(laps) - used, "%s%d: %s%s", r ? ", " : "", i + 1, t, i == best ? " (best)" : i == worst ? " (slowest)" : "");
        }
        values["laps"] = laps;
    } else values["laps"] = "None yet";
    static const char* const STATES[] = {"ready", "running", "paused", "done"};
    if (timer.state == STW_DONE){
        char t[24];
        stw_Format(t, sizeof(t), now > timer.doneUs ? now - timer.doneUs : 0, 0);
        snprintf(text, sizeof(text), "Time's up, %s ago", t);
    } else stw_State_Text(text, sizeof(text), STATES[timer.state & 3], stw_Ceil_Seconds(stw_Timer_Left(timer, s.timerSeconds, now)) * 1000000LL, 0);
    values["timerText"] = text;
    values["timerState"] = timer.state == STW_RUNNING ? "ok" : timer.state == STW_DONE ? "error" : timer.state == STW_PAUSED ? "warn" : "off";
    values["timerRunning"] = timer.state == STW_RUNNING ? 1 : 0;
    if (iv.state == STW_IDLE){
        char t[16];
        stw_Format(t, sizeof(t), (int64_t)stw_Intervals_Length(s.work, s.rest, s.rounds) * 1000000LL, 0);
        snprintf(text, sizeof(text), "Ready: %d rounds, %s in all", s.rounds, t);
    } else if (iv.state == STW_DONE) snprintf(text, sizeof(text), "Finished");
    else {
        static const char* const PHASES[] = {"get ready", "work", "rest", "finished"};
        char t[16];
        stw_Format(t, sizeof(t), stw_Ceil_Seconds(stw_Intervals_Total_Left(iv, s, now)) * 1000000LL, 0);
        snprintf(text, sizeof(text), "Round %d of %d, %s%s, %s left", iv.round, iv.rounds, PHASES[iv.phase & 3], iv.state == STW_PAUSED ? " (paused)" : "", t);
    }
    values["ivText"] = text;
    values["ivState"] = iv.state == STW_RUNNING ? "ok" : iv.state == STW_PAUSED ? "warn" : iv.state == STW_DONE ? "info" : "off";
    values["ivRunning"] = iv.state == STW_RUNNING ? 1 : 0;
    heap_caps_free(watch);
}

// Commands change the timers under the lock; the app task draws and saves.
static void stw_Run(void (*fn)(int64_t now), bool settings){
    stw_Load_Settings();
    const int64_t now = stw_Now_Us();
    portENTER_CRITICAL(&stateLock);
    fn(now);
    portEXIT_CRITICAL(&stateLock);
    stw_Changed(settings);
}

// Command 0: {"mode": 0 stopwatch, 1 timer, 2 intervals}
static void stw_Ble_Mode(JsonDocument& command){
    long mode;
    if (!mtb_Kit_Json_Range(command["mode"], 0, STW_MODE_COUNT - 1, mode)){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Load_Settings();
    portENTER_CRITICAL(&stateLock);
    stwSettings.mode = (uint8_t)mode;
    portEXIT_CRITICAL(&stateLock);
    stw_Changed(true);
}

// Commands 1-3: the stopwatch's start or stop, lap, reset.
static void stw_Ble_Watch_Toggle(JsonDocument&){ stw_Run([](int64_t now){ stw_Watch_Toggle(stwWatch, now); stwSettings.mode = STW_MODE_STOPWATCH; }, true); }
static void stw_Ble_Watch_Lap(JsonDocument&){
    bool ok = false;
    const int64_t now = stw_Now_Us();
    portENTER_CRITICAL(&stateLock);
    ok = stw_Watch_Lap(stwWatch, now);
    portEXIT_CRITICAL(&stateLock);
    if (!ok){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Changed(false);
}
static void stw_Ble_Watch_Reset(JsonDocument&){ stw_Run([](int64_t){ stw_Watch_Reset(stwWatch); }, false); }

// Command 4: {"timer": seconds 5-86399} or {"timerMinutes": 1-1439}: the countdown's length (while it is not running).
static void stw_Ble_Timer_Length(JsonDocument& command){
    long seconds;
    if (mtb_Kit_Json_Range(command["timer"], 5, STW_TIMER_MAX, seconds)){}
    else if (mtb_Kit_Json_Range(command["timerMinutes"], 1, STW_TIMER_MAX / 60, seconds)) seconds *= 60;
    else { mtb_Ble_App_Cmd_Reject(); return; }
    stw_Load_Settings();
    portENTER_CRITICAL(&stateLock);
    const bool idle = stwTimer.state == STW_IDLE || stwTimer.state == STW_DONE;
    if (idle){ stw_Timer_Reset(stwTimer); stwSettings.timerSeconds = (uint32_t)seconds; stwSettings.mode = STW_MODE_TIMER; }
    portEXIT_CRITICAL(&stateLock);
    if (!idle){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Changed(true);
}

// Commands 5-6: the countdown's start, pause or go on; reset.
static void stw_Ble_Timer_Toggle(JsonDocument&){ stw_Run([](int64_t now){ stw_Timer_Toggle(stwTimer, stwSettings.timerSeconds, now); stwSettings.mode = STW_MODE_TIMER; }, true); }
static void stw_Ble_Timer_Reset(JsonDocument&){ stw_Run([](int64_t){ stw_Timer_Reset(stwTimer); }, false); }

// Command 7: {"preset": 0-4}, or any of {"work": 5-3600}, {"rest": 0-1800}, {"rounds": 1-99}: while not running.
static void stw_Ble_Intervals_Set(JsonDocument& command){
    int presets;
    const Stw_Preset_t* list = stw_Presets(presets);
    long preset = -1, work = -1, rest = -1, rounds = -1;
    const bool hasPreset = mtb_Kit_Json_Range(command["preset"], 0, presets - 1, preset);
    const bool hasWork = mtb_Kit_Json_Range(command["work"], 5, 3600, work);
    const bool hasRest = mtb_Kit_Json_Range(command["rest"], 0, 1800, rest);
    const bool hasRounds = mtb_Kit_Json_Range(command["rounds"], 1, 99, rounds);
    const bool bad = (!command["preset"].isNull() && !hasPreset) || (!command["work"].isNull() && !hasWork)
                  || (!command["rest"].isNull() && !hasRest) || (!command["rounds"].isNull() && !hasRounds);
    if (bad || !(hasPreset || hasWork || hasRest || hasRounds)){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Load_Settings();
    portENTER_CRITICAL(&stateLock);
    const bool idle = stwIntervals.state == STW_IDLE || stwIntervals.state == STW_DONE;
    if (idle){
        stw_Intervals_Reset(stwIntervals);
        if (hasPreset){ stwSettings.work = list[preset].work; stwSettings.rest = list[preset].rest; stwSettings.rounds = list[preset].rounds; }
        if (hasWork) stwSettings.work = (uint16_t)work;
        if (hasRest) stwSettings.rest = (uint16_t)rest;
        if (hasRounds) stwSettings.rounds = (uint8_t)rounds;
        stwSettings.mode = STW_MODE_INTERVALS;
    }
    portEXIT_CRITICAL(&stateLock);
    if (!idle){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Changed(true);
}

// Commands 8-9: the intervals' start, pause or go on; reset.
static void stw_Ble_Intervals_Toggle(JsonDocument&){ stw_Run([](int64_t now){ stw_Intervals_Toggle(stwIntervals, stwSettings, now); stwSettings.mode = STW_MODE_INTERVALS; }, true); }
static void stw_Ble_Intervals_Reset(JsonDocument&){ stw_Run([](int64_t){ stw_Intervals_Reset(stwIntervals); }, false); }

// Command 10: any of {"hundredths": 0-1}, {"flash": 0-1}, {"theme": 0-4}
static void stw_Ble_Display(JsonDocument& command){
    long hundredths = -1, flash = -1, theme = -1;
    const bool hasH = mtb_Kit_Json_Range(command["hundredths"], 0, 1, hundredths);
    const bool hasF = mtb_Kit_Json_Range(command["flash"], 0, 1, flash);
    const bool hasT = mtb_Kit_Json_Range(command["theme"], 0, 4, theme);
    if (!(hasH || hasF || hasT)){ mtb_Ble_App_Cmd_Reject(); return; }
    stw_Load_Settings();
    portENTER_CRITICAL(&stateLock);
    if (hasH) stwSettings.hundredths = (uint8_t)hundredths;
    if (hasF) stwSettings.flash = (uint8_t)flash;
    if (hasT) stwSettings.theme = (uint8_t)theme;
    portEXIT_CRITICAL(&stateLock);
    stw_Changed(true);
}
