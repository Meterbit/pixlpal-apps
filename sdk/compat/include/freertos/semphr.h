// Pixlpal compat: one thread, so a semaphore is always free.
#pragma once
#include "FreeRTOS.h"
typedef void* SemaphoreHandle_t;
typedef struct { int unused; } StaticSemaphore_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){ return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*){ return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void){ return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){ return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t){ return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t){ return pdTRUE; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t){ return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t){ return pdTRUE; }
static inline void vSemaphoreDelete(SemaphoreHandle_t){}
