// Pixlpal compat: FreeRTOS as an app sees it inside WebAssembly. An installed app is one thread: critical sections
// and mutexes do nothing, delays hand control back to Pixlpal (pxl_yield), and there are no other tasks.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configTICK_RATE_HZ 1000
#define tskNO_AFFINITY 0x7FFFFFFF
#define tskIDLE_PRIORITY 0
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#ifdef __cplusplus
extern "C" {
#endif
void pxl_compat_delay(uint32_t ms);
uint32_t pxl_compat_millis(void);
/** A worker's step, run each time the app's loop hands control back (its delay), never inside another step: what a
 *  firmware app did in a task of its own (fetching data...), cut into steps that return. At most 8. */
void pxl_compat_add_background(void (*step)(void));
void pxl_compat_remove_background(void (*step)(void));
/** A short wait if the app hasn't waited for 50 ms: for loops that read data already in memory, which on the
 *  firmware waited for the network. Gives the turn to the loop or the app's tasks, and a fresh instruction budget. */
void pxl_compat_breathe(void);
#ifdef __cplusplus
}
#endif
static inline void vTaskDelay(TickType_t t){ pxl_compat_delay(t); }
static inline TickType_t xTaskGetTickCount(void){ return pxl_compat_millis(); }
static inline void vTaskDelete(TaskHandle_t){}
static inline void taskYIELD(void){ pxl_compat_delay(0); }
/* No task runs beside the app. A task an app starts runs to its end at the app's next wait (its delay): right for a
 * worker that does one job and ends (a fetch), with the app's loop paused meanwhile. A worker that loops forever must
 * be cut into steps instead (pxl_compat_add_background). At most 8 waiting. */
BaseType_t xTaskCreatePinnedToCore(void (*task)(void*), const char* name, uint32_t stack, void* parameter, UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
static inline BaseType_t xTaskCreate(void (*task)(void*), const char* name, uint32_t stack, void* parameter, UBaseType_t priority, TaskHandle_t* handle){
    return xTaskCreatePinnedToCore(task, name, stack, parameter, priority, handle, tskNO_AFFINITY);
}
/* Task notifications, as counts: a task's wait hands the turn to the app's loop and its other tasks. */
TaskHandle_t xTaskGetCurrentTaskHandle(void);
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t wait);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
static inline void vTaskSuspend(TaskHandle_t){}
static inline void vTaskResume(TaskHandle_t){}

/* Queues work within the app's one thread: a receive that would wait hands control back to Pixlpal while it waits
 * (input and commands arrive then, and may send to the queue). */
typedef struct PxlQueue* QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
BaseType_t xQueueReset(QueueHandle_t q);
void vQueueDelete(QueueHandle_t q);
#include "freertos/timers.h"
