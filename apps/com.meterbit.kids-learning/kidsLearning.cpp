/**
 * Kids' Learning & Pixel Games app, ID {19,0} (KIDS): for children of 4 to 12. Maths that follows how the child
 * is doing (choose the answer with the knob, or type it on a USB keyboard), a 60-second times-tables race, spelling
 * (look, listen, spell: the word is shown, then spoken, then spelt letter by letter with the knob or typed), a
 * reading timer with a daily goal, and two original games (Star Catcher, Cave Glider) played with game time earned by
 * learning. Parents set each child's age, a daily time limit, bedtime, the race's tables and the school's spelling
 * words, and see progress, on the phone.
 *
 * The knob: turn, choose; click, pick or answer; double click, undo a letter (or hear the word again); hold, back.
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_usb_hid.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "snd_Sound.h"
#include "kid_Model.h"
#include "kid_Games.h"
#include "kid_Ui.h"
#include "kid_Mobile_Ui.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

EXT_RAM_BSS_ATTR TaskHandle_t kids_Task_H = NULL;
static void kids_App_Task(void* dApplication);
static void kid_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *kids_App;
Mtb_Applications* kids_App_GetInstance() {
    if (!kids_App){
        kids_App = new Mtb_Applications_StatusBar(kids_App_Task, &kids_Task_H, "Kids Learning", {19,0}, 8192);
        kids_App->mtb_App_Set_Mobile_UI(KID_UI_MANIFEST, KID_UI_API, nullptr, kid_Values);
    }
    return kids_App;
}
MTB_REGISTER_APP(kids_App, 19, 0)

EXT_RAM_BSS_ATTR static Kid_Settings_t settings;
EXT_RAM_BSS_ATTR static Kid_Settings_t received;
EXT_RAM_BSS_ATTR static Kid_Play_t play;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false, switchRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static uint32_t local_Day(uint32_t now, uint16_t* minute){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (minute) *minute = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
    return (uint32_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// What a phone command changed in a child's progress: the app keeps its own counts otherwise.
enum : uint8_t { TOUCH_LEVELS = 1, TOUCH_EXTRA = 2, TOUCH_ALL = 4 };
static volatile uint8_t touched[KID_CHILDREN];

static bool read_Settings(Kid_Settings_t& s){
    kid_Settings_Default(s);
    Kid_Settings_t* saved = (Kid_Settings_t*)heap_caps_calloc(1, sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (saved){
        mtb_Read_Nvs_Struct(KID_NVS_KEY, saved, sizeof(*saved));
        ok = saved->magic == KID_SETTINGS_MAGIC;
        if (ok) s = *saved;
        heap_caps_free(saved);
    }
    kid_Settings_Clamp(s);
    return ok;
}

// Sounds -----------------------------------------------------------------------------------------------------------------------
static void sound(const Snd_Note_t* notes, int count){ if (settings.sound) snd_Play(notes, count, 0.2f, false); }
static const Snd_Note_t RIGHT[] = {{880, 90}, {1318.5f, 160}};
static const Snd_Note_t WRONG[] = {{330, 140}, {262, 260}};
static const Snd_Note_t CATCH[] = {{1568, 50}, {2093, 70}};
static const Snd_Note_t HIT[] = {{147, 160}};
static const Snd_Note_t GEM[] = {{1760, 60}, {2349, 90}};
static const Snd_Note_t CRASH[] = {{220, 120}, {165, 120}, {110, 300}};
static const Snd_Note_t FANFARE[] = {{523.25f, 120}, {659.25f, 120}, {783.99f, 120}, {1046.5f, 350}, {0, 80}, {783.99f, 120}, {1046.5f, 500}};
static const Snd_Note_t TICK[] = {{1200, 25}};

static void say(const char* text){
    if (settings.speak) snd_Say(text, kid_Language_Code(settings.language), nullptr, 0, 0, false);
}

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Kid_Settings_t* begin_Change(void){
    Kid_Settings_t* next = (Kid_Settings_t*)heap_caps_malloc(sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void end_Change(Kid_Settings_t* next){
    kid_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    heap_caps_free(next);
}

static void cmd_Ack(JsonDocument&){}

static void cmd_Child_Slot(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["childSlot"], 0, KID_CHILDREN - 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    next->editing = (uint8_t)v;
    end_Change(next);
}

// {"childName"} | {"childAge"} | {"childOn"}
static void cmd_Child(JsonDocument& c){
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Child_t& k = next->children[next->editing];
    long v;
    bool ok = false;
    if (!c["childName"].isNull()){
        const char* n = c["childName"] | "";
        while (*n == ' ') n++;
        ok = n[0] && strlen(n) < sizeof(k.name);
        for (const char* p = n; ok && *p; p++) if ((unsigned char)*p < ' ') ok = false;
        if (ok) strcpy(k.name, n);
    } else if (!c["childAge"].isNull()){
        ok = mtb_Kit_Json_Range(c["childAge"], 4, 12, v);
        if (ok && k.age != v){
            k.age = (uint8_t)v;
            k.mathLevel = kid_Math_Level_For_Age(k.age);
            k.spellLevel = kid_Spell_Level_For_Age(k.age);
            touched[next->editing] |= TOUCH_LEVELS;
        }
    } else if (!c["childOn"].isNull()){
        ok = mtb_Kit_Json_Range(c["childOn"], 0, 1, v);
        if (ok){
            int on = 0;
            for (int i = 0; i < KID_CHILDREN; i++) if (next->children[i].used && i != next->editing) on++;
            if (!v && !on) ok = false;                      // one child at least
            else k.used = (uint8_t)v;
        }
    }
    if (!ok){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    end_Change(next);
}

static void cmd_Setting(JsonDocument& c){
    static const struct { const char* key; long lo, hi; uint8_t Kid_Settings_t::* field; } K[] = {
        {"limitMinutes", 0, 240, &Kid_Settings_t::limitMinutes}, {"bedtimeOn", 0, 1, &Kid_Settings_t::bedtimeOn},
        {"rewardMinutes", 0, 15, &Kid_Settings_t::rewardMinutes}, {"maxGameMinutes", 0, 120, &Kid_Settings_t::maxGameMinutes},
        {"readGoal", 5, 120, &Kid_Settings_t::readGoal}, {"speak", 0, 1, &Kid_Settings_t::speak}, {"language", 0, 7, &Kid_Settings_t::language},
        {"sound", 0, 1, &Kid_Settings_t::sound}, {"easyAnswers", 0, 1, &Kid_Settings_t::easyAnswers}};
    for (const auto& k : K){
        if (c[k.key].isNull()) continue;
        long v;
        Kid_Settings_t* next = mtb_Kit_Json_Range(c[k.key], k.lo, k.hi, v) ? begin_Change() : nullptr;
        if (!next) break;
        next->*k.field = (uint8_t)v;
        end_Change(next);
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}

static void cmd_Bedtime(JsonDocument& c){
    const int a = mtb_Kit_Parse_Clock(c["bedFrom"] | ""), z = mtb_Kit_Parse_Clock(c["bedTo"] | "");
    if (a < 0 || z < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    next->bedFrom = (uint16_t)a;
    next->bedTo = (uint16_t)z;
    end_Change(next);
}

static void cmd_Search_Tables(JsonDocument& c){
    JsonDocument out;
    JsonArray r = out.to<JsonArray>();
    for (int t = 2; t <= 12; t++){
        JsonObject o = r.add<JsonObject>();
        char n[24], v[4];
        snprintf(n, sizeof(n), "The %d times table", t);
        snprintf(v, sizeof(v), "%d", t);
        o["name"] = n;
        o["value"] = v;
    }
    if (kids_App) kids_App->mtb_App_Send_Search_Results("tables", c["query"] | "", r, nullptr);
}

static void cmd_Tables(JsonDocument& c){
    if (!c["tables"].is<JsonArray>()){ mtb_Ble_App_Cmd_Reject(); return; }
    uint16_t mask = 0;
    for (JsonVariant v : c["tables"].as<JsonArray>()){
        const int t = atoi(v["value"] | "0");
        if (t >= 2 && t <= 12) mask |= (uint16_t)(1u << t);
    }
    if (!mask){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    next->tables = mask;
    end_Change(next);
}

static void cmd_Words(JsonDocument& c){
    const char* w = c["words"] | "";
    if (strlen(w) >= KID_WORDS_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* p = w; *p; p++) if ((unsigned char)*p < ' ' && *p != '\n'){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    strcpy(next->words, w);
    end_Change(next);
}

static void cmd_Extra(JsonDocument&){
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Child_t& k = next->children[next->editing];
    kid_New_Day(k, local_Day((uint32_t)time(nullptr), nullptr));
    k.extraSeconds = (uint16_t)(k.extraSeconds + 900 > 0xFFFF ? 0xFFFF : k.extraSeconds + 900);
    touched[next->editing] |= TOUCH_EXTRA;
    end_Change(next);
}

static void cmd_Reset(JsonDocument&){
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    Kid_Child_t& k = next->children[next->editing];
    const Kid_Child_t old = k;
    kid_Child_Default(k, old.name, old.age);
    k.used = old.used;
    touched[next->editing] |= TOUCH_ALL;
    end_Change(next);
}

static void cmd_Play_As(JsonDocument&){
    Kid_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!next->children[next->editing].used){ heap_caps_free(next); mtb_Ble_App_Cmd_Reject(); return; }
    next->active = next->editing;
    end_Change(next);
    switchRequested = true;
}

static void register_Commands(Mtb_Applications* app){
    app->mtb_App_Set_Ble_Comm_Fns(cmd_Ack, cmd_Child_Slot, cmd_Child, cmd_Setting, cmd_Bedtime, cmd_Search_Tables, cmd_Tables, cmd_Words,
                                  cmd_Extra, cmd_Reset, cmd_Play_As, cmd_Ack);
}

// The knob and the keyboard -----------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -----------------------------------------------------------------------------------------------------------------------
EXT_RAM_BSS_ATTR static char wordBuffer[KID_WORDS_LEN];
EXT_RAM_BSS_ATTR static const char* wordList[40];
EXT_RAM_BSS_ATTR static const char* roundWords[KID_SPELL_ROUND];
static uint32_t rng = 1;
static bool dirty = false;

static Kid_Child_t& kid(void){ return settings.children[settings.active]; }

static void go_Menu(void){ play.screen = KID_SCR_MENU; play.feedback = 0; }

// Speaks a question for the youngest: "3 plus 4".
static void say_Question(void){
    if (!settings.speak || kid().mathLevel > 2) return;
    char t[48];
    snprintf(t, sizeof(t), "%d %s %d", play.q.a, play.q.op == '+' ? "plus" : play.q.op == '-' ? "take away" : play.q.op == 'x' ? "times" : "divided by", play.q.b);
    say(t);
}

static void next_Question(bool race){
    if (race) kid_Race_Question(settings.tables, rng, play.q);
    else kid_Math_Question(kid().mathLevel, settings.easyAnswers, rng, play.q);
    play.entry[0] = '\0';
    play.choice = 1;
    play.feedback = 0;
    if (!race) say_Question();
}

static void start_Maths(bool race){
    play.screen = race ? KID_SCR_RACE : KID_SCR_MATH;
    play.number = play.right = play.rightRun = play.wrongRun = 0;
    play.raceEnd = (uint32_t)time(nullptr) + KID_RACE_SECONDS;
    next_Question(race);
}

static void finish_Round(uint8_t skill, int right, int asked, bool race){
    Kid_Child_t& c = kid();
    const uint32_t today = local_Day((uint32_t)time(nullptr), nullptr);
    if (asked == 0){ go_Menu(); return; }
    const uint16_t given = kid_Round_Done(settings, c, skill, right, asked, today);
    bool best = false;
    if (race && right > c.bestRace){ c.bestRace = (uint16_t)right; best = true; }
    dirty = true;
    play.screen = KID_SCR_DONE;
    const bool good = right * 10 >= asked * 7;
    snprintf(play.title, sizeof(play.title), best ? "NEW BEST!" : good ? "WELL DONE!" : "GOOD TRY!");
    if (race) snprintf(play.line1, sizeof(play.line1), "%d RIGHT IN 60 SECONDS, +%d STARS", right, right);
    else snprintf(play.line1, sizeof(play.line1), "%d RIGHT OF %d, +%d STARS", right, asked, right);
    if (given) snprintf(play.line2, sizeof(play.line2), "+%u MINUTES OF GAMES", (unsigned)((given + 59) / 60));
    else if (good && settings.rewardMinutes) snprintf(play.line2, sizeof(play.line2), "TODAY'S GAME TIME IS ALL EARNED");
    else play.line2[0] = '\0';
    if (good || best) sound(FANFARE, 7);
    char t[64];
    snprintf(t, sizeof(t), "%s! %s.", good || best ? "Well done" : "Good try", c.name);
    say(t);
}

static void answer(int value){
    const bool race = play.screen == KID_SCR_RACE;
    const bool right = value == play.q.answer;
    Kid_Child_t& c = kid();
    if (play.q.skill < KID_SKILLS){
        c.asked[play.q.skill]++;
        if (right) c.right[play.q.skill]++;
    }
    if (right) play.right++;
    if (!race) c.mathLevel = kid_Adapt(c.mathLevel, KID_MATH_LEVELS - 1, play.rightRun, play.wrongRun, right);
    play.number++;
    play.feedback = right ? 1 : 2;
    play.feedbackUntil = millis() + (race ? (right ? 350 : 1200) : (right ? 900 : 2200));
    sound(right ? RIGHT : WRONG, 2);
}

static void start_Spelling(void){
    const int n = kid_Words(settings, kid().spellLevel, wordList, 40, wordBuffer, sizeof(wordBuffer));
    // Five different words (fewer if the list is short).
    int picked = 0;
    for (int tries = 0; picked < KID_SPELL_ROUND && tries < 200 && n; tries++){
        const char* w = wordList[kid_Random_Range(rng, 0, n - 1)];
        bool seen = false;
        for (int k = 0; k < picked; k++) if (roundWords[k] == w) seen = true;
        if (!seen || n < KID_SPELL_ROUND) roundWords[picked++] = w;
    }
    play.screen = KID_SCR_SPELL;
    play.number = play.right = play.rightRun = play.wrongRun = 0;
    play.word = nullptr;
}

static void next_Word(void){
    play.word = roundWords[play.number];
    play.typed[0] = '\0';
    play.letter = 0;
    play.lookPhase = 1;
    play.lookUntil = millis() + 4000;
    play.feedback = 0;
    char t[64];
    snprintf(t, sizeof(t), "Spell: %s", play.word);
    say(t);
}

static void check_Word(void){
    const bool right = kid_Spelling_Right(play.word, play.typed);
    Kid_Child_t& c = kid();
    c.asked[KID_SPELL]++;
    if (right){ c.right[KID_SPELL]++; play.right++; }
    if (!settings.words[0]) c.spellLevel = kid_Adapt(c.spellLevel, KID_SPELL_LEVELS - 1, play.rightRun, play.wrongRun, right);
    play.number++;
    play.feedback = right ? 1 : 2;
    play.feedbackUntil = millis() + (right ? 1500 : 3000);
    sound(right ? RIGHT : WRONG, 2);
}

static void start_Game(int which){
    if (!kid().gameSeconds) return;
    if (which == 0){ kid_Catch_Start(play.catcher); play.screen = KID_SCR_CATCH; }
    else { kid_Cave_Start(play.cave, rng); play.screen = KID_SCR_CAVE; }
}

static void lock_Screen(bool bedtime){
    snd_Stop();
    play.screen = KID_SCR_LOCKED;
    if (bedtime){
        snprintf(play.title, sizeof(play.title), "BEDTIME");
        snprintf(play.line1, sizeof(play.line1), "SLEEP WELL, %s", kid().name);
        snprintf(play.line2, sizeof(play.line2), "SEE YOU AT %02u:%02u", settings.bedTo / 60, settings.bedTo % 60);
    } else {
        snprintf(play.title, sizeof(play.title), "ALL DONE FOR TODAY");
        snprintf(play.line1, sizeof(play.line1), "GREAT WORK, %s!", kid().name);
        snprintf(play.line2, sizeof(play.line2), "SEE YOU TOMORROW");
    }
    mtb_Kit_Upper(play.line1);
}

static int used_Children(int* slots){
    int n = 0;
    for (int i = 0; i < KID_CHILDREN; i++) if (settings.children[i].used) slots[n++] = i;
    return n;
}

static void kids_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    register_Commands(THIS_APP);
    THIS_APP->mtb_App_Init();

    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(KID_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    longPressRequested = false;
    switchRequested = false;
    clicksRequested = 0;
    turns = 0;
    rng = esp_random() | 1;
    memset(&play, 0, sizeof(play));
    for (volatile uint8_t& t : touched) t = 0;
    int slots[KID_CHILDREN];
    play.screen = used_Children(slots) > 1 ? KID_SCR_PICK : KID_SCR_MENU;
    mtb_Usb_Input_Open(false);                         // a USB keyboard types answers and words
    appOpen = true;
    uint32_t lastSecond = 0, savedAt = millis(), phoneMs = 0, gameStepMs = millis();
    bool phoneDirty = true;
    dirty = false;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            // The phone sets names, ages and the rest; the app keeps its own counts (unless the phone reset them,
            // gave extra time, or changed an age and so the levels).
            Kid_Child_t* mine = (Kid_Child_t*)settings.children;
            Kid_Settings_t* incoming = &received;
            for (int i = 0; i < KID_CHILDREN; i++){
                Kid_Child_t& c = incoming->children[i];
                const Kid_Child_t m = mine[i];
                const uint8_t t = touched[i];
                touched[i] = 0;
                if (t & TOUCH_ALL) continue;
                const uint8_t mathLevel = c.mathLevel, spellLevel = c.spellLevel;
                const uint16_t extra = c.extraSeconds;
                char name[sizeof(c.name)];
                memcpy(name, c.name, sizeof(name));
                const uint8_t age = c.age, used = c.used;
                c = m;
                memcpy(c.name, name, sizeof(name));
                c.age = age;
                c.used = used;
                if (t & TOUCH_LEVELS){ c.mathLevel = mathLevel; c.spellLevel = spellLevel; }
                if (t & TOUCH_EXTRA) c.extraSeconds = extra;
            }
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            dirty = phoneDirty = true;
            if (switchRequested){ switchRequested = false; snd_Stop(); go_Menu(); }
            if (play.screen == KID_SCR_LOCKED) go_Menu();          // more time, a new bedtime: look again below
        }
        play.typing = mtb_Usb_Keyboard_Connected();

        // Each second: the day, the limits, the time spent.
        if (now != lastSecond && now > 1700000000u){
            lastSecond = now;
            uint16_t minute;
            const uint32_t today = local_Day(now, &minute);
            for (Kid_Child_t& c : settings.children) if (c.used && c.day != today){ kid_New_Day(c, today); dirty = true; }
            Kid_Child_t& c = kid();
            const bool busy = play.screen != KID_SCR_PICK && play.screen != KID_SCR_LOCKED;
            if (busy && (kid_Bedtime(settings, minute) || kid_Seconds_Left(settings, c) == 0)) lock_Screen(kid_Bedtime(settings, minute));
            else if (play.screen == KID_SCR_LOCKED && !kid_Bedtime(settings, minute) && kid_Seconds_Left(settings, c)) go_Menu();
            const bool game = play.screen == KID_SCR_CATCH || play.screen == KID_SCR_CAVE;
            const bool reading = play.screen == KID_SCR_READ && !play.readPaused;
            const bool learning = play.screen == KID_SCR_MATH || play.screen == KID_SCR_RACE || play.screen == KID_SCR_SPELL;
            if (game || reading || learning){
                kid_Spend_Second(c, game, reading);
                if (reading) play.readSeconds++;
                dirty = true;
            }
            if (game && !c.gameSeconds){
                play.screen = KID_SCR_DONE;
                snprintf(play.title, sizeof(play.title), "GAME TIME IS UP");
                snprintf(play.line1, sizeof(play.line1), "LEARN A LITTLE MORE");
                snprintf(play.line2, sizeof(play.line2), "TO EARN MORE GAME TIME");
            }
            if (play.screen == KID_SCR_RACE && !play.feedback && now >= play.raceEnd) finish_Round(KID_SKILLS, play.right, play.number, true);
            if (now % 30 == 0) phoneDirty = true;
        }

        // Input: the knob, then the keyboard.
        int turn = turns;
        if (turn) turns = 0;
        uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        bool back = longPressRequested;
        if (back) longPressRequested = false;
        char typed = 0;
        bool enter = false, backspace = false;
        Mtb_Usb_Input_t in;
        while (mtb_Usb_Input_Read(in, 0)){
            if (in.type != MTB_USB_KEY_DOWN) continue;
            switch (in.key){
                case 0x4F: case 0x52: turn++; break;                    // right, up
                case 0x50: case 0x51: turn--; break;                    // left, down
                case 0x28: case 0x58: enter = true; break;              // Enter
                case 0x2A: backspace = true; break;
                case 0x29: back = true; break;                          // Esc
                default: if (in.ascii >= ' ' && in.ascii <= '~') typed = in.ascii; break;
            }
        }
        // Enter and Space work as a click where nothing is typed.
        const bool typingScreen = (play.screen == KID_SCR_MATH || play.screen == KID_SCR_RACE) ? (play.typing && !play.q.choice && !settings.easyAnswers)
                                : play.screen == KID_SCR_SPELL ? play.typing && !play.lookPhase : false;
        if (!typingScreen){
            if (enter || typed == ' ') clicks = 1;
            if (backspace) back = true;
            typed = 0;
            enter = backspace = false;
        }

        switch (play.screen){
            case KID_SCR_PICK: {
                const int n = used_Children(slots);
                if (turn){ play.index = ((play.index + turn) % n + n) % n; sound(TICK, 1); }
                if (clicks == 1){ settings.active = (uint8_t)slots[play.index]; dirty = true; go_Menu(); play.index = 0; say(kid().name); }
                break;
            }
            case KID_SCR_MENU:
                if (turn){ play.index = ((play.index + turn) % KID_M_COUNT + KID_M_COUNT) % KID_M_COUNT; sound(TICK, 1); }
                if (clicks == 1){
                    switch (play.index){
                        case KID_M_MATH: start_Maths(false); break;
                        case KID_M_RACE: start_Maths(true); say("Ready, go!"); break;
                        case KID_M_SPELL: start_Spelling(); next_Word(); break;
                        case KID_M_READ: play.screen = KID_SCR_READ; play.readSeconds = 0; play.readPaused = false; say("Happy reading!"); break;
                        case KID_M_GAMES: play.screen = KID_SCR_GAMES; play.index = 0; break;
                        default: play.screen = KID_SCR_STARS; break;
                    }
                }
                if (back && used_Children(slots) > 1){ play.screen = KID_SCR_PICK; play.index = 0; }
                break;
            case KID_SCR_MATH: case KID_SCR_RACE: {
                const bool race = play.screen == KID_SCR_RACE;
                if (play.feedback){
                    if ((int32_t)(ms - play.feedbackUntil) >= 0 || clicks){
                        play.feedback = 0;
                        if (!race && play.number >= KID_ROUND) finish_Round(KID_SKILLS, play.right, play.number, false);
                        else if (race && now >= play.raceEnd) finish_Round(KID_SKILLS, play.right, play.number, true);
                        else next_Question(race);
                    }
                    break;
                }
                if (back){ finish_Round(KID_SKILLS, play.right, play.number, race); break; }
                const bool choices = play.q.choice || !play.typing || settings.easyAnswers;
                if (choices){
                    if (turn){ play.choice = ((play.choice + turn) % 3 + 3) % 3; sound(TICK, 1); }
                    if (clicks == 1) answer(play.q.choices[play.choice]);
                } else {
                    const size_t n = strlen(play.entry);
                    if (typed >= '0' && typed <= '9' && n < 6){ play.entry[n] = typed; play.entry[n + 1] = '\0'; }
                    if (backspace && n) play.entry[n - 1] = '\0';
                    if ((enter || clicks == 1) && n) answer(atoi(play.entry));
                }
                break;
            }
            case KID_SCR_SPELL:
                if (!play.word){ next_Word(); break; }
                if (play.feedback){
                    if ((int32_t)(ms - play.feedbackUntil) >= 0 || clicks){
                        play.feedback = 0;
                        if (play.number >= KID_SPELL_ROUND) finish_Round(KID_SPELL, play.right, play.number, false);
                        else next_Word();
                    }
                    break;
                }
                if (play.lookPhase){
                    if ((int32_t)(ms - play.lookUntil) >= 0 || clicks == 1 || enter){ play.lookPhase = 0; }
                    if (back){ finish_Round(KID_SPELL, play.right, play.number, false); }
                    break;
                }
                {
                    const size_t n = strlen(play.typed);
                    if (play.typing){
                        if (isalpha((unsigned char)typed) || typed == '\'' || typed == '-'){ if (n < sizeof(play.typed) - 1){ play.typed[n] = (char)tolower((unsigned char)typed); play.typed[n + 1] = '\0'; } }
                        if (backspace && n) play.typed[n - 1] = '\0';
                        if (enter && n) check_Word();
                        if (back) finish_Round(KID_SPELL, play.right, play.number, false);
                    } else {
                        if (turn){ play.letter = (uint8_t)(((play.letter + turn) % 26 + 26) % 26); sound(TICK, 1); }
                        if (clicks == 1 && n < sizeof(play.typed) - 1){ play.typed[n] = (char)('a' + play.letter); play.typed[n + 1] = '\0'; }
                        if (clicks == 2){
                            if (n) play.typed[n - 1] = '\0';
                            else { char t[64]; snprintf(t, sizeof(t), "Spell: %s", play.word); say(t); }
                        }
                        if (back){ if (n) check_Word(); else finish_Round(KID_SPELL, play.right, play.number, false); }
                    }
                }
                break;
            case KID_SCR_READ:
                if (clicks == 1){ play.readPaused = !play.readPaused; }
                if (back){
                    Kid_Child_t& c = kid();
                    const uint32_t today = local_Day(now, nullptr);
                    const uint16_t stars = kid_Reading_Done(c, (uint16_t)play.readSeconds, today);
                    dirty = true;
                    play.screen = KID_SCR_DONE;
                    snprintf(play.title, sizeof(play.title), "READING DONE");
                    snprintf(play.line1, sizeof(play.line1), "%u MINUTES, +%u STARS", (unsigned)(play.readSeconds / 60), stars);
                    if (c.readWeek[0] >= settings.readGoal){ snprintf(play.line2, sizeof(play.line2), "TODAY'S READING GOAL DONE!"); sound(FANFARE, 7); }
                    else snprintf(play.line2, sizeof(play.line2), "%u MORE MINUTES FOR TODAY'S GOAL", settings.readGoal - c.readWeek[0]);
                }
                break;
            case KID_SCR_GAMES:
                if (turn){ play.index = play.index ? 0 : 1; sound(TICK, 1); }
                if (clicks == 1) start_Game(play.index);
                if (back){ go_Menu(); play.index = KID_M_GAMES; }
                break;
            case KID_SCR_CATCH: case KID_SCR_CAVE: {
                const bool catcher = play.screen == KID_SCR_CATCH;
                const bool over = catcher ? play.catcher.over : play.cave.over;
                if (over){
                    if (clicks == 1) start_Game(catcher ? 0 : 1);
                    if (back){ play.screen = KID_SCR_GAMES; }
                    break;
                }
                if (back){ play.screen = KID_SCR_GAMES; break; }
                if (turn){ if (catcher) kid_Catch_Move(play.catcher, turn); else kid_Cave_Move(play.cave, -turn); }
                break;
            }
            case KID_SCR_STARS: if (clicks || back) go_Menu(); break;
            case KID_SCR_DONE:
                if (clicks == 1 || back){
                    const bool fromGame = !strcmp(play.title, "GAME TIME IS UP");
                    go_Menu();
                    play.index = fromGame ? KID_M_GAMES : play.index;
                }
                break;
            case KID_SCR_LOCKED:
                if (back && used_Children(slots) > 1){ play.screen = KID_SCR_PICK; play.index = 0; }
                break;
        }

        // The games run at 25 steps a second.
        if (play.screen == KID_SCR_CATCH || play.screen == KID_SCR_CAVE){
            while ((int32_t)(ms - gameStepMs) >= 40){
                gameStepMs += 40;
                if (play.screen == KID_SCR_CATCH){
                    const uint8_t e = kid_Catch_Step(play.catcher, rng);
                    if (e == KID_STAR || e == KID_HEART) sound(CATCH, 2);
                    else if (e == KID_ROCK) sound(play.catcher.over ? CRASH : HIT, play.catcher.over ? 3 : 1);
                } else {
                    const uint8_t e = kid_Cave_Step(play.cave, rng);
                    if (e == 1) sound(GEM, 2);
                    else if (e == 2) sound(CRASH, 3);
                }
            }
        } else gameStepMs = ms;

        Kid_View_t v = {&settings, &play, now, ms};
        kid_Draw(v);
        if (dirty && ms - savedAt > 30000){ dirty = false; savedAt = ms; mtb_Write_Nvs_Struct(KID_NVS_KEY, &settings, sizeof(settings)); phoneDirty = true; }
        if (phoneDirty && ms - phoneMs > 3000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(play.screen == KID_SCR_CATCH || play.screen == KID_SCR_CAVE ? 15 : 30);
    }
    snd_Stop();
    mtb_Usb_Input_Close();
    mtb_Write_Nvs_Struct(KID_NVS_KEY, &settings, sizeof(settings));
    appOpen = false;
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static void kid_Values(JsonObject values){
    Kid_Settings_t* s = (Kid_Settings_t*)heap_caps_malloc(sizeof(Kid_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *s = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*s);
    char t[160];
    const Kid_Child_t& e = s->children[s->editing];
    values["childSlot"] = s->editing;
    values["childName"] = e.name;
    values["childAge"] = e.age;
    values["childOn"] = e.used;
    values["limitMinutes"] = s->limitMinutes;
    values["bedtimeOn"] = s->bedtimeOn;
    snprintf(t, sizeof(t), "%02u:%02u", s->bedFrom / 60, s->bedFrom % 60);
    values["bedFrom"] = t;
    snprintf(t, sizeof(t), "%02u:%02u", s->bedTo / 60, s->bedTo % 60);
    values["bedTo"] = t;
    values["rewardMinutes"] = s->rewardMinutes;
    values["maxGameMinutes"] = s->maxGameMinutes;
    values["readGoal"] = s->readGoal;
    values["speak"] = s->speak;
    values["language"] = s->language;
    values["sound"] = s->sound;
    values["easyAnswers"] = s->easyAnswers;
    values["words"] = s->words;
    JsonArray tables = values["tables"].to<JsonArray>();
    for (int i = 2; i <= 12; i++){
        if (!(s->tables & (1u << i))) continue;
        JsonObject o = tables.add<JsonObject>();
        char n[24], v[4];
        snprintf(n, sizeof(n), "The %d times table", i);
        snprintf(v, sizeof(v), "%d", i);
        o["name"] = n;
        o["value"] = v;
    }
    // Every child at a glance.
    JsonArray rows = values["children"].to<JsonArray>();
    for (int i = 0; i < KID_CHILDREN; i++){
        const Kid_Child_t& c = s->children[i];
        if (!c.used) continue;
        JsonObject o = rows.add<JsonObject>();
        snprintf(t, sizeof(t), "%s, %u%s", c.name, c.age, i == s->active ? " (playing)" : "");
        o["label"] = t;
        snprintf(t, sizeof(t), "%u stars, %u-day streak, maths level %u, spelling level %u", c.stars, c.streak, c.mathLevel + 1, c.spellLevel + 1);
        o["value"] = t;
        o["state"] = "info";
    }
    // The child being changed, in detail.
    JsonArray d = values["progress"].to<JsonArray>();
    JsonObject o = d.add<JsonObject>();
    o["label"] = "Today";
    snprintf(t, sizeof(t), "%u min on Pixlpal (%u of games), %u right of %u, read %u min",
             (unsigned)(e.todaySeconds / 60), (unsigned)(e.todayGameSeconds / 60), e.todayRight, e.todayAsked, e.readWeek[0]);
    o["value"] = t;
    o["state"] = "info";
    static const char* const SKILL[KID_SKILLS] = {"Adding", "Taking away", "Times", "Dividing", "Spelling"};
    for (int k = 0; k < KID_SKILLS; k++){
        if (!e.asked[k]) continue;
        o = d.add<JsonObject>();
        o["label"] = SKILL[k];
        const unsigned pct = e.right[k] * 100u / e.asked[k];
        snprintf(t, sizeof(t), "%u right of %u (%u%%)", e.right[k], e.asked[k], pct);
        o["value"] = t;
        o["state"] = pct >= 80 ? "ok" : pct >= 60 ? "warn" : "error";
    }
    o = d.add<JsonObject>();
    o["label"] = "Reading this week";
    unsigned week = 0;
    for (uint16_t m : e.readWeek) week += m;
    snprintf(t, sizeof(t), "%u minutes (goal %u a day)", week, s->readGoal);
    o["value"] = t;
    o["state"] = week >= s->readGoal * 5u ? "ok" : "info";
    o = d.add<JsonObject>();
    o["label"] = "Times-tables race";
    snprintf(t, sizeof(t), "Best: %u right in 60 seconds", e.bestRace);
    o["value"] = t;
    o["state"] = "info";
    o = d.add<JsonObject>();
    o["label"] = "Game time saved";
    snprintf(t, sizeof(t), "%u:%02u", (unsigned)(e.gameSeconds / 60), (unsigned)(e.gameSeconds % 60));
    o["value"] = t;
    o["state"] = "info";
    heap_caps_free(s);
}
