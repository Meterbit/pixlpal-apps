// Business & Creator Counters: the providers' answers. No Arduino (tested on a PC against saved answers).
#include "bcc_Parse.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// The device gives a PSRAM allocator (bcc_Net.cpp); a PC test gives the default one.
ArduinoJson::Allocator* bcc_Json_Allocator(void);

static void why_Set(char* why, size_t size, const char* text){ if (why && size) snprintf(why, size, "%s", text); }

// Whether @p text contains @p part, case aside (newlib has no strcasestr).
static bool has(const char* text, const char* part){
    if (!text || !part) return false;
    const size_t n = strlen(part);
    for (const char* p = text; *p; p++) if (strncasecmp(p, part, n) == 0) return true;
    return n == 0;
}

static int64_t days_From_Civil(int y, int m, int d){
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

uint32_t bcc_Parse_Iso(const char* t){
    if (!t) return 0;
    int y, mo, d, h = 0, mi = 0, s = 0, n = 0;
    if (sscanf(t, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &n) < 6 || n == 0) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return 0;
    const char* p = t + n;
    if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
    int64_t offset = 0;
    if (*p == '+' || *p == '-'){
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) < 1) return 0;
        offset = (oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    }
    const int64_t v = days_From_Civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - offset;
    return v > 0 ? (uint32_t)v : 0;
}

void bcc_Iso(char* dest, size_t size, uint32_t unix){
    int64_t z = unix / 86400 + 719468;
    const int64_t era = z / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int y = (int)(yoe + era * 400 + (m <= 2));
    const unsigned s = unix % 86400;
    snprintf(dest, size, "%04d-%02u-%02uT%02u:%02u:%02uZ", y, m, d, s / 3600, s / 60 % 60, s % 60);
}

// The common HTTP answers.
static uint8_t http_State(int code, const char* who, char* why, size_t ws){
    char t[96];
    if (code <= 0){ snprintf(t, sizeof(t), "Cannot reach %s", who); why_Set(why, ws, t); return BCC_ERROR; }
    if (code == 401){ snprintf(t, sizeof(t), "%s refused the key", who); why_Set(why, ws, t); return BCC_KEY; }
    if (code == 429){ snprintf(t, sizeof(t), "%s: too many requests, waiting", who); why_Set(why, ws, t); return BCC_LIMIT; }
    if (code >= 500){ snprintf(t, sizeof(t), "%s is having trouble (HTTP %d)", who, code); why_Set(why, ws, t); return BCC_ERROR; }
    snprintf(t, sizeof(t), "%s answered HTTP %d", who, code);
    why_Set(why, ws, t);
    return BCC_ERROR;
}

static bool upper3(const char* in, char out[4]){
    if (!in || strlen(in) != 3) return false;
    for (int i = 0; i < 3; i++){ if (!isalpha((unsigned char)in[i])) return false; out[i] = (char)toupper((unsigned char)in[i]); }
    out[3] = '\0';
    return true;
}

// One paid sale into the day: counted, its money added, and kept as the newest if it is.
static void add_Sale(Bcc_Sales_t& s, const char* currency, int64_t minor, uint32_t at, const char* label){
    char code[4];
    if (!upper3(currency, code)) return;
    if (s.count < 65535) s.count++;
    bcc_Money_Add(s.money, s.moneyCount, code, minor);
    if (at >= s.lastAt){
        s.lastAt = at;
        memcpy(s.last.code, code, 4);
        s.last.minor = minor;
        snprintf(s.lastLabel, sizeof(s.lastLabel), "%s", label ? label : "");
    }
}

// Stripe ---------------------------------------------------------------------------------------------------------
uint8_t bcc_Parse_Stripe(const char* body, size_t length, int code, Bcc_Sales_t& sales, Bcc_Page_t& page, char* why, size_t ws){
    memset(&page, 0, sizeof(page));
    JsonDocument filter;
    JsonObject item = filter["data"].add<JsonObject>();
    for (const char* f : {"id", "amount", "amount_refunded", "currency", "status", "paid", "refunded", "created", "livemode"}) item[f] = true;
    filter["has_more"] = true;
    filter["error"]["message"] = true;
    filter["error"]["code"] = true;
    JsonDocument doc(bcc_Json_Allocator());
    const bool parsed = body && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    if (code != 200){
        const char* message = parsed ? (doc["error"]["message"] | "") : "";
        if (code == 403){
            why_Set(why, ws, "This restricted key cannot read payments: give it Charges: Read");
            return BCC_KEY;
        }
        const uint8_t st = http_State(code, "Stripe", why, ws);
        if (st == BCC_ERROR && message[0]){ char t[96]; snprintf(t, sizeof(t), "Stripe: %.80s", message); why_Set(why, ws, t); }
        return st;
    }
    if (!parsed || !doc["data"].is<JsonArray>()){ why_Set(why, ws, "Stripe sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    for (JsonObject c : doc["data"].as<JsonArray>()){
        const char* id = c["id"] | "";
        snprintf(page.cursor, sizeof(page.cursor), "%s", id);
        page.items++;
        if (!(c["livemode"] | true)) sales.test = 1;
        const char* status = c["status"] | "";
        if (strcmp(status, "succeeded") != 0 || !(c["paid"] | false) || (c["refunded"] | false)) continue;
        const int64_t net = (c["amount"] | (int64_t)0) - (c["amount_refunded"] | (int64_t)0);
        add_Sale(sales, c["currency"] | "", net, c["created"] | 0u, "");
    }
    page.more = (doc["has_more"] | false) && page.cursor[0];
    return BCC_OK;
}

// Paystack -------------------------------------------------------------------------------------------------------
uint8_t bcc_Parse_Paystack(const char* body, size_t length, int code, int pageNumber, Bcc_Sales_t& sales, Bcc_Page_t& page,
                           char* why, size_t ws){
    memset(&page, 0, sizeof(page));
    JsonDocument filter;
    JsonObject item = filter["data"].add<JsonObject>();
    for (const char* f : {"status", "amount", "currency", "paid_at", "created_at", "reference"}) item[f] = true;
    filter["meta"]["pageCount"] = true;
    filter["status"] = true;
    filter["message"] = true;
    JsonDocument doc(bcc_Json_Allocator());
    const bool parsed = body && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    if (code != 200 || (parsed && doc["status"].is<bool>() && !doc["status"].as<bool>())){
        const char* message = parsed ? (doc["message"] | "") : "";
        if (code == 401 || code == 403 || has(message, "key")){ why_Set(why, ws, "Paystack refused the secret key"); return BCC_KEY; }
        const uint8_t st = http_State(code == 200 ? 400 : code, "Paystack", why, ws);
        if (st == BCC_ERROR && message[0]){ char t[96]; snprintf(t, sizeof(t), "Paystack: %.80s", message); why_Set(why, ws, t); }
        return st;
    }
    if (!parsed || !doc["data"].is<JsonArray>()){ why_Set(why, ws, "Paystack sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    for (JsonObject t : doc["data"].as<JsonArray>()){
        page.items++;
        if (strcmp(t["status"] | "", "success") != 0) continue;
        const char* when = t["paid_at"] | (t["created_at"] | "");
        add_Sale(sales, t["currency"] | "", t["amount"] | (int64_t)0, bcc_Parse_Iso(when), "");
    }
    const int pageCount = doc["meta"]["pageCount"] | 0;
    page.more = page.items > 0 && pageNumber < pageCount;
    return BCC_OK;
}

// Shopify --------------------------------------------------------------------------------------------------------
size_t bcc_Shopify_Query(char* dest, size_t size, const char* sinceIso, const char* cursor){
    JsonDocument doc;
    const bool first = !cursor || !cursor[0];
    // The shop's plan and currency come with the first page only.
    doc["query"] = first
        ? "query($q:String!){shop{currencyCode plan{publicDisplayName partnerDevelopment shopifyPlus}}"
          "orders(first:100,query:$q,sortKey:CREATED_AT,reverse:true){nodes{name createdAt cancelledAt test "
          "totalPriceSet{shopMoney{amount currencyCode}}}pageInfo{hasNextPage endCursor}}}"
        : "query($q:String!,$after:String){orders(first:100,after:$after,query:$q,sortKey:CREATED_AT,reverse:true){nodes{name "
          "createdAt cancelledAt test totalPriceSet{shopMoney{amount currencyCode}}}pageInfo{hasNextPage endCursor}}}";
    char q[64];
    snprintf(q, sizeof(q), "created_at:>='%s'", sinceIso);
    doc["variables"]["q"] = q;
    if (!first) doc["variables"]["after"] = cursor;
    return serializeJson(doc, dest, size);
}

uint8_t bcc_Parse_Shopify(const char* body, size_t length, int code, Bcc_Sales_t& sales, Bcc_Page_t& page, char* why, size_t ws){
    memset(&page, 0, sizeof(page));
    JsonDocument doc(bcc_Json_Allocator());
    const bool parsed = body && !deserializeJson(doc, body, length);
    if (code == 401){ why_Set(why, ws, "Shopify refused the access token"); return BCC_KEY; }
    if (code == 402){ why_Set(why, ws, "The store is frozen: its Shopify bill is unpaid"); return BCC_ERROR; }
    if (code == 403){ why_Set(why, ws, "The app needs the read_orders access scope"); return BCC_KEY; }
    if (code == 404){ why_Set(why, ws, "No such store: check the myshopify.com name"); return BCC_KEY; }
    if (code == 423){ why_Set(why, ws, "The store is locked"); return BCC_ERROR; }
    if (code != 200) return http_State(code, "Shopify", why, ws);
    if (!parsed){ why_Set(why, ws, "Shopify sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    if (doc["errors"].is<JsonArray>() && doc["errors"].size()){
        JsonObject e = doc["errors"][0];
        const char* message = e["message"] | "";
        const char* ecode = e["extensions"]["code"] | "";
        if (!strcmp(ecode, "THROTTLED")){ why_Set(why, ws, "Shopify: too many requests, waiting"); return BCC_LIMIT; }
        if (!strcmp(ecode, "ACCESS_DENIED") || has(message, "access denied")){
            why_Set(why, ws, "The app needs the read_orders access scope");
            return BCC_KEY;
        }
        char t[96];
        snprintf(t, sizeof(t), "Shopify: %.80s", message);
        why_Set(why, ws, t);
        return BCC_ERROR;
    }
    JsonObject shop = doc["data"]["shop"];
    if (!shop.isNull()){
        const char* plan = shop["plan"]["publicDisplayName"] | "";
        snprintf(sales.plan, sizeof(sales.plan), "Shopify %s", plan[0] ? plan : "store");
        sales.test = (shop["plan"]["partnerDevelopment"] | false) ? 1 : 0;
    }
    JsonObject orders = doc["data"]["orders"];
    if (orders.isNull() || !orders["nodes"].is<JsonArray>()){ why_Set(why, ws, "Shopify sent no orders list"); return BCC_ERROR; }
    for (JsonObject o : orders["nodes"].as<JsonArray>()){
        page.items++;
        if (!o["cancelledAt"].isNull()) continue;
        if ((o["test"] | false) && !sales.test) continue;           // test orders count only on a development store
        JsonObject money = o["totalPriceSet"]["shopMoney"];
        const char* currency = money["currencyCode"] | "";
        int64_t minor;
        const char* amount = money["amount"].is<const char*>() ? money["amount"].as<const char*>() : nullptr;
        char number[32];
        if (!amount && money["amount"].is<double>()){ snprintf(number, sizeof(number), "%.3f", money["amount"].as<double>()); amount = number; }
        if (!bcc_Parse_Amount(amount, currency, minor)) continue;
        add_Sale(sales, currency, minor, bcc_Parse_Iso(o["createdAt"] | ""), o["name"] | "");
    }
    page.more = orders["pageInfo"]["hasNextPage"] | false;
    snprintf(page.cursor, sizeof(page.cursor), "%s", (const char*)(orders["pageInfo"]["endCursor"] | ""));
    if (!page.cursor[0]) page.more = false;
    return BCC_OK;
}

uint8_t bcc_Parse_Shopify_Token(const char* body, size_t length, int code, char* token, size_t tokenSize, uint32_t& expiresIn,
                                char* why, size_t ws){
    JsonDocument doc;
    const bool parsed = body && !deserializeJson(doc, body, length);
    if (code == 200 && parsed && doc["access_token"].is<const char*>()){
        snprintf(token, tokenSize, "%s", doc["access_token"].as<const char*>());
        expiresIn = doc["expires_in"] | 86399u;
        return BCC_OK;
    }
    const char* description = parsed ? (doc["error_description"] | (doc["error"] | "")) : "";
    if (strstr(description, "shop_not_permitted")){
        why_Set(why, ws, "The app and the store must be in the same Shopify organization");
        return BCC_KEY;
    }
    if (code == 400 || code == 401 || code == 403){ why_Set(why, ws, "Shopify refused the client ID or secret"); return BCC_KEY; }
    if (code == 404){ why_Set(why, ws, "No such store: check the myshopify.com name"); return BCC_KEY; }
    return http_State(code, "Shopify", why, ws);
}

// YouTube --------------------------------------------------------------------------------------------------------
static int64_t number_Text(JsonVariantConst v){
    if (v.is<const char*>()) return strtoll(v.as<const char*>(), nullptr, 10);
    return v | (int64_t)0;
}

uint8_t bcc_Parse_Youtube(const char* body, size_t length, int code, Bcc_Creator_t& c, char* why, size_t ws){
    JsonDocument filter;
    filter["items"][0]["snippet"]["title"] = true;
    filter["items"][0]["statistics"] = true;
    filter["error"]["message"] = true;
    filter["error"]["errors"][0]["reason"] = true;
    JsonDocument doc;
    const bool parsed = body && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    if (code != 200){
        const char* reason = parsed ? (doc["error"]["errors"][0]["reason"] | "") : "";
        if (!strcmp(reason, "quotaExceeded") || !strcmp(reason, "rateLimitExceeded")){ why_Set(why, ws, "YouTube: today's quota is used up"); return BCC_LIMIT; }
        if (!strcmp(reason, "keyInvalid") || (code == 400 && has(parsed ? (doc["error"]["message"] | "") : "", "key"))){
            why_Set(why, ws, "YouTube refused the API key");
            return BCC_KEY;
        }
        if (!strcmp(reason, "accessNotConfigured")){ why_Set(why, ws, "Turn on YouTube Data API v3 for this key's project"); return BCC_KEY; }
        if (code == 403){ why_Set(why, ws, "YouTube refused the key (check its restrictions)"); return BCC_KEY; }
        return http_State(code, "YouTube", why, ws);
    }
    if (!parsed){ why_Set(why, ws, "YouTube sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    JsonObject item = doc["items"][0];
    if (item.isNull()){ why_Set(why, ws, "No such channel: check the @handle or channel ID"); return BCC_KEY; }
    JsonObject st = item["statistics"];
    c.subsHidden = (st["hiddenSubscriberCount"] | false) ? 1 : 0;
    c.subs = c.subsHidden ? -1 : number_Text(st["subscriberCount"]);
    c.views = number_Text(st["viewCount"]);
    c.videos = (uint32_t)number_Text(st["videoCount"]);
    snprintf(c.channel, sizeof(c.channel), "%s", (const char*)(item["snippet"]["title"] | ""));
    return BCC_OK;
}

// GitHub ---------------------------------------------------------------------------------------------------------
uint8_t bcc_Parse_Github(const char* body, size_t length, int code, int repo, Bcc_Creator_t& c, char* why, size_t ws){
    JsonDocument filter;
    for (const char* f : {"full_name", "stargazers_count", "forks_count", "followers", "login", "message"}) filter[f] = true;
    JsonDocument doc;
    const bool parsed = body && !deserializeJson(doc, body, length, DeserializationOption::Filter(filter));
    if (code != 200){
        const char* message = parsed ? (doc["message"] | "") : "";
        if (code == 404){
            why_Set(why, ws, repo >= 0 ? "Repo not found (a private one needs a token)" : "No such GitHub user");
            return BCC_KEY;
        }
        if ((code == 403 || code == 429) && has(message, "rate limit")){ why_Set(why, ws, "GitHub: hourly limit reached, waiting"); return BCC_LIMIT; }
        if (code == 401){ why_Set(why, ws, "GitHub refused the token"); return BCC_KEY; }
        return http_State(code, "GitHub", why, ws);
    }
    if (!parsed){ why_Set(why, ws, "GitHub sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    if (repo < 0){
        if (!doc["followers"].is<long>()){ why_Set(why, ws, "GitHub sent no followers count"); return BCC_ERROR; }
        c.followers = doc["followers"] | 0u;
        return BCC_OK;
    }
    if (repo >= BCC_REPOS || !doc["stargazers_count"].is<long>()){ why_Set(why, ws, "GitHub sent no stars count"); return BCC_ERROR; }
    c.repoStars[repo] = doc["stargazers_count"] | 0u;
    const char* name = doc["full_name"] | "";
    const char* slash = strchr(name, '/');
    snprintf(c.repoName[repo], sizeof(c.repoName[repo]), "%s", slash ? slash + 1 : name);
    return BCC_OK;
}

// Plausible ------------------------------------------------------------------------------------------------------
static uint8_t plausible_Error(int code, const char* body, size_t length, char* why, size_t ws){
    if (code == 401){ why_Set(why, ws, "Plausible refused the API key"); return BCC_KEY; }
    if (code == 402){ why_Set(why, ws, "Plausible's Stats API needs its Business plan"); return BCC_PLAN; }
    if (code == 404){ why_Set(why, ws, "Plausible has no such site for this key"); return BCC_KEY; }
    JsonDocument doc;
    if (body && !deserializeJson(doc, body, length) && doc["error"].is<const char*>()){
        const char* e = doc["error"];
        if (has(e, "business") || has(e, "upgrade") || has(e, "plan")){
            why_Set(why, ws, "Plausible's Stats API needs its Business plan");
            return BCC_PLAN;
        }
        if (code == 400 || code == 403){ char t[96]; snprintf(t, sizeof(t), "Plausible: %.80s", e); why_Set(why, ws, t); return code == 403 ? BCC_KEY : BCC_ERROR; }
    }
    if (code == 403){ why_Set(why, ws, "Plausible refused the key for this site"); return BCC_KEY; }
    return http_State(code, "Plausible", why, ws);
}

uint8_t bcc_Parse_Plausible_Live(const char* body, size_t length, int code, Bcc_Creator_t& c, char* why, size_t ws){
    if (code != 200) return plausible_Error(code, body, length, why, ws);
    char number[16] = "";
    size_t n = 0;
    for (size_t i = 0; body && i < length && n + 1 < sizeof(number); i++){
        if (isdigit((unsigned char)body[i])) number[n++] = body[i];
        else if (n || (body[i] != ' ' && body[i] != '\n' && body[i] != '\r')) break;
    }
    number[n] = '\0';
    if (!n){ why_Set(why, ws, "Plausible sent an answer Pixlpal cannot read"); return BCC_ERROR; }
    c.live = (int32_t)strtol(number, nullptr, 10);
    return BCC_OK;
}

uint8_t bcc_Parse_Plausible_Today(const char* body, size_t length, int code, Bcc_Creator_t& c, char* why, size_t ws){
    if (code != 200) return plausible_Error(code, body, length, why, ws);
    JsonDocument doc;
    if (!body || deserializeJson(doc, body, length) || !doc["results"][0]["metrics"].is<JsonArray>()){
        why_Set(why, ws, "Plausible sent an answer Pixlpal cannot read");
        return BCC_ERROR;
    }
    JsonArray m = doc["results"][0]["metrics"];
    c.visitors = m[0] | 0;
    c.pageviews = m[1] | 0;
    return BCC_OK;
}
