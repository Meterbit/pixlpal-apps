// The verse-of-the-day worker of the Bible-based FAITH apps.
#include "fth_Worker.h"
#include "fth_Net.h"
#include "mtb_engine.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

struct Worker_t {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lockBuffer;
    volatile bool running, stop, refresh, another, changed;
    Fth_Worker_Config_t config;         // under lock
    Fth_Verse_t* verse;                 // under lock
    char error[80];                     // under lock
    uint32_t version;
};
EXT_RAM_BSS_ATTR static Worker_t W;
static bool made = false;

static void lock(void){ xSemaphoreTake(W.lock, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(W.lock); }
static uint32_t mono(void){ return millis() / 1000; }

// An installed app has one thread: the worker is a step the compat layer runs each time the app's loop waits
// (pxl_compat_add_background). Its state lives here between steps; the timers below decide when to fetch.
static Fth_Worker_Config_t c;
static uint32_t verseAt, verseBackoff, dailyAt, dailyBackoff, dailyDone, pickDay;
static uint8_t pick;
static Fth_Verse_t* v = nullptr;
static uint32_t nextStepMs = 0;

static void worker_Begin(void){
    lock();
    c = W.config;
    unlock();
    verseAt = 0; verseBackoff = 60; dailyAt = 0; dailyBackoff = 60; dailyDone = 0; pickDay = 0; pick = 0;
    if (!v) v = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    if (!v) W.stop = true;
}

static void worker_Step(void){
    if (W.stop || !W.running) return;
    if (millis() < nextStepMs && !W.refresh && !W.another && !W.changed) return;     // about once a second
    nextStepMs = millis() + 1000;
    {
        if (W.changed){ W.changed = false; lock(); c = W.config; unlock(); verseAt = 0; }
        const bool all = W.refresh;
        if (all){ W.refresh = false; verseAt = 0; dailyAt = 0; dailyDone = 0; }
        const uint32_t now = (uint32_t)time(nullptr);
        if (now > 1700000000u && Mtb_Applications::internetConnectStatus){
            int y, m, d;
            const uint32_t today = fth_Local_Day(now, &y, &m, &d, nullptr);
            if (pickDay != today){ pickDay = today; pick = 0; }
            if (W.another){ W.another = false; pick++; verseAt = 0; }
            // The app's daily job.
            if (c.daily && dailyDone != today && (int32_t)(mono() - dailyAt) >= 0){
                if (c.daily(today, y, m, d)){ dailyDone = today; dailyBackoff = 60; }
                else { dailyAt = mono() + dailyBackoff; dailyBackoff = dailyBackoff * 2 > 1800 ? 1800 : dailyBackoff * 2; }
            }
            // The verse.
            if (c.verse && c.reference && (int32_t)(mono() - verseAt) >= 0){
                lock();
                const bool fresh = W.verse->day == today && W.verse->pick == pick && W.verse->edition == c.edition && W.verse->text[0];
                unlock();
                if (fresh && !all) verseAt = mono() + 60;
                else {
                    char e[80];
                    memset(v, 0, sizeof(*v));
                    if (fth_Fetch_Bible(c.reference(today, pick), c.translation, *v, e, sizeof(e))){
                        v->day = today;
                        v->pick = pick;
                        v->edition = c.edition;
                        lock();
                        *W.verse = *v;
                        W.error[0] = '\0';
                        W.version++;
                        unlock();
                        if (c.nvsKey[0]) mtb_Write_Nvs_Struct(c.nvsKey, v, sizeof(Fth_Verse_t));
                        verseBackoff = 60;
                        verseAt = mono() + 60;
                    } else {
                        lock();
                        fth_Copy(W.error, sizeof(W.error), e);
                        W.version++;
                        unlock();
                        verseAt = mono() + verseBackoff;
                        verseBackoff = verseBackoff * 2 > 1800 ? 1800 : verseBackoff * 2;
                    }
                }
            }
        }
    }
}

bool fth_Worker_Start(const Fth_Worker_Config_t& config){
    if (!made){
        W.lock = xSemaphoreCreateMutexStatic(&W.lockBuffer);
        W.verse = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
        if (!W.lock || !W.verse) return false;
        made = true;
    }
    for (int i = 0; i < 400 && W.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (W.running) return false;
    Fth_Verse_t* saved = (Fth_Verse_t*)heap_caps_calloc(1, sizeof(Fth_Verse_t), MALLOC_CAP_SPIRAM);
    if (saved && config.nvsKey[0]){
        mtb_Read_Nvs_Struct(config.nvsKey, saved, sizeof(Fth_Verse_t));
        saved->text[sizeof(saved->text) - 1] = '\0';
        saved->extra[sizeof(saved->extra) - 1] = '\0';
        saved->reference[sizeof(saved->reference) - 1] = '\0';
    }
    lock();
    W.config = config;
    if (saved) *W.verse = *saved;
    W.error[0] = '\0';
    W.version++;
    unlock();
    heap_caps_free(saved);
    W.stop = false;
    W.refresh = false;
    W.another = false;
    W.changed = false;
    W.running = true;
    worker_Begin();
    pxl_compat_add_background(worker_Step);
    return true;
}

void fth_Worker_Stop(void){
    if (!W.running) return;
    W.stop = true;
    pxl_compat_remove_background(worker_Step);
    W.running = false;
}

void fth_Worker_Configure(const Fth_Worker_Config_t& config){
    if (!made) return;
    lock();
    W.config = config;
    unlock();
    W.changed = true;
}

void fth_Worker_Refresh(void){ W.refresh = true; }
void fth_Worker_Another(void){ W.another = true; }

void fth_Worker_Changed(void){
    if (!made) return;
    lock();
    W.version++;
    unlock();
}

bool fth_Worker_Copy(Fth_Verse_t* verse, char* error, size_t errorSize, uint32_t& version){
    if (!made) return false;
    lock();
    const bool changed = version != W.version;
    if (changed){
        if (verse) *verse = *W.verse;
        if (error) fth_Copy(error, errorSize, W.error);
        version = W.version;
    }
    unlock();
    return changed;
}
