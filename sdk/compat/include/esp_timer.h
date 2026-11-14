#pragma once
#include <stdint.h>
#include "pixlpal.h"
static inline int64_t esp_timer_get_time(void){ return (int64_t)pxl_millis() * 1000; }
