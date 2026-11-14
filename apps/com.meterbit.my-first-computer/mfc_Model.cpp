// My First Computer's model: settings, levels, badges, names, the editor, the calculator, the piano and the pretend
// internet. No Arduino, no panel: tested on a PC.
#include "mfc_Model.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Settings -----------------------------------------------------------------------------------------------------------
void mfc_Child_Default(Mfc_Child_t& c){
    memset(&c, 0, sizeof(c));
    c.earnedLevel = 1;
    c.volume = 6;
    c.lastProgram = MFC_PROGRAMS;
}

void mfc_Child_Reset(Mfc_Child_t& c){
    const uint8_t mode = c.levelMode, click = c.oneClick;
    mfc_Child_Default(c);
    c.levelMode = mode;                 // the parent's choices stay
    c.oneClick = click;
}

void mfc_Settings_Default(Mfc_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = MFC_SETTINGS_MAGIC;
    for (auto& c : s.c) mfc_Child_Default(c);
    s.programs = (uint16_t)((1u << MFC_PROGRAMS) - 1);
    snprintf(s.code, sizeof(s.code), "%s", MFC_DEFAULT_CODE);
}

void mfc_Settings_Clamp(Mfc_Settings_t& s){
    for (auto& c : s.c){
        if (c.levelMode > 3) c.levelMode = 0;
        if (c.earnedLevel < 1 || c.earnedLevel > 3) c.earnedLevel = 1;
        if (c.oneClick > 2) c.oneClick = 0;
        c.badges &= (1u << MFC_BADGES) - 1;
        for (auto& p : c.secret) if (p > MFC_PICTURES) p = 0;
        if (!c.secret[0] || !c.secret[1]) c.secret[0] = c.secret[1] = 0;
        if (c.wallpaper >= MFC_WALLPAPERS) c.wallpaper = 0;
        if (c.theme >= MFC_THEMES) c.theme = 0;
        c.bigPointer = c.bigPointer ? 1 : 0;
        c.slowDouble = c.slowDouble ? 1 : 0;
        if (c.volume > 10) c.volume = 6;
        if (c.lastProgram > MFC_PROGRAMS) c.lastProgram = MFC_PROGRAMS;
    }
    s.programs &= (uint16_t)((1u << MFC_PROGRAMS) - 1);
    s.programs |= (1u << MFC_P_SETTINGS) | (1u << MFC_P_LICENCE);      // always there
    s.code[MFC_CODE_LEN] = '\0';
    if (!mfc_Code_Valid(s.code)){ snprintf(s.code, sizeof(s.code), "%s", MFC_DEFAULT_CODE); s.codeSet = 0; }
    s.codeSet = s.codeSet ? 1 : 0;
    s.locked = s.locked ? 1 : 0;
    if (s.editing >= MFC_CHILDREN) s.editing = 0;
}

// Levels and programs ------------------------------------------------------------------------------------------------
uint8_t mfc_Level_For_Age(uint8_t age){ return age >= 9 ? 3 : age >= 6 ? 2 : 1; }

uint8_t mfc_Level(const Mfc_Child_t& c, uint8_t age){
    if (c.levelMode >= 1 && c.levelMode <= 3) return c.levelMode;
    const uint8_t a = mfc_Level_For_Age(age);
    return c.earnedLevel > a ? c.earnedLevel : a;
}

uint8_t mfc_Program_Level(uint8_t p){
    switch (p){
        case MFC_P_KEYS: case MFC_P_WRITE: case MFC_P_FILES: case MFC_P_CALC: case MFC_P_BIN: return 2;
        case MFC_P_WEB: return 3;
        default: return 1;              // Mouse School, Paint, Piano, Settings, the Licence
    }
}

uint8_t mfc_Badge_Level(uint8_t b){ return b <= MFC_B_SHUTDOWN ? 1 : b <= MFC_B_CALC ? 2 : 3; }

bool mfc_Program_Open(const Mfc_Settings_t& s, const Mfc_Child_t& c, uint8_t age, uint8_t p){
    if (p >= MFC_PROGRAMS) return false;
    if (p == MFC_P_BIN && !(s.programs & (1u << MFC_P_FILES))) return false;   // the bin is part of My Files
    return (s.programs & (1u << p)) && mfc_Level(c, age) >= mfc_Program_Level(p);
}

const char* mfc_Program_Name(uint8_t p){
    static const char* const N[MFC_PROGRAMS] = {"MOUSE SCHOOL", "KEYBOARD", "PAINT", "MY WRITING", "MY FILES", "CALCULATOR",
                                                "PIANO", "INTERNET", "SETTINGS", "MY LICENCE", "RECYCLE BIN"};
    return p < MFC_PROGRAMS ? N[p] : "";
}

bool mfc_One_Click(const Mfc_Child_t& c, uint8_t age){
    if (c.oneClick == 1) return true;
    if (c.oneClick == 2) return false;
    return mfc_Level(c, age) == 1;
}

uint16_t mfc_Double_Ms(const Mfc_Child_t& c){ return c.slowDouble ? 800 : 500; }

// Badges ---------------------------------------------------------------------------------------------------------------
const char* mfc_Badge_Name(uint8_t b){
    static const char* const N[MFC_BADGES] = {
        "POINTER", "CLICKER", "DOUBLE CLICK", "DRAG AND DROP", "ARTIST", "MUSICIAN", "SHUT DOWN",
        "SCROLLER", "KEY FINDER", "MY NAME", "SPECIAL KEYS", "WRITER", "RENAMER", "TIDY UP", "NUMBER CRUNCHER",
        "RIGHT CLICK", "WEB EXPLORER", "SEARCHER", "POP-UP PRO", "SAFETY STAR"};
    return b < MFC_BADGES ? N[b] : "";
}

const char* mfc_Badge_How(uint8_t b){
    static const char* const H[MFC_BADGES] = {
        "Point at the stars in Mouse School", "Pop the balloons in Mouse School", "Open the presents in Mouse School",
        "Take the animals to the barn in Mouse School", "Save a picture in Paint", "Play a song on the Piano",
        "Shut the computer down from the Start menu",
        "Scroll down to the treasure in Mouse School", "Find the keys in Keyboard", "Type your name in Keyboard",
        "Learn the special keys in Keyboard", "Save some writing in My Writing", "Rename a file in My Files",
        "Delete a file, then bring it back from the Recycle Bin", "Work out 3 sums on the Calculator",
        "Use right click menus in Mouse School", "Follow 5 links and go Back on the Internet",
        "Search the Internet and open what you found", "Close the prize pop-up with its X",
        "Ask a grown-up before typing on the Space Club page"};
    return b < MFC_BADGES ? H[b] : "";
}

bool mfc_Has(const Mfc_Child_t& c, uint8_t b){ return b < MFC_BADGES && (c.badges & (1u << b)); }

int mfc_Level_Badges(const Mfc_Child_t& c, uint8_t level, int& total){
    int n = 0;
    total = 0;
    for (uint8_t b = 0; b < MFC_BADGES; b++){
        if (mfc_Badge_Level(b) != level) continue;
        total++;
        if (mfc_Has(c, b)) n++;
    }
    return n;
}

bool mfc_Award(Mfc_Child_t& c, uint8_t b, uint8_t age){
    if (b >= MFC_BADGES || mfc_Has(c, b)) return false;
    c.badges |= 1u << b;
    // By age: a level whose badges are all earned opens the next.
    if (c.levelMode == 0){
        const uint8_t now = mfc_Level(c, age);
        int total = 0;
        if (now < 3 && mfc_Level_Badges(c, now, total) == total) c.earnedLevel = (uint8_t)(now + 1);
    }
    return true;
}

int mfc_Badge_Count(const Mfc_Child_t& c){
    int n = 0;
    for (uint8_t b = 0; b < MFC_BADGES; b++) if (mfc_Has(c, b)) n++;
    return n;
}

// The parent code -----------------------------------------------------------------------------------------------------
bool mfc_Code_Valid(const char* code){
    if (!code || strlen(code) != MFC_CODE_LEN) return false;
    for (int i = 0; i < MFC_CODE_LEN; i++) if (!isdigit((unsigned char)code[i])) return false;
    return true;
}

bool mfc_Code_Right(const Mfc_Settings_t& s, const char* entered){
    const char* code = s.codeSet && mfc_Code_Valid(s.code) ? s.code : MFC_DEFAULT_CODE;
    if (!entered || strlen(entered) != MFC_CODE_LEN) return false;
    uint8_t diff = 0;                    // every digit compared, right or wrong
    for (int i = 0; i < MFC_CODE_LEN; i++) diff |= (uint8_t)(code[i] ^ entered[i]);
    return diff == 0;
}

// File names --------------------------------------------------------------------------------------------------------------
void mfc_Name_Clean(const char* typed, char* out, size_t size){
    size_t n = 0;
    bool gap = false;
    for (const char* p = typed ? typed : ""; *p && n + 1 < size && n < MFC_NAME_LEN; p++){
        const char c = (char)toupper((unsigned char)*p);
        if (c == ' ' || c == '_'){ gap = n > 0; continue; }
        if (!isalnum((unsigned char)c) && c != '-') continue;
        if (gap){
            if (n + 2 >= size || n + 2 > MFC_NAME_LEN) break;      // no room for "_X": the name ends at the word
            out[n++] = '_';
        }
        gap = false;
        out[n++] = c;
    }
    out[n] = '\0';
}

void mfc_Name_Show(const char* name, char* out, size_t size){
    size_t n = 0;
    for (const char* p = name ? name : ""; *p && n + 1 < size; p++) out[n++] = *p == '_' ? ' ' : *p;
    out[n] = '\0';
}

void mfc_Name_Next(const char* base, const char (*taken)[MFC_NAME_LEN + 1], int count, char* out, size_t size){
    for (int i = 1; i < 100; i++){
        char t[MFC_NAME_LEN + 1];
        snprintf(t, sizeof(t), "%.*s_%d", MFC_NAME_LEN - 3, base, i);
        bool used = false;
        for (int k = 0; k < count && !used; k++) if (!strcmp(taken[k], t)) used = true;
        if (!used){ snprintf(out, size, "%s", t); return; }
    }
    snprintf(out, size, "%.*s", (int)(size - 1), base);
}

// The text editor ---------------------------------------------------------------------------------------------------------------
void mfc_Text_Clear(Mfc_Text_t& x){ x.t[0] = '\0'; x.len = x.cursor = 0; x.changed = 0; }

void mfc_Text_Set(Mfc_Text_t& x, const char* text){
    size_t n = 0;
    for (const char* p = text ? text : ""; *p && n < MFC_TEXT_MAX; p++){
        const char c = *p == '\r' ? 0 : *p;
        if (c == '\n' || (c >= ' ' && c <= '~')) x.t[n++] = c;
    }
    x.t[n] = '\0';
    x.len = (uint16_t)n;
    x.cursor = x.len;
    x.changed = 0;
}

bool mfc_Text_Insert(Mfc_Text_t& x, char c){
    if (x.len >= MFC_TEXT_MAX || !(c == '\n' || (c >= ' ' && c <= '~'))) return false;
    memmove(x.t + x.cursor + 1, x.t + x.cursor, (size_t)(x.len - x.cursor + 1));
    x.t[x.cursor++] = c;
    x.len++;
    x.changed = 1;
    return true;
}

void mfc_Text_Backspace(Mfc_Text_t& x){
    if (!x.cursor) return;
    memmove(x.t + x.cursor - 1, x.t + x.cursor, (size_t)(x.len - x.cursor + 1));
    x.cursor--;
    x.len--;
    x.changed = 1;
}

void mfc_Text_Delete(Mfc_Text_t& x){
    if (x.cursor >= x.len) return;
    memmove(x.t + x.cursor, x.t + x.cursor + 1, (size_t)(x.len - x.cursor));
    x.len--;
    x.changed = 1;
}

int mfc_Text_Layout(const Mfc_Text_t& x, int cols, uint16_t* starts, int max){
    if (cols < 4) cols = 4;
    int lines = 0;
    int i = 0;
    starts[lines++] = 0;
    while (i < x.len && lines < max){
        // A line from i: up to cols characters, broken at the last space, or at a new line.
        int end = i, lastSpace = -1;
        while (end < x.len && end - i < cols && x.t[end] != '\n'){ if (x.t[end] == ' ') lastSpace = end; end++; }
        int next;
        if (end < x.len && x.t[end] == '\n') next = end + 1;
        else if (end >= x.len) break;
        else if (x.t[end] == ' ') next = end + 1;                  // the line ends just before a space
        else if (lastSpace >= 0) next = lastSpace + 1;
        else next = end;                                            // a word longer than a line
        starts[lines++] = (uint16_t)next;
        i = next;
    }
    return lines;
}

void mfc_Text_Where(const Mfc_Text_t& x, const uint16_t* starts, int lines, int& line, int& col){
    line = 0;
    for (int l = 0; l < lines; l++) if (starts[l] <= x.cursor) line = l;
    col = x.cursor - starts[line];
}

void mfc_Text_Move(Mfc_Text_t& x, int dx, int dy, int cols){
    if (dx){
        int c = (int)x.cursor + dx;
        x.cursor = (uint16_t)(c < 0 ? 0 : c > x.len ? x.len : c);
    }
    if (dy){
        static uint16_t starts[MFC_LINES_MAX];
        const int lines = mfc_Text_Layout(x, cols, starts, MFC_LINES_MAX);
        int line, col;
        mfc_Text_Where(x, starts, lines, line, col);
        const int to = line + dy;
        if (to < 0){ x.cursor = 0; return; }
        if (to >= lines){ x.cursor = x.len; return; }
        const int end = to + 1 < lines ? starts[to + 1] - 1 : x.len;      // the last place on that line
        int c = starts[to] + col;
        if (c > end) c = end;
        if (to + 1 >= lines && c > x.len) c = x.len;
        x.cursor = (uint16_t)c;
    }
}

int mfc_Text_Words(const Mfc_Text_t& x){
    int n = 0;
    bool in = false;
    for (int i = 0; i < x.len; i++){
        const bool w = isalnum((unsigned char)x.t[i]);
        if (w && !in) n++;
        in = w;
    }
    return n;
}

// The calculator -------------------------------------------------------------------------------------------------------------
void mfc_Calc_Format(double v, char* out, size_t size){
    if (!isfinite(v) || fabs(v) >= 1e10){ snprintf(out, size, "TOO BIG"); return; }
    if (fabs(v) < 1e-9) v = 0;
    char t[32];
    // As many decimals as fit in 10 digits, then the zeros at the end taken off.
    const int whole = fabs(v) >= 1 ? (int)floor(log10(fabs(v))) + 1 : 1;
    int decimals = 10 - whole;
    if (decimals < 0) decimals = 0;
    if (decimals > 8) decimals = 8;
    snprintf(t, sizeof(t), "%.*f", decimals, v);
    if (strchr(t, '.')){
        char* e = t + strlen(t) - 1;
        while (*e == '0') *e-- = '\0';
        if (*e == '.') *e = '\0';
    }
    if (!strcmp(t, "-0")) snprintf(t, sizeof(t), "0");
    snprintf(out, size, "%s", t);
}

void mfc_Calc_Clear(Mfc_Calc_t& c){
    const uint8_t answers = c.answers;
    memset(&c, 0, sizeof(c));
    c.answers = answers;
    snprintf(c.entry, sizeof(c.entry), "0");
    c.fresh = true;
}

static double entry_Value(const Mfc_Calc_t& c){ return strtod(c.entry, nullptr); }

static bool work_Out(Mfc_Calc_t& c){
    const double b = entry_Value(c);
    double r = c.acc;
    switch (c.op){
        case '+': r = c.acc + b; break;
        case '-': r = c.acc - b; break;
        case '*': r = c.acc * b; break;
        case '/':
            if (b == 0){ c.error = true; snprintf(c.message, sizeof(c.message), "CAN'T DIVIDE BY 0"); snprintf(c.entry, sizeof(c.entry), "0"); return false; }
            r = c.acc / b;
            break;
        default: r = b; break;
    }
    mfc_Calc_Format(r, c.entry, sizeof(c.entry));
    if (!strcmp(c.entry, "TOO BIG")){ c.error = true; snprintf(c.message, sizeof(c.message), "THAT'S TOO BIG!"); snprintf(c.entry, sizeof(c.entry), "0"); return false; }
    c.acc = r;
    return true;
}

void mfc_Calc_Key(Mfc_Calc_t& c, char key){
    if (key == 'C' || key == 'c'){ mfc_Calc_Clear(c); return; }
    if (c.error){ mfc_Calc_Clear(c); if (key == '=' || strchr("+-*/", key)) return; }
    c.message[0] = '\0';
    if (isdigit((unsigned char)key) || key == '.'){
        if (c.fresh){ snprintf(c.entry, sizeof(c.entry), "0"); c.fresh = false; }
        const size_t n = strlen(c.entry);
        int digits = 0;
        for (size_t i = 0; i < n; i++) if (isdigit((unsigned char)c.entry[i])) digits++;
        if (key == '.'){ if (!strchr(c.entry, '.') && n + 1 < sizeof(c.entry)) strcat(c.entry, "."); return; }
        if (digits >= 10) return;
        if (!strcmp(c.entry, "0")){ c.entry[0] = key; return; }
        if (n + 1 < sizeof(c.entry)){ c.entry[n] = key; c.entry[n + 1] = '\0'; }
        return;
    }
    if (key == '<'){
        if (c.fresh) return;
        const size_t n = strlen(c.entry);
        if (n <= 1 || (n == 2 && c.entry[0] == '-')) snprintf(c.entry, sizeof(c.entry), "0");
        else c.entry[n - 1] = '\0';
        return;
    }
    if (strchr("+-*/", key)){
        if (c.op && !c.fresh){ if (!work_Out(c)) return; }
        else if (!c.op) c.acc = entry_Value(c);
        c.op = key;
        c.fresh = true;
        return;
    }
    if (key == '='){
        if (!c.op) return;
        if (work_Out(c) && c.answers < 255) c.answers++;
        c.op = 0;
        c.fresh = true;
    }
}

// The piano ---------------------------------------------------------------------------------------------------------------------
// White keys C D E F G A B; the semitones from C of each.
static const uint8_t WHITE_SEMI[7] = {0, 2, 4, 5, 7, 9, 11};

static int semitone_Of(int key){
    if (key >= 100){
        const int w = key - 100;
        if (w < 0 || w >= MFC_PIANO_WHITE) return -1;
        const int n = w % 7;
        if (n == 2 || n == 6) return -1;     // no black key after E and B
        return (w / 7) * 12 + WHITE_SEMI[n] + 1;
    }
    if (key < 0 || key >= MFC_PIANO_WHITE) return -1;
    return (key / 7) * 12 + WHITE_SEMI[key % 7];
}

float mfc_Piano_Hz(int key){
    const int s = semitone_Of(key);
    if (s < 0) return 0;
    return 261.63f * powf(2.0f, s / 12.0f);  // from middle C (C4)
}

const char* mfc_Piano_Name(int key){
    static const char* const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int s = semitone_Of(key);
    return s < 0 ? "" : N[s % 12];
}

int mfc_Piano_Key_For(char c){
    c = (char)tolower((unsigned char)c);
    static const char LOW_W[] = "zxcvbnm", HIGH_W[] = "qwertyu";
    static const char LOW_B[] = "sd ghj", HIGH_B[] = "23 567";   // black keys after C, D, (E none), F, G, A
    for (int i = 0; i < 7; i++){ if (LOW_W[i] == c) return i; if (HIGH_W[i] == c) return 7 + i; }
    for (int i = 0; i < 6; i++){
        if (LOW_B[i] == ' ') continue;
        if (LOW_B[i] == c) return 100 + i;
        if (HIGH_B[i] == c) return 100 + 7 + i;
    }
    return -1;
}

// White key numbers: C4 0, D4 1, E4 2, F4 3, G4 4, A4 5, B4 6, C5 7.
static const int8_t TWINKLE[] = {0, 0, 4, 4, 5, 5, 4, 3, 3, 2, 2, 1, 1, 0, 4, 4, 3, 3, 2, 2, 1, 4, 4, 3, 3, 2, 2, 1,
                                 0, 0, 4, 4, 5, 5, 4, 3, 3, 2, 2, 1, 1, 0};
static const int8_t MARY[] = {2, 1, 0, 1, 2, 2, 2, 1, 1, 1, 2, 4, 4, 2, 1, 0, 1, 2, 2, 2, 2, 1, 1, 2, 1, 0};
static const Mfc_Song_t SONGS[MFC_SONGS] = {
    {"TWINKLE TWINKLE", TWINKLE, (int)sizeof(TWINKLE)},
    {"MARY HAD A LAMB", MARY, (int)sizeof(MARY)},
};
const Mfc_Song_t& mfc_Song(int i){ return SONGS[i >= 0 && i < MFC_SONGS ? i : 0]; }

// The secret pictures -------------------------------------------------------------------------------------------------------
const char* mfc_Picture_Name(uint8_t i){
    static const char* const N[MFC_PICTURES] = {"CAT", "STAR", "SUN", "HEART", "FISH", "TREE"};
    return i < MFC_PICTURES ? N[i] : "";
}

// The pretend internet -------------------------------------------------------------------------------------------------------
static const Mfc_Page_t PAGES[] = {
    {"home", "KIDSNET", RGB(40, 110, 220), MFC_PAGE_HOME,
     "Welcome to KidsNet, a pretend internet to practise on. Click a link to visit a page, then use Back to return.\n"
     "[Animals|animals] [Space|space] [Dinosaurs|dinosaurs] [Oceans|oceans]\n"
     "[Fun Games|games] [Space Club|club] [Safety Rules|safety]",
     "home start kidsnet"},
    {"results", "SEARCH RESULTS", RGB(40, 110, 220), MFC_PAGE_RESULTS, "", ""},
    {"animals", "ANIMALS", RGB(60, 160, 60), MFC_PAGE_TEXT,
     "Animals live all over the world: in jungles, deserts, oceans and even in the ice. Choose an animal to read about.\n"
     "[Lions|lions] [Penguins|penguins] [Elephants|elephants]\n[Home|home]",
     "animals animal pets wild zoo"},
    {"lions", "LIONS", RGB(210, 150, 30), MFC_PAGE_TEXT,
     "Lions live in Africa, on grassy plains called savannas. A family of lions is called a pride. "
     "A lion's roar can be heard 8 kilometres away. Lions sleep for up to 20 hours a day!\n"
     "More animals: [Penguins|penguins] [Elephants|elephants]",
     "lion lions cat cats roar africa pride savanna"},
    {"penguins", "PENGUINS", RGB(70, 120, 200), MFC_PAGE_TEXT,
     "Penguins are birds that cannot fly, but they are wonderful swimmers. Most penguins live in the south of the "
     "world, many of them in Antarctica. Emperor penguins are the biggest: as tall as a six year old child.\n"
     "More animals: [Lions|lions] [Elephants|elephants]",
     "penguin penguins bird birds ice antarctica swim"},
    {"elephants", "ELEPHANTS", RGB(130, 130, 140), MFC_PAGE_TEXT,
     "Elephants are the biggest animals that live on land. They use their trunks to drink, to smell and to pick "
     "things up. A baby elephant is called a calf, and it stays close to its mother.\n"
     "More animals: [Lions|lions] [Penguins|penguins]",
     "elephant elephants trunk big africa asia calf"},
    {"space", "SPACE", RGB(60, 40, 140), MFC_PAGE_TEXT,
     "Space is everything beyond our sky: the Sun, the Moon, the planets and the stars. Where would you like to go?\n"
     "[The Planets|planets] [The Moon|moon] [The Sun|sun]\n[Home|home]",
     "space stars rocket astronaut sky"},
    {"planets", "THE PLANETS", RGB(200, 90, 40), MFC_PAGE_TEXT,
     "Eight planets go around our Sun: Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus and Neptune. "
     "Jupiter is the biggest. Saturn has beautiful rings made of ice and rock. Earth is the only planet we know "
     "with living things.\n[The Moon|moon] [The Sun|sun]",
     "planet planets mars jupiter saturn earth venus mercury uranus neptune"},
    {"moon", "THE MOON", RGB(120, 120, 150), MFC_PAGE_TEXT,
     "The Moon goes around the Earth about once a month. The first people walked on the Moon in 1969. "
     "There is no air on the Moon, so astronauts wear space suits to breathe.\n[The Planets|planets] [The Sun|sun]",
     "moon astronaut 1969 night"},
    {"sun", "THE SUN", RGB(230, 160, 0), MFC_PAGE_TEXT,
     "The Sun is a star: a giant ball of very hot gas. It gives us light and warmth, and plants need it to grow. "
     "Never look straight at the Sun, because it can hurt your eyes.\n[The Planets|planets] [The Moon|moon]",
     "sun star light hot day"},
    {"dinosaurs", "DINOSAURS", RGB(110, 150, 50), MFC_PAGE_TEXT,
     "Dinosaurs lived millions of years ago, long before people. We know about them from fossils: bones and "
     "footprints that turned to stone.\n[T. rex|trex] [Diplodocus|diplodocus]\n[Home|home]",
     "dinosaur dinosaurs dino fossil fossils"},
    {"trex", "T. REX", RGB(170, 60, 50), MFC_PAGE_TEXT,
     "Tyrannosaurus rex was a huge meat eater with teeth as long as bananas. Its arms were tiny, but its legs and "
     "its jaws were very strong.\nMore dinosaurs: [Diplodocus|diplodocus]",
     "trex rex tyrannosaurus dinosaur teeth"},
    {"diplodocus", "DIPLODOCUS", RGB(80, 140, 120), MFC_PAGE_TEXT,
     "Diplodocus was a plant eater with a very long neck and an even longer tail. From its nose to the end of its "
     "tail it was about as long as a tennis court.\nMore dinosaurs: [T. rex|trex]",
     "diplodocus dinosaur neck plants"},
    {"oceans", "OCEANS", RGB(20, 110, 170), MFC_PAGE_TEXT,
     "Oceans cover most of our planet. They are home to fish, octopuses, coral and the biggest animal of all.\n"
     "[Whales|whales]\n[Home|home]",
     "ocean oceans sea fish coral water"},
    {"whales", "WHALES", RGB(40, 80, 160), MFC_PAGE_TEXT,
     "The blue whale is the biggest animal that has ever lived, even bigger than the dinosaurs. Whales breathe air, "
     "so they swim up to the top of the sea and breathe through a blowhole.\n[Oceans|oceans]",
     "whale whales blue ocean sea"},
    {"games", "FUN GAMES", RGB(200, 50, 150), MFC_PAGE_GAMES,
     "This is a pretend games page. Real websites with games often have pop-ups and adverts. Watch out!\n[Home|home]",
     "games game play fun"},
    {"club", "SPACE CLUB", RGB(60, 40, 140), MFC_PAGE_CLUB,
     "Join the Space Club! Fill in the form below.", "club join space form"},
    {"safety", "SAFETY RULES", RGB(40, 160, 90), MFC_PAGE_TEXT,
     "1. Ask a grown-up before you go online.\n"
     "2. Never type your home address, your school, your phone number or a password.\n"
     "3. Don't click on pop-ups that say you have won a prize.\n"
     "4. If something worries you, stop and tell a grown-up.\n"
     "5. Be kind online, just like at school.\n[Home|home]",
     "safety safe rules help password grown-up"},
};

int mfc_Web_Pages(void){ return (int)(sizeof(PAGES) / sizeof(PAGES[0])); }
const Mfc_Page_t& mfc_Web_Page(int i){ return PAGES[i >= 0 && i < mfc_Web_Pages() ? i : 0]; }

int mfc_Web_Find(const char* address){
    if (!address) return -1;
    if (!strncmp(address, "kidsnet/", 8)) address += 8;
    for (int i = 0; i < mfc_Web_Pages(); i++) if (!strcmp(PAGES[i].address, address)) return i;
    return -1;
}

// True if word (lower case) is one of the space-separated words in list.
static bool has_Word(const char* list, const char* word){
    const size_t n = strlen(word);
    for (const char* p = list; *p; ){
        while (*p == ' ') p++;
        const char* e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && !strncmp(p, word, n)) return true;
        p = e;
    }
    return false;
}

int mfc_Web_Search(const char* query, int* out, int max){
    int score[64] = {0};
    char word[24];
    const int pages = mfc_Web_Pages() < 64 ? mfc_Web_Pages() : 64;
    for (const char* p = query ? query : ""; *p; ){
        while (*p && !isalnum((unsigned char)*p)) p++;
        size_t n = 0;
        while (*p && isalnum((unsigned char)*p)){ if (n + 1 < sizeof(word)) word[n++] = (char)tolower((unsigned char)*p); p++; }
        word[n] = '\0';
        if (n < 2) continue;
        for (int i = 0; i < pages; i++){
            if (PAGES[i].kind == MFC_PAGE_RESULTS) continue;
            if (has_Word(PAGES[i].keywords, word)) score[i] += 2;
            else if (n >= 3){                 // the start of a word: "dino", "plan"
                for (const char* k = PAGES[i].keywords; *k; ){
                    while (*k == ' ') k++;
                    if (!strncmp(k, word, n)){ score[i] += 1; break; }
                    while (*k && *k != ' ') k++;
                }
            }
        }
    }
    int count = 0;
    while (count < max){
        int best = -1;
        for (int i = 0; i < pages; i++) if (score[i] > 0 && (best < 0 || score[i] > score[best])) best = i;
        if (best < 0) break;
        out[count++] = best;
        score[best] = 0;
    }
    return count;
}

void mfc_Web_Layout(const char* text, int cols, Mfc_Layout_t& L){
    memset(&L, 0, sizeof(L));
    for (auto& p : L.linkPage) p = -1;
    if (cols < 8) cols = 8;
    int line = 0, col = 0;
    int link = -1;
    const char* p = text ? text : "";
    const char* base = p;
    while (*p && L.words < MFC_WEB_WORDS){
        if (*p == '\n'){ line++; col = 0; p++; continue; }
        if (*p == ' '){ p++; continue; }
        if (*p == '['){                   // a link starts
            p++;
            link = L.links < MFC_WEB_LINKS ? L.links++ : -1;
            // Its address: after the '|' of this link.
            const char* bar = strchr(p, '|');
            const char* end = bar ? strchr(bar, ']') : nullptr;
            if (link >= 0 && bar && end){
                char address[24];
                const size_t n = (size_t)(end - bar - 1) < sizeof(address) - 1 ? (size_t)(end - bar - 1) : sizeof(address) - 1;
                memcpy(address, bar + 1, n);
                address[n] = '\0';
                L.linkPage[link] = (int8_t)mfc_Web_Find(address);
            }
            continue;
        }
        if (*p == '|'){                   // the address: skipped, the link ends at ']'
            const char* end = strchr(p, ']');
            p = end ? end + 1 : p + strlen(p);
            link = -1;
            // A link ends a word group: links are spaced out like buttons.
            col++;
            continue;
        }
        // A word: up to a space, a new line, '[' or '|'.
        const char* e = p;
        while (*e && *e != ' ' && *e != '\n' && *e != '[' && *e != '|' && *e != ']') e++;
        int len = (int)(e - p);
        if (len > cols) len = cols;
        if (col > 0 && col + len > cols){ line++; col = 0; }
        Mfc_Word_t& w = L.w[L.words++];
        w.line = (uint8_t)(line < 255 ? line : 255);
        w.col = (uint8_t)col;
        w.len = (uint8_t)len;
        w.link = (int8_t)link;
        w.at = (uint16_t)(p - base);
        col += len + 1;
        p = e;
        if (*p == ']') p++;
    }
    L.lines = line + 1;
}
