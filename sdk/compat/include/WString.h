// Pixlpal compat: the few parts of Arduino's String the firmware's apps use (building JSON text to send).
#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
class String {
    char* s_ = nullptr;
    size_t n_ = 0;
    void set(const char* p, size_t n){ char* t = (char*)malloc(n + 1); if (!t) return; if (n) memcpy(t, p, n); t[n] = 0; free(s_); s_ = t; n_ = n; }
public:
    String(){ set("", 0); }
    String(const char* p){ set(p ? p : "", p ? strlen(p) : 0); }
    String(const String& o){ set(o.c_str(), o.n_); }
    String(char c){ char b[2] = {c, 0}; set(b, c ? 1 : 0); }
    String(int v){ char b[16]; set(b, (size_t)snprintf(b, sizeof(b), "%d", v)); }
    String(unsigned v){ char b[16]; set(b, (size_t)snprintf(b, sizeof(b), "%u", v)); }
    String(long v){ char b[24]; set(b, (size_t)snprintf(b, sizeof(b), "%ld", v)); }
    String(unsigned long v){ char b[24]; set(b, (size_t)snprintf(b, sizeof(b), "%lu", v)); }
    String(double v, int decimals = 2){ char b[40]; set(b, (size_t)snprintf(b, sizeof(b), "%.*f", decimals, v)); }
    ~String(){ free(s_); }
    String& operator=(const String& o){ if (this != &o) set(o.c_str(), o.n_); return *this; }
    String& operator=(const char* p){ set(p ? p : "", p ? strlen(p) : 0); return *this; }
    String& operator+=(const String& o){ return concat(o.c_str(), o.n_); }
    String& operator+=(const char* p){ return p ? concat(p, strlen(p)) : *this; }
    String& operator+=(char c){ return concat(&c, 1); }
    String& operator+=(int v){ return *this += String(v); }
    String& concat(const char* p, size_t n){ char* t = (char*)malloc(n_ + n + 1); if (!t) return *this; memcpy(t, s_, n_); memcpy(t + n_, p, n); t[n_ + n] = 0; free(s_); s_ = t; n_ += n; return *this; }
    const char* c_str() const { return s_ ? s_ : ""; }
    size_t length() const { return n_; }
    bool isEmpty() const { return n_ == 0; }
    bool operator==(const char* p) const { return strcmp(c_str(), p ? p : "") == 0; }
    bool operator==(const String& o) const { return strcmp(c_str(), o.c_str()) == 0; }
    bool operator!=(const char* p) const { return !(*this == p); }
    char operator[](size_t i) const { return i < n_ ? s_[i] : 0; }
    int indexOf(char c) const { const char* p = strchr(c_str(), c); return p ? (int)(p - c_str()) : -1; }
    int indexOf(const char* t) const { const char* p = strstr(c_str(), t); return p ? (int)(p - c_str()) : -1; }
    String substring(size_t from, size_t to = (size_t)-1) const { if (to > n_) to = n_; if (from > to) from = to; String r; r.set(c_str() + from, to - from); return r; }
    long toInt() const { return strtol(c_str(), nullptr, 10); }
    float toFloat() const { return strtof(c_str(), nullptr); }
};
inline String operator+(const String& a, const String& b){ String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b){ String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b){ String r(a); r += b; return r; }
