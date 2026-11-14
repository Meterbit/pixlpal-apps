// Pixlpal compat: the owner's settings an app may read.
#pragma once
#include "pixlpal.h"
static inline bool mtb_Silent_Mode(void){ return pxl_silent() != 0; }
static inline bool mtb_Clock_24h(void){ return pxl_clock_24h() != 0; }
