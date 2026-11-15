#include "fbl_Crest.h"
#include "fbl_Config.h"
#include "fbl_Api.h"
#include "mtb_graphics.h"
#include "mtb_github.h"
#include "mtb_kit_image.h"
#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char TAG[] = "FBL_CREST";

struct Fbl_Crest_Slot_t {
    uint32_t teamId;
    uint32_t used;
    uint16_t rgb[FBL_CREST * FBL_CREST];
    uint8_t alpha[FBL_CREST * FBL_CREST];
};

static Fbl_Crest_Slot_t* slots = nullptr;       // PSRAM
static SemaphoreHandle_t crestLock = NULL;
static QueueHandle_t wanted = NULL;
static TaskHandle_t crestTask_H = NULL;
static volatile uint32_t version = 0;
static uint32_t useCounter = 0;
static uint32_t failed[16];                     // Teams with no crest to be had, not asked again this session.
static uint8_t failedNext = 0;

static int fbl_Slot_Of(uint32_t teamId){
    for (int i = 0; i < FBL_CREST_SLOTS; i++) if (slots[i].teamId == teamId) return i;
    return -1;
}

static void fbl_Crest_Path(char* path, size_t size, uint32_t teamId){
    snprintf(path, size, FBL_CREST_DIR "/%u.c%d", (unsigned)teamId, FBL_CREST);
}

// From flash, else downloaded, shrunk and saved. Into the least recently drawn slot.
static void fbl_Load(uint32_t teamId){
    uint16_t* rgb = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * FBL_CREST * FBL_CREST, MALLOC_CAP_SPIRAM);
    uint8_t* alpha = (uint8_t*)heap_caps_malloc(FBL_CREST * FBL_CREST, MALLOC_CAP_SPIRAM);
    if (rgb == nullptr || alpha == nullptr){ heap_caps_free(rgb); heap_caps_free(alpha); return; }
    char path[48];
    fbl_Crest_Path(path, sizeof(path), teamId);
    bool loaded = false;
    const size_t bytes = sizeof(uint16_t) * FBL_CREST * FBL_CREST + FBL_CREST * FBL_CREST;
    if (LittleFS.exists(path)){
        File file = LittleFS.open(path, "r");
        if (file && file.size() == bytes){
            loaded = file.read((uint8_t*)rgb, sizeof(uint16_t) * FBL_CREST * FBL_CREST) == sizeof(uint16_t) * FBL_CREST * FBL_CREST
                  && file.read(alpha, FBL_CREST * FBL_CREST) == FBL_CREST * FBL_CREST;
        }
        if (file) file.close();
    }
    if (!loaded){
        char url[96];
        snprintf(url, sizeof(url), FBL_CREST_URL, (unsigned)teamId);
        uint8_t* png = nullptr;
        size_t length = 0;
        if (fbl_Api_Get_Image(url, &png, &length, 96 * 1024)){
            // Decoded by Pixlpal at its own size (up to 256 x 256), its transparency kept.
            int iw = 0, ih = 0;
            unsigned char* image = mtb_Kit_Decode_Image(png, length, 256, 256, &iw, &ih);
            const unsigned width = (unsigned)iw, height = (unsigned)ih;
            if (image){
                fbl_Crest_Downscale(image, width, height, rgb, alpha);
                loaded = true;
                if (mtb_Prepare_Flash_File_Path(path)){
                    File file = LittleFS.open(path, "w");
                    if (file){
                        file.write((const uint8_t*)rgb, sizeof(uint16_t) * FBL_CREST * FBL_CREST);
                        file.write(alpha, FBL_CREST * FBL_CREST);
                        file.close();
                    }
                }
            }
            free(image);
            heap_caps_free(png);
        }
    }
    if (loaded){
        xSemaphoreTake(crestLock, portMAX_DELAY);
        int slot = fbl_Slot_Of(teamId);
        if (slot < 0){
            slot = 0;
            for (int i = 1; i < FBL_CREST_SLOTS; i++) if (slots[i].used < slots[slot].used) slot = i;
        }
        slots[slot].teamId = teamId;
        slots[slot].used = ++useCounter;
        memcpy(slots[slot].rgb, rgb, sizeof(slots[slot].rgb));
        memcpy(slots[slot].alpha, alpha, sizeof(slots[slot].alpha));
        xSemaphoreGive(crestLock);
        version = version + 1;
    } else {
        failed[failedNext] = teamId;
        failedNext = (failedNext + 1) % (sizeof(failed) / sizeof(failed[0]));
        ESP_LOGW(TAG, "No crest for team %u", (unsigned)teamId);
    }
    heap_caps_free(rgb);
    heap_caps_free(alpha);
}

static void fbl_Crest_Step(void){
    {
        uint32_t teamId;
        if (xQueueReceive(wanted, &teamId, 0) != pdTRUE) return;
        xSemaphoreTake(crestLock, portMAX_DELAY);
        const bool have = fbl_Slot_Of(teamId) >= 0;
        xSemaphoreGive(crestLock);
        bool gaveUp = false;
        for (uint32_t f : failed) if (f == teamId) gaveUp = true;
        if (!have && !gaveUp) fbl_Load(teamId);
    }
}

static bool fbl_Crest_Start(void){
    if (crestTask_H != NULL) return true;
    if (slots == nullptr) slots = (Fbl_Crest_Slot_t*)heap_caps_calloc(FBL_CREST_SLOTS, sizeof(Fbl_Crest_Slot_t), MALLOC_CAP_SPIRAM);
    if (crestLock == NULL) crestLock = xSemaphoreCreateMutex();
    if (wanted == NULL) wanted = xQueueCreate(24, sizeof(uint32_t));
    if (slots == nullptr || crestLock == NULL || wanted == NULL) return false;
    pxl_compat_add_background(fbl_Crest_Step);
    crestTask_H = (TaskHandle_t)1;
    return true;
}

void fbl_Crest_Want(uint32_t teamId){
    if (teamId == 0 || !fbl_Crest_Start()) return;
    xSemaphoreTake(crestLock, portMAX_DELAY);
    const bool have = fbl_Slot_Of(teamId) >= 0;
    xSemaphoreGive(crestLock);
    if (!have) xQueueSend(wanted, &teamId, 0);
}

bool fbl_Crest_Draw(uint32_t teamId, int16_t x, int16_t y, uint16_t background){
    if (slots == nullptr || crestLock == NULL || teamId == 0) return false;
    static uint16_t frame[FBL_CREST * FBL_CREST];
    xSemaphoreTake(crestLock, portMAX_DELAY);
    const int slot = fbl_Slot_Of(teamId);
    if (slot >= 0){
        slots[slot].used = ++useCounter;
        const int br = (background >> 11) & 31, bg = (background >> 5) & 63, bb = background & 31;
        for (int i = 0; i < FBL_CREST * FBL_CREST; i++){
            const int a = slots[slot].alpha[i];
            const uint16_t c = slots[slot].rgb[i];
            const int r = (((c >> 11) & 31) * a + br * (255 - a)) / 255;
            const int g = (((c >> 5) & 63) * a + bg * (255 - a)) / 255;
            const int b = ((c & 31) * a + bb * (255 - a)) / 255;
            frame[i] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    xSemaphoreGive(crestLock);
    if (slot < 0) return false;
    mtb_Panel_Draw_FrameRGB565(x, y, FBL_CREST, FBL_CREST, frame);
    return true;
}

uint32_t fbl_Crest_Version(void){ return version; }
