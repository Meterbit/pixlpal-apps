// Pixlpal compat: Arduino's file system API on the app's own files (pxl_file_*). Paths are made one flat name:
// "/kitwx/omt.json" is "kitwx_omt.json" (a long one is cut and given a hash). A file is read whole when opened and
// written whole when closed (64 KB at most).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "WString.h"
namespace fs {
class File {
public:
    File() {}
    File(const char* name, char mode);
    File(const File& o);
    File& operator=(const File& o);
    ~File();
    explicit operator bool() const;
    size_t size() const;
    size_t read(uint8_t* buf, size_t n);
    int read();
    size_t readBytes(char* buf, size_t n){ return read((uint8_t*)buf, n); }
    String readString();
    size_t write(const uint8_t* buf, size_t n);
    size_t write(uint8_t c){ return write(&c, 1); }
    size_t print(const char* s){ return s ? write((const uint8_t*)s, strlen(s)) : 0; }
    size_t print(const String& s){ return print(s.c_str()); }
    size_t println(const char* s = ""){ return print(s) + print("\n"); }
    int available() const;
    bool seek(size_t pos);
    size_t position() const;
    void flush(){}
    void close();
    const char* name() const;
    bool isDirectory() const { return false; }
    File openNextFile(){ return File(); }
private:
    struct Data;
    Data* d_ = nullptr;
};
class FS {
public:
    bool begin(bool = false, const char* = "/littlefs", uint8_t = 10, const char* = nullptr){ return true; }
    File open(const char* path, const char* mode = "r", bool create = false);
    File open(const String& path, const char* mode = "r", bool create = false){ return open(path.c_str(), mode, create); }
    bool exists(const char* path);
    bool exists(const String& path){ return exists(path.c_str()); }
    bool remove(const char* path);
    bool remove(const String& path){ return remove(path.c_str()); }
    bool rename(const char* from, const char* to);
    bool mkdir(const char*){ return true; }
    bool rmdir(const char*){ return true; }
    size_t totalBytes(){ return 256 * 1024; }
    size_t usedBytes();
};
}
using fs::File;
using fs::FS;
void pxl_compat_file_name(const char* path, char* out, size_t size);
