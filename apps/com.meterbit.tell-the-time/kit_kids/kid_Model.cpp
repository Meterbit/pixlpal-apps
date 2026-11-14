// Kids' Learning: the model.
#include "kid_Model.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

uint8_t kid_Math_Level_For_Age(uint8_t age){
    static const uint8_t L[] = {0, 1, 2, 3, 5, 6, 7, 8, 9};      // ages 4..12
    return age < 4 ? 0 : age > 12 ? 9 : L[age - 4];
}

uint8_t kid_Spell_Level_For_Age(uint8_t age){
    return age <= 5 ? 0 : age == 6 ? 1 : age == 7 ? 2 : age <= 9 ? 3 : 4;
}

void kid_Child_Default(Kid_Child_t& c, const char* name, uint8_t age){
    memset(&c, 0, sizeof(c));
    copy(c.name, sizeof(c.name), name);
    c.age = age;
    c.mathLevel = kid_Math_Level_For_Age(age);
    c.spellLevel = kid_Spell_Level_For_Age(age);
}

void kid_Settings_Default(Kid_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = KID_SETTINGS_MAGIC;
    kid_Child_Default(s.children[0], "Child", 7);
    s.children[0].used = 1;
    kid_Child_Default(s.children[1], "Child 2", 9);
    kid_Child_Default(s.children[2], "Child 3", 5);
    s.limitMinutes = 60;
    s.bedFrom = 20 * 60;
    s.bedTo = 7 * 60;
    s.rewardMinutes = 3;
    s.maxGameMinutes = 20;
    s.readGoal = 20;
    s.speak = 1;
    s.sound = 1;
    s.tables = 0x1FFC;                                          // 2 to 12
}

void kid_Settings_Clamp(Kid_Settings_t& s){
    s.magic = KID_SETTINGS_MAGIC;
    bool any = false;
    for (Kid_Child_t& c : s.children){
        c.name[sizeof(c.name) - 1] = '\0';
        if (!c.name[0]) copy(c.name, sizeof(c.name), "Child");
        if (c.age < 4) c.age = 4;
        if (c.age > 12) c.age = 12;
        if (c.mathLevel >= KID_MATH_LEVELS) c.mathLevel = KID_MATH_LEVELS - 1;
        if (c.spellLevel >= KID_SPELL_LEVELS) c.spellLevel = KID_SPELL_LEVELS - 1;
        c.used = c.used ? 1 : 0;
        any |= c.used;
    }
    if (!any) s.children[0].used = 1;
    if (s.active >= KID_CHILDREN || !s.children[s.active].used){ s.active = 0; for (int i = 0; i < KID_CHILDREN; i++) if (s.children[i].used){ s.active = (uint8_t)i; break; } }
    if (s.editing >= KID_CHILDREN) s.editing = 0;
    if (s.limitMinutes > 240) s.limitMinutes = 240;
    if (s.bedFrom >= 1440) s.bedFrom = 20 * 60;
    if (s.bedTo >= 1440) s.bedTo = 7 * 60;
    if (s.rewardMinutes > 15) s.rewardMinutes = 15;
    if (s.maxGameMinutes > 120) s.maxGameMinutes = 120;
    if (s.readGoal < 5) s.readGoal = 5;
    if (s.readGoal > 120) s.readGoal = 120;
    if (s.language > 7) s.language = 0;
    s.tables &= 0x1FFC;
    if (!s.tables) s.tables = 0x1FFC;
    s.bedtimeOn = s.bedtimeOn ? 1 : 0;
    s.speak = s.speak ? 1 : 0;
    s.sound = s.sound ? 1 : 0;
    s.easyAnswers = s.easyAnswers ? 1 : 0;
    s.words[KID_WORDS_LEN - 1] = '\0';
}

const char* kid_Language_Code(uint8_t i){
    static const char* const L[] = {"en", "fr", "es", "pt", "de", "it", "nl", "sw"};
    return i < 8 ? L[i] : "en";
}

uint32_t kid_Random(uint32_t& x){
    if (!x) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

int kid_Random_Range(uint32_t& state, int lo, int hi){
    if (hi <= lo) return lo;
    return lo + (int)(kid_Random(state) % (uint32_t)(hi - lo + 1));
}

// Maths ---------------------------------------------------------------------------------------------------------------------
static void make_Choices(Kid_Question_t& q, uint32_t& rng){
    // The answer and two near it (never negative, never the same), in a random order.
    int c[3] = {q.answer, -1, -1};
    for (int k = 1; k < 3; k++){
        for (int tries = 0; tries < 20; tries++){
            int d = kid_Random_Range(rng, 1, q.answer < 10 ? 3 : 10);
            const int v = kid_Random(rng) & 1 ? q.answer + d : q.answer - d;
            if (v < 0 || v == c[0] || v == c[1]) continue;
            c[k] = v;
            break;
        }
        if (c[k] < 0) c[k] = q.answer + k * 2 + 1;
    }
    const int at = kid_Random_Range(rng, 0, 2);
    const int t = c[0]; c[0] = c[at]; c[at] = t;
    memcpy(q.choices, c, sizeof(c));
}

static void finish(Kid_Question_t& q, char op, int a, int b, int answer, uint8_t skill){
    q.a = a;
    q.b = b;
    q.op = op;
    q.answer = answer;
    q.skill = skill;
    snprintf(q.text, sizeof(q.text), "%d %c %d =", a, op, b);
}

static void add_Sub(Kid_Question_t& q, uint32_t& rng, int max, bool sub){
    if (sub){
        const int a = kid_Random_Range(rng, 1, max), b = kid_Random_Range(rng, 0, a);
        finish(q, '-', a, b, a - b, KID_SUB);
    } else {
        const int a = kid_Random_Range(rng, 0, max), b = kid_Random_Range(rng, 0, max - a);
        finish(q, '+', a, b, a + b, KID_ADD);
    }
}

static void mul_Div(Kid_Question_t& q, uint32_t& rng, int tableMax, bool div, const int* tables = nullptr, int tableCount = 0){
    const int t = tables ? tables[kid_Random_Range(rng, 0, tableCount - 1)] : kid_Random_Range(rng, 2, tableMax);
    const int n = kid_Random_Range(rng, 1, tableMax < 10 ? 10 : tableMax);
    if (div) finish(q, '/', t * n, t, n, KID_DIV);
    else if (kid_Random(rng) & 1) finish(q, 'x', t, n, t * n, KID_MUL);
    else finish(q, 'x', n, t, t * n, KID_MUL);
}

void kid_Math_Question(uint8_t level, bool easy, uint32_t& rng, Kid_Question_t& q){
    memset(&q, 0, sizeof(q));
    const int pick = kid_Random_Range(rng, 0, 99);
    static const int EASY_TABLES[] = {2, 5, 10};
    switch (level){
        case 0: add_Sub(q, rng, 5, false); break;
        case 1: add_Sub(q, rng, 10, pick < 30); break;
        case 2: add_Sub(q, rng, 20, pick < 45); break;
        case 3: add_Sub(q, rng, 50, pick < 45); break;
        case 4: if (pick < 35) mul_Div(q, rng, 10, false, EASY_TABLES, 3); else add_Sub(q, rng, 100, pick < 65); break;
        case 5: if (pick < 70) mul_Div(q, rng, 10, pick < 30); else add_Sub(q, rng, 100, pick < 85); break;
        case 6: if (pick < 70) mul_Div(q, rng, 12, pick < 30); else add_Sub(q, rng, 200, pick < 85); break;
        case 7:
            if (pick < 40){ const int a = kid_Random_Range(rng, 11, 49), b = kid_Random_Range(rng, 2, 9); finish(q, 'x', a, b, a * b, KID_MUL); }
            else if (pick < 70) mul_Div(q, rng, 12, true);
            else add_Sub(q, rng, 500, pick < 85);
            break;
        case 8:
            if (pick < 35){ const int a = kid_Random_Range(rng, 12, 99), b = kid_Random_Range(rng, 2, 9); finish(q, 'x', a, b, a * b, KID_MUL); }
            else if (pick < 60){ const int b = kid_Random_Range(rng, 2, 9), n = kid_Random_Range(rng, 11, 30); finish(q, '/', b * n, b, n, KID_DIV); }
            else add_Sub(q, rng, 1000, pick < 80);
            break;
        default:
            if (pick < 35){ const int a = kid_Random_Range(rng, 11, 25), b = kid_Random_Range(rng, 11, 19); finish(q, 'x', a, b, a * b, KID_MUL); }
            else if (pick < 60){ const int b = kid_Random_Range(rng, 3, 12), n = kid_Random_Range(rng, 12, 60); finish(q, '/', b * n, b, n, KID_DIV); }
            else add_Sub(q, rng, 5000, pick < 80);
            break;
    }
    q.choice = level <= 1 || easy;                              // must be chosen (the youngest); else chosen with the knob, typed on a keyboard
    make_Choices(q, rng);
}

void kid_Race_Question(uint16_t tables, uint32_t& rng, Kid_Question_t& q){
    memset(&q, 0, sizeof(q));
    int list[11], n = 0;
    for (int t = 2; t <= 12; t++) if (tables & (1u << t)) list[n++] = t;
    if (!n){ for (int t = 2; t <= 12; t++) list[n++] = t; }
    mul_Div(q, rng, 12, false, list, n);
    make_Choices(q, rng);
}

uint8_t kid_Adapt(uint8_t level, uint8_t maxLevel, uint8_t& rightRun, uint8_t& wrongRun, bool right){
    if (right){ rightRun++; wrongRun = 0; } else { wrongRun++; rightRun = 0; }
    if (rightRun >= 5 && level < maxLevel){ rightRun = 0; return (uint8_t)(level + 1); }
    if (wrongRun >= 2 && level > 0){ wrongRun = 0; return (uint8_t)(level - 1); }
    return level;
}

// Spelling ---------------------------------------------------------------------------------------------------------------------
// Words a child of each age meets, spelt the same in British and American English.
static const char* const WORDS[KID_SPELL_LEVELS][24] = {
    {"cat", "dog", "sun", "hat", "pig", "bed", "cup", "map", "red", "bus", "fox", "hen", "jam", "leg", "mop", "net", "pen", "rug", "top", "van",
     "web", "zip", "box", "bug"},
    {"fish", "ship", "frog", "milk", "duck", "jump", "hand", "tree", "book", "star", "rain", "play", "bird", "cake", "home", "moon", "blue",
     "green", "chair", "sleep", "black", "clock", "lunch", "shell"},
    {"water", "happy", "apple", "little", "sister", "brother", "yellow", "friend", "school", "garden", "flower", "orange", "pencil", "window",
     "rabbit", "teacher", "morning", "people", "animal", "dinner", "thing", "would", "could", "please"},
    {"because", "answer", "beautiful", "believe", "breakfast", "careful", "different", "elephant", "holiday", "library", "minute",
     "question", "remember", "special", "surprise", "though", "through", "thought", "weather", "whether", "village", "island", "kitchen", "secret"},
    {"environment", "government", "necessary", "rhythm", "separate", "temperature", "vegetable", "knowledge", "mysterious", "immediately",
     "conscience", "embarrass", "achieve", "parliament", "restaurant", "sincerely", "occasion", "guarantee", "physical", "rehearse",
     "community", "definite", "excellent", "language"},
};

int kid_Words(const Kid_Settings_t& s, uint8_t level, const char** out, int max, char* buffer, size_t bufferSize){
    int n = 0;
    if (s.words[0]){
        copy(buffer, bufferSize, s.words);
        char* p = buffer;
        while (*p && n < max){
            while (*p == ',' || *p == ' ' || *p == '\n' || *p == ';') p++;
            if (!*p) break;
            char* w = p;
            while (*p && *p != ',' && *p != '\n' && *p != ';') p++;
            char* e = p;
            if (*p) *p++ = '\0';
            while (e > w && e[-1] == ' ') *--e = '\0';
            if (*w) out[n++] = w;
        }
        if (n) return n;
    }
    if (level >= KID_SPELL_LEVELS) level = KID_SPELL_LEVELS - 1;
    for (int i = 0; i < 24 && n < max; i++) out[n++] = WORDS[level][i];
    return n;
}

bool kid_Spelling_Right(const char* word, const char* typed){
    const char* a = word;
    const char* b = typed;
    for (;;){
        while (*a && !isalpha((unsigned char)*a)) a++;
        while (*b && !isalpha((unsigned char)*b)) b++;
        if (!*a || !*b) return !*a && !*b;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
}

// Time -------------------------------------------------------------------------------------------------------------------------
void kid_New_Day(Kid_Child_t& c, uint32_t today){
    if (c.day == today) return;
    // The reading week moves on by the days that passed.
    const uint32_t gap = c.day && today > c.day ? today - c.day : 7;
    for (int i = 6; i >= 0; i--) c.readWeek[i] = (uint32_t)i >= gap ? c.readWeek[i - gap] : 0;
    c.day = today;
    c.todaySeconds = c.todayGameSeconds = c.todayReadSeconds = c.extraSeconds = 0;
    c.todayRight = c.todayAsked = 0;
    if (c.lastLearnDay && c.lastLearnDay + 1 < today) c.streak = 0;
}

uint16_t kid_Seconds_Left(const Kid_Settings_t& s, const Kid_Child_t& c){
    if (!s.limitMinutes) return 0xFFFF;
    const uint32_t limit = s.limitMinutes * 60u + c.extraSeconds;
    return c.todaySeconds >= limit ? 0 : (uint16_t)(limit - c.todaySeconds);
}

bool kid_Bedtime(const Kid_Settings_t& s, uint16_t minute){
    if (!s.bedtimeOn || s.bedFrom == s.bedTo) return false;
    return s.bedFrom < s.bedTo ? (minute >= s.bedFrom && minute < s.bedTo) : (minute >= s.bedFrom || minute < s.bedTo);
}

void kid_Spend_Second(Kid_Child_t& c, bool game, bool reading){
    if (c.todaySeconds < 0xFFFF) c.todaySeconds++;
    if (game){
        if (c.todayGameSeconds < 0xFFFF) c.todayGameSeconds++;
        if (c.gameSeconds) c.gameSeconds--;
    }
    if (reading && c.todayReadSeconds < 0xFFFF) c.todayReadSeconds++;
}

static void learnt_Today(Kid_Child_t& c, uint32_t today){
    if (c.lastLearnDay == today) return;
    c.streak = (uint16_t)(c.lastLearnDay + 1 == today ? c.streak + 1 : 1);
    c.lastLearnDay = today;
}

uint16_t kid_Round_Done(const Kid_Settings_t& s, Kid_Child_t& c, uint8_t skill, int right, int asked, uint32_t today){
    if (asked <= 0) return 0;
    if (skill < KID_SKILLS){
        c.right[skill] = (uint16_t)(c.right[skill] + right > 60000 ? 60000 : c.right[skill] + right);
        c.asked[skill] = (uint16_t)(c.asked[skill] + asked > 60000 ? 60000 : c.asked[skill] + asked);
    }
    c.todayRight = (uint16_t)(c.todayRight + right);
    c.todayAsked = (uint16_t)(c.todayAsked + asked);
    c.stars = (uint16_t)(c.stars + right > 60000 ? 60000 : c.stars + right);
    learnt_Today(c, today);
    // A good round earns game time, up to the day's games limit (counting what is saved).
    if (!s.rewardMinutes || right * 10 < asked * 7) return 0;
    const uint32_t cap = s.maxGameMinutes * 60u;
    const uint32_t used = c.todayGameSeconds + c.gameSeconds;
    if (used >= cap) return 0;
    uint32_t give = s.rewardMinutes * 60u;
    if (used + give > cap) give = cap - used;
    c.gameSeconds = (uint16_t)(c.gameSeconds + give);
    return (uint16_t)give;
}

uint16_t kid_Reading_Done(Kid_Child_t& c, uint16_t seconds, uint32_t today){
    const uint16_t minutes = (uint16_t)(seconds / 60);
    c.readWeek[0] = (uint16_t)(c.readWeek[0] + minutes);
    const uint16_t stars = (uint16_t)(minutes / 5);
    c.stars = (uint16_t)(c.stars + stars);
    if (minutes) learnt_Today(c, today);
    return stars;
}
