// Business & Creator Counters' sounds, on the shared sound kit: a cash register for a sale (a drawer's thunk, then a
// bell ringing out), a fanfare for a milestone, and the words after them when the user wants them said.
#include "bcc_App.h"
#include "snd_Sound.h"

static const Snd_Note_t CHING[] = {{196.0f, 40}, {0, 30}, {2637.0f, 55}, {3520.0f, 65}, {4186.0f, 420}};
static const Snd_Note_t FANFARE[] = {{523.25f, 140}, {659.25f, 140}, {783.99f, 140}, {1046.5f, 360}, {0, 90}, {783.99f, 140}, {1046.5f, 560}};

static float level(uint8_t volume){ return 0.04f * (volume < 1 ? 1 : volume > 10 ? 10 : volume); }

void bcc_Sound_Sale(uint8_t volume, const char* speech){
    const int n = sizeof(CHING) / sizeof(CHING[0]);
    if (speech && speech[0]) snd_Say(speech, "en", CHING, n, level(volume), false);
    else snd_Play(CHING, n, level(volume), false);
}

void bcc_Sound_Milestone(uint8_t volume, const char* speech){
    const int n = sizeof(FANFARE) / sizeof(FANFARE[0]);
    if (speech && speech[0]) snd_Say(speech, "en", FANFARE, n, level(volume), false);
    else snd_Play(FANFARE, n, level(volume), false);
}

void bcc_Sound_Stop(void){ snd_Stop(); }
