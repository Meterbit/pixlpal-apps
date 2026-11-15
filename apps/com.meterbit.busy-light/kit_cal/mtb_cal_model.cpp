#include "mtb_cal_model.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define CAL_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const char* const DAYS[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void copy(char* d, size_t n, const char* s){
    if (n == 0) return;
    size_t k = 0;
    if (s) while (s[k] && k + 1 < n) k++;
    memmove(d, s ? s : "", k);
    d[k] = '\0';
}

// Settings ------------------------------------------------------------------------------------------------------
void mtb_Cal_Settings_Default(Mtb_Cal_Settings_t& s, uint16_t accent){
    memset(&s, 0, sizeof(s));
    s.magic = MTB_CAL_SETTINGS_MAGIC;
    s.view = MTB_CAL_VIEW_NEXT;
    s.daysAhead = 7;
    s.hour24 = 1;
    s.showTasks = 1;
    s.remindMinutes = 10;
    s.chime = 1;
    s.refreshMinutes = 10;
    s.showMail = 1;
    s.accent = accent;
}

void mtb_Cal_Settings_Clamp(Mtb_Cal_Settings_t& s){
    if (s.view >= MTB_CAL_VIEW_COUNT) s.view = MTB_CAL_VIEW_NEXT;
    if (s.daysAhead < 1) s.daysAhead = 1;
    if (s.daysAhead > 30) s.daysAhead = 30;
    s.hour24 = s.hour24 ? 1 : 0;
    s.showTasks = s.showTasks ? 1 : 0;
    s.showDeclined = s.showDeclined ? 1 : 0;
    if (s.remindMinutes > 60) s.remindMinutes = 60;
    s.chime = s.chime ? 1 : 0;
    if (s.refreshMinutes < 5) s.refreshMinutes = 5;
    if (s.refreshMinutes > 60) s.refreshMinutes = 60;
    s.showMail = s.showMail ? 1 : 0;
    s.weekStart = s.weekStart ? 1 : 0;
    if (s.calendarCount > MTB_CAL_PICKS) s.calendarCount = MTB_CAL_PICKS;
    for (int i = 0; i < MTB_CAL_PICKS; i++){
        s.calendars[i].id[MTB_CAL_ID - 1] = s.calendars[i].name[MTB_CAL_NAME - 1] = '\0';
        if (i < s.calendarCount && s.calendars[i].id[0] == '\0'){       // an empty pick: drop it
            memmove(&s.calendars[i], &s.calendars[i + 1], sizeof(Mtb_Cal_Pick_t) * (MTB_CAL_PICKS - i - 1));
            memset(&s.calendars[MTB_CAL_PICKS - 1], 0, sizeof(Mtb_Cal_Pick_t));
            s.calendarCount--;
            i--;
        }
    }
    s.taskList.id[MTB_CAL_ID - 1] = s.taskList.name[MTB_CAL_NAME - 1] = '\0';
    s.holidays[sizeof(s.holidays) - 1] = s.holidayName[sizeof(s.holidayName) - 1] = '\0';
    for (char* c = s.holidays; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (s.holidays[0] && strlen(s.holidays) != 2) s.holidays[0] = '\0';
}

// Time ----------------------------------------------------------------------------------------------------------
static int32_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void civil_From_Days(int32_t z, int& y, int& m, int& d){
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)yoe + era * 400 + (m <= 2);
}

int32_t mtb_Cal_Local_Day(uint32_t t){
    const time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    return days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

uint32_t mtb_Cal_Day_Start(int32_t day){
    int y, m, d;
    civil_From_Days(day, y, m, d);
    struct tm lt = {};
    lt.tm_year = y - 1900;
    lt.tm_mon = m - 1;
    lt.tm_mday = d;
    lt.tm_isdst = -1;
    const time_t t = mktime(&lt);
    return t > 0 ? (uint32_t)t : 0;
}

bool mtb_Cal_Parse_Time(const char* text, uint32_t& t, bool& dateOnly){
    t = 0;
    dateOnly = false;
    if (text == nullptr) return false;
    int y, mo, d;
    if (sscanf(text, "%4d-%2d-%2d", &y, &mo, &d) != 3 || y < 1971 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    const char* p = text + 10;
    if (*p != 'T' && *p != ' '){
        dateOnly = true;
        t = mtb_Cal_Day_Start(days_From_Civil(y, mo, d));
        return t != 0;
    }
    int h, mi, s = 0;
    if (sscanf(p + 1, "%2d:%2d", &h, &mi) != 2 || h > 24 || mi > 59) return false;
    p += 6;
    if (*p == ':'){ s = atoi(p + 1); p += 3; }
    while (*p == '.' || isdigit((unsigned char)*p)) p++;
    int32_t offset = 0;                     // "Z", or no zone at all (asked for in UTC)
    if (*p == '+' || *p == '-'){
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) < 1 && sscanf(p + 1, "%2d%2d", &oh, &om) < 1) return false;
        offset = (oh * 3600 + om * 60) * (*p == '+' ? 1 : -1);
    }
    const int64_t unix = (int64_t)days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - offset;
    if (unix <= 0 || unix > 0xFFFFFFFFLL) return false;
    t = (uint32_t)unix;
    return true;
}

void mtb_Cal_Clock(char* dest, size_t size, uint32_t t, bool hour24){
    const time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (hour24) snprintf(dest, size, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(dest, size, "%d:%02d %s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

void mtb_Cal_Day_Label(char* dest, size_t size, int32_t day, int32_t today){
    if (day == today) { copy(dest, size, "TODAY"); return; }
    if (day == today + 1) { copy(dest, size, "TOMORROW"); return; }
    if (day == today - 1) { copy(dest, size, "YESTERDAY"); return; }
    int y, m, d;
    civil_From_Days(day, y, m, d);
    const int weekday = (int)(((day % 7) + 7 + 3) % 7);    // 1970-01-01 was a Thursday (Monday = 0)
    snprintf(dest, size, "%s %d %s", DAYS[weekday], d, MONTHS[m - 1]);
}

void mtb_Cal_Span(char* dest, size_t size, uint32_t seconds){
    const uint32_t minutes = (seconds + 59) / 60;
    if (minutes < 60) snprintf(dest, size, "%u MIN", (unsigned)minutes);
    else if (minutes < 24 * 60){
        if (minutes % 60) snprintf(dest, size, "%u H %u MIN", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
        else snprintf(dest, size, "%u H", (unsigned)(minutes / 60));
    } else {
        const uint32_t hours = (minutes / 60) % 24;
        if (hours) snprintf(dest, size, "%u D %u H", (unsigned)(minutes / 1440), (unsigned)hours);
        else snprintf(dest, size, "%u D", (unsigned)(minutes / 1440));
    }
}

// Events --------------------------------------------------------------------------------------------------------
uint32_t mtb_Cal_Key(const char* title, uint32_t start){
    uint32_t h = 2166136261u ^ start;
    for (const unsigned char* c = (const unsigned char*)(title ? title : ""); *c; c++){ h ^= *c; h *= 16777619u; }
    h ^= start >> 16;
    h *= 16777619u;
    return h ? h : 1;
}

// a before b: by start; on the same local day all-day events come first.
static bool before(const Mtb_Cal_Event_t& a, const Mtb_Cal_Event_t& b){
    if (a.allDay != b.allDay && mtb_Cal_Local_Day(a.start) == mtb_Cal_Local_Day(b.start)) return a.allDay;
    if (a.start != b.start) return a.start < b.start;
    return a.end < b.end;
}

bool mtb_Cal_Add_Event(Mtb_Cal_Data_t& data, const Mtb_Cal_Event_t& event){
    if (event.end < event.start) return false;
    for (int i = 0; i < data.eventCount; i++) if (data.events[i].key == event.key) return false;
    int at = data.eventCount;
    while (at > 0 && before(event, data.events[at - 1])) at--;
    if (data.eventCount >= MTB_CAL_EVENTS){
        if (at >= MTB_CAL_EVENTS) return false;          // later than everything kept
        data.eventCount = MTB_CAL_EVENTS - 1;            // the latest gives way
    }
    memmove(&data.events[at + 1], &data.events[at], sizeof(Mtb_Cal_Event_t) * (data.eventCount - at));
    data.events[at] = event;
    data.eventCount++;
    return true;
}

bool mtb_Cal_Add_Task(Mtb_Cal_Data_t& data, const Mtb_Cal_Task_t& task){
    if (task.title[0] == '\0') return false;
    int at = data.taskCount;
    if (task.dueDay >= 0) while (at > 0 && (data.tasks[at - 1].dueDay < 0 || data.tasks[at - 1].dueDay > task.dueDay)) at--;
    if (data.taskCount >= MTB_CAL_TASKS){
        if (at >= MTB_CAL_TASKS) return false;
        data.taskCount = MTB_CAL_TASKS - 1;
    }
    memmove(&data.tasks[at + 1], &data.tasks[at], sizeof(Mtb_Cal_Task_t) * (data.taskCount - at));
    data.tasks[at] = task;
    data.taskCount++;
    return true;
}

bool mtb_Cal_Shown(const Mtb_Cal_Event_t& e, const Mtb_Cal_Settings_t& s, uint32_t now){
    if (e.end <= now && !(e.end == e.start && e.start + 60 > now)) return false;
    if ((e.flags & MTB_CAL_DECLINED) && !s.showDeclined) return false;
    const uint32_t horizon = mtb_Cal_Day_Start(mtb_Cal_Local_Day(now) + s.daysAhead);
    return e.start < horizon;
}

int mtb_Cal_Agenda(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now, uint8_t* order, int max){
    int n = 0;
    for (int i = 0; i < data.eventCount && n < max; i++) if (mtb_Cal_Shown(data.events[i], s, now)) order[n++] = (uint8_t)i;
    return n;
}

int mtb_Cal_Next(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now){
    int running = -1, next = -1, allDay = -1;
    for (int i = 0; i < data.eventCount; i++){
        const Mtb_Cal_Event_t& e = data.events[i];
        if (!mtb_Cal_Shown(e, s, now)) continue;
        if (e.allDay){ if (allDay < 0) allDay = i; continue; }
        if (e.start <= now && now < e.end){ if (running < 0 || e.start >= data.events[running].start) running = i; }
        else if (e.start > now && next < 0) next = i;
    }
    return running >= 0 ? running : next >= 0 ? next : allDay;
}

void mtb_Cal_When(char* dest, size_t size, const Mtb_Cal_Event_t& e, uint32_t now, bool hour24){
    const int32_t today = mtb_Cal_Local_Day(now);
    char span[24], day[24], clock[12];
    if (e.allDay){
        const int32_t first = mtb_Cal_Local_Day(e.start);
        const int32_t last = mtb_Cal_Local_Day(e.end > e.start ? e.end - 1 : e.start);
        if (first <= today && last > today){
            mtb_Cal_Day_Label(day, sizeof(day), last, today);
            snprintf(dest, size, "UNTIL %s · ALL DAY", day);
        } else {
            mtb_Cal_Day_Label(day, sizeof(day), first < today ? today : first, today);
            snprintf(dest, size, "%s · ALL DAY", day);
        }
        return;
    }
    if (e.start <= now && now < e.end){
        mtb_Cal_Span(span, sizeof(span), e.end - now);
        snprintf(dest, size, "NOW · %s LEFT", span);
        return;
    }
    if (e.start > now && e.start - now < 3600){
        mtb_Cal_Span(span, sizeof(span), e.start - now);
        snprintf(dest, size, "IN %s", span);
        return;
    }
    mtb_Cal_Day_Label(day, sizeof(day), mtb_Cal_Local_Day(e.start), today);
    mtb_Cal_Clock(clock, sizeof(clock), e.start, hour24);
    snprintf(dest, size, "%s %s", day, clock);
}

void mtb_Cal_Range(char* dest, size_t size, const Mtb_Cal_Event_t& e, bool hour24){
    if (e.allDay){
        const int32_t days = mtb_Cal_Local_Day(e.end > e.start ? e.end - 1 : e.start) - mtb_Cal_Local_Day(e.start) + 1;
        if (days > 1) snprintf(dest, size, "ALL DAY · %d DAYS", (int)days);
        else copy(dest, size, "ALL DAY");
        return;
    }
    char a[12], b[12];
    mtb_Cal_Clock(a, sizeof(a), e.start, hour24);
    mtb_Cal_Clock(b, sizeof(b), e.end, hour24);
    if (e.end == e.start) copy(dest, size, a);
    else snprintf(dest, size, "%s - %s", a, b);
}

float mtb_Cal_Progress(const Mtb_Cal_Event_t& e, uint32_t now){
    if (e.end <= e.start || now < e.start || now >= e.end) return 0.0f;
    return (float)(now - e.start) / (float)(e.end - e.start);
}

int mtb_Cal_Reminder(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, uint32_t now, uint32_t lastKey){
    if (s.remindMinutes == 0) return -1;
    for (int i = 0; i < data.eventCount; i++){
        const Mtb_Cal_Event_t& e = data.events[i];
        if (e.allDay || (e.flags & (MTB_CAL_DECLINED | MTB_CAL_FREE)) || e.key == lastKey) continue;
        if (e.start > now && e.start - now <= (uint32_t)s.remindMinutes * 60) return i;
    }
    return -1;
}

int mtb_Cal_Count_On(const Mtb_Cal_Data_t& data, const Mtb_Cal_Settings_t& s, int32_t day, uint32_t now){
    const uint32_t a = mtb_Cal_Day_Start(day), b = mtb_Cal_Day_Start(day + 1);
    int n = 0;
    for (int i = 0; i < data.eventCount; i++){
        const Mtb_Cal_Event_t& e = data.events[i];
        if ((e.flags & MTB_CAL_DECLINED) && !s.showDeclined) continue;
        if (e.start < b && (e.end > a || (e.end == e.start && e.start >= a))) n++;
    }
    (void)now;
    return n;
}

// Tasks ---------------------------------------------------------------------------------------------------------
void mtb_Cal_Due_Label(char* dest, size_t size, const Mtb_Cal_Task_t& t, int32_t today){
    if (t.dueDay < 0){ copy(dest, size, ""); return; }
    if (t.dueDay < today){ snprintf(dest, size, "OVERDUE %d D", (int)(today - t.dueDay)); return; }
    mtb_Cal_Day_Label(dest, size, t.dueDay, today);
}

int mtb_Cal_Overdue(const Mtb_Cal_Data_t& data, int32_t today){
    int n = 0;
    for (int i = 0; i < data.taskCount; i++) if (data.tasks[i].dueDay >= 0 && data.tasks[i].dueDay < today) n++;
    return n;
}

// Colours -------------------------------------------------------------------------------------------------------
uint16_t mtb_Cal_Color(const char* hex, int index){
    static const uint16_t PALETTE[] = {CAL_RGB(66, 133, 244), CAL_RGB(52, 168, 83), CAL_RGB(251, 188, 4), CAL_RGB(234, 67, 53),
                                       CAL_RGB(171, 71, 188), CAL_RGB(0, 172, 193), CAL_RGB(255, 112, 67), CAL_RGB(158, 157, 36)};
    if (hex && hex[0] == '#' && strlen(hex) == 7){
        bool ok = true;
        for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)hex[i])) ok = false;
        if (ok){
            const unsigned long v = strtoul(hex + 1, nullptr, 16);
            int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
            const int brightest = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (brightest < 120){                       // too dark on black: lift, keeping the hue
                const int add = 120 - brightest;
                r += add; g += add; b += add;
            }
            return CAL_RGB(r, g, b);
        }
    }
    if (index < 0) index = 0;
    return PALETTE[index % (int)(sizeof(PALETTE) / sizeof(PALETTE[0]))];
}
