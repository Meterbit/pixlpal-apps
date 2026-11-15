/**
 * Build Status: up to six projects' CI at a glance, GitHub Actions (owner/repo) or GitLab pipelines
 * (gitlab:group/project), each on its default branch or one given (@branch). Green passing, red failing, blue running
 * (pulsing), grey cancelled or skipped; with the workflow, the commit's author and how long ago. A project going from
 * passing to failing sounds an alarm with a red banner; failing to passing gets a cheer and confetti.
 *
 * A worker task asks GitHub's and GitLab's APIs (tokens from the phone, kept in NVS). Knob: turn to see one project
 * with its last commit's title, click to check now. NVS "bldSave".
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
#include "bld_Mobile_Ui.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#define W           256
#define H           128
#define REPOS       6
#define SPEC_LEN    96
#define TOKEN_LEN   120
#define NVS_KEY     "bldSave"
#define SAVE_MAGIC  0x424C4401u
#define UA          "Pixlpal-BuildStatus/1.0"

EXT_RAM_BSS_ATTR TaskHandle_t buildStatus_Task_H = NULL;
static void buildStatus_App_Task(void* dApplication);
static void bld_Values(JsonObject values);

EXT_RAM_BSS_ATTR Mtb_Applications_FullScreen* buildStatus_App;
Mtb_Applications* buildStatus_App_GetInstance(){
    if (!buildStatus_App){
        buildStatus_App = new Mtb_Applications_FullScreen(buildStatus_App_Task, &buildStatus_Task_H, "Build Status", {0,0}, 8192);
        buildStatus_App->mtb_App_Set_Mobile_UI(BLD_UI_MANIFEST, BLD_UI_API, nullptr, bld_Values);
    }
    return buildStatus_App;
}
MTB_REGISTER_APP(buildStatus_App, 0, 0)       // the catalogue gives the app its number

struct Bld_Save_t {
    uint32_t magic;
    char spec[REPOS][SPEC_LEN];
    char githubToken[TOKEN_LEN], gitlabToken[TOKEN_LEN];
    uint8_t minutes, sound;
};

enum : uint8_t { R_UNKNOWN = 0, R_PASS, R_FAIL, R_RUN, R_OTHER, R_ERROR };

struct Bld_Repo_t {
    uint8_t state, lastDone;            // lastDone: the last finished run's R_PASS, R_FAIL or R_OTHER
    char branch[48];
    bool branchKnown;
    char workflow[32], title[72], author[32], why[40];
    uint32_t at;                        // the run's last change, Unix seconds
    uint32_t runId;
};

struct Bld_Shared_t {
    Bld_Repo_t repo[REPOS];
    uint32_t roundAt, version;
    uint8_t broke, fixed;               // a bit a repo, for the app task to show
    volatile bool checkNow, stop, running;
};

static EXT_RAM_BSS_ATTR Bld_Save_t save;
static EXT_RAM_BSS_ATTR Bld_Shared_t sh;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool appOpen = false, phoneChanged = false;

static void read_Save(Bld_Save_t& s){
    memset(&s, 0, sizeof(s));
    mtb_Read_Nvs_Struct(NVS_KEY, &s, sizeof(s));
    if (s.magic != SAVE_MAGIC){ memset(&s, 0, sizeof(s)); s.magic = SAVE_MAGIC; s.minutes = 2; s.sound = 1; }
    if (s.minutes < 1 || s.minutes > 30) s.minutes = 2;
    for (int i = 0; i < REPOS; i++) s.spec[i][SPEC_LEN - 1] = '\0';
    s.githubToken[TOKEN_LEN - 1] = s.gitlabToken[TOKEN_LEN - 1] = '\0';
}

// A project: "owner/repo", "owner/repo@branch", "gitlab:group/sub/project@branch".
struct Bld_Spec_t { bool gitlab; char path[SPEC_LEN]; char branch[48]; };

static bool parse_Spec(const char* text, Bld_Spec_t& s){
    memset(&s, 0, sizeof(s));
    const char* p = text;
    if (!strncasecmp(p, "gitlab:", 7)){ s.gitlab = true; p += 7; }
    else if (!strncasecmp(p, "github:", 7)) p += 7;
    else if (!strncasecmp(p, "https://github.com/", 19)) p += 19;
    else if (!strncasecmp(p, "https://gitlab.com/", 19)){ s.gitlab = true; p += 19; }
    const char* at = strchr(p, '@');
    size_t n = at ? (size_t)(at - p) : strlen(p);
    while (n && p[n - 1] == '/') n--;
    if (!n || n >= sizeof(s.path)) return false;
    memcpy(s.path, p, n);
    if (at){ if (!at[1] || strlen(at + 1) >= sizeof(s.branch)) return false; snprintf(s.branch, sizeof(s.branch), "%s", at + 1); }
    const char* slash = strchr(s.path, '/');
    if (!slash || slash == s.path || !slash[1]) return false;
    if (!s.gitlab && strchr(slash + 1, '/')) return false;                       // GitHub: exactly owner/repo
    for (const char* k = s.path; *k; k++) if (!(isalnum((unsigned char)*k) || strchr("-_./", *k))) return false;
    for (const char* k = s.branch; *k; k++) if ((unsigned char)*k <= ' ' || strchr("?#&%", *k)) return false;
    return true;
}

/** "octocat/hello-world" -> "HELLO-WORLD" for the screen. */
static void short_Name(const char* spec, char* out, size_t n){
    Bld_Spec_t s;
    if (!parse_Spec(spec, s)){ snprintf(out, n, "%s", spec); return; }
    const char* last = strrchr(s.path, '/');
    snprintf(out, n, "%s", last ? last + 1 : s.path);
    mtb_Kit_Upper(out);
}

// Phone commands (the BLE task) ---------------------------------------------------------------------------------------------
static void cmd_Repo(JsonDocument& c){
    for (int i = 0; i < REPOS; i++){
        char key[8];
        snprintf(key, sizeof(key), "repo%d", i);
        if (c[key].isNull()) continue;
        const char* v = c[key] | "";
        while (*v == ' ') v++;
        char spec[SPEC_LEN];
        snprintf(spec, sizeof(spec), "%s", v);
        size_t n = strlen(spec);
        while (n && spec[n - 1] == ' ') spec[--n] = '\0';
        Bld_Spec_t ps;
        if (n && !parse_Spec(spec, ps)){ mtb_Ble_App_Cmd_Reject(); return; }
        portENTER_CRITICAL(&lock);
        snprintf(save.spec[i], SPEC_LEN, "%s", spec);
        memset(&sh.repo[i], 0, sizeof(sh.repo[i]));
        sh.version++;
        portEXIT_CRITICAL(&lock);
        sh.checkNow = true;
        phoneChanged = true;
        return;
    }
    mtb_Ble_App_Cmd_Reject();
}
static void cmd_Token(JsonDocument& c, const char* key, char* dest){
    const char* v = c[key] | (const char*)nullptr;
    if (!v || strlen(v) >= TOKEN_LEN){ mtb_Ble_App_Cmd_Reject(); return; }
    for (const char* k = v; *k; k++) if ((unsigned char)*k <= ' ' || (unsigned char)*k > '~'){ mtb_Ble_App_Cmd_Reject(); return; }
    portENTER_CRITICAL(&lock);
    snprintf(dest, TOKEN_LEN, "%s", v);
    for (int i = 0; i < REPOS; i++) sh.repo[i].branchKnown = false;          // a private repo may answer now
    portEXIT_CRITICAL(&lock);
    sh.checkNow = true;
    phoneChanged = true;
}
static void cmd_Github(JsonDocument& c){ cmd_Token(c, "githubToken", save.githubToken); }
static void cmd_Gitlab(JsonDocument& c){ cmd_Token(c, "gitlabToken", save.gitlabToken); }
static void cmd_Minutes(JsonDocument& c){
    long v;
    if (!mtb_Kit_Json_Range(c["minutes"], 1, 30, v)){ mtb_Ble_App_Cmd_Reject(); return; }
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
    for (char* k = tmp; *k; k++) if (*k == '\n' || *k == '\r'){ *k = '\0'; break; }     // a commit's first line
    mtb_Kit_To_Ascii(dest, n, tmp);
}

static void error_Text(int code, bool hasToken, char* why, size_t n){
    if (code == PXL_HTTP_NOT_ALLOWED) snprintf(why, n, "NOT ALLOWED");
    else if (code <= 0) snprintf(why, n, "NO ANSWER");
    else if (code == 401) snprintf(why, n, "TOKEN REFUSED");
    else if (code == 403 || code == 429) snprintf(why, n, hasToken ? "NOT ALLOWED BY TOKEN" : "RATE LIMITED: ADD A TOKEN");
    else if (code == 404) snprintf(why, n, hasToken ? "NOT FOUND" : "NOT FOUND: PRIVATE? ADD A TOKEN");
    else snprintf(why, n, "HTTP %d", code);
}

/** GET a JSON API answer, parsed with @p filter. The HTTP status, or a negative reason. */
static int get_Json(const char* url, const char* const* headers, JsonDocument& doc, JsonDocument& filter){
    Mtb_Kit_Http_t r = {};
    char* body = mtb_Kit_Http_Get(url, headers, nullptr, 0, 60000, r, UA);
    if (r.code >= 200 && r.code < 300 && body){
        if (deserializeJson(doc, body, r.length, DeserializationOption::Filter(filter))) r.code = -2;
    }
    heap_caps_free(body);
    return r.code;
}

static uint8_t github_State(const char* status, const char* conclusion){
    if (!status) return R_UNKNOWN;
    if (strcmp(status, "completed")) return R_RUN;                              // queued, in_progress, waiting...
    if (!conclusion) return R_OTHER;
    if (!strcmp(conclusion, "success")) return R_PASS;
    if (!strcmp(conclusion, "failure") || !strcmp(conclusion, "timed_out") || !strcmp(conclusion, "startup_failure")) return R_FAIL;
    return R_OTHER;                                                             // cancelled, skipped, neutral...
}
static uint8_t gitlab_State(const char* status){
    if (!status) return R_UNKNOWN;
    if (!strcmp(status, "success")) return R_PASS;
    if (!strcmp(status, "failed")) return R_FAIL;
    if (!strcmp(status, "running") || !strcmp(status, "pending") || !strcmp(status, "created") || !strcmp(status, "preparing") ||
        !strcmp(status, "waiting_for_resource")) return R_RUN;
    return R_OTHER;                                                             // canceled, skipped, manual, scheduled
}

/** One project's latest run into @p out (out.branch kept between rounds). */
static void check_Repo(const Bld_Spec_t& s, const Bld_Save_t& sv, Bld_Repo_t& out){
    char url[300], auth[TOKEN_LEN + 16];
    JsonDocument doc, filter;
    if (!s.gitlab){
        const bool tok = sv.githubToken[0] != 0;
        snprintf(auth, sizeof(auth), "Bearer %s", sv.githubToken);
        const char* headers[] = {"Accept", "application/vnd.github+json", "X-GitHub-Api-Version", "2022-11-28", tok ? "Authorization" : nullptr, auth, nullptr};
        if (s.branch[0]){ snprintf(out.branch, sizeof(out.branch), "%s", s.branch); out.branchKnown = true; }
        if (!out.branchKnown){
            snprintf(url, sizeof(url), "https://api.github.com/repos/%s", s.path);
            filter["default_branch"] = true;
            const int code = get_Json(url, headers, doc, filter);
            if (code < 200 || code >= 300){ out.state = R_ERROR; error_Text(code, tok, out.why, sizeof(out.why)); return; }
            snprintf(out.branch, sizeof(out.branch), "%s", doc["default_branch"] | "main");
            out.branchKnown = true;
            doc.clear(); filter.clear();
        }
        char br[100];
        mtb_Kit_Url_Encode(br, sizeof(br), out.branch);
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/actions/runs?per_page=1&exclude_pull_requests=true&branch=%s", s.path, br);
        JsonObject f = filter["workflow_runs"][0].to<JsonObject>();
        f["id"] = true; f["status"] = true; f["conclusion"] = true; f["name"] = true; f["display_title"] = true; f["updated_at"] = true;
        f["actor"]["login"] = true; f["head_commit"]["author"]["name"] = true;
        const int code = get_Json(url, headers, doc, filter);
        if (code < 200 || code >= 300){ out.state = R_ERROR; error_Text(code, tok, out.why, sizeof(out.why)); return; }
        JsonObject run = doc["workflow_runs"][0];
        if (run.isNull()){ out.state = R_UNKNOWN; snprintf(out.why, sizeof(out.why), "NO RUNS ON %s", out.branch); mtb_Kit_Upper(out.why); return; }
        out.state = github_State(run["status"], run["conclusion"]);
        out.runId = run["id"] | 0u;
        copy_Ascii(out.workflow, sizeof(out.workflow), run["name"] | "");
        copy_Ascii(out.title, sizeof(out.title), run["display_title"] | "");
        const char* who = run["head_commit"]["author"]["name"] | (const char*)nullptr;
        copy_Ascii(out.author, sizeof(out.author), who ? who : (run["actor"]["login"] | ""));
        out.at = mtb_Kit_Parse_Iso_Time(run["updated_at"] | "");
        out.why[0] = '\0';
        return;
    }
    // GitLab
    const bool tok = sv.gitlabToken[0] != 0;
    const char* headers[] = {tok ? "PRIVATE-TOKEN" : nullptr, sv.gitlabToken, nullptr};
    char project[200];
    mtb_Kit_Url_Encode(project, sizeof(project), s.path);
    if (s.branch[0]){ snprintf(out.branch, sizeof(out.branch), "%s", s.branch); out.branchKnown = true; }
    if (!out.branchKnown){
        snprintf(url, sizeof(url), "https://gitlab.com/api/v4/projects/%s", project);
        filter["default_branch"] = true;
        const int code = get_Json(url, headers, doc, filter);
        if (code < 200 || code >= 300){ out.state = R_ERROR; error_Text(code, tok, out.why, sizeof(out.why)); return; }
        snprintf(out.branch, sizeof(out.branch), "%s", doc["default_branch"] | "main");
        out.branchKnown = true;
        doc.clear(); filter.clear();
    }
    char br[100];
    mtb_Kit_Url_Encode(br, sizeof(br), out.branch);
    snprintf(url, sizeof(url), "https://gitlab.com/api/v4/projects/%s/pipelines?per_page=1&ref=%s", project, br);
    JsonObject f = filter[0].to<JsonObject>();
    f["id"] = true; f["status"] = true; f["updated_at"] = true; f["source"] = true;
    const int code = get_Json(url, headers, doc, filter);
    if (code < 200 || code >= 300){ out.state = R_ERROR; error_Text(code, tok, out.why, sizeof(out.why)); return; }
    JsonObject p = doc[0];
    if (p.isNull()){ out.state = R_UNKNOWN; snprintf(out.why, sizeof(out.why), "NO PIPELINES ON %s", out.branch); mtb_Kit_Upper(out.why); return; }
    const uint32_t id = p["id"] | 0u;
    out.state = gitlab_State(p["status"]);
    out.at = mtb_Kit_Parse_Iso_Time(p["updated_at"] | "");
    snprintf(out.workflow, sizeof(out.workflow), "PIPELINE #%lu", (unsigned long)id);
    out.why[0] = '\0';
    if (id != out.runId){                                                       // a new pipeline: who started it, and its commit
        out.runId = id;
        out.author[0] = out.title[0] = '\0';
        doc.clear(); filter.clear();
        snprintf(url, sizeof(url), "https://gitlab.com/api/v4/projects/%s/pipelines/%lu", project, (unsigned long)id);
        filter["user"]["name"] = true;
        if (get_Json(url, headers, doc, filter) == 200) copy_Ascii(out.author, sizeof(out.author), doc["user"]["name"] | "");
        doc.clear(); filter.clear();
        snprintf(url, sizeof(url), "https://gitlab.com/api/v4/projects/%s/repository/commits/%s", project, br);
        filter["title"] = true;
        if (get_Json(url, headers, doc, filter) == 200) copy_Ascii(out.title, sizeof(out.title), doc["title"] | "");
    }
}

static void worker(void*){
    uint32_t nextRound = 0;
    while (!sh.stop){
        if (!sh.checkNow && (int32_t)(millis() - nextRound) < 0){ vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        sh.checkNow = false;
        Bld_Save_t s;
        portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
        int githubN = 0;
        for (int i = 0; i < REPOS; i++){ Bld_Spec_t ps; if (s.spec[i][0] && parse_Spec(s.spec[i], ps) && !ps.gitlab) githubN++; }
        // Without a token GitHub allows 60 requests an hour: space the rounds to stay under about 50.
        uint32_t minutes = s.minutes;
        if (!s.githubToken[0] && githubN){ const uint32_t floorMin = (githubN * 60 + 49) / 50; if (minutes < floorMin) minutes = floorMin; }
        nextRound = millis() + minutes * 60000u;
        for (int i = 0; i < REPOS && !sh.stop; i++){
            Bld_Spec_t ps;
            if (!s.spec[i][0] || !parse_Spec(s.spec[i], ps)) continue;
            Bld_Repo_t r;
            portENTER_CRITICAL(&lock); r = sh.repo[i]; portEXIT_CRITICAL(&lock);
            check_Repo(ps, s, r);
            portENTER_CRITICAL(&lock);
            if (!strcmp(s.spec[i], save.spec[i])){                              // not changed meanwhile
                const uint8_t was = sh.repo[i].lastDone;
                if (r.state == R_PASS || r.state == R_FAIL || r.state == R_OTHER){
                    if (was == R_PASS && r.state == R_FAIL) sh.broke |= (uint8_t)(1 << i);
                    if (was == R_FAIL && r.state == R_PASS) sh.fixed |= (uint8_t)(1 << i);
                    r.lastDone = r.state;
                }
                sh.repo[i] = r;
                sh.version++;
            }
            portEXIT_CRITICAL(&lock);
        }
        sh.roundAt = (uint32_t)time(nullptr);
    }
    sh.running = false;
    vTaskDelete(nullptr);
}

// Drawing ------------------------------------------------------------------------------------------------------------------------------
static const uint16_t C_BG = MTB_KIT_RGB(5, 7, 12), C_TILE = MTB_KIT_RGB(16, 20, 30), C_HEAD = MTB_KIT_RGB(14, 18, 28);
static const uint16_t C_PASS = MTB_KIT_RGB(40, 200, 90), C_FAIL = MTB_KIT_RGB(235, 45, 45), C_RUN = MTB_KIT_RGB(70, 140, 255);

static uint16_t state_Color(uint8_t s, uint32_t ms){
    switch (s){
        case R_PASS: return C_PASS;
        case R_FAIL: return C_FAIL;
        case R_RUN:  return mtb_Kit_Blend(C_RUN, MTB_KIT_RGB(170, 205, 255), 0.5f + 0.5f * (float)((ms / 60) % 20 < 10 ? (ms / 60) % 10 : 10 - (ms / 60) % 10) / 10.0f);
        case R_ERROR: return KIT_ORANGE;
        default: return KIT_DIM;
    }
}
static const char* state_Word(uint8_t s){
    static const char* const N[] = {"CHECKING", "PASSING", "FAILING", "RUNNING", "CANCELLED", "ERROR"};
    return N[s < 6 ? s : 0];
}

static void ago_Text(char* t, size_t n, uint32_t at){
    const uint32_t now = (uint32_t)time(nullptr);
    if (!at || now < at){ t[0] = '\0'; return; }
    mtb_Kit_Ago(t, n, (int32_t)(now - at));
    mtb_Kit_Upper(t);
}

static void tile(const Bld_Save_t& s, const Bld_Repo_t& r, int i, int x, int y, int w, int h, uint32_t ms){
    const uint16_t sc = state_Color(r.state, ms);
    const uint16_t bg = r.state == R_FAIL ? MTB_KIT_RGB(48, 10, 12) : C_TILE;
    mtb_Panel_Fill_Rect(x, y, x + w - 1, y + h - 1, bg);
    mtb_Panel_Fill_Rect(x, y, x + 2, y + h - 1, sc);
    char name[SPEC_LEN], fit[SPEC_LEN], t[96], ago[20];
    short_Name(s.spec[i], name, sizeof(name));
    const char* word = r.state == R_OTHER ? "CANCELLED" : state_Word(r.state);
    const int ww = mtb_Kit_Text_Width(Terminal6x8, word);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, name, w - ww - 16);
    mtb_Kit_Text(x + 6, y + 2, Terminal6x8, KIT_TEXT, fit, bg);
    mtb_Kit_Text_Right(x + w - 4, y + 2, Terminal6x8, sc, word, bg);
    if (h < 18) return;
    if (r.state == R_ERROR || (r.state == R_UNKNOWN && r.why[0])) snprintf(t, sizeof(t), "%s", r.why);
    else {
        ago_Text(ago, sizeof(ago), r.at);
        char br[48], who[32];
        snprintf(br, sizeof(br), "%s", r.branch); mtb_Kit_Upper(br);
        snprintf(who, sizeof(who), "%s", r.author); mtb_Kit_Upper(who);
        snprintf(t, sizeof(t), "%s%s%s%s%s", br, who[0] ? "  " : "", who, ago[0] ? "  " : "", ago);
    }
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, w - 12);
    mtb_Kit_Text(x + 6, y + 12, Terminal4x6, r.state == R_ERROR ? KIT_ORANGE : KIT_LABEL, fit, bg);
}

static void draw_All(const Bld_Save_t& s, const Bld_Shared_t& d, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    int list[REPOS], used = 0;
    for (int i = 0; i < REPOS; i++) if (s.spec[i][0]) list[used++] = i;
    mtb_Panel_Fill_Rect(0, 0, W - 1, 9, C_HEAD);
    mtb_Kit_Text(2, 1, Terminal6x8, KIT_LABEL, "BUILDS", C_HEAD);
    int pass = 0, fail = 0, known = 0;
    for (int k = 0; k < used; k++){ const uint8_t st = d.repo[list[k]].state; if (st == R_PASS || st == R_FAIL || st == R_OTHER || st == R_RUN){ known++; if (st == R_FAIL) fail++; else if (st == R_PASS) pass++; } }
    char t[40];
    uint16_t c = KIT_LABEL;
    if (!used) t[0] = '\0';
    else if (!known) snprintf(t, sizeof(t), "CHECKING...");
    else if (fail){ snprintf(t, sizeof(t), "%d FAILING", fail); c = (ms / 500) % 2 ? C_FAIL : KIT_DIM; }
    else { snprintf(t, sizeof(t), "%d OF %d PASSING", pass, known); c = C_PASS; }
    mtb_Kit_Text_Centre(W / 2, 1, Terminal6x8, c, t, C_HEAD);
    if (d.roundAt){ char ago[20]; ago_Text(ago, sizeof(ago), d.roundAt); mtb_Kit_Text_Right(W - 2, 2, Terminal4x6, KIT_DIM, ago, C_HEAD); }
    if (!used){
        mtb_Kit_Text_Centre(W / 2, 50, Terminal8x12, KIT_TEXT, "NO PROJECTS YET", C_BG);
        mtb_Kit_Text_Centre(W / 2, 68, Terminal4x6, KIT_LABEL, "ADD THEM IN THE PHONE APP", C_BG);
        return;
    }
    const int cols = used > 4 ? 2 : 1, rows = (used + cols - 1) / cols;
    const int tw = (W - (cols - 1) * 2) / cols, th = (H - 11 - (rows - 1) * 2) / rows;
    for (int k = 0; k < used; k++){
        const int col = k / rows, row = k % rows;
        tile(s, d.repo[list[k]], list[k], col * (tw + 2), 11 + row * (th + 2), tw, th > 30 ? 30 : th, ms);
    }
}

static void draw_One(const Bld_Save_t& s, const Bld_Shared_t& d, int i, uint32_t ms){
    mtb_Panel_Fill_Screen(C_BG);
    const Bld_Repo_t& r = d.repo[i];
    const uint16_t sc = state_Color(r.state, ms);
    Bld_Spec_t ps;
    parse_Spec(s.spec[i], ps);
    char t[120], fit[120], ago[20];
    snprintf(t, sizeof(t), "%s%s", ps.gitlab ? "GITLAB  " : "GITHUB  ", ps.path);
    mtb_Kit_Upper(t);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal4x6, t, W - 8);
    mtb_Kit_Text(4, 2, Terminal4x6, KIT_DIM, fit, C_BG);
    short_Name(s.spec[i], t, sizeof(t));
    mtb_Kit_Fit(fit, sizeof(fit), PT_Sans_Narrow_Bold18x22, t, W - 8);
    mtb_Kit_Text(4, 10, PT_Sans_Narrow_Bold18x22, KIT_TEXT, fit, C_BG);
    mtb_Panel_Fill_Rect(0, 36, W - 1, 61, sc);
    mtb_Kit_Text(8, 41, Terminal10x17, BLACK, r.state == R_OTHER ? "CANCELLED" : state_Word(r.state), sc);
    char br[48];
    snprintf(br, sizeof(br), "%s", r.branch); mtb_Kit_Upper(br);
    mtb_Kit_Text_Right(W - 8, 45, Terminal8x12, BLACK, br, sc);
    if (r.state == R_ERROR || (r.state == R_UNKNOWN && r.why[0])){ mtb_Kit_Text(4, 70, Terminal6x8, KIT_ORANGE, r.why, C_BG); return; }
    snprintf(t, sizeof(t), "%s", r.title[0] ? r.title : "-");
    char lines[2][64];
    const int n = mtb_Kit_Wrap(t, Terminal6x8, W - 8, &lines[0][0], sizeof(lines[0]), 2);
    for (int k = 0; k < n; k++) mtb_Kit_Text(4, 67 + k * 11, Terminal6x8, KIT_TEXT, lines[k], C_BG);
    ago_Text(ago, sizeof(ago), r.at);
    char who[32], wf[32];
    snprintf(who, sizeof(who), "%s", r.author); mtb_Kit_Upper(who);
    snprintf(wf, sizeof(wf), "%s", r.workflow); mtb_Kit_Upper(wf);
    char ttl[72]; snprintf(ttl, sizeof(ttl), "%s", r.title); mtb_Kit_Upper(ttl);
    if (!strcmp(ttl, wf)) snprintf(t, sizeof(t), "%s%s", who[0] ? "BY " : "", who);       // a scheduled run: its title is the workflow's name
    else snprintf(t, sizeof(t), "%s%s%s", wf, who[0] ? "  BY " : "", who);
    mtb_Kit_Fit(fit, sizeof(fit), Terminal6x8, t, W - 8);
    mtb_Kit_Text(4, 94, Terminal6x8, KIT_LABEL, fit, C_BG);
    mtb_Kit_Text(4, 106, Terminal6x8, KIT_LABEL, ago, C_BG);
    mtb_Kit_Text_Right(W - 4, 119, Terminal4x6, KIT_DIM, "TURN: NEXT", C_BG);
}

// The app -------------------------------------------------------------------------------------------------------------------------------
static void alert_Sound(bool broke){
    static const Snd_Note_t BROKE[] = {{440, 180}, {0, 60}, {349, 180}, {0, 60}, {294, 420}};
    static const Snd_Note_t FIXED[] = {{523, 90}, {659, 90}, {784, 90}, {1047, 160}, {0, 50}, {784, 90}, {1047, 300}};
    if (!save.sound || mtb_Silent_Mode()) return;
    if (broke) snd_Play(BROKE, 5, 0.35f, true); else snd_Play(FIXED, 7, 0.3f, false);
}

static void confetti(uint32_t ms, uint32_t since){
    static const uint16_t C[] = {KIT_GOLD, KIT_CYAN, KIT_GREEN, KIT_ORANGE, MTB_KIT_RGB(255, 80, 200), KIT_TEXT};
    const uint32_t t = ms - since;
    for (int k = 0; k < 60; k++){
        const uint32_t seed = (uint32_t)k * 2654435761u;
        const int x = (int)(seed % W);
        const int speed = 20 + (int)((seed >> 8) % 40);
        const int y = (int)((t * speed / 1000 + (seed >> 16) % H) % H);
        mtb_Panel_Fill_Rect((int16_t)x, (int16_t)y, (int16_t)(x + 1), (int16_t)(y + 1), C[k % 6]);
    }
}

static void buildStatus_App_Task(void* dApplication){
    Mtb_Applications* THIS_APP = (Mtb_Applications*)dApplication;
    THIS_APP->mtb_App_Set_EC11_Cb_Fns(on_Button, on_Turn);
    THIS_APP->mtb_App_Set_Ble_Comm_Fns(cmd_Repo, cmd_Github, cmd_Gitlab, cmd_Minutes, cmd_Sound, cmd_Check);
    THIS_APP->mtb_App_Init();
    read_Save(save);
    turns = 0; clickRequested = false; phoneChanged = false;
    memset(&sh, 0, sizeof(sh));
    sh.checkNow = true;
    sh.running = true;
    if (xTaskCreatePinnedToCore(worker, "bldNet", 12288, nullptr, MTB_PRIO_APP, nullptr, tskNO_AFFINITY) != pdPASS) sh.running = false;
    appOpen = true;

    Bld_Shared_t* d = (Bld_Shared_t*)heap_caps_calloc(1, sizeof(Bld_Shared_t), MALLOC_CAP_SPIRAM);
    if (!d){ sh.stop = true; appOpen = false; mtb_Delete_This_App(THIS_APP); return; }
    uint32_t version = 0xFFFFFFFF, lastDraw = 0, pushedMs = 0, bannerUntil = 0, bannerSince = 0;
    int view = -1;
    char banner[48] = "";
    bool bannerBroke = false, redraw = true, push = true, saveNow = false;
    while (THIS_APP_IS_ACTIVE == pdTRUE){
        const uint32_t ms = millis();
        if (phoneChanged){ phoneChanged = false; redraw = push = saveNow = true; if (view >= 0 && !save.spec[view][0]) view = -1; }
        const int t = turns;
        if (t){
            turns -= t;
            int k = view;
            for (int step = 0; step < (t > 0 ? t : -t); step++)
                for (int n = 0; n <= REPOS; n++){
                    k += t > 0 ? 1 : -1;
                    if (k >= REPOS) k = -1;
                    if (k < -1) k = REPOS - 1;
                    if (k == -1 || save.spec[k][0]) break;
                }
            view = k;
            redraw = true;
        }
        if (clickRequested){ clickRequested = false; sh.checkNow = true; }
        uint8_t broke, fixed;
        portENTER_CRITICAL(&lock);
        if (sh.version != version){ *d = sh; version = sh.version; redraw = push = true; }
        broke = sh.broke; fixed = sh.fixed; sh.broke = sh.fixed = 0;
        portEXIT_CRITICAL(&lock);
        if (broke || fixed){
            const int i = __builtin_ctz(broke ? broke : fixed);
            char name[SPEC_LEN];
            short_Name(save.spec[i], name, sizeof(name));
            char br[48]; snprintf(br, sizeof(br), "%s", d->repo[i].branch); mtb_Kit_Upper(br);
            snprintf(banner, sizeof(banner), broke ? "%.20s %.12s BROKE" : "%.20s FIXED!", name, br);
            bannerBroke = broke != 0;
            bannerSince = ms;
            bannerUntil = ms + (broke ? 10000 : 6000);
            alert_Sound(bannerBroke);
        }
        const bool bannerOn = (int32_t)(bannerUntil - ms) > 0;
        if (redraw || ms - lastDraw >= (bannerOn ? 60u : 120u)){
            redraw = false;
            lastDraw = ms;
            Bld_Save_t s;
            portENTER_CRITICAL(&lock); s = save; portEXIT_CRITICAL(&lock);
            mtb_Panel_Begin_Update();
            if (view < 0) draw_All(s, *d, ms); else draw_One(s, *d, view, ms);
            if (bannerOn){
                const bool on = !bannerBroke || (ms / 400) % 2;
                const uint16_t bg = bannerBroke ? (on ? C_FAIL : MTB_KIT_RGB(60, 0, 0)) : C_PASS;
                if (!bannerBroke) confetti(ms, bannerSince);
                mtb_Panel_Fill_Rect(8, 46, W - 9, 80, bg);
                char fit[48];
                mtb_Kit_Fit(fit, sizeof(fit), Terminal8x12, banner, W - 28);
                mtb_Kit_Text_Centre(W / 2, 57, Terminal8x12, on ? BLACK : C_FAIL, fit, bg);
            }
            mtb_Panel_End_Update();
        }
        if (saveNow){ saveNow = false; mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save)); }
        if (push && ms - pushedMs >= 1500){ push = false; pushedMs = ms; THIS_APP->mtb_App_Push_Mobile_Values(); }
        delay(20);
    }
    appOpen = false;
    sh.stop = true;
    for (int i = 0; i < 400 && sh.running; i++) vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(d);
    mtb_Write_Nvs_Struct(NVS_KEY, &save, sizeof(save));
    mtb_Delete_This_App(THIS_APP);
}

// What the phone shows (the BLE task) -------------------------------------------------------------------------------------------------
static void bld_Values(JsonObject values){
    Bld_Save_t* s = (Bld_Save_t*)heap_caps_malloc(sizeof(Bld_Save_t), MALLOC_CAP_SPIRAM);
    Bld_Shared_t* d = (Bld_Shared_t*)heap_caps_malloc(sizeof(Bld_Shared_t), MALLOC_CAP_SPIRAM);
    if (!s || !d){ heap_caps_free(s); heap_caps_free(d); return; }
    if (appOpen){ portENTER_CRITICAL(&lock); *s = save; *d = sh; portEXIT_CRITICAL(&lock); }
    else { read_Save(*s); memset(d, 0, sizeof(*d)); }
    JsonArray rows = values["builds"].to<JsonArray>();
    char t[140];
    for (int i = 0; i < REPOS; i++){
        char key[8];
        snprintf(key, sizeof(key), "repo%d", i);
        values[key] = s->spec[i];
        if (!s->spec[i][0]) continue;
        const Bld_Repo_t& r = d->repo[i];
        JsonObject o = rows.add<JsonObject>();
        o["label"] = s->spec[i];
        if (!appOpen){ o["value"] = "Checked while Build Status is open"; o["state"] = "off"; continue; }
        switch (r.state){
            case R_PASS: snprintf(t, sizeof(t), "Passing on %s%s%s", r.branch, r.author[0] ? ", by " : "", r.author); o["state"] = "ok"; break;
            case R_FAIL: snprintf(t, sizeof(t), "Failing on %s%s%s", r.branch, r.author[0] ? ", by " : "", r.author); o["state"] = "error"; break;
            case R_RUN:  snprintf(t, sizeof(t), "Running on %s", r.branch); o["state"] = "info"; break;
            case R_OTHER: snprintf(t, sizeof(t), "Cancelled or skipped on %s", r.branch); o["state"] = "off"; break;
            case R_ERROR: snprintf(t, sizeof(t), "Can't check: %s", r.why); o["state"] = "warn"; break;
            default: snprintf(t, sizeof(t), "%s", r.why[0] ? r.why : "Checking"); o["state"] = "info"; break;
        }
        o["value"] = t;
        if (r.at){ char k2[8]; snprintf(k2, sizeof(k2), "at%d", i); values[k2] = r.at; o["ago"] = k2; }
    }
    values["githubToken_set"] = s->githubToken[0] ? 1 : 0;
    values["gitlabToken_set"] = s->gitlabToken[0] ? 1 : 0;
    values["minutes"] = s->minutes;
    values["sound"] = s->sound;
    heap_caps_free(s);
    heap_caps_free(d);
}
