/**
 * @file mfc_App.h
 * @brief My First Computer's running state, shared by the shell (mfc_Shell.cpp: what clicks and keys do), the
 *        screens (mfc_Ui.cpp) and the app task (myFirstComputer.cpp).
 *
 * Each frame, drawing lists what can be clicked (the hits). The mouse clicks what is under the pointer; the knob
 * steps through the same list (turn), and opens what it is on (click). So every screen works with either.
 * The platform calls at the end (files, sounds) are the device's (mfc_Files.cpp); a PC test gives its own.
 */
#ifndef MFC_APP_H
#define MFC_APP_H

#include "mfc_Model.h"
#include "kk_Kids.h"

// The screen ---------------------------------------------------------------------------------------------------------
#define MFC_W           256
#define MFC_H           128
#define MFC_BAR_Y       116             // the taskbar, to the bottom
#define MFC_TOP         10              // a window's content starts under its title bar
// Paint's canvas.
#define MFC_CANVAS_X    26
#define MFC_CANVAS_Y    21
#define MFC_CANVAS_W    224
#define MFC_CANVAS_H    84
// My Writing's page (small text: Terminal6x8; big: Terminal8x12).
#define MFC_WR_X        3
#define MFC_WR_Y        22
#define MFC_WR_COLS(big)    ((big) ? 30 : 41)
#define MFC_WR_LINE(big)    ((big) ? 13 : 9)
#define MFC_WR_ROWS(big)    ((big) ? 7 : 10)
// The Internet's page.
#define MFC_WEB_X       4
#define MFC_WEB_Y       34
#define MFC_WEB_COLS    40
#define MFC_WEB_LINE    9
#define MFC_WEB_ROWS    9
// My Files' list.
#define MFC_FL_Y        25
#define MFC_FL_ROW      10
#define MFC_FL_ROWS     9
// The piano's keys.
#define MFC_PIANO_X     2
#define MFC_PIANO_Y     44
#define MFC_PIANO_KEY_W 18
// Mouse School's long picture (the scroll lesson).
#define MFC_SCROLL_H    300

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define MFC_PSRAM EXT_RAM_BSS_ATTR
#else
#define MFC_PSRAM
#endif

/** The calculator's buttons: the key it types, where it is (row, column, columns wide) and its label. */
struct Mfc_Calc_Button_t { char key; uint8_t row, col, span; const char* label; };
const Mfc_Calc_Button_t* mfc_Calc_Buttons(int& count);
#define MFC_CALC_X      70
#define MFC_CALC_Y      34
#define MFC_CALC_BW     30
#define MFC_CALC_BH     16

enum Mfc_Mode_t : uint8_t {
    MFC_M_BOOT = 0,                     // starting up
    MFC_M_SIGNIN,                       // who is using the computer
    MFC_M_SECRET,                       // the secret pictures (making them, or signing in with them)
    MFC_M_DESKTOP,
    MFC_M_SHUTDOWN,                     // "shutting down"
    MFC_M_OFF,                          // a power button
    MFC_M_LOCK,                         // time is up, bedtime, or a grown-up locked it
    MFC_M_PARENT,                       // the parent code
    MFC_M_PARENT_MENU,                  // the code was right
};

// What can be clicked: kind << 8 | index.
enum Mfc_Hit_Kind_t : uint8_t {
    H_NONE = 0, H_ICON, H_START, H_START_ITEM, H_TASK, H_CLOSE, H_MIN, H_MENU, H_MENU_ITEM, H_DLG_BTN, H_DLG_FIELD,
    H_DLG_ITEM, H_CTX_ITEM, H_AVATAR, H_PICTURE, H_POWER, H_DIGIT, H_PARENT_BTN, H_BTN, H_TOOL, H_COLOR, H_CANVAS,
    H_FILE, H_FOLDER, H_KEY, H_LINK, H_LESSON, H_ITEM, H_SETTING, H_FIELD, H_TEXT, H_BADGE, H_TARGET, H_POPUP_X,
    H_POPUP_CLAIM, H_DESKTOP,
};
#define MFC_HIT(kind, index)    ((uint16_t)(((kind) << 8) | ((index) & 0xFF)))
#define MFC_HIT_KIND(id)        ((uint8_t)((id) >> 8))
#define MFC_HIT_INDEX(id)       ((uint8_t)((id) & 0xFF))

struct Mfc_Hit_t { int16_t x0, y0, x1, y1; uint16_t id; };
#define MFC_HITS 112

enum Mfc_Pointer_t : uint8_t { MFC_PTR_ARROW = 0, MFC_PTR_HAND, MFC_PTR_TEXT, MFC_PTR_PEN, MFC_PTR_WAIT };

// Menus and dialogs -------------------------------------------------------------------------------------------------
#define MFC_MENU_ITEMS 5
struct Mfc_Menu_t {
    bool open;
    int16_t x, y;
    uint8_t count;
    uint8_t action[MFC_MENU_ITEMS];     /**< Mfc_Action_t */
    const char* label[MFC_MENU_ITEMS];
    uint8_t which;                      /**< menu bar: which menu (0 FILE, 1 TEXT, 2 TOOLS) */
    uint8_t target;                     /**< what was right-clicked */
};

enum Mfc_Action_t : uint8_t {
    A_NONE = 0, A_OK, A_CANCEL, A_NEW, A_OPEN, A_SAVE, A_SAVE_AS, A_RENAME, A_DELETE, A_RESTORE, A_EMPTY_BIN,
    A_DONT_SAVE, A_BIG_TEXT, A_SMALL_TEXT, A_TEXT_COLOR, A_CLEAR, A_OPEN_FILE, A_PROPERTIES,
    A_SHOW_DESKTOP, A_ICON_OPEN, A_ICON_ABOUT, A_COLOR0, A_COLOR1, A_COLOR2,
};

enum Mfc_Dialog_Kind_t : uint8_t {
    D_NONE = 0, D_INFO, D_SAVE, D_RENAME, D_EMPTY_BIN, D_UNSAVED, D_OPEN, D_LESSON, D_FULL, D_TIME, D_ABOUT,
};

struct Mfc_Dialog_t {
    uint8_t kind;                       /**< Mfc_Dialog_Kind_t */
    char title[24];
    char text[140];
    uint8_t buttons;
    const char* button[3];
    uint8_t action[3];
    uint8_t icon;                       /**< 0 info, 1 question, 2 warning, 3 well done */
    char field[MFC_NAME_LEN + 6];       /**< a name being typed */
    uint8_t fieldOn;
    uint8_t then;                       /**< D_UNSAVED: what to do after (Mfc_Then_t) */
    uint8_t thenArg;
    // D_OPEN: the files to choose from.
    uint8_t folder;
    int8_t chosen;
};

enum Mfc_Then_t : uint8_t { THEN_NOTHING = 0, THEN_CLOSE, THEN_OPEN_PROGRAM, THEN_NEW, THEN_OPEN_DIALOG, THEN_SIGN_OUT, THEN_SHUT_DOWN };

struct Mfc_File_t {
    char name[MFC_NAME_LEN + 1];
    uint8_t kind;                       /**< 0 a picture (.png), 1 writing (.txt) */
    uint32_t size, time;
};
#define MFC_LIST 16

// The programs' own state -------------------------------------------------------------------------------------------
enum Mfc_Lesson_t : uint8_t { L_POINT = 0, L_CLICK, L_DOUBLE, L_DRAG, L_SCROLL, L_RIGHT, MFC_LESSONS };
#define MFC_THINGS 8
struct Mfc_Thing_t { int16_t x, y; uint8_t state, kind; uint32_t ms; };   // a star, balloon, present, animal, pot
struct Mfc_Mouse_School_t {
    int8_t lesson;                      /**< -1: the choice of lessons */
    Mfc_Thing_t t[MFC_THINGS];
    uint8_t count, done, goal;
    int16_t scroll;                     /**< L_SCROLL: how far down, pixels */
    int8_t held;                        /**< L_DRAG: the animal held (-1 none) */
    int16_t heldDx, heldDy;
    uint8_t want;                       /**< L_RIGHT: the colour asked for */
    uint8_t round;
    char hint[64];
    uint32_t hintMs, rng;
};

enum Mfc_Keys_Activity_t : uint8_t { K_MENU = 0, K_FIND, K_NAME, K_SPECIAL, K_NUMBERS };
struct Mfc_Keys_t {
    uint8_t activity;
    char target;                        /**< the key to find (lower case) */
    uint8_t done, goal;
    char typed[20];
    uint8_t step;                       /**< K_SPECIAL: which special key */
    int8_t frog;                        /**< the arrows' frog, 0-7 */
    uint8_t lit;                        /**< the key pressed last, lit for a moment */
    uint32_t litMs, rng;
    bool wrong;
};

enum Mfc_Tool_t : uint8_t { T_PENCIL = 0, T_BRUSH, T_BIG, T_SPRAY, T_FILL, T_ERASER, MFC_TOOLS };
struct Mfc_Paint_t {
    uint8_t tool, color;
    bool drawing;
    int16_t lastX, lastY;               /**< on the canvas */
    char name[MFC_NAME_LEN + 1];        /**< "" until saved */
    bool changed;
    int16_t kx, ky;                     /**< keyboard pen */
    uint32_t rng;
};

struct Mfc_Write_t {
    char name[MFC_NAME_LEN + 1];
    int16_t top;                        /**< first line shown */
    uint8_t big, color;
};

struct Mfc_Files_View_t {
    uint8_t folder;
    Mfc_File_t list[MFC_LIST];
    int8_t count, selected, top;
    int8_t dragging;                    /**< a file being dragged (-1 none) */
    bool deletedOne, restoredOne;
    bool stale;                         /**< read the list again */
};

enum Mfc_Piano_Mode_t : uint8_t { PIANO_FREE = 0, PIANO_SONG };
struct Mfc_Piano_t { uint8_t mode, song; int pos; int lit; uint32_t litMs; bool wrong; };

#define MFC_HISTORY 16
struct Mfc_Web_t {
    int8_t page;
    int8_t history[MFC_HISTORY];
    uint8_t at, count;                  /**< history: where we are, how many */
    int16_t scroll;                     /**< lines */
    char query[20];
    bool searchOn;                      /**< typing in the search box */
    int8_t results[6];
    uint8_t resultCount;
    uint8_t links;                      /**< links followed */
    bool usedBack, searched;
    uint8_t popup;                      /**< games page: 0 none yet, 1 showing, 2 closed */
    uint8_t clubField;                  /**< 0 none, 1 name, 2 address, 3 password */
    char clubName[14];
    Mfc_Layout_t layout;
    int8_t laidOut;                     /**< the page laid out (-1 none) */
};

// The state -----------------------------------------------------------------------------------------------------------
struct Mfc_State_t {
    Mfc_Settings_t* set;
    Kk_Session_t* k;
    uint16_t* canvas;                   /**< Paint: MFC_CANVAS_W x MFC_CANVAS_H (PSRAM) */
    uint32_t ms;                        /**< now */
    uint8_t mode;
    uint32_t modeMs;
    uint8_t child;                      /**< the child signed in: their place in Kids Learning (0-2) */
    uint8_t pick;                       /**< sign in: the choice */

    // The pointer.
    int16_t px, py;
    int32_t accX, accY;                 /**< mouse counts not yet a pixel */
    uint8_t buttons;
    bool usingKnob;                     /**< the knob moved last (the pointer jumps from thing to thing) */
    uint16_t pressHit;
    int16_t pressX, pressY;
    uint32_t pressMs;
    bool dragging;
    uint16_t lastClickHit;
    uint32_t lastClickMs;
    int focus;                          /**< the knob: the hit it is on */
    uint16_t hover;
    uint8_t pointer;                    /**< Mfc_Pointer_t */
    Mfc_Hit_t hits[MFC_HITS];
    int hitCount;

    // The desktop.
    uint8_t prog;                       /**< the open program (MFC_PROGRAMS: none) */
    bool minimized;
    bool startOpen;
    int8_t selectedIcon;
    Mfc_Menu_t ctx;                     /**< a right click menu */
    Mfc_Menu_t menu;                    /**< a menu bar's menu */
    Mfc_Dialog_t dlg;
    char toast[44];
    uint32_t toastMs;
    int8_t badge;                       /**< a new badge, shown big (-1 none) */
    uint32_t badgeMs;

    // The programs.
    Mfc_Mouse_School_t ms_;
    Mfc_Keys_t keys;
    Mfc_Paint_t paint;
    Mfc_Text_t text;
    Mfc_Write_t write;
    Mfc_Files_View_t files;
    Mfc_Calc_t calc;
    Mfc_Piano_t piano;
    Mfc_Web_t web;
    int8_t licenceBadge;                /**< the badge whose "how" is shown */
    int8_t fileCount;                   /**< the child's files, every folder (read again when a file changes) */

    // The secret pictures.
    uint8_t secretTry[MFC_SECRET_LEN];
    uint8_t secretN;
    bool secretMaking;
    uint8_t secretFails;

    // The parent code.
    char code[MFC_CODE_LEN + 1];
    uint8_t codeN, codeFails, digit;
    uint32_t codeWaitUntil;
    uint8_t parentFrom;                 /**< the mode to go back to */

    // For the app task.
    bool saveSettings;                  /**< the settings changed */
    bool pushPhone;                     /**< the phone's page should hear */
    bool exitApp;                       /**< a grown-up chose to leave */
    bool signedIn;                      /**< time counts */
    bool timeWarned;
    uint8_t stopWhy;                    /**< MFC_M_LOCK: Kk_Stop_t, or 3 locked by a grown-up */
    bool redraw;
};

// The shell (mfc_Shell.cpp) -----------------------------------------------------------------------------------------
void mfc_Begin(Mfc_State_t& s);
void mfc_Mouse_Move(Mfc_State_t& s, int dx, int dy);
void mfc_Mouse_Buttons(Mfc_State_t& s, uint8_t buttons);
void mfc_Mouse_Wheel(Mfc_State_t& s, int steps);
/** @brief A key pressed: its HID usage, the modifiers, its character (0 if none). */
void mfc_Key(Mfc_State_t& s, uint8_t usage, uint8_t modifiers, char ascii);
void mfc_Knob_Turn(Mfc_State_t& s, int dir);
void mfc_Knob_Click(Mfc_State_t& s, int count);
/** @brief The knob held: the parent code. */
void mfc_Knob_Hold(Mfc_State_t& s);
/** @brief Once a frame: animations and timers. */
void mfc_Tick(Mfc_State_t& s);
/** @brief Play must stop (Kk_Stop_t, MFC_STOP_LOCKED or MFC_STOP_LEAVING), or may go on (0). Work open is saved first. */
#define MFC_STOP_LOCKED     3
#define MFC_STOP_LEAVING    4
void mfc_Stop(Mfc_State_t& s, uint8_t why);
/** @brief A minute left today: the warning. */
void mfc_Time_Warning(Mfc_State_t& s);
/** @brief The phone signed a child in. */
void mfc_Sign_In(Mfc_State_t& s, uint8_t sessionChild);
/** @brief The child's settings and badges. */
Mfc_Child_t& mfc_Me(Mfc_State_t& s);
uint8_t mfc_Age(const Mfc_State_t& s);
/** @brief Gives a badge, with a fanfare. */
void mfc_Badge(Mfc_State_t& s, uint8_t badge);

// The screens (mfc_Ui.cpp) ------------------------------------------------------------------------------------------------
/** @brief Draws the screen and lists its hits. */
void mfc_Draw(Mfc_State_t& s);
uint16_t mfc_Theme_Color(uint8_t theme);
uint16_t mfc_Paint_Color(uint8_t i);
#define MFC_PAINT_COLORS 16

// The platform (mfc_Files.cpp on Pixlpal) ------------------------------------------------------------------------------
enum Mfc_Sound_t : uint8_t { S_CLICK = 0, S_OPEN, S_CLOSE, S_POP, S_YAY, S_OOPS, S_FANFARE, S_ERROR, S_START, S_BYE, S_TRASH };
void mfc_Sound(const Mfc_State_t& s, uint8_t sound);
void mfc_Note(const Mfc_State_t& s, float hz, int ms);
void mfc_Speak(const Mfc_State_t& s, const char* text);
bool mfc_Have_Keyboard(void);
bool mfc_Have_Mouse(void);
/** @brief The time for the taskbar ("10:42", or "" before the clock is set). */
void mfc_Clock_Text(char* out, size_t size);
/** @brief A folder's files, newest first. @return how many. */
int mfc_Files_List(uint8_t child, uint8_t folder, Mfc_File_t* out, int max);
int mfc_Files_Count(uint8_t child);
uint32_t mfc_Files_Bytes(uint8_t child);
bool mfc_File_Save_Picture(uint8_t child, const char* name, const uint16_t* pixels, int w, int h);
bool mfc_File_Load_Picture(uint8_t child, uint8_t folder, const char* name, uint16_t* pixels, int w, int h);
bool mfc_File_Save_Text(uint8_t child, const char* name, const char* text);
bool mfc_File_Load_Text(uint8_t child, uint8_t folder, const char* name, char* out, size_t size);
/** @brief To the Recycle Bin, back from it (to its own folder), renamed, or gone for good. */
bool mfc_File_To_Bin(uint8_t child, uint8_t folder, const char* name, uint8_t kind);
bool mfc_File_Restore(uint8_t child, const char* name, uint8_t kind);
bool mfc_File_Rename(uint8_t child, uint8_t folder, const char* from, const char* to, uint8_t kind);
bool mfc_File_Erase(uint8_t child, uint8_t folder, const char* name, uint8_t kind);
void mfc_Files_Erase_All(uint8_t child);

#endif
