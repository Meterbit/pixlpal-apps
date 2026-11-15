#include "fh_Symbol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool fh_Symbol_Valid(const char* s){
    if (s == nullptr) return false;
    const size_t n = strlen(s);
    if (n == 0 || n > 11) return false;
    for (size_t i = 0; i < n; i++){
        const char c = s[i];
        if (!(isupper((unsigned char)c) || isdigit((unsigned char)c) || c == '.' || c == '-' || c == '^')) return false;
    }
    return true;
}

bool fh_Symbol_Normalise(const char* text, char* dest, size_t size){
    if (text == nullptr || size < 2) return false;
    while (*text == ' ') text++;
    size_t n = 0;
    for (; text[n] && text[n] != ' ' && n + 1 < size; n++) dest[n] = (char)toupper((unsigned char)text[n]);
    dest[n] = '\0';
    return fh_Symbol_Valid(dest);
}

