// Calm Corner's model: feelings, breathing, grounding, stretches, the feelings log. No Arduino.
#include "clm_Model.h"
#include <stdio.h>
#include <string.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void clm_Settings_Default(Clm_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = CLM_SETTINGS_MAGIC;
    s.breathStyle = CLM_BREATH_AUTO;
    s.breaths = 5;
    s.tones = 1;
    s.lightsMinutes = 3;
    s.keepLog = 1;
    snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
}

void clm_Settings_Clamp(Clm_Settings_t& s){
    s.magic = CLM_SETTINGS_MAGIC;
    if (s.breathStyle >= CLM_BREATH_STYLES) s.breathStyle = CLM_BREATH_AUTO;
    if (s.breaths < 3 || s.breaths > 10) s.breaths = 5;
    s.tones = s.tones ? 1 : 0;
    if (s.lightsMinutes < 1 || s.lightsMinutes > 10) s.lightsMinutes = 3;
    s.keepLog = s.keepLog ? 1 : 0;
    s.alertFeelings &= (1u << CLM_FEELINGS) - 1;
    s.phoneAlerts = s.phoneAlerts ? 1 : 0;
    if (s.logCount > CLM_LOG) s.logCount = CLM_LOG;
    if (s.logHead >= CLM_LOG) s.logHead = 0;
    for (uint8_t i = 0; i < NTF_CHANNELS; i++) s.notify.on[i] = s.notify.on[i] ? 1 : 0;
    s.notify.ntfyServer[sizeof(s.notify.ntfyServer) - 1] = '\0';
    if (!s.notify.ntfyServer[0]) snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
    s.notify.ntfyTopic[sizeof(s.notify.ntfyTopic) - 1] = '\0';
    s.notify.ntfyToken[sizeof(s.notify.ntfyToken) - 1] = '\0';
    s.notify.telegramToken[sizeof(s.notify.telegramToken) - 1] = '\0';
    s.notify.telegramChat[sizeof(s.notify.telegramChat) - 1] = '\0';
    s.notify.whatsappPhone[sizeof(s.notify.whatsappPhone) - 1] = '\0';
    s.notify.whatsappKey[sizeof(s.notify.whatsappKey) - 1] = '\0';
}

const char* clm_Feeling_Name(uint8_t f){
    static const char* const N[CLM_FEELINGS] = {"HAPPY", "CALM", "EXCITED", "TIRED", "SAD", "WORRIED", "ANGRY"};
    return f < CLM_FEELINGS ? N[f] : "";
}

const char* clm_Feeling_Word(uint8_t f){
    static const char* const N[CLM_FEELINGS] = {"happy", "calm", "excited", "tired", "sad", "worried", "angry"};
    return f < CLM_FEELINGS ? N[f] : "";
}

uint16_t clm_Feeling_Color(uint8_t f){
    static const uint16_t C[CLM_FEELINGS] = {RGB(255, 200, 0), RGB(80, 200, 160), RGB(255, 120, 200), RGB(150, 130, 220),
                                             RGB(70, 130, 255), RGB(255, 150, 60), RGB(240, 50, 40)};
    return f < CLM_FEELINGS ? C[f] : RGB(150, 150, 150);
}

const char* clm_Answer(uint8_t f){
    switch (f){
        case CLM_HAPPY: return "YAY! HAPPY FEELINGS ARE GREAT. KEEP SMILING!";
        case CLM_CALM: return "LOVELY. A CALM BODY AND A CALM MIND.";
        case CLM_EXCITED: return "WOW, EXCITED! LET'S BREATHE SO YOUR BODY CAN SLOW DOWN.";
        case CLM_TIRED: return "TIRED IS OK. A SLOW STRETCH CAN HELP.";
        case CLM_SAD: return "IT'S OK TO FEEL SAD. A HUG OR A TALK CAN HELP TOO.";
        case CLM_WORRIED: return "WORRIES FEEL BIG. LET'S NOTICE WHAT IS AROUND YOU.";
        case CLM_ANGRY: return "IT'S OK TO FEEL ANGRY. LET'S BREATHE TOGETHER.";
        default: return "";
    }
}

uint8_t clm_Suggest(uint8_t f){
    switch (f){
        case CLM_TIRED: return CLM_STRETCH;
        case CLM_WORRIED: return CLM_GROUND;
        case CLM_HAPPY: case CLM_CALM: case CLM_SAD: return CLM_LIGHTS;
        default: return CLM_BREATHE;
    }
}

const char* clm_Activity_Name(uint8_t a){
    static const char* const N[CLM_ACTIVITIES] = {"BREATHE", "LOOK AROUND", "STRETCH", "CALM LIGHTS"};
    return a < CLM_ACTIVITIES ? N[a] : "";
}

void clm_Breath_Pattern(uint8_t style, uint8_t age, uint8_t s[4]){
    if (style == CLM_BREATH_AUTO) style = age >= 8 ? CLM_BREATH_BOX : CLM_BREATH_BALLOON;
    if (style == CLM_BREATH_BOX){ s[0] = 4; s[1] = 4; s[2] = 4; s[3] = 4; }
    else { s[0] = 3; s[1] = 1; s[2] = 4; s[3] = 1; }
}

void clm_Breath_At(uint8_t style, uint8_t age, uint8_t breaths, uint32_t ms, Clm_Breath_t& out){
    uint8_t p[4];
    clm_Breath_Pattern(style, age, p);
    const uint32_t cycle = (uint32_t)(p[0] + p[1] + p[2] + p[3]) * 1000u;
    const uint32_t n = ms / cycle;
    out = {};
    if (n >= breaths){ out.phase = CLM_DONE; out.breath = breaths; out.size = 0; return; }
    out.breath = (uint8_t)(n + 1);
    uint32_t t = ms % cycle;
    for (uint8_t ph = 0; ph < 4; ph++){
        const uint32_t len = p[ph] * 1000u;
        if (t < len){
            out.phase = ph;
            out.progress = (float)t / (float)len;
            out.secondsLeft = (uint8_t)((len - t + 999) / 1000);
            break;
        }
        t -= len;
    }
    // The shape: grows breathing in (eased), stays full, shrinks breathing out, stays small.
    const float e = out.progress * out.progress * (3 - 2 * out.progress);
    switch (out.phase){
        case CLM_IN: out.size = e; break;
        case CLM_HOLD_IN: out.size = 1; break;
        case CLM_OUT: out.size = 1 - e; break;
        default: out.size = 0; break;
    }
}

int clm_Ground_Steps(uint8_t age){ return age >= 7 ? 5 : 3; }

const char* clm_Ground_Text(int step){
    static const char* const T[5] = {"THINGS YOU CAN SEE", "THINGS YOU CAN HEAR", "THINGS YOU CAN TOUCH", "THINGS YOU CAN SMELL", "THING YOU CAN TASTE"};
    return step >= 0 && step < 5 ? T[step] : "";
}

uint8_t clm_Ground_Count(int step){ return (uint8_t)(step >= 0 && step < 5 ? 5 - step : 0); }

const char* clm_Stretch_Text(int i){
    static const char* const T[CLM_STRETCHES] = {"REACH UP HIGH TO THE SKY", "LEAN SLOWLY TO ONE SIDE", "NOW THE OTHER SIDE",
                                                 "ROLL YOUR SHOULDERS BACK", "SHAKE YOUR HANDS LOOSE", "GIVE YOURSELF A BIG HUG"};
    return i >= 0 && i < CLM_STRETCHES ? T[i] : "";
}

void clm_Log(Clm_Settings_t& s, const Clm_Entry_t& e){
    s.log[s.logHead] = e;
    s.logHead = (uint8_t)((s.logHead + 1) % CLM_LOG);
    if (s.logCount < CLM_LOG) s.logCount++;
}

bool clm_Log_Get(const Clm_Settings_t& s, int i, Clm_Entry_t& e){
    if (i < 0 || i >= s.logCount) return false;
    e = s.log[(s.logHead + CLM_LOG - 1 - i) % CLM_LOG];
    return true;
}

void clm_Log_Counts(const Clm_Settings_t& s, uint8_t child, uint32_t now, uint32_t days, uint16_t counts[CLM_FEELINGS]){
    memset(counts, 0, sizeof(uint16_t) * CLM_FEELINGS);
    Clm_Entry_t e;
    for (int i = 0; clm_Log_Get(s, i, e); i++){
        if (e.child != child || e.feeling >= CLM_FEELINGS || now - e.at > days * 86400u) continue;
        counts[e.feeling]++;
    }
}
