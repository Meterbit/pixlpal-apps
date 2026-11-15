#include "cc_Coin_Id.h"
#include <ctype.h>
#include <string.h>

bool cc_Id_Valid(const char* id){
    if (id == nullptr) return false;
    const size_t n = strlen(id);
    if (n == 0 || n > 39) return false;
    for (size_t i = 0; i < n; i++){
        const char c = id[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-')) return false;
    }
    return true;
}

bool cc_Parse_Value(const char* value, char* id, size_t idSize, char* symbol, size_t symbolSize){
    if (value == nullptr) return false;
    const char* bar = strchr(value, '|');
    if (bar == nullptr || (size_t)(bar - value) >= idSize) return false;
    memcpy(id, value, (size_t)(bar - value));
    id[bar - value] = '\0';
    const size_t n = strlen(bar + 1);
    if (n == 0 || n >= symbolSize) return false;
    for (size_t i = 0; i < n; i++){
        const char c = bar[1 + i];
        if (!(isalnum((unsigned char)c))) return false;
        symbol[i] = (char)toupper((unsigned char)c);
    }
    symbol[n] = '\0';
    return cc_Id_Valid(id);
}

