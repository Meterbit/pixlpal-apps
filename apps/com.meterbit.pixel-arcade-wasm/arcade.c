/**
 * Pixel Arcade as a WebAssembly app: the built-in Pixel Arcade {20,0} (App_Meterbit_Pixel_Arcade) ported to the
 * Pixlpal App API (../../sdk/pixlpal.h), as the runtime's speed test and the first example for developers.
 *   - Worm: eat the fruit and grow; don't bite yourself (or, above Relaxed, the walls).
 *   - Brick Breaker: bounce the ball off the bat through a wall of bricks; three balls, each wall faster.
 *   - Comet Dodge: fly through a comet field and catch the stars; the field gets faster.
 * The knob (turn, click to pause, hold for the menu), a USB keyboard or mouse, or the phone as a game pad. Best
 * scores and plays are kept with pxl_save_write.
 *
 * No C library: the few helpers it needs are at the top.
 */
#include "pixlpal.h"

#define W        PXL_W
#define H        PXL_H
#define BAR      10
#define GAMES    3
#define SAVE_MAGIC 0x41524302u
#define RGB      PXL_RGB
#define M_PI_F   3.14159265f

static const uint16_t WHITE = PXL_WHITE, BLACK = PXL_BLACK;
#define KIT_LABEL  RGB(150, 150, 150)
#define KIT_DIM    RGB(85, 85, 85)
#define KIT_GOLD   RGB(255, 196, 0)
#define KIT_RED    RGB(235, 40, 40)
#define KIT_ORANGE RGB(255, 140, 0)

// Helpers (no C library) ------------------------------------------------------------------------------------------------
static void zero(void* p, int n){ volatile uint8_t* b = (volatile uint8_t*)p; while (n-- > 0) *b++ = 0; }
static void fill_bytes(void* p, uint8_t v, int n){ volatile uint8_t* b = (volatile uint8_t*)p; while (n-- > 0) *b++ = v; }
static float floorf_(float x){ return __builtin_floorf(x); }
static float fabsf_(float x){ return __builtin_fabsf(x); }
// sin and cos for |x| <= pi/2 (the games use up to 60 degrees).
static float sinf_(float x){ const float x2 = x * x; return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72)))); }
static float cosf_(float x){ const float x2 = x * x; return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56))); }
static float clampf(float v, float lo, float hi){ return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rnd(uint32_t n){ return n ? pxl_random() % n : 0; }

// Text building: p moves along the buffer; the caller makes it big enough.
static char* put_s(char* p, const char* s){ while (*s) *p++ = *s++; *p = 0; return p; }
static char* put_u(char* p, uint32_t v){
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static int str_eq(const char* a, const char* b){ while (*a && *a == *b){ a++; b++; } return *a == *b; }

// Saved -------------------------------------------------------------------------------------------------------------------
typedef struct {
    uint32_t magic;
    uint32_t best[GAMES];
    uint32_t plays[GAMES];
    uint8_t speed, sound, game, pad;
} Save_t;
static Save_t save;

static void read_Save(void){
    zero(&save, sizeof(save));
    if (pxl_save_read(&save, sizeof(save)) != sizeof(save) || save.magic != SAVE_MAGIC){
        zero(&save, sizeof(save)); save.magic = SAVE_MAGIC; save.speed = 1; save.sound = 1;
    }
    if (save.speed > 2) save.speed = 1;
    if (save.game >= GAMES) save.game = 0;
    save.sound = save.sound ? 1 : 0;
}

static const char* const GAME_NAME[GAMES] = {"WORM", "BRICK BREAKER", "COMET DODGE"};
static const char* const GAME_TITLE[GAMES] = {"Worm", "Brick Breaker", "Comet Dodge"};

// Drawing helpers: corners, as in the built-in version.
static void rect(int x0, int y0, int x1, int y1, uint16_t c){ if (x1 >= x0 && y1 >= y0) pxl_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, c); }
static void frame(int x0, int y0, int x1, int y1, uint16_t c){ pxl_frame_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, c); }
static void dot(int x, int y, uint16_t c){ if (x >= 0 && y >= 0 && x < W && y < H) pxl_pixel(x, y, c); }
static void disc(int x, int y, int r, uint16_t c){ pxl_circle(x, y, r, c); }
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c){ pxl_triangle(x0, y0, x1, y1, x2, y2, c); }
static void text_c(int x, int y, int font, uint16_t c, const char* t, uint16_t bg){ pxl_text(x, y, font, PXL_CENTRE, c, bg, t); }
static void text_r(int x, int y, int font, uint16_t c, const char* t, uint16_t bg){ pxl_text(x, y, font, PXL_RIGHT, c, bg, t); }
static void text_l(int x, int y, int font, uint16_t c, const char* t, uint16_t bg){ pxl_text(x, y, font, PXL_LEFT, c, bg, t); }

// Sound --------------------------------------------------------------------------------------------------------------------
static void sound(const pxl_note_t* n, int count){ if (save.sound) pxl_sound(n, count); }
static const pxl_note_t S_EAT[]   = {{880, 30}, {1320, 40}};
static const pxl_note_t S_GOLD[]  = {{988, 50}, {1319, 50}, {1760, 80}};
static const pxl_note_t S_BAT[]   = {{330, 25}};
static const pxl_note_t S_WALL[]  = {{220, 15}};
static const pxl_note_t S_LOST[]  = {{440, 90}, {330, 90}, {220, 160}};
static const pxl_note_t S_OVER[]  = {{392, 120}, {311, 120}, {262, 120}, {196, 300}};
static const pxl_note_t S_BEST[]  = {{523, 90}, {659, 90}, {784, 90}, {1047, 240}};
static const pxl_note_t S_LEVEL[] = {{659, 70}, {784, 70}, {988, 70}, {1319, 160}};
static const pxl_note_t S_START[] = {{523, 60}, {784, 90}};

// The games -----------------------------------------------------------------------------------------------------------------
enum { B_LEFT = 0, B_RIGHT, B_UP, B_DOWN, B_OK, B_BACK };

#define WM_CELL   4
#define WM_COLS   (W / WM_CELL)
#define WM_TOP    12
#define WM_ROWS   ((H - WM_TOP) / WM_CELL)
#define WM_N      (WM_COLS * WM_ROWS)

typedef struct {
    uint16_t body[WM_N];
    uint8_t occ[WM_N];
    int head, len, grow;
    int8_t dx, dy;
    int8_t q[3][2];
    uint8_t qn;
    int fruit, gold;
    uint32_t goldUntil, nextStep;
    uint16_t eaten;
} Worm_t;

#define BR_COLS   14
#define BR_ROWS   6
#define BR_W      16
#define BR_H      5
#define BR_GAP    2
#define BR_LEFT   ((W - (BR_COLS * (BR_W + BR_GAP) - BR_GAP)) / 2)
#define BR_TOP    16
#define BAT_Y     120

typedef struct {
    uint8_t alive[BR_ROWS][BR_COLS];
    int left;
    float batX, ballX, ballY, vx, vy, speed;
    int batW;
    int stuck;
    uint32_t launchAt;
    int lives, level;
} Brick_t;

#define CM_ROCKS  24
#define CM_SKY    40
typedef struct { float x, y, v; uint8_t r, star, used; } Rock_t;
typedef struct {
    float shipY, target;
    Rock_t rocks[CM_ROCKS];
    float skyX[CM_SKY]; uint8_t skyY[CM_SKY], skyV[CM_SKY];
    uint32_t nextSpawn, startedAt;
    float distance;
    uint16_t stars;
} Comet_t;

enum { M_MENU = 0, M_PLAY, M_PAUSE, M_OVER };

typedef struct {
    uint8_t mode, game, choice;
    uint32_t score, ms, overAt;
    int newBest;
    uint8_t held;
    char banner[24]; uint32_t bannerUntil;
    Worm_t worm;
    Brick_t brick;
    Comet_t comet;
} Arc_t;

static Arc_t A;
static int dirty, push;
static uint32_t savedMs;

static void set_Banner(Arc_t* a, const char* t){ char* p = a->banner; int i = 0; while (t[i] && i < 23){ p[i] = t[i]; i++; } p[i] = 0; a->bannerUntil = a->ms + 1500; }

// Worm ---------------------------------------------------------------------------------------------------------------------
static int worm_Free_Cell(Worm_t* w){
    for (int tries = 0; tries < 400; tries++){
        const int c = (int)rnd(WM_N);
        if (!w->occ[c] && c != w->fruit && c != w->gold) return c;
    }
    for (int c = 0; c < WM_N; c++) if (!w->occ[c] && c != w->fruit && c != w->gold) return c;
    return -1;
}

static void worm_Start(Arc_t* a){
    Worm_t* w = &a->worm;
    zero(w, sizeof(*w));
    w->len = 4;
    const int y = WM_ROWS / 2;
    for (int i = 0; i < w->len; i++){ const int c = y * WM_COLS + 10 + i; w->body[i] = (uint16_t)c; w->occ[c] = 1; }
    w->head = w->len - 1;
    w->dx = 1; w->dy = 0;
    w->gold = -1;
    w->fruit = worm_Free_Cell(w);
    w->nextStep = a->ms + 400;
}

static void worm_Turn(Worm_t* w, int dx, int dy){
    int8_t lx = w->dx, ly = w->dy;
    if (w->qn){ lx = w->q[w->qn - 1][0]; ly = w->q[w->qn - 1][1]; }
    if ((dx == -lx && dy == -ly) || (dx == lx && dy == ly) || w->qn >= 3) return;
    w->q[w->qn][0] = (int8_t)dx; w->q[w->qn][1] = (int8_t)dy; w->qn++;
}
static void worm_Turn_Relative(Worm_t* w, int dir){
    int8_t lx = w->dx, ly = w->dy;
    if (w->qn){ lx = w->q[w->qn - 1][0]; ly = w->q[w->qn - 1][1]; }
    if (dir > 0) worm_Turn(w, -ly, lx); else worm_Turn(w, ly, -lx);
}

static int worm_Step(Arc_t* a){
    Worm_t* w = &a->worm;
    if (w->qn){
        w->dx = w->q[0][0]; w->dy = w->q[0][1];
        w->q[0][0] = w->q[1][0]; w->q[0][1] = w->q[1][1]; w->q[1][0] = w->q[2][0]; w->q[1][1] = w->q[2][1];
        w->qn--;
    }
    const int h = w->body[w->head];
    int x = h % WM_COLS + w->dx, y = h / WM_COLS + w->dy;
    if (x < 0 || y < 0 || x >= WM_COLS || y >= WM_ROWS){
        if (save.speed > 0) return 0;
        x = (x + WM_COLS) % WM_COLS; y = (y + WM_ROWS) % WM_ROWS;
    }
    const int c = y * WM_COLS + x;
    if (w->grow > 0){ w->grow--; w->len++; }
    else { const int tail = (w->head - w->len + 1 + WM_N) % WM_N; w->occ[w->body[tail]] = 0; }
    if (w->occ[c]) return 0;
    w->head = (w->head + 1) % WM_N;
    w->body[w->head] = (uint16_t)c;
    w->occ[c] = 1;
    if (c == w->fruit){
        w->grow += 2; w->eaten++; a->score += 10;
        w->fruit = worm_Free_Cell(w);
        sound(S_EAT, 2);
        if (w->gold < 0 && w->eaten % 5 == 0){ w->gold = worm_Free_Cell(w); w->goldUntil = a->ms + 6000; }
    } else if (c == w->gold){
        w->grow += 4; a->score += 50; w->gold = -1;
        sound(S_GOLD, 3);
        set_Banner(a, "+50");
    }
    if (w->gold >= 0 && a->ms > w->goldUntil) w->gold = -1;
    if (w->fruit < 0) return 0;
    return 1;
}

static uint32_t worm_Interval(const Worm_t* w){
    static const int BASE[] = {150, 115, 85}, FLOOR[] = {80, 60, 42};
    const int t = BASE[save.speed] - w->len;
    return (uint32_t)(t < FLOOR[save.speed] ? FLOOR[save.speed] : t);
}

static void worm_Cell(int c, uint16_t col){
    const int x = (c % WM_COLS) * WM_CELL, y = WM_TOP + (c / WM_COLS) * WM_CELL;
    rect(x, y, x + WM_CELL - 1, y + WM_CELL - 1, col);
}

static void worm_Draw(const Arc_t* a){
    const Worm_t* w = &a->worm;
    rect(0, WM_TOP - 1, W - 1, WM_TOP - 1, save.speed ? RGB(90, 60, 30) : RGB(30, 50, 30));
    for (int i = w->len - 1; i >= 0; i--){
        const int c = w->body[(w->head - i + WM_N) % WM_N];
        const float t = w->len > 1 ? (float)i / (float)(w->len - 1) : 0;
        worm_Cell(c, pxl_blend(RGB(120, 255, 90), RGB(20, 110, 40), t));
    }
    const int hc = w->body[w->head];
    const int hx = (hc % WM_COLS) * WM_CELL, hy = WM_TOP + (hc / WM_COLS) * WM_CELL;
    if (w->dx) { dot(hx + (w->dx > 0 ? 3 : 0), hy, BLACK); dot(hx + (w->dx > 0 ? 3 : 0), hy + 3, BLACK); }
    else       { dot(hx, hy + (w->dy > 0 ? 3 : 0), BLACK); dot(hx + 3, hy + (w->dy > 0 ? 3 : 0), BLACK); }
    if (w->fruit >= 0){ worm_Cell(w->fruit, RGB(235, 40, 50)); dot((w->fruit % WM_COLS) * WM_CELL + 2, WM_TOP + (w->fruit / WM_COLS) * WM_CELL - 1, RGB(60, 200, 60)); }
    if (w->gold >= 0 && ((a->ms / 150) % 2 || w->goldUntil - a->ms > 2000)) worm_Cell(w->gold, KIT_GOLD);
}

// Brick Breaker -----------------------------------------------------------------------------------------------------------
static void brick_Wall(Brick_t* b){
    fill_bytes(b->alive, 1, sizeof(b->alive));
    b->left = BR_ROWS * BR_COLS;
    if (b->level >= 3) for (int r = 0; r < BR_ROWS; r++) for (int c = 0; c < BR_COLS; c++)
        if (((r + c + b->level) % 5) == 0){ b->alive[r][c] = 0; b->left--; }
}
static void brick_Serve(Arc_t* a){
    Brick_t* b = &a->brick;
    b->stuck = 1;
    b->launchAt = a->ms + 900;
    b->ballX = b->batX + b->batW / 2.0f;
    b->ballY = BAT_Y - 2;
}
static void brick_Start(Arc_t* a){
    Brick_t* b = &a->brick;
    zero(b, sizeof(*b));
    static const int BAT[] = {40, 32, 26};
    static const float SPEED[] = {0.11f, 0.14f, 0.17f};
    b->batW = BAT[save.speed];
    b->speed = SPEED[save.speed];
    b->batX = (W - b->batW) / 2.0f;
    b->lives = 3;
    b->level = 1;
    brick_Wall(b);
    brick_Serve(a);
}
static uint16_t brick_Color(int r){
    static const uint16_t C[BR_ROWS] = {RGB(235, 50, 60), RGB(255, 140, 0), RGB(255, 215, 0), RGB(60, 210, 90), RGB(60, 160, 255), RGB(170, 90, 255)};
    return C[r % BR_ROWS];
}
static void brick_Move_Bat(Brick_t* b, float dx){ b->batX = clampf(b->batX + dx, 0, (float)(W - b->batW)); }

static int brick_Update(Arc_t* a, uint32_t dt){
    Brick_t* b = &a->brick;
    if (a->held & (1 << B_LEFT)) brick_Move_Bat(b, -0.22f * dt);
    if (a->held & (1 << B_RIGHT)) brick_Move_Bat(b, 0.22f * dt);
    if (b->stuck){
        b->ballX = b->batX + b->batW / 2.0f;
        b->ballY = BAT_Y - 2;
        if (a->ms >= b->launchAt){
            b->stuck = 0;
            const float ang = ((float)rnd(60) - 30.0f) * M_PI_F / 180.0f;
            b->vx = b->speed * sinf_(ang); b->vy = -b->speed * cosf_(ang);
        }
        return 1;
    }
    const int steps = 4;
    for (int s = 0; s < steps; s++){
        const float py = b->ballY;
        b->ballX += b->vx * dt / steps;
        b->ballY += b->vy * dt / steps;
        if (b->ballX < 1){ b->ballX = 1; b->vx = fabsf_(b->vx); sound(S_WALL, 1); }
        if (b->ballX > W - 2){ b->ballX = W - 2; b->vx = -fabsf_(b->vx); sound(S_WALL, 1); }
        if (b->ballY < BAR + 2){ b->ballY = BAR + 2; b->vy = fabsf_(b->vy); sound(S_WALL, 1); }
        if (b->vy > 0 && b->ballY + 1 >= BAT_Y && py + 1 <= BAT_Y + 2 && b->ballX >= b->batX - 2 && b->ballX <= b->batX + b->batW + 1){
            const float t = clampf((b->ballX - (b->batX + b->batW / 2.0f)) / (b->batW / 2.0f), -1, 1);
            const float ang = t * 60.0f * M_PI_F / 180.0f;
            b->vx = b->speed * sinf_(ang); b->vy = -b->speed * cosf_(ang);
            b->ballY = BAT_Y - 2;
            sound(S_BAT, 1);
        }
        if (b->ballY > H + 2){
            b->lives--;
            sound(S_LOST, 3);
            if (b->lives <= 0) return 0;
            set_Banner(a, "BALL LOST");
            brick_Serve(a);
            return 1;
        }
        const int cx = (int)floorf_((b->ballX - BR_LEFT) / (BR_W + BR_GAP)), cy = (int)floorf_((b->ballY - BR_TOP) / (BR_H + BR_GAP));
        if (cx >= 0 && cy >= 0 && cx < BR_COLS && cy < BR_ROWS && b->alive[cy][cx]){
            b->alive[cy][cx] = 0;
            b->left--;
            a->score += (uint32_t)(5 * (BR_ROWS - cy) * b->level);
            const int pcy = (int)floorf_((py - BR_TOP) / (BR_H + BR_GAP));
            if (pcy != cy) b->vy = -b->vy; else b->vx = -b->vx;
            const pxl_note_t n[] = {{500 + 120 * (BR_ROWS - cy), 25}};
            sound(n, 1);
            if (b->left <= 0){
                b->level++;
                b->speed *= 1.12f;
                a->score += 100;
                brick_Wall(b);
                brick_Serve(a);
                sound(S_LEVEL, 4);
                char t[24]; put_u(put_s(t, "WALL "), (uint32_t)b->level);
                set_Banner(a, t);
                return 1;
            }
            break;
        }
    }
    return 1;
}

static void brick_Draw(const Arc_t* a){
    const Brick_t* b = &a->brick;
    for (int r = 0; r < BR_ROWS; r++) for (int c = 0; c < BR_COLS; c++){
        if (!b->alive[r][c]) continue;
        const int x = BR_LEFT + c * (BR_W + BR_GAP), y = BR_TOP + r * (BR_H + BR_GAP);
        const uint16_t col = brick_Color(r);
        rect(x, y, x + BR_W - 1, y + BR_H - 1, col);
        rect(x, y, x + BR_W - 1, y, pxl_blend(col, WHITE, 0.45f));
    }
    const int bx = (int)b->batX;
    rect(bx, BAT_Y, bx + b->batW - 1, BAT_Y + 2, RGB(210, 215, 230));
    rect(bx + 2, BAT_Y + 3, bx + b->batW - 3, BAT_Y + 3, RGB(110, 115, 140));
    rect((int)b->ballX - 1, (int)b->ballY - 1, (int)b->ballX + 1, (int)b->ballY + 1, WHITE);
}

// Comet Dodge --------------------------------------------------------------------------------------------------------------
#define SHIP_X0   12
#define SHIP_X1   26
static void comet_Start(Arc_t* a){
    Comet_t* c = &a->comet;
    zero(c, sizeof(*c));
    c->shipY = c->target = (BAR + H) / 2.0f;
    for (int i = 0; i < CM_SKY; i++){ c->skyX[i] = (float)rnd(W); c->skyY[i] = (uint8_t)(BAR + 1 + rnd(H - BAR - 1)); c->skyV[i] = (uint8_t)(1 + rnd(3)); }
    c->startedAt = a->ms;
    c->nextSpawn = a->ms + 600;
}
static float comet_Speed(const Arc_t* a){
    static const float BASE[] = {0.05f, 0.07f, 0.09f};
    const float t = (a->ms - a->comet.startedAt) / 1000.0f;
    return BASE[save.speed] * (1.0f + clampf(t / 60.0f, 0, 2.0f));
}

static int comet_Update(Arc_t* a, uint32_t dt){
    Comet_t* c = &a->comet;
    if (a->held & (1 << B_UP)) c->target -= 0.12f * dt;
    if (a->held & (1 << B_DOWN)) c->target += 0.12f * dt;
    c->target = clampf(c->target, BAR + 6, H - 6);
    const float d = c->target - c->shipY, step = 0.2f * dt;
    c->shipY += fabsf_(d) < step ? d : (d > 0 ? step : -step);
    const float v = comet_Speed(a);
    c->distance += v * dt;
    for (int i = 0; i < CM_SKY; i++){
        c->skyX[i] -= v * dt * c->skyV[i] / 4.0f;
        if (c->skyX[i] < 0){ c->skyX[i] += W; c->skyY[i] = (uint8_t)(BAR + 1 + rnd(H - BAR - 1)); }
    }
    if (a->ms >= c->nextSpawn){
        const float t = (a->ms - c->startedAt) / 1000.0f;
        const int gap = 850 - (int)(t * 6);
        c->nextSpawn = a->ms + (uint32_t)(gap < 260 ? 260 : gap) + rnd(200);
        for (int i = 0; i < CM_ROCKS; i++){
            Rock_t* r = &c->rocks[i];
            if (r->used) continue;
            r->used = 1;
            r->star = rnd(5) == 0;
            r->r = r->star ? 3 : (uint8_t)(2 + rnd(5));
            r->x = W + r->r + 2;
            r->y = (float)(BAR + 4 + rnd(H - BAR - 8));
            r->v = r->star ? 0.8f : 0.8f + rnd(60) / 100.0f;
            break;
        }
    }
    const float sy0 = c->shipY - 4, sy1 = c->shipY + 4;
    for (int i = 0; i < CM_ROCKS; i++){
        Rock_t* r = &c->rocks[i];
        if (!r->used) continue;
        r->x -= v * r->v * dt;
        if (r->x < -12){ r->used = 0; continue; }
        const float nx = clampf(r->x, SHIP_X0, SHIP_X1), ny = clampf(r->y, sy0, sy1);
        const float dx = r->x - nx, dy = r->y - ny;
        const float rr = r->r + (r->star ? 2.0f : 0.0f);
        if (dx * dx + dy * dy < rr * rr){
            if (r->star){ r->used = 0; c->stars++; a->score += 10; sound(S_EAT, 2); continue; }
            return 0;
        }
    }
    a->score = (uint32_t)(c->distance / 20.0f) + c->stars * 10u;
    return 1;
}

static void comet_Draw(const Arc_t* a){
    const Comet_t* c = &a->comet;
    for (int i = 0; i < CM_SKY; i++) dot((int)c->skyX[i], c->skyY[i], c->skyV[i] == 3 ? RGB(150, 150, 170) : c->skyV[i] == 2 ? RGB(80, 80, 100) : RGB(45, 45, 60));
    for (int i = 0; i < CM_ROCKS; i++){
        const Rock_t* r = &c->rocks[i];
        if (!r->used) continue;
        const int x = (int)r->x, y = (int)r->y;
        if (r->star){
            const uint16_t g = KIT_GOLD;
            rect(x - 3, y, x + 3, y, g); rect(x, y - 3, x, y + 3, g);
            rect(x - 1, y - 1, x + 1, y + 1, g);
            continue;
        }
        for (int t = 1; t <= 10; t++) dot(x + r->r + t, y + ((t % 3) - 1) * (t > 5 ? 1 : 0), pxl_dim(RGB(255, 150, 40), (uint8_t)(200 - t * 18)));
        disc(x, y, r->r, RGB(150, 120, 110));
        disc(x - 1, y - 1, r->r > 2 ? r->r - 2 : 1, RGB(200, 170, 150));
    }
    const int y = (int)c->shipY;
    tri(SHIP_X0, y - 4, SHIP_X0, y + 4, SHIP_X1, y, RGB(80, 200, 255));
    tri(SHIP_X0 + 2, y - 2, SHIP_X0 + 2, y + 2, SHIP_X0 + 9, y, RGB(220, 245, 255));
    const uint16_t flame = (a->ms / 60) % 2 ? RGB(255, 120, 0) : RGB(255, 220, 0);
    tri(SHIP_X0 - 1, y - 2, SHIP_X0 - 1, y + 2, SHIP_X0 - 5 - (int)((a->ms / 60) % 3), y, flame);
}

// The menu, the bar and the boxes -------------------------------------------------------------------------------------------
static void mini_Art(int g, int x, int y){
    if (g == 0){
        static const int8_t P[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {6, 3}, {6, 4}, {7, 4}, {8, 4}};
        const int n = sizeof(P) / sizeof(P[0]);
        for (int i = 0; i < n; i++) rect(x + 6 + P[i][0] * 6, y + 8 + P[i][1] * 6, x + 11 + P[i][0] * 6, y + 13 + P[i][1] * 6,
                                         pxl_blend(RGB(20, 110, 40), RGB(120, 255, 90), (float)i / (n - 1)));
        rect(x + 6 + 10 * 6, y + 8 + 4 * 6, x + 11 + 10 * 6, y + 13 + 4 * 6, RGB(235, 40, 50));
    } else if (g == 1){
        for (int r = 0; r < 4; r++) for (int c = 0; c < 6; c++) if ((r + c) % 7 != 3) rect(x + 4 + c * 12, y + 4 + r * 6, x + 14 + c * 12, y + 8 + r * 6, brick_Color(r));
        rect(x + 34, y + 30, x + 36, y + 32, WHITE);
        rect(x + 24, y + 38, x + 48, y + 40, RGB(210, 215, 230));
    } else {
        dot(x + 10, y + 6, KIT_DIM); dot(x + 50, y + 12, KIT_DIM); dot(x + 66, y + 34, KIT_DIM); dot(x + 30, y + 38, KIT_DIM);
        tri(x + 6, y + 18, x + 6, y + 28, x + 22, y + 23, RGB(80, 200, 255));
        disc(x + 48, y + 16, 5, RGB(150, 120, 110)); for (int t = 1; t < 9; t++) dot(x + 53 + t, y + 16, pxl_dim(RGB(255, 150, 40), (uint8_t)(200 - t * 20)));
        disc(x + 60, y + 32, 3, RGB(150, 120, 110));
        rect(x + 36, y + 30, x + 42, y + 30, KIT_GOLD); rect(x + 39, y + 27, x + 39, y + 33, KIT_GOLD);
    }
}

static void menu_Draw(const Arc_t* a){
    const uint16_t bgc = RGB(8, 8, 20);
    pxl_fill(bgc);
    for (int i = 0; i < 40; i++) dot((int)((i * 97 + a->ms / 90) % W), (i * 37) % H, i % 3 ? RGB(40, 40, 70) : RGB(90, 90, 130));
    text_c(W / 2, 2, PXL_FONT_LARGE, KIT_GOLD, "PIXEL ARCADE", bgc);
    for (int g = 0; g < GAMES; g++){
        const int x = 6 + g * 83, y = 18;
        const int on = a->choice == g;
        rect(x, y, x + 77, y + 90, on ? RGB(30, 30, 60) : RGB(16, 16, 32));
        frame(x, y, x + 77, y + 90, on ? KIT_GOLD : RGB(60, 60, 90));
        if (on && (a->ms / 400) % 2) frame(x + 1, y + 1, x + 76, y + 89, KIT_GOLD);
        mini_Art(g, x + 2, y + 6);
        const uint16_t bg = on ? RGB(30, 30, 60) : RGB(16, 16, 32);
        text_c(x + 39, y + 58, PXL_FONT_NORMAL, on ? WHITE : KIT_LABEL, GAME_NAME[g], bg);
        char t[24];
        put_u(put_s(t, "BEST "), save.best[g]);
        text_c(x + 39, y + 72, PXL_FONT_NORMAL, on ? KIT_GOLD : KIT_DIM, t, bg);
    }
    static const char* const SPEED[] = {"RELAXED", "NORMAL", "FAST"};
    char hint[64];
    put_s(put_s(hint, "TURN TO CHOOSE - CLICK TO PLAY - "), SPEED[save.speed]);
    text_c(W / 2, 116, PXL_FONT_SMALL, KIT_LABEL, hint, bgc);
}

static void bar_Draw(const Arc_t* a){
    const uint16_t bg = RGB(20, 20, 36);
    rect(0, 0, W - 1, BAR - 1, bg);
    text_l(2, 1, PXL_FONT_NORMAL, KIT_LABEL, GAME_NAME[a->game], bg);
    char t[32];
    put_u(t, a->score);
    text_c(W / 2, 1, PXL_FONT_NORMAL, WHITE, t, bg);
    put_u(put_s(t, "BEST "), a->score > save.best[a->game] ? a->score : save.best[a->game]);
    if (a->game == 1){
        for (int i = 0; i < a->brick.lives && i < 5; i++) disc(W - 6 - i * 7, 4, 2, WHITE);
        char lv[12]; put_u(put_s(lv, "WALL "), (uint32_t)a->brick.level);
        text_r(W - 40, 1, PXL_FONT_NORMAL, KIT_LABEL, lv, bg);
    } else if (a->game == 2){
        char s[16]; put_u(put_s(s, "* "), a->comet.stars);
        text_r(W - 2, 1, PXL_FONT_NORMAL, KIT_GOLD, s, bg);
        text_r(W - 40, 1, PXL_FONT_NORMAL, KIT_DIM, t, bg);
    } else text_r(W - 2, 1, PXL_FONT_NORMAL, KIT_DIM, t, bg);
}

static void box(const char* big, uint16_t bigColor, const char* line1, uint16_t c1, const char* line2){
    const uint16_t bg = RGB(16, 16, 40);
    rect(38, 34, W - 39, 98, bg);
    frame(38, 34, W - 39, 98, RGB(90, 90, 160));
    text_c(W / 2, 42, PXL_FONT_LARGE, bigColor, big, bg);
    if (line1) text_c(W / 2, 62, PXL_FONT_NORMAL, c1, line1, bg);
    if (line2 && line2[0]) text_c(W / 2, 82, PXL_FONT_SMALL, KIT_LABEL, line2, bg);
}

static void arc_Draw(const Arc_t* a){
    if (a->mode == M_MENU){ menu_Draw(a); return; }
    const uint16_t bg = a->game == 2 ? RGB(2, 2, 10) : BLACK;
    pxl_fill(bg);
    if (a->game == 0) worm_Draw(a); else if (a->game == 1) brick_Draw(a); else comet_Draw(a);
    bar_Draw(a);
    if (a->mode == M_PLAY && a->ms < a->bannerUntil) text_c(W / 2, 60, PXL_FONT_LARGE, KIT_GOLD, a->banner, bg);
    if (a->mode == M_PAUSE) box("PAUSED", WHITE, 0, 0, "CLICK TO GO ON - HOLD FOR THE MENU");
    if (a->mode == M_OVER){
        char t[32];
        char* end = put_u(put_s(t, a->newBest ? "NEW BEST: " : "SCORE "), a->score);
        if (a->newBest) put_s(end, "!");
        box(a->game == 2 ? "CRASH!" : "GAME OVER", a->game == 2 ? KIT_ORANGE : KIT_RED, t, a->newBest ? KIT_GOLD : WHITE,
            a->ms - a->overAt < 800 ? "" : "CLICK TO PLAY AGAIN - HOLD FOR THE MENU");
    }
}

// What the controls do --------------------------------------------------------------------------------------------------------
static void game_Start(Arc_t* a, uint8_t g){
    a->game = g % GAMES;
    a->score = 0;
    a->newBest = 0;
    a->held = 0;
    a->bannerUntil = 0;
    a->mode = M_PLAY;
    if (a->game == 0) worm_Start(a); else if (a->game == 1) brick_Start(a); else comet_Start(a);
    sound(S_START, 2);
}

static void game_Over(Arc_t* a){
    a->mode = M_OVER;
    a->overAt = a->ms;
    save.plays[a->game]++;
    a->newBest = a->score > save.best[a->game];
    if (a->newBest) save.best[a->game] = a->score;
    if (a->newBest) sound(S_BEST, 4); else sound(S_OVER, 4);
}

static void to_Menu(Arc_t* a){ a->mode = M_MENU; a->choice = a->game; a->held = 0; }

static void press(Arc_t* a, uint8_t b){
    switch (a->mode){
        case M_MENU:
            if (b == B_LEFT || b == B_UP) a->choice = (uint8_t)((a->choice + GAMES - 1) % GAMES);
            else if (b == B_RIGHT || b == B_DOWN) a->choice = (uint8_t)((a->choice + 1) % GAMES);
            else if (b == B_OK){ save.game = a->choice; game_Start(a, a->choice); }
            break;
        case M_PLAY:
            if (b == B_OK) a->mode = M_PAUSE;
            else if (b == B_BACK) to_Menu(a);
            else if (a->game == 0){
                static const int8_t D[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                worm_Turn(&a->worm, D[b][0], D[b][1]);
            } else if (a->game == 1){ if (b == B_LEFT) brick_Move_Bat(&a->brick, -24); else if (b == B_RIGHT) brick_Move_Bat(&a->brick, 24); }
            else { if (b == B_UP) a->comet.target -= 16; else if (b == B_DOWN) a->comet.target += 16; }
            break;
        case M_PAUSE:
            if (b == B_OK) a->mode = M_PLAY; else if (b == B_BACK) to_Menu(a);
            break;
        case M_OVER:
            if (a->ms - a->overAt < 800) break;
            if (b == B_OK) game_Start(a, a->game); else if (b == B_BACK) to_Menu(a);
            break;
    }
}

static void turn(Arc_t* a, int dir){
    if (a->mode == M_MENU){ press(a, dir > 0 ? B_RIGHT : B_LEFT); return; }
    if (a->mode != M_PLAY) return;
    if (a->game == 0) worm_Turn_Relative(&a->worm, dir);
    else if (a->game == 1) brick_Move_Bat(&a->brick, dir * 14.0f);
    else a->comet.target += dir * 10.0f;
}

static int key_Btn(int key){
    switch (key){
        case 0x50: case 0x04: return B_LEFT;
        case 0x4F: case 0x07: return B_RIGHT;
        case 0x52: case 0x1A: return B_UP;
        case 0x51: case 0x16: return B_DOWN;
        case 0x28: case 0x2C: case 0x13: return B_OK;
        case 0x29: return B_BACK;
        default: return -1;
    }
}

// The phone page ---------------------------------------------------------------------------------------------------------------
static const char UI_MANIFEST[] =
    "{\"appID\": \"20/0\", \"name\": \"Pixel Arcade\", \"author\": \"Meterbit Cyb\", \"version\": \"v1.0.0-wasm\","
    " \"about\": \"Three quick arcade games on Pixlpal: Worm (eat, grow, don't bite yourself), Brick Breaker (bounce the ball through the wall) and Comet Dodge (fly through the comets and catch the stars). Play with the knob, a USB keyboard or mouse, or this phone as a game pad. This copy runs as a WebAssembly app.\"}";

static const char UI_WIDGETS[] =
    "{\"widgets\": ["
    "{\"type\": \"image\", \"src\": \"asset:pixelArcade.png\", \"height\": 64, \"alt\": \"Pixel Arcade\"},"
    "{\"type\": \"statusPanel\", \"title\": \"On Pixlpal Now\", \"source\": \"now\"},"
    "{\"type\": \"statusPanel\", \"title\": \"Best Scores\", \"source\": \"scores\", \"emptyText\": \"No games played yet.\"},"
    "{\"type\": \"radioGroup\", \"title\": \"Game\", \"default\": 0, \"options\": ["
        "{\"label\": \"Worm: Eat the Fruit and Grow\", \"value\": 0},"
        "{\"label\": \"Brick Breaker: Clear the Wall\", \"value\": 1},"
        "{\"label\": \"Comet Dodge: Fly Through the Comets\", \"value\": 2}],"
        " \"command\": {\"app_command\": 1, \"args\": [\"game\"]}},"
    "{\"type\": \"section\", \"id\": \"pad\", \"title\": \"Game Pad\", \"widgets\": ["
        "{\"type\": \"borderButton\", \"label\": \"UP\", \"command\": {\"app_command\": 0, \"pad\": \"up\"}},"
        "{\"type\": \"borderButton\", \"label\": \"LEFT\", \"command\": {\"app_command\": 0, \"pad\": \"left\"}},"
        "{\"type\": \"borderButton\", \"label\": \"RIGHT\", \"command\": {\"app_command\": 0, \"pad\": \"right\"}},"
        "{\"type\": \"borderButton\", \"label\": \"DOWN\", \"command\": {\"app_command\": 0, \"pad\": \"down\"}},"
        "{\"type\": \"borderButton\", \"label\": \"START / PAUSE\", \"command\": {\"app_command\": 0, \"pad\": \"ok\"}},"
        "{\"type\": \"borderButton\", \"label\": \"BACK TO THE MENU\", \"command\": {\"app_command\": 0, \"pad\": \"back\"}}]},"
    "{\"type\": \"radioGroup\", \"title\": \"Speed\", \"default\": 1, \"options\": ["
        "{\"label\": \"Relaxed\", \"value\": 0}, {\"label\": \"Normal\", \"value\": 1}, {\"label\": \"Fast\", \"value\": 2}],"
        " \"command\": {\"app_command\": 2, \"args\": [\"speed\"]}},"
    "{\"type\": \"switch\", \"title\": \"Sound\", \"default\": true, \"command\": {\"app_command\": 3, \"args\": [\"sound\"]}},"
    "{\"type\": \"borderButton\", \"label\": \"FORGET THE BEST SCORES\", \"confirm\": {\"title\": \"Forget them?\", \"text\": \"Every game's best score goes back to 0.\", \"ok\": \"Forget\", \"cancel\": \"Keep\"}, \"command\": {\"app_command\": 4}},"
    "{\"type\": \"richTextBlock\", \"title\": \"How to Play\", \"blocks\": ["
        "{\"type\": \"bullet\", \"text\": \"Knob: turn to choose a game, click to start. In a game, click to pause, hold to go back to the menu.\"},"
        "{\"type\": \"bullet\", \"text\": \"Worm: each turn of the knob turns the worm left or right. On a keyboard, the arrows point it. Relaxed speed lets the worm go through the walls.\"},"
        "{\"type\": \"bullet\", \"text\": \"Brick Breaker: turn the knob, move the mouse or hold the arrows to move the bat. Three balls a game; each cleared wall is a faster one.\"},"
        "{\"type\": \"bullet\", \"text\": \"Comet Dodge: turn the knob, move the mouse or hold up and down to fly. Stars are 10 points; one comet ends the flight.\"},"
        "{\"type\": \"note\", \"text\": \"Keyboard: Enter or Space starts and pauses, Esc goes back to the menu.\"}]}"
    "], \"toasts\": [{\"when\": {\"app_command\": 4}, \"text\": \"Best scores forgotten\"}]}";

static void send_Values(void){
    static char j[640];
    char* p = put_s(j, "{\"now\": [{\"label\": \"Pixlpal\", \"value\": \"Pixel Arcade is open: pick a game below, or use the knob\", \"state\": \"ok\"}], \"scores\": [");
    for (int g = 0; g < GAMES; g++){
        p = put_s(p, g ? ", {\"label\": \"" : "{\"label\": \"");
        p = put_s(p, GAME_TITLE[g]);
        p = put_s(p, "\", \"value\": \"");
        if (save.plays[g]){
            p = put_u(put_s(p, "Best "), save.best[g]);
            p = put_u(put_s(p, ", played "), save.plays[g]);
            p = put_s(p, save.plays[g] == 1 ? " time\", \"state\": \"info\"}" : " times\", \"state\": \"info\"}");
        } else p = put_s(p, "Not played yet\", \"state\": \"off\"}");
    }
    p = put_u(put_s(p, "], \"game\": "), save.game);
    p = put_u(put_s(p, ", \"speed\": "), save.speed);
    p = put_u(put_s(p, ", \"sound\": "), save.sound);
    put_s(p, "}");
    pxl_phone_values(j);
}

// What Pixlpal calls -------------------------------------------------------------------------------------------------------------
static char inbox[PXL_INBOX_SIZE];
PXL_EXPORT("pxl_inbox") char* pxl_inbox(void){ return inbox; }

PXL_EXPORT("pxl_start") void pxl_start(void){
    read_Save();
    zero(&A, sizeof(A));
    A.ms = pxl_millis();
    A.mode = M_MENU;
    A.choice = A.game = save.game;
    pxl_phone_ui(UI_MANIFEST, UI_WIDGETS);
    send_Values();          // here too: the phone may ask for the page before the app is shown
}

PXL_EXPORT("pxl_stop") void pxl_stop(void){ pxl_sound_stop(); pxl_save_write(&save, sizeof(save)); }

PXL_EXPORT("pxl_input") void pxl_input(int kind, int a, int b){
    (void)b;
    switch (kind){
        case PXL_IN_TURN: turn(&A, a > 0 ? 1 : -1); break;
        case PXL_IN_CLICK: press(&A, B_OK); break;
        case PXL_IN_HOLD: if (A.mode != M_MENU) to_Menu(&A); break;
        case PXL_IN_KEY_DOWN: { const int k = key_Btn(a); if (k < 0) break; if (k <= B_DOWN) A.held |= (uint8_t)(1 << k); press(&A, (uint8_t)k); break; }
        case PXL_IN_KEY_UP: { const int k = key_Btn(a); if (k >= 0 && k <= B_DOWN) A.held &= (uint8_t)~(1 << k); break; }
        case PXL_IN_MOUSE_MOVE:
            if (A.mode != M_PLAY) break;
            if (A.game == 1) brick_Move_Bat(&A.brick, a * 0.6f);
            else if (A.game == 2) A.comet.target = clampf(A.comet.target + b * 0.4f, BAR + 6, H - 6);
            break;
        case PXL_IN_MOUSE_WHEEL: turn(&A, a > 0 ? -1 : 1); break;
        case PXL_IN_MOUSE_BUTTONS: if (a & 1) press(&A, B_OK); else if (a & 2) press(&A, B_BACK); break;
    }
}

PXL_EXPORT("pxl_command") int pxl_command(int command, int length){
    if (length < 0 || length >= PXL_INBOX_SIZE) return 1;
    inbox[length] = 0;
    switch (command){
        case 0: {
            static const char* const NAMES[] = {"left", "right", "up", "down", "ok", "back"};
            char p[8];
            if (pxl_json_str(inbox, "pad", p, sizeof(p)) < 0) return 1;
            for (int i = 0; i < 6; i++) if (str_eq(p, NAMES[i])){ press(&A, (uint8_t)i); return 0; }
            return 1;
        }
        case 1: {
            const int g = pxl_json_int(inbox, "game", -1);
            if (g < 0 || g >= GAMES) return 1;
            save.game = (uint8_t)g;
            if (A.mode == M_MENU || A.mode == M_OVER || A.mode == M_PAUSE){ A.choice = save.game; game_Start(&A, save.game); }
            break;
        }
        case 2: { const int v = pxl_json_int(inbox, "speed", -1); if (v < 0 || v > 2) return 1; save.speed = (uint8_t)v; break; }
        case 3: { const int v = pxl_json_int(inbox, "sound", -1); if (v < 0 || v > 1) return 1; save.sound = (uint8_t)v; break; }
        case 4: zero(save.best, sizeof(save.best)); zero(save.plays, sizeof(save.plays)); break;
        default: return 1;
    }
    dirty = push = 1;
    return 0;
}

PXL_EXPORT("pxl_frame") void pxl_frame(uint32_t ms, uint32_t dt){
    static uint8_t lastMode = 0xFF;
    if (dt > 50) dt = 50;
    A.ms = ms;
    if (A.mode == M_PLAY){
        int alive = 1;
        if (A.game == 0){
            if (ms >= A.worm.nextStep){ alive = worm_Step(&A); A.worm.nextStep = ms + worm_Interval(&A.worm); }
        } else if (A.game == 1) alive = brick_Update(&A, dt);
        else alive = comet_Update(&A, dt);
        if (!alive){ game_Over(&A); dirty = push = 1; }
    }
    if (A.mode != lastMode){ lastMode = A.mode; push = 1; }
    arc_Draw(&A);
    if (dirty && ms - savedMs >= 3000){ dirty = 0; savedMs = ms; pxl_save_write(&save, sizeof(save)); }
    if (push){ push = 0; send_Values(); }
}
