// Typing Hero's model: lessons, fingers, lines to type, words a minute, the falling letters. No Arduino.
#include "typ_Model.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const Typ_Lesson_t LESSONS[TYP_LESSONS] = {
    {"HOME ROW: LEFT HAND", "asdf ", "REST YOUR LEFT FINGERS ON A S D F"},
    {"HOME ROW: RIGHT HAND", "jkl; ", "REST YOUR RIGHT FINGERS ON J K L ;"},
    {"THE HOME ROW", "asdfjkl; ", "BOTH HANDS ON THE HOME ROW. FEEL THE BUMPS ON F AND J"},
    {"G AND H", "asdfghjkl ", "REACH G AND H WITH YOUR POINTING FINGERS"},
    {"E AND I", "asdfghjkl ei", "REACH UP FOR E AND I, THEN COME BACK HOME"},
    {"THE TOP ROW", "asdfghjkl qwertyuiop", "REACH UP, THEN BACK TO THE HOME ROW"},
    {"THE BOTTOM ROW", "asdfghjkl zxcvbnm", "REACH DOWN, THEN BACK TO THE HOME ROW"},
    {"ALL THE LETTERS", "abcdefghijklmnopqrstuvwxyz ", "LOOK AT THE SCREEN, NOT THE KEYS"},
    {"COMMAS AND FULL STOPS", "abcdefghijklmnopqrstuvwxyz ,.", "COMMA AND FULL STOP ARE ON THE RIGHT"},
    {"CAPITAL LETTERS", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ", "HOLD SHIFT WITH THE OTHER HAND"},
    {"NUMBERS", "abcdefghijklmnopqrstuvwxyz0123456789 ", "REACH UP TO THE NUMBER ROW"},
    {"WORDS", "abcdefghijklmnopqrstuvwxyz ", "TYPE WHOLE WORDS SMOOTHLY"},
    {"SENTENCES", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ,.", "CAPITALS, SPACES AND FULL STOPS"},
};

// Common short words, and words made only of the home row and nearby keys.
static const char* const WORDS[] = {
    "as", "ad", "add", "dad", "sad", "fad", "lad", "all", "fall", "ask", "flask", "salad", "glad", "flag", "gas", "has", "had",
    "hall", "shall", "dash", "flash", "half", "lash", "ash", "hag", "jag", "sag", "lag", "gal", "aha", "alas", "dial", "seal",
    "like", "side", "hide", "life", "file", "kid", "lid", "skid", "deal", "lead", "fee", "see", "she", "he", "the", "and", "is",
    "it", "to", "we", "you", "for", "are", "with", "they", "this", "from", "one", "can", "said", "use", "each", "how", "if",
    "will", "up", "out", "many", "then", "them", "some", "her", "make", "him", "into", "time", "look", "two", "more", "write",
    "go", "see", "no", "way", "could", "people", "my", "than", "first", "water", "been", "call", "who", "now", "find", "long",
    "down", "day", "did", "get", "come", "made", "may", "part", "play", "jump", "run", "fast", "slow", "happy", "red", "blue",
    "green", "sun", "moon", "star", "tree", "fish", "bird", "cat", "dog", "book", "home", "school", "friend", "game", "music",
};
static const int WORD_COUNT = (int)(sizeof(WORDS) / sizeof(WORDS[0]));

static const char* const SENTENCES[] = {
    "The cat sat on a mat.", "I like to read books.", "We can play in the park.", "My dog runs very fast.",
    "The sun is hot today.", "Fish swim in the sea.", "I have two red shoes.", "Birds sing in the trees.",
    "We eat lunch at noon.", "The moon is bright.", "Rain makes the grass green.", "My friend has a kite.",
    "Bees make sweet honey.", "Stars shine at night.", "I can type with ten fingers.",
};
static const int SENTENCE_COUNT = (int)(sizeof(SENTENCES) / sizeof(SENTENCES[0]));

void typ_Settings_Default(Typ_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = TYP_SETTINGS_MAGIC;
    s.keyboardMap = 1;
    s.passAccuracy = 90;
}

void typ_Settings_Clamp(Typ_Settings_t& s){
    s.magic = TYP_SETTINGS_MAGIC;
    for (uint8_t& l : s.lesson) if (l >= TYP_LESSONS) l = TYP_LESSONS - 1;
    for (uint8_t& a : s.accuracy) if (a > 100) a = 100;
    s.keyboardMap = s.keyboardMap ? 1 : 0;
    s.unlockAll = s.unlockAll ? 1 : 0;
    if (s.passAccuracy < 50 || s.passAccuracy > 100) s.passAccuracy = 90;
}

const Typ_Lesson_t& typ_Lesson(int i){ return LESSONS[i >= 0 && i < TYP_LESSONS ? i : 0]; }

static const char* const ROWS[4] = {"1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};

bool typ_Key_Place(char c, int& row, int& col){
    c = (char)tolower((unsigned char)c);
    for (row = 0; row < 4; row++){
        const char* p = strchr(ROWS[row], c);
        if (p && c){ col = (int)(p - ROWS[row]); return true; }
    }
    row = col = -1;
    return false;
}

uint8_t typ_Finger(char c){
    if (c == ' ') return TYP_THUMB;
    int row, col;
    if (!typ_Key_Place(c, row, col)) return TYP_R_PINKY;
    static const uint8_t BY_COL[10] = {TYP_L_PINKY, TYP_L_RING, TYP_L_MIDDLE, TYP_L_INDEX, TYP_L_INDEX, TYP_R_INDEX, TYP_R_INDEX, TYP_R_MIDDLE, TYP_R_RING, TYP_R_PINKY};
    return BY_COL[col];
}

const char* typ_Finger_Name(uint8_t f){
    static const char* const N[TYP_FINGERS] = {"LEFT LITTLE FINGER", "LEFT RING FINGER", "LEFT MIDDLE FINGER", "LEFT POINTING FINGER", "THUMB",
                                              "RIGHT POINTING FINGER", "RIGHT MIDDLE FINGER", "RIGHT RING FINGER", "RIGHT LITTLE FINGER"};
    return f < TYP_FINGERS ? N[f] : "";
}

uint16_t typ_Finger_Color(uint8_t f){
    static const uint16_t C[TYP_FINGERS] = {RGB(230, 70, 90), RGB(255, 150, 40), RGB(240, 220, 50), RGB(70, 210, 90), RGB(150, 150, 150),
                                            RGB(60, 200, 220), RGB(80, 130, 255), RGB(170, 100, 255), RGB(240, 100, 200)};
    return f < TYP_FINGERS ? C[f] : RGB(150, 150, 150);
}

static uint32_t next(uint32_t& r){ r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r ? r : (r = 0xA341316Cu); }

static bool made_Of(const char* w, const char* keys){
    for (; *w; w++) if (!strchr(keys, *w)) return false;
    return true;
}

static void append(char* out, size_t size, const char* word){
    const size_t n = strlen(out);
    snprintf(out + n, size - n, "%s%s", n ? " " : "", word);
}

void typ_Line(int lesson, uint32_t& r, char* out, size_t size){
    out[0] = '\0';
    if (lesson < 0 || lesson >= TYP_LESSONS) lesson = 0;
    if (lesson == TYP_LESSONS - 1){
        snprintf(out, size, "%s", SENTENCES[next(r) % SENTENCE_COUNT]);
        return;
    }
    const char* keys = LESSONS[lesson].keys;
    // Letters only, for the words.
    char letters[64];
    size_t nl = 0;
    for (const char* p = keys; *p && nl + 1 < sizeof(letters); p++) if (*p >= 'a' && *p <= 'z') letters[nl++] = *p;
    letters[nl] = '\0';
    // Every key but space, for the made-up practice words (the home row's ';' too).
    char pool[72];
    size_t np = 0;
    for (const char* p = keys; *p && np + 1 < sizeof(pool); p++) if (*p != ' ' && !(*p >= 'A' && *p <= 'Z') && !(*p >= '0' && *p <= '9') && *p != ',' && *p != '.') pool[np++] = *p;
    pool[np] = '\0';
    const char* fit[64];
    int nf = 0;
    for (int i = 0; i < WORD_COUNT && nf < 64; i++) if (made_Of(WORDS[i], letters)) fit[nf++] = WORDS[i];
    const size_t limit = size - 1 < TYP_LINE ? size - 1 : TYP_LINE;
    for (int guard = 0; guard < 40; guard++){
        char w[16];
        if (nf >= 5 && (next(r) % 3 != 0 || lesson >= 7)) snprintf(w, sizeof(w), "%s", fit[next(r) % nf]);
        else {
            // A made-up word of the lesson's letters (practice runs like "fjf dkd").
            const int len = 2 + (int)(next(r) % 3);
            for (int i = 0; i < len; i++) w[i] = pool[next(r) % np];
            w[len] = '\0';
        }
        if (lesson == 9 && (next(r) & 1)) w[0] = (char)toupper((unsigned char)w[0]);
        if (lesson == 10 && next(r) % 3 == 0) snprintf(w, sizeof(w), "%u", (unsigned)(next(r) % 100));
        if (lesson == 8){ const uint32_t k = next(r) % 5; if (k == 0) strncat(w, ",", sizeof(w) - strlen(w) - 1); else if (k == 1) strncat(w, ".", sizeof(w) - strlen(w) - 1); }
        if (strlen(out) + (out[0] ? 1 : 0) + strlen(w) > limit) break;
        append(out, size, w);
    }
}

const char* typ_Race_Word(uint32_t& r){ return WORDS[33 + next(r) % (WORD_COUNT - 33)]; }

int typ_Wpm(uint32_t chars, uint32_t ms){
    if (ms < 1000) return 0;
    return (int)((chars * 60000u / 5u) / ms);
}

void typ_Fall_Start(Typ_Fall_t& f){
    memset(&f, 0, sizeof(f));
    f.speed = 10.0f;
    f.spawnIn = 0.5f;
    f.lives = 3;
}

void typ_Fall_Step(Typ_Fall_t& f, float dt, const char* keys, int width, int height, uint32_t& r){
    if (f.over) return;
    for (Typ_Drop_t& d : f.d){
        if (!d.live) continue;
        d.y += f.speed * dt;
        if (d.y >= height){ d.live = false; if (f.lives) f.lives--; if (!f.lives) f.over = true; }
    }
    f.spawnIn -= dt;
    if (f.spawnIn <= 0){
        char pool[64];
        size_t n = 0;
        for (const char* p = keys; *p && n + 1 < sizeof(pool); p++) if (*p != ' ') pool[n++] = *p;
        for (Typ_Drop_t& d : f.d){
            if (d.live || !n) continue;
            d.c = pool[next(r) % n];
            d.x = (float)(8 + next(r) % (uint32_t)(width - 24));
            d.y = -8;
            d.live = true;
            break;
        }
        const float gap = 1.8f - f.score * 0.02f;
        f.spawnIn = gap < 0.55f ? 0.55f : gap;
    }
}

bool typ_Fall_Key(Typ_Fall_t& f, char c){
    int best = -1;
    for (int i = 0; i < TYP_DROPS; i++) if (f.d[i].live && f.d[i].c == c && (best < 0 || f.d[i].y > f.d[best].y)) best = i;
    if (best < 0) return false;
    f.d[best].live = false;
    f.score++;
    f.zapped = (uint8_t)(best + 1);
    if (f.score % 5 == 0) f.speed += 2.0f;
    return true;
}
