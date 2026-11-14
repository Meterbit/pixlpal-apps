// The kids apps' session: Kids Learning's children and rules, time and stars added to the child playing.
#include "kk_Kids.h"
#include "kid_Model.h"
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

uint32_t kk_Local_Day(uint32_t now){
    const time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    return (uint32_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

uint16_t kk_Local_Minute(uint32_t now){
    const time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    return (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
}

static void fill(Kk_Session_t& k, const Kid_Settings_t& s){
    k.count = 0;
    for (uint8_t i = 0; i < KID_CHILDREN && k.count < KK_CHILDREN; i++){
        if (!s.children[i].used) continue;
        k.real[k.count] = i;
        snprintf(k.names[k.count], sizeof(k.names[0]), "%s", s.children[i].name);
        k.ages[k.count] = s.children[i].age;
        k.stars[k.count] = s.children[i].stars;
        k.count++;
    }
    if (!k.count){                          // none set up: one child, Kids Learning's first
        k.real[0] = 0;
        snprintf(k.names[0], sizeof(k.names[0]), "%s", s.children[0].name);
        k.ages[0] = s.children[0].age;
        k.stars[0] = s.children[0].stars;
        k.count = 1;
    }
    k.sound = s.sound;
    k.speak = s.speak;
    k.language = s.language;
    k.bedtimeOn = s.bedtimeOn;
    k.bedFrom = s.bedFrom;
    k.bedTo = s.bedTo;
}

/** Reads the stored settings, adds what is pending to the child playing, and writes them back. */
static bool flush(Kk_Session_t& k, bool write){
    Kid_Settings_t* s = (Kid_Settings_t*)heap_caps_malloc(sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return false;
    memset(s, 0, sizeof(*s));
    mtb_Read_Nvs_Struct(KID_NVS_KEY, s, sizeof(*s));
    if (s->magic != KID_SETTINGS_MAGIC) kid_Settings_Default(*s);
    kid_Settings_Clamp(*s);
    if (k.child >= k.count) k.child = 0;
    const uint8_t who = k.loaded ? k.real[k.child] : s->active;
    Kid_Child_t& c = s->children[who < KID_CHILDREN ? who : 0];
    const uint32_t now = (uint32_t)time(nullptr);
    if (now > 1700000000u) kid_New_Day(c, kk_Local_Day(now));
    const uint32_t seconds = (uint32_t)c.todaySeconds + k.pendingSeconds;
    c.todaySeconds = (uint16_t)(seconds > 0xFFFF ? 0xFFFF : seconds);
    const uint32_t stars = (uint32_t)c.stars + k.pendingStars;
    c.stars = (uint16_t)(stars > 0xFFFF ? 0xFFFF : stars);
    const bool changed = k.pendingSeconds || k.pendingStars || s->active != who;
    s->active = who;
    if (write && changed) mtb_Write_Nvs_Struct(KID_NVS_KEY, s, sizeof(*s));
    k.pendingSeconds = k.pendingStars = 0;
    fill(k, *s);
    k.child = 0;
    for (uint8_t i = 0; i < k.count; i++) if (k.real[i] == who) k.child = i;
    k.left = kid_Seconds_Left(*s, c);
    k.savedMs = millis();
    k.loaded = true;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    return true;
}

bool kk_Begin(Kk_Session_t& k){
    memset(&k, 0, sizeof(k));
    if (flush(k, false)) return true;
    k.count = 1;
    snprintf(k.names[0], sizeof(k.names[0]), "Child");
    k.ages[0] = 7;
    k.left = 0xFFFF;
    k.sound = 1;
    k.loaded = true;
    return false;
}

void kk_End(Kk_Session_t& k){ if (k.loaded) flush(k, true); }

void kk_Select(Kk_Session_t& k, uint8_t child){
    if (child >= k.count) return;
    flush(k, true);                         // what is pending goes to the child before
    k.child = child;
    flush(k, true);                         // the new one is Kids Learning's active child too
}

void kk_Tick(Kk_Session_t& k, bool counts){
    if (counts){
        if (k.pendingSeconds < 0xFFFF) k.pendingSeconds++;
        if (k.left != 0xFFFF && k.left) k.left--;
    }
    if ((k.pendingSeconds || k.pendingStars) && millis() - k.savedMs >= 60000) flush(k, true);
}

void kk_Add_Stars(Kk_Session_t& k, uint16_t n){
    k.pendingStars = (uint16_t)(k.pendingStars + n);
    if (k.child < k.count) k.stars[k.child] = (uint16_t)(k.stars[k.child] + n);
}

static const Snd_Note_t YAY[] = {{784, 90}, {988, 90}, {1175, 160}};
static const Snd_Note_t OOPS[] = {{392, 140}, {330, 220}};
static const Snd_Note_t CLICK[] = {{1568, 25}};
static const Snd_Note_t FANFARE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 260}, {0, 60}, {784, 110}, {1047, 380}};
static const Snd_Note_t LEVEL_UP[] = {{659, 80}, {784, 80}, {988, 80}, {1319, 240}};
static const Snd_Note_t SOFT[] = {{523, 300}};

void kk_Sound(const Kk_Session_t& k, uint8_t sound){
    if (!k.sound) return;
    switch (sound){
        case KK_YAY: snd_Play(YAY, 3, 0.4f, false); break;
        case KK_OOPS: snd_Play(OOPS, 2, 0.35f, false); break;
        case KK_CLICK: snd_Play(CLICK, 1, 0.25f, true); break;
        case KK_FANFARE: snd_Play(FANFARE, 7, 0.45f, true); break;
        case KK_LEVEL_UP: snd_Play(LEVEL_UP, 4, 0.4f, true); break;
        default: snd_Play(SOFT, 1, 0.2f, false); break;
    }
}

void kk_Say(const Kk_Session_t& k, const char* text){
    if (!k.speak || !text || !text[0]) return;
    snd_Say(text, kid_Language_Code(k.language), nullptr, 0, 0.4f, false);
}
