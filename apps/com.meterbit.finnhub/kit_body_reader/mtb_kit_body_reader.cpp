#include "mtb_kit_body_reader.h"
#include <string.h>

size_t Mtb_Kit_Body_Reader_t::readBytes(char* dest, size_t size){
    size_t copied = 0;
    if (copied < size && pushed >= 0){ dest[copied++] = (char)pushed; pushed = -1; }
    while (copied < size){
        if (position >= length && !fill()) break;
        size_t take = length - position;
        if (take > size - copied) take = size - copied;
        memcpy(dest + copied, buffer + position, take);
        position += take;
        copied += take;
    }
    return copied;
}

bool Mtb_Kit_Body_Reader_t::find(const char* token){
    const size_t n = strlen(token);
    size_t matched = 0;
    for (;;){
        const int c = read();
        if (c < 0) return false;
        if (c == token[matched]){ if (++matched == n) return true; }
        else matched = (c == token[0]) ? 1 : 0;
    }
}

bool Mtb_Kit_Body_Reader_t::skipPast(char target){
    int c;
    while ((c = read()) >= 0) if ((char)c == target) return true;
    return false;
}

int Mtb_Kit_Body_Reader_t::next_Non_Space(void){
    for (;;){
        const int c = read();
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return c;
    }
}

bool Mtb_Kit_Body_Reader_t::fill(void){
    if (maxBytes && total >= maxBytes) return false;
    const uint32_t start = millis();
    while (millis() - start < timeoutMs){
        const int waiting = stream.available();
        if (waiting > 0){
            const int received = stream.read(buffer, waiting < (int)sizeof(buffer) ? waiting : (int)sizeof(buffer));
            if (received > 0){
                position = 0;
                length = (size_t)received;
                total += (size_t)received;
                return true;
            }
        } else if (!stream.connected()) return false;
        delay(2);
    }
    return false;
}
