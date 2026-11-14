// My First Computer on Pixlpal: the children's files in flash, sounds, the USB keyboard and mouse, the clock.
//
// Files: the app's own (see mfc_File_Path). Pictures are PNG (so a parent can save one to the phone); writing is plain
// text. Names are only A-Z, 0-9, '-' and '_' (mfc_Name_Clean), so nothing a child types can reach another child's.
#include "mfc_App.h"
#include "mfc_Files.h"
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_usb_hid.h"
#include "snd_Sound.h"
#include "lodepng.h"
#include "esp_heap_caps.h"
#include "pixlpal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// As an installed app the files are the app's own (pxl_file_*), one folder of flat names: "c<child><p|t|b>_<NAME>.png"
// or ".txt" (p pictures, t writing, b the bin), at most 22 characters. mfc_File_Path gives that name.
static const char SUB[MFC_FOLDERS] = {'p', 't', 'b'};

void mfc_File_Path(uint8_t child, uint8_t folder, const char* name, uint8_t kind, char* out, size_t size){
    snprintf(out, size, "c%u%c_%s.%s", (unsigned)(child % MFC_CHILDREN), SUB[folder % MFC_FOLDERS], name, kind ? "txt" : "png");
}

/** "CAT.png" -> "CAT", kind 0. False for anything that is not one of ours. */
static bool split_Name(const char* file, char* name, size_t size, uint8_t& kind){
    const char* dot = strrchr(file, '.');
    if (!dot || dot == file) return false;
    if (!strcmp(dot, ".png")) kind = 0;
    else if (!strcmp(dot, ".txt")) kind = 1;
    else return false;
    const size_t n = (size_t)(dot - file);
    if (n > MFC_NAME_LEN || n + 1 > size) return false;
    for (size_t i = 0; i < n; i++) if (!(isupper((unsigned char)file[i]) || isdigit((unsigned char)file[i]) || file[i] == '_' || file[i] == '-')) return false;
    memcpy(name, file, n);
    name[n] = '\0';
    return true;
}

static bool exists(const char* name){
    char b[1];
    return pxl_file_read(name, b, 1) >= 0;
}

int mfc_Files_List(uint8_t child, uint8_t folder, Mfc_File_t* out, int max){
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "c%u%c_", (unsigned)(child % MFC_CHILDREN), SUB[folder % MFC_FOLDERS]);
    const size_t pl = strlen(prefix);
    char* list = (char*)malloc(8192);
    if (!list) return 0;
    const int bytes = pxl_file_list("", list, 8192);
    int n = 0;
    for (char* line = list; bytes > 0 && *line && n < max;){
        char* end = strchr(line, '\n');
        if (end) *end = '\0';
        char* tab = strchr(line, '\t');
        if (tab && strncmp(line, prefix, pl) == 0){
            *tab = '\0';
            Mfc_File_t f = {};
            if (split_Name(line + pl, f.name, sizeof(f.name), f.kind)){
                char* tab2 = strchr(tab + 1, '\t');
                f.size = (uint32_t)strtoul(tab + 1, nullptr, 10);
                f.time = tab2 ? (uint32_t)strtoul(tab2 + 1, nullptr, 10) : 0;
                out[n++] = f;
            }
        }
        line = end ? end + 1 : line + strlen(line);
    }
    free(list);
    // Newest first (then by name).
    for (int i = 1; i < n; i++){
        Mfc_File_t f = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].time < f.time || (out[j].time == f.time && strcmp(out[j].name, f.name) > 0))){ out[j + 1] = out[j]; j--; }
        out[j + 1] = f;
    }
    return n;
}

int mfc_Files_Count(uint8_t child){
    Mfc_File_t* list = (Mfc_File_t*)heap_caps_malloc(sizeof(Mfc_File_t) * MFC_LIST, MALLOC_CAP_SPIRAM);
    if (!list) return 0;
    int n = 0;
    for (uint8_t f = 0; f < MFC_FOLDERS; f++) n += mfc_Files_List(child, f, list, MFC_LIST);
    heap_caps_free(list);
    return n;
}

uint32_t mfc_Files_Bytes(uint8_t child){
    Mfc_File_t* list = (Mfc_File_t*)heap_caps_malloc(sizeof(Mfc_File_t) * MFC_LIST, MALLOC_CAP_SPIRAM);
    if (!list) return 0;
    uint32_t bytes = 0;
    for (uint8_t f = 0; f < MFC_FOLDERS; f++){
        const int n = mfc_Files_List(child, f, list, MFC_LIST);
        for (int i = 0; i < n; i++) bytes += list[i].size;
    }
    heap_caps_free(list);
    return bytes;
}

// Pixlpal writes a file beside the old one and puts it in place: a power cut never leaves half a file.
static bool write_All(const char* name, const void* data, size_t size){
    return pxl_file_write(name, data, (int)size) == (int)size;
}

bool mfc_File_Save_Picture(uint8_t child, const char* name, const uint16_t* px, int w, int h){
    uint8_t* rgb = (uint8_t*)heap_caps_malloc((size_t)w * h * 3, MALLOC_CAP_SPIRAM);
    if (!rgb) return false;
    for (int i = 0; i < w * h; i++){
        const uint16_t c = px[i];
        rgb[i * 3] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((c & 31) * 255 / 31);
    }
    unsigned char* png = nullptr;
    size_t size = 0;
    const unsigned err = lodepng_encode24(&png, &size, rgb, (unsigned)w, (unsigned)h);
    heap_caps_free(rgb);
    if (err || !png) return false;
    char path[40];
    mfc_File_Path(child, MFC_F_PICTURES, name, 0, path, sizeof(path));
    const bool ok = write_All(path, png, size);
    free(png);
    return ok;
}

bool mfc_File_Read(const char* path, uint8_t** data, size_t* size, size_t max){
    if (max > 64 * 1024) max = 64 * 1024;                  // an installed app's file is 64 KB at most
    uint8_t* b = (uint8_t*)heap_caps_malloc(max + 1, MALLOC_CAP_SPIRAM);
    if (!b) return false;
    const int n = pxl_file_read(path, b, (int)max + 1);
    if (n <= 0 || (size_t)n > max){ heap_caps_free(b); return false; }
    b[n] = 0;
    *data = b;
    *size = (size_t)n;
    return true;
}

bool mfc_File_Load_Picture(uint8_t child, uint8_t folder, const char* name, uint16_t* px, int w, int h){
    char path[40];
    mfc_File_Path(child, folder, name, 0, path, sizeof(path));
    uint8_t* png = nullptr;
    size_t size = 0;
    if (!mfc_File_Read(path, &png, &size, 64 * 1024)) return false;
    unsigned char* rgb = nullptr;
    unsigned iw = 0, ih = 0;
    const unsigned err = lodepng_decode24(&rgb, &iw, &ih, png, size);
    heap_caps_free(png);
    if (err || !rgb) return false;
    // Ours are the canvas's size; anything else is cut or padded with white.
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++){
            uint16_t c = 0xFFFF;
            if ((unsigned)x < iw && (unsigned)y < ih){
                const uint8_t* p = rgb + ((size_t)y * iw + x) * 3;
                c = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
            }
            px[y * w + x] = c;
        }
    free(rgb);
    return true;
}

bool mfc_File_Save_Text(uint8_t child, const char* name, const char* text){
    char path[40];
    mfc_File_Path(child, MFC_F_WRITING, name, 1, path, sizeof(path));
    return write_All(path, text, strlen(text));
}

bool mfc_File_Load_Text(uint8_t child, uint8_t folder, const char* name, char* out, size_t size){
    char path[40];
    mfc_File_Path(child, folder, name, 1, path, sizeof(path));
    const int n = pxl_file_read(path, out, (int)size - 1);
    if (n < 0) return false;
    out[n] = '\0';
    return true;
}

// Moving a file: read it, write it under its new name, remove the old one.
static bool rename_File(const char* a, const char* b){
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!mfc_File_Read(a, &data, &size, 64 * 1024)) return false;
    const bool ok = write_All(b, data, size) && pxl_file_remove(a) == 0;
    heap_caps_free(data);
    return ok;
}

/** Moves a file, with "_2", "_3"... if its name is taken where it goes. */
static bool move(uint8_t child, uint8_t from, uint8_t to, const char* name, uint8_t kind){
    char a[40], b[40], n[MFC_NAME_LEN + 1];
    mfc_File_Path(child, from, name, kind, a, sizeof(a));
    snprintf(n, sizeof(n), "%s", name);
    mfc_File_Path(child, to, n, kind, b, sizeof(b));
    for (int i = 2; exists(b) && i < 100; i++){
        snprintf(n, sizeof(n), "%.*s_%d", MFC_NAME_LEN - 3, name, i);
        mfc_File_Path(child, to, n, kind, b, sizeof(b));
    }
    return rename_File(a, b);
}

bool mfc_File_To_Bin(uint8_t child, uint8_t folder, const char* name, uint8_t kind){ return folder != MFC_F_BIN && move(child, folder, MFC_F_BIN, name, kind); }
bool mfc_File_Restore(uint8_t child, const char* name, uint8_t kind){ return move(child, MFC_F_BIN, kind ? MFC_F_WRITING : MFC_F_PICTURES, name, kind); }

bool mfc_File_Rename(uint8_t child, uint8_t folder, const char* from, const char* to, uint8_t kind){
    char a[40], b[40];
    mfc_File_Path(child, folder, from, kind, a, sizeof(a));
    mfc_File_Path(child, folder, to, kind, b, sizeof(b));
    if (exists(b)) return false;
    return rename_File(a, b);
}

bool mfc_File_Erase(uint8_t child, uint8_t folder, const char* name, uint8_t kind){
    char p[40];
    mfc_File_Path(child, folder, name, kind, p, sizeof(p));
    return pxl_file_remove(p) == 0;
}

void mfc_Files_Erase_All(uint8_t child){
    Mfc_File_t* list = (Mfc_File_t*)heap_caps_malloc(sizeof(Mfc_File_t) * MFC_LIST, MALLOC_CAP_SPIRAM);
    if (!list) return;
    for (uint8_t f = 0; f < MFC_FOLDERS; f++){
        for (int round = 0; round < 8; round++){
            const int n = mfc_Files_List(child, f, list, MFC_LIST);
            if (!n) break;
            for (int i = 0; i < n; i++) mfc_File_Erase(child, f, list[i].name, list[i].kind);
        }
    }
    heap_caps_free(list);
}

// Sounds ---------------------------------------------------------------------------------------------------------------
static float level_Of(const Mfc_State_t& s, float base){
    const uint8_t v = s.signedIn || s.mode == MFC_M_DESKTOP ? s.set->c[s.child % MFC_CHILDREN].volume : 6;
    return base * v / 6.0f;
}

void mfc_Sound(const Mfc_State_t& s, uint8_t sound){
    if (!s.k || !s.k->sound) return;
    static const Snd_Note_t CLICK[] = {{1568, 18}};
    static const Snd_Note_t OPEN[] = {{660, 50}, {990, 70}};
    static const Snd_Note_t CLOSE[] = {{990, 50}, {660, 70}};
    static const Snd_Note_t POP[] = {{1200, 30}, {1800, 40}};
    static const Snd_Note_t YAY[] = {{784, 90}, {988, 90}, {1175, 160}};
    static const Snd_Note_t OOPS[] = {{392, 140}, {330, 220}};
    static const Snd_Note_t FANFARE[] = {{523, 110}, {659, 110}, {784, 110}, {1047, 240}, {0, 50}, {784, 100}, {1047, 340}};
    static const Snd_Note_t ERROR[] = {{440, 90}, {0, 40}, {440, 120}};
    static const Snd_Note_t START[] = {{523, 120}, {784, 120}, {1047, 120}, {1568, 260}};
    static const Snd_Note_t BYE[] = {{1047, 120}, {784, 120}, {523, 260}};
    static const Snd_Note_t TRASH[] = {{300, 40}, {220, 60}, {160, 90}};
    const float k = level_Of(s, 1.0f);
    if (k <= 0) return;
    switch (sound){
        case S_CLICK: snd_Play(CLICK, 1, 0.18f * k, true); break;
        case S_OPEN: snd_Play(OPEN, 2, 0.22f * k, false); break;
        case S_CLOSE: snd_Play(CLOSE, 2, 0.22f * k, false); break;
        case S_POP: snd_Play(POP, 2, 0.28f * k, true); break;
        case S_YAY: snd_Play(YAY, 3, 0.35f * k, false); break;
        case S_OOPS: snd_Play(OOPS, 2, 0.3f * k, false); break;
        case S_FANFARE: snd_Play(FANFARE, 7, 0.4f * k, true); break;
        case S_ERROR: snd_Play(ERROR, 3, 0.25f * k, true); break;
        case S_START: snd_Play(START, 4, 0.3f * k, false); break;
        case S_BYE: snd_Play(BYE, 3, 0.3f * k, false); break;
        default: snd_Play(TRASH, 3, 0.3f * k, true); break;
    }
}

void mfc_Note(const Mfc_State_t& s, float hz, int ms){
    if (!s.k || !s.k->sound || hz <= 0) return;
    const Snd_Note_t n[1] = {{hz, ms}};
    const float k = level_Of(s, 0.35f);
    if (k > 0) snd_Play(n, 1, k > 1 ? 1 : k, false);
}

void mfc_Speak(const Mfc_State_t& s, const char* text){ if (s.k) kk_Say(*s.k, text); }

bool mfc_Have_Keyboard(void){ return mtb_Usb_Keyboard_Connected(); }
bool mfc_Have_Mouse(void){ return mtb_Usb_Mouse_Connected(); }

void mfc_Clock_Text(char* out, size_t size){
    const time_t now = time(nullptr);
    if (now < 1700000000){ snprintf(out, size, "%s", ""); return; }
    struct tm t;
    localtime_r(&now, &t);
    if (mtb_Clock_24h()) snprintf(out, size, "%02d:%02d", t.tm_hour, t.tm_min);
    else snprintf(out, size, "%d:%02d%s", t.tm_hour % 12 ? t.tm_hour % 12 : 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
}
