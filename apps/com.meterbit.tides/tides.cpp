/**
 * Tides & Water, ID {3,3} (WEATHER): for homes by the sea, a lagoon, a lake or a river. The tide now with the next
 * highs and lows and a 24-hour curve, a table of the coming days, the sea (waves, swell, water temperature, currents),
 * wind, rain and thunder, the river's flow against its last 10 years, sun, moon and spring tides. Alerts for high
 * water, rough sea, strong gusts, thunder and a high river, on the display and (optionally) on phones through ntfy,
 * Telegram or WhatsApp.
 *
 * The knob: click or turn, the next page (back by turning left); two clicks, read again now; hold, pages turn by
 * themselves or stay.
 */
#include "mtb_engine.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_json.h"
#include "mtb_text_scroll.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ntf_Notify.h"
#include "tdw_App.h"
#include "tdw_Mobile_Ui.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Tides and Water)"

EXT_RAM_BSS_ATTR TaskHandle_t tides_Task_H = NULL;
static void tides_App_Task(void* dApplication);
static void tdw_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_StatusBar *tides_App;
Mtb_Applications* tides_App_GetInstance() {
    if (!tides_App){
        tides_App = new Mtb_Applications_StatusBar(tides_App_Task, &tides_Task_H, "Tides and Water", {3,3}, 8192);
        tides_App->mtb_App_Set_Mobile_UI(TDW_UI_MANIFEST, TDW_UI_API, nullptr, tdw_Values);
    }
    return tides_App;
}
MTB_REGISTER_APP(tides_App, 3, 3)

EXT_RAM_BSS_ATTR static Tdw_Settings_t settings;
EXT_RAM_BSS_ATTR static Tdw_Settings_t received;
static portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool settingsReceived = false, appOpen = false, refreshRequested = false, testRequested = false, longPressRequested = false;
static volatile uint8_t clicksRequested = 0;
static volatile int turns = 0;

// Alerts already told, kept while Pixlpal runs (also across app cycling), so each goes out once.
struct Sent_t { uint8_t kind; uint32_t slot; uint32_t sentAt; };
EXT_RAM_BSS_ATTR static Sent_t sent[16];
// The alerts in force, for the phone (the BLE task reads them).
EXT_RAM_BSS_ATTR static Tdw_Alert_t shownAlerts[TDW_ALERTS_MAX];
static volatile uint8_t shownCount = 0;

static bool read_Settings(Tdw_Settings_t& s){
    tdw_Settings_Default(s);
    Tdw_Settings_t* saved = (Tdw_Settings_t*)heap_caps_calloc(1, sizeof(Tdw_Settings_t), MALLOC_CAP_SPIRAM);
    if (!saved) return false;
    mtb_Read_Nvs_Struct(TDW_NVS_KEY, saved, sizeof(*saved));
    const bool ok = saved->magic == TDW_SETTINGS_MAGIC;
    if (ok) s = *saved;
    memset(saved, 0, sizeof(*saved));
    heap_caps_free(saved);
    tdw_Settings_Clamp(s);
    return ok;
}

// Phone commands (the BLE task) -----------------------------------------------------------------------------------------------
static Tdw_Settings_t* begin_Change(void){
    Tdw_Settings_t* next = (Tdw_Settings_t*)heap_caps_malloc(sizeof(Tdw_Settings_t), MALLOC_CAP_SPIRAM);
    if (!next) return nullptr;
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        *next = settingsReceived ? received : settings;
        portEXIT_CRITICAL(&requestLock);
    } else read_Settings(*next);
    return next;
}

static void drop_Change(Tdw_Settings_t* next){
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
    mtb_Ble_App_Cmd_Reject();
}

static void end_Change(Tdw_Settings_t* next){
    tdw_Settings_Clamp(*next);
    if (appOpen){
        portENTER_CRITICAL(&requestLock);
        received = *next;
        settingsReceived = true;
        portEXIT_CRITICAL(&requestLock);
    } else mtb_Write_Nvs_Struct(TDW_NVS_KEY, next, sizeof(*next));     // closed: saved now, used when it opens
    memset(next, 0, sizeof(*next));
    heap_caps_free(next);
}

static bool clean(const char* in, char* out, size_t size){
    while (*in == ' ') in++;
    size_t n = strlen(in);
    while (n && in[n - 1] == ' ') n--;
    if (n >= size) return false;
    for (size_t i = 0; i < n; i++) if ((unsigned char)in[i] < ' ') return false;
    memcpy(out, in, n);
    out[n] = '\0';
    return true;
}

// A search: coordinates are answered at once; names go to Open-Meteo's geocoding while the app runs.
static void search(JsonDocument& c, const char* id){
    const char* q = c["query"] | "";
    if (strlen(q) < 2 || strlen(q) > 60){ mtb_Ble_App_Cmd_Reject(); return; }
    double lat, lon;
    if (tdw_Parse_Coordinates(q, lat, lon)){
        JsonDocument out;
        JsonArray results = out.to<JsonArray>();
        JsonObject o = results.add<JsonObject>();
        char name[48], value[48];
        snprintf(name, sizeof(name), "%.4f, %.4f", lat, lon);
        snprintf(value, sizeof(value), "%.5f,%.5f", lat, lon);
        o["name"] = name;
        o["value"] = value;
        o["detail"] = "Coordinates";
        if (tides_App) tides_App->mtb_App_Send_Search_Results(id, q, results);
        return;
    }
    if (!appOpen || !tdw_Net_Search(id, q)) mtb_Ble_App_Cmd_Reject();
}
static void cmd_Search_Place(JsonDocument& c){ search(c, "places"); }
static void cmd_Search_River(JsonDocument& c){ search(c, "rivers"); }

/** {name, value: "lat,lng"} into a place. False if it is not one. */
static bool picked(JsonVariantConst v, char* name, size_t size, double& lat, double& lon){
    const char* value = v["value"] | "";
    return tdw_Parse_Coordinates(value, lat, lon) && clean(v["name"] | value, name, size) && name[0];
}

static void cmd_Place(JsonDocument& c){
    char name[TDW_PLACE_LEN];
    double lat, lon;
    if (!picked(c["place"], name, sizeof(name), lat, lon)){ mtb_Ble_App_Cmd_Reject(); return; }
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    snprintf(n->place, sizeof(n->place), "%s", name);
    n->lat = lat;
    n->lon = lon;
    n->noaaChecked = 0;                 // a new place: find its NOAA station again
    n->noaaId[0] = n->noaaName[0] = '\0';
    end_Change(n);
}

static void cmd_River_Place(JsonDocument& c){
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    JsonVariantConst v = c["riverPlace"];
    if (v.isNull() || (v.is<JsonObjectConst>() && v.size() == 0)){ n->riverPlace[0] = '\0'; end_Change(n); return; }
    char name[TDW_PLACE_LEN];
    double lat, lon;
    if (!picked(v, name, sizeof(name), lat, lon)){ drop_Change(n); return; }
    snprintf(n->riverPlace, sizeof(n->riverPlace), "%s", name);
    n->riverLat = lat;
    n->riverLon = lon;
    end_Change(n);
}

static void cmd_Source(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["source"], TDW_SRC_AUTO, TDW_SRC_OPENMETEO, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    n->source = (uint8_t)v;
    end_Change(n);
}

static void cmd_Units(JsonDocument& c){
    long v;
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    if (!c["datum"].isNull() && mtb_Kit_Json_Range(c["datum"], 0, 1, v)) n->datum = (uint8_t)v;
    else if (!c["imperial"].isNull() && mtb_Kit_Json_Range(c["imperial"], 0, 1, v)) n->imperial = (uint8_t)v;
    else if (!c["windUnit"].isNull() && mtb_Kit_Json_Range(c["windUnit"], 0, TDW_MS, v)) n->windUnit = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

static void cmd_Display(JsonDocument& c){
    long v;
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char key[4] = "p0";
    for (int i = 0; i < TDW_P_ALERTS; i++){
        key[1] = (char)('0' + i);
        if (c[key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[key], 0, 1, v)){ drop_Change(n); return; }
        if (v) n->pages = (uint8_t)(n->pages | (1u << i)); else n->pages = (uint8_t)(n->pages & ~(1u << i));
        end_Change(n);
        return;
    }
    if (!c["cycle"].isNull() && mtb_Kit_Json_Range(c["cycle"], 0, 1, v)) n->cycle = (uint8_t)v;
    else if (!c["seconds"].isNull() && mtb_Kit_Json_Range(c["seconds"], 4, 60, v)) n->seconds = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

/** A height the phone sends as 1.8 or 1 (metres or feet), into centimetres. */
static bool height_Cm(JsonVariantConst v, float perUnitCm, float maxUnits, uint16_t& cm){
    double d;
    if (v.is<long>() || v.is<double>()) d = v.as<double>();
    else return false;
    if (!(d >= 0 && d <= maxUnits)) return false;
    cm = (uint16_t)lround(d * perUnitCm);
    return true;
}

static void cmd_Alerts(JsonDocument& c){
    long v;
    uint16_t cm;
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    struct Flag_t { const char* key; uint8_t* field; };
    const Flag_t flags[] = {{"alertOn", &n->alertOn}, {"stormAlert", &n->stormAlert}, {"phoneAlerts", &n->phoneAlerts},
                            {"ntfyOn", &n->notify.on[NTF_NTFY]}, {"telegramOn", &n->notify.on[NTF_TELEGRAM]}, {"whatsappOn", &n->notify.on[NTF_WHATSAPP]}};
    for (const Flag_t& f : flags){
        if (c[f.key].isNull()) continue;
        if (!mtb_Kit_Json_Range(c[f.key], 0, 1, v)){ drop_Change(n); return; }
        *f.field = (uint8_t)v;
        end_Change(n);
        return;
    }
    if (!c["highWaterM"].isNull()){ if (!height_Cm(c["highWaterM"], 100.0f, 20.0f, cm)){ drop_Change(n); return; } n->highWaterCm = cm; }
    else if (!c["highWaterFt"].isNull()){ if (!height_Cm(c["highWaterFt"], 30.48f, 65.0f, cm)){ drop_Change(n); return; } n->highWaterCm = cm; }
    else if (!c["waveAlert"].isNull() && mtb_Kit_Json_Range(c["waveAlert"], 0, 100, v)) n->waveAlertDm = (uint8_t)v;
    else if (!c["gustAlert"].isNull() && mtb_Kit_Json_Range(c["gustAlert"], 0, 200, v)) n->gustAlert = (uint16_t)v;
    else if (!c["riverAlert"].isNull() && mtb_Kit_Json_Range(c["riverAlert"], 0, TDW_RIVER_EXTREME, v)
             && (v == 0 || v >= TDW_RIVER_HIGH)) n->riverAlert = (uint8_t)v;
    else if (!c["leadHours"].isNull() && mtb_Kit_Json_Range(c["leadHours"], 1, 24, v)) n->leadHours = (uint8_t)v;
    else { drop_Change(n); return; }
    end_Change(n);
}

// Keys: kept on Pixlpal, never sent back. Empty forgets one.
static void cmd_Key(JsonDocument& c){
    const bool wt = !c["worldTidesKey"].isNull();
    if (!wt && c["stormglassKey"].isNull()){ mtb_Ble_App_Cmd_Reject(); return; }
    char k[96];
    bool ok = clean(wt ? (c["worldTidesKey"] | "") : (c["stormglassKey"] | ""), k, wt ? sizeof(settings.worldTidesKey) : sizeof(settings.stormglassKey));
    const size_t len = ok ? strlen(k) : 0;
    ok = ok && (len == 0 || len >= 16);
    for (size_t i = 0; ok && i < len; i++) if (!isalnum((unsigned char)k[i]) && k[i] != '-' && k[i] != '_') ok = false;
    if (!ok){ memset(k, 0, sizeof(k)); mtb_Ble_App_Cmd_Reject(); return; }
    Tdw_Settings_t* n = begin_Change();
    if (!n){ memset(k, 0, sizeof(k)); mtb_Ble_App_Cmd_Reject(); return; }
    if (wt) snprintf(n->worldTidesKey, sizeof(n->worldTidesKey), "%s", k);
    else snprintf(n->stormglassKey, sizeof(n->stormglassKey), "%s", k);
    memset(k, 0, sizeof(k));
    end_Change(n);
}

// {"ntfyTopic"} | {"ntfyServer"} | {"telegramChat"} | {"whatsappPhone"}
static void cmd_Notify_Text(JsonDocument& c){
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[80];
    bool ok = false;
    Ntf_Config_t& k = n->notify;
    if (!c["ntfyServer"].isNull()){
        ok = clean(c["ntfyServer"] | "", t, sizeof(t));
        const char* h = !strncmp(t, "https://", 8) ? t + 8 : t;
        char host[80];
        snprintf(host, sizeof(host), "%s", h[0] ? h : "ntfy.sh");
        char* slash = strchr(host, '/');
        if (slash) *slash = '\0';
        ok = ok && strlen(host) < sizeof(k.ntfyServer) && ntf_Valid_Server(host);
        if (ok) snprintf(k.ntfyServer, sizeof(k.ntfyServer), "%s", host);
    }
    else if (!c["ntfyTopic"].isNull()){ ok = clean(c["ntfyTopic"] | "", t, sizeof(k.ntfyTopic)) && t[0] && ntf_Valid_Topic(t); if (ok) snprintf(k.ntfyTopic, sizeof(k.ntfyTopic), "%s", t); }
    else if (!c["telegramChat"].isNull()){ ok = clean(c["telegramChat"] | "", t, sizeof(k.telegramChat)) && ntf_Valid_Chat(t); if (ok) snprintf(k.telegramChat, sizeof(k.telegramChat), "%s", t); }
    else if (!c["whatsappPhone"].isNull()){ ok = clean(c["whatsappPhone"] | "", t, sizeof(k.whatsappPhone)) && ntf_Valid_Phone(t); if (ok) snprintf(k.whatsappPhone, sizeof(k.whatsappPhone), "%s", t); }
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

static void cmd_Notify_Secret(JsonDocument& c){
    Tdw_Settings_t* n = begin_Change();
    if (!n){ mtb_Ble_App_Cmd_Reject(); return; }
    char t[80];
    bool ok = false;
    Ntf_Config_t& k = n->notify;
    if (!c["ntfyToken"].isNull()){ ok = clean(c["ntfyToken"] | "", t, sizeof(k.ntfyToken)); if (ok) snprintf(k.ntfyToken, sizeof(k.ntfyToken), "%s", t); }
    else if (!c["telegramToken"].isNull()){ ok = clean(c["telegramToken"] | "", t, sizeof(k.telegramToken)) && (!t[0] || strchr(t, ':')); if (ok) snprintf(k.telegramToken, sizeof(k.telegramToken), "%s", t); }
    else if (!c["whatsappKey"].isNull()){ ok = clean(c["whatsappKey"] | "", t, sizeof(k.whatsappKey)); if (ok) snprintf(k.whatsappKey, sizeof(k.whatsappKey), "%s", t); }
    memset(t, 0, sizeof(t));
    if (!ok){ drop_Change(n); return; }
    end_Change(n);
}

static void cmd_Refresh(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } refreshRequested = true; }

static void cmd_Test(JsonDocument&){
    bool ready = false;
    portENTER_CRITICAL(&requestLock);
    const Ntf_Config_t k = (settingsReceived ? received : settings).notify;
    portEXIT_CRITICAL(&requestLock);
    for (uint8_t ch = 0; ch < NTF_CHANNELS; ch++) if (ntf_Ready(k, ch)) ready = true;
    if (!appOpen || !ready){ mtb_Ble_App_Cmd_Reject(); return; }
    testRequested = true;
}

// The knob ------------------------------------------------------------------------------------------------------------------
static void on_Button(button_event_t b){
    if (b.type == BUTTON_PRESSED_LONG) longPressRequested = true;
    else if (b.type == BUTTON_CLICKED && b.count >= 1 && b.count <= 2) clicksRequested = (uint8_t)b.count;
}
static void on_Turn(rotary_encoder_rotation_t direction){ turns += direction == ROT_CLOCKWISE ? 1 : -1; }

// The app -------------------------------------------------------------------------------------------------------------------
static bool page_Has_Data(const Tdw_Data_t& d, uint8_t p){
    switch (p){
        case TDW_P_TIDE: case TDW_P_TABLE: return d.tide.state != TDW_NONE;
        case TDW_P_SEA: return d.sea.known || !d.sea.updated;
        case TDW_P_RIVER: return d.river.known || !d.river.updated;
        case TDW_P_ALERTS: return d.alertCount > 0;
        default: return true;
    }
}

static bool page_Shown(const Tdw_Settings_t& s, const Tdw_Data_t& d, uint8_t p){
    if (p == TDW_P_ALERTS) return s.alertOn && d.alertCount > 0;
    return (s.pages & (1u << p)) && page_Has_Data(d, p);
}

/** True if this alert was not told yet (and remembers it). */
static bool first_Time(const Tdw_Alert_t& a, uint32_t now){
    const uint32_t slot = a.at / (3 * 3600);   // the same event a little earlier or later is the same alert
    int free = -1;
    for (int i = 0; i < 16; i++){
        if (sent[i].sentAt && now - sent[i].sentAt > 24 * 3600) sent[i].sentAt = 0;
        if (!sent[i].sentAt){ if (free < 0) free = i; continue; }
        if (sent[i].kind == a.kind && (sent[i].slot == slot || sent[i].slot + 1 == slot || sent[i].slot == slot + 1)) return false;
    }
    if (free < 0){ free = 0; for (int i = 1; i < 16; i++) if (sent[i].sentAt < sent[free].sentAt) free = i; }
    sent[free] = {a.kind, slot, now};
    return true;
}

static void tides_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Search_Place, cmd_Place, cmd_Search_River, cmd_River_Place, cmd_Source, cmd_Units,
                                       cmd_Display, cmd_Alerts, cmd_Key, cmd_Notify_Text, cmd_Notify_Secret, cmd_Refresh);
    THIS_APP->mtb_App_Set_Ble_Comm_Fn(12, cmd_Test);
    THIS_APP->mtb_App_Init();
    const bool had = read_Settings(settings);
    // A hard-to-guess ntfy topic of its own, the first time.
    if (!settings.notify.ntfyTopic[0])
        snprintf(settings.notify.ntfyTopic, sizeof(settings.notify.ntfyTopic), "pixlpal-water-%08lx%04lx", (unsigned long)esp_random(),
                 (unsigned long)(esp_random() & 0xFFFF));
    (void)had;
    mtb_Write_Nvs_Struct(TDW_NVS_KEY, &settings, sizeof(settings));
    shownCount = 0;
    settingsReceived = false;
    refreshRequested = false;
    testRequested = false;
    longPressRequested = false;
    clicksRequested = 0;
    turns = 0;
    Tdw_Data_t* d = (Tdw_Data_t*)heap_caps_calloc(1, sizeof(Tdw_Data_t), MALLOC_CAP_SPIRAM);
    Tdw_Places_t* places = (Tdw_Places_t*)heap_caps_calloc(1, sizeof(Tdw_Places_t), MALLOC_CAP_SPIRAM);
    if (!d || !places || !tdw_Net_Start(settings)){
        heap_caps_free(d);
        heap_caps_free(places);
        mtb_Delete_This_App(THIS_APP);
        return;
    }
    ntf_Start(settings.notify, UA, "ocean");
    appOpen = true;
    uint8_t page = TDW_P_TIDE;
    uint32_t version = 0, pageSince = millis(), phoneMs = 0, lastSecond = 0, alertCheck = 0;
    bool redraw = true, phoneDirty = true, settingsDirty = false;

    auto step_Page = [&](int dir){
        for (int i = 0; i < TDW_PAGES; i++){
            page = (uint8_t)((page + TDW_PAGES + dir) % TDW_PAGES);
            if (page_Shown(settings, *d, page)) break;
        }
        pageSince = millis();
        redraw = true;
    };

    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        const uint32_t now = (uint32_t)time(nullptr);
        if (settingsReceived){
            portENTER_CRITICAL(&requestLock);
            settings = received;
            settingsReceived = false;
            portEXIT_CRITICAL(&requestLock);
            tdw_Net_Set_Settings(settings);
            ntf_Set_Config(settings.notify);
            settingsDirty = redraw = phoneDirty = true;
            alertCheck = 0;
        }
        if (refreshRequested){ refreshRequested = false; tdw_Net_Refresh(); }
        if (tdw_Net_Copy(*d, version)){ redraw = phoneDirty = true; alertCheck = 0; }

        // The worker's news for the settings.
        Tdw_Station_t st;
        if (tdw_Net_Take_Station(st)){
            settings.noaaChecked = st.checked;
            snprintf(settings.noaaId, sizeof(settings.noaaId), "%s", st.id);
            snprintf(settings.noaaName, sizeof(settings.noaaName), "%s", st.name);
            settings.noaaKm = st.km;
            settingsDirty = phoneDirty = true;
        }
        uint32_t credits;
        if (tdw_Net_Take_Credits(credits)){
            const time_t tt = (time_t)now;
            struct tm lt;
            localtime_r(&tt, &lt);
            const uint32_t month = (uint32_t)(lt.tm_year * 12 + lt.tm_mon);
            if (settings.wtMonth != month){ settings.wtMonth = month; settings.wtCredits = 0; }
            settings.wtCredits += credits;
            settingsDirty = phoneDirty = true;
        }
        // Search answers for the phone.
        if (tdw_Net_Take_Places(*places)){
            JsonDocument out;
            JsonArray results = out.to<JsonArray>();
            for (int i = 0; i < places->count; i++){
                JsonObject o = results.add<JsonObject>();
                char value[48], detail[96];
                snprintf(value, sizeof(value), "%.5f,%.5f", places->lats[i], places->lons[i]);
                snprintf(detail, sizeof(detail), "%s%s%.2f, %.2f", places->details[i], places->details[i][0] ? ", " : "", places->lats[i], places->lons[i]);
                o["name"] = places->names[i];
                o["value"] = value;
                o["detail"] = detail;
            }
            THIS_APP->mtb_App_Send_Search_Results(places->searchId, places->query, results,
                                                  places->count < 0 ? "The place search did not answer: try again" : nullptr);
        }

        // Alerts: worked out each minute and when the data changes; each told once.
        if (!alertCheck || ms - alertCheck > 60000){
            alertCheck = ms ? ms : 1;
            const uint8_t before = d->alertCount;
            d->alertCount = (uint8_t)tdw_Alerts(settings, *d, now, d->alerts, TDW_ALERTS_MAX);
            if (d->alertCount != before) redraw = phoneDirty = true;
            portENTER_CRITICAL(&requestLock);
            memcpy(shownAlerts, d->alerts, sizeof(shownAlerts));
            shownCount = d->alertCount;
            portEXIT_CRITICAL(&requestLock);
            for (int i = 0; i < d->alertCount; i++){
                if (!first_Time(d->alerts[i], now)) continue;
                char text[96];
                mtb_Kit_To_Ascii(text, sizeof(text), d->alerts[i].text);
                mtb_Kit_Upper(text);
                statusBarNotif.mtb_Scroll_This_Text(text, RED);
                if (settings.phoneAlerts){
                    char title[80];
                    snprintf(title, sizeof(title), "Water alert: %s", settings.place);
                    const uint8_t kind = d->alerts[i].kind;
                    ntf_Send(title, d->alerts[i].text, kind == TDW_A_STORM || kind == TDW_A_HIGH_WATER ? NTF_HIGH : NTF_NORMAL);
                }
                page = TDW_P_ALERTS;            // show the news
                pageSince = ms;
                redraw = true;
            }
        }
        if (testRequested){
            testRequested = false;
            char text[160];
            snprintf(text, sizeof(text), "A test from Pixlpal's Tides & Water at %s. Alerts will arrive like this one.", settings.place);
            ntf_Send("Tides & Water test", text, NTF_NORMAL);
        }

        // The knob.
        const uint8_t clicks = clicksRequested;
        if (clicks) clicksRequested = 0;
        const int t = turns;
        if (t) turns = 0;
        if (clicks == 1) step_Page(1);
        else if (clicks == 2) tdw_Net_Refresh();
        if (t) step_Page(t > 0 ? 1 : -1);
        if (longPressRequested){ longPressRequested = false; settings.cycle = settings.cycle ? 0 : 1; settingsDirty = phoneDirty = true; pageSince = ms; }
        if (!page_Shown(settings, *d, page)) step_Page(1);
        if (settings.cycle && ms - pageSince >= settings.seconds * 1000u) step_Page(1);
        if (now != lastSecond){ lastSecond = now; if (now % 30 == 0) redraw = true; }   // the tide moves

        if (redraw){
            redraw = false;
            Tdw_View_t v = {page, &settings, d, now, nullptr};
            if (now < 1700000000u) v.message = "Waiting for the clock...";
            else if (!Mtb_Applications::internetConnectStatus && !d->tide.updated && !d->wx.updated) v.message = "Waiting for the internet...";
            tdw_Draw(v);
        }
        if (settingsDirty){ settingsDirty = false; mtb_Write_Nvs_Struct(TDW_NVS_KEY, &settings, sizeof(settings)); }
        if (phoneDirty && ms - phoneMs > 2000){ phoneDirty = false; phoneMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    shownCount = 0;
    tdw_Net_Stop();
    ntf_Stop();
    if (settingsDirty) mtb_Write_Nvs_Struct(TDW_NVS_KEY, &settings, sizeof(settings));
    heap_caps_free(d);
    heap_caps_free(places);
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) ---------------------------------------------------------------------------------------
static void row(JsonArray rows, const char* label, const char* value, const char* state){
    JsonObject o = rows.add<JsonObject>();
    o["label"] = label;
    o["value"] = value;
    if (state) o["state"] = state;
}

static void tdw_Values(JsonObject values){
    Tdw_Settings_t* s = (Tdw_Settings_t*)heap_caps_malloc(sizeof(Tdw_Settings_t), MALLOC_CAP_SPIRAM);
    if (!s) return;
    if (appOpen){ portENTER_CRITICAL(&requestLock); *s = settingsReceived ? received : settings; portEXIT_CRITICAL(&requestLock); }
    else read_Settings(*s);
    char t[160], a[32], b[32];
    values["place"]["name"] = s->place;
    snprintf(t, sizeof(t), "%.5f,%.5f", s->lat, s->lon);
    values["place"]["value"] = t;
    if (s->riverPlace[0]){
        values["riverPlace"]["name"] = s->riverPlace;
        snprintf(t, sizeof(t), "%.5f,%.5f", s->riverLat, s->riverLon);
        values["riverPlace"]["value"] = t;
    } else values["riverPlace"].to<JsonObject>();
    values["source"] = s->source;
    values["datum"] = s->datum;
    values["imperial"] = s->imperial;
    values["windUnit"] = s->windUnit;
    char key[4] = "p0";
    for (int i = 0; i < TDW_P_ALERTS; i++){ key[1] = (char)('0' + i); values[key] = (s->pages >> i) & 1; }
    values["cycle"] = s->cycle;
    values["seconds"] = s->seconds;
    values["alertOn"] = s->alertOn;
    values["highWaterM"] = round(s->highWaterCm / 10.0) / 10.0;
    values["highWaterFt"] = round(s->highWaterCm / 30.48 * 2) / 2.0;
    values["waveAlert"] = s->waveAlertDm;
    values["gustAlert"] = s->gustAlert;
    values["riverAlert"] = s->riverAlert;
    values["stormAlert"] = s->stormAlert;
    values["leadHours"] = s->leadHours;
    values["phoneAlerts"] = s->phoneAlerts;
    const Ntf_Config_t& k = s->notify;
    values["ntfyOn"] = k.on[NTF_NTFY];
    values["telegramOn"] = k.on[NTF_TELEGRAM];
    values["whatsappOn"] = k.on[NTF_WHATSAPP];
    values["ntfyServer"] = k.ntfyServer;
    values["ntfyTopic"] = k.ntfyTopic;
    values["telegramChat"] = k.telegramChat;
    values["whatsappPhone"] = k.whatsappPhone;
    values["ntfyToken_set"] = k.ntfyToken[0] ? 1 : 0;
    values["telegramToken_set"] = k.telegramToken[0] ? 1 : 0;
    values["whatsappKey_set"] = k.whatsappKey[0] ? 1 : 0;
    values["worldTidesKey_set"] = s->worldTidesKey[0] ? 1 : 0;
    values["stormglassKey_set"] = s->stormglassKey[0] ? 1 : 0;
    const bool imperial = s->imperial;
    const uint8_t windUnit = s->windUnit;
    const uint32_t wtCredits = s->wtCredits;
    const bool hasWt = s->worldTidesKey[0], hasSg = s->stormglassKey[0];
    char topic[80];
    snprintf(topic, sizeof(topic), "%s%s%s", strcmp(k.ntfyServer, "ntfy.sh") ? k.ntfyServer : "", strcmp(k.ntfyServer, "ntfy.sh") ? "/" : "", k.ntfyTopic);
    const bool ntfy = s->phoneAlerts && k.on[NTF_NTFY] && k.ntfyTopic[0];
    memset(s, 0, sizeof(*s));
    heap_caps_free(s);

    JsonArray rows = values["status"].to<JsonArray>();
    JsonArray alerts = values["alertRows"].to<JsonArray>();
    if (ntfy){ JsonObject o = alerts.add<JsonObject>(); o["label"] = "Subscribe in ntfy to"; o["value"] = topic; o["state"] = "info"; }
    if (!appOpen) return;
    Tdw_Data_t* d = (Tdw_Data_t*)heap_caps_calloc(1, sizeof(Tdw_Data_t), MALLOC_CAP_SPIRAM);
    if (!d) return;
    uint32_t version = 0xFFFFFFFFu;
    tdw_Net_Copy(*d, version);
    const uint32_t now = (uint32_t)time(nullptr);
    // The tide.
    const Tdw_Tide_t& td = d->tide;
    if (td.state == TDW_OK && td.n >= 2){
        float h = NAN;
        tdw_Level_At(td, now, h);
        const int ni = tdw_Next_Extreme(td, now);
        tdw_Height_Text(a, sizeof(a), h, imperial);
        if (ni >= 0){
            tdw_Height_Text(b, sizeof(b), td.e[ni].h, imperial);
            char c[8];
            tdw_Clock(c, sizeof(c), td.e[ni].t, d->utcOffset);
            snprintf(t, sizeof(t), "%s, %s. %s %s at %s", a, td.e[ni].high ? "rising" : "falling", td.e[ni].high ? "High" : "Low", b, c);
        } else snprintf(t, sizeof(t), "%s", a);
        row(rows, "Tide", t, "ok");
        if (td.station[0]) snprintf(t, sizeof(t), "%s %s (%.0f km), above %s", tdw_Source_Name(td.source), td.station, (double)td.stationKm, td.datum);
        else snprintf(t, sizeof(t), "%s ocean model (%.0f km cell), above %s", tdw_Source_Name(td.source), (double)td.stationKm, td.datum);
        row(rows, "From", t, td.why[0] ? "warn" : "info");
        if (td.why[0]) row(rows, "Note", td.why, "warn");
        if (td.observed && now - td.obsT < 3600){ tdw_Height_Text(a, sizeof(a), td.obsH, imperial); snprintf(t, sizeof(t), "%s (measured)", a); row(rows, "Gauge", t, "ok"); }
    } else row(rows, "Tide", td.why[0] ? td.why : "Asking...", td.state == TDW_NONE ? "off" : td.state == TDW_WAIT ? "info" : "error");
    // The sea.
    if (d->sea.known){
        tdw_Height_Text(a, sizeof(a), d->sea.wave, imperial);
        tdw_Temp_Text(b, sizeof(b), d->sea.sst, imperial);
        snprintf(t, sizeof(t), "%s, waves %s, water %s", tdw_Sea_State(d->sea.wave), a, b);
        const uint8_t sev = tdw_Sea_Severity(d->sea.wave);
        row(rows, "Sea", t, sev <= 1 ? "ok" : sev == 2 ? "warn" : "error");
    } else if (d->sea.updated) row(rows, "Sea", "No open sea near this place", "off");
    // Wind.
    if (d->wx.known){
        tdw_Wind_Text(a, sizeof(a), d->wx.wind, windUnit);
        tdw_Wind_Text(b, sizeof(b), d->wx.gust, windUnit);
        snprintf(t, sizeof(t), "%s from %s, gusts %s. %s", a, tdw_Compass(d->wx.windDir), b, tdw_Weather_Text(d->wx.code));
        row(rows, "Wind", t, d->wx.gust >= 60 ? "error" : d->wx.gust >= 40 ? "warn" : "ok");
    }
    // The river.
    if (d->river.known){
        const float q = d->river.q[d->river.today];
        const uint8_t level = tdw_River_Level(d->river, q);
        if (d->river.climate) snprintf(t, sizeof(t), "%.0f m3/s, %s (usual %.0f)", (double)q, tdw_River_Name(level), (double)d->river.p50);
        else snprintf(t, sizeof(t), "%.0f m3/s", (double)q);
        row(rows, "River", t, level >= TDW_RIVER_VERY_HIGH ? "error" : level == TDW_RIVER_HIGH ? "warn" : "ok");
    } else if (d->river.updated) row(rows, "River", "No river big enough for the flood model here", "off");
    // Sources and plans.
    if (d->noaaText[0]) row(rows, "NOAA", d->noaaText, "info");
    if (hasWt){ snprintf(t, sizeof(t), "%lu credits this month (2 a day)", (unsigned long)wtCredits); row(rows, "WorldTides", t, "info"); }
    if (hasSg){
        if (d->sgQuota) snprintf(t, sizeof(t), "%s: %lu of %lu requests today", tdw_Stormglass_Plan(d->sgQuota), (unsigned long)d->sgUsed, (unsigned long)d->sgQuota);
        else snprintf(t, sizeof(t), "Key saved");
        row(rows, "Stormglass", t, d->sgQuota && d->sgUsed >= d->sgQuota ? "warn" : "info");
    }
    snprintf(t, sizeof(t), "%lu calls today (free: 10,000 a day)", (unsigned long)d->omCalls);
    row(rows, "Open-Meteo", t, "info");
    // Alerts.
    portENTER_CRITICAL(&requestLock);
    const int n = shownCount;
    memcpy(d->alerts, shownAlerts, sizeof(shownAlerts));
    portEXIT_CRITICAL(&requestLock);
    for (int i = 0; i < n; i++) row(alerts, "Now", d->alerts[i].text, "error");
    if (n == 0) row(alerts, "Now", "Nothing to watch out for", "ok");
    heap_caps_free(d);
}
