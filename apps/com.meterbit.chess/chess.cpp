/**
 * Chess: a live game from Lichess TV. The board (left, white at the bottom) from the game's position, the last move
 * marked; beside it each player's title, name, rating and clock (the clock of the side to move runs down between
 * checks), the last move, the opening; and when the game ends, the result, then the channel's next game. A second
 * page, with the owner's Lichess name, shows their ratings (bullet, blitz, rapid, classical, puzzles).
 *
 * Lichess's open API, no key: GET /api/tv/channels (the channel's game), /game/export/{id} (JSON, with the last
 * position and the clocks) every 4 seconds, /api/user/{name} every 10 minutes. NVS "chsSave".
 * Knob: turn, the game or the ratings; click, the next game.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "chs_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "chsSave"
#define SAVE_MAGIC  0x43485301u
#define UA          "Pixlpal-Chess/1.0 (pixlpal.com)"
#define SQ          16
#define POLL_MS     4000u
#define RESULT_MS   9000u

static const char* const CHANNEL[] = {"best", "bullet", "blitz", "rapid", "classical"};
static const char* const CHANNEL_NAME[] = {"TOP RATED", "BULLET", "BLITZ", "RAPID", "CLASSICAL"};

EXT_RAM_BSS_ATTR TaskHandle_t chess_Task_H = NULL;
static void chess_App_Task(void* dApplication);
static void chs_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* chess_App;
Mtb_Applications* chess_App_GetInstance(){
    if (!chess_App){
        chess_App = new Mtb_Applications_FullScreen(chess_App_Task, &chess_Task_H, "Chess", {0,0}, 8192);
        chess_App->mtb_App_Set_Mobile_UI(CHS_UI_MANIFEST, CHS_UI_API, nullptr, chs_Values);
    }
    return chess_App;
}
MTB_REGISTER_APP(chess_App, 0, 0)       // the catalogue gives the app its number

struct Chs_Save_t { uint32_t magic; uint8_t channel; char user[32]; };

struct Chs_Player_t { char name[24], title[6]; int rating; int32_t clockCs; };     // clock: centiseconds, -1 none
struct Chs_Game_t {
    char id[12];
    char board[64];                     // a8..h1, row by row: 'K'..'p' or ' '
    bool whiteToMove;
    int8_t from, to;                    // the last move's squares (0 a8 .. 63 h1), -1 none
    char last[10], opening[40], speed[12], status[20], winner[8];
    Chs_Player_t white, black;
    bool over;
};
struct Chs_Ratings_t { bool known; char name[32]; int bullet, blitz, rapid, classical, puzzle; };

struct Chs_Shared_t {
    Chs_Game_t g;
    bool known;
    uint32_t fetchedMs;                 // when the clocks were read (millis)
    Chs_Ratings_t r;
    char why[44];
    uint32_t version;
    volatile bool nextGame, ratingsNow, stop, running;
};

static EXT_RAM_BSS_ATTR Chs_Save_t save;
static EXT_RAM_BSS_ATTR Chs_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;
static volatile int pageRequested = -1;

static void read_Save(Chs_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; }
    if (s.channel > 4) s.channel = 0;
    s.user[sizeof(s.user) - 1] = '\0';
}

// Phone commands ------------------------------------------------------------------------------------------------------------
static void cmd_Channel(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["channel"], 0, 4, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.channel = (uint8_t)v;
    sh.nextGame = true;
    phoneChanged = true;
}
static void cmd_User(JsonDocument& c){
    const char* v = c["user"] | (const char*)nullptr;
    if (!v || strlen(v) >= sizeof(save.user)){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if (!(isalnum((unsigned char)*k) || *k == '-' || *k == '_')){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.user, sizeof(save.user), "%s", v); sh.r.known = false; portEXIT_CRITICAL(&lock);
    sh.ratingsNow = true;
    phoneChanged = true;
}
static void cmd_Page(JsonDocument& c){ long v; if (!mtb_Kit_Json_Range(c["page"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; } pageRequested = (int)v; }
static void cmd_Next(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.nextGame = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile bool clickRequested = false;
static volatile int turns = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static int get_Json(const char* url, JsonDocument& doc, JsonDocument* filter, size_t max){
    const char* headers[] = {"Accept", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, max, r, UA);
    if (r.code == 200 && body){
        const DeserializationError e = filter ? deserializeJson(doc, body, r.length, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body, r.length);
        if (r.truncated || e) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static bool parse_Fen(const char* fen, Chs_Game_t& g){
    memset(g.board, ' ', sizeof(g.board));
    int sq = 0;
    const char* p = fen;
    for (; *p && *p != ' '; p++){
        if (*p == '/') continue;
        if (isdigit((unsigned char)*p)){ sq += *p - '0'; continue; }
        if (sq < 64) g.board[sq++] = *p;
    }
    g.whiteToMove = *p == ' ' && p[1] != 'b';
    return sq == 64;
}

static int8_t square_Of(const char* s){ return (s[0] >= 'a' && s[0] <= 'h' && s[1] >= '1' && s[1] <= '8') ? (int8_t)((8 - (s[1] - '0')) * 8 + (s[0] - 'a')) : -1; }

static void read_Player(JsonVariantConst p, Chs_Player_t& o){
    memset(&o, 0, sizeof(o));
    mtb_Kit_To_Ascii(o.name, sizeof(o.name), p["user"]["name"] | (p["aiLevel"].is<int>() ? "Stockfish" : "Anonymous"));
    mtb_Kit_To_Ascii(o.title, sizeof(o.title), p["user"]["title"] | "");
    o.rating = p["rating"] | 0;
    o.clockCs = -1;
}

static int fetch_Channel(uint8_t channel, char* id, size_t n){
    JsonDocument doc;
    const int code = get_Json("https://lichess.org/api/tv/channels", doc, nullptr, 16000);
    if (code != 200) return code;
    snprintf(id, n, "%s", doc[CHANNEL[channel]]["gameId"] | "");
    return id[0] ? 200 : -2;
}

static int fetch_Game(const char* id, Chs_Game_t& g){
    char url[160];
    snprintf(url, sizeof(url), "https://lichess.org/game/export/%s?moves=true&clocks=true&evals=false&opening=true&lastFen=true", id);
    JsonDocument doc, filter;
    filter["status"] = true; filter["speed"] = true; filter["winner"] = true; filter["lastFen"] = true; filter["lastMove"] = true;
    filter["moves"] = true; filter["clocks"] = true; filter["opening"]["name"] = true; filter["clock"]["initial"] = true;
    for (const char* side : {"white", "black"}){
        filter["players"][side]["user"]["name"] = true; filter["players"][side]["user"]["title"] = true;
        filter["players"][side]["rating"] = true; filter["players"][side]["aiLevel"] = true;
    }
    const int code = get_Json(url, doc, &filter, 48000);
    if (code != 200) return code;
    Chs_Game_t o = {};
    snprintf(o.id, sizeof(o.id), "%s", id);
    if (!parse_Fen(doc["lastFen"] | "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", o)) return -2;
    const char* lm = doc["lastMove"] | "";
    o.from = strlen(lm) >= 4 ? square_Of(lm) : -1;
    o.to = strlen(lm) >= 4 ? square_Of(lm + 2) : -1;
    const char* moves = doc["moves"] | "";
    const char* lastSpace = strrchr(moves, ' ');
    snprintf(o.last, sizeof(o.last), "%s", lastSpace ? lastSpace + 1 : moves);
    mtb_Kit_To_Ascii(o.opening, sizeof(o.opening), doc["opening"]["name"] | "");
    snprintf(o.speed, sizeof(o.speed), "%s", doc["speed"] | "");
    snprintf(o.status, sizeof(o.status), "%s", doc["status"] | "");
    snprintf(o.winner, sizeof(o.winner), "%s", doc["winner"] | "");
    o.over = strcmp(o.status, "started") && strcmp(o.status, "created");
    read_Player(doc["players"]["white"], o.white);
    read_Player(doc["players"]["black"], o.black);
    // Clocks: the clock of the side that moved, after each move (white's first); before any move, the starting time.
    JsonArrayConst clocks = doc["clocks"].as<JsonArrayConst>();
    const int n = (int)clocks.size();
    const int32_t initial = (doc["clock"]["initial"] | 0) * 100;
    o.white.clockCs = n >= 1 ? clocks[(n - 1) & ~1].as<int32_t>() : (initial ? initial : -1);
    o.black.clockCs = n >= 2 ? clocks[n % 2 ? n - 2 : n - 1].as<int32_t>() : (initial ? initial : -1);
    g = o;
    return 200;
}

static int fetch_Ratings(const char* user, Chs_Ratings_t& r){
    char url[96];
    snprintf(url, sizeof(url), "https://lichess.org/api/user/%s", user);
    JsonDocument doc, filter;
    filter["username"] = true;
    for (const char* p : {"bullet", "blitz", "rapid", "classical", "puzzle"}) filter["perfs"][p]["rating"] = true;
    const int code = get_Json(url, doc, &filter, 16000);
    if (code != 200) return code;
    mtb_Kit_To_Ascii(r.name, sizeof(r.name), doc["username"] | user);
    r.bullet = doc["perfs"]["bullet"]["rating"] | 0; r.blitz = doc["perfs"]["blitz"]["rating"] | 0;
    r.rapid = doc["perfs"]["rapid"]["rating"] | 0; r.classical = doc["perfs"]["classical"]["rating"] | 0;
    r.puzzle = doc["perfs"]["puzzle"]["rating"] | 0;
    r.known = true;
    return 200;
}

static void worker(void*){
    char id[12] = "";
    uint32_t nextPoll = 0, overAt = 0, ratingsAt = 0;
    static Chs_Game_t g;
    while (!sh.stop){
        Chs_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        // The ratings.
        if (s.user[0] && (sh.ratingsNow || !ratingsAt || millis() - ratingsAt >= 600000u)){
            sh.ratingsNow = false;
            ratingsAt = millis() ? millis() : 1;
            Chs_Ratings_t r = {};
            const int code = fetch_Ratings(s.user, r);
            portENTER_CRITICAL(&lock);
            if (code == 200 && !strcmp(s.user, save.user)) sh.r = r;
            else if (code == 404) snprintf(sh.why, sizeof(sh.why), "NO LICHESS PLAYER OF THAT NAME");
            sh.version++;
            portEXIT_CRITICAL(&lock);
        }
        if (sh.nextGame){ sh.nextGame = false; id[0] = '\0'; overAt = 0; }
        if ((int32_t)(millis() - nextPoll) < 0){ vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        nextPoll = millis() + POLL_MS;
        if (overAt && millis() - overAt >= RESULT_MS){ id[0] = '\0'; overAt = 0; }
        int code = 200;
        if (!id[0]) code = fetch_Channel(s.channel, id, sizeof(id));
        if (code == 200) code = fetch_Game(id, g);
        if (code == 429) nextPoll = millis() + 60000u;              // Lichess asks to wait a minute
        portENTER_CRITICAL(&lock);
        if (code == 200){
            sh.g = g; sh.known = true; sh.fetchedMs = millis(); sh.why[0] = '\0';
            if (g.over && !overAt) overAt = millis() ? millis() : 1;
        } else {
            if (code == 429) snprintf(sh.why, sizeof(sh.why), "LICHESS SAYS WAIT A MINUTE");
            else if (code <= 0) snprintf(sh.why, sizeof(sh.why), "NO ANSWER FROM LICHESS");
            else snprintf(sh.why, sizeof(sh.why), "LICHESS: HTTP %d", code);
            if (code == 404) id[0] = '\0';
        }
        sh.version++;
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(10, 10, 12), C_LIGHT = MTB_KIT_RGB(240, 217, 181), C_DARK = MTB_KIT_RGB(181, 136, 99);
static const uint16_t C_LIGHT_HL = MTB_KIT_RGB(205, 210, 106), C_DARK_HL = MTB_KIT_RGB(170, 162, 58);

// 12 x 12 pieces: '#' is the piece.
static const char* const SPRITE[6][12] = {
    { "............", "............", ".....##.....", "....####....", "....####....", ".....##.....", "....####....", ".....##.....", "....####....", "...######...", "..########..", "............" },   // pawn
    { "............", "..##.##.##..", "..########..", "...######...", "...######...", "...######...", "...######...", "...######...", "..########..", ".##########.", ".##########.", "............" },   // rook
    { "............", ".....#.#....", "....######..", "...###.####.", "..########..", "..###.####..", "......####..", ".....#####..", "....######..", "...#######..", "..#########.", "............" },   // knight
    { "............", ".....##.....", "....####....", "...###.##...", "...##.###...", "...######...", "....####....", ".....##.....", "....####....", "...######...", "..########..", "............" },   // bishop
    { "............", ".#..#..#..#.", ".#..#..#..#.", ".##.####.##.", "..########..", "..########..", "...######...", "...######...", "....####....", "...######...", "..########..", "............" },   // queen
    { "............", ".....##.....", "....####....", ".....##.....", "..##.##.##..", ".##########.", ".##########.", "..########..", "...######...", "...######...", "..########..", "............" },   // king
};

static int sprite_Of(char p){
    switch (tolower((unsigned char)p)){ case 'p': return 0; case 'r': return 1; case 'n': return 2; case 'b': return 3; case 'q': return 4; case 'k': return 5; }
    return -1;
}

static void piece(int x, int y, char p){
    const int k = sprite_Of(p);
    if (k < 0) return;
    const bool white = isupper((unsigned char)p);
    const uint16_t fill = white ? MTB_KIT_RGB(250, 250, 250) : MTB_KIT_RGB(20, 20, 20);
    const uint16_t edge = white ? MTB_KIT_RGB(30, 30, 30) : MTB_KIT_RGB(200, 200, 200);
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++){
            if (SPRITE[k][r][c] != '#') continue;
            const bool inside = r > 0 && r < 11 && c > 0 && c < 11 && SPRITE[k][r - 1][c] == '#' && SPRITE[k][r + 1][c] == '#' && SPRITE[k][r][c - 1] == '#' && SPRITE[k][r][c + 1] == '#';
            mtb_Panel_Fill_Rect(x + c, y + r, x + c, y + r, inside ? fill : edge);
        }
}

static void clock_Text(char* t, size_t n, int32_t cs){
    if (cs < 0){ t[0] = '\0'; return; }
    const int32_t s = cs / 100;
    if (s >= 3600) snprintf(t, n, "%ld:%02ld:%02ld", (long)(s / 3600), (long)(s / 60 % 60), (long)(s % 60));
    else if (s >= 20) snprintf(t, n, "%ld:%02ld", (long)(s / 60), (long)(s % 60));
    else snprintf(t, n, "0:%02ld.%ld", (long)s, (long)(cs / 10 % 10));
}

static void player_Box(int y, const Chs_Player_t& p, int32_t clockCs, bool toMove, bool over){
    char t[40], f[40];
    if (p.title[0]){ mtb_Kit_Text(134, y, Terminal4x6, KIT_GOLD, p.title, C_BG); }
    snprintf(t, sizeof(t), "%s", p.name); mtb_Kit_Upper(t);
    mtb_Kit_Fit(f, sizeof(f), Terminal6x8, t, 120 - (p.title[0] ? 18 : 0));
    mtb_Kit_Text(p.title[0] ? 152 : 134, y - 1, Terminal6x8, KIT_TEXT, f, C_BG);
    if (p.rating){ snprintf(t, sizeof(t), "%d", p.rating); mtb_Kit_Text(134, y + 9, Terminal4x6, KIT_LABEL, t, C_BG); }
    clock_Text(t, sizeof(t), clockCs);
    const uint16_t bg = toMove && !over ? MTB_KIT_RGB(40, 50, 70) : C_BG;
    if (t[0]){
        const int w = mtb_Kit_Text_Width(Terminal8x12, t);
        mtb_Panel_Fill_Rect(W - 6 - w, y + 7, W - 3, y + 20, bg);
        mtb_Kit_Text_Right(W - 4, y + 8, Terminal8x12, toMove && !over ? KIT_TEXT : KIT_LABEL, t, bg);
    }
}

static void draw_Game(const Chs_Save_t& s, const Chs_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    if (!d.known){ mtb_Kit_Text_Centre(W / 2, 58, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "FINDING A GAME ON LICHESS TV...", C_BG); return; }
    const Chs_Game_t& g = d.g;
    for (int sq = 0; sq < 64; sq++){
        const int r = sq / 8, c = sq % 8;
        const bool light = (r + c) % 2 == 0, hl = sq == g.from || sq == g.to;
        const uint16_t col = hl ? (light ? C_LIGHT_HL : C_DARK_HL) : (light ? C_LIGHT : C_DARK);
        mtb_Panel_Fill_Rect(c * SQ, r * SQ, c * SQ + SQ - 1, r * SQ + SQ - 1, col);
        if (g.board[sq] != ' ') piece(c * SQ + 2, r * SQ + 2, g.board[sq]);
    }
    // The clock of the side to move runs down from the last check.
    const int32_t since = (int32_t)((ms - d.fetchedMs) / 10);
    int32_t wc = g.white.clockCs, bc = g.black.clockCs;
    if (!g.over && wc >= 0 && bc >= 0){ if (g.whiteToMove) wc = wc > since ? wc - since : 0; else bc = bc > since ? bc - since : 0; }
    player_Box(4, g.black, bc, !g.whiteToMove, g.over);
    player_Box(100, g.white, wc, g.whiteToMove, g.over);
    char t[48], f[48];
    if (g.over){
        const char* res = !strcmp(g.winner, "white") ? "1-0" : !strcmp(g.winner, "black") ? "0-1" : "1/2-1/2";
        mtb_Kit_Text_Centre(192, 38, PT_Sans_Narrow_Bold18x22, KIT_GOLD, res, C_BG);
        snprintf(t, sizeof(t), "%s", g.status); mtb_Kit_Upper(t);
        mtb_Kit_Text_Centre(192, 62, Terminal4x6, KIT_LABEL, t, C_BG);
    } else {
        snprintf(t, sizeof(t), "%s", g.last);
        mtb_Kit_Text_Centre(192, 36, Terminal10x17, KIT_TEXT, t, C_BG);
        snprintf(t, sizeof(t), "%s  %s", CHANNEL_NAME[s.channel], g.speed); mtb_Kit_Upper(t);
        mtb_Kit_Text_Centre(192, 56, Terminal4x6, KIT_CYAN, t, C_BG);
    }
    snprintf(t, sizeof(t), "%s", g.opening); mtb_Kit_Upper(t);
    char lines[2][48];
    const int n = mtb_Kit_Wrap(t, Terminal4x6, 118, &lines[0][0], sizeof(lines[0]), 2);
    for (int k = 0; k < n; k++) mtb_Kit_Text_Centre(192, 72 + k * 8, Terminal4x6, KIT_DIM, lines[k], C_BG);
    (void)f;
}

static void draw_Ratings(const Chs_Save_t& s, const Chs_Shared_t& d){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48];
    if (!s.user[0]){ mtb_Kit_Text_Centre(W / 2, 54, Terminal6x8, KIT_LABEL, "ADD YOUR LICHESS NAME IN THE PHONE APP", C_BG); return; }
    snprintf(t, sizeof(t), "%s", d.r.known ? d.r.name : s.user); mtb_Kit_Upper(t);
    mtb_Kit_Text_Centre(W / 2, 4, PT_Sans_Narrow_Bold18x22, KIT_TEXT, t, C_BG);
    if (!d.r.known){ mtb_Kit_Text_Centre(W / 2, 60, Terminal6x8, d.why[0] ? KIT_ORANGE : KIT_LABEL, d.why[0] ? d.why : "LOADING...", C_BG); return; }
    const char* name[5] = {"BULLET", "BLITZ", "RAPID", "CLASSICAL", "PUZZLES"};
    const int value[5] = {d.r.bullet, d.r.blitz, d.r.rapid, d.r.classical, d.r.puzzle};
    for (int k = 0; k < 5; k++){
        const int x = 8 + k * 50;
        mtb_Kit_Text_Centre(x + 20, 44, Terminal4x6, KIT_LABEL, name[k], C_BG);
        snprintf(t, sizeof(t), "%d", value[k]);
        mtb_Kit_Text_Centre(x + 20, 56, Terminal8x12, value[k] ? KIT_GOLD : KIT_DIM, value[k] ? t : "-", C_BG);
    }
    mtb_Kit_Text_Centre(W / 2, 100, Terminal4x6, KIT_DIM, "LICHESS.ORG", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chess_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Channel, cmd_User, cmd_Page, cmd_Next);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    clickRequested = false; turns = 0; phoneChanged = false; pageRequested = -1;
    memset(&sh, 0, sizeof(sh));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "chsNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Chs_Shared_t* d = (Chs_Shared_t*)heap_caps_calloc(1, sizeof(Chs_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, lastDraw = 0;
    int page = 0;
    bool redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        if (clickRequested){ clickRequested = false; sh.nextGame = true; page = 0; redraw = true; }
        const int t = turns;
        if (t){ turns -= t; page = save.user[0] ? 1 - page : 0; redraw = true; }
        if (pageRequested >= 0){ page = pageRequested; pageRequested = -1; redraw = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        portEXIT_CRITICAL(&lock);
        if (redraw || (page == 0 && ms - lastDraw >= 500u)){          // the running clock (each piece is drawn pixel by pixel: not too often)
            redraw = false;
            lastDraw = ms;
            Chs_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            if (page == 0) draw_Game(s, *d, ms); else draw_Ratings(s, *d);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 3000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(50);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void chs_Values(JsonObject values){
    Chs_Save_t s;
    Chs_Shared_t* d = (Chs_Shared_t*)heap_caps_calloc(1, sizeof(Chs_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; *d = sh; portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["now"].to<JsonArray>();
    char t[96];
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Watching";
    if (!appOpen){ o["value"] = "Lichess TV, while Chess is open"; o["state"] = "off"; }
    else if (!d->known){ o["value"] = d->why[0] ? d->why : "Finding a game"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        snprintf(t, sizeof(t), "%s%s%s (%d) v %s%s%s (%d)", d->g.white.title, d->g.white.title[0] ? " " : "", d->g.white.name, d->g.white.rating,
                 d->g.black.title, d->g.black.title[0] ? " " : "", d->g.black.name, d->g.black.rating);
        o["value"] = t; o["state"] = d->g.over ? "ok" : "info";
    }
    if (d->r.known){
        o = rows.add<JsonObject>();
        o["label"] = d->r.name;
        snprintf(t, sizeof(t), "Bullet %d, blitz %d, rapid %d, classical %d, puzzles %d", d->r.bullet, d->r.blitz, d->r.rapid, d->r.classical, d->r.puzzle);
        o["value"] = t; o["state"] = "info";
    }
    values["channel"] = s.channel;
    values["user"] = s.user;
    heap_caps_free(d);
}
