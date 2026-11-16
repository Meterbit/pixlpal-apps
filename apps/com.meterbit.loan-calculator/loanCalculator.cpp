/**
 * Loan & Savings Calculator: a calculator customers use with the knob while they wait in a bank branch.
 *
 * Loans: choose one of the bank's loans (its name and rate), an amount and a term, and see the monthly payment (equal
 * payments, interest monthly at a twelfth of the yearly rate), the interest over the term, the total to repay, and a
 * bar of how much of it is the loan and how much interest. Savings: a monthly deposit and a term, and what it grows
 * to at the bank's savings rate (each deposit at the month's end, interest added monthly).
 *
 * The knob: turn to change the field in the box, click for the next field, double click to switch between loans and
 * savings. Two minutes after the last turn it goes back to the start for the next customer. The bank sets its loans,
 * savings rate, currency and the note along the bottom on the phone. NVS "lncSave".
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "esp_heap_caps.h"
#include "lnc_Mobile_Ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#define W           256
#define H           128
#define LOANS       3
#define NVS_KEY     "lncSave"
#define SAVE_MAGIC  0x4C4E4301u
#define IDLE_MS     120000u
#define MAX_AMOUNT  999999999u

EXT_RAM_BSS_ATTR TaskHandle_t loanCalculator_Task_H = NULL;
static void loanCalculator_App_Task(void* dApplication);
static void lnc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* loanCalculator_App;
Mtb_Applications* loanCalculator_App_GetInstance(){
    if (!loanCalculator_App){
        loanCalculator_App = new Mtb_Applications_FullScreen(loanCalculator_App_Task, &loanCalculator_Task_H, "Loan Calculator", {0,0}, 8192);
        loanCalculator_App->mtb_App_Set_Mobile_UI(LNC_UI_MANIFEST, LNC_UI_API, nullptr, lnc_Values);
    }
    return loanCalculator_App;
}
MTB_REGISTER_APP(loanCalculator_App, 0, 0)    // the catalogue gives the app its number

struct Lnc_Loan_t { char name[22]; uint16_t rateBp; uint32_t maxAmount; uint16_t maxMonths; };     // rateBp: 1850 = 18.50% a year

struct Lnc_Save_t {
    uint32_t magic;
    char bank[36];
    char currency[6];
    Lnc_Loan_t loan[LOANS];
    uint8_t loans;
    uint16_t savingsBp;
    uint32_t savingsMax;
    char note[80];
};

/** What the customer has chosen (not kept: it starts again for each customer). */
struct Lnc_Calc_t { uint8_t savings, field, product; uint32_t amount, deposit; uint16_t months, saveMonths; };

static EXT_RAM_BSS_ATTR Lnc_Save_t save;
static Lnc_Calc_t calc;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Lnc_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){
        memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC;
        s.loans = 2;
        s.loan[0] = {"Personal loan", 1500, 50000, 60};
        s.loan[1] = {"Car loan", 1200, 200000, 72};
        s.savingsBp = 450; s.savingsMax = 5000;
        snprintf(s.note, sizeof(s.note), "Illustration only: ask us for a quote.");
    }
    if (s.loans < 1 || s.loans > LOANS){ s.loans = 1; s.loan[0] = {"Loan", 1500, 50000, 60}; }
    for (int i = 0; i < LOANS; i++){
        s.loan[i].name[sizeof(s.loan[i].name) - 1] = '\0';
        if (i < s.loans && (s.loan[i].maxAmount < 10 || s.loan[i].maxMonths < 1)){ s.loan[i].maxAmount = 50000; s.loan[i].maxMonths = 60; }
    }
    if (s.savingsMax < 10) s.savingsMax = 5000;
    s.bank[sizeof(s.bank) - 1] = s.currency[sizeof(s.currency) - 1] = s.note[sizeof(s.note) - 1] = '\0';
}

// Steps --------------------------------------------------------------------------------------------------------------------------
/** A round step for amounts up to @p max: about a hundredth of it, as 1, 2 or 5 times a power of ten. */
static uint32_t amount_Step(uint32_t max){
    double x = max / 100.0;
    if (x < 1) return 1;
    double p = pow(10, floor(log10(x)));
    const double m = x / p;
    return (uint32_t)((m < 2 ? 1 : m < 5 ? 2 : 5) * p);
}
static uint32_t round_To(uint32_t v, uint32_t step){ return step ? (v + step / 2) / step * step : v; }

/** The next term up or down: by the month to a year, by three months to five years, then by the year. */
static uint16_t month_Step(uint16_t m, int dir, uint16_t max){
    int v = m;
    if (dir > 0) v = v < 12 ? v + 1 : v < 60 ? v + 3 : v + 12;
    else v = v <= 12 ? v - 1 : v <= 60 ? v - 3 : v - 12;
    if (v > 12 && v < 60) v = (v + 1) / 3 * 3;
    if (v > 60) v = (v + 6) / 12 * 12;
    if (v < 1) v = 1;
    if (v > max) v = max;
    return (uint16_t)v;
}

/** The start, for the next customer. */
static void calc_Reset(Lnc_Calc_t& c, const Lnc_Save_t& s, bool keepMode){
    const uint8_t mode = keepMode ? c.savings : 0;
    memset(&c, 0, sizeof(c));
    c.savings = mode;
    const Lnc_Loan_t& l = s.loan[0];
    c.amount = round_To(l.maxAmount / 5, amount_Step(l.maxAmount));
    if (c.amount < amount_Step(l.maxAmount)) c.amount = amount_Step(l.maxAmount);
    c.months = l.maxMonths < 24 ? l.maxMonths : 24;
    c.deposit = round_To(s.savingsMax / 5, amount_Step(s.savingsMax));
    if (c.deposit < amount_Step(s.savingsMax)) c.deposit = amount_Step(s.savingsMax);
    c.saveMonths = 24;
}

/** A product chosen: the amount and term kept within it. */
static void calc_Fit(Lnc_Calc_t& c, const Lnc_Save_t& s){
    if (c.product >= s.loans) c.product = 0;
    const Lnc_Loan_t& l = s.loan[c.product];
    const uint32_t st = amount_Step(l.maxAmount);
    if (c.amount > l.maxAmount) c.amount = l.maxAmount;
    if (c.amount < st) c.amount = st;
    if (c.months > l.maxMonths) c.months = l.maxMonths;
    if (c.months < 1) c.months = 1;
    const uint32_t ss = amount_Step(s.savingsMax);
    if (c.deposit > s.savingsMax) c.deposit = s.savingsMax;
    if (c.deposit < ss) c.deposit = ss;
    if (c.saveMonths < 1) c.saveMonths = 1;
    if (c.saveMonths > 120) c.saveMonths = 120;
}

// The sums -------------------------------------------------------------------------------------------------------------------------------
struct Lnc_Result_t { double monthly, interest, total; };

static Lnc_Result_t loan_Result(uint32_t amount, uint16_t rateBp, uint16_t months){
    const double r = rateBp / 10000.0 / 12.0, p = amount;
    Lnc_Result_t x;
    x.monthly = r > 0 ? p * r / (1 - pow(1 + r, -(double)months)) : p / months;
    x.total = x.monthly * months;
    x.interest = x.total - p;
    return x;
}

/** Savings: @c total what it grows to, @c interest what of that is interest, @c monthly what was put in. */
static Lnc_Result_t savings_Result(uint32_t deposit, uint16_t rateBp, uint16_t months){
    const double r = rateBp / 10000.0 / 12.0, d = deposit;
    Lnc_Result_t x;
    x.total = r > 0 ? d * (pow(1 + r, (double)months) - 1) / r : d * months;
    x.monthly = d * months;
    x.interest = x.total - x.monthly;
    return x;
}

/** "1,250,000"; under a thousand with its cents ("845.20"). */
static void number_Text(double v, char* out, size_t n){
    if (v < 0) v = 0;
    if (v < 1000){ snprintf(out, n, "%.2f", v); return; }
    char d[24]; snprintf(d, sizeof(d), "%.0f", v);
    const size_t len = strlen(d);
    size_t k = 0;
    for (size_t i = 0; i < len && k + 1 < n; i++){
        out[k++] = d[i];
        const size_t left = len - 1 - i;
        if (left && left % 3 == 0 && k + 1 < n) out[k++] = ',';
    }
    out[k] = '\0';
}

/** The amount with the bank's currency: "KSh 1,250,000", "$1,250". */
static void money_Text(const char* currency, double v, char* out, size_t n){
    char num[32]; number_Text(v, num, sizeof(num));
    const size_t cl = strlen(currency);
    if (!cl) snprintf(out, n, "%s", num);
    else if (cl == 1 && !isalpha((unsigned char)currency[0])) snprintf(out, n, "%s%s", currency, num);
    else snprintf(out, n, "%s %s", currency, num);
}

static void rate_Text(uint16_t bp, char* out, size_t n){
    if (bp % 100 == 0) snprintf(out, n, "%u%%", (unsigned)(bp / 100));
    else if (bp % 10 == 0) snprintf(out, n, "%u.%u%%", (unsigned)(bp / 100), (unsigned)(bp / 10 % 10));
    else snprintf(out, n, "%u.%02u%%", (unsigned)(bp / 100), (unsigned)(bp % 100));
}

static void months_Text(uint16_t m, char* out, size_t n){
    if (m % 12 == 0) snprintf(out, n, "%u MONTHS (%u YR%s)", (unsigned)m, (unsigned)(m / 12), m == 12 ? "" : "S");
    else snprintf(out, n, "%u MONTH%s", (unsigned)m, m == 1 ? "" : "S");
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void trim(char*& p){
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = '\0';
}

/** "18.5%" or "18.5": basis points (up to 99.99%). */
static bool rate_Bp(const char* t, uint16_t& bp){
    char b[16]; size_t k = 0;
    for (const char* p = t; *p && k < sizeof(b) - 1; p++) if (*p != '%') b[k++] = *p;
    b[k] = '\0';
    char* e; const double v = strtod(b, &e);
    if (e == b || *e || !(v >= 0) || v >= 100) return false;
    bp = (uint16_t)lround(v * 100);
    return true;
}

/** "5,000,000": a whole amount. */
static bool amount_Value(const char* t, uint32_t& v){
    uint64_t x = 0; int digits = 0;
    for (const char* p = t; *p; p++){
        if (*p == ',') continue;
        if (!isdigit((unsigned char)*p)) return false;
        x = x * 10 + (uint64_t)(*p - '0');
        if (++digits > 9) return false;
    }
    if (!digits || x < 10 || x > MAX_AMOUNT) return false;
    v = (uint32_t)x;
    return true;
}

/** Splits off the last word of @p p (its end trimmed): the word, or nullptr if there is no word before it. */
static char* last_Word(char* p){
    char* sp = strrchr(p, ' ');
    if (!sp) return nullptr;
    *sp = '\0';
    char* w = sp + 1;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = '\0';
    return w;
}

static void cmd_Bank(JsonDocument& c){
    const char* v = c["bank"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[64]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.bank, sizeof(save.bank), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Currency(JsonDocument& c){
    const char* v = c["currency"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[32]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    if (strlen(p) > 5 || strchr(p, ' ')){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock); snprintf(save.currency, sizeof(save.currency), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Loan(JsonDocument& c){
    for (int i = 0; i < LOANS; i++){
        char key[8]; snprintf(key, sizeof(key), "loan%d", i);
        if (c[key].isNull()) continue;
        char t[96]; mtb_Kit_To_Ascii(t, sizeof(t), c[key] | "");
        char* p = t; trim(p);
        Lnc_Loan_t l = {};
        if (*p){
            char* months = last_Word(p);
            char* amount = months ? last_Word(p) : nullptr;
            char* rate = amount ? last_Word(p) : nullptr;
            if (rate && !strcmp(rate, "%")){ rate = last_Word(p); if (rate) strcat(rate, "%"); }    // "18.5 %"
            char* e = nullptr;
            const long m = months ? strtol(months, &e, 10) : 0;
            if (!rate || !*p || !e || *e || m < 1 || m > 480 || !amount_Value(amount, l.maxAmount) || !rate_Bp(rate, l.rateBp)){ mtb_Ble_App_Cmd_Reject(); return; }
            l.maxMonths = (uint16_t)m;
            snprintf(l.name, sizeof(l.name), "%s", p);
        }
        portENTER_CRITICAL(&lock);
        if (!*p){
            if (i >= save.loans){ portEXIT_CRITICAL(&lock); return; }                  // nothing there
            if (save.loans <= 1){ portEXIT_CRITICAL(&lock); mtb_Ble_App_Cmd_Reject(); return; }   // one stays
            memmove(&save.loan[i], &save.loan[i + 1], sizeof(save.loan[0]) * (LOANS - 1 - i));
            memset(&save.loan[LOANS - 1], 0, sizeof(save.loan[0]));
            save.loans--;
        } else {
            const int at = i < save.loans ? i : save.loans;
            save.loan[at] = l;
            if (at >= save.loans) save.loans++;
        }
        calc_Fit(calc, save);
        portEXIT_CRITICAL(&lock);
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Savings(JsonDocument& c){
    const char* v = c["savings"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[48]; snprintf(t, sizeof(t), "%s", v);
    char* p = t; trim(p);
    char rate[16] = "", amount[24] = "", more[4] = "";
    uint16_t bp; uint32_t max = 0;
    const int n = sscanf(p, "%15s %23s %3s", rate, amount, more);
    if (n < 1 || n > 2 || !rate_Bp(rate, bp) || (n == 2 && !amount_Value(amount, max))){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    save.savingsBp = bp;
    if (n == 2) save.savingsMax = max;
    calc_Fit(calc, save);
    portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}
static void cmd_Note(JsonDocument& c){
    const char* v = c["note"] | (const char*)nullptr;
    if (!v){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[128]; mtb_Kit_To_Ascii(t, sizeof(t), v);
    char* p = t; trim(p);
    portENTER_CRITICAL(&lock); snprintf(save.note, sizeof(save.note), "%s", p); portEXIT_CRITICAL(&lock);
    phoneChanged = true;
}

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool knobClick = false, knobDouble = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED){ if (b.count >= 2) knobDouble = true; else knobClick = true; } }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

/** Turned @p t clicks on the field in the box (faster turning, bigger steps). Under the lock. */
static void calc_Turn(Lnc_Calc_t& c, const Lnc_Save_t& s, int t){
    const int dir = t > 0 ? 1 : -1, n = abs(t), mult = n >= 3 ? 5 : 1;
    if (!c.savings){
        if (c.field == 0){ c.product = (uint8_t)(((c.product + t) % s.loans + s.loans) % s.loans); calc_Fit(c, s); return; }
        if (c.field == 1){
            const uint32_t st = amount_Step(s.loan[c.product].maxAmount);
            int64_t v = (int64_t)round_To(c.amount, st) + (int64_t)t * mult * st;
            if (v < st) v = st;
            if (v > s.loan[c.product].maxAmount) v = s.loan[c.product].maxAmount;
            c.amount = (uint32_t)v;
            return;
        }
        for (int i = 0; i < n * mult; i++) c.months = month_Step(c.months, dir, s.loan[c.product].maxMonths);
    } else {
        if (c.field == 0){
            const uint32_t st = amount_Step(s.savingsMax);
            int64_t v = (int64_t)round_To(c.deposit, st) + (int64_t)t * mult * st;
            if (v < st) v = st;
            if (v > s.savingsMax) v = s.savingsMax;
            c.deposit = (uint32_t)v;
            return;
        }
        for (int i = 0; i < n * mult; i++) c.saveMonths = month_Step(c.saveMonths, dir, 120);
    }
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 12, 22), C_HEAD = MTB_KIT_RGB(0, 70, 70), C_BOX = MTB_KIT_RGB(14, 26, 42);
static const uint16_t C_TEAL = MTB_KIT_RGB(0, 190, 170), C_INT = MTB_KIT_RGB(255, 140, 40), C_SAVE = MTB_KIT_RGB(90, 220, 140);

/** A field: its label, its value, in a box while it is the one the knob changes. */
static void field(int y, const char* label, const char* value, const char* extra, bool chosen, uint32_t ms){
    const int x0 = 2, x1 = 124, h = 22;
    if (chosen){
        mtb_Panel_Fill_Rect(x0, (int16_t)y, x1, (int16_t)(y + h - 1), C_BOX);
        mtb_Panel_Draw_Rect(x0, (int16_t)y, x1, (int16_t)(y + h - 1), (ms / 500) % 2 ? KIT_GOLD : MTB_KIT_RGB(150, 110, 0));
    }
    const uint16_t bg = chosen ? C_BOX : C_BG;
    mtb_Kit_Text(6, (int16_t)(y + 2), Terminal4x6, chosen ? KIT_GOLD : KIT_LABEL, label, bg);
    if (chosen) mtb_Kit_Text_Right(x1 - 3, (int16_t)(y + 2), Terminal4x6, KIT_GOLD, "< TURN >", bg);
    char fit[48];
    const int ew = extra ? mtb_Kit_Text_Width(Terminal6x8, extra) + 6 : 0;
    const uint8_t* font = mtb_Kit_Text_Width(Terminal8x12, value) <= x1 - 8 - ew ? Terminal8x12 : Terminal6x8;
    mtb_Kit_Fit(fit, sizeof(fit), font, value, (int16_t)(x1 - 8 - ew));
    mtb_Kit_Text(6, (int16_t)(y + (font == Terminal8x12 ? 9 : 11)), font, KIT_TEXT, fit, bg);
    if (extra) mtb_Kit_Text_Right(x1 - 3, (int16_t)(y + 11), Terminal6x8, KIT_GOLD, extra, bg);
}

/** A sum on the right: big if it fits. */
static void big_Sum(int y, const char* label, const char* value, uint16_t color){
    mtb_Kit_Text(132, (int16_t)y, Terminal4x6, KIT_LABEL, label, C_BG);
    if (mtb_Kit_Text_Width(PT_Sans_Narrow_Bold18x22, value) <= W - 136) mtb_Kit_Text(132, (int16_t)(y + 8), PT_Sans_Narrow_Bold18x22, color, value, C_BG);
    else mtb_Kit_Text(132, (int16_t)(y + 12), Terminal8x12, color, value, C_BG);
}
static void small_Sum(int y, const char* label, const char* value, uint16_t color){
    mtb_Kit_Text(132, (int16_t)y, Terminal4x6, KIT_LABEL, label, C_BG);
    mtb_Kit_Text(132, (int16_t)(y + 7), Terminal6x8, color, value, C_BG);
}

/** The bar: the two parts of a total, and their shares under it. */
static void share_Bar(int y, double a, double b, const char* la, const char* lb, uint16_t ca, uint16_t cb){
    const int x0 = 4, x1 = W - 5, w = x1 - x0 + 1;
    const double t = a + b;
    const int wa = t > 0 ? (int)lround(w * a / t) : w;
    if (wa > 0) mtb_Panel_Fill_Rect(x0, (int16_t)y, (int16_t)(x0 + wa - 1), (int16_t)(y + 5), ca);
    if (wa < w) mtb_Panel_Fill_Rect((int16_t)(x0 + wa), (int16_t)y, x1, (int16_t)(y + 5), cb);
    char t1[32], t2[32];
    const int pa = t > 0 ? (int)lround(100 * a / t) : 100;
    snprintf(t1, sizeof(t1), "%s %d%%", la, pa);
    snprintf(t2, sizeof(t2), "%s %d%%", lb, 100 - pa);
    mtb_Kit_Text(x0, (int16_t)(y + 8), Terminal4x6, ca, t1, C_BG);
    mtb_Kit_Text_Right(x1, (int16_t)(y + 8), Terminal4x6, cb, t2, C_BG);
}

static void draw(const Lnc_Save_t& s, const Lnc_Calc_t& c, uint32_t ms, bool idle){
    mtb_Panel_Fill_Screen(C_BG);
    char t[96], v[48], x[24], fit[96];
    mtb_Panel_Fill_Rect(0, 0, W - 1, 10, C_HEAD);
    snprintf(t, sizeof(t), "%s", s.bank[0] ? s.bank : "YOUR MONEY");
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 100);
    mtb_Kit_Text(3, 2, Terminal6x8, KIT_TEXT, fit, C_HEAD);
    mtb_Kit_Text_Right(W - 3, 2, Terminal6x8, KIT_GOLD, c.savings ? "SAVINGS CALCULATOR" : "LOAN CALCULATOR", C_HEAD);
    mtb_Panel_Draw_VLine(128, 14, 84, KIT_LINE);
    if (!c.savings){
        const Lnc_Loan_t& l = s.loan[c.product < s.loans ? c.product : 0];
        snprintf(t, sizeof(t), "%s", l.name); mtb_Kit_Upper(t);
        rate_Text(l.rateBp, x, sizeof(x));
        field(14, "LOAN", t, x, c.field == 0, ms);
        money_Text(s.currency, c.amount, v, sizeof(v));
        field(39, "AMOUNT", v, nullptr, c.field == 1, ms);
        months_Text(c.months, v, sizeof(v));
        field(64, "TO REPAY OVER", v, nullptr, c.field == 2, ms);
        const Lnc_Result_t r = loan_Result(c.amount, l.rateBp, c.months);
        money_Text(s.currency, r.monthly, v, sizeof(v));
        big_Sum(14, "EACH MONTH", v, KIT_TEXT);
        money_Text(s.currency, r.interest, v, sizeof(v));
        small_Sum(48, "INTEREST OVER THE TERM", v, C_INT);
        money_Text(s.currency, r.total, v, sizeof(v));
        small_Sum(68, "TOTAL TO REPAY", v, KIT_TEXT);
        share_Bar(92, c.amount, r.interest, "THE LOAN", "INTEREST", C_TEAL, C_INT);
    } else {
        money_Text(s.currency, c.deposit, v, sizeof(v));
        field(14, "SAVE EACH MONTH", v, nullptr, c.field == 0, ms);
        months_Text(c.saveMonths, v, sizeof(v));
        field(39, "FOR", v, nullptr, c.field == 1, ms);
        rate_Text(s.savingsBp, x, sizeof(x));
        snprintf(t, sizeof(t), "AT %s A YEAR", x);
        mtb_Kit_Text(6, 66, Terminal4x6, KIT_LABEL, "SAVINGS RATE", C_BG);
        mtb_Kit_Text(6, 75, Terminal6x8, KIT_GOLD, t, C_BG);
        const Lnc_Result_t r = savings_Result(c.deposit, s.savingsBp, c.saveMonths);
        snprintf(t, sizeof(t), "AFTER %u MONTH%s", (unsigned)c.saveMonths, c.saveMonths == 1 ? "" : "S");
        money_Text(s.currency, r.total, v, sizeof(v));
        big_Sum(14, t, v, C_SAVE);
        money_Text(s.currency, r.monthly, v, sizeof(v));
        small_Sum(48, "YOU PUT IN", v, KIT_TEXT);
        money_Text(s.currency, r.interest, v, sizeof(v));
        small_Sum(68, "INTEREST EARNED", v, C_SAVE);
        share_Bar(92, r.monthly, r.interest, "YOUR SAVINGS", "INTEREST", C_TEAL, C_SAVE);
    }
    // Along the bottom: how to use it, and the bank's note, in turn.
    mtb_Panel_Fill_Rect(0, H - 9, W - 1, H - 1, C_HEAD);
    const bool help = idle ? (ms / 3000) % 2 == 0 : (ms / 6000) % 3 == 0;
    if (help || !s.note[0]){
        if (idle) snprintf(t, sizeof(t), "TRY IT: TURN THE KNOB");
        else if (c.savings) snprintf(t, sizeof(t), "TURN: CHANGE   CLICK: NEXT   DOUBLE CLICK: LOANS");
        else snprintf(t, sizeof(t), "TURN: CHANGE   CLICK: NEXT   DOUBLE CLICK: SAVINGS");
        mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_GOLD, t, C_HEAD);
    } else {
        snprintf(t, sizeof(t), "%s", s.note); mtb_Kit_Upper(t);
        mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - 6);
        mtb_Kit_Text_Centre(W / 2, H - 7, Terminal4x6, KIT_TEXT, fit, C_HEAD);
    }
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void loanCalculator_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Bank, cmd_Currency, cmd_Loan, cmd_Savings, cmd_Note);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    calc_Reset(calc, save, false);
    turns = 0; knobClick = knobDouble = false; phoneChanged = false;
    appOpen = true;
    Lnc_Save_t* s = (Lnc_Save_t*)heap_caps_malloc(sizeof(Lnc_Save_t), MALLOC_CAP_SPIRAM);
    if (!s){ appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t lastDraw = 0, pushedMs = 0, lastUse = millis() - IDLE_MS;
    bool push = true, saveNow = false, idle = true;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; push = saveNow = true; lastDraw = 0; }
        const int t = turns;
        Lnc_Calc_t c;
        portENTER_CRITICAL(&lock);
        if (t){ turns -= t; calc_Turn(calc, save, t); }
        if (knobClick) calc.field = (uint8_t)((calc.field + 1) % (calc.savings ? 2 : 3));
        if (knobDouble){ calc.savings = !calc.savings; calc.field = 0; }
        const bool used = t || knobClick || knobDouble;
        knobClick = knobDouble = false;
        if (!used && !idle && ms - lastUse >= IDLE_MS) calc_Reset(calc, save, false);
        c = calc; *s = save;
        portEXIT_CRITICAL(&lock);
        if (used){ lastUse = ms; lastDraw = 0; push = true; }
        const bool nowIdle = ms - lastUse >= IDLE_MS;
        if (nowIdle != idle){ idle = nowIdle; lastDraw = 0; push = true; }
        if (ms - lastDraw >= 250){
            lastDraw = ms;
            mtb_Panel_Begin_Update();
            draw(*s, c, ms, idle);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, s, sizeof(*s)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    heap_caps_free(s);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void lnc_Values(JsonObject values){
    Lnc_Save_t* s = (Lnc_Save_t*)heap_caps_malloc(sizeof(Lnc_Save_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    Lnc_Calc_t c;
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; c = calc; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); calc_Reset(c, *s, false); }
    char t[96], key[8], a[32], r[16], m[32];
    values["bank"] = s->bank;
    values["currency"] = s->currency;
    for (int i = 0; i < LOANS; i++){
        snprintf(key, sizeof(key), "loan%d", i);
        if (i < s->loans){ rate_Text(s->loan[i].rateBp, r, sizeof(r)); snprintf(t, sizeof(t), "%s %s %lu %u", s->loan[i].name, r, (unsigned long)s->loan[i].maxAmount, (unsigned)s->loan[i].maxMonths); values[key] = t; }
        else values[key] = "";
    }
    rate_Text(s->savingsBp, r, sizeof(r));
    snprintf(t, sizeof(t), "%s %lu", r, (unsigned long)s->savingsMax);
    values["savings"] = t;
    values["note"] = s->note;
    JsonArray rows = values["calc"].to<JsonArray>();
    JsonObject row = rows.add<JsonObject>();
    if (!appOpen){ row["label"] = "Loan & Savings Calculator"; row["value"] = "Shown while it is open on Pixlpal"; row["state"] = "off"; heap_caps_free(s); return; }
    if (!c.savings){
        const Lnc_Loan_t& l = s->loan[c.product < s->loans ? c.product : 0];
        rate_Text(l.rateBp, r, sizeof(r));
        money_Text(s->currency, c.amount, a, sizeof(a));
        snprintf(t, sizeof(t), "%s at %s: %s over %u months", l.name, r, a, (unsigned)c.months);
        row["label"] = "Loan"; row["value"] = t; row["state"] = "info";
        const Lnc_Result_t x = loan_Result(c.amount, l.rateBp, c.months);
        money_Text(s->currency, x.monthly, a, sizeof(a)); money_Text(s->currency, x.total, m, sizeof(m));
        snprintf(t, sizeof(t), "%s a month, %s in all", a, m);
        row = rows.add<JsonObject>(); row["label"] = "Repaid"; row["value"] = t; row["state"] = "ok";
    } else {
        money_Text(s->currency, c.deposit, a, sizeof(a));
        snprintf(t, sizeof(t), "%s a month for %u months at %s", a, (unsigned)c.saveMonths, r);
        row["label"] = "Savings"; row["value"] = t; row["state"] = "info";
        const Lnc_Result_t x = savings_Result(c.deposit, s->savingsBp, c.saveMonths);
        money_Text(s->currency, x.total, a, sizeof(a));
        row = rows.add<JsonObject>(); row["label"] = "Grows to"; row["value"] = a; row["state"] = "ok";
    }
    heap_caps_free(s);
}
