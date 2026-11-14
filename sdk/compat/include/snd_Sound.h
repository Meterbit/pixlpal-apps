// Pixlpal compat: sounds and speech through pxl_sound and pxl_say.
#pragma once
#include <stdint.h>
#include <stdbool.h>
struct Snd_Note_t { float f; int ms; };
#define SND_NOTES_MAX 40
bool snd_Play(const Snd_Note_t* notes, int count, float level, bool square);
bool snd_Say(const char* text, const char* language, const Snd_Note_t* notes, int count, float level, bool square);
void snd_Stop(void);
bool snd_Busy(void);
