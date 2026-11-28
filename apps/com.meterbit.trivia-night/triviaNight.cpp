/**
 * Trivia Night: a quiz for one to four players, from the Open Trivia Database (opentdb.com, no key). A round of 5 to
 * 20 questions, fetched at once (encode=url3986, so nothing is lost in HTML entities), with a session token so they
 * don't repeat. Each question in turn is a player's: turn the knob to choose an answer, click to lock it in; the right
 * one shows green, a wrong pick red. Points by difficulty (1, 2, 3), a bonus point for three in a row; an optional
 * time limit. After the round, the scores, the winner, then click for the next round. Open Trivia DB allows one
 * request every 5 seconds an address. NVS "trvSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "esp_heap_caps.h"
#include "snd_Sound.h"
#include "trv_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define W           256
#define H           128
#define NVS_KEY     "trvSave"
#define SAVE_MAGIC  0x54525601u
#define UA          "Pixlpal-TriviaNight/1.0"
#define Q_MAX       20
#define PLAYERS     4
#define REVEAL_MS   2600u

EXT_RAM_BSS_ATTR TaskHandle_t triviaNight_Task_H = NULL;
static void triviaNight_App_Task(void* dApplication);
static void trv_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* triviaNight_App;
Mtb_Applications* triviaNight_App_GetInstance(){
    if (!triviaNight_App){
        triviaNight_App = new Mtb_Applications_FullScreen(triviaNight_App_Task, &triviaNight_Task_H, "Trivia Night", {0,0}, 8192);
        triviaNight_App->mtb_App_Set_Mobile_UI(TRV_UI_MANIFEST, TRV_UI_API, nullptr, trv_Values);
    }
    return triviaNight_App;
}
MTB_REGISTER_APP(triviaNight_App, 0, 0)         // the catalogue gives the app its number

struct Trv_Save_t {
    uint32_t magic;
    uint8_t category, difficulty, questions, timer, sound, kind, players, pad;
    char name[PLAYERS][16];
    uint16_t best;                      // one player's best round
    char token[72];                     // Open Trivia DB's session token
    uint32_t tokenAt;
};

struct Trv_Q_t {
    char question[200];
    char answer[4][64];
    int count;                          // 2 (true or false) or 4
    int right;                          // which answer is right, after shuffling
    uint8_t points;                     // 1 easy, 2 medium, 3 hard
    char category[40];
};

struct Trv_Shared_t {
    Trv_Q_t q[Q_MAX];
    int count;
    bool ready;                         // a round's questions are in
    char why[48];
    uint32_t version;
    volatile bool stop, running, fetchNow;
};

static EXT_RAM_BSS_ATTR Trv_Save_t save;
static EXT_RAM_BSS_ATTR Trv_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false, saveWanted = false, newGame = false;

// The game, for the phone too.
enum { G_LOADING = 0, G_ASK, G_REVEAL, G_SCORES };
struct Trv_Game_t { uint8_t phase; int index, pick, turn; int score[PLAYERS], streak[PLAYERS], right[PLAYERS]; int round; };
static EXT_RAM_BSS_ATTR Trv_Game_t game;

static void read_Save(Trv_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.questions = 10; s.timer = 20; s.sound = 1; s.players = 1; }
    if (s.category != 0 && (s.category < 9 || s.category > 32)) s.category = 0;
    if (s.difficulty > 3) s.difficulty = 0;
    if (s.questions < 5 || s.questions > Q_MAX) s.questions = 10;
    if (s.timer > 60) s.timer = 20;
    if (s.kind > 2) s.kind = 0;
    if (s.players < 1 || s.players > PLAYERS) s.players = 1;
    s.sound = s.sound ? 1 : 0;
    for (auto& n : s.name) n[sizeof(n) - 1] = '\0';
    s.token[sizeof(s.token) - 1] = '\0';
}

// Phone commands (the BLE task) --------------------------------------------------------------------------------------------
static bool range_Cmd(JsonDocument& c, const char* key, long lo, long hi, uint8_t& out){
    long v;
    if (!mtb_Kit_Json_Range(c[key], lo, hi, v)){ mtb_Ble_App_Cmd_Reject(); return false; }
    out = (uint8_t)v;
    phoneChanged = saveWanted = true;
    return true;
}
static void cmd_Category(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["category"], 0, 32, v) || (v && v < 9)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.category = (uint8_t)v;
    phoneChanged = saveWanted = true;
}
static void cmd_Difficulty(JsonDocument& c){ range_Cmd(c, "difficulty", 0, 3, save.difficulty); }
static void cmd_Questions(JsonDocument& c){ range_Cmd(c, "questions", 5, Q_MAX, save.questions); }
static void cmd_Names(JsonDocument& c){
    const char* v = c["names"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char names[PLAYERS][16] = {};
    int n = 0;
    const char* p = v;
    while (*p){
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        const char* e = p;
        while (*e && *e != ',') e++;
        if (n >= PLAYERS){ mtb_Ble_App_Cmd_Reject(); return; }
        char t[64];
        snprintf(t, sizeof(t), "%.*s", (int)(e - p), p);
        size_t k = strlen(t);
        while (k && t[k - 1] == ' ') t[--k] = '\0';
        mtb_Kit_To_Ascii(names[n], sizeof(names[n]), t);
        mtb_Kit_Upper(names[n]);
        if (names[n][0]) n++;
        p = e;
    }
    portENTER_CRITICAL(&lock);
    memcpy(save.name, names, sizeof(names));
    save.players = (uint8_t)(n ? n : 1);
    portEXIT_CRITICAL(&lock);
    newGame = true;
    phoneChanged = saveWanted = true;
}
static void cmd_Timer(JsonDocument& c){ range_Cmd(c, "timer", 0, 60, save.timer); }
static void cmd_Sound(JsonDocument& c){ range_Cmd(c, "sound", 0, 1, save.sound); }
static void cmd_New(JsonDocument&){ newGame = true; phoneChanged = true; }
static void cmd_Kind(JsonDocument& c){ range_Cmd(c, "kind", 0, 2, save.kind); }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0, clicks = 0;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clicks++; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker -------------------------------------------------------------------------------------------------------------------------
/** Percent-decoding (url3986) into UTF-8, then the panel's ASCII. */
static void decode(char* out, size_t n, const char* in){
    char t[400];
    size_t k = 0;
    for (const char* p = in; *p && k + 1 < sizeof(t); p++){
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])){
            const char h[3] = {p[1], p[2], 0};
            t[k++] = (char)strtol(h, nullptr, 16);
            p += 2;
        } else t[k++] = *p;
    }
    t[k] = '\0';
    mtb_Kit_To_Ascii(out, n, t);
}

static int get_Json(const char* url, JsonDocument& doc, size_t max){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, max, r, UA);
    int code = r.code;
    if (code == 200 && body && (r.truncated || deserializeJson(doc, body, r.length))) code = -100;
    heap_caps_free(body);
    return code;
}

static bool new_Token(char* token, size_t n){
    JsonDocument doc;
    if (get_Json("https://opentdb.com/api_token.php?command=request", doc, 2048) != 200 || (doc["response_code"] | 1) != 0) return false;
    snprintf(token, n, "%s", doc["token"] | "");
    return token[0] != '\0';
}

static void shuffle(Trv_Q_t& q){
    for (int i = q.count - 1; i > 0; i--){
        const int j = (int)(pxl_random() % (uint32_t)(i + 1));
        if (i == j) continue;
        char t[64];
        memcpy(t, q.answer[i], sizeof(t)); memcpy(q.answer[i], q.answer[j], sizeof(t)); memcpy(q.answer[j], t, sizeof(t));
        if (q.right == i) q.right = j; else if (q.right == j) q.right = i;
    }
}

/** A round's questions. Returns 0, or why not (Open Trivia DB's response code, or an HTTP one). */
static int fetch_Round(Trv_Save_t& s, Trv_Q_t* out, int& count){
    static const char* const DIFF[4] = {"", "easy", "medium", "hard"};
    static const char* const KIND[3] = {"", "multiple", "boolean"};
    // A token lasts 6 hours unused.
    const uint32_t now = (uint32_t)time(nullptr);
    if (!s.token[0] || (now > 1700000000 && now - s.tokenAt > 5 * 3600)){
        if (!new_Token(s.token, sizeof(s.token))) s.token[0] = '\0';
        s.tokenAt = now;
        vTaskDelay(pdMS_TO_TICKS(5200));               // one request every 5 seconds
    }
    for (int attempt = 0; attempt < 3; attempt++){
        char url[256];
        int n = snprintf(url, sizeof(url), "https://opentdb.com/api.php?amount=%d&encode=url3986", s.questions);
        if (s.category) n += snprintf(url + n, sizeof(url) - n, "&category=%d", s.category);
        if (s.difficulty) n += snprintf(url + n, sizeof(url) - n, "&difficulty=%s", DIFF[s.difficulty]);
        if (s.kind) n += snprintf(url + n, sizeof(url) - n, "&type=%s", KIND[s.kind]);
        if (s.token[0]) snprintf(url + n, sizeof(url) - n, "&token=%s", s.token);
        JsonDocument doc(mtb_Kit_Json_Allocator());
        const int code = get_Json(url, doc, 32000);
        if (code != 200) return code;
        const int rc = doc["response_code"] | 2;
        if (rc == 0){
            count = 0;
            for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()){
                if (count >= Q_MAX) break;
                Trv_Q_t& q = out[count];
                memset(&q, 0, sizeof(q));
                decode(q.question, sizeof(q.question), r["question"] | "");
                decode(q.category, sizeof(q.category), r["category"] | "");
                decode(q.answer[0], sizeof(q.answer[0]), r["correct_answer"] | "");
                q.count = 1;
                for (JsonVariantConst a : r["incorrect_answers"].as<JsonArrayConst>()) if (q.count < 4) decode(q.answer[q.count++], sizeof(q.answer[0]), a | "");
                char diff[12];
                decode(diff, sizeof(diff), r["difficulty"] | "");
                q.points = !strcmp(diff, "hard") ? 3 : !strcmp(diff, "medium") ? 2 : 1;
                q.right = 0;
                if (q.count == 2){
                    // True first, then False, as people expect.
                    if (!strcmp(q.answer[0], "False")){ char t[64]; memcpy(t, q.answer[0], 64); memcpy(q.answer[0], q.answer[1], 64); memcpy(q.answer[1], t, 64); q.right = 1; }
                } else shuffle(q);
                if (q.question[0] && q.count >= 2) count++;
            }
            return count ? 0 : 1;
        }
        vTaskDelay(pdMS_TO_TICKS(5200));
        if (rc == 3 || rc == 4){                       // the token is unknown, or has asked them all: start again
            if (rc == 4 && s.token[0]){
                char reset[160];
                snprintf(reset, sizeof(reset), "https://opentdb.com/api_token.php?command=reset&token=%s", s.token);
                JsonDocument d;
                get_Json(reset, d, 2048);
            } else s.token[0] = '\0';
            if (!s.token[0]){ new_Token(s.token, sizeof(s.token)); s.tokenAt = now; }
            vTaskDelay(pdMS_TO_TICKS(5200));
            continue;
        }
        if (rc == 1){ return 1; }                       // not enough questions for that choice
        if (rc == 5) continue;                          // too soon: again after the wait
        return rc;
    }
    return 5;
}

static void worker(void*){
    static Trv_Q_t q[Q_MAX];
    while (!sh.stop){
        if (sh.fetchNow){
            sh.fetchNow = false;
            Trv_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            int count = 0;
            const int why = fetch_Round(s, q, count);
            portENTER_CRITICAL(&lock);
            snprintf(save.token, sizeof(save.token), "%s", s.token);
            save.tokenAt = s.tokenAt;
            if (!why){ memcpy(sh.q, q, sizeof(q)); sh.count = count; sh.ready = true; sh.why[0] = '\0'; }
            else if (why == 1) snprintf(sh.why, sizeof(sh.why), "NOT ENOUGH QUESTIONS: TRY ANOTHER CHOICE");
            else if (why == 5 || why == 429) snprintf(sh.why, sizeof(sh.why), "OPEN TRIVIA DB IS BUSY: CLICK TO TRY AGAIN");
            else if (why < 0) snprintf(sh.why, sizeof(sh.why), "NO ANSWER FROM OPEN TRIVIA DB: CLICK");
            else snprintf(sh.why, sizeof(sh.why), "OPEN TRIVIA DB SAID %d: CLICK TO TRY AGAIN", why);
            sh.version++;
            saveWanted = true;
            portEXIT_CRITICAL(&lock);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ----------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(12, 6, 24), C_BAR = MTB_KIT_RGB(36, 18, 70), C_BOX = MTB_KIT_RGB(30, 24, 52);
static const uint16_t C_PICK = MTB_KIT_RGB(70, 60, 150);
static const uint16_t PLAYER_COLOR[PLAYERS] = {MTB_KIT_RGB(255, 200, 40), MTB_KIT_RGB(80, 200, 255), MTB_KIT_RGB(255, 110, 180), MTB_KIT_RGB(120, 230, 110)};

static const char* player_Name(const Trv_Save_t& s, int k, char* buf, size_t n){
    if (s.name[k][0]) return s.name[k];
    snprintf(buf, n, s.players == 1 ? "YOU" : "PLAYER %d", k + 1);
    return buf;
}

static void draw_Header(const Trv_Save_t& s, const Trv_Shared_t& d){
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_BAR);
    char t[64], f[64], nb[16];
    const Trv_Q_t& q = d.q[game.index];
    snprintf(t, sizeof(t), "%d/%d %s", game.index + 1, d.count, q.category);
    mtb_Kit_Upper(t);
    const char* who = player_Name(s, game.turn, nb, sizeof(nb));
    char right[40];
    snprintf(right, sizeof(right), "%s %d", who, game.score[game.turn]);
    const int rw = mtb_Kit_Text_Width(Terminal6x8, right);
    mtb_Kit_Fit(f, sizeof(f), Terminal4x6, t, W - rw - 12);
    mtb_Kit_Text(3, 2, Terminal4x6, KIT_LABEL, f, C_BAR);
    mtb_Kit_Text_Right(W - 3, 1, Terminal6x8, PLAYER_COLOR[game.turn], right, C_BAR);
}

static void draw_Question(const Trv_Save_t& s, const Trv_Shared_t& d, uint32_t ms, uint32_t askedMs){
    mtb_Panel_Fill_Screen(C_BG);
    draw_Header(s, d);
    const Trv_Q_t& q = d.q[game.index];
    // The question: normal type if it fits in four lines, small type otherwise.
    char lines[7][64];
    const bool four = q.count == 4;
    const int room = four ? 4 : 6;
    int n = mtb_Kit_Wrap(q.question, Terminal6x8, W - 8, &lines[0][0], sizeof(lines[0]), room);
    bool small = false;
    char test[8][64];
    if (mtb_Kit_Wrap(q.question, Terminal6x8, W - 8, &test[0][0], sizeof(test[0]), 8) > room){
        n = mtb_Kit_Wrap(q.question, Terminal4x6, W - 8, &lines[0][0], sizeof(lines[0]), four ? 6 : 7);
        small = true;
    }
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 13 + i * (small ? 7 : 9), small ? Terminal4x6 : Terminal6x8, KIT_TEXT, lines[i], C_BG);
    // The answers.
    static const char LETTER[4] = {'A', 'B', 'C', 'D'};
    const int top = four ? 52 : 74, step = four ? 17 : 22;
    for (int i = 0; i < q.count; i++){
        const int y = top + i * step;
        uint16_t box = C_BOX, fg = KIT_TEXT;
        if (game.phase == G_REVEAL){
            if (i == q.right){ box = MTB_KIT_RGB(20, 120, 50); fg = KIT_TEXT; }
            else if (i == game.pick){ box = MTB_KIT_RGB(150, 30, 30); }
            else fg = KIT_DIM;
        } else if (i == game.pick) box = C_PICK;
        mtb_Panel_Fill_Round_Rect(2, (int16_t)y, W - 3, (int16_t)(y + step - 3), 3, box);
        char t[80], f[80];
        snprintf(t, sizeof(t), "%c", LETTER[i]);
        mtb_Kit_Text(7, y + (step - 3) / 2 - 4, Terminal6x8, game.phase == G_ASK && i == game.pick ? KIT_GOLD : KIT_LABEL, t, box);
        const uint8_t* font = mtb_Kit_Text_Width(Terminal6x8, q.answer[i]) <= W - 28 ? Terminal6x8 : Terminal4x6;
        snprintf(t, sizeof(t), "%s", q.answer[i]);
        mtb_Kit_Fit(f, sizeof(f), font, t, W - 28);
        mtb_Kit_Text(20, y + (step - 3) / 2 - (font == Terminal6x8 ? 4 : 3), font, fg, f, box);
    }
    // The time left.
    if (game.phase == G_ASK && s.timer){
        const float left = 1.0f - (float)(ms - askedMs) / (s.timer * 1000.0f);
        mtb_Kit_Bar(2, H - 3, W - 4, 2, left < 0 ? 0 : left, left < 0.25f ? KIT_RED : KIT_GOLD);
    }
    if (game.phase == G_REVEAL){
        const bool ok = game.pick == q.right;
        char t[40];
        if (ok) snprintf(t, sizeof(t), "+%d%s", q.points, game.streak[game.turn] >= 3 && game.streak[game.turn] % 3 == 0 ? " +1 STREAK" : "");
        else snprintf(t, sizeof(t), game.pick < 0 ? "TIME!" : "WRONG");
        const int tw = mtb_Kit_Text_Width(Terminal8x12, t);
        const int y = four ? 38 : 58;
        mtb_Panel_Fill_Round_Rect((int16_t)(W - tw - 12), (int16_t)(y - 1), W - 3, (int16_t)(y + 12), 3, ok ? KIT_GREEN : KIT_RED);
        mtb_Kit_Text_Right(W - 7, y, Terminal8x12, C_BG, t, ok ? KIT_GREEN : KIT_RED);
    }
}

static void draw_Scores(const Trv_Save_t& s, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    char t[48], nb[16];
    int order[PLAYERS];
    for (int i = 0; i < s.players; i++) order[i] = i;
    for (int i = 1; i < s.players; i++){ const int v = order[i]; int j = i - 1; while (j >= 0 && game.score[order[j]] < game.score[v]){ order[j + 1] = order[j]; j--; } order[j + 1] = v; }
    if (s.players == 1){
        mtb_Kit_Text_Centre(W / 2, 8, Terminal8x12, KIT_LABEL, "ROUND OVER", C_BG);
        snprintf(t, sizeof(t), "%d", game.score[0]);
        mtb_Kit_Text_Centre(W / 2, 28, PT_Sans_Narrow_Bold28x34, KIT_GOLD, t, C_BG);
        snprintf(t, sizeof(t), "POINTS  -  %d OF %d RIGHT", game.right[0], sh.count);
        mtb_Kit_Text_Centre(W / 2, 68, Terminal6x8, KIT_TEXT, t, C_BG);
        if (game.score[0] >= s.best && game.score[0] > 0) mtb_Kit_Text_Centre(W / 2, 84, Terminal8x12, (ms / 400) % 2 ? KIT_GOLD : KIT_ORANGE, "A NEW BEST!", C_BG);
        else { snprintf(t, sizeof(t), "BEST %d", s.best); mtb_Kit_Text_Centre(W / 2, 86, Terminal6x8, KIT_LABEL, t, C_BG); }
    } else {
        const bool tie = game.score[order[0]] == game.score[order[1]];
        if (tie) mtb_Kit_Text_Centre(W / 2, 6, PT_Sans_Narrow_Bold18x22, KIT_GOLD, "A TIE!", C_BG);
        else {
            snprintf(t, sizeof(t), "%s WINS!", player_Name(s, order[0], nb, sizeof(nb)));
            mtb_Kit_Text_Centre(W / 2, 6, PT_Sans_Narrow_Bold18x22, PLAYER_COLOR[order[0]], t, C_BG);
        }
        for (int i = 0; i < s.players; i++){
            const int k = order[i], y = 36 + i * 18;
            mtb_Panel_Fill_Round_Rect(30, (int16_t)y, W - 31, (int16_t)(y + 14), 3, C_BOX);
            snprintf(t, sizeof(t), "%d", i + 1);
            mtb_Kit_Text(36, y + 3, Terminal6x8, KIT_LABEL, t, C_BOX);
            mtb_Kit_Text(52, y + 3, Terminal6x8, PLAYER_COLOR[k], player_Name(s, k, nb, sizeof(nb)), C_BOX);
            snprintf(t, sizeof(t), "%d", game.score[k]);
            mtb_Kit_Text_Right(W - 38, y + 2, Terminal8x12, KIT_TEXT, t, C_BOX);
        }
    }
    mtb_Kit_Text_Centre(W / 2, H - 10, Terminal6x8, (ms / 700) % 2 ? KIT_LABEL : KIT_DIM, "CLICK FOR THE NEXT ROUND", C_BG);
}

static void draw_Loading(const Trv_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    mtb_Kit_Text_Centre(W / 2, 30, PT_Sans_Narrow_Bold28x34, KIT_GOLD, "TRIVIA NIGHT", C_BG);
    if (d.why[0]) mtb_Kit_Text_Centre(W / 2, 80, Terminal6x8, KIT_ORANGE, d.why, C_BG);
    else {
        static const char* const DOTS[4] = {"FINDING QUESTIONS", "FINDING QUESTIONS.", "FINDING QUESTIONS..", "FINDING QUESTIONS..."};
        mtb_Kit_Text_Centre(W / 2, 80, Terminal6x8, KIT_LABEL, DOTS[(ms / 400) % 4], C_BG);
    }
}

static void play(const Snd_Note_t* n, int count){ if (save.sound) snd_Play(n, count, 0.25f, false); }

// The app -------------------------------------------------------------------------------------------------------------------------------
static void triviaNight_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Category, cmd_Difficulty, cmd_Questions, cmd_Names, cmd_Timer, cmd_Sound, cmd_New, cmd_Kind);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = clicks = 0; phoneChanged = saveWanted = newGame = false;
    memset(&sh, 0, sizeof(sh));
    memset(&game, 0, sizeof(game));
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "trvNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;
    Trv_Shared_t* d = (Trv_Shared_t*)heap_caps_calloc(1, sizeof(Trv_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    static const Snd_Note_t RIGHT[] = {{784, 90}, {1047, 180}};
    static const Snd_Note_t WRONG[] = {{220, 300}};
    static const Snd_Note_t TICK[] = {{1200, 15}};
    static const Snd_Note_t END[] = {{523, 140}, {659, 140}, {784, 140}, {1047, 140}, {784, 140}, {1047, 360}};
    uint32_t version = 0xFFFFFFFF, pushedMs = 0, phaseMs = millis();
    bool push = true;
    game.phase = G_LOADING;
    sh.fetchNow = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = true; }
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ memcpy(d, &sh, sizeof(sh)); version = sh.version; push = true; }
        portEXIT_CRITICAL(&lock);
        const int t = turns, c = clicks;
        if (t) turns -= t;
        if (c) clicks -= c;
        if (newGame){
            newGame = false;
            memset(&game, 0, sizeof(game));
            game.phase = G_LOADING; phaseMs = ms;
            portENTER_CRITICAL(&lock); sh.ready = false; portEXIT_CRITICAL(&lock);
            d->ready = false;
            sh.fetchNow = true;
            push = true;
        }
        switch (game.phase){
            case G_LOADING:
                if (d->ready){ game.phase = G_ASK; game.index = 0; game.pick = 0; game.turn = 0; phaseMs = ms; push = true; }
                else if (c && d->why[0] && !sh.fetchNow){ portENTER_CRITICAL(&lock); sh.why[0] = '\0'; sh.version++; portEXIT_CRITICAL(&lock); sh.fetchNow = true; }
                break;
            case G_ASK: {
                const Trv_Q_t& q = d->q[game.index];
                if (t){ game.pick = ((game.pick + t) % q.count + q.count) % q.count; play(TICK, 1); }
                const bool late = save.timer && ms - phaseMs >= save.timer * 1000u;
                if (c || late){
                    if (late && !c) game.pick = -1;
                    const int k = game.turn;
                    if (game.pick == q.right){
                        game.streak[k]++; game.right[k]++;
                        game.score[k] += q.points + (game.streak[k] >= 3 && game.streak[k] % 3 == 0 ? 1 : 0);
                        play(RIGHT, 2);
                    } else { game.streak[k] = 0; play(WRONG, 1); }
                    game.phase = G_REVEAL; phaseMs = ms; push = true;
                }
                break;
            }
            case G_REVEAL:
                if (ms - phaseMs >= REVEAL_MS || (c && ms - phaseMs >= 500)){
                    if (game.index + 1 >= d->count){
                        game.phase = G_SCORES; phaseMs = ms; push = true;
                        if (save.players == 1 && game.score[0] > save.best){ save.best = (uint16_t)game.score[0]; saveWanted = true; }
                        play(END, 6);
                        // The next round's questions, while the scores show.
                        portENTER_CRITICAL(&lock); sh.ready = false; portEXIT_CRITICAL(&lock);
                        d->ready = false;
                        sh.fetchNow = true;
                    } else {
                        game.index++; game.pick = 0; game.turn = (game.turn + 1) % save.players; game.phase = G_ASK; phaseMs = ms; push = true;
                    }
                }
                break;
            case G_SCORES:
                if (c && ms - phaseMs >= 1000){
                    memset(game.score, 0, sizeof(game.score)); memset(game.streak, 0, sizeof(game.streak)); memset(game.right, 0, sizeof(game.right));
                    game.round++;
                    game.phase = G_LOADING; phaseMs = ms; push = true;
                }
                break;
        }
        Trv_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        if (game.phase == G_LOADING) draw_Loading(*d, ms);
        else if (game.phase == G_SCORES) draw_Scores(s, ms);
        else draw_Question(s, *d, ms, phaseMs);
        if (saveWanted){ saveWanted = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 2000){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows --------------------------------------------------------------------------------------------------------------
static void trv_Values(JsonObject values){
    Trv_Save_t s;
    if (appOpen){ portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock); } else read_Save(s);
    JsonArray rows = values["scores"].to<JsonArray>();
    char t[64], nb[16];
    if (appOpen){
        for (int k = 0; k < s.players; k++){
            JsonObject o = rows.add<JsonObject>();
            o["label"] = player_Name(s, k, nb, sizeof(nb));
            snprintf(t, sizeof(t), "%d points, %d right", game.score[k], game.right[k]);
            o["value"] = t;
            o["state"] = game.phase == G_ASK && game.turn == k ? "ok" : "info";
        }
        JsonObject o = rows.add<JsonObject>();
        o["label"] = "Now";
        if (game.phase == G_LOADING) o["value"] = "Finding questions";
        else if (game.phase == G_SCORES) o["value"] = "The round is over: click for the next";
        else { snprintf(t, sizeof(t), "Question %d of %d", game.index + 1, sh.count); o["value"] = t; }
        o["state"] = "info";
    }
    if (s.players == 1 && s.best){ JsonObject o = rows.add<JsonObject>(); o["label"] = "Best round"; snprintf(t, sizeof(t), "%d points", s.best); o["value"] = t; o["state"] = "ok"; }
    char names[80] = "";
    for (int k = 0; k < PLAYERS; k++) if (s.name[k][0]){ if (names[0]) strncat(names, ", ", sizeof(names) - strlen(names) - 1); strncat(names, s.name[k], sizeof(names) - strlen(names) - 1); }
    values["names"] = names;
    values["category"] = s.category;
    values["difficulty"] = s.difficulty;
    values["questions"] = s.questions;
    values["timer"] = s.timer;
    values["sound"] = s.sound;
    values["kind"] = s.kind;
}
