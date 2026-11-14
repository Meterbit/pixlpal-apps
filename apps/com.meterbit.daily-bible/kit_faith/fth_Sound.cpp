// The FAITH apps' chime and bells, as an installed app plays them: notes through Pixlpal (pxl_sound), which plays
// them while the app keeps drawing. The firmware's version synthesised ringing bells as raw audio, which installed
// apps can't play; these are bell-like note sequences instead.
#include "fth_Sound.h"
#include "snd_Sound.h"
#include "pixlpal.h"

void fth_Chime(bool soft){
    static const Snd_Note_t CALL[] = {{523.25f, 260}, {659.25f, 260}, {783.99f, 260}, {659.25f, 260}, {523.25f, 520}};
    static const Snd_Note_t SOFT[] = {{659.25f, 700}};
    if (soft) snd_Play(SOFT, 1, 0.3f, false);
    else snd_Play(CALL, 5, 0.3f, false);
}

// Strokes in groups (the Angelus: 3, 3, 3, then 9), a pause between groups: at most 40 notes, as pxl_sound takes.
void fth_Bell(const int* groups, int count){
    Snd_Note_t n[SND_NOTES_MAX];
    int k = 0;
    for (int g = 0; g < count && k < SND_NOTES_MAX; g++){
        for (int s = 0; s < groups[g] && k < SND_NOTES_MAX; s++){ n[k].f = 392.0f; n[k].ms = 600; k++; }
        if (k < SND_NOTES_MAX){ n[k].f = 0; n[k].ms = 900; k++; }
    }
    if (k) snd_Play(n, k, 0.3f, false);
}

void fth_Angelus_Bell(void){
    static const int ANGELUS[] = {3, 3, 3, 9};
    fth_Bell(ANGELUS, 4);
}

bool fth_Sound_Play(int kind){
    if (snd_Busy()) return false;
    if (kind == FTH_SOUND_ANGELUS) fth_Angelus_Bell();
    else fth_Chime(kind == FTH_SOUND_SOFT);
    return true;
}

void fth_Sound_Stop(void){ snd_Stop(); }

bool fth_Sound_Busy(void){ return snd_Busy(); }
