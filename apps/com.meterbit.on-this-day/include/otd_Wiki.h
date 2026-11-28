/**
 * @file otd_Wiki.h
 * @brief Reads a day's "Selected anniversaries" page (the wikitext behind Wikipedia's "On this day" on its main page):
 *        the observances, the events with their years, and the births and deaths list, as plain text. Links keep
 *        their words, bold and italics go, templates and comments go (a few that carry words keep them). Pure C++:
 *        tested on a PC. The text stays UTF-8; the app makes it ASCII for the panel.
 */
#ifndef OTD_WIKI_H
#define OTD_WIKI_H

#include <stddef.h>

#define OTD_EVENTS      8
#define OTD_PEOPLE      8
#define OTD_TEXT        320
#define OTD_YEAR        16

struct Otd_Event_t { char year[OTD_YEAR]; char text[OTD_TEXT]; };
struct Otd_Person_t { char name[48]; char about[56]; char year[OTD_YEAR]; bool died; };
struct Otd_Day_t {
    char observances[OTD_TEXT];         // "Teachers' Day in Spain"; empty if none
    Otd_Event_t event[OTD_EVENTS];
    int events;
    Otd_Person_t person[OTD_PEOPLE];
    int people;
};

/** Reads @p wikitext (as the API gives it, after JSON). Returns the number of events. */
int otd_Parse(const char* wikitext, Otd_Day_t& day);

/** Wiki markup to plain text: "[[A|b]] '''c'''" -> "b c". Exposed for tests. */
void otd_Plain(const char* in, size_t n, char* out, size_t size);

#endif
