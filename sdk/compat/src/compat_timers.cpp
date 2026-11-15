// Pixlpal compat: FreeRTOS software timers (freertos/timers.h), run by a background step at the app's waits.
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "pixlpal.h"

struct PxlTimer {
    bool used, active, autoReload;
    TickType_t period;
    uint32_t dueMs;
    void* id;
    TimerCallbackFunction_t callback;
};
static PxlTimer timers[12];

static void timers_Step(void){
    const uint32_t now = pxl_millis();
    for (PxlTimer& t : timers){
        if (!t.used || !t.active || (int32_t)(now - t.dueMs) < 0) continue;
        if (t.autoReload) t.dueMs = now + (t.period ? t.period : 1);
        else t.active = false;
        if (t.callback) t.callback(&t);           // it may start, stop or delete timers
    }
}

TimerHandle_t xTimerCreate(const char*, TickType_t period, UBaseType_t autoReload, void* id, TimerCallbackFunction_t callback){
    for (PxlTimer& t : timers){
        if (t.used) continue;
        t = {true, false, autoReload != 0, period, 0, id, callback};
        pxl_compat_add_background(timers_Step);
        return &t;
    }
    return nullptr;
}
BaseType_t xTimerStart(TimerHandle_t t, TickType_t){ if (!t || !t->used) return pdFAIL; t->active = true; t->dueMs = pxl_millis() + t->period; return pdPASS; }
BaseType_t xTimerReset(TimerHandle_t t, TickType_t w){ return xTimerStart(t, w); }
BaseType_t xTimerStop(TimerHandle_t t, TickType_t){ if (!t || !t->used) return pdFAIL; t->active = false; return pdPASS; }
BaseType_t xTimerChangePeriod(TimerHandle_t t, TickType_t period, TickType_t w){ if (!t || !t->used) return pdFAIL; t->period = period; return xTimerStart(t, w); }
BaseType_t xTimerDelete(TimerHandle_t t, TickType_t){ if (!t) return pdFAIL; *t = {}; return pdPASS; }
BaseType_t xTimerIsTimerActive(TimerHandle_t t){ return t && t->used && t->active ? pdTRUE : pdFALSE; }
void* pvTimerGetTimerID(TimerHandle_t t){ return t ? t->id : nullptr; }
TickType_t xTimerGetPeriod(TimerHandle_t t){ return t ? t->period : 0; }
