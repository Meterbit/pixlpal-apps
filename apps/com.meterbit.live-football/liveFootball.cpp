/**
 * Live Football app, ID {5,0}: live scores, goal celebrations, matchday boards and a countdown to your team's
 * next match, from API-Football.
 *
 * This file holds the app task: which card is on screen, goal and banner detection, the knob and the settings.
 * The data comes from fbl_Service.cpp, which paces its requests to the day's quota, and the screens are drawn by
 * fbl_Ui.cpp and fbl_Ui_Match.cpp.
 */
#include "mtb_engine.h"
#include <time.h>
#include <math.h>
#include <ctype.h>
#include "esp_heap_caps.h"
#include "fbl_Config.h"
#include "fbl_Service.h"
#include "fbl_Crest.h"
#include "fbl_Ui.h"
#include "fbl_Mobile_Ui.h"

static const char TAG[] = "LIVE_FOOTBALL";

#define FBL_CLOCK_VALID_AFTER   1700000000UL
#define FBL_MAX_CARDS           40
#define FBL_OVERLAYS            4
#define FBL_RECENT_SEC          (90UL * 60)     // A finished match stays on a live card this long.

EXT_RAM_BSS_ATTR TaskHandle_t liveFootball_Task_H = NULL;
void liveFootball_App_Task(void*);
static void fbl_Mobile_Ui_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar* liveFootbalScores_App;
Mtb_Applications* liveFootbalScores_App_GetInstance(){
    if (!liveFootbalScores_App){
        liveFootbalScores_App = new Mtb_Applications_StatusBar(liveFootball_App_Task, &liveFootball_Task_H, "Live Football", {5,0}, 8192);
        liveFootbalScores_App->mtb_App_Set_Mobile_UI(FBL_UI_MANIFEST, FBL_UI_API, FBL_UI_DATA, fbl_Mobile_Ui_Values);
    }
    return liveFootbalScores_App;
}
MTB_REGISTER_APP(liveFootbalScores_App, 5, 0)

EXT_RAM_BSS_ATTR static Fbl_Settings_t fblSettings;
static Fbl_Data_t* viewData = nullptr;      // PSRAM: the copy the screens draw.

static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t clicksRequested = 0;
static volatile bool longPressRequested = false;
static volatile bool settingsReceived = false;
static volatile bool sampleGoalRequested = false;
EXT_RAM_BSS_ATTR static Fbl_Settings_t receivedSettings;

static void fbl_Button(button_event_t button);
static void fbl_Ble_Leagues(JsonDocument& command);
static void fbl_Ble_Search(JsonDocument& command);
static void fbl_Ble_Teams(JsonDocument& command);
static void fbl_Ble_Quiet(JsonDocument& command);
static void fbl_Ble_Quiet_Times(JsonDocument& command);
static void fbl_Search_Answer(const char* query, const Fbl_Team_Found_t* teams, uint8_t count, const char* error);
static void fbl_Ble_Table(JsonDocument& command);
static void fbl_Ble_Key(JsonDocument& command);
static void fbl_Ble_Goals(JsonDocument& command);
static void fbl_Ble_Live(JsonDocument& command);
static void fbl_Ble_Pages(JsonDocument& command);
static void fbl_Ble_Refresh(JsonDocument& command);
static void fbl_Ble_Sample(JsonDocument& command);

//##############################################################################################################
static uint32_t fbl_Now(void){
    const time_t now = time(nullptr);
    return now > (time_t)FBL_CLOCK_VALID_AFTER ? (uint32_t)now : 0;
}

static bool fbl_Read_Saved_Settings(Fbl_Settings_t& settings){
    Fbl_Settings_t defaults;
    fbl_Settings_Default(defaults);
    settings = defaults;
    mtb_Read_Nvs_Struct(FBL_NVS_KEY, &settings, sizeof(Fbl_Settings_t));
    const bool saved = settings.magic == FBL_SETTINGS_MAGIC;
    if (!saved) settings = defaults;
    if (settings.apiKey[0] == '\0') memcpy(settings.apiKey, defaults.apiKey, sizeof(settings.apiKey));
    fbl_Settings_Clamp(settings);
    return saved;
}

static void fbl_Save_Settings(void){ mtb_Write_Nvs_Struct(FBL_NVS_KEY, &fblSettings, sizeof(Fbl_Settings_t)); }

static bool fbl_Is_Followed(uint16_t leagueId){
    for (uint8_t i = 0; i < fblSettings.followCount; i++) if (fblSettings.follow[i] == leagueId) return true;
    return false;
}

//##############################################################################################################
struct Fbl_Card_t { uint8_t screen; uint8_t board; uint8_t page; uint32_t matchId; };

// What there is to show, in rotation order: live matches (favourites first), the favourite's next match, today,
// yesterday's results, tomorrow, further ahead, the table, the scorers.
static int fbl_Build_Cards(const Fbl_View_t& base, Fbl_Card_t* cards, uint32_t now){
    int count = 0;
    const Fbl_Data_t& d = *viewData;
    for (int pass = 0; pass < 2; pass++){
        for (int i = 0; i < d.matchCount && count < FBL_MAX_CARDS; i++){
            const Fbl_Match_t& m = d.matches[i];
            const bool favourite = fbl_Match_Is_Favourite(m, fblSettings.favourites);
            if ((pass == 0) != favourite) continue;
            const uint8_t phase = fbl_Phase(m.status);
            const bool recent = (phase == FBL_PHASE_FINISHED || m.awaitingFinal) && m.goals[0] >= 0 && now < m.kickoff + FBL_MATCH_LENGTH_SEC + FBL_RECENT_SEC && (favourite || m.awaitingFinal);
            if (fbl_In_Play(m) || recent) cards[count++] = {FBL_SCREEN_LIVE, 0, 0, m.id};
        }
    }
    // The favourite's next match, within a week.
    const Fbl_Match_t* next = nullptr;
    for (int i = 0; i < d.matchCount; i++){
        const Fbl_Match_t& m = d.matches[i];
        if (fbl_Phase(m.status) == FBL_PHASE_UPCOMING && m.kickoff > now && m.kickoff - now < 7UL * 86400 && fbl_Match_Is_Favourite(m, fblSettings.favourites) && (!next || m.kickoff < next->kickoff)) next = &m;
    }
    if (next && count < FBL_MAX_CARDS) cards[count++] = {FBL_SCREEN_COUNTDOWN, 0, 0, next->id};
    static const uint8_t BOARDS[] = {FBL_BOARD_TODAY, FBL_BOARD_YESTERDAY, FBL_BOARD_TOMORROW, FBL_BOARD_AHEAD};
    for (uint8_t board : BOARDS){
        const uint8_t pages = fbl_Ui_Board_Pages(base, board);
        for (uint8_t p = 0; p < pages && count < FBL_MAX_CARDS; p++) cards[count++] = {FBL_SCREEN_BOARD, board, p, 0};
    }
    const uint8_t tablePages = fbl_Ui_Table_Pages(base);
    for (uint8_t p = 0; p < tablePages && count < FBL_MAX_CARDS; p++) cards[count++] = {FBL_SCREEN_TABLE, 0, p, 0};
    if (d.scorerCount && count < FBL_MAX_CARDS) cards[count++] = {FBL_SCREEN_SCORERS, 0, 0, 0};
    return count;
}

// When there is nothing to show, or something is wrong, what to say.
static bool fbl_Message(const Fbl_Status_t& status, bool online, uint32_t now, int cards, const char*& title, const char*& detail){
    if (status.keyMissing){ title = "Add your API key"; detail = "Get a free key at dashboard.api-football.com and paste it into Live Football in the Pixlpal app."; return true; }
    if (status.keyRejected){ title = "Key not accepted"; detail = "API-Football did not accept the key. Check it on your API-Football dashboard and send it again."; return true; }
    if (cards > 0) return false;
    if (!online){ title = "Offline"; detail = "Scores appear once Pixlpal is connected to the internet."; return true; }
    if (now == 0){ title = "Setting the clock"; detail = "Scores appear once Pixlpal knows the time."; return true; }
    if (status.quotaUsed){ title = "Daily limit reached"; detail = "All of today's API-Football requests are used. Scores come back after midnight UTC."; return true; }
    if (viewData->dayFetched[1] == 0){ title = "Kick-off soon"; detail = "Getting today's matches for your leagues..."; return true; }
    title = "No matches";
    detail = "None of your leagues or teams play yesterday, today or tomorrow. Add leagues in the Pixlpal app.";
    return true;
}

// Quiet hours, local time; a span past midnight (22:00 to 07:00) wraps.
static bool fbl_Quiet_Now(const Fbl_Settings_t& settings, uint32_t now){
    if (!settings.quietHours || now == 0 || settings.quietFrom == settings.quietTo) return false;
    const time_t t = (time_t)now;
    struct tm local;
    localtime_r(&t, &local);
    const uint16_t minute = (uint16_t)(local.tm_hour * 60 + local.tm_min);
    if (settings.quietFrom < settings.quietTo) return minute >= settings.quietFrom && minute < settings.quietTo;
    return minute >= settings.quietFrom || minute < settings.quietTo;
}

//##############################################################################################################
struct Fbl_Seen_t { uint32_t id; int8_t goals[2]; uint8_t reds; uint8_t phase; uint8_t awaiting; };
struct Fbl_Overlay_t { uint8_t screen; uint8_t banner; uint8_t side; uint32_t matchId; };

static uint8_t fbl_Reds(const Fbl_Match_t& m){
    uint8_t n = 0;
    for (int i = 0; i < m.eventCount; i++) if (m.events[i].type == FBL_EV_RED) n++;
    return n;
}

void liveFootball_App_Task(void* dApplication){
    Mtb_Applications *THIS_APP = (Mtb_Applications *)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(fbl_Button, mtb_Brightness_Control);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(fbl_Ble_Leagues, fbl_Ble_Search, fbl_Ble_Table, fbl_Ble_Key, fbl_Ble_Goals, fbl_Ble_Live,
                                       fbl_Ble_Pages, fbl_Ble_Refresh, fbl_Ble_Sample, fbl_Ble_Teams, fbl_Ble_Quiet, fbl_Ble_Quiet_Times);
    fbl_Service_Set_Search_Callback(fbl_Search_Answer);
    THIS_APP->mtb_App_Init();
    //************************************************************************************ */
    if (!fbl_Read_Saved_Settings(fblSettings)) fbl_Save_Settings();
    if (viewData == nullptr) viewData = (Fbl_Data_t*)heap_caps_calloc(1, sizeof(Fbl_Data_t), MALLOC_CAP_SPIRAM);
    Fbl_Card_t* cards = (Fbl_Card_t*)heap_caps_calloc(FBL_MAX_CARDS, sizeof(Fbl_Card_t), MALLOC_CAP_SPIRAM);
    Fbl_Seen_t* seen = (Fbl_Seen_t*)heap_caps_calloc(FBL_MAX_MATCHES, sizeof(Fbl_Seen_t), MALLOC_CAP_SPIRAM);
    if (viewData == nullptr || cards == nullptr || seen == nullptr){
        ESP_LOGE(TAG, "Out of memory");
        heap_caps_free(cards);
        heap_caps_free(seen);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    fbl_Service_Begin(fblSettings);
    clicksRequested = 0;
    longPressRequested = false;
    settingsReceived = false;
    sampleGoalRequested = false;

    uint32_t version = 0, crestVersion = 0, valuesPushMs = 0;
    int seenCount = -1;             // -1: the first data only sets what has been seen, no celebrations.
    Fbl_Overlay_t overlays[FBL_OVERLAYS];
    uint8_t overlayCount = 0;
    uint32_t overlayStartMs = 0;
    Fbl_Status_t status = {};
    int cardCount = 0, card = 0;
    uint32_t cardStartMs = millis(), lastAnimateMs = 0, frame = 0;
    uint8_t shownScreen = 0xFF;
    int shownCard = -1;
    int lastMinute = -1;
    bool redraw = true;

    for (;;){
        const uint32_t beforeCheckpoint = millis();
        if (THIS_APP_IS_ACTIVE != pdTRUE) break;
        if (millis() - beforeCheckpoint > 300) redraw = true;
        const uint32_t nowMs = millis();
        const uint32_t now = fbl_Now();
        const bool online = Mtb_Applications::internetConnectStatus == true;

        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            fblSettings = receivedSettings;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            fbl_Settings_Clamp(fblSettings);
            fbl_Save_Settings();
            fbl_Service_Set_Settings(fblSettings);
            redraw = true;
        }
        fbl_Service_Poll(online, now);
        const bool newData = fbl_Service_Copy(*viewData, version);
        fbl_Service_Get_Status(status);
        // The phone's status panel: sent when something changed (the engine skips identical values).
        if (newData || settingsReceived || nowMs - valuesPushMs >= 30000){
            valuesPushMs = nowMs;
            THIS_APP->mtb_App_Push_Mobile_Values();
        }
        const bool quiet = fbl_Quiet_Now(fblSettings, now);

        // Goals, red cards, kick-offs and final whistles since the last data.
        if (newData){
            redraw = true;
            for (int i = 0; i < viewData->matchCount; i++){
                const Fbl_Match_t& m = viewData->matches[i];
                const bool favourite = fbl_Match_Is_Favourite(m, fblSettings.favourites);
                const bool alerts = fblSettings.goalAlerts == FBL_ALERTS_ALL ? (favourite || fbl_Is_Followed(m.leagueId))
                                  : fblSettings.goalAlerts == FBL_ALERTS_FAVOURITE && favourite;
                Fbl_Seen_t* s = nullptr;
                for (int k = 0; k < (seenCount < 0 ? 0 : seenCount); k++) if (seen[k].id == m.id) s = &seen[k];
                const uint8_t phase = fbl_Phase(m.status), reds = fbl_Reds(m);
                if (s && alerts && !quiet){
                    for (int side = 0; side < 2; side++){
                        if (m.goals[side] > s->goals[side] && s->goals[side] >= 0 && overlayCount < FBL_OVERLAYS) overlays[overlayCount++] = {FBL_SCREEN_GOAL, 0, (uint8_t)side, m.id};
                    }
                    if (reds > s->reds && overlayCount < FBL_OVERLAYS) overlays[overlayCount++] = {FBL_SCREEN_BANNER, FBL_BANNER_RED, 0, m.id};
                }
                if (s && favourite && fblSettings.goalAlerts != FBL_ALERTS_OFF && !quiet){
                    if (s->phase == FBL_PHASE_UPCOMING && phase == FBL_PHASE_LIVE && overlayCount < FBL_OVERLAYS) overlays[overlayCount++] = {FBL_SCREEN_BANNER, FBL_BANNER_KICKOFF, 0, m.id};
                    const bool endedNow = (phase == FBL_PHASE_FINISHED && s->phase != FBL_PHASE_FINISHED && !s->awaiting) || (m.awaitingFinal && !s->awaiting);
                    if (endedNow && overlayCount < FBL_OVERLAYS) overlays[overlayCount++] = {FBL_SCREEN_BANNER, FBL_BANNER_FULLTIME, 0, m.id};
                }
            }
            // Remember this data.
            seenCount = 0;
            for (int i = 0; i < viewData->matchCount && seenCount < FBL_MAX_MATCHES; i++){
                const Fbl_Match_t& m = viewData->matches[i];
                seen[seenCount++] = {m.id, {m.goals[0], m.goals[1]}, fbl_Reds(m), fbl_Phase(m.status), m.awaitingFinal};
            }
        }
        if (sampleGoalRequested){
            sampleGoalRequested = false;
            for (int i = 0; i < viewData->matchCount; i++){
                if (viewData->matches[i].goals[0] < 0 && i + 1 < viewData->matchCount) continue;
                if (overlayCount < FBL_OVERLAYS) overlays[overlayCount++] = {FBL_SCREEN_GOAL, 0, 0, viewData->matches[i].id};
                break;
            }
        }
        if (fbl_Crest_Version() != crestVersion){ crestVersion = fbl_Crest_Version(); redraw = true; }

        // The base view, and the cards.
        Fbl_View_t view = {};
        view.data = viewData;
        view.settings = &fblSettings;
        view.status = &status;
        view.now = now ? now : (uint32_t)time(nullptr);
        view.clockValid = now != 0;
        cardCount = fbl_Build_Cards(view, cards, view.now);
        if (card >= cardCount) card = 0;

        // Overlay: a goal or a banner, then back to the cards.
        if (overlayCount > 0){
            if (overlayStartMs == 0){ overlayStartMs = nowMs; frame = 0; redraw = true; }
            const uint32_t length = overlays[0].screen == FBL_SCREEN_GOAL ? FBL_GOAL_SHOW_MS : FBL_BANNER_SHOW_MS;
            if (nowMs - overlayStartMs > length || fbl_Find_Match(*viewData, overlays[0].matchId) < 0){
                memmove(overlays, overlays + 1, sizeof(Fbl_Overlay_t) * --overlayCount);
                overlayStartMs = 0;
                redraw = true;
                continue;
            }
        }

        // Rotation: live cards stay twice as long.
        const bool liveCard = cardCount > 0 && cards[card].screen == FBL_SCREEN_LIVE;
        const uint32_t cardTime = fblSettings.pageSeconds * 1000UL * (liveCard ? 2 : 1);
        if (overlayCount == 0 && fblSettings.autoCycle && cardCount > 1 && nowMs - cardStartMs >= cardTime){
            card = (card + 1) % cardCount;
            cardStartMs = nowMs;
        }

        // The knob.
        const uint8_t clicks = clicksRequested;
        const bool longPress = longPressRequested;
        if (clicks || longPress){
            clicksRequested = 0;
            longPressRequested = false;
            redraw = true;
            if (overlayCount > 0){
                memmove(overlays, overlays + 1, sizeof(Fbl_Overlay_t) * --overlayCount);
                overlayStartMs = 0;
            } else if (longPress){
                fblSettings.autoCycle = !fblSettings.autoCycle;
                fbl_Save_Settings();
            } else if (clicks == 1 && cardCount) card = (card + 1) % cardCount;
            else if (clicks == 2 && cardCount){
                // To the next live match, or today's board.
                for (int k = 1; k <= cardCount; k++){
                    const int c = (card + k) % cardCount;
                    if (cards[c].screen == FBL_SCREEN_LIVE || (cards[c].screen == FBL_SCREEN_BOARD && cards[c].board == FBL_BOARD_TODAY && cards[c].page == 0)){ card = c; break; }
                }
            } else if (clicks == 3) ESP_LOGI(TAG, "Refresh %s", fbl_Service_Refresh_Now(now) ? "requested" : "refused");
            cardStartMs = nowMs;
        }

        // What to draw.
        const char *title, *detail;
        if (overlayCount > 0){
            view.screen = overlays[0].screen;
            view.match = fbl_Find_Match(*viewData, overlays[0].matchId);
            view.goalSide = overlays[0].side;
            view.banner = overlays[0].banner;
        } else if (fbl_Message(status, online, now, cardCount, title, detail)){
            view.screen = FBL_SCREEN_MESSAGE;
            view.message = title;
            view.detail = detail;
        } else {
            const Fbl_Card_t& c = cards[card];
            view.screen = c.screen;
            view.board = c.board;
            view.page = c.page;
            view.match = c.matchId ? fbl_Find_Match(*viewData, c.matchId) : -1;
            for (int k = 0; k < cardCount; k++) if (cards[k].screen == FBL_SCREEN_LIVE){ if (k < card) view.liveIndex++; view.liveCount++; }
        }
        if (view.screen != shownScreen || card != shownCard){ redraw = true; if (overlayCount == 0) frame = 0; }
        if (view.clockValid){
            const int minute = (int)(view.now / 60);
            if (minute != lastMinute){ lastMinute = minute; if (view.screen == FBL_SCREEN_BOARD || view.screen == FBL_SCREEN_LIVE) redraw = true; }
        }

        if (redraw){
            view.frame = frame;
            mtb_Panel_Begin_Update();
            fbl_Ui_Draw(view);
            mtb_Panel_End_Update();
            shownScreen = view.screen;
            shownCard = card;
            redraw = false;
            lastAnimateMs = nowMs;
        } else if (nowMs - lastAnimateMs >= 100){
            lastAnimateMs = nowMs;
            view.frame = ++frame;
            mtb_Panel_Begin_Update();
            fbl_Ui_Animate(view);
            mtb_Panel_End_Update();
        }
        delay(30);
    }
    heap_caps_free(cards);
    heap_caps_free(seen);
    mtb_Delete_This_App(THIS_APP);
}

//##############################################################################################################
// Click: next card. Double: next live match. Triple: refresh now. Long press: rotation on or off. On a goal or a
// banner, any click closes it.
static void fbl_Button(button_event_t button){
    switch (button.type){
        case BUTTON_PRESSED_LONG: longPressRequested = true; break;
        case BUTTON_CLICKED: if (button.count >= 1 && button.count <= 3) clicksRequested = (uint8_t)button.count; break;
        default: break;
    }
}

static void fbl_Ble_Begin_Change(Fbl_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    next = settingsReceived ? receivedSettings : fblSettings;
    portEXIT_CRITICAL(&requestLock);
}

static void fbl_Ble_End_Change(const Fbl_Settings_t& next){
    portENTER_CRITICAL(&requestLock);
    receivedSettings = next;
    settingsReceived = true;
    portEXIT_CRITICAL(&requestLock);
}

static Fbl_Settings_t* fbl_Ble_Settings(void){ return (Fbl_Settings_t*)heap_caps_malloc(sizeof(Fbl_Settings_t), MALLOC_CAP_SPIRAM); }

// The status panel: plan, what it includes, requests left, the live pace, the key. Only once the service has run.
static void fbl_Status_Values(JsonObject values){
    Fbl_Status_t status;
    fbl_Service_Get_Status(status);
    if (!status.started) return;
    const uint32_t now = fbl_Now();
    const Fbl_Quota_t& q = status.quota;
    const bool today = q.seen != 0 && now != 0 && q.seen / 86400 == now / 86400;
    values["paid"] = q.paid ? 1 : 0;
    if (status.keyMissing){ values["keyText"] = "No key: add one below"; values["keyState"] = "error"; }
    else if (status.keyRejected){ values["keyText"] = "Not accepted by API-Football"; values["keyState"] = "error"; }
    else if (q.seen){ values["keyText"] = "Accepted"; values["keyState"] = "ok"; }
    if (q.seen){
        values["plan"] = q.plan[0] ? q.plan : "Free";
        values["planState"] = q.paid ? "ok" : "info";
        if (!q.paid) values["features"] = "Live scores, yesterday, today and tomorrow";
        else if (status.locked) values["features"] = "Live, fixtures ahead; table or scorers not in this plan";
        else values["features"] = "Live, fixtures ahead, table and top scorers";
    }
    if (today){
        values["left"] = q.exhausted ? 0 : q.remaining;
        values["limit"] = q.limitDay;
        values["quotaState"] = (q.exhausted || q.remaining == 0) ? "error" : q.remaining <= FBL_RESERVE_REQUESTS ? "warn" : "ok";
    }
    char pace[40];
    if (status.quotaUsed){ values["pace"] = "Paused until midnight UTC"; values["paceState"] = "error"; }
    else if (status.liveInterval){
        if (status.liveInterval < 120) snprintf(pace, sizeof(pace), "Every %u s", (unsigned)status.liveInterval);
        else snprintf(pace, sizeof(pace), "Every %u min", (unsigned)((status.liveInterval + 30) / 60));
        values["pace"] = pace;
        values["paceState"] = "ok";
    } else { values["pace"] = "Waiting for a match to start"; values["paceState"] = "off"; }
    if (status.updated) values["updated"] = status.updated;
}

// What the phone UI shows when it opens. The key itself never leaves the device. Runs on the BLE parser task.
static void fbl_Mobile_Ui_Values(JsonObject values){
    Fbl_Settings_t* s = fbl_Ble_Settings();
    if (s == nullptr) return;
    fbl_Read_Saved_Settings(*s);
    portENTER_CRITICAL(&requestLock);
    if (settingsReceived) *s = receivedSettings;
    portEXIT_CRITICAL(&requestLock);
    JsonArray leagues = values["leagues"].to<JsonArray>();
    for (uint8_t i = 0; i < s->followCount; i++) leagues.add(String(s->follow[i]));
    JsonArray teams = values["teams"].to<JsonArray>();
    for (uint8_t i = 0; i < s->favourites.count; i++){
        JsonObject team = teams.add<JsonObject>();
        team["name"] = s->favourites.team[i].name;
        team["value"] = s->favourites.team[i].id;
    }
    values["quiet"] = s->quietHours ? 1 : 0;
    char clock[8];
    snprintf(clock, sizeof(clock), "%02u:%02u", s->quietFrom / 60, s->quietFrom % 60);
    values["quietFrom"] = clock;
    snprintf(clock, sizeof(clock), "%02u:%02u", s->quietTo / 60, s->quietTo % 60);
    values["quietTo"] = clock;
    values["table"] = String(s->tableLeague);
    values["goals"] = s->goalAlerts;
    values["liveMinutes"] = s->liveMinutes;
    values["cycle"] = s->autoCycle ? 1 : 0;
    values["seconds"] = s->pageSeconds;
    values["api_key_set"] = s->apiKey[0] ? 1 : 0;
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);
    fbl_Status_Values(values);
}

static bool fbl_Ble_Number(JsonVariant field, long& value){
    if (field.is<bool>()) value = field.as<bool>() ? 1 : 0;
    else if (field.is<long>()) value = field.as<long>();
    else if (field.is<double>()) value = lround(field.as<double>());
    else if (field.is<const char*>()){
        char* end = nullptr;
        value = strtol(field.as<const char*>(), &end, 10);
        if (end == field.as<const char*>() || *end != '\0') return false;
    } else return false;
    return true;
}

// Command 0: {"leagues": ["39", "140"]}: 1 to 8 known leagues. Older phone apps send "[39, 140]".
static void fbl_Ble_Leagues(JsonDocument& command){
    uint16_t ids[FBL_MAX_FOLLOW];
    int count = 0;
    bool valid = true;
    auto add = [&](long id){
        if (!fbl_Find_League((uint16_t)id) || id <= 0 || id > 65535){ valid = false; return; }
        for (int k = 0; k < count; k++) if (ids[k] == id) return;
        if (count >= FBL_MAX_FOLLOW){ valid = false; return; }
        ids[count++] = (uint16_t)id;
    };
    JsonVariant leagues = command["leagues"];
    if (leagues.is<JsonArray>()){
        for (JsonVariant item : leagues.as<JsonArray>()){ long id; if (fbl_Ble_Number(item, id)) add(id); else valid = false; }
    } else if (leagues.is<const char*>()){
        for (const char* c = leagues.as<const char*>(); *c; ){
            if (isdigit((unsigned char)*c)){ char* end; add(strtol(c, &end, 10)); c = end; }
            else if (strchr("[], \"", *c)) c++;
            else { valid = false; break; }
        }
    } else valid = false;
    if (!valid || count == 0){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->followCount = (uint8_t)count;
    memcpy(next->follow, ids, sizeof(uint16_t) * count);
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 1: {"query": "arsenal"}: a team search, answered later (command 248) with the teams found. Refused for
// a query of fewer than 3 or more than 30 letters, digits, spaces and . - ' &, without a key or requests left, or
// while another search waits.
static void fbl_Ble_Search(JsonDocument& command){
    const char* query = command["query"];
    if (!fbl_Service_Search(query, fbl_Now())) mtb_Ble_App_Cmd_Reject();
}

static void fbl_Search_Answer(const char* query, const Fbl_Team_Found_t* teams, uint8_t count, const char* error){
    JsonDocument doc;
    JsonArray results = doc.to<JsonArray>();
    for (uint8_t i = 0; i < count; i++){
        const Fbl_Team_Found_t& t = teams[i];
        JsonObject item = results.add<JsonObject>();
        item["name"] = t.name;
        item["value"] = t.id;
        char detail[48];
        if (t.national) snprintf(detail, sizeof(detail), "National team%s%s", t.country[0] ? " - " : "", t.country);
        else if (t.founded) snprintf(detail, sizeof(detail), "%s%sfounded %u", t.country, t.country[0] ? " - " : "", t.founded);
        else snprintf(detail, sizeof(detail), "%s", t.country);
        item["detail"] = detail;
        char image[72];
        snprintf(image, sizeof(image), FBL_CREST_URL, (unsigned)t.id);
        item["image"] = image;
    }
    if (liveFootbalScores_App) liveFootbalScores_App->mtb_App_Send_Search_Results("teams", query, results, error);
}

// Command 9: {"teams": [{"name": "Arsenal", "value": 42}, ...]}: the favourite teams, up to 8; [] clears.
static void fbl_Ble_Teams(JsonDocument& command){
    JsonVariant teams = command["teams"];
    if (!teams.is<JsonArray>() || teams.size() > FBL_MAX_FAVOURITES){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Favourites_t picked = {};
    for (JsonVariant item : teams.as<JsonArray>()){
        long id;
        const char* name = item["name"];
        if (!fbl_Ble_Number(item["value"], id) || id <= 0 || name == nullptr || name[0] == '\0'){ mtb_Ble_App_Cmd_Reject(); return; }
        bool repeated = false;
        for (uint8_t k = 0; k < picked.count; k++) if (picked.team[k].id == (uint32_t)id) repeated = true;
        if (repeated) continue;
        picked.team[picked.count].id = (uint32_t)id;
        snprintf(picked.team[picked.count].name, sizeof(picked.team[0].name), "%s", name);
        picked.count++;
    }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->favourites = picked;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
    for (uint8_t i = 0; i < picked.count; i++) fbl_Crest_Want(picked.team[i].id);
}

// Command 10: {"quiet": 1}: no celebrations during quiet hours.
static void fbl_Ble_Quiet(JsonDocument& command){
    long value;
    if (!fbl_Ble_Number(command["quiet"], value) || value < 0 || value > 1){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->quietHours = (uint8_t)value;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// "HH:MM", 24-hour, to minutes after midnight; -1 if not one.
static int fbl_Minutes_Of(const char* text){
    if (text == nullptr || strlen(text) != 5 || text[2] != ':') return -1;
    for (int i : {0, 1, 3, 4}) if (!isdigit((unsigned char)text[i])) return -1;
    const int hours = (text[0] - '0') * 10 + (text[1] - '0');
    const int minutes = (text[3] - '0') * 10 + (text[4] - '0');
    return hours < 24 && minutes < 60 ? hours * 60 + minutes : -1;
}

// Command 11: {"quietFrom": "22:00", "quietTo": "07:00"}
static void fbl_Ble_Quiet_Times(JsonDocument& command){
    const int from = fbl_Minutes_Of(command["quietFrom"]);
    const int to = fbl_Minutes_Of(command["quietTo"]);
    if (from < 0 || to < 0 || from == to){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->quietFrom = (uint16_t)from;
    next->quietTo = (uint16_t)to;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 2: {"table": "39"}
static void fbl_Ble_Table(JsonDocument& command){
    long id;
    if (!fbl_Ble_Number(command["table"], id) || id <= 0 || id > 65535 || !fbl_Find_League((uint16_t)id)){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->tableLeague = (uint16_t)id;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 3: {"api_key": "..."}: API-Football keys are 32 hex characters; letters and digits, 16 to 64, accepted.
static void fbl_Ble_Key(JsonDocument& command){
    const char* key = command["api_key"];
    const size_t length = key ? strlen(key) : 0;
    bool valid = length >= 16 && length <= 64;
    for (size_t i = 0; valid && i < length; i++) if (!isalnum((unsigned char)key[i])) valid = false;
    if (!valid){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    memcpy(next->apiKey, key, length + 1);
    fbl_Ble_End_Change(*next);
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

// Command 4: {"goals": 0-2}
static void fbl_Ble_Goals(JsonDocument& command){
    long value;
    if (!fbl_Ble_Number(command["goals"], value) || value < 0 || value > FBL_ALERTS_ALL){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->goalAlerts = (uint8_t)value;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 5: {"liveMinutes": 1-15}
static void fbl_Ble_Live(JsonDocument& command){
    long value;
    if (!fbl_Ble_Number(command["liveMinutes"], value) || value < FBL_LIVE_MIN_LOWEST || value > FBL_LIVE_MIN_HIGHEST){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    next->liveMinutes = (uint8_t)value;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 6: {"cycle": 1} and/or {"seconds": 10}
static void fbl_Ble_Pages(JsonDocument& command){
    long cycle, seconds;
    const bool hasCycle = fbl_Ble_Number(command["cycle"], cycle);
    const bool hasSeconds = fbl_Ble_Number(command["seconds"], seconds);
    if ((!hasCycle && !hasSeconds) || (hasSeconds && (seconds < FBL_PAGE_SEC_LOWEST || seconds > FBL_PAGE_SEC_HIGHEST))){ mtb_Ble_App_Cmd_Reject(); return; }
    Fbl_Settings_t* next = fbl_Ble_Settings();
    if (next == nullptr){ mtb_Ble_App_Cmd_Reject(); return; }
    fbl_Ble_Begin_Change(*next);
    if (hasCycle) next->autoCycle = cycle != 0;
    if (hasSeconds) next->pageSeconds = (uint8_t)seconds;
    fbl_Ble_End_Change(*next);
    heap_caps_free(next);
}

// Command 7: {} - refresh now. Refused within two minutes of the last one, or with the day's quota used up.
static void fbl_Ble_Refresh(JsonDocument&){
    if (!fbl_Service_Refresh_Now(fbl_Now())) mtb_Ble_App_Cmd_Reject();
}

// Command 8: {} - a goal celebration on the display, with a match in the list.
static void fbl_Ble_Sample(JsonDocument&){
    if (viewData == nullptr || viewData->matchCount == 0){ mtb_Ble_App_Cmd_Reject(); return; }
    sampleGoalRequested = true;
}
