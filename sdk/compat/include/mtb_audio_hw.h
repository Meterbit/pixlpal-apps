// Pixlpal compat: raw audio (PCM) is the firmware's own; installed apps play notes (pxl_sound) and speech (pxl_say).
// mtb_Audio_Pcm_Begin says it isn't there, so an app falls back or stays quiet.
#pragma once
#include <stdint.h>
#include <stddef.h>
static inline bool mtb_Audio_Pcm_Begin(void){ return false; }
static inline size_t mtb_Audio_Pcm_Write(const int32_t*, size_t, uint32_t){ return 0; }
static inline void mtb_Audio_Pcm_End(void){}
