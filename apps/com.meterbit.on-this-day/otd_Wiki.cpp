// A day's "Selected anniversaries" wikitext to plain text (otd_Wiki.h).
#include "otd_Wiki.h"
#include <string.h>
#include <ctype.h>

struct Out { char* p; size_t size, n; };
static void put(Out& o, char c){ if (o.n + 1 < o.size) o.p[o.n++] = c; }
static void puts_(Out& o, const char* s){ while (*s) put(o, *s++); }

static bool starts(const char* p, const char* e, const char* s){
    const size_t n = strlen(s);
    return (size_t)(e - p) >= n && strncmp(p, s, n) == 0;
}
static bool starts_Ci(const char* p, const char* e, const char* s){
    const size_t n = strlen(s);
    return (size_t)(e - p) >= n && strncasecmp(p, s, n) == 0;
}

// The end of the {{...}} or [[...]] that opens at p (nested), or e.
static const char* close_Of(const char* p, const char* e, char open, char close){
    int depth = 0;
    for (const char* q = p; q + 1 < e; q++){
        if (q[0] == open && q[1] == open){ depth++; q++; continue; }
        if (q[0] == close && q[1] == close){ if (--depth == 0) return q + 2; q++; }
    }
    return e;
}

// The parameters of a template body "name|a|b" (top level only: links and templates inside stay whole).
static int split(const char* p, const char* e, const char** from, const char** to, int max){
    int n = 0, depth = 0;
    const char* start = p;
    for (const char* q = p; q < e; q++){
        if (q + 1 < e && ((q[0] == '[' && q[1] == '[') || (q[0] == '{' && q[1] == '{'))){ depth++; q++; continue; }
        if (q + 1 < e && ((q[0] == ']' && q[1] == ']') || (q[0] == '}' && q[1] == '}'))){ depth--; q++; continue; }
        if (*q == '|' && depth == 0){ if (n < max){ from[n] = start; to[n] = q; n++; } start = q + 1; }
    }
    if (n < max){ from[n] = start; to[n] = e; n++; }
    return n;
}

static void plain(Out& o, const char* p, const char* e);

// A template: the few that carry words keep them; the rest go.
static void template_Text(Out& o, const char* p, const char* e){
    const char* from[12]; const char* to[12];
    const int n = split(p, e, from, to, 12);
    char name[32];
    size_t k = 0;
    for (const char* q = from[0]; q < to[0] && k + 1 < sizeof(name); q++) if (*q != ' ' || k) name[k++] = (char)tolower((unsigned char)*q);
    while (k && name[k - 1] == ' ') k--;
    name[k] = '\0';
    if (!strcmp(name, "snd") || !strcmp(name, "spaced ndash") || !strcmp(name, "spnd") || !strcmp(name, "ndash") || !strcmp(name, "mdash")){ puts_(o, " - "); return; }
    if ((!strcmp(name, "nowrap") || !strcmp(name, "nobr") || !strcmp(name, "nobold") || !strcmp(name, "noitalic") || !strcmp(name, "small")) && n >= 2){ plain(o, from[1], to[1]); return; }
    if ((!strncmp(name, "lang", 4) || !strcmp(name, "transl") || !strcmp(name, "transliteration") || !strcmp(name, "nihongo")) && n >= 2){
        const int i = !strcmp(name, "nihongo") ? 1 : n - 1;
        plain(o, from[i], to[i]);
        return;
    }
    if ((!strcmp(name, "ship") || !strcmp(name, "uss") || !strcmp(name, "hms") || !strcmp(name, "sclass") || !strcmp(name, "ss") || !strcmp(name, "hmas")) && n >= 2){
        if (strcmp(name, "ship") && strcmp(name, "sclass")){ for (const char* q = from[0]; q < to[0]; q++) if (*q != ' ') put(o, (char)toupper((unsigned char)*q)); put(o, ' '); }
        for (int i = 1; i < n && i < 3; i++){ if (i > 1) put(o, ' '); plain(o, from[i], to[i]); }
        return;
    }
    if ((!strcmp(name, "convert") || !strcmp(name, "cvt")) && n >= 3){ plain(o, from[1], to[1]); put(o, ' '); plain(o, from[2], to[2]); return; }
}

static void plain(Out& o, const char* p, const char* e){
    while (p < e){
        if (starts(p, e, "<!--")){ const char* q = strstr(p + 4, "-->"); p = (q && q < e) ? q + 3 : e; continue; }
        if (starts_Ci(p, e, "<ref")){
            const char* gt = (const char*)memchr(p, '>', (size_t)(e - p));
            if (gt && gt[-1] == '/'){ p = gt + 1; continue; }
            const char* q = strstr(p, "</ref>");
            p = (q && q < e) ? q + 6 : e;
            continue;
        }
        if (*p == '<'){ const char* gt = (const char*)memchr(p, '>', (size_t)(e - p)); p = gt ? gt + 1 : e; continue; }
        if (starts(p, e, "{{")){ const char* q = close_Of(p, e, '{', '}'); template_Text(o, p + 2, q - 2 > p + 2 ? q - 2 : p + 2); p = q; continue; }
        if (starts(p, e, "[[")){
            const char* q = close_Of(p, e, '[', ']');
            const char* in = p + 2; const char* end = q - 2 > in ? q - 2 : in;
            if (starts_Ci(in, end, "File:") || starts_Ci(in, end, "Image:") || starts_Ci(in, end, "Category:")){ p = q; continue; }
            const char* from[8]; const char* to[8];
            const int n = split(in, end, from, to, 8);
            plain(o, from[n - 1], to[n - 1]);
            p = q;
            continue;
        }
        if (starts(p, e, "'''")){ p += 3; continue; }
        if (starts(p, e, "''")){ p += 2; continue; }
        if (*p == '&'){
            static const struct { const char* name; const char* text; } ENT[] = {
                {"&nbsp;", " "}, {"&ndash;", "-"}, {"&mdash;", "-"}, {"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"},
                {"&minus;", "-"}, {"&thinsp;", ""}, {"&lt;", "<"}, {"&gt;", ">"}};
            bool done = false;
            for (const auto& x : ENT) if (starts(p, e, x.name)){ puts_(o, x.text); p += strlen(x.name); done = true; break; }
            if (done) continue;
        }
        put(o, *p == '\n' || *p == '\t' ? ' ' : *p);
        p++;
    }
}

// Spaces tidied, "(pictured)" and the like left out.
static void tidy(char* s){
    // The main page's notes on its picture: "(pictured)", "(signing pictured)", "(artist's impression shown)".
    static const char* const END[] = {"pictured)", "depicted)", "illustrated)", "shown)"};
    for (char* q = strstr(s, " ("); q; q = strstr(q + 1, " (")){
        char* close = strchr(q, ')');
        if (!close || close - q > 48) continue;
        bool drop = false;
        for (const char* e : END){ const size_t n = strlen(e); if ((size_t)(close + 1 - q) >= n && !strncmp(close + 1 - n, e, n)) drop = true; }
        if (drop){ memmove(q, close + 1, strlen(close + 1) + 1); q--; }
    }
    char* w = s;
    bool space = true;
    for (char* r = s; *r; r++){
        if (*r == ' '){ if (!space) *w++ = ' '; space = true; }
        else { if ((*r == ',' || *r == '.' || *r == ';' || *r == ':' || *r == ')') && w > s && w[-1] == ' ') w--; *w++ = *r; space = *r == '('; if (*r == '(') space = true; }
    }
    while (w > s && w[-1] == ' ') w--;
    *w = '\0';
}

void otd_Plain(const char* in, size_t n, char* out, size_t size){
    if (!size) return;
    Out o = {out, size, 0};
    plain(o, in, in + n);
    out[o.n] = '\0';
    tidy(out);
}

static const char* line_End(const char* p){ const char* q = strchr(p, '\n'); return q ? q : p + strlen(p); }

// The year and the rest of an event line "[[1161]] – text" (after "* ").
static bool event_Line(const char* p, const char* e, Otd_Event_t& ev){
    static const char* const DASH[] = {"\xE2\x80\x93", "&ndash;", "{{snd}}", " - "};
    const char* cut = nullptr; size_t len = 0;
    for (const char* d : DASH){
        const char* q = strstr(p, d);
        if (q && q < e && (!cut || q < cut)){ cut = q; len = strlen(d); }
    }
    if (!cut || cut - p > 60) return false;
    otd_Plain(p, (size_t)(cut - p), ev.year, sizeof(ev.year));
    otd_Plain(cut + len, (size_t)(e - cut - len), ev.text, sizeof(ev.text));
    return ev.year[0] && ev.text[0];
}

static void people(const char* p, const char* e, Otd_Day_t& day){
    const char* from[3 * OTD_PEOPLE + 4]; const char* to[3 * OTD_PEOPLE + 4];
    const int n = split(p, e, from, to, 3 * OTD_PEOPLE + 4);
    for (int i = 1; i + 1 < n && day.people < OTD_PEOPLE; i += 3){      // name | b or d | year
        Otd_Person_t& who = day.person[day.people];
        memset(&who, 0, sizeof(who));
        otd_Plain(from[i], (size_t)(to[i] - from[i]), who.name, sizeof(who.name));
        // The comment beside a name says who they were: "<!--American politician-->".
        const char* c = strstr(from[i], "<!--");
        if (c && c < to[i]){
            const char* ce = strstr(c, "-->");
            if (ce && ce < to[i]) otd_Plain(c + 4, (size_t)(ce - c - 4), who.about, sizeof(who.about));
        }
        char kind[8];
        otd_Plain(from[i + 1], (size_t)(to[i + 1] - from[i + 1]), kind, sizeof(kind));
        who.died = kind[0] == 'd';
        if (i + 2 < n) otd_Plain(from[i + 2], (size_t)(to[i + 2] - from[i + 2]), who.year, sizeof(who.year));
        if (who.name[0] && (kind[0] == 'b' || kind[0] == 'd')) day.people++;
    }
}

int otd_Parse(const char* text, Otd_Day_t& day){
    memset(&day, 0, sizeof(day));
    if (!text) return 0;
    const char* p = strstr(text, "</noinclude>");
    p = p ? p + 12 : text;
    while (*p){
        const char* e = line_End(p);
        while (*p == ' ') p++;
        if (*p == '*'){
            const char* q = p + 1;
            while (*q == ' ') q++;
            if (day.events < OTD_EVENTS && event_Line(q, e, day.event[day.events])) day.events++;
        } else if (starts_Ci(p, e, "{{Born and died list")){
            const char* end = close_Of(p, p + strlen(p), '{', '}');
            people(p + 2, end - 2, day);
            e = line_End(end > p ? end - 1 : p);
        } else if (starts(p, e, "'''[[") && !day.observances[0] && !day.events){
            // The title line: "'''[[November 27]]''': observances" (often a comment only).
            char line[OTD_TEXT];
            otd_Plain(p, (size_t)(e - p), line, sizeof(line));
            const char* colon = strchr(line, ':');
            if (colon){
                colon++;
                while (*colon == ' ') colon++;
                size_t k = 0;
                while (colon[k] && k + 1 < sizeof(day.observances)){ day.observances[k] = colon[k]; k++; }
                day.observances[k] = '\0';
            }
        }
        p = *e ? e + 1 : e;
    }
    return day.events;
}
