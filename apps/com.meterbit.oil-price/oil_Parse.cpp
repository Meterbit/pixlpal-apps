#include "oil_Parse.h"
#include <string.h>
#include <stdlib.h>

//##############################################################################################################
// A byte source with one byte of push-back.
class Oil_Peek_t {
public:
    explicit Oil_Peek_t(Oil_Byte_Source_t& in) : in(in) {}
    int get(void){
        if (held >= 0){ const int c = held; held = -1; return c; }
        return in.read();
    }
    void unget(int c){ held = c; }
    int next_Non_Space(void){
        int c;
        do c = get(); while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        return c;
    }
private:
    Oil_Byte_Source_t& in;
    int held = -1;
};

// Reads a JSON string whose opening quote has been read. Returns false at the end of input.
static bool oil_Read_String(Oil_Peek_t& in, char* dest, size_t size){
    size_t n = 0;
    for (;;){
        int c = in.get();
        if (c < 0) return false;
        if (c == '"') break;
        if (c == '\\'){
            c = in.get();
            if (c < 0) return false;
            switch (c){
                case 'n': case 'r': case 't': c = ' '; break;
                case 'b': case 'f': c = 0; break;
                case 'u': {
                    char hex[5] = {0};
                    for (int i = 0; i < 4; i++){
                        const int h = in.get();
                        if (h < 0) return false;
                        hex[i] = (char)h;
                    }
                    const unsigned long cp = strtoul(hex, nullptr, 16);
                    // Kept as UTF-8 so oil_Clean_Text() can map typographic punctuation.
                    if (cp < 0x80) c = (int)cp;
                    else {
                        char utf8[3];
                        size_t length;
                        if (cp < 0x800){ utf8[0] = (char)(0xC0 | (cp >> 6)); utf8[1] = (char)(0x80 | (cp & 0x3F)); length = 2; }
                        else { utf8[0] = (char)(0xE0 | (cp >> 12)); utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[2] = (char)(0x80 | (cp & 0x3F)); length = 3; }
                        for (size_t i = 0; i < length && n + 1 < size; i++) dest[n++] = utf8[i];
                        c = 0;
                    }
                    break;
                }
                default: break;     // \" \\ \/
            }
            if (c == 0) continue;
        }
        if (n + 1 < size) dest[n++] = (char)c;
    }
    dest[n] = '\0';
    return true;
}

// Skips a value that starts with c (already read): a string, object, array or literal. Returns the byte after a
// literal, which ends it, or 0 when the value ended itself; -1 at the end of input.
static int oil_Skip_Value(Oil_Peek_t& in, int c){
    if (c == '"'){
        char dummy[2];
        return oil_Read_String(in, dummy, sizeof(dummy)) ? 0 : -1;
    }
    if (c == '{' || c == '['){
        int depth = 1;
        while (depth > 0){
            c = in.get();
            if (c < 0) return -1;
            if (c == '"'){ if (oil_Skip_Value(in, c) < 0) return -1; }
            else if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') depth--;
        }
        return 0;
    }
    do c = in.get(); while (c >= 0 && c != ',' && c != '}' && c != ']' && c != ' ' && c != '\n' && c != '\r' && c != '\t');
    return c;
}

// Reads the scalar fields of an object whose '{' has been read.
static bool oil_Read_Row(Oil_Peek_t& in, Oil_Json_Field_Fn onField, void* context){
    char key[OIL_PARSE_KEY_LEN];
    char value[OIL_PARSE_VALUE_LEN];
    int c = in.next_Non_Space();
    if (c == '}') return true;
    for (;;){
        if (c != '"' || !oil_Read_String(in, key, sizeof(key))) return false;
        if (in.next_Non_Space() != ':') return false;
        c = in.next_Non_Space();
        if (c == '"'){
            if (!oil_Read_String(in, value, sizeof(value))) return false;
            onField(context, key, value, false);
        } else if (c == '{' || c == '['){
            if (oil_Skip_Value(in, c) < 0) return false;
        } else if (c >= 0){
            size_t n = 0;
            while (c >= 0 && c != ',' && c != '}' && c != ' ' && c != '\n' && c != '\r' && c != '\t'){
                if (n + 1 < sizeof(value)) value[n++] = (char)c;
                c = in.get();
            }
            value[n] = '\0';
            in.unget(c);
            const bool isNull = strcmp(value, "null") == 0;
            onField(context, key, isNull ? "" : value, isNull);
        } else return false;
        c = in.next_Non_Space();
        if (c == '}') return true;
        if (c != ',') return false;
        c = in.next_Non_Space();
    }
}

int oil_Json_Rows(Oil_Byte_Source_t& source, const char* arrayKey, Oil_Json_Field_Fn onField, Oil_Json_Row_Fn onRow, void* context){
    Oil_Peek_t in(source);
    char text[OIL_PARSE_KEY_LEN];

    // Find "arrayKey": [ at any depth. Every string is read whole, so a quote inside one is never mistaken for a key.
    for (;;){
        int c = in.get();
        if (c < 0) return -1;
        if (c != '"') continue;
        if (!oil_Read_String(in, text, sizeof(text))) return -1;
        c = in.next_Non_Space();
        if (c != ':'){ in.unget(c); continue; }
        c = in.next_Non_Space();
        if (c == '[' && strcmp(text, arrayKey) == 0) break;
        in.unget(c);
    }

    int rows = 0;
    int c = in.next_Non_Space();
    if (c == ']') return 0;
    for (;;){
        if (c == '{'){
            if (!oil_Read_Row(in, onField, context)) return -1;
            rows++;
            if (!onRow(context)) return rows;
        } else {
            const int after = oil_Skip_Value(in, c);   // Not an object: ignored.
            if (after < 0) return -1;
            if (after > 0) in.unget(after);
        }
        c = in.next_Non_Space();
        if (c == ']') return rows;
        if (c != ',') return -1;
        c = in.next_Non_Space();
    }
}

//##############################################################################################################
// Matches a fixed pattern against a stream, one byte at a time. The patterns used here repeat no prefix other
// than their first character, so a restart on that character is enough.
struct Oil_Matcher_t {
    const char* pattern;
    size_t length;
    size_t matched;
    explicit Oil_Matcher_t(const char* p) : pattern(p), length(strlen(p)), matched(0) {}
    bool feed(int c){
        if (c == pattern[matched]) matched++;
        else matched = (c == pattern[0]) ? 1 : 0;
        if (matched == length){ matched = 0; return true; }
        return false;
    }
};

int oil_Tie_Articles(Oil_Byte_Source_t& in, Oil_Article_Fn onArticle, void* context){
    Oil_Matcher_t dateTag("class=\"date\">");
    Oil_Matcher_t linkTag("<h1><a href=\"detail.php?id=");
    char date[24] = "";
    char title[OIL_PARSE_VALUE_LEN];
    int found = 0;

    for (;;){
        int c = in.read();
        if (c < 0) return found;
        if (dateTag.feed(c)){
            size_t n = 0;
            while ((c = in.read()) >= 0 && c != '<') if (n + 1 < sizeof(date)) date[n++] = (char)c;
            date[n] = '\0';
            if (c < 0) return found;
        }
        if (!linkTag.feed(c)) continue;

        uint32_t id = 0;
        while ((c = in.read()) >= '0' && c <= '9') id = id * 10 + (uint32_t)(c - '0');
        while (c >= 0 && c != '>') c = in.read();      // Rest of the <a ...> tag.
        size_t n = 0;
        while ((c = in.read()) >= 0 && c != '<') if (n + 1 < sizeof(title)) title[n++] = (char)c;
        title[n] = '\0';
        if (c < 0) return found;
        if (id != 0 && n > 0){
            found++;
            if (!onArticle(context, date, id, title)) return found;
        }
        date[0] = '\0';     // A date belongs to the article that follows it, never to a later one.
    }
}
