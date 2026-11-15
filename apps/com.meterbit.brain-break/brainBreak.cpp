/**
 * Brain Break: a one to three minute movement break. A stick figure on the left does each activity (star jumps,
 * marching, arm circles, squats, balancing, dance and freeze...) while its name and a countdown show on the right and a
 * bar along the bottom shows the whole break. It starts with "GET READY", and ends with slow breathing (a circle that
 * grows and shrinks) and "WELL DONE!". Seated activities for small rooms.
 *
 * The figure is drawn from a pose (joint angles), each activity a function of time giving its pose. Knob: click to
 * start and to skip an activity, hold to stop, turn before the start to change the length. NVS "brkSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "brk_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define W           256
#define H           128
#define NVS_KEY     "brkSave"
#define SAVE_MAGIC  0x42524B01u
#define ACT_S       20
#define READY_S     3
#define CALM_S      24
#define DONE_S      5
#define PLAN_MAX    10
#define PI_F        3.14159265f

EXT_RAM_BSS_ATTR TaskHandle_t brainBreak_Task_H = NULL;
static void brainBreak_App_Task(void* dApplication);
static void brk_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* brainBreak_App;
Mtb_Applications* brainBreak_App_GetInstance(){
    if (!brainBreak_App){
        brainBreak_App = new Mtb_Applications_FullScreen(brainBreak_App_Task, &brainBreak_Task_H, "Brain Break", {0,0}, 8192);
        brainBreak_App->mtb_App_Set_Mobile_UI(BRK_UI_MANIFEST, BRK_UI_API, nullptr, brk_Values);
    }
    return brainBreak_App;
}
MTB_REGISTER_APP(brainBreak_App, 0, 0)        // the catalogue gives the app its number

struct Brk_Save_t { uint32_t magic; uint8_t minutes, seated, sound; };
static EXT_RAM_BSS_ATTR Brk_Save_t save;
static volatile bool appOpen = false, phoneChanged = false;
static volatile bool cmdStart = false, cmdStop = false, cmdSkip = false;

static void read_Save(Brk_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 2; s.sound = 1; }
    if (s.minutes < 1 || s.minutes > 3) s.minutes = 2;
}

// The figure ------------------------------------------------------------------------------------------------------------------------
/** Joint angles in degrees: an arm or leg at 0 hangs straight down, 90 is out to the side, 180 straight up. */
struct Pose_t {
    float la, ra, le, re;               // arms, and their elbows' extra bend
    float ll, rl;                       // legs
    float lk, rk;                       // knee lift, 0-1
    float crouch, bend, lean;           // hips down 0-1, bent forward 0-1, torso tilt (degrees)
    bool seated;
    float dx;                           // a shake or step sideways (pixels)
};

static void thick(float x0, float y0, float x1, float y1, uint16_t c){
    mtb_Panel_Draw_Line((int16_t)lroundf(x0), (int16_t)lroundf(y0), (int16_t)lroundf(x1), (int16_t)lroundf(y1), c);
    mtb_Panel_Draw_Line((int16_t)lroundf(x0 + 1), (int16_t)lroundf(y0), (int16_t)lroundf(x1 + 1), (int16_t)lroundf(y1), c);
    mtb_Panel_Draw_Line((int16_t)lroundf(x0), (int16_t)lroundf(y0 + 1), (int16_t)lroundf(x1), (int16_t)lroundf(y1 + 1), c);
}

static void limb(float x, float y, float angle, float extra, float l1, float l2, int side, uint16_t c, float* hx = nullptr, float* hy = nullptr){
    const float a = angle * PI_F / 180.0f * side;                              // side: -1 the figure's right (screen left)
    const float ex = x + sinf(a) * l1, ey = y + cosf(a) * l1;
    const float b = (angle + extra) * PI_F / 180.0f * side;
    const float hx2 = ex + sinf(b) * l2, hy2 = ey + cosf(b) * l2;
    thick(x, y, ex, ey, c);
    thick(ex, ey, hx2, hy2, c);
    if (hx){ *hx = hx2; *hy = hy2; }
}

static void figure(const Pose_t& p, float cx, float ground, uint16_t body, uint16_t skin){
    cx += p.dx;
    const float torso = 26.0f, upper = 13.0f, fore = 12.0f, thigh = 16.0f, shin = 16.0f;
    float hipY = ground - (thigh + shin) + p.crouch * 12.0f;
    if (p.seated){
        hipY = ground - shin - 4;
        mtb_Panel_Fill_Rect((int16_t)(cx - 18), (int16_t)(hipY + 3), (int16_t)(cx + 18), (int16_t)(hipY + 6), MTB_KIT_RGB(120, 80, 40));     // the chair
        mtb_Panel_Fill_Rect((int16_t)(cx - 18), (int16_t)(hipY + 6), (int16_t)(cx - 16), (int16_t)ground, MTB_KIT_RGB(120, 80, 40));
        mtb_Panel_Fill_Rect((int16_t)(cx + 16), (int16_t)(hipY + 6), (int16_t)(cx + 18), (int16_t)ground, MTB_KIT_RGB(120, 80, 40));
    }
    const float t = (p.lean) * PI_F / 180.0f;
    const float tl = torso * (1.0f - 0.55f * p.bend);
    const float sx = cx + sinf(t) * tl, sy = hipY - cosf(t) * tl;
    // Legs.
    for (int side = -1; side <= 1; side += 2){
        const float l = side < 0 ? p.rl : p.ll, k = side < 0 ? p.rk : p.lk;
        const float hx = cx + side * 3;
        if (p.seated){                                                          // thighs toward us (short), shins down
            const float kx = hx + side * 5, ky = hipY + 4 - k * 6;
            thick(hx, hipY, kx, ky, body);
            thick(kx, ky, kx, ground - k * 6, body);
            continue;
        }
        const float spread = l + p.crouch * 22.0f;
        const float a = spread * PI_F / 180.0f * side;
        const float tlen = thigh * (1.0f - 0.65f * k);
        const float kx = hx + sinf(a) * tlen, ky = hipY + cosf(a) * tlen;
        const float b = (spread - p.crouch * 40.0f) * PI_F / 180.0f * side;
        const float fx = kx + sinf(b) * shin * (1.0f - 0.2f * k), fy = ky + cosf(b) * shin * (1.0f - 0.2f * k);
        thick(hx, hipY, kx, ky, body);
        thick(kx, ky, fx, fy, body);
    }
    // Torso, arms, head.
    thick(cx, hipY, sx, sy, body);
    thick(sx - 6, sy + 2, sx + 6, sy + 2, body);                                // shoulders: raised arms pass beside the head
    limb(sx - 6, sy + 2, p.ra, p.re, upper, fore, -1, body);
    limb(sx + 6, sy + 2, p.la, p.le, upper, fore, 1, body);
    const float hy = sy - 9 + p.bend * 6;
    mtb_Panel_Fill_Circle((int16_t)lroundf(sx + sinf(t) * 9), (int16_t)lroundf(hy), 7, skin);
}

// The activities: each a pose at time t (seconds into it).
enum : uint8_t { A_JACKS = 0, A_MARCH, A_JOG, A_CIRCLES, A_TWIST, A_SQUAT, A_BALANCE, A_TOES, A_REACH, A_SHAKE, A_DANCE, A_PUNCH,
                 A_S_MARCH, A_S_ARMS, A_S_ROLL, A_S_TWIST, A_S_FINGERS, A_S_PUNCH, A_BREATHE, A_WAVE, A_COUNT };

struct Brk_Activity_t { const char* name; uint8_t anim; bool seated; };
static const Brk_Activity_t ACTS[] = {
    {"STAR JUMPS", A_JACKS, false}, {"MARCH ON THE SPOT", A_MARCH, false}, {"JOG ON THE SPOT", A_JOG, false},
    {"ARM CIRCLES", A_CIRCLES, false}, {"TWIST SIDE TO SIDE", A_TWIST, false}, {"SQUATS", A_SQUAT, false},
    {"STAND ON ONE LEG", A_BALANCE, false}, {"TOUCH YOUR TOES", A_TOES, false}, {"REACH FOR THE SKY", A_REACH, false},
    {"SHAKE IT ALL OUT", A_SHAKE, false}, {"DANCE AND FREEZE", A_DANCE, false}, {"PUNCH THE AIR", A_PUNCH, false},
    {"SEATED MARCH", A_S_MARCH, true}, {"ARM RAISES", A_S_ARMS, true}, {"SHOULDER ROLLS", A_S_ROLL, true},
    {"TWIST IN YOUR CHAIR", A_S_TWIST, true}, {"WIGGLE YOUR FINGERS", A_S_FINGERS, true}, {"PUNCH THE AIR", A_S_PUNCH, true},
};
static const int ACT_COUNT = (int)(sizeof(ACTS) / sizeof(ACTS[0]));

/** Dance and Freeze: the freezes, seconds into the dance. */
static bool frozen(float t){ return (t >= 4.5f && t < 6.5f) || (t >= 10.0f && t < 12.0f) || (t >= 15.5f && t < 17.5f); }

static Pose_t pose_Of(uint8_t anim, float t){
    Pose_t p = {};
    p.la = p.ra = 12; p.ll = p.rl = 4;
    const float s = sinf(t * 2 * PI_F);
    switch (anim){
        case A_JACKS: { const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 0.8f); p.la = p.ra = 15 + 155 * u; p.ll = p.rl = 3 + 22 * u; break; }
        case A_MARCH: case A_JOG: {
            const float w = sinf(t * 2 * PI_F / (anim == A_JOG ? 0.5f : 1.0f));
            p.lk = w > 0 ? w : 0; p.rk = w < 0 ? -w : 0;
            p.la = 15 + 25 * (w < 0 ? -w : 0); p.ra = 15 + 25 * (w > 0 ? w : 0); p.le = p.re = 40;
            break;
        }
        case A_CIRCLES: p.la = p.ra = 95 + 45 * sinf(t * 2 * PI_F / 1.2f); p.ll = p.rl = 12; break;
        case A_TWIST: { const float w = sinf(t * 2 * PI_F / 1.6f); p.la = 90 + 35 * w; p.ra = 90 - 35 * w; p.le = p.re = 50; p.lean = 6 * w; p.ll = p.rl = 12; break; }
        case A_SQUAT: { const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 2.2f); p.crouch = u; p.la = p.ra = 30 + 55 * u; p.ll = p.rl = 10; break; }
        case A_BALANCE: p.la = p.ra = 90; p.rk = 0.95f; p.rl = 8; p.lean = 3 * sinf(t * 1.7f); break;
        case A_TOES: { const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 3.0f); p.bend = u; p.la = p.ra = 10 + 150 * (1 - u); p.ll = p.rl = 8; break; }
        case A_REACH: p.la = 165 + 10 * s; p.ra = 165 - 10 * s; p.lean = 9 * sinf(t * 2 * PI_F / 3.0f); p.ll = p.rl = 8; break;
        case A_SHAKE: p.la = 35 + 20 * sinf(t * 37); p.ra = 35 + 20 * sinf(t * 41 + 1); p.ll = p.rl = 9 + 4 * sinf(t * 29); p.dx = 2.0f * sinf(t * 43); break;
        case A_DANCE:
            if (frozen(t)){ p.la = 160; p.ra = 70; p.re = 70; p.rk = 0.5f; p.lean = -6; break; }
            p.la = 140 + 30 * sinf(t * 5); p.ra = 40 + 30 * sinf(t * 5 + PI_F); p.lean = 8 * sinf(t * 2.5f);
            p.lk = sinf(t * 5) > 0.6f ? 0.4f : 0; p.rk = sinf(t * 5) < -0.6f ? 0.4f : 0; p.dx = 4 * sinf(t * 2.5f);
            break;
        case A_PUNCH: case A_S_PUNCH: {
            const bool left = fmodf(t, 0.8f) < 0.4f;
            p.la = left ? 95 : 55; p.le = left ? 0 : 100; p.ra = left ? 55 : 95; p.re = left ? 100 : 0;
            p.ll = p.rl = 14; p.seated = anim == A_S_PUNCH;
            break;
        }
        case A_S_MARCH: { p.seated = true; const float w = sinf(t * 2 * PI_F / 1.0f); p.lk = w > 0 ? w : 0; p.rk = w < 0 ? -w : 0; p.la = p.ra = 20; p.le = p.re = 50; break; }
        case A_S_ARMS: { p.seated = true; const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 2.5f); p.la = p.ra = 15 + 160 * u; break; }
        case A_S_ROLL: { p.seated = true; p.la = p.ra = 18 + 12 * sinf(t * 2 * PI_F / 1.5f); p.le = p.re = 30 + 20 * cosf(t * 2 * PI_F / 1.5f); break; }
        case A_S_TWIST: { p.seated = true; const float w = sinf(t * 2 * PI_F / 2.0f); p.la = 80 + 30 * w; p.ra = 80 - 30 * w; p.le = p.re = 70; p.lean = 7 * w; break; }
        case A_S_FINGERS: p.seated = true; p.la = 160 + 8 * sinf(t * 31); p.ra = 160 + 8 * sinf(t * 29 + 1); break;
        case A_BREATHE: { p.seated = true; const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 8.0f); p.la = p.ra = 20 + 30 * u; p.le = p.re = 30; break; }
        case A_WAVE: p.la = 15; p.ra = 150; p.re = -30 + 40 * sinf(t * 2 * PI_F / 0.7f); break;
        default: break;
    }
    return p;
}

// The break ------------------------------------------------------------------------------------------------------------------------
enum : uint8_t { B_IDLE = 0, B_READY, B_ACT, B_CALM, B_DONE };

struct Brk_Run_t {
    uint8_t state;
    uint8_t plan[PLAN_MAX], planN, at;  // the activities, and which is on
    uint32_t since;                     // millis() the part started
};
static Brk_Run_t run;

static void make_Plan(void){
    const int n = (save.minutes * 60 - CALM_S) / ACT_S;
    run.planN = (uint8_t)(n < 1 ? 1 : n > PLAN_MAX ? PLAN_MAX : n);
    int last = -1;
    bool danced = false;                                                        // one Dance and Freeze a break at most
    for (int i = 0; i < run.planN; i++){
        int pick;
        do pick = (int)(esp_random() % (uint32_t)ACT_COUNT);
        while (ACTS[pick].seated != (save.seated != 0) || pick == last || (danced && ACTS[pick].anim == A_DANCE));
        if (ACTS[pick].anim == A_DANCE) danced = true;
        run.plan[i] = (uint8_t)pick;
        last = pick;
    }
}

static void beep(int kind){
    if (!save.sound || mtb_Silent_Mode()) return;
    static const Snd_Note_t TICK[] = {{880, 60}};
    static const Snd_Note_t GO[] = {{1175, 220}};
    static const Snd_Note_t FREEZE[] = {{220, 180}};
    static const Snd_Note_t DONE[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 300}};
    if (kind == 0) snd_Play(TICK, 1, 0.2f, false);
    else if (kind == 1) snd_Play(GO, 1, 0.25f, false);
    else if (kind == 2) snd_Play(FREEZE, 1, 0.3f, true);
    else snd_Play(DONE, 4, 0.3f, false);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Minutes(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["minutes"], 1, 3, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.minutes = (uint8_t)v; phoneChanged = true; }
static void cmd_Seated(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["seated"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.seated = (uint8_t)v; phoneChanged = true; }
static void cmd_Sound(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } save.sound = (uint8_t)v; phoneChanged = true; }
static void cmd_Start(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } cmdStart = true; }
static void cmd_Stop(JsonDocument&){ cmdStop = true; }
static void cmd_Skip(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } cmdSkip = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobHold = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) knobClick = true; else if (b.type == BUTTON_PRESSED_LONG) knobHold = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(8, 10, 24), C_BODY = MTB_KIT_RGB(80, 200, 255), C_SKIN = MTB_KIT_RGB(255, 200, 120);
static const uint16_t C_GROUND = MTB_KIT_RGB(30, 40, 70);

static void title(const char* text, int y, uint16_t color){
    char lines[2][32];
    const int n = mtb_Kit_Wrap(text, PT_Sans_Narrow_Bold18x22, W - 120, &lines[0][0], sizeof(lines[0]), 2);
    for (int i = 0; i < n; i++) mtb_Kit_Text(114, y + i * 24, PT_Sans_Narrow_Bold18x22, color, lines[i], C_BG);
}

static void progress(uint32_t ms){
    // The whole break: ready, the activities, the breathing; the part on now lights as it goes.
    const int parts = run.planN + 1, x0 = 2, w = W - 4, y = 124;
    const int part = run.state == B_ACT ? run.at : run.state == B_CALM ? run.planN : run.state == B_DONE ? parts : -1;
    for (int i = 0; i < parts; i++){
        const int a = x0 + i * w / parts, b = x0 + (i + 1) * w / parts - 2;
        uint16_t c = i < part ? KIT_GREEN : KIT_LINE;
        mtb_Panel_Fill_Rect((int16_t)a, (int16_t)y, (int16_t)b, (int16_t)(y + 2), c);
        if (i == part){
            const float len = (i == run.planN ? CALM_S : ACT_S) * 1000.0f, f = (float)(ms - run.since) / len;
            mtb_Panel_Fill_Rect((int16_t)a, (int16_t)y, (int16_t)(a + (int)((b - a) * (f > 1 ? 1 : f))), (int16_t)(y + 2), KIT_GOLD);
        }
    }
}

static void draw(uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    mtb_Panel_Fill_Rect(0, 119, 110, 121, C_GROUND);
    const float t = (ms - run.since) / 1000.0f;
    char s[24];
    switch (run.state){
        case B_IDLE:
            figure(pose_Of(A_WAVE, ms / 1000.0f), 56, 118, C_BODY, C_SKIN);
            mtb_Kit_Text(114, 14, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "BRAIN", C_BG);
            mtb_Kit_Text(114, 46, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "BREAK", C_BG);
            snprintf(s, sizeof(s), "%u MIN%s", (unsigned)save.minutes, save.seated ? ", SEATED" : "");
            mtb_Kit_Text(114, 86, Terminal6x8, KIT_LABEL, s, C_BG);
            mtb_Kit_Text(114, 100, Terminal6x8, KIT_GREEN, "CLICK TO START", C_BG);
            break;
        case B_READY: {
            figure(pose_Of(A_REACH, t), 56, 118, C_BODY, C_SKIN);
            title("GET READY", 14, KIT_TEXT);
            snprintf(s, sizeof(s), "%d", READY_S - (int)t > 0 ? READY_S - (int)t : 1);
            mtb_Kit_Text(140, 46, MS_Gothic31x60, KIT_GOLD, s, C_BG);
            break;
        }
        case B_ACT: {
            const Brk_Activity_t& a = ACTS[run.plan[run.at]];
            const bool freeze = a.anim == A_DANCE && frozen(t);
            if (freeze && (ms / 200) % 2) mtb_Panel_Fill_Rect(112, 0, W - 1, 118, MTB_KIT_RGB(40, 60, 140));
            figure(pose_Of(a.anim, t), 56, 118, freeze ? KIT_TEXT : C_BODY, C_SKIN);
            if (freeze) mtb_Kit_Text(118, 30, PT_Sans_Narrow_Bold28x34, KIT_TEXT, "FREEZE!", (ms / 200) % 2 ? MTB_KIT_RGB(40, 60, 140) : C_BG);
            else title(a.name, 8, KIT_TEXT);
            const int left = ACT_S - (int)t;
            snprintf(s, sizeof(s), "%d", left > 0 ? left : 0);
            mtb_Kit_Text_Right(W - 6, 82, PT_Sans_Narrow_Bold28x34, left <= 3 ? KIT_GOLD : KIT_CYAN, s, C_BG);
            snprintf(s, sizeof(s), "%u OF %u", (unsigned)(run.at + 1), (unsigned)run.planN);
            mtb_Kit_Text(114, 106, Terminal4x6, KIT_DIM, s, C_BG);
            break;
        }
        case B_CALM: {
            // A circle that grows (breathe in, 4 s) and shrinks (out, 4 s), around a resting figure.
            const float u = 0.5f - 0.5f * cosf(t * 2 * PI_F / 8.0f);
            const bool in = fmodf(t, 8.0f) < 4.0f;
            mtb_Panel_Draw_Circle(56, 80, (int16_t)(18 + 30 * u), mtb_Kit_Dim(KIT_GREEN, 140));
            mtb_Panel_Draw_Circle(56, 80, (int16_t)(19 + 30 * u), mtb_Kit_Dim(KIT_GREEN, 90));
            figure(pose_Of(A_BREATHE, t), 56, 118, MTB_KIT_RGB(120, 220, 160), C_SKIN);
            title(in ? "BREATHE IN..." : "AND OUT...", 14, KIT_GREEN);
            mtb_Kit_Text(114, 68, Terminal6x8, KIT_LABEL, "SLOWLY", C_BG);
            break;
        }
        case B_DONE:
            figure(pose_Of(A_JACKS, 0.4f), 56, 118, KIT_GOLD, C_SKIN);
            mtb_Kit_Text(114, 20, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "WELL", C_BG);
            mtb_Kit_Text(114, 54, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "DONE!", C_BG);
            mtb_Kit_Text(114, 96, Terminal6x8, KIT_LABEL, "READY TO LEARN", C_BG);
            break;
    }
    if (run.state != B_IDLE) progress(ms);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void brainBreak_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Minutes, cmd_Seated, cmd_Sound, cmd_Start, cmd_Stop, cmd_Skip);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    memset(&run, 0, sizeof(run));
    turns = 0; knobClick = knobHold = false; cmdStart = cmdStop = cmdSkip = false;
    appOpen = true;
    uint32_t lastDraw = 0, pushedMs = 0;
    int lastSecond = -1;
    bool push = true, saveNow = false, wasFrozen = false;
    uint8_t lastState = B_IDLE;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; }
        const int t = turns;
        if (t){
            turns -= t;
            if (run.state == B_IDLE){ int m = save.minutes + t; save.minutes = (uint8_t)(m < 1 ? 1 : m > 3 ? 3 : m); push = saveNow = true; }
        }
        const bool start = cmdStart || (knobClick && (run.state == B_IDLE || run.state == B_DONE));
        const bool skip = cmdSkip || (knobClick && !start);
        const bool stop = cmdStop || knobHold;
        knobClick = knobHold = false; cmdStart = cmdStop = cmdSkip = false;
        if (stop){ run.state = B_IDLE; push = true; }
        else if (start){ make_Plan(); run.state = B_READY; run.at = 0; run.since = ms; lastSecond = -1; push = true; }
        else if (skip && run.state == B_ACT) run.since = ms - ACT_S * 1000;     // the activity ends now
        else if (skip && run.state == B_READY) run.since = ms - READY_S * 1000;

        // Moving on.
        const uint32_t in = ms - run.since;
        if (run.state == B_READY && in >= READY_S * 1000){ run.state = B_ACT; run.at = 0; run.since = ms; beep(1); }
        else if (run.state == B_ACT && in >= ACT_S * 1000){
            if (run.at + 1 < run.planN){ run.at++; run.since = ms; beep(1); }
            else { run.state = B_CALM; run.since = ms; beep(1); }
        } else if (run.state == B_CALM && in >= CALM_S * 1000){ run.state = B_DONE; run.since = ms; beep(3); }
        else if (run.state == B_DONE && in >= DONE_S * 1000) run.state = B_IDLE;
        if (run.state != lastState){ lastState = run.state; push = true; }

        // The last seconds tick; a freeze booms.
        const int second = (int)((ms - run.since) / 1000);
        if ((run.state == B_READY || run.state == B_ACT) && second != lastSecond){
            lastSecond = second;
            const int total = run.state == B_READY ? READY_S : ACT_S;
            if (total - second <= 3 && total - second > 0) beep(0);
        }
        if (run.state == B_ACT && ACTS[run.plan[run.at]].anim == A_DANCE){
            const bool f = frozen((ms - run.since) / 1000.0f);
            if (f && !wasFrozen) beep(2);
            wasFrozen = f;
        } else wasFrozen = false;

        if (ms - lastDraw >= 40){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(ms);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(15);
    }
    appOpen = false;
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void brk_Values(JsonObject values){
    Brk_Save_t s;
    if (appOpen) s = save; else read_Save(s);
    values["minutes"] = s.minutes;
    values["seated"] = s.seated;
    values["sound"] = s.sound;
    JsonArray rows = values["now"].to<JsonArray>();
    JsonObject r = rows.add<JsonObject>();
    r["label"] = "Brain break";
    char t[64];
    if (!appOpen){ r["value"] = "Open Brain Break on Pixlpal to start one"; r["state"] = "off"; return; }
    switch (run.state){
        case B_READY: r["value"] = "Get ready"; r["state"] = "info"; break;
        case B_ACT: snprintf(t, sizeof(t), "%s (%u of %u)", ACTS[run.plan[run.at]].name, (unsigned)(run.at + 1), (unsigned)run.planN); r["value"] = t; r["state"] = "ok"; break;
        case B_CALM: r["value"] = "Breathing, to finish"; r["state"] = "ok"; break;
        case B_DONE: r["value"] = "Done: ready to learn"; r["state"] = "ok"; break;
        default: snprintf(t, sizeof(t), "Ready: %u minute%s%s", (unsigned)s.minutes, s.minutes == 1 ? "" : "s", s.seated ? ", seated" : ""); r["value"] = t; r["state"] = "info"; break;
    }
}
