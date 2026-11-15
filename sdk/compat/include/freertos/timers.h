// Pixlpal compat: FreeRTOS software timers. An installed app has no timer task: a timer's callback runs at the app's
// next wait (its delay) after the timer is due, never inside a worker's step. At most 12 timers.
#pragma once
#include "freertos/FreeRTOS.h"
typedef struct PxlTimer* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);
TimerHandle_t xTimerCreate(const char* name, TickType_t period, UBaseType_t autoReload, void* id, TimerCallbackFunction_t callback);
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerReset(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t wait);   /**< and starts it */
BaseType_t xTimerDelete(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerIsTimerActive(TimerHandle_t timer);
void* pvTimerGetTimerID(TimerHandle_t timer);
TickType_t xTimerGetPeriod(TimerHandle_t timer);
static inline BaseType_t xTimerStartFromISR(TimerHandle_t t, BaseType_t*){ return xTimerStart(t, 0); }
static inline BaseType_t xTimerStopFromISR(TimerHandle_t t, BaseType_t*){ return xTimerStop(t, 0); }
static inline BaseType_t xTimerResetFromISR(TimerHandle_t t, BaseType_t*){ return xTimerReset(t, 0); }
