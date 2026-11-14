/**
 * @file mfc_Model.h
 * @brief My First Computer {19,8} (KIDS, ages 4-12): a pretend computer that teaches children to use a real one,
 *        safely. What the parent sets and what each child has earned, the levels by age, the badges of the Computer
 *        Licence, file names, the text editor, the calculator, the songs of the piano, the secret pictures and the
 *        pages of the pretend internet (built in: nothing goes online). No Arduino, no panel: tested on a PC.
 */
#ifndef MFC_MODEL_H
#define MFC_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define MFC_NVS_KEY         "mfcSet"
#define MFC_SETTINGS_MAGIC  0x4D464301u   // "MFC" 1
#define MFC_CHILDREN        3             // Kids Learning's children (their place in its list)
#define MFC_CODE_LEN        4
#define MFC_DEFAULT_CODE    "1234"
#define MFC_NAME_LEN        12            // a file's name, without the extension
#define MFC_TEXT_MAX        1200          // a piece of writing
#define MFC_FILES_MAX       16            // a child's files, every folder together
#define MFC_WALLPAPERS      6
#define MFC_THEMES          4
#define MFC_PICTURES        6             // the secret pictures to choose from
#define MFC_SECRET_LEN      2

/** The programs, in the order of the desktop's icons. */
enum Mfc_Program_t : uint8_t {
    MFC_P_MOUSE = 0, MFC_P_KEYS, MFC_P_PAINT, MFC_P_WRITE, MFC_P_FILES, MFC_P_CALC, MFC_P_PIANO, MFC_P_WEB,
    MFC_P_SETTINGS, MFC_P_LICENCE, MFC_P_BIN, MFC_PROGRAMS
};

/** The Computer Licence: one badge for each skill. */
enum Mfc_Badge_t : uint8_t {
    // Level 1 (from 4)
    MFC_B_POINT = 0, MFC_B_CLICK, MFC_B_DOUBLE, MFC_B_DRAG, MFC_B_ART, MFC_B_MUSIC, MFC_B_SHUTDOWN,
    // Level 2 (from 6)
    MFC_B_SCROLL, MFC_B_KEYS, MFC_B_NAME, MFC_B_SPECIAL, MFC_B_WRITER, MFC_B_RENAME, MFC_B_TIDY, MFC_B_CALC,
    // Level 3 (from 9)
    MFC_B_RIGHT, MFC_B_WEB, MFC_B_SEARCH, MFC_B_POPUP, MFC_B_SAFE,
    MFC_BADGES
};

/** Where a child's files are. */
enum Mfc_Folder_t : uint8_t { MFC_F_PICTURES = 0, MFC_F_WRITING, MFC_F_BIN, MFC_FOLDERS };

struct Mfc_Child_t {
    uint8_t levelMode;                  /**< 0 by age, moving up as badges are earned; 1-3 the parent's choice */
    uint8_t earnedLevel;                /**< by age: the level reached (1-3) */
    uint8_t oneClick;                   /**< 0 by level (one click at level 1), 1 one click, 2 two clicks */
    uint32_t badges;                    /**< bit per Mfc_Badge_t */
    uint8_t secret[MFC_SECRET_LEN];     /**< the secret pictures (index + 1; 0: none made yet) */
    uint8_t wallpaper, theme, bigPointer, slowDouble, volume;   /**< the child's own Settings */
    uint16_t sessions;                  /**< times signed in */
    uint32_t seconds;                   /**< on the computer, all time */
    uint8_t lastProgram;                /**< the last opened (MFC_PROGRAMS: none) */
    uint8_t reserved[7];
};

struct Mfc_Settings_t {
    uint32_t magic;
    Mfc_Child_t c[MFC_CHILDREN];
    uint16_t programs;                  /**< bit per Mfc_Program_t: the parent allows it */
    char code[MFC_CODE_LEN + 1];        /**< the parent code (digits) */
    uint8_t codeSet;                    /**< the parent chose one (else MFC_DEFAULT_CODE) */
    uint8_t locked;                     /**< a grown-up locked the computer */
    uint8_t editing;                    /**< the child the phone page shows */
    uint8_t reserved[8];
};

void mfc_Settings_Default(Mfc_Settings_t& s);
void mfc_Child_Default(Mfc_Child_t& c);
void mfc_Settings_Clamp(Mfc_Settings_t& s);
/** @brief Badges, levels and the child's own settings back to the start (the secret pictures too). */
void mfc_Child_Reset(Mfc_Child_t& c);

// Levels and programs ---------------------------------------------------------------------------------------------
/** @brief 1 (4-5), 2 (6-8) or 3 (9-12). */
uint8_t mfc_Level_For_Age(uint8_t age);
/** @brief The child's level now: the parent's, or by age and badges. */
uint8_t mfc_Level(const Mfc_Child_t& c, uint8_t age);
/** @brief The level that opens a program, and a badge's level. */
uint8_t mfc_Program_Level(uint8_t program);
uint8_t mfc_Badge_Level(uint8_t badge);
/** @brief The program is on the desktop: the parent allows it and the level opens it. */
bool mfc_Program_Open(const Mfc_Settings_t& s, const Mfc_Child_t& c, uint8_t age, uint8_t program);
const char* mfc_Program_Name(uint8_t program);
/** @brief One click opens an icon (else two, as on a real computer). */
bool mfc_One_Click(const Mfc_Child_t& c, uint8_t age);
/** @brief The milliseconds two clicks may be apart to count as a double click. */
uint16_t mfc_Double_Ms(const Mfc_Child_t& c);

// Badges -----------------------------------------------------------------------------------------------------------
const char* mfc_Badge_Name(uint8_t badge);
const char* mfc_Badge_How(uint8_t badge);
bool mfc_Has(const Mfc_Child_t& c, uint8_t badge);
/** @brief Gives a badge. True if it is new. By age, the level moves up when every badge of it is earned. */
bool mfc_Award(Mfc_Child_t& c, uint8_t badge, uint8_t age);
int mfc_Badge_Count(const Mfc_Child_t& c);
/** @brief Badges of @p level earned, and how many it has. */
int mfc_Level_Badges(const Mfc_Child_t& c, uint8_t level, int& total);

// The parent code ----------------------------------------------------------------------------------------------------
/** @brief Four digits, else false. */
bool mfc_Code_Valid(const char* code);
bool mfc_Code_Right(const Mfc_Settings_t& s, const char* entered);

// File names -----------------------------------------------------------------------------------------------------------
/** @brief A name a child typed made safe for a file: A-Z, 0-9, '-' and '_' (spaces become '_'), at most
 *         MFC_NAME_LEN. Empty if nothing was left. */
void mfc_Name_Clean(const char* typed, char* out, size_t size);
/** @brief A file name as shown: '_' as a space. */
void mfc_Name_Show(const char* name, char* out, size_t size);
/** @brief "PICTURE 3": the first of "<base>_<n>" not in @p taken (names without extension, @p count of them). */
void mfc_Name_Next(const char* base, const char (*taken)[MFC_NAME_LEN + 1], int count, char* out, size_t size);

// The text editor ---------------------------------------------------------------------------------------------------------
#define MFC_LINES_MAX 160
struct Mfc_Text_t {
    char t[MFC_TEXT_MAX + 1];
    uint16_t len, cursor;
    uint8_t changed;                    /**< not saved since the last change */
};
void mfc_Text_Clear(Mfc_Text_t& x);
void mfc_Text_Set(Mfc_Text_t& x, const char* text);
/** @brief A character typed at the cursor ('\n' a new line). False when full. */
bool mfc_Text_Insert(Mfc_Text_t& x, char c);
void mfc_Text_Backspace(Mfc_Text_t& x);
void mfc_Text_Delete(Mfc_Text_t& x);
/** @brief Where each line starts when the text wraps at @p cols characters (words kept whole). @return the lines. */
int mfc_Text_Layout(const Mfc_Text_t& x, int cols, uint16_t* starts, int max);
/** @brief The line and column of the cursor in a layout. */
void mfc_Text_Where(const Mfc_Text_t& x, const uint16_t* starts, int lines, int& line, int& col);
/** @brief Moves the cursor: @p dx characters, or @p dy lines (keeping the column). */
void mfc_Text_Move(Mfc_Text_t& x, int dx, int dy, int cols);
/** @brief Words in the text. */
int mfc_Text_Words(const Mfc_Text_t& x);

// The calculator -------------------------------------------------------------------------------------------------------------
struct Mfc_Calc_t {
    double acc;                         /**< what came before the operation */
    char op;                            /**< '+', '-', '*', '/' or 0 */
    char entry[14];                     /**< what is being typed, or the answer shown */
    bool fresh;                         /**< the next digit starts a new number */
    bool error;
    char message[24];                   /**< "CAN'T DIVIDE BY 0" */
    uint8_t answers;                    /**< answers worked out (the badge) */
};
void mfc_Calc_Clear(Mfc_Calc_t& c);
/** @brief A key: '0'-'9', '.', '+', '-', '*', '/', '=', 'C' (clear), '<' (rub out). */
void mfc_Calc_Key(Mfc_Calc_t& c, char key);
/** @brief A number as the calculator shows it (up to 10 digits; "TOO BIG" beyond). */
void mfc_Calc_Format(double v, char* out, size_t size);

// The piano -----------------------------------------------------------------------------------------------------------------
#define MFC_PIANO_WHITE 14                /**< C4 to B5 */
/** @brief A key of the piano: white keys 0-13, black keys 100 + the white key on their left. Its frequency in Hz. */
float mfc_Piano_Hz(int key);
/** @brief The note's name ("C", "F#"). */
const char* mfc_Piano_Name(int key);
/** @brief The piano key a computer key plays (the lower row z-m and s-j, the upper q-u and 2-7), or -1. */
int mfc_Piano_Key_For(char c);
struct Mfc_Song_t { const char* name; const int8_t* keys; int count; };
#define MFC_SONGS 2
const Mfc_Song_t& mfc_Song(int i);

// The secret pictures -------------------------------------------------------------------------------------------------------
const char* mfc_Picture_Name(uint8_t i);

// The pretend internet ----------------------------------------------------------------------------------------------------
/**
 * A page's text is words, with links written [shown words|address]. The pages, their addresses and the safety
 * lessons are all in the firmware.
 */
enum Mfc_Page_Kind_t : uint8_t { MFC_PAGE_TEXT = 0, MFC_PAGE_HOME, MFC_PAGE_RESULTS, MFC_PAGE_GAMES, MFC_PAGE_CLUB };
struct Mfc_Page_t {
    const char* address;                /**< "animals" (shown as kidsnet/animals) */
    const char* title;
    uint16_t color;                     /**< the page's header (RGB565) */
    uint8_t kind;
    const char* text;
    const char* keywords;               /**< what a search finds it by */
};
int mfc_Web_Pages(void);
const Mfc_Page_t& mfc_Web_Page(int i);
/** @brief The page at an address, or -1. */
int mfc_Web_Find(const char* address);
/** @brief Pages a search finds (best first), at most @p max. */
int mfc_Web_Search(const char* query, int* out, int max);

/** A word of a laid-out page: its line and column, and the link it belongs to (-1: none). */
struct Mfc_Word_t { uint8_t line, col, len; int8_t link; uint16_t at; };
#define MFC_WEB_WORDS 220
#define MFC_WEB_LINKS 16
struct Mfc_Layout_t {
    Mfc_Word_t w[MFC_WEB_WORDS];
    int words, lines;
    int8_t linkPage[MFC_WEB_LINKS];     /**< each link's page (-1: none) */
    int links;
};
/** @brief Lays out a page's text in @p cols columns. A word's text is page.text + at, len characters. */
void mfc_Web_Layout(const char* text, int cols, Mfc_Layout_t& out);

#endif
