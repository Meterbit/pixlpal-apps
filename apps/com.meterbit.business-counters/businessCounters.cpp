/**
 * Business & Creator Counters, ID {16,0} (BUSINESS): today's sales, orders and revenue from Stripe, Paystack and
 * Shopify, with a daily goal and the best day, and a cash register's ring for every new sale; and a creator's
 * counters: YouTube subscribers, GitHub stars and followers, and a website's visitors (Plausible), counting up as
 * they change, with a fanfare at each milestone (100, 250, 500, 1,000...). Pixlpal asks each provider itself, with the
 * user's own keys, which never leave it.
 *
 * The knob: click, the other page (or close a banner); double click, ask everything again now; long press, sound
 * off or on; turn, brightness.
 */
#include "mtb_engine.h"
#include "mtb_device_prefs.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "esp_heap_caps.h"
#include "bcc_App.h"
#include "bcc_Ui.h"
#include "bcc_Mobile_Ui.h"
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

EXT_RAM_BSS_ATTR TaskHandle_t businessCounters_Task_H = NULL;
static void businessCounters_App_Task(void* dApplication);
static void bcc_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *businessCounters_App;
Mtb_Applications* businessCounters_App_GetInstance() {
    if (!businessCounters_App){
        businessCounters_App = new Mtb_Applications_StatusBar(businessCounters_App_Task, &businessCounters_Task_H, "Business Counters", {16,0}, 8192);
        businessCounters_App->mtb_App_Set_Mobile_UI(BCC_UI_MANIFEST, BCC_UI_API, nullptr, bcc_Values);
    }
    return businessCounters_App;
}
MTB_REGISTER_APP(businessCounters_App, 16, 0)

EXT_RAM_BSS_ATTR static Bcc_Settings_t settings;
EXT_RAM_BSS_ATTR static Bcc_Settings_t received;
EXT_RAM_BSS_ATTR static Bcc_Status_t shown;            // the app task's copy (the phone reads it under the lock)
EXT_RAM_BSS_ATTR static Bcc_Log_t bccLog;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, longPressRequested = false, refreshRequested = false, testRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

static bool read_Settings(Bcc_Settings_t& s){
    bcc_Settings_Default(s);
    Bcc_Settings_t* saved = (Bcc_Settings_t*)heap_caps_calloc(1, sizeof(Bcc_Settings_t), MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (saved){
        mtb_Read_Nvs_Struct(BCC_NVS_KEY, saved, sizeof(*saved));
        ok = saved->magic == BCC_SETTINGS_MAGIC;
        if (ok) s = *saved;
        memset(saved, 0, sizeof(*saved));
        heap_caps_free(saved);
    }
    bcc_Settings_Clamp(s);
    return ok;
}

static uint32_t local_Day(uint32_t now, uint16_t* minute){
    const time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (minute) *minute = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
    return (uint32_t)mtb_Kit_Days_From_Civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Bcc_Settings_t* begin_Change(void){
    Bcc_Settings_t* next = (Bcc_Settings_t*)heap_caps_malloc(sizeof(Bcc_Settings_t), MALLOC_CAP_SPIRAM);
    if (next == nullptr) return nullptr;
    portENTER_CRITICAL(&requestLock);
    *next = settingsReceived ? received : settings;
    portEXIT_CRITICAL(&requestLock);
    return next;
}

static void end_Change(Bcc_Settings_t* next){
    bcc_Settings_Clamp(*next);
    portENTER_CRITICAL(&requestLock);
    received = *next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static void drop_Change(Bcc_Settings_t* next){
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
    mtb_Ble_App_Cmd_Reject();
}

// Text from the phone: trimmed, printable ASCII only when @p plain. False if too long or not allowed.
static bool clean(const char* in, char* out, size_t size, bool plain){
    if (!in) return false;
    while (*in == ' ') in++;
    size_t n = strlen(in);
    while (n && (in[n - 1] == ' ' || in[n - 1] == '\n' || in[n - 1] == '\r')) n--;
    if (n >= size) return false;
    for (size_t i = 0; i < n; i++){
        if ((unsigned char)in[i] < ' ') return false;
        if (plain && (in[i] <= ' ' || in[i] > '~')) return false;
    }
    memcpy(out, in, n);
    out[n] = '\0';
    return true;
}

static bool only(const char* text, const char* extra){
    for (const char* p = text; *p; p++) if (!isalnum((unsigned char)*p) && !strchr(extra, *p)) return false;
    return true;
}

static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }
static void cmd_Test(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } testRequested = true; }

// Switches and numbers.
static void cmd_Number(JsonDocument& c){
    static const struct { const char* key; long lo, hi; } K[] = {
        {"stripeOn", 0, 1}, {"paystackOn", 0, 1}, {"shopifyOn", 0, 1}, {"youtubeOn", 0, 1}, {"githubOn", 0, 1}, {"plausibleOn", 0, 1},
        {"goal", 0, 1000000000}, {"chime", 0, BCC_CHIME_ALL}, {"announce", 0, 1}, {"showAmounts", 0, 1}, {"quietOn", 0, 1},
        {"cycle", 0, 120}, {"volume", 1, 10}};
    for (const auto& k : K){
        if (c[k.key].isNull()) continue;
        long v;
        if (!mtb_Kit_Json_Range(c[k.key], k.lo, k.hi, v)) break;
        if (!strcmp(k.key, "cycle") && v > 0 && v < 5) break;
        Bcc_Settings_t* next = begin_Change();
        if (!next) break;
        const int idx = (int)(&k - K);
        if (idx < BCC_SOURCES) next->on[idx] = (uint8_t)v;
        else if (!strcmp(k.key, "goal")) next->goal = (uint32_t)v;
        else if (!strcmp(k.key, "chime")) next->chime = (uint8_t)v;
        else if (!strcmp(k.key, "announce")) next->announce = (uint8_t)v;
        else if (!strcmp(k.key, "showAmounts")) next->showAmounts = (uint8_t)v;
        else if (!strcmp(k.key, "quietOn")) next->quietOn = (uint8_t)v;
        else if (!strcmp(k.key, "cycle")) next->cycle = (uint8_t)v;
        else if (!strcmp(k.key, "volume")) next->volume = (uint8_t)v;
        end_Change(next);
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}

// Names, addresses and the goal's currency (none of them secret).
static void cmd_Text(JsonDocument& c){
    Bcc_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[260];
    bool ok = false;
    if (!c["shopStore"].isNull()){
        ok = clean(c["shopStore"] | "", t, sizeof(t), false);
        if (ok && !t[0]) next->shopStore[0] = '\0';
        else ok = ok && bcc_Shop_Domain(t, next->shopStore, sizeof(next->shopStore));
    } else if (!c["shopClientId"].isNull()){
        ok = clean(c["shopClientId"] | "", t, sizeof(next->shopClientId), true) && only(t, "-_");
        if (ok) strcpy(next->shopClientId, t);
    } else if (!c["ytChannel"].isNull()){
        ok = clean(c["ytChannel"] | "", t, sizeof(next->ytChannel), true) && only(t, "@._-");
        // A channel address works too: youtube.com/@name or youtube.com/channel/UC...
        if (ok) strcpy(next->ytChannel, t);
        else if (clean(c["ytChannel"] | "", t, sizeof(t), true)){
            const char* at = strstr(t, "/@");
            const char* uc = strstr(t, "/channel/");
            const char* from = at ? at + 1 : uc ? uc + 9 : nullptr;
            char part[64] = "";
            for (size_t i = 0; from && from[i] && from[i] != '/' && from[i] != '?' && i + 1 < sizeof(part); i++){ part[i] = from[i]; part[i + 1] = '\0'; }
            ok = part[0] && only(part, "@._-");
            if (ok) strcpy(next->ytChannel, part);
        }
    } else if (!c["ghRepos"].isNull()){
        ok = clean(c["ghRepos"] | "", t, sizeof(t), false) && bcc_Repos_Parse(t, next->ghRepos);
    } else if (!c["ghUser"].isNull()){
        ok = clean(c["ghUser"] | "", t, sizeof(next->ghUser), true) && only(t, "-");
        if (ok) strcpy(next->ghUser, t);
    } else if (!c["plSite"].isNull()){
        ok = clean(c["plSite"] | "", t, sizeof(next->plSite), true) && only(t, ".-_/");
        if (ok) strcpy(next->plSite, t);
    } else if (!c["plHost"].isNull()){
        ok = clean(c["plHost"] | "", t, sizeof(t), true);
        const char* h = t;
        if (ok && !strncmp(h, "https://", 8)) h += 8;
        char host[64];
        ok = ok && snprintf(host, sizeof(host), "%s", h) < (int)sizeof(host);
        if (ok){ char* slash = strchr(host, '/'); if (slash) *slash = '\0'; }
        ok = ok && only(host, ".-:");
        if (ok) strcpy(next->plHost, host[0] ? host : "plausible.io");
    } else if (!c["goalCurrency"].isNull()){
        ok = clean(c["goalCurrency"] | "", t, sizeof(t), true) && (t[0] == '\0' || (strlen(t) == 3 && only(t, "")));
        if (ok){ for (char* p = t; *p; p++) *p = (char)toupper((unsigned char)*p); strcpy(next->goalCurrency, t); }
    }
    if (!ok){ drop_Change(next); return; }
    end_Change(next);
}

// Keys and tokens: kept on Pixlpal, never sent back. Empty clears one.
static void cmd_Secret(JsonDocument& c){
    Bcc_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[160];
    bool ok = false;
    if (!c["stripeKey"].isNull()){
        ok = clean(c["stripeKey"] | "", t, sizeof(next->stripeKey), true) && (!t[0] || ((!strncmp(t, "sk_", 3) || !strncmp(t, "rk_", 3)) && only(t, "_")));
        if (ok) strcpy(next->stripeKey, t);
    } else if (!c["paystackKey"].isNull()){
        ok = clean(c["paystackKey"] | "", t, sizeof(next->paystackKey), true) && (!t[0] || (!strncmp(t, "sk_", 3) && only(t, "_")));
        if (ok) strcpy(next->paystackKey, t);
    } else if (!c["shopSecret"].isNull()){
        ok = clean(c["shopSecret"] | "", t, sizeof(next->shopSecret), true);
        if (ok) strcpy(next->shopSecret, t);
    } else if (!c["ytKey"].isNull()){
        ok = clean(c["ytKey"] | "", t, sizeof(next->ytKey), true) && only(t, "-_");
        if (ok) strcpy(next->ytKey, t);
    } else if (!c["ghToken"].isNull()){
        ok = clean(c["ghToken"] | "", t, sizeof(next->ghToken), true) && only(t, "_");
        if (ok) strcpy(next->ghToken, t);
    } else if (!c["plKey"].isNull()){
        ok = clean(c["plKey"] | "", t, sizeof(next->plKey), true) && only(t, "-_");
        if (ok) strcpy(next->plKey, t);
    }
    memset(t, 0, sizeof(t));
    if (!ok){ drop_Change(next); return; }
    end_Change(next);
}

// {"quietFrom": "22:00", "quietTo": "07:00"}
static void cmd_Quiet(JsonDocument& c){
    const int from = mtb_Kit_Parse_Clock(c["quietFrom"] | ""), to = mtb_Kit_Parse_Clock(c["quietTo"] | "");
    if (from < 0 || to < 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Bcc_Settings_t* next = begin_Change();
    if (!next){ mtb_Ble_App_Cmd_Reject(); return; }
    next->quietFrom = (uint16_t)from;
    next->quietTo = (uint16_t)to;
    end_Change(next);
}

// The knob ------------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------------
static bool any_Shop(const Bcc_Settings_t& s){ for (int i = 0; i < BCC_SHOPS; i++) if (s.on[i]) return true; return false; }
static bool any_Creator(const Bcc_Settings_t& s){ for (int i = BCC_YOUTUBE; i < BCC_SOURCES; i++) if (bcc_Source_Ready(s, (uint8_t)i)) return true; return false; }

static uint8_t valid_Page(const Bcc_Settings_t& s, uint8_t page){
    const bool shop = any_Shop(s), creator = any_Creator(s);
    if (page == BCC_PAGE_SALES && shop) return page;
    if (page == BCC_PAGE_CREATOR && creator) return page;
    return shop ? BCC_PAGE_SALES : creator ? BCC_PAGE_CREATOR : BCC_PAGE_SETUP;
}

// A number shown counting up (or down) to @p target over about a second.
static bool approach(double& shown, double target){
    if (shown == target) return false;
    const double d = target - shown;
    if (fabs(d) < 0.5){ shown = target; return true; }
    const double step = d / 6.0;
    shown += fabs(step) < 1 ? (d > 0 ? 1 : -1) : step;
    if ((d > 0 && shown > target) || (d < 0 && shown < target)) shown = target;
    return true;
}

static bool can_Sound(const Bcc_Settings_t& s, uint8_t kind, bool muted, uint16_t minute){
    if (muted || s.chime == BCC_CHIME_OFF || mtb_Silent_Mode()) return false;
    if (kind == BCC_BANNER_MILESTONE && s.chime != BCC_CHIME_ALL) return false;
    return !(s.quietOn && bcc_In_Window(minute, s.quietFrom, s.quietTo));
}

static void businessCounters_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Refresh, cmd_Number, cmd_Text, cmd_Secret, cmd_Quiet, cmd_Test);
    THIS_APP->mtb_App_Init();

    if (!read_Settings(settings)) mtb_Write_Nvs_Struct(BCC_NVS_KEY, &settings, sizeof(settings));
    settingsReceived = false;
    longPressRequested = false;
    refreshRequested = false;
    testRequested = false;
    clicksRequested = 0;
    turns = 0;
    memset(&bccLog, 0, sizeof(bccLog));
    mtb_Read_Nvs_Struct(BCC_LOG_KEY, &bccLog, sizeof(bccLog));
    if (bccLog.magic != BCC_SETTINGS_MAGIC) bcc_Log_Clear(bccLog);
    Bcc_Status_t* latest = (Bcc_Status_t*)heap_caps_calloc(1, sizeof(Bcc_Status_t), MALLOC_CAP_SPIRAM);
    Bcc_View_t* v = (Bcc_View_t*)heap_caps_calloc(1, sizeof(Bcc_View_t), MALLOC_CAP_SPIRAM);
    Bcc_Total_t* total = (Bcc_Total_t*)heap_caps_calloc(1, sizeof(Bcc_Total_t), MALLOC_CAP_SPIRAM);
    if (!latest || !v || !total || !bcc_Net_Start(settings)){
        heap_caps_free(latest); heap_caps_free(v); heap_caps_free(total);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    portENTER_CRITICAL(&requestLock);
    memset(&shown, 0, sizeof(shown));
    shown.creator.live = shown.creator.visitors = shown.creator.pageviews = -1;
    portEXIT_CRITICAL(&requestLock);
    appOpen = true;
    uint32_t version = 0, pageSince = millis(), bannerUntil = 0, phoneMs = 0, logSavedMs = 0, recordDay = 0;
    uint8_t page = valid_Page(settings, BCC_PAGE_SALES);
    bool redraw = true, phoneDirty = true, muted = false, logDirty = false, settingsDirty = false;
    uint32_t today = 0;

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        uint16_t minute = 0;
        if (now > 1700000000u) today = local_Day(now, &minute);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            settingsDirty = true;
            bcc_Net_Set_Settings(settings);
            page = valid_Page(settings, page);
            redraw = phoneDirty = true;
        }
        if (refreshRequested){ refreshRequested = false; bcc_Net_Refresh(); }

        // New answers: new sales, milestones, the day's total.
        if (bcc_Net_Copy(*latest, version)){
            Bcc_Status_t before;
            portENTER_CRITICAL(&requestLock);
            before = shown;
            shown = *latest;
            portEXIT_CRITICAL(&requestLock);
            int newSales = 0, newest = -1;
            for (int i = 0; i < BCC_SHOPS; i++){
                const Bcc_Sales_t &a = before.shops[i], &b = latest->shops[i];
                // Only a rise on the same day, after a first answer (not the sales made before the app opened).
                if (a.updated && b.updated && a.day == b.day && b.count > a.count){
                    newSales += b.count - a.count;
                    if (newest < 0 || b.lastAt >= latest->shops[newest].lastAt) newest = i;
                }
            }
            if (newSales && newest >= 0){
                const Bcc_Sales_t& shop = latest->shops[newest];
                v->banner = BCC_BANNER_SALE;
                snprintf(v->bannerTitle, sizeof(v->bannerTitle), newSales > 1 ? "%d NEW SALES!" : "NEW SALE!", newSales);
                if (settings.showAmounts) bcc_Format_Money(v->bannerBig, sizeof(v->bannerBig), shop.last.code, shop.last.minor, false);
                else snprintf(v->bannerBig, sizeof(v->bannerBig), "#%u", shop.count);
                snprintf(v->bannerDetail, sizeof(v->bannerDetail), "%s%s%s", bcc_Source_Name((uint8_t)newest), shop.lastLabel[0] ? " " : "", shop.lastLabel);
                mtb_Kit_Upper(v->bannerDetail);
                bannerUntil = ms + 9000;
                v->frame = 0;
                if (can_Sound(settings, BCC_BANNER_SALE, muted, minute)){
                    char speech[80] = "", money[48];
                    if (settings.announce){
                        if (settings.showAmounts){ bcc_Speak_Money(money, sizeof(money), shop.last.code, shop.last.minor); snprintf(speech, sizeof(speech), "New sale. %s", money); }
                        else snprintf(speech, sizeof(speech), "New sale");
                    }
                    bcc_Sound_Sale(settings.volume, speech);
                }
            }
            // Milestones: first counts are the starting point; past them, each 1, 2.5, 5 times ten to a power.
            const Bcc_Creator_t& c = latest->creator;
            struct { int64_t value; int64_t* seen64; uint32_t* seen32; const char* what; bool known; } M[] = {
                {c.subs, &bccLog.seenSubs, nullptr, "subscribers", c.updated[BCC_YOUTUBE] && !c.subsHidden && c.subs >= 0},
                {(int64_t)c.stars, nullptr, &bccLog.seenStars, "stars on GitHub", c.updated[BCC_GITHUB] && c.repoCount > 0},
                {(int64_t)c.followers, nullptr, &bccLog.seenFollowers, "followers on GitHub", c.updated[BCC_GITHUB] && settings.ghUser[0]}};
            for (auto& m : M){
                if (!m.known) continue;
                const int64_t seen = m.seen64 ? *m.seen64 : (*m.seen32 == 0xFFFFFFFFu ? -1 : (int64_t)*m.seen32);
                if (seen >= 0 && m.value > seen){
                    const int64_t mark = bcc_Milestone(seen, m.value);
                    if (mark && !(bannerUntil && v->banner == BCC_BANNER_SALE)){
                        v->banner = BCC_BANNER_MILESTONE;
                        snprintf(v->bannerTitle, sizeof(v->bannerTitle), "MILESTONE!");
                        bcc_Format_Count(v->bannerBig, sizeof(v->bannerBig), mark, false);
                        snprintf(v->bannerDetail, sizeof(v->bannerDetail), "%s", m.what);
                        mtb_Kit_Upper(v->bannerDetail);
                        bannerUntil = ms + 12000;
                        v->frame = 0;
                        if (can_Sound(settings, BCC_BANNER_MILESTONE, muted, minute)){
                            char speech[80] = "";
                            if (settings.announce) snprintf(speech, sizeof(speech), "Congratulations! %lld %s.", (long long)mark, m.what);
                            bcc_Sound_Milestone(settings.volume, speech);
                        }
                    }
                }
                if (seen < 0 || m.value > seen){
                    if (m.seen64) *m.seen64 = m.value;
                    else *m.seen32 = (uint32_t)m.value;
                    logDirty = true;
                }
            }
            phoneDirty = redraw = true;
        }

        // The day's total, and the best day (in the goal's currency, or the main one).
        bcc_Total(settings, shown.shops, today, *total);
        if (total->main >= 0 && today){
            const Bcc_Money_t& m = total->money[total->main];
            const bool sameCurrency = !bccLog.best.code[0] || !strcmp(bccLog.best.code, m.code);
            if (sameCurrency && m.minor > bccLog.best.minor){
                // Beating an earlier day is celebrated, once a day.
                if (bccLog.bestDay && bccLog.bestDay != today && recordDay != today && bccLog.best.minor > 0){
                    recordDay = today;
                    if (!bannerUntil){
                        v->banner = BCC_BANNER_RECORD;
                        snprintf(v->bannerTitle, sizeof(v->bannerTitle), "BEST DAY EVER!");
                        bcc_Format_Money(v->bannerBig, sizeof(v->bannerBig), m.code, m.minor, false);
                        snprintf(v->bannerDetail, sizeof(v->bannerDetail), "MORE THAN ANY DAY BEFORE");
                        bannerUntil = ms + 10000;
                        v->frame = 0;
                        if (settings.showAmounts && can_Sound(settings, BCC_BANNER_MILESTONE, muted, minute)) bcc_Sound_Milestone(settings.volume, nullptr);
                    }
                }
                bccLog.best = m;
                bccLog.bestDay = today;
                logDirty = true;
            }
        }

        // The knob.
        const int t = turns;
        if (t){ turns = 0; mtb_Brightness_Control(t > 0 ? ROT_CLOCKWISE : ROT_COUNTERCLOCKWISE); }
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        if (longPressRequested){
            longPressRequested = false;
            muted = !muted;
            if (muted) bcc_Sound_Stop();
            redraw = true;
        }
        if (clicks == 1){
            if (bannerUntil){ bannerUntil = 0; v->banner = BCC_BANNER_NONE; }
            else { page = valid_Page(settings, page == BCC_PAGE_SALES ? BCC_PAGE_CREATOR : BCC_PAGE_SALES); pageSince = ms; }
            redraw = true;
        } else if (clicks == 2) bcc_Net_Refresh();
        if (testRequested){
            testRequested = false;
            v->banner = BCC_BANNER_SALE;
            snprintf(v->bannerTitle, sizeof(v->bannerTitle), "NEW SALE!");
            if (settings.showAmounts) bcc_Format_Money(v->bannerBig, sizeof(v->bannerBig), total->main >= 0 ? total->money[total->main].code : "USD", 4950, false);
            else snprintf(v->bannerBig, sizeof(v->bannerBig), "#1");
            snprintf(v->bannerDetail, sizeof(v->bannerDetail), "A TEST FROM THE PHONE");
            bannerUntil = ms + 6000;
            v->frame = 0;
            char speech[64] = "", money[48];
            if (settings.announce){ bcc_Speak_Money(money, sizeof(money), total->main >= 0 ? total->money[total->main].code : "USD", 4950); snprintf(speech, sizeof(speech), "New sale. %s", money); }
            bcc_Sound_Sale(settings.volume, speech);
            redraw = true;
        }
        if (bannerUntil && (int32_t)(ms - bannerUntil) >= 0){ bannerUntil = 0; v->banner = BCC_BANNER_NONE; redraw = true; }

        // Pages turn by themselves when both are in use.
        const uint8_t valid = valid_Page(settings, page);
        if (valid != page){ page = valid; redraw = true; }
        if (!bannerUntil && settings.cycle && any_Shop(settings) && any_Creator(settings) && ms - pageSince >= settings.cycle * 1000u){
            page = page == BCC_PAGE_SALES ? BCC_PAGE_CREATOR : BCC_PAGE_SALES;
            pageSince = ms;
            redraw = true;
        }

        // Numbers count up to the new ones.
        bool moving = false;
        moving |= approach(v->shownMain, total->main >= 0 ? (double)total->money[total->main].minor : 0);
        moving |= approach(v->shownCount, (double)total->count);
        moving |= approach(v->shownSubs, (double)(shown.creator.subs > 0 ? shown.creator.subs : 0));
        moving |= approach(v->shownStars, (double)shown.creator.stars);
        moving |= approach(v->shownLive, (double)(shown.creator.live > 0 ? shown.creator.live : 0));
        if (moving) redraw = true;
        static uint32_t lastSecond = 0;
        if (now != lastSecond){ lastSecond = now; if (page == BCC_PAGE_CREATOR || now % 30 == 0) redraw = true; }

        v->settings = &settings;
        v->status = &shown;
        v->total = total;
        v->log = &bccLog;
        v->page = page;
        v->now = now;
        v->today = today;
        v->muted = muted;
        if (redraw){ redraw = false; bcc_Draw(*v); }
        else if (bannerUntil){ v->frame++; bcc_Draw_Banner_Frame(*v); }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(BCC_NVS_KEY, &settings, sizeof(settings)); }
        if (logDirty && ms - logSavedMs > 60000){ logDirty = false; logSavedMs = ms; mtb_Write_Nvs_Struct(BCC_LOG_KEY, &bccLog, sizeof(bccLog)); }
        if (phoneDirty && ms - phoneMs > 2000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    bcc_Sound_Stop();
    appOpen = false;
    bcc_Net_Stop();
    if (logDirty) mtb_Write_Nvs_Struct(BCC_LOG_KEY, &bccLog, sizeof(bccLog));
    if (settingsDirty) mtb_Write_Nvs_Struct(BCC_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(latest);
    heap_caps_free(v);
    heap_caps_free(total);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static const char* state_Word(uint8_t st){
    switch (st){ case BCC_OK: return "ok"; case BCC_WAIT: case BCC_OFF: return "info"; case BCC_LIMIT: case BCC_PLAN: return "warn"; default: return "error"; }
}

static void bcc_Values(JsonObject values){
    Bcc_Settings_t* s = (Bcc_Settings_t*)heap_caps_malloc(sizeof(Bcc_Settings_t), MALLOC_CAP_SPIRAM);
    Bcc_Status_t* st = (Bcc_Status_t*)heap_caps_calloc(1, sizeof(Bcc_Status_t), MALLOC_CAP_SPIRAM);
    Bcc_Log_t* log = (Bcc_Log_t*)heap_caps_calloc(1, sizeof(Bcc_Log_t), MALLOC_CAP_SPIRAM);
    if (!s || !st || !log){ heap_caps_free(s); heap_caps_free(st); heap_caps_free(log); return; }
    const bool open = appOpen;
    if (open){
        portENTER_CRITICAL(&requestLock);
        *s = settingsReceived ? received : settings;
        *st = shown;
        *log = bccLog;
        portEXIT_CRITICAL(&requestLock);
    } else {
        read_Settings(*s);
        mtb_Read_Nvs_Struct(BCC_LOG_KEY, log, sizeof(Bcc_Log_t));
        if (log->magic != BCC_SETTINGS_MAGIC) bcc_Log_Clear(*log);
    }
    static const char* const ON[BCC_SOURCES] = {"stripeOn", "paystackOn", "shopifyOn", "youtubeOn", "githubOn", "plausibleOn"};
    for (int i = 0; i < BCC_SOURCES; i++) values[ON[i]] = s->on[i];
    values["shopStore"] = s->shopStore;
    values["shopClientId"] = s->shopClientId;
    values["ytChannel"] = s->ytChannel;
    char t[200];
    size_t k = 0;
    t[0] = '\0';
    for (int i = 0; i < BCC_REPOS; i++) if (s->ghRepos[i][0]) k += (size_t)snprintf(t + k, sizeof(t) - k, "%s%s", k ? ", " : "", s->ghRepos[i]);
    values["ghRepos"] = t;
    values["ghUser"] = s->ghUser;
    values["plSite"] = s->plSite;
    values["plHost"] = s->plHost;
    snprintf(t, sizeof(t), "%lu", (unsigned long)s->goal);
    values["goal"] = s->goal ? t : "";
    values["goalCurrency"] = s->goalCurrency;
    values["chime"] = s->chime;
    values["announce"] = s->announce;
    values["showAmounts"] = s->showAmounts;
    values["quietOn"] = s->quietOn;
    char clock[8];
    snprintf(clock, sizeof(clock), "%02u:%02u", s->quietFrom / 60, s->quietFrom % 60);
    values["quietFrom"] = clock;
    snprintf(clock, sizeof(clock), "%02u:%02u", s->quietTo / 60, s->quietTo % 60);
    values["quietTo"] = clock;
    values["cycle"] = s->cycle;
    values["volume"] = s->volume;
    values["stripeKey_set"] = s->stripeKey[0] ? 1 : 0;
    values["paystackKey_set"] = s->paystackKey[0] ? 1 : 0;
    values["shopSecret_set"] = s->shopSecret[0] ? 1 : 0;
    values["ytKey_set"] = s->ytKey[0] ? 1 : 0;
    values["ghToken_set"] = s->ghToken[0] ? 1 : 0;
    values["plKey_set"] = s->plKey[0] ? 1 : 0;

    const uint32_t now = (uint32_t)time(nullptr);
    const uint32_t today = now > 1700000000u ? local_Day(now, nullptr) : 0;
    char money[32];
    // Today.
    JsonArray rows = values["today"].to<JsonArray>();
    Bcc_Total_t total;
    bcc_Total(*s, st->shops, today, total);
    JsonObject o = rows.add<JsonObject>();
    o["label"] = "Sales today";
    if (!open) { o["value"] = any_Shop(*s) ? "Open Business Counters on Pixlpal to count today's sales" : "Switch on a shop below"; o["state"] = "info"; }
    else {
        k = (size_t)snprintf(t, sizeof(t), "%lu", (unsigned long)total.count);
        for (uint8_t m = 0; m < total.moneyCount && s->showAmounts; m++){
            bcc_Format_Money(money, sizeof(money), total.money[m].code, total.money[m].minor, false);
            k += (size_t)snprintf(t + k, sizeof(t) - k, "%s%s", m ? " + " : ", ", money);
        }
        o["value"] = t;
        o["state"] = "ok";
        if (total.goalFraction >= 0){
            o = rows.add<JsonObject>();
            o["label"] = "Goal";
            int64_t scale = 1;
            for (uint8_t i = 0; i < bcc_Decimals(total.money[total.main].code); i++) scale *= 10;
            bcc_Format_Money(money, sizeof(money), total.money[total.main].code, (int64_t)s->goal * scale, false);
            snprintf(t, sizeof(t), "%d%% of %s", (int)(total.goalFraction * 100), money);
            o["value"] = t;
            o["state"] = total.goalFraction >= 1 ? "ok" : "info";
        }
    }
    if (log->bestDay && log->best.minor > 0){
        o = rows.add<JsonObject>();
        o["label"] = "Best day";
        bcc_Format_Money(money, sizeof(money), log->best.code, log->best.minor, false);
        o["value"] = money;
        o["state"] = log->bestDay == today ? "ok" : "info";
    }
    // Each shop.
    JsonArray shops = values["shops"].to<JsonArray>();
    for (int i = 0; i < BCC_SHOPS; i++){
        if (!s->on[i]) continue;
        const Bcc_Sales_t& shop = st->shops[i];
        o = shops.add<JsonObject>();
        snprintf(t, sizeof(t), "%s%s%s%s", bcc_Source_Name((uint8_t)i), shop.plan[0] ? " (" : "", shop.plan, shop.plan[0] ? ")" : "");
        o["label"] = t;
        if (!bcc_Source_Ready(*s, (uint8_t)i)) { o["value"] = i == BCC_SHOPIFY ? "Fill in the store and the secret or token" : "Paste the secret key"; o["state"] = "warn"; continue; }
        if (!open){ o["value"] = "Set up"; o["state"] = "info"; continue; }
        if (shop.state == BCC_WAIT){ o["value"] = "Asking..."; o["state"] = "info"; continue; }
        k = (size_t)snprintf(t, sizeof(t), "%u%s sale%s", shop.count, shop.more ? "+" : "", shop.count == 1 ? "" : "s");
        for (uint8_t m = 0; m < shop.moneyCount && s->showAmounts; m++){
            bcc_Format_Money(money, sizeof(money), shop.money[m].code, shop.money[m].minor, false);
            k += (size_t)snprintf(t + k, sizeof(t) - k, ", %s", money);
        }
        if (shop.state != BCC_OK) snprintf(t + k, sizeof(t) - k, "%s%s", shop.updated ? ". Now: " : "", shop.text);
        o["value"] = shop.updated || shop.state == BCC_OK ? t : shop.text;
        o["state"] = state_Word(shop.state);
    }
    // Creators.
    JsonArray creators = values["creators"].to<JsonArray>();
    const Bcc_Creator_t& c = st->creator;
    char a[24], b[24];
    for (uint8_t src = BCC_YOUTUBE; src < BCC_SOURCES; src++){
        if (!s->on[src]) continue;
        o = creators.add<JsonObject>();
        o["label"] = bcc_Source_Name(src);
        if (!bcc_Source_Ready(*s, src)){
            o["value"] = src == BCC_YOUTUBE ? "Fill in the channel and the API key" : src == BCC_GITHUB ? "Fill in a repo or a user" : "Fill in the site and the API key";
            o["state"] = "warn";
            continue;
        }
        if (!open){ o["value"] = "Set up"; o["state"] = "info"; continue; }
        const uint8_t state = c.state[src];
        t[0] = '\0';
        if (src == BCC_YOUTUBE && c.updated[src]){
            bcc_Format_Count(a, sizeof(a), c.subs, false);
            bcc_Format_Count(b, sizeof(b), c.views, false);
            if (c.subsHidden) snprintf(t, sizeof(t), "%s: subscribers hidden, %s views", c.channel, b);
            else snprintf(t, sizeof(t), "%s: %s subscribers, %s views, %u videos", c.channel, a, b, (unsigned)c.videos);
        } else if (src == BCC_GITHUB && c.updated[src]){
            k = 0;
            if (c.repoCount){ bcc_Format_Count(a, sizeof(a), c.stars, false); k += (size_t)snprintf(t, sizeof(t), "%s stars on %u repo%s", a, c.repoCount, c.repoCount == 1 ? "" : "s"); }
            if (s->ghUser[0]){ bcc_Format_Count(a, sizeof(a), c.followers, false); k += (size_t)snprintf(t + k, sizeof(t) - k, "%s%s followers", k ? ", " : "", a); }
            if (c.ghLimit) snprintf(t + k, sizeof(t) - k, ". GitHub allows %u requests an hour%s", c.ghLimit, s->ghToken[0] ? "" : " without a token");
        } else if (src == BCC_PLAUSIBLE && c.updated[src]){
            k = (size_t)snprintf(t, sizeof(t), "%d visitors now", c.live < 0 ? 0 : (int)c.live);
            if (c.visitors >= 0) snprintf(t + k, sizeof(t) - k, ", %d today, %d pageviews", (int)c.visitors, (int)c.pageviews);
        }
        if (state != BCC_OK && c.text[src][0]){
            const size_t n = strlen(t);
            snprintf(t + n, sizeof(t) - n, "%s%s", n ? ". Now: " : "", c.text[src]);
        } else if (state == BCC_OK && c.text[src][0]){
            const size_t n = strlen(t);
            snprintf(t + n, sizeof(t) - n, ". %s", c.text[src]);
        }
        o["value"] = t[0] ? t : (state == BCC_WAIT ? "Asking..." : "Waiting");
        o["state"] = state_Word(state);
    }
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    heap_caps_free(st);
    heap_caps_free(log);
}
