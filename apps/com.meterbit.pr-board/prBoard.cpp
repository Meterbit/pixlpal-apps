/**
 * PR Board: the owner's GitHub work at a glance. Three counts along the top: pull requests waiting for their review,
 * their own open pull requests, and open issues assigned to them; below, the list of one of them (the knob turns
 * between them). Their own pull requests show the review decision (approved, changes requested, review required)
 * and a failed check; any draft says so. A review request that wasn't there before gets a chime.
 *
 * One GraphQL request a round (POST api.github.com/graphql) asks for all three lists with only the fields shown, so
 * the answer stays small: GitHub's search REST API returns each pull request's whole description. The owner's login
 * is asked once (viewer), then used in the searches. The token comes from the phone and stays on Pixlpal (NVS
 * "prbSave"). Knob: turn, the next list; click, check now.
 */
#include "mtb_engine.h"
#include "mtb_ble.h"
#include "mtb_kit_json.h"
#include "mtb_kit_ui.h"
#include "mtb_kit_http.h"
#include "mtb_device_prefs.h"
#include "mtb_task_map.h"
#include "snd_Sound.h"
#include "esp_heap_caps.h"
#include "prb_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#define W           256
#define H           128
#define LISTS       3
#define ITEMS       6
#define TOKEN_LEN   120
#define SCOPE_LEN   80
#define NVS_KEY     "prbSave"
#define SAVE_MAGIC  0x50524201u
#define UA          "Pixlpal-PRBoard/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t prBoard_Task_H = NULL;
static void prBoard_App_Task(void* dApplication);
static void prb_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* prBoard_App;
Mtb_Applications* prBoard_App_GetInstance(){
    if (!prBoard_App){
        prBoard_App = new Mtb_Applications_FullScreen(prBoard_App_Task, &prBoard_Task_H, "PR Board", {0,0}, 8192);
        prBoard_App->mtb_App_Set_Mobile_UI(PRB_UI_MANIFEST, PRB_UI_API, nullptr, prb_Values);
    }
    return prBoard_App;
}
MTB_REGISTER_APP(prBoard_App, 0, 0)       // the catalogue gives the app its number

struct Prb_Save_t {
    uint32_t magic;
    char token[TOKEN_LEN];
    char scope[SCOPE_LEN];
    uint8_t minutes, sound;
};

enum : uint8_t { L_REVIEW = 0, L_MINE, L_ISSUES };
enum : uint8_t { D_NONE = 0, D_APPROVED, D_CHANGES, D_REQUIRED };
enum : uint8_t { CI_NONE = 0, CI_PASS, CI_FAIL, CI_PENDING };

struct Prb_Item_t {
    char repo[28], title[80], author[24];
    uint32_t number, at;
    bool draft;
    uint8_t decision, ci;
};

struct Prb_List_t {
    uint32_t total;
    uint8_t count;
    Prb_Item_t item[ITEMS];
};

struct Prb_Shared_t {
    Prb_List_t list[LISTS];
    char login[40];
    char why[48];                       // the last round's problem, or ""
    bool known;                         // a round has answered
    uint32_t roundAt, version;
    uint8_t newReviews;                 // review requests not there the round before
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Prb_Save_t save;
static EXT_RAM_BSS_ATTR Prb_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Prb_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 5; s.sound = 1; }
    if (s.minutes < 2 || s.minutes > 30) s.minutes = 5;
    s.token[TOKEN_LEN - 1] = s.scope[SCOPE_LEN - 1] = '\0';
}

// The scope: GitHub search words, put inside a GraphQL string. Letters, digits and - _ . / : and spaces only, so
// nothing in it can end the string or change the query.
static bool scope_Ok(const char* s){
    for (const char* k = s; *k; k++) if (!(isalnum((unsigned char)*k) || strchr("-_./: ", *k))) return false;
    return true;
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Token(JsonDocument& c){
    const char* v = c["token"] | (const char*)nullptr;
    if (!v || strlen(v) >= TOKEN_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(save.token, TOKEN_LEN, "%s", v);
    sh.login[0] = '\0';                                         // another token may be another person
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Scope(JsonDocument& c){
    const char* v = c["scope"] | (const char*)nullptr;
    if (!v || strlen(v) >= SCOPE_LEN || !scope_Ok(v)){ mtb_Ble_App_Cmd_Reject(); return; }
    while (*v == ' ') v++;
    portENTER_CRITICAL(&lock);
    snprintf(save.scope, SCOPE_LEN, "%s", v);
    size_t n = strlen(save.scope);
    while (n && save.scope[n - 1] == ' ') save.scope[--n] = '\0';
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 2, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.minutes = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Sound(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["sound"], 0, 1, v)){ mtb_Ble_App_Cmd_Reject(); return; }
    save.sound = (uint8_t)v;
    phoneChanged = true;
}
static void cmd_Check(JsonDocument&){ if (!appOpen){ mtb_Ble_App_Cmd_Reject(); return; } sh.checkNow = true; }

// Knob -----------------------------------------------------------------------------------------------------------------------------
static volatile int turns = 0;
static volatile bool clickRequested = false;
static void on_Button(button_event_t b){ if (b.type == BUTTON_CLICKED) clickRequested = true; }
static void on_Turn(rotary_encoder_rotation_t d){ turns += d == ROT_CLOCKWISE ? 1 : -1; }

// The worker ------------------------------------------------------------------------------------------------------------------------
static void copy_Ascii(char* dest, size_t n, const char* src){
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s", src ? src : "");
    for (char* k = tmp; *k; k++) if (*k == '\n' || *k == '\r'){ *k = '\0'; break; }
    mtb_Kit_To_Ascii(dest, n, tmp);
}

static void error_Text(int code, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "NOT ALLOWED");
    else if (code <= 0) snprintf(why, n, "NO ANSWER FROM GITHUB");
    else if (code == 401) snprintf(why, n, "TOKEN REFUSED BY GITHUB");
    else if (code == 403 || code == 429) snprintf(why, n, "GITHUB SAYS WAIT: RATE LIMITED");
    else snprintf(why, n, "GITHUB: HTTP %d", code);
}

/** POST a GraphQL query; the HTTP status (or a negative reason), the answer parsed with @p filter into @p doc. */
static int graphql(const char* token, const char* query, JsonDocument& doc, JsonDocument& filter, char* why, size_t whyN){
    // The body: {"query": "<query, its quotes escaped>"}
    static char body[1800];
    size_t n = (size_t)snprintf(body, sizeof(body), "{\"query\":\"");
    for (const char* k = query; *k && n + 4 < sizeof(body); k++){
        if (*k == '"' || *k == '\\') body[n++] = '\\';
        body[n++] = *k;
    }
    if (n + 3 >= sizeof(body)) return -1;
    body[n++] = '"'; body[n++] = '}'; body[n] = '\0';
    char auth[TOKEN_LEN + 16];
    snprintf(auth, sizeof(auth), "bearer %s", token);
    const char* headers[] = {"Authorization", auth, "Content-Type", "application/json", nullptr};
    Mtb_Kit_Http_t r = {};
    char* answer = mtb_Kit_Http_Send("POST", "https://api.github.com/graphql", headers, body, n, nullptr, 0, 60000, r, UA);
    memset(auth, 0, sizeof(auth));
    int code = r.code;
    if (code == 200 && answer){
        filter["errors"][0]["message"] = true;
        if (deserializeJson(doc, answer, r.length, DeserializationOption::Filter(filter))) code = -2;
        else if (!doc["errors"][0]["message"].isNull() && doc["data"].isNull()){
            // Nothing came back, only the error. (An error beside data, such as a fine-grained token that may not
            // read checks, leaves those fields empty: the lists still show, without the checks.)
            char m[96];
            copy_Ascii(m, sizeof(m), doc["errors"][0]["message"] | "");
            mtb_Kit_Upper(m);
            snprintf(why, whyN, "GITHUB: %.40s", m);
            code = -3;
        }
    }
    heap_caps_free(answer);
    if (code == -2) snprintf(why, whyN, "GITHUB'S ANSWER WAS TOO LONG");
    else if (code != 200 && code != -3) error_Text(code, why, whyN);
    return code;
}

static uint8_t decision_Of(const char* d){
    if (!d) return D_NONE;
    if (!strcmp(d, "APPROVED")) return D_APPROVED;
    if (!strcmp(d, "CHANGES_REQUESTED")) return D_CHANGES;
    if (!strcmp(d, "REVIEW_REQUIRED")) return D_REQUIRED;
    return D_NONE;
}
static uint8_t ci_Of(const char* s){
    if (!s) return CI_NONE;
    if (!strcmp(s, "SUCCESS")) return CI_PASS;
    if (!strcmp(s, "FAILURE") || !strcmp(s, "ERROR")) return CI_FAIL;
    return CI_PENDING;                                              // PENDING, EXPECTED
}

static void read_List(JsonVariantConst search, Prb_List_t& out){
    memset(&out, 0, sizeof(out));
    out.total = search["issueCount"] | 0u;
    for (JsonVariantConst node : search["nodes"].as<JsonArrayConst>()){
        if (out.count >= ITEMS) break;
        if (node["number"].isNull()) continue;                     // not a pull request or issue
        Prb_Item_t& it = out.item[out.count++];
        it.number = node["number"] | 0u;
        copy_Ascii(it.title, sizeof(it.title), node["title"] | "");
        copy_Ascii(it.repo, sizeof(it.repo), node["repository"]["name"] | "");
        copy_Ascii(it.author, sizeof(it.author), node["author"]["login"] | "");
        it.at = mtb_Kit_Parse_Iso_Time(node["updatedAt"] | "");
        it.draft = node["isDraft"] | false;
        it.decision = decision_Of(node["reviewDecision"]);
        it.ci = ci_Of(node["commits"]["nodes"][0]["commit"]["statusCheckRollup"]["state"]);
    }
}

static bool same_Item(const Prb_Item_t& a, const Prb_Item_t& b){ return a.number == b.number && !strcmp(a.repo, b.repo); }

/** One round: the login if not known yet, then the three lists in one query. True if the lists were read. */
static bool check_All(const Prb_Save_t& s, char* login, size_t loginN, Prb_List_t* lists, char* why, size_t whyN){
    JsonDocument doc, filter;
    why[0] = '\0';
    if (!login[0]){
        filter["data"]["viewer"]["login"] = true;
        const int code = graphql(s.token, "{viewer{login}}", doc, filter, why, whyN);
        if (code != 200) return false;
        const char* l = doc["data"]["viewer"]["login"] | "";
        for (const char* k = l; *k; k++) if (!(isalnum((unsigned char)*k) || *k == '-' || *k == '[' || *k == ']')){ snprintf(why, whyN, "GITHUB: AN ODD LOGIN"); return false; }
        snprintf(login, loginN, "%s", l);
        if (!login[0]){ snprintf(why, whyN, "GITHUB: NO LOGIN FOR THE TOKEN"); return false; }
        doc.clear(); filter.clear();
    }
    static char query[1400];
    const char* pr = "...on PullRequest{number title isDraft updatedAt reviewDecision author{login} repository{name} "
                     "commits(last:1){nodes{commit{statusCheckRollup{state}}}}}";
    const char* is = "...on Issue{number title updatedAt author{login} repository{name}}";
    snprintf(query, sizeof(query),
             "{r:search(type:ISSUE,first:%d,query:\"is:pr is:open archived:false review-requested:%s sort:updated-desc %s\"){issueCount nodes{%s}}"
             " m:search(type:ISSUE,first:%d,query:\"is:pr is:open archived:false author:%s sort:updated-desc %s\"){issueCount nodes{%s}}"
             " i:search(type:ISSUE,first:%d,query:\"is:issue is:open archived:false assignee:%s sort:updated-desc %s\"){issueCount nodes{%s}}}",
             ITEMS, login, s.scope, pr, ITEMS, login, s.scope, pr, ITEMS, login, s.scope, is);
    static const char* const KEYS[LISTS] = {"r", "m", "i"};
    for (int l = 0; l < LISTS; l++){
        JsonObject f = filter["data"][KEYS[l]].to<JsonObject>();
        f["issueCount"] = true;
        JsonObject n = f["nodes"][0].to<JsonObject>();
        n["number"] = true; n["title"] = true; n["isDraft"] = true; n["updatedAt"] = true; n["reviewDecision"] = true;
        n["author"]["login"] = true; n["repository"]["name"] = true;
        n["commits"]["nodes"][0]["commit"]["statusCheckRollup"]["state"] = true;
    }
    const int code = graphql(s.token, query, doc, filter, why, whyN);
    if (code != 200) return false;
    for (int l = 0; l < LISTS; l++) read_List(doc["data"][KEYS[l]], lists[l]);
    return true;
}

static void worker(void*){
    uint32_t nextRound = 0;
    static Prb_List_t lists[LISTS];
    while (!sh.stop){
        if (!sh.checkNow && (int32_t)(millis() - nextRound) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Prb_Save_t s;
        char login[40];
        portENTER_CRITICAL(&lock); s = save; snprintf(login, sizeof(login), "%s", sh.login); portEXIT_CRITICAL(&lock);
        nextRound = millis() + s.minutes * 60000u;
        if (!s.token[0]){
            portENTER_CRITICAL(&lock); sh.why[0] = '\0'; sh.known = false; sh.version++; portEXIT_CRITICAL(&lock);
            continue;
        }
        char why[48];
        const bool read = check_All(s, login, sizeof(login), lists, why, sizeof(why));
        portENTER_CRITICAL(&lock);
        if (!strcmp(s.token, save.token) && !strcmp(s.scope, save.scope)){     // not changed meanwhile
            snprintf(sh.login, sizeof(sh.login), "%s", login);
            snprintf(sh.why, sizeof(sh.why), "%s", why);
            if (read){
                // A review request not in the last round's list: a chime (not on the first round).
                uint8_t fresh = 0;
                if (sh.known)
                    for (int k = 0; k < lists[L_REVIEW].count; k++){
                        bool seen = false;
                        for (int j = 0; j < sh.list[L_REVIEW].count; j++) if (same_Item(lists[L_REVIEW].item[k], sh.list[L_REVIEW].item[j])) seen = true;
                        if (!seen) fresh++;
                    }
                memcpy(sh.list, lists, sizeof(lists));
                sh.known = true;
                sh.newReviews += fresh;
                sh.roundAt = (uint32_t)time(nullptr);
            }
            sh.version++;
        }
        portEXIT_CRITICAL(&lock);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(6, 8, 14), C_TILE = MTB_KIT_RGB(18, 22, 34), C_SEL = MTB_KIT_RGB(30, 40, 66);
static const uint16_t C_PURPLE = MTB_KIT_RGB(160, 110, 255), C_OPEN = MTB_KIT_RGB(60, 200, 100);
static const char* const LIST_NAME[LISTS] = {"TO REVIEW", "MY PULL REQUESTS", "MY ISSUES"};
static const char* const TILE_NAME[LISTS] = {"TO REVIEW", "MY PRS", "ISSUES"};

static void ago_Text(char* t, size_t n, uint32_t at){
    const uint32_t now = (uint32_t)time(nullptr);
    if (!at || now < at){ t[0] = '\0'; return; }
    mtb_Kit_Ago(t, n, (int32_t)(now - at));
    mtb_Kit_Upper(t);
}

static void count_Tile(int i, int x, int w, uint32_t total, bool selected, bool known){
    const uint16_t bg = selected ? C_SEL : C_TILE;
    mtb_Panel_Fill_Rect(x, 0, x + w - 1, 29, bg);
    if (selected) mtb_Panel_Fill_Rect(x, 28, x + w - 1, 29, KIT_CYAN);
    char t[12];
    if (known) snprintf(t, sizeof(t), "%lu", (unsigned long)total); else snprintf(t, sizeof(t), "-");
    const uint16_t c = !known ? KIT_DIM : i == L_REVIEW && total ? KIT_GOLD : total ? KIT_TEXT : KIT_LABEL;
    mtb_Kit_Text_Centre(x + w / 2, 1, PT_Sans_Narrow_Bold18x22, c, t, bg);
    mtb_Kit_Text_Centre(x + w / 2, 21, Terminal4x6, selected ? KIT_CYAN : KIT_LABEL, TILE_NAME[i], bg);
}

static void row(const Prb_Item_t& it, int l, int y){
    char t[120], fit[120], ago[20], tag[16] = "";
    uint16_t tc = KIT_LABEL;
    if (it.draft){ snprintf(tag, sizeof(tag), "DRAFT"); tc = KIT_DIM; }
    else if (l == L_MINE){
        if (it.ci == CI_FAIL){ snprintf(tag, sizeof(tag), "CHECKS"); tc = KIT_RED; }
        else if (it.decision == D_APPROVED){ snprintf(tag, sizeof(tag), "APPROVED"); tc = KIT_GREEN; }
        else if (it.decision == D_CHANGES){ snprintf(tag, sizeof(tag), "CHANGES"); tc = KIT_ORANGE; }
        else if (it.decision == D_REQUIRED){ snprintf(tag, sizeof(tag), "REVIEW"); tc = KIT_LABEL; }
        else if (it.ci == CI_PENDING){ snprintf(tag, sizeof(tag), "RUNNING"); tc = KIT_CYAN; }
    } else if (l == L_REVIEW && it.ci == CI_FAIL){ snprintf(tag, sizeof(tag), "CHECKS"); tc = KIT_RED; }
    // Line 1: repo#number and the title. Line 2: the author and when, and the tag.
    snprintf(t, sizeof(t), "%s#%lu", it.repo, (unsigned long)it.number);
    mtb_Kit_Upper(t);
    const int16_t rw = mtb_Kit_Text(4, y, Terminal4x6, l == L_ISSUES ? C_OPEN : C_PURPLE, t, C_BG);
    char title[80];
    snprintf(title, sizeof(title), "%s", it.title);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, title, W - 8 - (rw - 4) - 4);
    mtb_Kit_Text(rw + 4, y - 1, Terminal6x8, KIT_TEXT, fit, C_BG);
    ago_Text(ago, sizeof(ago), it.at);
    char who[24];
    snprintf(who, sizeof(who), "%s", it.author); mtb_Kit_Upper(who);
    snprintf(t, sizeof(t), "%s%s%s", who, ago[0] ? "  " : "", ago);
    mtb_Kit_Text(4, y + 8, Terminal4x6, KIT_DIM, t, C_BG);
    if (tag[0]) mtb_Kit_Text_Right(W - 4, y + 8, Terminal4x6, tc, tag, C_BG);
}

static void draw(const Prb_Save_t& s, const Prb_Shared_t& d, int view){
    mtb_Panel_Fill_Screen(C_BG);
    const int tw = (W - 4) / 3;
    for (int i = 0; i < LISTS; i++) count_Tile(i, i * (tw + 2), i == LISTS - 1 ? W - 2 * (tw + 2) : tw, d.list[i].total, i == view, d.known);
    if (!s.token[0]){
        mtb_Kit_Text_Centre(W / 2, 54, Terminal8x12, KIT_TEXT, "ADD A GITHUB TOKEN", C_BG);
        mtb_Kit_Text_Centre(W / 2, 72, Terminal4x6, KIT_LABEL, "IN THE PHONE APP, UNDER PR BOARD", C_BG);
        return;
    }
    const Prb_List_t& l = d.list[view];
    char t[64];
    snprintf(t, sizeof(t), "%s%s%s", LIST_NAME[view], s.scope[0] ? "  " : "", s.scope);
    mtb_Kit_Upper(t);
    char fit[64];
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, d.why[0] && d.known ? W - 132 : W - 70);
    mtb_Kit_Text(4, 33, Terminal4x6, KIT_CYAN, fit, C_BG);
    if (d.why[0] && d.known){ char w[48]; mtb_Kit_Fit(w, sizeof(w), Terminal4x6, d.why, 120); mtb_Kit_Text_Right(W - 4, 33, Terminal4x6, KIT_ORANGE, w, C_BG); }
    else if (d.roundAt){ char ago[20]; ago_Text(ago, sizeof(ago), d.roundAt); mtb_Kit_Text_Right(W - 4, 33, Terminal4x6, KIT_DIM, ago, C_BG); }
    if (d.why[0] && !d.known){
        char lines[3][64];
        const int n = mtb_Kit_Wrap(d.why, Terminal6x8, W - 16, &lines[0][0], sizeof(lines[0]), 3);
        for (int k = 0; k < n; k++) mtb_Kit_Text_Centre(W / 2, 58 + k * 11, Terminal6x8, KIT_ORANGE, lines[k], C_BG);
        return;
    }
    if (!d.known){ mtb_Kit_Text_Centre(W / 2, 66, Terminal6x8, KIT_LABEL, "CHECKING GITHUB...", C_BG); return; }
    if (!l.count){
        mtb_Kit_Text_Centre(W / 2, 62, Terminal8x12, KIT_GREEN, view == L_REVIEW ? "NO REVIEWS WAITING" : "NOTHING OPEN", C_BG);
        return;
    }
    for (int k = 0; k < l.count && k < 5; k++) row(l.item[k], view, 42 + k * 17);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void chime(void){
    static const Snd_Note_t N[] = {{784, 90}, {0, 40}, {988, 90}, {0, 40}, {1175, 220}};
    if (!save.sound || mtb_Silent_Mode()) return;
    snd_Play(N, 5, 0.3f, false);
}

static void prBoard_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Token, cmd_Scope, cmd_Minutes, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "prbNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Prb_Shared_t* d = (Prb_Shared_t*)heap_caps_calloc(1, sizeof(Prb_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0;
    int view = L_REVIEW;
    bool chose = false, redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; }
        const int t = turns;
        if (t){ turns -= t; view = ((view + t) % LISTS + LISTS) % LISTS; chose = true; redraw = true; }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; }
        uint8_t fresh;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        fresh = sh.newReviews; sh.newReviews = 0;
        portEXIT_CRITICAL(&lock);
        // Until the owner turns the knob: the reviews waiting, or their own pull requests when none are.
        if (!chose && d->known) view = d->list[L_REVIEW].total || !d->list[L_MINE].total ? L_REVIEW : L_MINE;
        if (fresh){ chime(); view = L_REVIEW; chose = false; redraw = true; }
        if (redraw || ms - lastDraw >= 30000u){              // "how long ago" moves on
            redraw = false;
            lastDraw = ms;
            Prb_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            draw(s, *d, view);
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(40);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void prb_Values(JsonObject values){
    Prb_Save_t* s = (Prb_Save_t*)heap_caps_malloc(sizeof(Prb_Save_t), MALLOC_CAP_SPIRAM);
    Prb_Shared_t* d = (Prb_Shared_t*)heap_caps_malloc(sizeof(Prb_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    JsonArray rows = values["now"].to<JsonArray>();
    char t[120];
    if (!s->token[0]){ JsonObject o = rows.add<JsonObject>(); o["label"] = "GitHub"; o["value"] = "No token yet"; o["state"] = "warn"; }
    else if (!appOpen){ JsonObject o = rows.add<JsonObject>(); o["label"] = "GitHub"; o["value"] = "Checked while PR Board is open"; o["state"] = "off"; }
    else if (!d->known){ JsonObject o = rows.add<JsonObject>(); o["label"] = "GitHub"; o["value"] = d->why[0] ? d->why : "Checking"; o["state"] = d->why[0] ? "warn" : "info"; }
    else {
        static const char* const LABEL[LISTS] = {"Waiting for your review", "Your open pull requests", "Issues assigned to you"};
        for (int l = 0; l < LISTS; l++){
            JsonObject o = rows.add<JsonObject>();
            o["label"] = LABEL[l];
            snprintf(t, sizeof(t), "%lu", (unsigned long)d->list[l].total);
            o["value"] = t;
            o["state"] = l == L_REVIEW && d->list[l].total ? "warn" : "ok";
        }
        int approved = 0, changes = 0, failing = 0;
        for (int k = 0; k < d->list[L_MINE].count; k++){
            const Prb_Item_t& it = d->list[L_MINE].item[k];
            if (it.decision == D_APPROVED) approved++;
            if (it.decision == D_CHANGES) changes++;
            if (it.ci == CI_FAIL) failing++;
        }
        if (d->list[L_MINE].count){
            JsonObject o = rows.add<JsonObject>();
            o["label"] = "Your pull requests";
            snprintf(t, sizeof(t), "%d approved, %d with changes asked, %d with failed checks", approved, changes, failing);
            o["value"] = t;
            o["state"] = failing || changes ? "warn" : "ok";
        }
        if (d->login[0]){ JsonObject o = rows.add<JsonObject>(); o["label"] = "Signed in as"; o["value"] = d->login; o["state"] = "info"; }
        if (d->why[0]){ JsonObject o = rows.add<JsonObject>(); o["label"] = "Last check"; o["value"] = d->why; o["state"] = "warn"; }
    }
    values["token_set"] = s->token[0] ? 1 : 0;
    values["scope"] = s->scope;
    values["minutes"] = s->minutes;
    values["sound"] = s->sound;
    heap_caps_free(s);
    heap_caps_free(d);
}
