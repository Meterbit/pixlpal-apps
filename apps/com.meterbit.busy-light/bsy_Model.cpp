// Busy Light's model: settings, statuses, Teams presence, the calendar and the decision. No Arduino.
#include "bsy_Model.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

ArduinoJson::Allocator* bsy_Json_Allocator(void);

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void bsy_Settings_Default(Bsy_Settings_t& s){
    memset(&s, 0, sizeof(s));
    s.magic = BSY_SETTINGS_MAGIC;
    s.useCalendar[BSY_GOOGLE] = 1;
    s.useCalendar[BSY_MICROSOFT] = 1;
    s.usePresence = 1;
    s.tentativeBusy = 1;
    s.showNext = 1;
    s.hour24 = 1;
    s.pulse = 1;
    s.workDays = 0x1F;                  // Monday to Friday
    s.workFrom = 9 * 60;
    s.workTo = 17 * 60;
    s.offHours = BSY_OFF_SHOW;
    s.knock = 1;
    s.knockSound = 1;
    s.phoneKnock = 1;
    s.manualMinutes = 60;
    s.manual = BSY_AUTO;
    snprintf(s.customText, sizeof(s.customText), "RECORDING");
    s.customColor = RGB(255, 40, 40);
    snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
    s.notify.on[NTF_NTFY] = 1;          // knocks reach whoever subscribes to the topic
}

void bsy_Settings_Clamp(Bsy_Settings_t& s){
    s.magic = BSY_SETTINGS_MAGIC;
    s.label[sizeof(s.label) - 1] = '\0';
    s.customText[sizeof(s.customText) - 1] = '\0';
    for (int i = 0; i < BSY_PROVIDERS; i++) s.useCalendar[i] = s.useCalendar[i] ? 1 : 0;
    s.usePresence = s.usePresence ? 1 : 0;
    s.allCalendars = s.allCalendars ? 1 : 0;
    s.tentativeBusy = s.tentativeBusy ? 1 : 0;
    s.showTitle = s.showTitle ? 1 : 0;
    s.showNext = s.showNext ? 1 : 0;
    s.hour24 = s.hour24 ? 1 : 0;
    s.pulse = s.pulse ? 1 : 0;
    s.workDays &= 0x7F;
    if (s.workFrom >= 1440) s.workFrom = 9 * 60;
    if (s.workTo > 1440) s.workTo = 17 * 60;
    if (s.offHours > BSY_OFF_DIM) s.offHours = BSY_OFF_SHOW;
    s.knock = s.knock ? 1 : 0;
    s.knockSound = s.knockSound ? 1 : 0;
    s.phoneKnock = s.phoneKnock ? 1 : 0;
    if (s.manualMinutes > 240) s.manualMinutes = 60;
    if (s.manual >= BSY_STATUSES) s.manual = BSY_AUTO;
    if (s.manual == BSY_AUTO) s.manualUntil = 0;
    for (uint8_t i = 0; i < NTF_CHANNELS; i++) s.notify.on[i] = s.notify.on[i] ? 1 : 0;
    s.notify.ntfyServer[sizeof(s.notify.ntfyServer) - 1] = '\0';
    if (!s.notify.ntfyServer[0]) snprintf(s.notify.ntfyServer, sizeof(s.notify.ntfyServer), "ntfy.sh");
    s.notify.ntfyTopic[sizeof(s.notify.ntfyTopic) - 1] = '\0';
    s.notify.ntfyToken[sizeof(s.notify.ntfyToken) - 1] = '\0';
    s.notify.telegramToken[sizeof(s.notify.telegramToken) - 1] = '\0';
    s.notify.telegramChat[sizeof(s.notify.telegramChat) - 1] = '\0';
    s.notify.whatsappPhone[sizeof(s.notify.whatsappPhone) - 1] = '\0';
    s.notify.whatsappKey[sizeof(s.notify.whatsappKey) - 1] = '\0';
}

int bsy_Severity(uint8_t status){
    switch (status){
        case BSY_FREE: return 1;
        case BSY_AWAY: case BSY_BRB: return 2;
        case BSY_BUSY: case BSY_DND: case BSY_FOCUS: case BSY_CUSTOM: return 3;
        case BSY_MEETING: case BSY_CALL: return 4;
        case BSY_PRESENTING: case BSY_ON_AIR: return 5;
        default: return 0;
    }
}

const char* bsy_Status_Text(uint8_t status){
    switch (status){
        case BSY_FREE: return "FREE";
        case BSY_BUSY: return "BUSY";
        case BSY_MEETING: return "IN A MEETING";
        case BSY_CALL: return "ON A CALL";
        case BSY_PRESENTING: return "PRESENTING";
        case BSY_ON_AIR: return "ON AIR";
        case BSY_DND: return "DO NOT DISTURB";
        case BSY_FOCUS: return "FOCUSING";
        case BSY_AWAY: return "AWAY";
        case BSY_BRB: return "BACK SOON";
        case BSY_OFF_WORK: return "OFF WORK";
        case BSY_OFFLINE: return "OFFLINE";
        default: return "";
    }
}

const char* bsy_Status_Name(uint8_t status){
    switch (status){
        case BSY_AUTO: return "Automatic";
        case BSY_FREE: return "Free";
        case BSY_BUSY: return "Busy";
        case BSY_MEETING: return "In a meeting";
        case BSY_CALL: return "On a call";
        case BSY_PRESENTING: return "Presenting";
        case BSY_ON_AIR: return "On air";
        case BSY_DND: return "Do not disturb";
        case BSY_FOCUS: return "Focusing";
        case BSY_AWAY: return "Away";
        case BSY_BRB: return "Back soon";
        case BSY_OFF_WORK: return "Off work";
        case BSY_OFFLINE: return "Offline";
        case BSY_CUSTOM: return "Your own text";
        default: return "";
    }
}

uint16_t bsy_Status_Color(uint8_t status, uint16_t customColor){
    switch (status){
        case BSY_FREE: return RGB(30, 220, 80);
        case BSY_BUSY: case BSY_MEETING: case BSY_CALL: return RGB(240, 30, 30);
        case BSY_PRESENTING: return RGB(255, 70, 0);
        case BSY_ON_AIR: return RGB(255, 0, 0);
        case BSY_DND: return RGB(200, 0, 140);
        case BSY_FOCUS: return RGB(150, 70, 255);
        case BSY_AWAY: case BSY_BRB: return RGB(255, 170, 0);
        case BSY_OFF_WORK: return RGB(70, 110, 200);
        case BSY_OFFLINE: return RGB(110, 110, 110);
        case BSY_CUSTOM: return customColor ? customColor : RGB(255, 40, 40);
        default: return RGB(110, 110, 110);
    }
}

static bool eq(const char* a, const char* b){ return a && b && !strcmp(a, b); }

uint8_t bsy_Presence_Status(const char* availability, const char* activity){
    // The activity is the more precise; the availability decides when it says nothing more.
    if (eq(activity, "Presenting")) return BSY_PRESENTING;
    if (eq(activity, "InACall") || eq(activity, "InAConferenceCall")) return BSY_CALL;
    if (eq(activity, "InAMeeting")) return BSY_MEETING;
    if (eq(activity, "Focusing")) return BSY_FOCUS;
    if (eq(activity, "DoNotDisturb") || eq(activity, "UrgentInterruptionsOnly")) return BSY_DND;
    if (eq(activity, "BeRightBack")) return BSY_BRB;
    if (eq(activity, "Away") || eq(activity, "Inactive") || eq(activity, "OutOfOffice")) return BSY_AWAY;
    if (eq(activity, "OffWork")) return BSY_OFF_WORK;
    if (eq(availability, "DoNotDisturb")) return BSY_DND;
    if (eq(availability, "Busy") || eq(availability, "BusyIdle")) return BSY_BUSY;
    if (eq(availability, "BeRightBack")) return BSY_BRB;
    if (eq(availability, "Away")) return BSY_AWAY;
    if (eq(availability, "Available") || eq(availability, "AvailableIdle")) return BSY_FREE;
    return BSY_AUTO;                    // Offline, PresenceUnknown
}

int bsy_Parse_Presence(const char* body, size_t length, int httpCode, uint32_t now, Bsy_Presence_t& out, char* why, size_t whySize){
    if (whySize) why[0] = '\0';
    JsonDocument doc(bsy_Json_Allocator());
    const bool parsed = body && length && !deserializeJson(doc, body, length);
    if (httpCode == 200 && parsed && doc["availability"].is<const char*>()){
        snprintf(out.availability, sizeof(out.availability), "%s", doc["availability"] | "");
        snprintf(out.activity, sizeof(out.activity), "%s", doc["activity"] | "");
        out.at = now;
        out.known = true;
        return 1;
    }
    const char* code = parsed ? (doc["error"]["code"] | "") : "";
    const char* message = parsed ? (doc["error"]["message"] | "") : "";
    // Personal accounts (and tenants without Teams) answer 4xx: there is no presence to read.
    if (httpCode == 403 || httpCode == 404 || (httpCode == 400 && strstr(message, "personal")) || strstr(message, "MSA")){
        snprintf(why, whySize, "Teams status needs a work or school account");
        return 0;
    }
    if (httpCode == 401 || strstr(code, "Authorization")){ snprintf(why, whySize, "Link Microsoft again to allow Teams status (Presence.Read)"); return 0; }
    snprintf(why, whySize, "Teams status: %s", message[0] ? message : code[0] ? code : httpCode > 0 ? "no answer" : "no connection");
    if (httpCode > 0 && whySize > 24 && !message[0] && !code[0]) snprintf(why, whySize, "Teams status: HTTP %d", httpCode);
    return -1;
}

bool bsy_Event_Busy(const Bsy_Event_t& e, bool tentativeBusy){
    if (e.allDay || e.end <= e.start) return false;
    if (e.flags & (BSY_EV_FREE | BSY_EV_DECLINED | BSY_EV_HOLIDAY | BSY_EV_BIRTHDAY)) return false;
    if ((e.flags & BSY_EV_TENTATIVE) && !tentativeBusy) return false;
    return true;
}

static bool starts_With(const char* text, const char* word){
    for (; *word; word++, text++) if (tolower((unsigned char)*text) != tolower((unsigned char)*word)) return false;
    return true;
}

uint8_t bsy_Event_Status(const Bsy_Event_t& e){
    if (starts_With(e.title, "focus time") || starts_With(e.title, "focus")) return BSY_FOCUS;
    if (starts_With(e.title, "out of office") || starts_With(e.title, "ooo")) return BSY_AWAY;
    return BSY_MEETING;
}

bool bsy_Add_Event(Bsy_Calendar_t& c, const Bsy_Event_t& e){
    for (int i = 0; i < c.count; i++) if (c.e[i].start == e.start && !strcmp(c.e[i].title, e.title)) return false;
    if (c.count >= BSY_EVENTS){
        if (e.start >= c.e[BSY_EVENTS - 1].start) return false;
        c.count--;                       // a later one gives way
    }
    int pos = c.count;
    while (pos > 0 && c.e[pos - 1].start > e.start){ c.e[pos] = c.e[pos - 1]; pos--; }
    c.e[pos] = e;
    c.count++;
    return true;
}

bool bsy_Working(const Bsy_Settings_t& s, int weekday, int minute){
    if (!s.workDays) return true;
    if (!(s.workDays & (1u << weekday))) return false;
    if (s.workFrom <= s.workTo) return minute >= s.workFrom && minute < s.workTo;
    return minute >= s.workFrom || minute < s.workTo;       // a night shift across midnight
}

void bsy_Decide(const Bsy_Settings_t& s, uint32_t now, int weekday, int minute, const Bsy_Calendar_t* cal,
                const Bsy_Presence_t* presence, Bsy_State_t& out){
    memset(&out, 0, sizeof(out));
    // The calendar: the meeting now (back-to-back ones joined), and the next one.
    uint8_t calStatus = BSY_AUTO;
    int current = -1;
    if (cal && cal->known){
        out.calendarKnown = true;
        for (int i = 0; i < cal->count; i++){
            const Bsy_Event_t& e = cal->e[i];
            if (!bsy_Event_Busy(e, s.tentativeBusy) || e.start > now || e.end <= now) continue;
            const uint8_t st = bsy_Event_Status(e);
            if (current < 0 || bsy_Severity(st) > bsy_Severity(calStatus) || (bsy_Severity(st) == bsy_Severity(calStatus) && e.start >= cal->e[current].start)){
                calStatus = st;
                current = i;
            }
        }
        uint32_t until = 0;
        if (current >= 0){
            until = cal->e[current].end;
            for (int i = 0; i < cal->count; i++){
                const Bsy_Event_t& e = cal->e[i];
                if (bsy_Event_Busy(e, s.tentativeBusy) && e.start <= now && e.end > until) until = e.end;
            }
            for (bool grew = true; grew;){
                grew = false;
                for (int i = 0; i < cal->count; i++){
                    const Bsy_Event_t& e = cal->e[i];
                    if (bsy_Event_Busy(e, s.tentativeBusy) && e.start <= until + 120 && e.end > until){ until = e.end; grew = true; }
                }
            }
        }
        const uint32_t after = current >= 0 ? until : now;
        for (int i = 0; i < cal->count; i++){
            const Bsy_Event_t& e = cal->e[i];
            if (!bsy_Event_Busy(e, s.tentativeBusy) || e.start < after || (current < 0 && e.start <= now)) continue;
            out.hasNext = true;
            out.nextStart = e.start;
            snprintf(out.nextTitle, sizeof(out.nextTitle), "%s", e.title);
            break;
        }
        if (current >= 0){
            out.start = cal->e[current].start;
            out.until = until;
            snprintf(out.title, sizeof(out.title), "%s", cal->e[current].title);
        }
    }

    // A status from the phone wins while it lasts.
    if (s.manual != BSY_AUTO && (!s.manualUntil || now < s.manualUntil)){
        out.status = s.manual;
        out.from = BSY_FROM_PHONE;
        out.start = 0;
        out.until = s.manualUntil;
        out.title[0] = '\0';
        return;
    }

    // Teams, while its answer is fresh; the busier of it and the calendar.
    uint8_t teams = BSY_AUTO;
    if (presence && presence->known && now - presence->at <= BSY_PRESENCE_FRESH)
        teams = bsy_Presence_Status(presence->availability, presence->activity);
    if (teams != BSY_AUTO && bsy_Severity(teams) >= bsy_Severity(calStatus)){
        out.status = teams;
        out.from = BSY_FROM_TEAMS;
        if (calStatus == BSY_AUTO || bsy_Severity(teams) < 3){ out.start = out.until = 0; out.title[0] = '\0'; }
        // Teams says available, but outside working hours: the hours decide.
        if (teams != BSY_FREE || bsy_Working(s, weekday, minute)) return;
    } else if (calStatus != BSY_AUTO){
        out.status = calStatus;
        out.from = BSY_FROM_CALENDAR;
        return;
    }
    out.start = out.until = 0;
    out.title[0] = '\0';
    out.from = BSY_FROM_HOURS;
    if (bsy_Working(s, weekday, minute)) out.status = BSY_FREE;
    else out.status = s.offHours == BSY_OFF_FREE ? BSY_FREE : BSY_OFF_WORK;
}

void bsy_Clock(char* dest, size_t size, uint32_t t, int32_t utcOffset, bool hour24){
    const int64_t local = (int64_t)t + utcOffset;
    const int s = (int)(((local % 86400) + 86400) % 86400);
    const int h = s / 3600, m = (s / 60) % 60;
    if (hour24) snprintf(dest, size, "%02d:%02d", h, m);
    else snprintf(dest, size, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}

void bsy_Span(char* dest, size_t size, uint32_t seconds){
    const uint32_t minutes = (seconds + 59) / 60;
    if (minutes < 60) snprintf(dest, size, "%lu MIN", (unsigned long)minutes);
    else if (minutes % 60 == 0) snprintf(dest, size, "%lu H", (unsigned long)(minutes / 60));
    else snprintf(dest, size, "%lu H %02lu", (unsigned long)(minutes / 60), (unsigned long)(minutes % 60));
}

static const char* const DAYS[] = {"MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};

void bsy_Detail(char* dest, size_t size, const Bsy_State_t& st, const Bsy_Settings_t& s, uint32_t now, int32_t utcOffset,
                int weekday, int minute){
    if (!size) return;
    dest[0] = '\0';
    char a[24], b[24];
    if (st.until > now){
        bsy_Clock(a, sizeof(a), st.until, utcOffset, s.hour24);
        bsy_Span(b, sizeof(b), st.until - now);
        snprintf(dest, size, "UNTIL %s, %s LEFT", a, b);
        return;
    }
    if (st.status == BSY_OFF_WORK && s.workDays){
        // The next working start.
        for (int k = 0; k <= 7; k++){
            const int day = (weekday + k) % 7;
            if (!(s.workDays & (1u << day))) continue;
            if (k == 0 && minute >= s.workFrom) continue;
            const uint32_t at = now + (uint32_t)((k * 1440 + s.workFrom - minute) * 60);
            bsy_Clock(a, sizeof(a), at, utcOffset, s.hour24);
            if (k == 0) snprintf(dest, size, "BACK AT %s", a);
            else if (k == 1) snprintf(dest, size, "BACK TOMORROW AT %s", a);
            else snprintf(dest, size, "BACK %s AT %s", DAYS[day], a);
            return;
        }
    }
    if (st.status == BSY_FREE && s.showNext){
        // The next meeting today, if any.
        const int left = 1440 - minute;
        if (st.hasNext && st.nextStart > now && st.nextStart - now < (uint32_t)left * 60){
            bsy_Clock(a, sizeof(a), st.nextStart, utcOffset, s.hour24);
            const uint32_t in = st.nextStart - now;
            if (in <= 3600){ bsy_Span(b, sizeof(b), in); snprintf(dest, size, "NEXT MEETING IN %s", b); }
            else snprintf(dest, size, "NEXT MEETING AT %s", a);
        } else if (st.calendarKnown) snprintf(dest, size, "NO MORE MEETINGS TODAY");
    }
}

const char* bsy_Reply_Text(uint8_t reply){
    switch (reply){
        case BSY_REPLY_COME_IN: return "COME IN";
        case BSY_REPLY_MINUTE: return "ONE MINUTE";
        case BSY_REPLY_FIVE: return "FIVE MINUTES";
        case BSY_REPLY_NOT_NOW: return "NOT NOW, SORRY";
        default: return "";
    }
}

uint16_t bsy_Reply_Color(uint8_t reply){
    switch (reply){
        case BSY_REPLY_COME_IN: return RGB(30, 220, 80);
        case BSY_REPLY_MINUTE: case BSY_REPLY_FIVE: return RGB(255, 170, 0);
        case BSY_REPLY_NOT_NOW: return RGB(240, 30, 30);
        default: return RGB(80, 200, 255);
    }
}
