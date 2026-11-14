// My First Computer's shell: what clicks, keys and the knob do on each screen. No panel and no files of its own
// (mfc_Files.cpp gives them on Pixlpal): tested on a PC with the screens.
#include "mfc_App.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#define COUNTS_PER_PIXEL    3
#define DRAG_START          3           // pixels moved before a press becomes a drag
#define BOOT_MS             2400
#define SHUTDOWN_MS         2200
#define TOAST_MS            2200
#define BADGE_MS            3200
#define PARENT_IDLE_MS      30000
#define CODE_WAIT_MS        60000

// HID usages.
#define K_ENTER     0x28
#define K_ESC       0x29
#define K_BACKSPACE 0x2A
#define K_TAB       0x2B
#define K_SPACE     0x2C
#define K_DELETE    0x4C
#define K_RIGHT     0x4F
#define K_LEFT      0x50
#define K_DOWN      0x51
#define K_UP        0x52
#define K_PAD_ENTER 0x58
#define MOD_CTRL    0x11
#define MOD_SHIFT   0x22
#define MOD_GUI     0x88

static const Mfc_Calc_Button_t CALC_BUTTONS[] = {
    {'C', 0, 0, 2, "C"}, {'<', 0, 2, 1, "<"}, {'/', 0, 3, 1, "/"},
    {'7', 1, 0, 1, "7"}, {'8', 1, 1, 1, "8"}, {'9', 1, 2, 1, "9"}, {'*', 1, 3, 1, "X"},
    {'4', 2, 0, 1, "4"}, {'5', 2, 1, 1, "5"}, {'6', 2, 2, 1, "6"}, {'-', 2, 3, 1, "-"},
    {'1', 3, 0, 1, "1"}, {'2', 3, 1, 1, "2"}, {'3', 3, 2, 1, "3"}, {'+', 3, 3, 1, "+"},
    {'0', 4, 0, 2, "0"}, {'.', 4, 2, 1, "."}, {'=', 4, 3, 1, "="},
};
const Mfc_Calc_Button_t* mfc_Calc_Buttons(int& count){ count = (int)(sizeof(CALC_BUTTONS) / sizeof(CALC_BUTTONS[0])); return CALC_BUTTONS; }

MFC_PSRAM static Mfc_File_t scratchList[MFC_LIST];
MFC_PSRAM static char loadBuffer[MFC_TEXT_MAX + 4];

static uint32_t rnd(uint32_t& r){ r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
static int rnd_Range(uint32_t& r, int lo, int hi){ return lo + (int)(rnd(r) % (uint32_t)(hi - lo + 1)); }

Mfc_Child_t& mfc_Me(Mfc_State_t& s){ return s.set->c[s.child % MFC_CHILDREN]; }
uint8_t mfc_Age(const Mfc_State_t& s){ return kk_Age(*s.k); }
static uint8_t level(Mfc_State_t& s){ return mfc_Level(mfc_Me(s), mfc_Age(s)); }

static void go(Mfc_State_t& s, uint8_t mode){
    s.mode = mode;
    s.modeMs = s.ms;
    s.startOpen = false;
    s.ctx.open = s.menu.open = false;
    s.focus = -1;
    s.redraw = true;
}

static void toast(Mfc_State_t& s, const char* text){
    snprintf(s.toast, sizeof(s.toast), "%s", text);
    s.toastMs = s.ms;
    s.redraw = true;
}

void mfc_Badge(Mfc_State_t& s, uint8_t b){
    if (!mfc_Award(mfc_Me(s), b, mfc_Age(s))) return;
    s.badge = (int8_t)b;
    s.badgeMs = s.ms;
    kk_Add_Stars(*s.k, 3);
    mfc_Sound(s, S_FANFARE);
    s.saveSettings = s.pushPhone = s.redraw = true;
}

// Dialogs ----------------------------------------------------------------------------------------------------------------
static void dialog(Mfc_State_t& s, uint8_t kind, const char* title, const char* text, uint8_t icon,
                   const char* b0, uint8_t a0, const char* b1 = nullptr, uint8_t a1 = A_NONE, const char* b2 = nullptr, uint8_t a2 = A_NONE){
    Mfc_Dialog_t& d = s.dlg;
    const uint8_t then = d.then, thenArg = d.thenArg;
    memset(&d, 0, sizeof(d));
    d.then = then;
    d.thenArg = thenArg;
    d.kind = kind;
    snprintf(d.title, sizeof(d.title), "%s", title);
    snprintf(d.text, sizeof(d.text), "%s", text ? text : "");
    d.icon = icon;
    d.button[0] = b0; d.action[0] = a0;
    d.button[1] = b1; d.action[1] = a1;
    d.button[2] = b2; d.action[2] = a2;
    d.buttons = (uint8_t)(b2 ? 3 : b1 ? 2 : 1);
    d.chosen = -1;
    s.startOpen = false;
    s.ctx.open = s.menu.open = false;
    s.focus = -1;
    s.redraw = true;
}

static void info(Mfc_State_t& s, const char* title, const char* text, uint8_t icon = 0){
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_INFO, title, text, icon, "OK", A_OK);
}

static void lesson(Mfc_State_t& s, const char* title, const char* text, bool good){
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_LESSON, title, text, good ? 3 : 2, "OK", A_OK);
    mfc_Sound(s, good ? S_YAY : S_ERROR);
    mfc_Speak(s, text);
}

static void close_Dialog(Mfc_State_t& s){ s.dlg.kind = D_NONE; s.focus = -1; s.redraw = true; }

// Files --------------------------------------------------------------------------------------------------------------------
static bool exists(Mfc_State_t& s, uint8_t folder, const char* name, uint8_t kind){
    const int n = mfc_Files_List(s.child, folder, scratchList, MFC_LIST);
    for (int i = 0; i < n; i++) if (scratchList[i].kind == kind && !strcmp(scratchList[i].name, name)) return true;
    return false;
}

static void default_Name(Mfc_State_t& s, uint8_t folder, const char* base, char* out, size_t size){
    static char taken[MFC_LIST][MFC_NAME_LEN + 1];
    const int n = mfc_Files_List(s.child, folder, scratchList, MFC_LIST);
    for (int i = 0; i < n; i++) snprintf(taken[i], sizeof(taken[0]), "%s", scratchList[i].name);
    mfc_Name_Next(base, taken, n, out, size);
}

static bool full(Mfc_State_t& s){ return mfc_Files_Count(s.child) >= MFC_FILES_MAX; }

static void say_Full(Mfc_State_t& s){
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_FULL, "THE DISK IS FULL", "There is no room for another file. Delete some files in My Files, then empty the Recycle Bin.", 2, "OK", A_OK);
    mfc_Sound(s, S_ERROR);
}

// The open program's work ---------------------------------------------------------------------------------------------------
static bool unsaved(Mfc_State_t& s){
    if (s.prog == MFC_P_PAINT) return s.paint.changed;
    if (s.prog == MFC_P_WRITE) return s.text.changed && s.text.len > 0;
    return false;
}

static void perform(Mfc_State_t& s, uint8_t then, uint8_t arg);

/** Writes the work as @p name. False (with a dialog) if it could not. */
static bool write_Work(Mfc_State_t& s, const char* name){
    const bool picture = s.prog == MFC_P_PAINT;
    const uint8_t folder = picture ? MFC_F_PICTURES : MFC_F_WRITING;
    if (!exists(s, folder, name, picture ? 0 : 1) && full(s)){ say_Full(s); return false; }
    const bool ok = picture ? mfc_File_Save_Picture(s.child, name, s.canvas, MFC_CANVAS_W, MFC_CANVAS_H)
                            : mfc_File_Save_Text(s.child, name, s.text.t);
    if (!ok){ info(s, "NOT SAVED", "Something went wrong and the file was not saved. Try again.", 2); return false; }
    if (picture){ snprintf(s.paint.name, sizeof(s.paint.name), "%s", name); s.paint.changed = false; mfc_Badge(s, MFC_B_ART); }
    else { snprintf(s.write.name, sizeof(s.write.name), "%s", name); s.text.changed = 0; mfc_Badge(s, MFC_B_WRITER); }
    char t[44], shown[MFC_NAME_LEN + 1];
    mfc_Name_Show(name, shown, sizeof(shown));
    snprintf(t, sizeof(t), "SAVED: %s", shown);
    toast(s, t);
    mfc_Sound(s, S_YAY);
    s.files.stale = true;
    s.fileCount = -1;
    s.pushPhone = true;
    return true;
}

static void ask_Name(Mfc_State_t& s, uint8_t then, uint8_t arg){
    const bool picture = s.prog == MFC_P_PAINT;
    s.dlg.then = then;
    s.dlg.thenArg = arg;
    dialog(s, D_SAVE, "SAVE AS", mfc_Have_Keyboard() ? "Type a name for it, then click SAVE." : "Click SAVE. (Plug in a keyboard to type your own name.)",
           1, "SAVE", A_SAVE, "CANCEL", A_CANCEL);
    char name[MFC_NAME_LEN + 1];
    default_Name(s, picture ? MFC_F_PICTURES : MFC_F_WRITING, picture ? "PICTURE" : "STORY", name, sizeof(name));
    mfc_Name_Show(name, s.dlg.field, sizeof(s.dlg.field));
    s.dlg.fieldOn = 1;
}

/** Save: as the name it has, else asks for one. Then @p then. */
static void save(Mfc_State_t& s, uint8_t then, uint8_t arg, bool asNew){
    const char* name = s.prog == MFC_P_PAINT ? s.paint.name : s.write.name;
    if (asNew || !name[0]){ ask_Name(s, then, arg); return; }
    if (write_Work(s, name)) perform(s, then, arg);
}

static void ask_Unsaved(Mfc_State_t& s, uint8_t then, uint8_t arg){
    const char* name = s.prog == MFC_P_PAINT ? s.paint.name : s.write.name;
    char t[100], shown[MFC_NAME_LEN + 1];
    mfc_Name_Show(name, shown, sizeof(shown));
    if (name[0]) snprintf(t, sizeof(t), "Do you want to save the changes to %s?", shown);
    else snprintf(t, sizeof(t), "Do you want to save your %s first?", s.prog == MFC_P_PAINT ? "picture" : "writing");
    s.dlg.then = then;
    s.dlg.thenArg = arg;
    dialog(s, D_UNSAVED, "SAVE YOUR WORK?", t, 1, "SAVE", A_SAVE, "DON'T SAVE", A_DONT_SAVE, "CANCEL", A_CANCEL);
    mfc_Sound(s, S_ERROR);
}

static void clear_Canvas(Mfc_State_t& s){
    if (!s.canvas) return;
    for (int i = 0; i < MFC_CANVAS_W * MFC_CANVAS_H; i++) s.canvas[i] = 0xFFFF;
}

static void open_Dialog_Files(Mfc_State_t& s){
    const bool picture = s.prog == MFC_P_PAINT;
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_OPEN, picture ? "OPEN A PICTURE" : "OPEN SOME WRITING", "", 1, "OPEN", A_OPEN_FILE, "CANCEL", A_CANCEL);
    s.dlg.folder = picture ? MFC_F_PICTURES : MFC_F_WRITING;
    s.files.count = (int8_t)mfc_Files_List(s.child, s.dlg.folder, s.files.list, MFC_LIST);
    int k = 0;                           // only this program's kind
    for (int i = 0; i < s.files.count; i++) if (s.files.list[i].kind == (picture ? 0 : 1)) s.files.list[k++] = s.files.list[i];
    s.files.count = (int8_t)k;
    s.files.stale = true;                // the Files window reads its own list again
    if (!k) snprintf(s.dlg.text, sizeof(s.dlg.text), picture ? "No pictures saved yet." : "No writing saved yet.");
}

static bool load_Into(Mfc_State_t& s, uint8_t prog, uint8_t folder, const char* name){
    if (prog == MFC_P_PAINT){
        if (!mfc_File_Load_Picture(s.child, folder, name, s.canvas, MFC_CANVAS_W, MFC_CANVAS_H)){ info(s, "CAN'T OPEN IT", "That picture could not be opened.", 2); return false; }
        snprintf(s.paint.name, sizeof(s.paint.name), "%s", name);
        s.paint.changed = false;
    } else {
        if (!mfc_File_Load_Text(s.child, folder, name, loadBuffer, sizeof(loadBuffer))){ info(s, "CAN'T OPEN IT", "That writing could not be opened.", 2); return false; }
        mfc_Text_Set(s.text, loadBuffer);
        s.text.cursor = 0;
        snprintf(s.write.name, sizeof(s.write.name), "%s", name);
        s.write.top = 0;
    }
    mfc_Sound(s, S_OPEN);
    return true;
}

// Programs -------------------------------------------------------------------------------------------------------------------
static void web_Go(Mfc_State_t& s, int page, bool remember);

static void start_Program(Mfc_State_t& s, uint8_t p){
    s.menu.open = s.ctx.open = false;
    switch (p){
        case MFC_P_MOUSE: memset(&s.ms_, 0, sizeof(s.ms_)); s.ms_.lesson = -1; s.ms_.held = -1; s.ms_.rng = s.ms | 1; break;
        case MFC_P_KEYS: memset(&s.keys, 0, sizeof(s.keys)); s.keys.rng = s.ms | 1; break;
        case MFC_P_PAINT:
            memset(&s.paint, 0, sizeof(s.paint));
            s.paint.tool = T_BRUSH; s.paint.color = 2; s.paint.rng = s.ms | 1;
            s.paint.kx = MFC_CANVAS_W / 2; s.paint.ky = MFC_CANVAS_H / 2;
            clear_Canvas(s);
            break;
        case MFC_P_WRITE: mfc_Text_Clear(s.text); memset(&s.write, 0, sizeof(s.write)); break;
        case MFC_P_BIN:
        case MFC_P_FILES:
            memset(&s.files, 0, sizeof(s.files));
            s.files.folder = p == MFC_P_BIN ? MFC_F_BIN : MFC_F_PICTURES;
            s.files.selected = s.files.dragging = -1;
            s.files.stale = true;
            break;
        case MFC_P_CALC: mfc_Calc_Clear(s.calc); s.calc.answers = 0; break;
        case MFC_P_PIANO: memset(&s.piano, 0, sizeof(s.piano)); s.piano.lit = -1; break;
        case MFC_P_WEB: {
            const uint8_t popup = s.web.popup == 2 ? 2 : 0;     // closed once, it stays closed this time on
            const bool back = s.web.usedBack, searched = s.web.searched;
            const uint8_t links = s.web.links;
            memset(&s.web, 0, sizeof(s.web));
            s.web.popup = popup; s.web.usedBack = back; s.web.searched = searched; s.web.links = links;
            s.web.laidOut = -1;
            web_Go(s, mfc_Web_Find("home"), true);
            break;
        }
        case MFC_P_LICENCE: s.licenceBadge = -1; break;
        default: break;
    }
}

static void close_Now(Mfc_State_t& s){
    if (s.prog == MFC_PROGRAMS) return;
    s.prog = MFC_PROGRAMS;
    s.minimized = false;
    s.menu.open = s.ctx.open = false;
    mfc_Sound(s, S_CLOSE);
    s.focus = -1;
    s.redraw = true;
}

static void open_Program(Mfc_State_t& s, uint8_t p){
    if (!mfc_Program_Open(*s.set, mfc_Me(s), mfc_Age(s), p)){ mfc_Sound(s, S_ERROR); return; }
    s.startOpen = false;
    const uint8_t as = p == MFC_P_BIN ? (uint8_t)MFC_P_FILES : p;
    if (s.prog == as && p != MFC_P_BIN){ s.minimized = false; s.redraw = true; return; }
    if (s.prog != MFC_PROGRAMS && unsaved(s)){ ask_Unsaved(s, THEN_OPEN_PROGRAM, p); return; }
    s.prog = as;
    s.minimized = false;
    s.toast[0] = '\0';                   // "click twice" was the first click of these two
    start_Program(s, p);
    mfc_Me(s).lastProgram = as;
    mfc_Sound(s, S_OPEN);
    s.focus = -1;
    s.saveSettings = s.pushPhone = s.redraw = true;
}

static void close_Program(Mfc_State_t& s){
    if (unsaved(s)){ ask_Unsaved(s, THEN_CLOSE, 0); return; }
    close_Now(s);
}

static void sign_Out(Mfc_State_t& s){
    s.prog = MFC_PROGRAMS;
    s.signedIn = false;
    s.pick = s.k->child;
    go(s, MFC_M_SIGNIN);
    mfc_Sound(s, S_BYE);
}

static void shut_Down(Mfc_State_t& s){
    s.prog = MFC_PROGRAMS;
    mfc_Badge(s, MFC_B_SHUTDOWN);
    s.badge = -1;                        // the goodbye is the celebration
    s.signedIn = false;
    go(s, MFC_M_SHUTDOWN);
    mfc_Sound(s, S_BYE);
    char t[48];
    snprintf(t, sizeof(t), "Goodbye %s!", kk_Name(*s.k));
    mfc_Speak(s, t);
}

static void perform(Mfc_State_t& s, uint8_t then, uint8_t arg){
    switch (then){
        case THEN_CLOSE: close_Now(s); break;
        case THEN_OPEN_PROGRAM: if (s.prog != MFC_PROGRAMS){ s.paint.changed = false; s.text.changed = 0; } open_Program(s, arg); break;
        case THEN_NEW:
            if (s.prog == MFC_P_PAINT){ clear_Canvas(s); s.paint.name[0] = '\0'; s.paint.changed = false; }
            else { mfc_Text_Clear(s.text); s.write.name[0] = '\0'; s.write.top = 0; }
            mfc_Sound(s, S_OPEN);
            break;
        case THEN_OPEN_DIALOG: open_Dialog_Files(s); break;
        case THEN_SIGN_OUT: sign_Out(s); break;
        case THEN_SHUT_DOWN: shut_Down(s); break;
        default: break;
    }
    s.redraw = true;
}

/** Asks about unsaved work first. */
static void then_Do(Mfc_State_t& s, uint8_t then, uint8_t arg){
    if (unsaved(s)){ ask_Unsaved(s, then, arg); return; }
    perform(s, then, arg);
}

// The Internet ------------------------------------------------------------------------------------------------------------------
static void web_Go(Mfc_State_t& s, int page, bool remember){
    Mfc_Web_t& w = s.web;
    if (page < 0) return;
    if (remember){
        if (w.count && w.history[w.at] == page && w.page == page) return;
        if (w.count) w.count = (uint8_t)(w.at + 1);          // forward pages are forgotten
        if (w.count >= MFC_HISTORY){ memmove(w.history, w.history + 1, MFC_HISTORY - 1); w.count--; }
        w.history[w.count] = (int8_t)page;
        w.at = w.count++;
    }
    w.page = (int8_t)page;
    w.scroll = 0;
    w.laidOut = -1;
    w.clubField = 0;
    if (mfc_Web_Page(page).kind == MFC_PAGE_GAMES && w.popup != 2) w.popup = 0;   // it shows after a moment
    s.focus = -1;
    s.redraw = true;
}

static void web_Search(Mfc_State_t& s){
    Mfc_Web_t& w = s.web;
    int found[6];
    w.resultCount = (uint8_t)mfc_Web_Search(w.query, found, 6);
    for (int i = 0; i < w.resultCount; i++) w.results[i] = (int8_t)found[i];
    w.searchOn = false;
    web_Go(s, mfc_Web_Find("results"), true);
    mfc_Sound(s, S_CLICK);
}

static void web_Link(Mfc_State_t& s, int page){
    if (page < 0) return;
    s.web.links++;
    web_Go(s, page, true);
    mfc_Sound(s, S_CLICK);
    if (s.web.links >= 5 && s.web.usedBack) mfc_Badge(s, MFC_B_WEB);
}

static void web_Back(Mfc_State_t& s, int dir){
    Mfc_Web_t& w = s.web;
    const int to = (int)w.at + dir;
    if (to < 0 || to >= w.count){ mfc_Sound(s, S_ERROR); return; }
    w.at = (uint8_t)to;
    web_Go(s, w.history[w.at], false);
    if (dir < 0){ w.usedBack = true; if (w.links >= 5) mfc_Badge(s, MFC_B_WEB); }
    mfc_Sound(s, S_CLICK);
}

// Mouse School -------------------------------------------------------------------------------------------------------------------
static bool lesson_Open(Mfc_State_t& s, int l){ return l == L_SCROLL ? level(s) >= 2 : l == L_RIGHT ? level(s) >= 3 : true; }

static const uint8_t LESSON_BADGE[MFC_LESSONS] = {MFC_B_POINT, MFC_B_CLICK, MFC_B_DOUBLE, MFC_B_DRAG, MFC_B_SCROLL, MFC_B_RIGHT};

static void hint(Mfc_State_t& s, const char* text){
    snprintf(s.ms_.hint, sizeof(s.ms_.hint), "%s", text);
    s.ms_.hintMs = s.ms;
    s.redraw = true;
}

static void balloon(Mfc_Mouse_School_t& m, Mfc_Thing_t& t){
    t.x = (int16_t)rnd_Range(m.rng, 20, 236);
    t.y = (int16_t)(MFC_BAR_Y + rnd_Range(m.rng, 0, 40));
    t.kind = (uint8_t)rnd_Range(m.rng, 0, 5);
    t.state = 0;
}

static void lesson_Start(Mfc_State_t& s, int l){
    Mfc_Mouse_School_t& m = s.ms_;
    const uint32_t rng = m.rng;
    memset(&m, 0, sizeof(m));
    m.rng = rng ? rng : 1;
    m.lesson = (int8_t)l;
    m.held = -1;
    switch (l){
        case L_POINT:
            m.count = 5; m.goal = 5;
            for (int i = 0; i < m.count; i++){ m.t[i].x = (int16_t)(28 + i * 48 + rnd_Range(m.rng, -6, 6)); m.t[i].y = (int16_t)rnd_Range(m.rng, 34, 100); }
            hint(s, "MOVE THE POINTER ONTO EACH STAR");
            break;
        case L_CLICK:
            m.count = 3; m.goal = 8;
            for (int i = 0; i < m.count; i++){ balloon(m, m.t[i]); m.t[i].y = (int16_t)(60 + i * 25); }
            hint(s, "CLICK A BALLOON TO POP IT");
            break;
        case L_DOUBLE:
            m.count = 4; m.goal = 4;
            for (int i = 0; i < m.count; i++){ m.t[i].x = (int16_t)(40 + i * 58); m.t[i].y = 70; m.t[i].kind = (uint8_t)i; }
            hint(s, "DOUBLE CLICK A PRESENT: CLICK TWICE, QUICKLY");
            break;
        case L_DRAG:
            m.count = 4; m.goal = 4;
            for (int i = 0; i < m.count; i++){ m.t[i].x = (int16_t)(22 + (i % 2) * 40); m.t[i].y = (int16_t)(40 + (i / 2) * 40); m.t[i].kind = (uint8_t)i; }
            hint(s, "HOLD THE BUTTON DOWN AND DRAG EACH ANIMAL HOME");
            break;
        case L_SCROLL:
            m.count = 1; m.goal = 1;
            m.t[0].x = 128; m.t[0].y = MFC_SCROLL_H - 26;
            hint(s, "ROLL THE MOUSE WHEEL TO SCROLL DOWN TO THE TREASURE");
            break;
        case L_RIGHT:
            m.count = 3; m.goal = 3;
            for (int i = 0; i < m.count; i++){ m.t[i].x = (int16_t)(40 + i * 40); m.t[i].y = 84; m.t[i].kind = (uint8_t)i; }
            m.want = (uint8_t)rnd_Range(m.rng, 0, 2);
            hint(s, "RIGHT CLICK THE POT WITH THE STAR, THEN CHOOSE PAINT");
            break;
    }
    mfc_Sound(s, S_OPEN);
    s.focus = -1;
    s.redraw = true;
}

static void lesson_Progress(Mfc_State_t& s){
    Mfc_Mouse_School_t& m = s.ms_;
    m.done++;
    if (m.done >= m.goal){
        mfc_Sound(s, S_YAY);
        mfc_Badge(s, LESSON_BADGE[m.lesson]);
        hint(s, "WELL DONE!");
    } else mfc_Sound(s, S_POP);
    s.redraw = true;
}

static bool lesson_Done(const Mfc_State_t& s){ return s.ms_.lesson >= 0 && s.ms_.done >= s.ms_.goal; }

static int barn_X0(void){ return 170; }

static void drop_Animal(Mfc_State_t& s){
    Mfc_Mouse_School_t& m = s.ms_;
    if (m.held < 0) return;
    Mfc_Thing_t& t = m.t[m.held];
    if (t.x >= barn_X0() - 4 && t.y >= 40){
        t.state = 1;
        t.x = (int16_t)(barn_X0() + 14 + (m.done % 2) * 30);
        t.y = (int16_t)(84 + (m.done / 2) * 0);
        lesson_Progress(s);
    } else hint(s, "KEEP HOLDING THE BUTTON UNTIL IT IS IN THE BARN");
    m.held = -1;
    s.redraw = true;
}

// Paint ------------------------------------------------------------------------------------------------------------------------
static void plot(Mfc_State_t& s, int x, int y, int r, uint16_t c){
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++){
            if (dx * dx + dy * dy > r * r + r) continue;
            const int X = x + dx, Y = y + dy;
            if (X >= 0 && Y >= 0 && X < MFC_CANVAS_W && Y < MFC_CANVAS_H) s.canvas[Y * MFC_CANVAS_W + X] = c;
        }
}

static int brush_Radius(uint8_t tool){ return tool == T_PENCIL ? 0 : tool == T_BRUSH ? 1 : tool == T_ERASER ? 3 : 4; }

static void spray(Mfc_State_t& s, int x, int y){
    const uint16_t c = mfc_Paint_Color(s.paint.color);
    for (int i = 0; i < 6; i++){
        const int dx = rnd_Range(s.paint.rng, -6, 6), dy = rnd_Range(s.paint.rng, -6, 6);
        if (dx * dx + dy * dy <= 36) plot(s, x + dx, y + dy, 0, c);
    }
}

static void stroke(Mfc_State_t& s, int x0, int y0, int x1, int y1){
    if (s.paint.tool == T_SPRAY){ spray(s, x1, y1); return; }
    const uint16_t c = s.paint.tool == T_ERASER ? 0xFFFF : mfc_Paint_Color(s.paint.color);
    const int r = brush_Radius(s.paint.tool);
    const int dx = abs(x1 - x0), dy = abs(y1 - y0), steps = dx > dy ? dx : dy;
    for (int i = 0; i <= steps; i++){
        const int x = steps ? x0 + (x1 - x0) * i / steps : x0, y = steps ? y0 + (y1 - y0) * i / steps : y0;
        plot(s, x, y, r, c);
    }
}

static void fill(Mfc_State_t& s, int x, int y){
    const uint16_t from = s.canvas[y * MFC_CANVAS_W + x], to = mfc_Paint_Color(s.paint.color);
    if (from == to) return;
    MFC_PSRAM static uint32_t stack[2048];
    int n = 0;
    stack[n++] = (uint32_t)(y << 16 | x);
    while (n > 0){
        const uint32_t p = stack[--n];
        int px = (int)(p & 0xFFFF), py = (int)(p >> 16);
        while (px > 0 && s.canvas[py * MFC_CANVAS_W + px - 1] == from) px--;
        bool up = false, down = false;
        for (; px < MFC_CANVAS_W && s.canvas[py * MFC_CANVAS_W + px] == from; px++){
            s.canvas[py * MFC_CANVAS_W + px] = to;
            if (py > 0){
                const bool f = s.canvas[(py - 1) * MFC_CANVAS_W + px] == from;
                if (f && !up && n < 2048){ stack[n++] = (uint32_t)((py - 1) << 16 | px); up = true; } else if (!f) up = false;
            }
            if (py + 1 < MFC_CANVAS_H){
                const bool f = s.canvas[(py + 1) * MFC_CANVAS_W + px] == from;
                if (f && !down && n < 2048){ stack[n++] = (uint32_t)((py + 1) << 16 | px); down = true; } else if (!f) down = false;
            }
        }
    }
}

static bool on_Canvas(int x, int y){ return x >= MFC_CANVAS_X && y >= MFC_CANVAS_Y && x < MFC_CANVAS_X + MFC_CANVAS_W && y < MFC_CANVAS_Y + MFC_CANVAS_H; }

static void paint_Press(Mfc_State_t& s){
    const int x = s.px - MFC_CANVAS_X, y = s.py - MFC_CANVAS_Y;
    if (s.paint.tool == T_FILL){ fill(s, x, y); s.paint.changed = true; mfc_Sound(s, S_POP); s.redraw = true; return; }
    s.paint.drawing = true;
    s.paint.lastX = (int16_t)x; s.paint.lastY = (int16_t)y;
    stroke(s, x, y, x, y);
    s.paint.changed = true;
    s.redraw = true;
}

static void paint_Move(Mfc_State_t& s){
    if (!s.paint.drawing) return;
    int x = s.px - MFC_CANVAS_X, y = s.py - MFC_CANVAS_Y;
    if (x < -8) x = -8;
    if (y < -8) y = -8;
    if (x > MFC_CANVAS_W + 8) x = MFC_CANVAS_W + 8;
    if (y > MFC_CANVAS_H + 8) y = MFC_CANVAS_H + 8;
    stroke(s, s.paint.lastX, s.paint.lastY, x, y);
    s.paint.lastX = (int16_t)x; s.paint.lastY = (int16_t)y;
    s.redraw = true;
}

// Menus -------------------------------------------------------------------------------------------------------------------------
static void menu_Bar(Mfc_State_t& s, uint8_t which, int16_t x){
    Mfc_Menu_t& m = s.menu;
    if (m.open && m.which == which){ m.open = false; s.redraw = true; return; }
    memset(&m, 0, sizeof(m));
    m.open = true;
    m.which = which;
    m.x = x;
    m.y = MFC_TOP + 10;
    auto add = [&](const char* label, uint8_t a){ if (m.count < MFC_MENU_ITEMS){ m.label[m.count] = label; m.action[m.count++] = a; } };
    if (which == 0){ add("NEW", A_NEW); add("OPEN...", A_OPEN); add("SAVE", A_SAVE); add("SAVE AS...", A_SAVE_AS); }
    else if (s.prog == MFC_P_WRITE){ add("BIG TEXT", A_BIG_TEXT); add("SMALL TEXT", A_SMALL_TEXT); add("CHANGE COLOUR", A_TEXT_COLOR); }
    else if (s.prog == MFC_P_PAINT){ add("CLEAR PICTURE", A_CLEAR); }
    s.focus = -1;
    mfc_Sound(s, S_CLICK);
    s.redraw = true;
}

static void context(Mfc_State_t& s, uint8_t target){
    Mfc_Menu_t& m = s.ctx;
    memset(&m, 0, sizeof(m));
    m.open = true;
    m.x = s.px; m.y = s.py;
    m.target = target;
    s.focus = -1;
    s.redraw = true;
}

static void ctx_Add(Mfc_State_t& s, const char* label, uint8_t a){ Mfc_Menu_t& m = s.ctx; if (m.count < MFC_MENU_ITEMS){ m.label[m.count] = label; m.action[m.count++] = a; } }

// My Files ---------------------------------------------------------------------------------------------------------------------
static void files_Refresh(Mfc_State_t& s){
    Mfc_Files_View_t& f = s.files;
    if (!f.stale) return;
    f.count = (int8_t)mfc_Files_List(s.child, f.folder, f.list, MFC_LIST);
    if (f.selected >= f.count) f.selected = -1;
    if (f.top > f.count - MFC_FL_ROWS) f.top = (int8_t)(f.count > MFC_FL_ROWS ? f.count - MFC_FL_ROWS : 0);
    f.stale = false;
}

static void files_Open(Mfc_State_t& s, int i){
    Mfc_Files_View_t& f = s.files;
    if (i < 0 || i >= f.count) return;
    if (f.folder == MFC_F_BIN){ info(s, "IT'S IN THE BIN", "Things in the Recycle Bin can't be opened. Click RESTORE to bring it back first."); return; }
    const Mfc_File_t file = f.list[i];
    const uint8_t prog = file.kind == 0 ? MFC_P_PAINT : MFC_P_WRITE;
    if (!mfc_Program_Open(*s.set, mfc_Me(s), mfc_Age(s), prog)){ info(s, "NOT YET", "That program isn't on this computer yet."); return; }
    open_Program(s, prog);
    if (s.prog == prog) load_Into(s, prog, file.kind == 0 ? MFC_F_PICTURES : MFC_F_WRITING, file.name);
}

static void files_Delete(Mfc_State_t& s, int i){
    Mfc_Files_View_t& f = s.files;
    if (i < 0 || i >= f.count) return;
    if (f.folder == MFC_F_BIN) return;
    if (mfc_File_To_Bin(s.child, f.folder, f.list[i].name, f.list[i].kind)){
        mfc_Sound(s, S_TRASH);
        toast(s, "MOVED TO THE RECYCLE BIN");
        f.deletedOne = true;
        f.stale = true;
        f.selected = -1;
        s.fileCount = -1;
        s.pushPhone = true;
    }
}

static void files_Restore(Mfc_State_t& s, int i){
    Mfc_Files_View_t& f = s.files;
    if (i < 0 || i >= f.count || f.folder != MFC_F_BIN) return;
    if (mfc_File_Restore(s.child, f.list[i].name, f.list[i].kind)){
        mfc_Sound(s, S_YAY);
        toast(s, f.list[i].kind == 0 ? "BACK IN MY PICTURES" : "BACK IN MY WRITING");
        f.restoredOne = true;
        f.stale = true;
        f.selected = -1;
        s.fileCount = -1;
        s.pushPhone = true;
        if (f.deletedOne) mfc_Badge(s, MFC_B_TIDY);
    }
}

static void files_Rename(Mfc_State_t& s, int i){
    Mfc_Files_View_t& f = s.files;
    if (i < 0 || i >= f.count) return;
    if (f.folder == MFC_F_BIN){ info(s, "IT'S IN THE BIN", "Restore it first, then rename it."); return; }
    if (!mfc_Have_Keyboard()){ info(s, "A KEYBOARD, PLEASE", "Plug in a keyboard to type a new name."); return; }
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_RENAME, "RENAME", "Type the new name, then click RENAME.", 1, "RENAME", A_RENAME, "CANCEL", A_CANCEL);
    mfc_Name_Show(f.list[i].name, s.dlg.field, sizeof(s.dlg.field));
    s.dlg.fieldOn = 1;
    s.dlg.chosen = (int8_t)i;
}

static void files_Context(Mfc_State_t& s, int i){
    Mfc_Files_View_t& f = s.files;
    f.selected = (int8_t)i;
    context(s, (uint8_t)i);
    if (f.folder == MFC_F_BIN){ ctx_Add(s, "RESTORE", A_RESTORE); ctx_Add(s, "DELETE FOREVER", A_EMPTY_BIN); }
    else { ctx_Add(s, "OPEN", A_OPEN_FILE); ctx_Add(s, "RENAME", A_RENAME); ctx_Add(s, "DELETE", A_DELETE); }
}

static void empty_Bin(Mfc_State_t& s, int only){
    Mfc_Files_View_t& f = s.files;
    const int n = mfc_Files_List(s.child, MFC_F_BIN, scratchList, MFC_LIST);
    for (int i = 0; i < n; i++){
        if (only >= 0 && strcmp(scratchList[i].name, f.list[only].name)) continue;
        mfc_File_Erase(s.child, MFC_F_BIN, scratchList[i].name, scratchList[i].kind);
    }
    mfc_Sound(s, S_TRASH);
    toast(s, only >= 0 ? "GONE FOR GOOD" : "THE RECYCLE BIN IS EMPTY");
    f.stale = true;
    f.selected = -1;
    s.fileCount = -1;
    s.pushPhone = true;
}

// Actions (menus, dialogs) ---------------------------------------------------------------------------------------------------------
static void action(Mfc_State_t& s, uint8_t a){
    Mfc_Files_View_t& f = s.files;
    switch (a){
        case A_NEW: then_Do(s, THEN_NEW, 0); break;
        case A_OPEN: then_Do(s, THEN_OPEN_DIALOG, 0); break;
        case A_SAVE: save(s, THEN_NOTHING, 0, false); break;
        case A_SAVE_AS: save(s, THEN_NOTHING, 0, true); break;
        case A_BIG_TEXT: s.write.big = 1; s.write.top = 0; break;
        case A_SMALL_TEXT: s.write.big = 0; s.write.top = 0; break;
        case A_TEXT_COLOR: s.write.color = (uint8_t)((s.write.color + 1) % 4); break;
        case A_CLEAR: clear_Canvas(s); s.paint.changed = true; mfc_Sound(s, S_TRASH); break;
        case A_OPEN_FILE: files_Open(s, f.selected); break;
        case A_RENAME: files_Rename(s, f.selected); break;
        case A_DELETE: files_Delete(s, f.selected); break;
        case A_RESTORE: files_Restore(s, f.selected); break;
        case A_EMPTY_BIN:
            s.dlg.then = THEN_NOTHING;
            dialog(s, D_EMPTY_BIN, "DELETE FOREVER?", f.selected >= 0 && s.ctx.open ? "This file will be gone for good. Are you sure?"
                                                       : "Everything in the Recycle Bin will be gone for good. Are you sure?",
                   2, "YES, DELETE", A_OK, "NO", A_CANCEL);
            s.dlg.chosen = s.ctx.open ? f.selected : -1;
            break;
        case A_ICON_OPEN: open_Program(s, s.ctx.target); break;
        case A_ICON_ABOUT: {
            static const char* const ABOUT[MFC_PROGRAMS] = {
                "Lessons for the mouse: point, click, double click, drag, scroll and right click.",
                "Find the keys, type your name, and learn Space, Enter, Backspace, Shift and the arrows.",
                "Draw and paint pictures, then save them in My Pictures.",
                "Write stories and notes, then save them in My Writing.",
                "Your pictures and writing: open, rename and delete them.",
                "Work out sums with numbers.",
                "Play music, or learn a song by following the lights.",
                "A pretend internet to practise on. It never goes online.",
                "Change the wallpaper, the window colour, the pointer and the sound.",
                "Your Computer Licence: every badge you have earned.",
                "Deleted files wait here. Restore them, or empty the bin."};
            s.dlg.then = THEN_NOTHING;
            dialog(s, D_ABOUT, mfc_Program_Name(s.ctx.target), ABOUT[s.ctx.target % MFC_PROGRAMS], 0, "OK", A_OK);
            break;
        }
        case A_COLOR0: case A_COLOR1: case A_COLOR2: {     // Mouse School: the pot's menu
            Mfc_Mouse_School_t& m = s.ms_;
            if (a == A_COLOR0){
                if (s.ctx.target == m.want){
                    m.t[m.want].state = 1;
                    m.round++;
                    lesson_Progress(s);
                    if (!lesson_Done(s)){
                        int next = m.want;
                        for (int i = 0; i < 3 && next == m.want; i++) next = (m.want + 1 + rnd_Range(m.rng, 0, 1)) % 3;
                        m.want = (uint8_t)next;
                        for (int i = 0; i < 3; i++) m.t[i].state = 0;
                        hint(s, "NOW THE POT WITH THE STAR: RIGHT CLICK, PAINT");
                    }
                } else hint(s, "THAT'S NOT THE POT WITH THE STAR. TRY ANOTHER");
            } else hint(s, a == A_COLOR1 ? "SNIFF... IT SMELLS OF PAINT! CHOOSE PAINT" : "WHOOPS, IT WOBBLED. CHOOSE PAINT");
            break;
        }
        default: break;
    }
    s.ctx.open = s.menu.open = false;
    s.redraw = true;
}

static void dialog_Button(Mfc_State_t& s, int b){
    Mfc_Dialog_t& d = s.dlg;
    if (b < 0 || b >= d.buttons) return;
    const uint8_t a = d.action[b];
    const uint8_t kind = d.kind, then = d.then, arg = d.thenArg;
    mfc_Sound(s, S_CLICK);
    switch (kind){
        case D_UNSAVED:
            close_Dialog(s);
            if (a == A_SAVE) save(s, then, arg, false);
            else if (a == A_DONT_SAVE){ s.paint.changed = false; s.text.changed = 0; perform(s, then, arg); }
            break;
        case D_SAVE: {
            if (a != A_SAVE){ close_Dialog(s); break; }
            char name[MFC_NAME_LEN + 1];
            mfc_Name_Clean(d.field, name, sizeof(name));
            if (!name[0]){ snprintf(d.text, sizeof(d.text), "A name needs letters or numbers. Try again."); mfc_Sound(s, S_ERROR); break; }
            const bool picture = s.prog == MFC_P_PAINT;
            const char* current = picture ? s.paint.name : s.write.name;
            if (strcmp(name, current) && exists(s, picture ? MFC_F_PICTURES : MFC_F_WRITING, name, picture ? 0 : 1)){
                snprintf(d.text, sizeof(d.text), "There is already a file with that name. Type another name.");
                mfc_Sound(s, S_ERROR);
                break;
            }
            close_Dialog(s);
            if (write_Work(s, name)) perform(s, then, arg);
            break;
        }
        case D_RENAME: {
            if (a != A_RENAME){ close_Dialog(s); break; }
            Mfc_Files_View_t& f = s.files;
            char name[MFC_NAME_LEN + 1];
            mfc_Name_Clean(d.field, name, sizeof(name));
            if (!name[0]){ snprintf(d.text, sizeof(d.text), "A name needs letters or numbers. Try again."); mfc_Sound(s, S_ERROR); break; }
            const int i = d.chosen;
            if (i < 0 || i >= f.count){ close_Dialog(s); break; }
            if (!strcmp(name, f.list[i].name)){ close_Dialog(s); break; }
            if (exists(s, f.folder, name, f.list[i].kind)){ snprintf(d.text, sizeof(d.text), "There is already a file with that name. Type another name."); mfc_Sound(s, S_ERROR); break; }
            close_Dialog(s);
            if (mfc_File_Rename(s.child, f.folder, f.list[i].name, name, f.list[i].kind)){
                toast(s, "RENAMED");
                mfc_Badge(s, MFC_B_RENAME);
                f.stale = true;
                s.fileCount = -1;
                s.pushPhone = true;
            }
            break;
        }
        case D_EMPTY_BIN: {
            const int only = d.chosen;
            close_Dialog(s);
            if (a == A_OK) empty_Bin(s, only);
            break;
        }
        case D_OPEN: {
            if (a != A_OPEN_FILE){ close_Dialog(s); break; }
            const int i = d.chosen;
            if (i < 0 || i >= s.files.count){ snprintf(d.text, sizeof(d.text), "Click a file first, then OPEN."); mfc_Sound(s, S_ERROR); break; }
            const Mfc_File_t file = s.files.list[i];
            close_Dialog(s);
            load_Into(s, s.prog, d.folder, file.name);
            break;
        }
        default: close_Dialog(s); break;
    }
    s.redraw = true;
}

// Clicks ------------------------------------------------------------------------------------------------------------------------
static int hit_At(const Mfc_State_t& s, int x, int y){
    for (int i = s.hitCount - 1; i >= 0; i--){
        const Mfc_Hit_t& h = s.hits[i];
        if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) return i;
    }
    return -1;
}

static uint16_t id_At(const Mfc_State_t& s, int x, int y){ const int i = hit_At(s, x, y); return i >= 0 ? s.hits[i].id : 0; }

static void keys_Start(Mfc_State_t& s, uint8_t activity);

static void parent_Digit(Mfc_State_t& s, char d);

/** A click (left button let go where it was pressed), or the knob's click (@p knob: it opens, like two clicks). */
static void click(Mfc_State_t& s, uint16_t id, bool twice, bool knob){
    const uint8_t kind = MFC_HIT_KIND(id), i = MFC_HIT_INDEX(id);
    // Something open on top closes when clicking elsewhere.
    if (s.ctx.open && kind != H_CTX_ITEM){ s.ctx.open = false; s.redraw = true; if (kind == H_DESKTOP) return; }
    if (s.menu.open && kind != H_MENU_ITEM && kind != H_MENU){ s.menu.open = false; s.redraw = true; }
    if (s.startOpen && kind != H_START_ITEM && kind != H_START){ s.startOpen = false; s.redraw = true; if (kind == H_DESKTOP) return; }
    s.redraw = true;
    switch (kind){
        // Signing in, the power, the parent ----------------------------------------------------------------------------
        case H_AVATAR: mfc_Sign_In(s, i); return;
        case H_PICTURE: {
            if (s.secretN < MFC_SECRET_LEN) s.secretTry[s.secretN++] = (uint8_t)(i + 1);
            mfc_Sound(s, S_CLICK);
            if (s.secretN < MFC_SECRET_LEN) return;
            Mfc_Child_t& me = mfc_Me(s);
            if (s.secretMaking){
                memcpy(me.secret, s.secretTry, sizeof(me.secret));
                s.saveSettings = s.pushPhone = true;
                s.secretN = 0;
                go(s, MFC_M_DESKTOP);
                toast(s, "YOUR SECRET IS MADE. REMEMBER IT!");
                mfc_Sound(s, S_YAY);
            } else if (!memcmp(me.secret, s.secretTry, sizeof(me.secret))){
                s.secretN = 0;
                s.secretFails = 0;
                go(s, MFC_M_DESKTOP);
                mfc_Sound(s, S_START);
            } else {
                s.secretN = 0;
                s.secretFails++;
                mfc_Sound(s, S_OOPS);
            }
            return;
        }
        case H_POWER: go(s, MFC_M_BOOT); mfc_Sound(s, S_START); return;
        case H_DIGIT: parent_Digit(s, (char)('0' + i)); return;
        case H_PARENT_BTN:
            if (s.mode == MFC_M_SECRET){ s.secretN = 0; go(s, MFC_M_SIGNIN); return; }
            switch (i){
                case 0: go(s, s.parentFrom == MFC_M_PARENT || s.parentFrom == MFC_M_PARENT_MENU ? (uint8_t)MFC_M_SIGNIN : s.parentFrom); break;
                case 1: if (s.codeN) s.code[--s.codeN] = '\0'; break;
                case 10: s.exitApp = true; break;
                case 11:
                    s.set->locked = s.set->locked ? 0 : 1;
                    s.saveSettings = s.pushPhone = true;
                    if (s.set->locked){ s.mode = s.parentFrom == MFC_M_LOCK ? (uint8_t)MFC_M_SIGNIN : s.parentFrom; s.stopWhy = 0; mfc_Stop(s, MFC_STOP_LOCKED); }
                    else { s.signedIn = false; s.stopWhy = 0; go(s, MFC_M_SIGNIN); }
                    break;
                case 12: s.prog = MFC_PROGRAMS; s.signedIn = false; go(s, MFC_M_SIGNIN); break;
                case 13: go(s, s.parentFrom); break;
                default: break;
            }
            return;
        // The desktop ---------------------------------------------------------------------------------------------------
        case H_DESKTOP: s.selectedIcon = -1; return;
        case H_START: s.startOpen = !s.startOpen; s.focus = -1; mfc_Sound(s, S_CLICK); return;
        case H_START_ITEM:
            s.startOpen = false;
            if (i < MFC_PROGRAMS) open_Program(s, i);
            else if (i == 60) then_Do(s, THEN_SIGN_OUT, 0);
            else if (i == 61) then_Do(s, THEN_SHUT_DOWN, 0);
            return;
        case H_ICON:
            if (twice || knob || mfc_One_Click(mfc_Me(s), mfc_Age(s))){ s.selectedIcon = (int8_t)i; open_Program(s, i); }
            else {
                s.selectedIcon = (int8_t)i;
                mfc_Sound(s, S_CLICK);
                toast(s, "CLICK TWICE, QUICKLY, TO OPEN IT");
            }
            return;
        case H_TASK: s.minimized = !s.minimized; mfc_Sound(s, S_CLICK); return;
        case H_MIN: s.minimized = true; s.menu.open = false; mfc_Sound(s, S_CLOSE); toast(s, "IT'S STILL OPEN: CLICK IT ON THE TASKBAR"); return;
        case H_CLOSE: close_Program(s); return;
        case H_MENU: {
            int16_t x = 2;
            for (uint8_t k = 0; k < i; k++) x = (int16_t)(x + 30);
            menu_Bar(s, i, x);
            return;
        }
        case H_MENU_ITEM: if (i < s.menu.count) action(s, s.menu.action[i]); return;
        case H_CTX_ITEM: if (i < s.ctx.count) action(s, s.ctx.action[i]); return;
        case H_DLG_BTN: dialog_Button(s, i); return;
        case H_DLG_ITEM:
            if (s.dlg.chosen == (int8_t)i && (twice || knob)){ s.dlg.chosen = (int8_t)i; dialog_Button(s, 0); return; }
            s.dlg.chosen = (int8_t)i;
            if (twice || knob) dialog_Button(s, 0);
            return;
        case H_DLG_FIELD: return;
        default: break;
    }

    // The programs ------------------------------------------------------------------------------------------------------------
    switch (s.prog){
        case MFC_P_MOUSE: {
            Mfc_Mouse_School_t& m = s.ms_;
            if (kind == H_LESSON){
                if (lesson_Open(s, i)) lesson_Start(s, i);
                else { mfc_Sound(s, S_ERROR); toast(s, "THIS LESSON OPENS AT A HIGHER LEVEL"); }
                return;
            }
            if (kind == H_BTN && i == 0){ m.lesson = -1; s.focus = -1; mfc_Sound(s, S_CLOSE); return; }
            if (kind == H_BTN && i == 1){ lesson_Start(s, m.lesson); return; }
            if (lesson_Done(s)) return;
            if (kind == H_ITEM && i < m.count){
                Mfc_Thing_t& t = m.t[i];
                switch (m.lesson){
                    case L_POINT: if (!t.state){ t.state = 1; lesson_Progress(s); } return;
                    case L_CLICK: t.state = 1; t.ms = s.ms; lesson_Progress(s); return;
                    case L_DOUBLE:
                        if (t.state) return;
                        if (twice || knob){ t.state = 1; t.ms = s.ms; lesson_Progress(s); }
                        else { t.ms = s.ms; t.kind |= 0x80; hint(s, "NEARLY! CLICK TWICE, QUICKLY"); mfc_Sound(s, S_CLICK); }
                        return;
                    case L_DRAG:
                        if (knob && !t.state){ m.held = (int8_t)i; hint(s, "NOW TURN TO THE BARN AND CLICK"); mfc_Sound(s, S_CLICK); }
                        return;
                    case L_SCROLL: lesson_Progress(s); return;
                    case L_RIGHT:
                        if (knob){ context(s, i); ctx_Add(s, "PAINT", A_COLOR0); ctx_Add(s, "SMELL", A_COLOR1); ctx_Add(s, "WOBBLE", A_COLOR2); return; }
                        hint(s, "THAT WAS THE LEFT BUTTON. USE THE RIGHT ONE");
                        mfc_Sound(s, S_CLICK);
                        return;
                }
            }
            if (kind == H_TARGET && m.lesson == L_DRAG && m.held >= 0){       // the knob drops it in the barn
                m.t[m.held].x = (int16_t)(barn_X0() + 10); m.t[m.held].y = 60;
                drop_Animal(s);
                return;
            }
            if (m.lesson == L_POINT || m.lesson == L_CLICK) mfc_Sound(s, S_CLICK);
            return;
        }
        case MFC_P_KEYS:
            if (kind == H_LESSON && i >= 1 && i <= 4) keys_Start(s, i);
            else if (kind == H_BTN && i == 0){ s.keys.activity = K_MENU; s.focus = -1; mfc_Sound(s, S_CLOSE); }
            return;
        case MFC_P_PAINT:
            if (kind == H_TOOL && i < MFC_TOOLS){ s.paint.tool = i; mfc_Sound(s, S_CLICK); }
            else if (kind == H_COLOR && i < MFC_PAINT_COLORS){ s.paint.color = i; if (s.paint.tool == T_ERASER) s.paint.tool = T_BRUSH; mfc_Sound(s, S_CLICK); }
            else if (kind == H_CANVAS && knob){ s.paint.kx = (int16_t)(s.px - MFC_CANVAS_X); s.paint.ky = (int16_t)(s.py - MFC_CANVAS_Y); }
            return;
        case MFC_P_WRITE:
            if (kind == H_TEXT){
                // The cursor goes where the text was clicked.
                const int cols = MFC_WR_COLS(s.write.big), lineH = MFC_WR_LINE(s.write.big), cw = s.write.big ? 8 : 6;
                static uint16_t starts[MFC_LINES_MAX];
                const int lines = mfc_Text_Layout(s.text, cols, starts, MFC_LINES_MAX);
                int line = s.write.top + (s.py - MFC_WR_Y) / lineH, col = (s.px - MFC_WR_X + cw / 2) / cw;
                if (line >= lines){ s.text.cursor = s.text.len; return; }
                if (line < 0) line = 0;
                const int end = line + 1 < lines ? starts[line + 1] : s.text.len;
                int at = starts[line] + (col < 0 ? 0 : col);
                if (line + 1 < lines && at >= end) at = end - 1;
                if (at > s.text.len) at = s.text.len;
                s.text.cursor = (uint16_t)at;
            }
            return;
        case MFC_P_FILES: {
            Mfc_Files_View_t& f = s.files;
            if (kind == H_FOLDER && i < MFC_FOLDERS){ f.folder = i; f.stale = true; f.selected = -1; f.top = 0; mfc_Sound(s, S_CLICK); files_Refresh(s); return; }
            if (kind == H_FILE){
                const int k = i;
                if (k >= f.count) return;
                if ((twice || knob) && f.selected == k){ files_Open(s, k); return; }
                f.selected = (int8_t)k;
                if (knob) return;                              // a second knob click opens it
                if (twice) files_Open(s, k);
                else mfc_Sound(s, S_CLICK);
                return;
            }
            if (kind == H_BTN){
                if (f.folder == MFC_F_BIN){
                    if (i == 0){ if (f.selected < 0) toast(s, "CLICK A FILE FIRST"); else files_Restore(s, f.selected); }
                    else if (i == 1){ if (!f.count) toast(s, "THE RECYCLE BIN IS ALREADY EMPTY"); else { s.ctx.open = false; action(s, A_EMPTY_BIN); } }
                } else if (f.selected < 0) toast(s, "CLICK A FILE FIRST");
                else if (i == 0) files_Open(s, f.selected);
                else if (i == 1) files_Rename(s, f.selected);
                else if (i == 2) files_Delete(s, f.selected);
                return;
            }
            if (kind == H_ITEM){ f.selected = -1; return; }   // the empty part of the list
            return;
        }
        case MFC_P_CALC: {
            int n;
            const Mfc_Calc_Button_t* b = mfc_Calc_Buttons(n);
            if (kind == H_KEY && i < n){
                const uint8_t before = s.calc.answers;
                mfc_Calc_Key(s.calc, b[i].key);
                mfc_Sound(s, s.calc.error ? S_ERROR : S_CLICK);
                if (s.calc.answers != before && s.calc.answers >= 3) mfc_Badge(s, MFC_B_CALC);
            }
            return;
        }
        case MFC_P_PIANO: {
            Mfc_Piano_t& p = s.piano;
            if (kind == H_BTN && i <= MFC_SONGS){
                p.mode = i == 0 ? PIANO_FREE : PIANO_SONG;
                p.song = (uint8_t)(i ? i - 1 : 0);
                p.pos = 0;
                mfc_Sound(s, S_CLICK);
                return;
            }
            if (kind == H_KEY){
                const int key = i;
                mfc_Note(s, mfc_Piano_Hz(key), 350);
                p.lit = key;
                p.litMs = s.ms;
                if (p.mode == PIANO_SONG){
                    const Mfc_Song_t& song = mfc_Song(p.song);
                    if (key == song.keys[p.pos]){
                        p.wrong = false;
                        if (++p.pos >= song.count){ p.pos = 0; p.mode = PIANO_FREE; mfc_Badge(s, MFC_B_MUSIC); toast(s, "YOU PLAYED THE WHOLE SONG!"); }
                    } else p.wrong = true;
                }
            }
            return;
        }
        case MFC_P_WEB: {
            Mfc_Web_t& w = s.web;
            if (kind == H_POPUP_X){ w.popup = 2; mfc_Badge(s, MFC_B_POPUP); lesson(s, "WELL DONE!", "You closed the pop-up with its X. That is just the right thing to do.", true); return; }
            if (kind == H_POPUP_CLAIM){ lesson(s, "THAT WAS A TRICK!", "Real prizes don't pop up on websites. Close pop-ups like this with the X, and tell a grown-up.", false); return; }
            if (w.popup == 1) return;                       // the pop-up is in the way
            if (kind == H_BTN){
                if (i == 0) web_Back(s, -1);
                else if (i == 1) web_Back(s, 1);
                else if (i == 2) web_Link(s, mfc_Web_Find("home"));
                else if (i == 3) lesson(s, "GOOD THINKING", "If anything online worries you, stop and tell a grown-up. You did the right thing.", true);
                else if (i >= 10 && i < 13){
                    static const char* const SUGGEST[3] = {"lions", "planets", "dinosaurs"};
                    snprintf(w.query, sizeof(w.query), "%s", SUGGEST[i - 10]);
                    web_Search(s);
                }
                else if (i == 14) web_Search(s);
                else if (i == 20) lesson(s, "ASK FIRST", "Always ask a grown-up before you join a website or fill in a form.", false);
                else if (i == 21){ mfc_Badge(s, MFC_B_SAFE); lesson(s, "GREAT CHOICE!", "A grown-up can check whether a website is safe before you type anything.", true); }
                return;
            }
            if (kind == H_LINK){ if (i < w.layout.links) web_Link(s, w.layout.linkPage[i]); return; }
            if (kind == H_ITEM && i < w.resultCount){ web_Link(s, w.results[i]); w.searched = true; mfc_Badge(s, MFC_B_SEARCH); return; }
            if (kind == H_FIELD){
                if (i == 0){ w.searchOn = true; if (!mfc_Have_Keyboard()) toast(s, "PLUG IN A KEYBOARD, OR CLICK A WORD BELOW"); return; }
                w.clubField = i;
                lesson(s, "STOP AND THINK", i == 1 ? "Before you type even your name on a website, ask a grown-up if it's OK."
                                                   : "Never type your home address or a password on a website. Ask a grown-up first.", false);
                return;
            }
            return;
        }
        case MFC_P_SETTINGS: {
            Mfc_Child_t& me = mfc_Me(s);
            if (kind != H_SETTING) return;
            if (i < MFC_WALLPAPERS) me.wallpaper = i;
            else if (i >= 10 && i < 10 + MFC_THEMES) me.theme = (uint8_t)(i - 10);
            else if (i == 20) me.bigPointer = !me.bigPointer;
            else if (i == 21) me.slowDouble = !me.slowDouble;
            else if (i == 30 && me.volume > 0) me.volume--;
            else if (i == 31 && me.volume < 10) me.volume++;
            else if (i == 32){ mfc_Sound(s, S_YAY); return; }
            s.saveSettings = true;
            mfc_Sound(s, S_CLICK);
            return;
        }
        case MFC_P_LICENCE: if (kind == H_BADGE && i < MFC_BADGES){ s.licenceBadge = (int8_t)i; mfc_Sound(s, S_CLICK); } return;
        default: return;
    }
}

/** The right button: a menu where there is one. */
static void right_Click(Mfc_State_t& s, uint16_t id){
    const uint8_t kind = MFC_HIT_KIND(id), i = MFC_HIT_INDEX(id);
    s.startOpen = false;
    s.menu.open = false;
    if (s.dlg.kind != D_NONE) return;
    if (s.prog == MFC_P_MOUSE && !s.minimized && s.ms_.lesson == L_RIGHT && kind == H_ITEM && !lesson_Done(s)){
        context(s, i);
        ctx_Add(s, "PAINT", A_COLOR0); ctx_Add(s, "SMELL", A_COLOR1); ctx_Add(s, "WOBBLE", A_COLOR2);
        mfc_Sound(s, S_CLICK);
        return;
    }
    if (level(s) < 3){ s.ctx.open = false; return; }        // right click menus are for level 3
    if (kind == H_ICON){ context(s, i); ctx_Add(s, "OPEN", A_ICON_OPEN); ctx_Add(s, "WHAT IS IT?", A_ICON_ABOUT); return; }
    if (kind == H_FILE && s.prog == MFC_P_FILES && i < s.files.count){ files_Context(s, i); return; }
    s.ctx.open = false;
    s.redraw = true;
}

// Input ------------------------------------------------------------------------------------------------------------------------------
static void update_Hover(Mfc_State_t& s){
    const uint16_t id = id_At(s, s.px, s.py);
    const uint8_t kind = MFC_HIT_KIND(id);
    uint8_t ptr = MFC_PTR_ARROW;
    if (kind == H_LINK || (kind == H_ITEM && s.prog == MFC_P_WEB && s.mode == MFC_M_DESKTOP)) ptr = MFC_PTR_HAND;
    else if (kind == H_TEXT || kind == H_DLG_FIELD || kind == H_FIELD) ptr = MFC_PTR_TEXT;
    else if (kind == H_CANVAS) ptr = MFC_PTR_PEN;
    if (s.mode == MFC_M_BOOT || s.mode == MFC_M_SHUTDOWN) ptr = MFC_PTR_WAIT;
    if (id != s.hover || ptr != s.pointer){ s.hover = id; s.pointer = ptr; s.redraw = true; }
    // Mouse School: pointing at a star is enough.
    if (s.mode == MFC_M_DESKTOP && s.prog == MFC_P_MOUSE && !s.minimized && s.ms_.lesson == L_POINT && kind == H_ITEM && s.dlg.kind == D_NONE){
        Mfc_Thing_t& t = s.ms_.t[MFC_HIT_INDEX(id) % MFC_THINGS];
        if (!t.state){ t.state = 1; lesson_Progress(s); }
    }
}

void mfc_Mouse_Move(Mfc_State_t& s, int dx, int dy){
    s.accX += dx;
    s.accY += dy;
    const int mx = s.accX / COUNTS_PER_PIXEL, my = s.accY / COUNTS_PER_PIXEL;
    s.accX -= mx * COUNTS_PER_PIXEL;
    s.accY -= my * COUNTS_PER_PIXEL;
    if (!mx && !my) return;
    s.usingKnob = false;
    int x = s.px + mx, y = s.py + my;
    s.px = (int16_t)(x < 0 ? 0 : x >= MFC_W ? MFC_W - 1 : x);
    s.py = (int16_t)(y < 0 ? 0 : y >= MFC_H ? MFC_H - 1 : y);
    s.redraw = true;
    // A press that moves becomes a drag.
    if ((s.buttons & 1) && !s.dragging && (abs(s.px - s.pressX) > DRAG_START || abs(s.py - s.pressY) > DRAG_START)){
        const uint8_t kind = MFC_HIT_KIND(s.pressHit);
        if (s.prog == MFC_P_MOUSE && s.ms_.lesson == L_DRAG && kind == H_ITEM){
            const int i = MFC_HIT_INDEX(s.pressHit);
            if (i < s.ms_.count && !s.ms_.t[i].state){ s.ms_.held = (int8_t)i; s.ms_.heldDx = (int16_t)(s.ms_.t[i].x - s.pressX); s.ms_.heldDy = (int16_t)(s.ms_.t[i].y - s.pressY); s.dragging = true; }
        } else if (s.prog == MFC_P_FILES && kind == H_FILE && MFC_HIT_INDEX(s.pressHit) < s.files.count){
            s.files.dragging = (int8_t)MFC_HIT_INDEX(s.pressHit);
            s.files.selected = s.files.dragging;
            s.dragging = true;
        }
    }
    if (s.dragging && s.ms_.held >= 0 && s.prog == MFC_P_MOUSE){
        Mfc_Thing_t& t = s.ms_.t[s.ms_.held];
        t.x = (int16_t)(s.px + s.ms_.heldDx);
        t.y = (int16_t)(s.py + s.ms_.heldDy);
    }
    if (s.prog == MFC_P_PAINT && s.paint.drawing) paint_Move(s);
    update_Hover(s);
}

void mfc_Mouse_Buttons(Mfc_State_t& s, uint8_t buttons){
    const uint8_t pressed = (uint8_t)(buttons & ~s.buttons), released = (uint8_t)(s.buttons & ~buttons);
    s.buttons = buttons;
    s.usingKnob = false;
    if (pressed & 1){
        const uint16_t id = id_At(s, s.px, s.py);
        s.pressHit = id;
        s.pressX = s.px; s.pressY = s.py;
        s.pressMs = s.ms;
        s.dragging = false;
        if (s.mode == MFC_M_DESKTOP && s.dlg.kind == D_NONE && !s.menu.open && !s.ctx.open && MFC_HIT_KIND(id) == H_CANVAS && s.prog == MFC_P_PAINT) paint_Press(s);
    }
    if (released & 1){
        const uint16_t id = id_At(s, s.px, s.py);
        if (s.dragging){
            s.dragging = false;
            if (s.prog == MFC_P_MOUSE && s.ms_.held >= 0) drop_Animal(s);
            else if (s.prog == MFC_P_FILES && s.files.dragging >= 0){
                const int file = s.files.dragging;
                s.files.dragging = -1;
                if (MFC_HIT_KIND(id) == H_FOLDER){
                    const uint8_t to = MFC_HIT_INDEX(id);
                    if (to == MFC_F_BIN && s.files.folder != MFC_F_BIN) files_Delete(s, file);
                    else if (s.files.folder == MFC_F_BIN && to != MFC_F_BIN) files_Restore(s, file);
                }
            }
            s.redraw = true;
        } else if (s.prog == MFC_P_PAINT && s.paint.drawing){
            s.paint.drawing = false;
        } else if (id && id == s.pressHit){
            const bool twice = id == s.lastClickHit && s.ms - s.lastClickMs <= (s.mode == MFC_M_DESKTOP ? mfc_Double_Ms(mfc_Me(s)) : 500u);
            s.lastClickHit = twice ? 0 : id;
            s.lastClickMs = s.ms;
            click(s, id, twice, false);
        } else if (!id){
            s.startOpen = false; s.ctx.open = s.menu.open = false; s.redraw = true;
        }
    }
    if (pressed & 2) right_Click(s, id_At(s, s.px, s.py));
    if (pressed & 4) click(s, id_At(s, s.px, s.py), true, false);      // the middle button: a double click
    update_Hover(s);
}

static void scroll_By(Mfc_State_t& s, int steps){
    if (s.mode != MFC_M_DESKTOP || s.prog == MFC_PROGRAMS || s.minimized) return;
    switch (s.prog){
        case MFC_P_MOUSE:
            if (s.ms_.lesson == L_SCROLL){
                int v = s.ms_.scroll + steps * 12;
                const int max = MFC_SCROLL_H - (MFC_BAR_Y - MFC_TOP - 14);
                s.ms_.scroll = (int16_t)(v < 0 ? 0 : v > max ? max : v);
            }
            break;
        case MFC_P_WRITE: {
            static uint16_t starts[MFC_LINES_MAX];
            const int lines = mfc_Text_Layout(s.text, MFC_WR_COLS(s.write.big), starts, MFC_LINES_MAX);
            int v = s.write.top + steps;
            const int max = lines - MFC_WR_ROWS(s.write.big);
            s.write.top = (int16_t)(v > max ? (max > 0 ? max : 0) : v < 0 ? 0 : v);
            break;
        }
        case MFC_P_WEB: {
            int v = s.web.scroll + steps;
            const int max = s.web.layout.lines - MFC_WEB_ROWS;
            s.web.scroll = (int16_t)(v > max ? (max > 0 ? max : 0) : v < 0 ? 0 : v);
            break;
        }
        case MFC_P_FILES: {
            int v = s.files.top + steps;
            const int max = s.files.count - MFC_FL_ROWS;
            s.files.top = (int8_t)(v > max ? (max > 0 ? max : 0) : v < 0 ? 0 : v);
            break;
        }
        default: break;
    }
    s.redraw = true;
}

void mfc_Mouse_Wheel(Mfc_State_t& s, int steps){
    if (!steps) return;
    if (s.dlg.kind == D_OPEN){ int v = s.files.top - steps; s.files.top = (int8_t)(v < 0 ? 0 : v); s.redraw = true; return; }
    scroll_By(s, -steps);               // the wheel away from you goes up
}

// The keyboard -------------------------------------------------------------------------------------------------------------------
static void keys_Pick(Mfc_State_t& s){
    Mfc_Keys_t& k = s.keys;
    char next;
    if (k.activity == K_NUMBERS) next = (char)('0' + rnd_Range(k.rng, 0, 9));
    else { static const char L[] = "abcdefghijklmnopqrstuvwxyz"; next = L[rnd_Range(k.rng, 0, 25)]; }
    if (next == k.target) next = k.activity == K_NUMBERS ? (char)('0' + (next - '0' + 3) % 10) : (char)('a' + (next - 'a' + 7) % 26);
    k.target = next;
}

static void keys_Start(Mfc_State_t& s, uint8_t activity){
    Mfc_Keys_t& k = s.keys;
    if (!mfc_Have_Keyboard()){ info(s, "A KEYBOARD, PLEASE", "Plug a USB keyboard into Pixlpal to learn the keys."); return; }
    const uint32_t rng = k.rng;
    memset(&k, 0, sizeof(k));
    k.rng = rng ? rng : 1;
    k.activity = activity;
    k.goal = activity == K_FIND || activity == K_NUMBERS ? 10 : activity == K_SPECIAL ? 6 : 1;
    if (activity == K_FIND || activity == K_NUMBERS) keys_Pick(s);
    s.focus = -1;
    mfc_Sound(s, S_OPEN);
    if (activity == K_NAME) mfc_Speak(s, "Type your name. Use Shift for the capital letter.");
}

/** The name to type: as Kids Learning has it, with a capital first letter. */
static void name_Target(const Mfc_State_t& s, char* out, size_t size){
    snprintf(out, size, "%s", kk_Name(*s.k));
    for (size_t i = 0; out[i]; i++) out[i] = (char)(i == 0 ? toupper((unsigned char)out[i]) : tolower((unsigned char)out[i]));
}

static const uint8_t SPECIAL_KEYS[6] = {K_SPACE, K_ENTER, K_BACKSPACE, 0 /* shift + a letter */, K_RIGHT, K_ESC};

static void keys_Key(Mfc_State_t& s, uint8_t usage, uint8_t mods, char ascii){
    Mfc_Keys_t& k = s.keys;
    k.lit = usage;
    k.litMs = s.ms;
    if (usage == K_ESC && k.activity != K_SPECIAL){ k.activity = K_MENU; mfc_Sound(s, S_CLOSE); return; }
    switch (k.activity){
        case K_FIND: case K_NUMBERS: {
            if (!ascii) return;
            const char c = (char)tolower((unsigned char)ascii);
            if (c == k.target){
                k.wrong = false;
                if (++k.done >= k.goal){ mfc_Badge(s, MFC_B_KEYS); mfc_Sound(s, S_YAY); }
                else { mfc_Sound(s, S_POP); keys_Pick(s); }
            } else { k.wrong = true; mfc_Sound(s, S_OOPS); }
            return;
        }
        case K_NAME: {
            if (k.done) return;
            const size_t n = strlen(k.typed);
            if (usage == K_BACKSPACE){ if (n) k.typed[n - 1] = '\0'; k.wrong = false; return; }
            char want[20];
            name_Target(s, want, sizeof(want));
            if (usage == K_ENTER || usage == K_PAD_ENTER){
                if (!strcmp(k.typed, want)){ k.done = 1; mfc_Badge(s, MFC_B_NAME); mfc_Sound(s, S_YAY); }
                else { k.wrong = true; mfc_Sound(s, S_OOPS); }
                return;
            }
            if (ascii >= ' ' && ascii <= '~' && n + 1 < sizeof(k.typed)){ k.typed[n] = ascii; k.typed[n + 1] = '\0'; mfc_Sound(s, S_CLICK); }
            k.wrong = false;
            if (!strcmp(k.typed, want)){ k.done = 1; mfc_Badge(s, MFC_B_NAME); mfc_Sound(s, S_YAY); }
            return;
        }
        case K_SPECIAL: {
            if (k.step >= 6) return;
            bool right;
            if (k.step == 3) right = ascii >= 'A' && ascii <= 'Z' && (mods & MOD_SHIFT);
            else if (k.step == 4){
                if (usage == K_RIGHT){ if (++k.frog >= 7){ k.frog = 7; right = true; } else { mfc_Sound(s, S_POP); return; } }
                else if (usage == K_LEFT){ if (k.frog > 0) k.frog--; mfc_Sound(s, S_POP); return; }
                else right = false;
            } else right = usage == SPECIAL_KEYS[k.step];
            if (right){
                k.wrong = false;
                k.step++;
                k.done = k.step;
                if (k.step >= 6){ mfc_Badge(s, MFC_B_SPECIAL); mfc_Sound(s, S_YAY); }
                else mfc_Sound(s, S_POP);
            } else { k.wrong = true; mfc_Sound(s, S_OOPS); }
            return;
        }
        default: return;
    }
}

static void write_Key(Mfc_State_t& s, uint8_t usage, uint8_t mods, char ascii){
    Mfc_Text_t& x = s.text;
    const int cols = MFC_WR_COLS(s.write.big);
    if ((mods & MOD_CTRL) && (ascii == 's' || ascii == 'S')){ save(s, THEN_NOTHING, 0, false); return; }
    switch (usage){
        case K_ENTER: case K_PAD_ENTER: if (!mfc_Text_Insert(x, '\n')) toast(s, "THIS PAGE IS FULL"); break;
        case K_BACKSPACE: mfc_Text_Backspace(x); break;
        case K_DELETE: mfc_Text_Delete(x); break;
        case K_LEFT: mfc_Text_Move(x, -1, 0, cols); break;
        case K_RIGHT: mfc_Text_Move(x, 1, 0, cols); break;
        case K_UP: mfc_Text_Move(x, 0, -1, cols); break;
        case K_DOWN: mfc_Text_Move(x, 0, 1, cols); break;
        case K_TAB: for (int i = 0; i < 4; i++) mfc_Text_Insert(x, ' '); break;
        default:
            if (ascii >= ' ' && ascii <= '~' && !(mods & MOD_CTRL)){ if (!mfc_Text_Insert(x, ascii)) toast(s, "THIS PAGE IS FULL"); }
            else return;
            break;
    }
    // Keep the cursor in sight.
    static uint16_t starts[MFC_LINES_MAX];
    const int lines = mfc_Text_Layout(x, cols, starts, MFC_LINES_MAX);
    int line, col;
    mfc_Text_Where(x, starts, lines, line, col);
    const int rows = MFC_WR_ROWS(s.write.big);
    if (line < s.write.top) s.write.top = (int16_t)line;
    if (line >= s.write.top + rows) s.write.top = (int16_t)(line - rows + 1);
}

static void parent_Digit(Mfc_State_t& s, char d){
    if (s.mode != MFC_M_PARENT) return;
    s.modeMs = s.ms;
    if (s.ms < s.codeWaitUntil){ mfc_Sound(s, S_ERROR); return; }
    if (s.codeN < MFC_CODE_LEN){ s.code[s.codeN++] = d; s.code[s.codeN] = '\0'; mfc_Sound(s, S_CLICK); }
    if (s.codeN < MFC_CODE_LEN) return;
    if (mfc_Code_Right(*s.set, s.code)){
        s.codeFails = 0;
        memset(s.code, 0, sizeof(s.code));
        s.codeN = 0;
        const uint8_t from = s.parentFrom;
        go(s, MFC_M_PARENT_MENU);
        s.parentFrom = from;
        mfc_Sound(s, S_YAY);
    } else {
        memset(s.code, 0, sizeof(s.code));
        s.codeN = 0;
        if (++s.codeFails >= 3){ s.codeFails = 0; s.codeWaitUntil = s.ms + CODE_WAIT_MS; }
        mfc_Sound(s, S_OOPS);
    }
    s.redraw = true;
}

void mfc_Key(Mfc_State_t& s, uint8_t usage, uint8_t mods, char ascii){
    s.redraw = true;
    if (s.mode == MFC_M_PARENT){
        if (ascii >= '0' && ascii <= '9') parent_Digit(s, ascii);
        else if (usage == K_BACKSPACE && s.codeN) s.code[--s.codeN] = '\0';
        else if (usage == K_ESC) go(s, s.parentFrom);
        return;
    }
    if (s.mode == MFC_M_PARENT_MENU){ if (usage == K_ESC) go(s, s.parentFrom); return; }
    if (s.mode == MFC_M_OFF){ if (usage == K_ENTER || usage == K_SPACE){ go(s, MFC_M_BOOT); mfc_Sound(s, S_START); } return; }
    if (s.mode == MFC_M_SIGNIN){
        if (usage == K_RIGHT || usage == K_DOWN){ s.pick = (uint8_t)((s.pick + 1) % s.k->count); }
        else if (usage == K_LEFT || usage == K_UP){ s.pick = (uint8_t)((s.pick + s.k->count - 1) % s.k->count); }
        else if (usage == K_ENTER) mfc_Sign_In(s, s.pick);
        return;
    }
    if (s.mode == MFC_M_SECRET){ if (usage == K_ESC){ s.secretN = 0; go(s, MFC_M_SIGNIN); } return; }
    if (s.mode != MFC_M_DESKTOP) return;

    // A dialog takes the keys.
    Mfc_Dialog_t& d = s.dlg;
    if (d.kind != D_NONE){
        if (d.fieldOn){
            const size_t n = strlen(d.field);
            if (usage == K_BACKSPACE){ if (n) d.field[n - 1] = '\0'; return; }
            if (usage == K_ENTER || usage == K_PAD_ENTER){ dialog_Button(s, 0); return; }
            if (usage == K_ESC){ dialog_Button(s, d.buttons - 1); return; }
            if (ascii && (isalnum((unsigned char)ascii) || ascii == ' ' || ascii == '-') && n < MFC_NAME_LEN){ d.field[n] = (char)toupper((unsigned char)ascii); d.field[n + 1] = '\0'; }
            return;
        }
        if (usage == K_ENTER || usage == K_PAD_ENTER) dialog_Button(s, 0);
        else if (usage == K_ESC) dialog_Button(s, d.buttons - 1);
        else if (d.kind == D_OPEN && (usage == K_DOWN || usage == K_UP) && s.files.count){
            int c = d.chosen + (usage == K_DOWN ? 1 : -1);
            d.chosen = (int8_t)(c < 0 ? 0 : c >= s.files.count ? s.files.count - 1 : c);
        }
        return;
    }
    if (usage == K_ESC && (s.startOpen || s.menu.open || s.ctx.open)){ s.startOpen = false; s.menu.open = s.ctx.open = false; return; }
    if ((mods & MOD_GUI) && !ascii){ s.startOpen = !s.startOpen; s.focus = -1; mfc_Sound(s, S_CLICK); return; }   // the Windows key
    if (s.prog == MFC_PROGRAMS || s.minimized){
        // The desktop: arrows choose an icon, Enter opens it.
        if (usage == K_RIGHT || usage == K_LEFT || usage == K_DOWN || usage == K_UP){
            int n = 0;
            int8_t open[MFC_PROGRAMS];
            for (uint8_t p = 0; p < MFC_PROGRAMS; p++) if (mfc_Program_Open(*s.set, mfc_Me(s), mfc_Age(s), p)) open[n++] = (int8_t)p;
            if (!n) return;
            int at = 0;
            for (int i = 0; i < n; i++) if (open[i] == s.selectedIcon) at = i;
            const int step = usage == K_RIGHT ? 1 : usage == K_LEFT ? -1 : usage == K_DOWN ? 5 : -5;
            at = ((at + step) % n + n) % n;
            s.selectedIcon = open[at];
        } else if ((usage == K_ENTER || usage == K_PAD_ENTER) && s.selectedIcon >= 0) open_Program(s, (uint8_t)s.selectedIcon);
        return;
    }
    switch (s.prog){
        case MFC_P_KEYS: if (s.keys.activity != K_MENU) keys_Key(s, usage, mods, ascii); break;
        case MFC_P_WRITE: write_Key(s, usage, mods, ascii); break;
        case MFC_P_CALC: {
            char key = 0;
            if (ascii && strchr("0123456789.+-*/=", ascii)) key = ascii;
            else if (ascii == 'x' || ascii == 'X') key = '*';
            else if (usage == K_ENTER || usage == K_PAD_ENTER) key = '=';
            else if (usage == K_BACKSPACE) key = '<';
            else if (usage == K_ESC || ascii == 'c' || ascii == 'C') key = 'C';
            if (key){
                const uint8_t before = s.calc.answers;
                mfc_Calc_Key(s.calc, key);
                mfc_Sound(s, s.calc.error ? S_ERROR : S_CLICK);
                if (s.calc.answers != before && s.calc.answers >= 3) mfc_Badge(s, MFC_B_CALC);
            }
            break;
        }
        case MFC_P_PIANO: {
            const int key = ascii ? mfc_Piano_Key_For(ascii) : -1;
            if (key >= 0){
                // As a click on that key.
                click(s, MFC_HIT(H_KEY, key), false, false);
            }
            break;
        }
        case MFC_P_WEB: {
            Mfc_Web_t& w = s.web;
            if (w.searchOn || mfc_Web_Page(w.page).kind == MFC_PAGE_HOME){
                const size_t n = strlen(w.query);
                if (usage == K_BACKSPACE){ if (n) w.query[n - 1] = '\0'; w.searchOn = true; }
                else if (usage == K_ENTER || usage == K_PAD_ENTER){ if (n) web_Search(s); }
                else if (ascii && (isalnum((unsigned char)ascii) || ascii == ' ') && n + 1 < sizeof(w.query)){ w.query[n] = (char)tolower((unsigned char)ascii); w.query[n + 1] = '\0'; w.searchOn = true; }
            }
            if (usage == K_DOWN) scroll_By(s, 1);
            else if (usage == K_UP) scroll_By(s, -1);
            else if (usage == K_BACKSPACE && !w.searchOn && mfc_Web_Page(w.page).kind != MFC_PAGE_HOME) web_Back(s, -1);
            break;
        }
        case MFC_P_MOUSE:
            if (s.ms_.lesson == L_SCROLL){ if (usage == K_DOWN) scroll_By(s, 1); else if (usage == K_UP) scroll_By(s, -1); }
            else if (usage == K_ESC && s.ms_.lesson >= 0) s.ms_.lesson = -1;
            break;
        case MFC_P_PAINT: {
            // The arrows move the pointer; Space puts the pen down or lifts it.
            const int step = (mods & MOD_SHIFT) ? 8 : 2;
            int dx = usage == K_RIGHT ? step : usage == K_LEFT ? -step : 0, dy = usage == K_DOWN ? step : usage == K_UP ? -step : 0;
            if ((mods & MOD_CTRL) && (ascii == 's' || ascii == 'S')){ save(s, THEN_NOTHING, 0, false); break; }
            if (dx || dy){
                int x = s.px + dx, y = s.py + dy;
                s.px = (int16_t)(x < 0 ? 0 : x >= MFC_W ? MFC_W - 1 : x);
                s.py = (int16_t)(y < 0 ? 0 : y >= MFC_H ? MFC_H - 1 : y);
                if (s.paint.drawing) paint_Move(s);
                update_Hover(s);
            } else if (usage == K_SPACE){
                if (s.paint.drawing) s.paint.drawing = false;
                else if (on_Canvas(s.px, s.py)) paint_Press(s);
            } else if (ascii >= '1' && ascii <= '9') s.paint.color = (uint8_t)(ascii - '0');
            break;
        }
        default: break;
    }
}

// The knob ------------------------------------------------------------------------------------------------------------------------
static bool focusable(const Mfc_State_t& s, const Mfc_Hit_t& h){
    const uint8_t kind = MFC_HIT_KIND(h.id);
    if (kind == H_DESKTOP || kind == H_TEXT || kind == H_DLG_FIELD) return false;
    if (kind == H_CANVAS) return false;
    (void)s;
    return true;
}

void mfc_Knob_Turn(Mfc_State_t& s, int dir){
    if (!dir) return;
    s.redraw = true;
    if (s.mode == MFC_M_PARENT){ s.digit = (uint8_t)((s.digit + 10 + dir) % 10); s.modeMs = s.ms; return; }
    if (s.mode == MFC_M_DESKTOP && s.dlg.kind == D_NONE && s.prog == MFC_P_MOUSE && !s.minimized && s.ms_.lesson == L_SCROLL && !lesson_Done(s)
        && s.ms_.scroll < MFC_SCROLL_H - (MFC_BAR_Y - MFC_TOP - 14)){ scroll_By(s, dir * 2); return; }
    if (s.hitCount <= 0) return;
    // The next thing that can be clicked.
    int at = s.focus;
    for (int tries = 0; tries < s.hitCount; tries++){
        at = (at + dir + s.hitCount) % s.hitCount;
        if (at < 0) at += s.hitCount;
        if (focusable(s, s.hits[at])) break;
    }
    s.focus = at;
    const Mfc_Hit_t& h = s.hits[at];
    s.px = (int16_t)((h.x0 + h.x1) / 2);
    s.py = (int16_t)((h.y0 + h.y1) / 2);
    s.usingKnob = true;
    update_Hover(s);
    if (s.ms_.held >= 0 && s.prog == MFC_P_MOUSE){ s.ms_.t[s.ms_.held].x = s.px; s.ms_.t[s.ms_.held].y = s.py; }
}

void mfc_Knob_Click(Mfc_State_t& s, int count){
    s.redraw = true;
    s.usingKnob = true;
    if (s.mode == MFC_M_PARENT){ if (count == 1) parent_Digit(s, (char)('0' + s.digit)); else if (s.codeN) s.code[--s.codeN] = '\0'; return; }
    if (count >= 2){
        // Back: whatever is open on top closes.
        if (s.dlg.kind != D_NONE){ dialog_Button(s, s.dlg.buttons - 1); return; }
        if (s.startOpen || s.ctx.open || s.menu.open){ s.startOpen = false; s.ctx.open = s.menu.open = false; return; }
        if (s.mode == MFC_M_SECRET){ s.secretN = 0; go(s, MFC_M_SIGNIN); return; }
        if (s.mode == MFC_M_DESKTOP && s.prog == MFC_P_MOUSE && s.ms_.lesson >= 0){ s.ms_.lesson = -1; s.focus = -1; return; }
        if (s.mode == MFC_M_DESKTOP && s.prog == MFC_P_KEYS && s.keys.activity != K_MENU){ s.keys.activity = K_MENU; s.focus = -1; return; }
        if (s.mode == MFC_M_DESKTOP && s.prog != MFC_PROGRAMS){ close_Program(s); return; }
        return;
    }
    if (s.mode == MFC_M_OFF){ go(s, MFC_M_BOOT); mfc_Sound(s, S_START); return; }
    if (s.focus < 0 || s.focus >= s.hitCount){
        // Nothing chosen yet: the first thing.
        mfc_Knob_Turn(s, 1);
        return;
    }
    const uint16_t id = s.hits[s.focus].id;
    click(s, id, false, true);
    update_Hover(s);
}

void mfc_Knob_Hold(Mfc_State_t& s){
    if (s.mode == MFC_M_PARENT || s.mode == MFC_M_PARENT_MENU){ go(s, s.parentFrom); return; }
    const uint8_t from = s.mode == MFC_M_BOOT || s.mode == MFC_M_SHUTDOWN ? (uint8_t)MFC_M_SIGNIN : s.mode;
    memset(s.code, 0, sizeof(s.code));
    s.codeN = 0;
    s.digit = 0;
    go(s, MFC_M_PARENT);
    s.parentFrom = from;
    mfc_Sound(s, S_CLICK);
}

// Signing in, stopping --------------------------------------------------------------------------------------------------------------
void mfc_Sign_In(Mfc_State_t& s, uint8_t c){
    if (c >= s.k->count) return;
    if (s.set->locked){ mfc_Stop(s, 3); return; }
    kk_Select(*s.k, c);
    s.pick = c;
    s.child = (uint8_t)(s.k->real[s.k->child] % MFC_CHILDREN);
    Mfc_Child_t& me = mfc_Me(s);
    me.sessions++;
    s.saveSettings = s.pushPhone = true;
    s.prog = MFC_PROGRAMS;
    s.minimized = false;
    s.selectedIcon = -1;
    s.badge = -1;
    s.timeWarned = false;
    s.secretN = 0;
    s.secretFails = 0;
    s.web.popup = 0;
    s.fileCount = -1;
    s.signedIn = true;
    char t[48];
    snprintf(t, sizeof(t), "Hello %s!", kk_Name(*s.k));
    mfc_Speak(s, t);
    if (level(s) >= 3){
        s.secretMaking = !me.secret[0];
        go(s, MFC_M_SECRET);
    } else go(s, MFC_M_DESKTOP);
    mfc_Sound(s, S_START);
}

void mfc_Stop(Mfc_State_t& s, uint8_t why){
    if (!why){
        if (s.mode == MFC_M_LOCK && s.stopWhy != MFC_STOP_LEAVING){ s.stopWhy = 0; go(s, MFC_M_SIGNIN); }
        return;
    }
    if (s.stopWhy == MFC_STOP_LEAVING && s.mode == MFC_M_LOCK) return;     // on the way out: it stays "see you soon"
    if (s.mode == MFC_M_LOCK && s.stopWhy == why) return;
    // Work open is kept: saved under its name, or as AUTOSAVE.
    if (unsaved(s)){
        const char* name = s.prog == MFC_P_PAINT ? s.paint.name : s.write.name;
        if (!name[0]) default_Name(s, s.prog == MFC_P_PAINT ? MFC_F_PICTURES : MFC_F_WRITING, "AUTOSAVE", (char*)name, MFC_NAME_LEN + 1);
        if (!full(s) || exists(s, s.prog == MFC_P_PAINT ? MFC_F_PICTURES : MFC_F_WRITING, name, s.prog == MFC_P_PAINT ? 0 : 1)) write_Work(s, name);
    }
    s.dlg.kind = D_NONE;
    s.prog = MFC_PROGRAMS;
    s.signedIn = false;
    s.stopWhy = why;
    if (s.mode == MFC_M_PARENT || s.mode == MFC_M_PARENT_MENU){ s.parentFrom = MFC_M_LOCK; return; }
    go(s, MFC_M_LOCK);
    mfc_Sound(s, S_BYE);
}

void mfc_Time_Warning(Mfc_State_t& s){
    if (s.timeWarned || s.mode != MFC_M_DESKTOP) return;
    s.timeWarned = true;
    if (s.dlg.kind != D_NONE) return;
    s.dlg.then = THEN_NOTHING;
    dialog(s, D_TIME, "ONE MINUTE LEFT", "Computer time is nearly over for today. Save your work now!", 2, "OK", A_OK);
    mfc_Sound(s, S_ERROR);
    mfc_Speak(s, "One minute left. Save your work now!");
}

// The frame -------------------------------------------------------------------------------------------------------------------------
void mfc_Begin(Mfc_State_t& s){
    s.mode = MFC_M_BOOT;
    s.modeMs = s.ms;
    s.prog = MFC_PROGRAMS;
    s.px = MFC_W / 2;
    s.py = MFC_H / 2;
    s.selectedIcon = -1;
    s.badge = -1;
    s.focus = -1;
    s.licenceBadge = -1;
    s.fileCount = -1;
    s.files.selected = s.files.dragging = -1;
    s.ms_.lesson = -1;
    s.ms_.held = -1;
    s.web.page = -1;
    s.web.laidOut = -1;
    s.piano.lit = -1;
    s.pick = s.k ? s.k->child : 0;
    s.child = s.k ? (uint8_t)(s.k->real[s.k->child] % MFC_CHILDREN) : 0;
    if (s.set && s.set->locked){ s.stopWhy = 3; s.mode = MFC_M_LOCK; }
    mfc_Calc_Clear(s.calc);
    s.redraw = true;
}

void mfc_Tick(Mfc_State_t& s){
    static uint32_t lastMs = 0;
    const uint32_t dt = lastMs && s.ms > lastMs ? s.ms - lastMs : 0;
    lastMs = s.ms;
    switch (s.mode){
        case MFC_M_BOOT:
            if (s.ms - s.modeMs >= BOOT_MS){ s.pick = s.k->child; go(s, MFC_M_SIGNIN); }
            else s.redraw = true;
            break;
        case MFC_M_SHUTDOWN:
            if (s.ms - s.modeMs >= SHUTDOWN_MS) go(s, MFC_M_OFF);
            else s.redraw = true;
            break;
        case MFC_M_PARENT: case MFC_M_PARENT_MENU:
            if (s.ms - s.modeMs >= PARENT_IDLE_MS) go(s, s.parentFrom);
            if (s.codeWaitUntil && s.ms < s.codeWaitUntil + 1000) s.redraw = true;
            break;
        default: break;
    }
    if (s.fileCount < 0 && (s.signedIn || s.mode == MFC_M_DESKTOP)){ s.fileCount = (int8_t)mfc_Files_Count(s.child); s.redraw = true; }
    if (s.toast[0] && s.ms - s.toastMs >= TOAST_MS){ s.toast[0] = '\0'; s.redraw = true; }
    if (s.badge >= 0){ if (s.ms - s.badgeMs >= BADGE_MS){ s.badge = -1; } s.redraw = true; }
    if (s.mode != MFC_M_DESKTOP || s.prog == MFC_PROGRAMS || s.minimized) return;

    // The programs' animations.
    if (s.prog == MFC_P_MOUSE && s.ms_.lesson >= 0){
        Mfc_Mouse_School_t& m = s.ms_;
        if (m.lesson == L_CLICK && !lesson_Done(s) && dt){
            for (int i = 0; i < m.count; i++){
                Mfc_Thing_t& t = m.t[i];
                if (t.state){ if (s.ms - t.ms > 250) balloon(m, t); continue; }
                const int speed = 14 + m.done * 2;                       // pixels a second, faster as they go
                t.ms += dt * (uint32_t)speed;
                while (t.ms >= 1000){ t.ms -= 1000; t.y--; }
                if (t.y < MFC_TOP - 10) balloon(m, t);
            }
            s.redraw = true;
        }
        if (m.lesson == L_DOUBLE) for (int i = 0; i < m.count; i++) if ((m.t[i].kind & 0x80) && s.ms - m.t[i].ms > 400){ m.t[i].kind &= 0x7F; s.redraw = true; }
        if (m.hint[0] && lesson_Done(s)) s.redraw = true;
    }
    if (s.prog == MFC_P_PAINT && s.paint.drawing && s.paint.tool == T_SPRAY && dt){ spray(s, s.px - MFC_CANVAS_X, s.py - MFC_CANVAS_Y); s.redraw = true; }
    if (s.prog == MFC_P_PIANO && s.piano.lit >= 0 && s.ms - s.piano.litMs > 300){ s.piano.lit = -1; s.redraw = true; }
    if (s.prog == MFC_P_KEYS && s.keys.lit && s.ms - s.keys.litMs > 300){ s.keys.lit = 0; s.redraw = true; }
    if (s.prog == MFC_P_FILES && s.files.stale){ files_Refresh(s); s.redraw = true; }
    if (s.prog == MFC_P_WEB && s.web.page >= 0 && mfc_Web_Page(s.web.page).kind == MFC_PAGE_GAMES && s.web.popup == 0 && s.ms - s.modeMs > 0){
        static uint32_t since = 0;
        if (!since) since = s.ms;
        if (s.ms - since > 900){ s.web.popup = 1; since = 0; s.focus = -1; mfc_Sound(s, S_ERROR); s.redraw = true; }
    }
    if (s.prog == MFC_P_WEB && s.web.popup == 1) s.redraw = (s.ms / 400) % 2 != ((s.ms - dt) / 400) % 2 || s.redraw;     // it flashes
}
