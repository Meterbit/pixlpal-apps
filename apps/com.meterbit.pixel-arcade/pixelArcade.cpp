/**
 * Pixel Arcade, ID {20,0} (GAMES): three quick games on the whole panel.
 *   - Worm: eat the fruit and grow; don't bite yourself (or, above Relaxed, the walls).
 *   - Brick Breaker: bounce the ball off the bat through a wall of bricks; three balls, each wall faster.
 *   - Comet Dodge: fly through a comet field and catch the stars; the field gets faster.
 * The knob (turn, click to pause, hold for the menu), a USB keyboard or mouse, or the phone as a game pad. Pixlpal
 * keeps each game's best score and how many times it was played (NVS "arcSave").
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_usb_hid.h"
#include "mtb_device_prefs.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "arc_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define RGB             MTB_KIT_RGB
#define W               256
#define H               128
#define BAR             10              // the score bar at the top
#define FRAME_MS        20
#define GAMES           3
#define NVS_KEY         "arcSave"
#define SAVE_MAGIC      0x41524301u

EXT_RAM_BSS_ATTR TaskHandle_t pixelArcade_Task_H = NULL;
static void pixelArcade_App_Task(void* dApplication);
static void arc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* pixelArcade_App;
Mtb_Applications* pixelArcade_App_GetInstance(){
    if (!pixelArcade_App){
        pixelArcade_App = new Mtb_Applications_FullScreen(pixelArcade_App_Task, &pixelArcade_Task_H, "Pixel Arcade", {20,0}, 8192);
        pixelArcade_App->mtb_App_Set_Mobile_UI(ARC_UI_MANIFEST, ARC_UI_API, nullptr, arc_Values);
    }
    return pixelArcade_App;
}
MTB_REGISTER_APP(pixelArcade_App, 20, 0)

static const char* const GAME_NAME[GAMES] = {"WORM", "BRICK BREAKER", "COMET DODGE"};
static const char* const GAME_TITLE[GAMES] = {"Worm", "Brick Breaker", "Comet Dodge"};

struct Arc_Save_t {
    uint32_t magic;
    uint32_t best[GAMES];
    uint32_t plays[GAMES];
    uint8_t speed;                      // 0 relaxed, 1 normal, 2 fast
    uint8_t sound;
    uint8_t game;
    uint8_t pad;
};

static Arc_Save_t save;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, forgetScores = false, startFromPhone = false;

// Input --------------------------------------------------------------------------------------------------------------
enum Btn_t : uint8_t { B_LEFT = 0, B_RIGHT, B_UP, B_DOWN, B_OK, B_BACK };
static volatile int turns = 0;
static volatile uint8_t clicks = 0;
static volatile bool holdRequested = false;
static volatile uint8_t padQueue[8];
static volatile uint8_t padHead = 0, padTail = 0;

static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) holdRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1) clicks = 1;
}
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// Phone commands (the BLE task) ----------------------------------------------------------------------------------------
static void cmd_Pad(JsonDocument& c){
    static const char* const NAMES[] = {"left", "right", "up", "down", "ok", "back"};
    const char* p = c["pad"] | "";
    if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; }
    for (uint8_t i = 0; i < 6; i++) if (!strcmp(p, NAMES[i])){
        portENTER_CRITICAL(&lock);
        const uint8_t next = (uint8_t)((padHead + 1) % sizeof(padQueue));
        if (next != padTail){ padQueue[padHead] = i; padHead = next; }
        portEXIT_CRITICAL(&lock);
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Game(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["game"], 0, GAMES - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.game = (uint8_t)v; portEXIT_CRITICAL(&lock);
    startFromPhone = true;
    phoneChanged = true;
}
static void cmd_Speed(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["speed"], 0, 2, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.speed = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); save.sound = (uint8_t)v; portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Forget(JsonDocument&){ forgetScores = true; phoneChanged = true; }

static void read_Save(Arc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.speed = 1; s.sound = 1; }
    if (s.speed > 2) s.speed = 1;
    if (s.game >= GAMES) s.game = 0;
    s.sound = s.sound ? 1 : 0;
}

// Small helpers ----------------------------------------------------------------------------------------------------------
static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) mtb_Panel_Fill_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void frame(int x0, int y0, int x1, int y1, uint16_t c){ mtb_Panel_Draw_Rect((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, c); }
static void dot(int x, int y, uint16_t c){ if (x >= 0 && y >= 0 && x < W && y < H) mtb_Panel_Draw_PixelRGB565((int16_t)x, (int16_t)y, c); }
static void disc(int x, int y, int r, uint16_t c){ mtb_Panel_Fill_Circle((int16_t)x, (int16_t)y, (int16_t)r, c); }
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c){ mtb_Panel_Fill_Triangle((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2, c); }
static uint32_t rnd(uint32_t n){ return n ? esp_random() % n : 0; }
static float clampf(float v, float lo, float hi){ return v < lo ? lo : v > hi ? hi : v; }

static void sound(const Snd_Note_t* n, int count){
    if (save.sound && !mtb_Silent_Mode()) snd_Play(n, count, 0.25f, true);
}
static const Snd_Note_t S_EAT[]   = {{880, 30}, {1320, 40}};
static const Snd_Note_t S_GOLD[]  = {{988, 50}, {1319, 50}, {1760, 80}};
static const Snd_Note_t S_BAT[]   = {{330, 25}};
static const Snd_Note_t S_WALL[]  = {{220, 15}};
static const Snd_Note_t S_LOST[]  = {{440, 90}, {330, 90}, {220, 160}};
static const Snd_Note_t S_OVER[]  = {{392, 120}, {311, 120}, {262, 120}, {196, 300}};
static const Snd_Note_t S_BEST[]  = {{523, 90}, {659, 90}, {784, 90}, {1047, 240}};
static const Snd_Note_t S_LEVEL[] = {{659, 70}, {784, 70}, {988, 70}, {1319, 160}};
static const Snd_Note_t S_START[] = {{523, 60}, {784, 90}};

// The games -----------------------------------------------------------------------------------------------------------------
#define WM_CELL   4
#define WM_COLS   (W / WM_CELL)                   // 64
#define WM_TOP    12
#define WM_ROWS   ((H - WM_TOP) / WM_CELL)        // 29
#define WM_N      (WM_COLS * WM_ROWS)

struct Worm_t {
    uint16_t body[WM_N];                // a ring of cells: body[head] is the head
    uint8_t occ[WM_N];
    int head, len, grow;
    int8_t dx, dy;
    int8_t q[3][2];                     // turns asked for, done one a step
    uint8_t qn;
    int fruit, gold;
    uint32_t goldUntil, nextStep;
    uint16_t eaten;
};

#define BR_COLS   14
#define BR_ROWS   6
#define BR_W      16
#define BR_H      5
#define BR_GAP    2
#define BR_LEFT   ((W - (BR_COLS * (BR_W + BR_GAP) - BR_GAP)) / 2)
#define BR_TOP    16
#define BAT_Y     120

struct Brick_t {
    uint8_t alive[BR_ROWS][BR_COLS];
    int left;
    float batX, ballX, ballY, vx, vy, speed;
    int batW;
    bool stuck;
    uint32_t launchAt;
    int lives, level;
};

#define CM_ROCKS  24
#define CM_SKY    40
struct Rock_t { float x, y, v; uint8_t r, star, used; };
struct Comet_t {
    float shipY, target;
    Rock_t rocks[CM_ROCKS];
    float skyX[CM_SKY]; uint8_t skyY[CM_SKY], skyV[CM_SKY];
    uint32_t nextSpawn, startedAt;
    float distance;
    uint16_t stars;
};

enum Mode_t : uint8_t { M_MENU = 0, M_PLAY, M_PAUSE, M_OVER };

struct Arc_t {
    uint8_t mode, game, choice;
    uint32_t score, ms, overAt;
    bool newBest;
    uint8_t held;                       // keyboard arrows held: bit per Btn_t
    char banner[24]; uint32_t bannerUntil;
    Worm_t worm;
    Brick_t brick;
    Comet_t comet;
};

static void set_Banner(Arc_t& a, const char* t){ snprintf(a.banner, sizeof(a.banner), "%s", t); a.bannerUntil = a.ms + 1500; }

// Worm ---------------------------------------------------------------------------------------------------------------------
static int worm_Free_Cell(Worm_t& w){
    for (int tries = 0; tries < 400; tries++){
        const int c = (int)rnd(WM_N);
        if (!w.occ[c] && c != w.fruit && c != w.gold) return c;
    }
    for (int c = 0; c < WM_N; c++) if (!w.occ[c] && c != w.fruit && c != w.gold) return c;
    return -1;
}

static void worm_Start(Arc_t& a){
    Worm_t& w = a.worm;
    memset(&w, 0, sizeof(w));
    w.len = 4;
    const int y = WM_ROWS / 2;
    for (int i = 0; i < w.len; i++){ const int c = y * WM_COLS + 10 + i; w.body[i] = (uint16_t)c; w.occ[c] = 1; }
    w.head = w.len - 1;
    w.dx = 1; w.dy = 0;
    w.gold = -1;
    w.fruit = worm_Free_Cell(w);
    w.nextStep = a.ms + 400;
}

static void worm_Turn(Worm_t& w, int dx, int dy){
    int8_t lx = w.dx, ly = w.dy;
    if (w.qn){ lx = w.q[w.qn - 1][0]; ly = w.q[w.qn - 1][1]; }
    if ((dx == -lx && dy == -ly) || (dx == lx && dy == ly) || w.qn >= 3) return;
    w.q[w.qn][0] = (int8_t)dx; w.q[w.qn][1] = (int8_t)dy; w.qn++;
}
static void worm_Turn_Relative(Worm_t& w, int dir){
    int8_t lx = w.dx, ly = w.dy;
    if (w.qn){ lx = w.q[w.qn - 1][0]; ly = w.q[w.qn - 1][1]; }
    if (dir > 0) worm_Turn(w, -ly, lx); else worm_Turn(w, ly, -lx);   // clockwise on the screen (y down), or back
}

static bool worm_Step(Arc_t& a){
    Worm_t& w = a.worm;
    if (w.qn){ w.dx = w.q[0][0]; w.dy = w.q[0][1]; memmove(w.q[0], w.q[1], sizeof(w.q[0]) * 2); w.qn--; }
    const int h = w.body[w.head];
    int x = h % WM_COLS + w.dx, y = h / WM_COLS + w.dy;
    if (x < 0 || y < 0 || x >= WM_COLS || y >= WM_ROWS){
        if (save.speed > 0) return false;                       // walls, above Relaxed
        x = (x + WM_COLS) % WM_COLS; y = (y + WM_ROWS) % WM_ROWS;
    }
    const int c = y * WM_COLS + x;
    if (w.grow > 0){ w.grow--; w.len++; }
    else { const int tail = (w.head - w.len + 1 + WM_N) % WM_N; w.occ[w.body[tail]] = 0; }
    if (w.occ[c]) return false;
    w.head = (w.head + 1) % WM_N;
    w.body[w.head] = (uint16_t)c;
    w.occ[c] = 1;
    if (c == w.fruit){
        w.grow += 2; w.eaten++; a.score += 10;
        w.fruit = worm_Free_Cell(w);
        sound(S_EAT, 2);
        if (w.gold < 0 && w.eaten % 5 == 0){ w.gold = worm_Free_Cell(w); w.goldUntil = a.ms + 6000; }
    } else if (c == w.gold){
        w.grow += 4; a.score += 50; w.gold = -1;
        sound(S_GOLD, 3);
        set_Banner(a, "+50");
    }
    if (w.gold >= 0 && a.ms > w.goldUntil) w.gold = -1;
    if (w.fruit < 0) return false;                              // the whole screen is worm
    return true;
}

static uint32_t worm_Interval(const Worm_t& w){
    static const int BASE[] = {150, 115, 85}, FLOOR[] = {80, 60, 42};
    const int t = BASE[save.speed] - w.len;
    return (uint32_t)(t < FLOOR[save.speed] ? FLOOR[save.speed] : t);
}

static void worm_Draw(const Arc_t& a){
    const Worm_t& w = a.worm;
    rect(0, WM_TOP - 1, W - 1, WM_TOP - 1, save.speed ? RGB(90, 60, 30) : RGB(30, 50, 30));
    auto cell = [](int c, int inset, uint16_t col){
        const int x = (c % WM_COLS) * WM_CELL, y = WM_TOP + (c / WM_COLS) * WM_CELL;
        rect(x + inset, y + inset, x + WM_CELL - 1 - inset, y + WM_CELL - 1 - inset, col);
    };
    for (int i = w.len - 1; i >= 0; i--){
        const int c = w.body[(w.head - i + WM_N) % WM_N];
        const float t = w.len > 1 ? (float)i / (float)(w.len - 1) : 0;
        cell(c, 0, mtb_Kit_Blend(RGB(120, 255, 90), RGB(20, 110, 40), t));
    }
    // Eyes, looking the way it goes.
    const int hc = w.body[w.head];
    const int hx = (hc % WM_COLS) * WM_CELL, hy = WM_TOP + (hc / WM_COLS) * WM_CELL;
    if (w.dx) { dot(hx + (w.dx > 0 ? 3 : 0), hy, BLACK); dot(hx + (w.dx > 0 ? 3 : 0), hy + 3, BLACK); }
    else      { dot(hx, hy + (w.dy > 0 ? 3 : 0), BLACK); dot(hx + 3, hy + (w.dy > 0 ? 3 : 0), BLACK); }
    if (w.fruit >= 0){ cell(w.fruit, 0, RGB(235, 40, 50)); dot((w.fruit % WM_COLS) * WM_CELL + 2, WM_TOP + (w.fruit / WM_COLS) * WM_CELL - 1, RGB(60, 200, 60)); }
    if (w.gold >= 0 && ((a.ms / 150) % 2 || w.goldUntil - a.ms > 2000)) cell(w.gold, 0, KIT_GOLD);
}

// Brick Breaker -----------------------------------------------------------------------------------------------------------
static void brick_Wall(Brick_t& b){
    memset(b.alive, 1, sizeof(b.alive));
    b.left = BR_ROWS * BR_COLS;
    // From level 3 on, some gaps make shapes (and fewer bricks to clear, but a faster ball).
    if (b.level >= 3) for (int r = 0; r < BR_ROWS; r++) for (int c = 0; c < BR_COLS; c++)
        if (((r + c + b.level) % 5) == 0){ b.alive[r][c] = 0; b.left--; }
}
static void brick_Serve(Arc_t& a){
    Brick_t& b = a.brick;
    b.stuck = true;
    b.launchAt = a.ms + 900;
    b.ballX = b.batX + b.batW / 2.0f;
    b.ballY = BAT_Y - 2;
}
static void brick_Start(Arc_t& a){
    Brick_t& b = a.brick;
    memset(&b, 0, sizeof(b));
    static const int BAT[] = {40, 32, 26};
    static const float SPEED[] = {0.11f, 0.14f, 0.17f};     // pixels a millisecond
    b.batW = BAT[save.speed];
    b.speed = SPEED[save.speed];
    b.batX = (W - b.batW) / 2.0f;
    b.lives = 3;
    b.level = 1;
    brick_Wall(b);
    brick_Serve(a);
}
static uint16_t brick_Color(int r){
    static const uint16_t C[BR_ROWS] = {RGB(235, 50, 60), RGB(255, 140, 0), RGB(255, 215, 0), RGB(60, 210, 90), RGB(60, 160, 255), RGB(170, 90, 255)};
    return C[r % BR_ROWS];
}

static void brick_Move_Bat(Brick_t& b, float dx){ b.batX = clampf(b.batX + dx, 0, (float)(W - b.batW)); }

/** False when the last ball is lost. */
static bool brick_Update(Arc_t& a, uint32_t dt){
    Brick_t& b = a.brick;
    if (a.held & (1 << B_LEFT)) brick_Move_Bat(b, -0.22f * dt);
    if (a.held & (1 << B_RIGHT)) brick_Move_Bat(b, 0.22f * dt);
    if (b.stuck){
        b.ballX = b.batX + b.batW / 2.0f;
        b.ballY = BAT_Y - 2;
        if (a.ms >= b.launchAt){
            b.stuck = false;
            const float ang = ((float)rnd(60) - 30.0f) * (float)M_PI / 180.0f;
            b.vx = b.speed * sinf(ang); b.vy = -b.speed * cosf(ang);
        }
        return true;
    }
    const int steps = 4;
    for (int s = 0; s < steps; s++){
        const float px = b.ballX, py = b.ballY;
        b.ballX += b.vx * dt / steps;
        b.ballY += b.vy * dt / steps;
        if (b.ballX < 1){ b.ballX = 1; b.vx = fabsf(b.vx); sound(S_WALL, 1); }
        if (b.ballX > W - 2){ b.ballX = W - 2; b.vx = -fabsf(b.vx); sound(S_WALL, 1); }
        if (b.ballY < BAR + 2){ b.ballY = BAR + 2; b.vy = fabsf(b.vy); sound(S_WALL, 1); }
        // The bat: where it is hit sets the angle (the middle straight up, the ends up to 60 degrees).
        if (b.vy > 0 && b.ballY + 1 >= BAT_Y && py + 1 <= BAT_Y + 2 && b.ballX >= b.batX - 2 && b.ballX <= b.batX + b.batW + 1){
            const float t = clampf((b.ballX - (b.batX + b.batW / 2.0f)) / (b.batW / 2.0f), -1, 1);
            const float ang = t * 60.0f * (float)M_PI / 180.0f;
            b.vx = b.speed * sinf(ang); b.vy = -b.speed * cosf(ang);
            b.ballY = BAT_Y - 2;
            sound(S_BAT, 1);
        }
        if (b.ballY > H + 2){
            b.lives--;
            sound(S_LOST, 3);
            if (b.lives <= 0) return false;
            set_Banner(a, "BALL LOST");
            brick_Serve(a);
            return true;
        }
        // A brick.
        const int cx = (int)floorf((b.ballX - BR_LEFT) / (BR_W + BR_GAP)), cy = (int)floorf((b.ballY - BR_TOP) / (BR_H + BR_GAP));
        if (cx >= 0 && cy >= 0 && cx < BR_COLS && cy < BR_ROWS && b.alive[cy][cx]){
            b.alive[cy][cx] = 0;
            b.left--;
            a.score += (uint32_t)(5 * (BR_ROWS - cy) * b.level);
            const int pcy = (int)floorf((py - BR_TOP) / (BR_H + BR_GAP));
            if (pcy != cy) b.vy = -b.vy; else b.vx = -b.vx;
            const Snd_Note_t n[] = {{(float)(500 + 120 * (BR_ROWS - cy)), 25}};
            sound(n, 1);
            if (b.left <= 0){
                b.level++;
                b.speed *= 1.12f;
                a.score += 100;
                brick_Wall(b);
                brick_Serve(a);
                sound(S_LEVEL, 4);
                char t[24]; snprintf(t, sizeof(t), "WALL %d", b.level);
                set_Banner(a, t);
                return true;
            }
            break;
        }
    }
    return true;
}

static void brick_Draw(const Arc_t& a){
    const Brick_t& b = a.brick;
    for (int r = 0; r < BR_ROWS; r++) for (int c = 0; c < BR_COLS; c++){
        if (!b.alive[r][c]) continue;
        const int x = BR_LEFT + c * (BR_W + BR_GAP), y = BR_TOP + r * (BR_H + BR_GAP);
        const uint16_t col = brick_Color(r);
        rect(x, y, x + BR_W - 1, y + BR_H - 1, col);
        rect(x, y, x + BR_W - 1, y, mtb_Kit_Blend(col, WHITE, 0.45f));
    }
    const int bx = (int)b.batX;
    rect(bx, BAT_Y, bx + b.batW - 1, BAT_Y + 2, RGB(210, 215, 230));
    rect(bx + 2, BAT_Y + 3, bx + b.batW - 3, BAT_Y + 3, RGB(110, 115, 140));
    rect((int)b.ballX - 1, (int)b.ballY - 1, (int)b.ballX + 1, (int)b.ballY + 1, WHITE);
}

// Comet Dodge --------------------------------------------------------------------------------------------------------------
#define SHIP_X0   12
#define SHIP_X1   26
static void comet_Start(Arc_t& a){
    Comet_t& c = a.comet;
    memset(&c, 0, sizeof(c));
    c.shipY = c.target = (BAR + H) / 2.0f;
    for (int i = 0; i < CM_SKY; i++){ c.skyX[i] = (float)rnd(W); c.skyY[i] = (uint8_t)(BAR + 1 + rnd(H - BAR - 1)); c.skyV[i] = (uint8_t)(1 + rnd(3)); }
    c.startedAt = a.ms;
    c.nextSpawn = a.ms + 600;
}
static float comet_Speed(const Arc_t& a){
    static const float BASE[] = {0.05f, 0.07f, 0.09f};
    const float t = (a.ms - a.comet.startedAt) / 1000.0f;
    return BASE[save.speed] * (1.0f + clampf(t / 60.0f, 0, 2.0f));    // up to three times as fast after two minutes
}

/** False on a crash. */
static bool comet_Update(Arc_t& a, uint32_t dt){
    Comet_t& c = a.comet;
    if (a.held & (1 << B_UP)) c.target -= 0.12f * dt;
    if (a.held & (1 << B_DOWN)) c.target += 0.12f * dt;
    c.target = clampf(c.target, BAR + 6, H - 6);
    const float d = c.target - c.shipY, step = 0.2f * dt;
    c.shipY += fabsf(d) < step ? d : (d > 0 ? step : -step);
    const float v = comet_Speed(a);
    c.distance += v * dt;
    for (int i = 0; i < CM_SKY; i++){
        c.skyX[i] -= v * dt * c.skyV[i] / 4.0f;
        if (c.skyX[i] < 0){ c.skyX[i] += W; c.skyY[i] = (uint8_t)(BAR + 1 + rnd(H - BAR - 1)); }
    }
    if (a.ms >= c.nextSpawn){
        const float t = (a.ms - c.startedAt) / 1000.0f;
        const int gap = 850 - (int)(t * 6);
        c.nextSpawn = a.ms + (uint32_t)(gap < 260 ? 260 : gap) + rnd(200);
        for (int i = 0; i < CM_ROCKS; i++){
            Rock_t& r = c.rocks[i];
            if (r.used) continue;
            r.used = 1;
            r.star = rnd(5) == 0;
            r.r = r.star ? 3 : (uint8_t)(2 + rnd(5));
            r.x = W + r.r + 2;
            r.y = (float)(BAR + 4 + rnd(H - BAR - 8));
            r.v = r.star ? 0.8f : 0.8f + rnd(60) / 100.0f;
            break;
        }
    }
    const float sy0 = c.shipY - 4, sy1 = c.shipY + 4;
    for (int i = 0; i < CM_ROCKS; i++){
        Rock_t& r = c.rocks[i];
        if (!r.used) continue;
        r.x -= v * r.v * dt;
        if (r.x < -12){ r.used = 0; continue; }
        const float nx = clampf(r.x, SHIP_X0, SHIP_X1), ny = clampf(r.y, sy0, sy1);
        const float dx = r.x - nx, dy = r.y - ny;
        const float rr = r.r + (r.star ? 2.0f : 0.0f);
        if (dx * dx + dy * dy < rr * rr){
            if (r.star){ r.used = 0; c.stars++; a.score += 10; sound(S_EAT, 2); continue; }
            return false;
        }
    }
    a.score = (uint32_t)(c.distance / 20.0f) + c.stars * 10u;
    return true;
}

static void comet_Draw(const Arc_t& a){
    const Comet_t& c = a.comet;
    for (int i = 0; i < CM_SKY; i++) dot((int)c.skyX[i], c.skyY[i], c.skyV[i] == 3 ? RGB(150, 150, 170) : c.skyV[i] == 2 ? RGB(80, 80, 100) : RGB(45, 45, 60));
    for (int i = 0; i < CM_ROCKS; i++){
        const Rock_t& r = c.rocks[i];
        if (!r.used) continue;
        const int x = (int)r.x, y = (int)r.y;
        if (r.star){
            const uint16_t g = KIT_GOLD;
            rect(x - 3, y, x + 3, y, g); rect(x, y - 3, x, y + 3, g);
            rect(x - 1, y - 1, x + 1, y + 1, g);
            continue;
        }
        for (int t = 1; t <= 10; t++) dot(x + r.r + t, y + ((t % 3) - 1) * (t > 5 ? 1 : 0), mtb_Kit_Dim(RGB(255, 150, 40), (uint8_t)(200 - t * 18)));
        disc(x, y, r.r, RGB(150, 120, 110));
        disc(x - 1, y - 1, r.r > 2 ? r.r - 2 : 1, RGB(200, 170, 150));
    }
    const int y = (int)c.shipY;
    tri(SHIP_X0, y - 4, SHIP_X0, y + 4, SHIP_X1, y, RGB(80, 200, 255));
    tri(SHIP_X0 + 2, y - 2, SHIP_X0 + 2, y + 2, SHIP_X0 + 9, y, RGB(220, 245, 255));
    const uint16_t flame = (a.ms / 60) % 2 ? RGB(255, 120, 0) : RGB(255, 220, 0);
    tri(SHIP_X0 - 1, y - 2, SHIP_X0 - 1, y + 2, SHIP_X0 - 5 - (int)((a.ms / 60) % 3), y, flame);
}

// The menu, the bar and the boxes -------------------------------------------------------------------------------------------
static void mini_Art(int g, int x, int y){
    if (g == 0){
        static const int8_t P[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {6, 3}, {6, 4}, {7, 4}, {8, 4}};
        const int n = sizeof(P) / sizeof(P[0]);
        for (int i = 0; i < n; i++) rect(x + 6 + P[i][0] * 6, y + 8 + P[i][1] * 6, x + 11 + P[i][0] * 6, y + 13 + P[i][1] * 6,
                                         mtb_Kit_Blend(RGB(20, 110, 40), RGB(120, 255, 90), (float)i / (n - 1)));
        rect(x + 6 + 10 * 6, y + 8 + 4 * 6, x + 11 + 10 * 6, y + 13 + 4 * 6, RGB(235, 40, 50));
    } else if (g == 1){
        for (int r = 0; r < 4; r++) for (int c = 0; c < 6; c++) if ((r + c) % 7 != 3) rect(x + 4 + c * 12, y + 4 + r * 6, x + 14 + c * 12, y + 8 + r * 6, brick_Color(r));
        rect(x + 34, y + 30, x + 36, y + 32, WHITE);
        rect(x + 24, y + 38, x + 48, y + 40, RGB(210, 215, 230));
    } else {
        dot(x + 10, y + 6, KIT_DIM); dot(x + 50, y + 12, KIT_DIM); dot(x + 66, y + 34, KIT_DIM); dot(x + 30, y + 38, KIT_DIM);
        tri(x + 6, y + 18, x + 6, y + 28, x + 22, y + 23, RGB(80, 200, 255));
        disc(x + 48, y + 16, 5, RGB(150, 120, 110)); for (int t = 1; t < 9; t++) dot(x + 53 + t, y + 16, mtb_Kit_Dim(RGB(255, 150, 40), (uint8_t)(200 - t * 20)));
        disc(x + 60, y + 32, 3, RGB(150, 120, 110));
        rect(x + 36, y + 30, x + 42, y + 30, KIT_GOLD); rect(x + 39, y + 27, x + 39, y + 33, KIT_GOLD);
    }
}

static void menu_Draw(const Arc_t& a){
    mtb_Panel_Fill_Screen(RGB(8, 8, 20));
    for (int i = 0; i < 40; i++) dot((int)((i * 97 + a.ms / 90) % W), (i * 37) % H, i % 3 ? RGB(40, 40, 70) : RGB(90, 90, 130));
    mtb_Kit_Text_Centre(W / 2, 2, Terminal8x12, KIT_GOLD, "PIXEL ARCADE", RGB(8, 8, 20));
    for (int g = 0; g < GAMES; g++){
        const int x = 6 + g * 83, y = 18;
        const bool on = a.choice == g;
        rect(x, y, x + 77, y + 90, on ? RGB(30, 30, 60) : RGB(16, 16, 32));
        frame(x, y, x + 77, y + 90, on ? KIT_GOLD : RGB(60, 60, 90));
        if (on && (a.ms / 400) % 2) frame(x + 1, y + 1, x + 76, y + 89, KIT_GOLD);
        mini_Art(g, x + 2, y + 6);
        const uint16_t bg = on ? RGB(30, 30, 60) : RGB(16, 16, 32);
        mtb_Kit_Text_Centre(x + 39, y + 58, Terminal6x8, on ? WHITE : KIT_LABEL, GAME_NAME[g], bg);
        char t[24];
        snprintf(t, sizeof(t), "BEST %lu", (unsigned long)save.best[g]);
        mtb_Kit_Text_Centre(x + 39, y + 72, Terminal6x8, on ? KIT_GOLD : KIT_DIM, t, bg);
    }
    static const char* const SPEED[] = {"RELAXED", "NORMAL", "FAST"};
    char hint[64];
    snprintf(hint, sizeof(hint), "TURN TO CHOOSE - CLICK TO PLAY - %s", SPEED[save.speed]);
    mtb_Kit_Text_Centre(W / 2, 116, Terminal4x6, KIT_LABEL, hint, RGB(8, 8, 20));
}

static void bar_Draw(const Arc_t& a){
    rect(0, 0, W - 1, BAR - 1, RGB(20, 20, 36));
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_LABEL, GAME_NAME[a.game], RGB(20, 20, 36));
    char t[32];
    snprintf(t, sizeof(t), "%lu", (unsigned long)a.score);
    mtb_Kit_Text_Centre(W / 2, 1, Terminal6x8, WHITE, t, RGB(20, 20, 36));
    snprintf(t, sizeof(t), "BEST %lu", (unsigned long)(a.score > save.best[a.game] ? a.score : save.best[a.game]));
    if (a.game == 1){
        for (int i = 0; i < a.brick.lives && i < 5; i++) disc(W - 6 - i * 7, 4, 2, WHITE);
        char lv[12]; snprintf(lv, sizeof(lv), "WALL %d", a.brick.level);
        mtb_Kit_Text_Right(W - 40, 1, Terminal6x8, KIT_LABEL, lv, RGB(20, 20, 36));
    } else if (a.game == 2){
        char s[16]; snprintf(s, sizeof(s), "* %u", a.comet.stars);
        mtb_Kit_Text_Right(W - 2, 1, Terminal6x8, KIT_GOLD, s, RGB(20, 20, 36));
        mtb_Kit_Text_Right(W - 40, 1, Terminal6x8, KIT_DIM, t, RGB(20, 20, 36));
    } else mtb_Kit_Text_Right(W - 2, 1, Terminal6x8, KIT_DIM, t, RGB(20, 20, 36));
}

static void box(const char* big, uint16_t bigColor, const char* line1, uint16_t c1, const char* line2){
    const uint16_t bg = RGB(16, 16, 40);
    rect(38, 34, W - 39, 98, bg);
    frame(38, 34, W - 39, 98, RGB(90, 90, 160));
    mtb_Kit_Text_Centre(W / 2, 42, Terminal8x12, bigColor, big, bg);
    if (line1) mtb_Kit_Text_Centre(W / 2, 62, Terminal6x8, c1, line1, bg);
    if (line2) mtb_Kit_Text_Centre(W / 2, 82, Terminal4x6, KIT_LABEL, line2, bg);
}

static void arc_Draw(const Arc_t& a){
    if (a.mode == M_MENU){ menu_Draw(a); return; }
    mtb_Panel_Fill_Screen(a.game == 2 ? RGB(2, 2, 10) : BLACK);
    if (a.game == 0) worm_Draw(a); else if (a.game == 1) brick_Draw(a); else comet_Draw(a);
    bar_Draw(a);
    if (a.mode == M_PLAY && a.ms < a.bannerUntil) mtb_Kit_Text_Centre(W / 2, 60, Terminal8x12, KIT_GOLD, a.banner, a.game == 2 ? RGB(2, 2, 10) : BLACK);
    if (a.mode == M_PAUSE) box("PAUSED", WHITE, nullptr, 0, "CLICK TO GO ON - HOLD FOR THE MENU");
    if (a.mode == M_OVER){
        char t[32];
        snprintf(t, sizeof(t), a.newBest ? "NEW BEST: %lu!" : "SCORE %lu", (unsigned long)a.score);
        box(a.game == 2 ? "CRASH!" : "GAME OVER", a.game == 2 ? KIT_ORANGE : KIT_RED, t, a.newBest ? KIT_GOLD : WHITE,
            a.ms - a.overAt < 800 ? "" : "CLICK TO PLAY AGAIN - HOLD FOR THE MENU");
    }
}

// What the controls do --------------------------------------------------------------------------------------------------------
static void game_Start(Arc_t& a, uint8_t g){
    a.game = g % GAMES;
    a.score = 0;
    a.newBest = false;
    a.held = 0;
    a.bannerUntil = 0;
    a.mode = M_PLAY;
    if (a.game == 0) worm_Start(a); else if (a.game == 1) brick_Start(a); else comet_Start(a);
    sound(S_START, 2);
}

static void game_Over(Arc_t& a){
    a.mode = M_OVER;
    a.overAt = a.ms;
    portENTER_CRITICAL(&lock);
    save.plays[a.game]++;
    a.newBest = a.score > save.best[a.game];
    if (a.newBest) save.best[a.game] = a.score;
    portEXIT_CRITICAL(&lock);
    if (a.newBest) sound(S_BEST, 4); else sound(S_OVER, 4);
}

static void to_Menu(Arc_t& a){ a.mode = M_MENU; a.choice = a.game; a.held = 0; }

static void press(Arc_t& a, uint8_t b){
    switch (a.mode){
        case M_MENU:
            if (b == B_LEFT || b == B_UP) a.choice = (uint8_t)((a.choice + GAMES - 1) % GAMES);
            else if (b == B_RIGHT || b == B_DOWN) a.choice = (uint8_t)((a.choice + 1) % GAMES);
            else if (b == B_OK){ save.game = a.choice; game_Start(a, a.choice); }
            break;
        case M_PLAY:
            if (b == B_OK) a.mode = M_PAUSE;
            else if (b == B_BACK) to_Menu(a);
            else if (a.game == 0){
                static const int8_t D[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                worm_Turn(a.worm, D[b][0], D[b][1]);
            } else if (a.game == 1){ if (b == B_LEFT) brick_Move_Bat(a.brick, -24); else if (b == B_RIGHT) brick_Move_Bat(a.brick, 24); }
            else { if (b == B_UP) a.comet.target -= 16; else if (b == B_DOWN) a.comet.target += 16; }
            break;
        case M_PAUSE:
            if (b == B_OK) a.mode = M_PLAY; else if (b == B_BACK) to_Menu(a);
            break;
        case M_OVER:
            if (a.ms - a.overAt < 800) break;              // a moment to see the score
            if (b == B_OK) game_Start(a, a.game); else if (b == B_BACK) to_Menu(a);
            break;
    }
}

static void turn(Arc_t& a, int dir){
    if (a.mode == M_MENU){ press(a, dir > 0 ? B_RIGHT : B_LEFT); return; }
    if (a.mode != M_PLAY) return;
    if (a.game == 0) worm_Turn_Relative(a.worm, dir);
    else if (a.game == 1) brick_Move_Bat(a.brick, dir * 14.0f);
    else a.comet.target += dir * 10.0f;
}

static int key_Btn(uint8_t key){
    switch (key){
        case 0x50: case 0x04: return B_LEFT;       // left arrow, A
        case 0x4F: case 0x07: return B_RIGHT;      // right arrow, D
        case 0x52: case 0x1A: return B_UP;         // up arrow, W
        case 0x51: case 0x16: return B_DOWN;       // down arrow, S
        case 0x28: case 0x2C: case 0x13: return B_OK;   // Enter, Space, P
        case 0x29: return B_BACK;                  // Esc
        default: return -1;
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------
static void pixelArcade_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Pad, cmd_Game, cmd_Speed, cmd_Sound, cmd_Forget);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clicks = 0; holdRequested = false; padHead = 0; padTail = 0;
    phoneChanged = false; forgetScores = false; startFromPhone = false;

    Arc_t* a = (Arc_t*)heap_caps_calloc(1, sizeof(Arc_t), MALLOC_CAP_SPIRAM);
    if (!a){ mtb_Delete_This_App(THIS_APP); return; }
    a->ms = millis();
    a->mode = M_MENU;
    a->choice = a->game = save.game;
    mtb_Usb_Input_Open(false);
    appOpen = true;

    uint32_t last = millis(), savedMs = 0;
    bool dirty = false, push = true;
    uint8_t lastMode = 0xFF;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t now = millis();
        if (now - last < FRAME_MS){ delay(2); continue; }
        uint32_t dt = now - last;
        last = now;
        if (dt > 50) dt = 50;
        a->ms = now;

        if (phoneChanged){
            phoneChanged = false;
            dirty = push = true;
            if (forgetScores){ forgetScores = false; portENTER_CRITICAL(&lock); memset(save.best, 0, sizeof(save.best)); memset(save.plays, 0, sizeof(save.plays)); portEXIT_CRITICAL(&lock); }
            if (startFromPhone){
                startFromPhone = false;
                if (a->mode == M_MENU || a->mode == M_OVER || a->mode == M_PAUSE){ a->choice = save.game; game_Start(*a, save.game); }
            }
        }

        // The knob, the phone's pad, then the keyboard and mouse.
        if (holdRequested){ holdRequested = false; if (a->mode != M_MENU) to_Menu(*a); }
        if (clicks){ clicks = 0; press(*a, B_OK); }
        const int t = turns;
        if (t){ turns -= t; for (int i = 0; i < (t > 0 ? t : -t) && i < 3; i++) turn(*a, t > 0 ? 1 : -1); }
        for (;;){
            uint8_t b = 0xFF;
            portENTER_CRITICAL(&lock);
            if (padTail != padHead){ b = padQueue[padTail]; padTail = (uint8_t)((padTail + 1) % sizeof(padQueue)); }
            portEXIT_CRITICAL(&lock);
            if (b == 0xFF) break;
            press(*a, b);
        }
        Mtb_Usb_Input_t in;
        int events = 0;
        while (events++ < 24 && mtb_Usb_Input_Read(in, 0)){
            if (in.type == MTB_USB_KEY_DOWN || in.type == MTB_USB_KEY_UP){
                const int b = key_Btn(in.key);
                if (b < 0) continue;
                if (in.type == MTB_USB_KEY_DOWN){ if (b <= B_DOWN) a->held |= (uint8_t)(1 << b); press(*a, (uint8_t)b); }
                else if (b <= B_DOWN) a->held &= (uint8_t)~(1 << b);
            } else if (in.type == MTB_USB_MOUSE_MOVE && a->mode == M_PLAY){
                if (a->game == 1) brick_Move_Bat(a->brick, in.dx * 0.6f);
                else if (a->game == 2) a->comet.target = clampf(a->comet.target + in.dy * 0.4f, BAR + 6, H - 6);
            } else if (in.type == MTB_USB_MOUSE_WHEEL){
                turn(*a, in.wheel > 0 ? -1 : 1);
            } else if (in.type == MTB_USB_MOUSE_BUTTONS){
                if (in.buttons & 1) press(*a, B_OK);
                else if (in.buttons & 2) press(*a, B_BACK);
            }
        }

        // The game.
        if (a->mode == M_PLAY){
            bool alive = true;
            if (a->game == 0){
                if (now >= a->worm.nextStep){ alive = worm_Step(*a); a->worm.nextStep = now + worm_Interval(a->worm); }
            } else if (a->game == 1) alive = brick_Update(*a, dt);
            else alive = comet_Update(*a, dt);
            if (!alive){ game_Over(*a); dirty = push = true; }
        }
        if (a->mode != lastMode){ lastMode = a->mode; push = true; }

        mtb_Panel_Begin_Update();
        arc_Draw(*a);
        mtb_Panel_End_Update();

        if (dirty && now - savedMs >= 3000){ dirty = false; savedMs = now; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push){ push = false; THIS_APP->mtb_App_Push_Mobile_Values(); }
    }
    appOpen = false;
    mtb_Usb_Input_Close();
    snd_Stop();
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    heap_caps_free(a);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ------------------------------------------------------------------------------------------------
static void arc_Values(JsonObject values){
    Arc_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); }
    else read_Save(s);
    JsonArray now = values["now"].to<JsonArray>();
    JsonObject o = now.add<JsonObject>();
    o["label"] = "Pixlpal";
    o["value"] = appOpen ? "Pixel Arcade is open: pick a game below, or use the knob" : "Pixel Arcade is not open";
    o["state"] = appOpen ? "ok" : "off";
    JsonArray rows = values["scores"].to<JsonArray>();
    char t[64];
    for (int g = 0; g < GAMES; g++){
        JsonObject r = rows.add<JsonObject>();
        r["label"] = GAME_TITLE[g];
        if (s.plays[g]) snprintf(t, sizeof(t), "Best %lu, played %lu time%s", (unsigned long)s.best[g], (unsigned long)s.plays[g], s.plays[g] == 1 ? "" : "s");
        else snprintf(t, sizeof(t), "Not played yet");
        r["value"] = t;
        r["state"] = s.plays[g] ? "info" : "off";
    }
    values["game"] = s.game;
    values["speed"] = s.speed;
    values["sound"] = s.sound;
}
