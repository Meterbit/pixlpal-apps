#include "opa_Parse.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void opa_Copy(char* dest, size_t size, const char* text){
    if (size == 0) return;
    size_t i = 0;
    for (; text != nullptr && text[i] && i + 1 < size; i++) dest[i] = (text[i] >= ' ' && text[i] <= '~') ? text[i] : ' ';
    dest[i] = '\0';
}

static float opa_Number(JsonVariantConst value){
    if (value.is<float>() || value.is<long>()) return value.as<float>();
    if (value.is<const char*>()){
        char* end = nullptr;
        const float parsed = strtof(value.as<const char*>(), &end);
        if (end != value.as<const char*>()) return parsed;
    }
    return NAN;
}

//##############################################################################################################
static void opa_Quote_Fields(JsonObject item){
    item["code"] = true;
    item["price"] = true;
    item["currency"] = true;
    item["unit"] = true;
    item["updated_at"] = true;
    item["created_at"] = true;
    item["data_status"] = true;
    item["stale"] = true;
    item["change_24h"] = true;
    static const char* const windows[] = {"24h", "7d", "30d", "90d"};
    for (const char* w : windows) item["changes"][w]["percent"] = true;
}

void opa_Quotes_Filter(JsonDocument& filter){
    filter.clear();
    opa_Quote_Fields(filter["data"]["prices"][0].to<JsonObject>());
    opa_Quote_Fields(filter["data"].as<JsonObject>());
}

static bool opa_Put_Quote(JsonVariantConst item, Opa_Data_t& data, uint32_t now, bool demo){
    const char* code = item["code"];
    if (code == nullptr) return false;
    const int index = opa_Find_Quote(data, code);
    if (index < 0) return false;
    const float price = opa_Number(item["price"]);
    if (isnan(price)) return false;
    Opa_Quote_t& quote = data.quotes[index];
    uint32_t observed = opa_Parse_Iso_Time(item["updated_at"]);
    if (observed == 0) observed = opa_Parse_Iso_Time(item["created_at"]);
    if (observed == 0 || observed > now + 3600) observed = now;
    opa_Quote_Put(quote, price, observed, now);
    opa_Copy(quote.currency, sizeof(quote.currency), item["currency"] | "");
    opa_Copy(quote.unit, sizeof(quote.unit), item["unit"] | "");
    static const char* const windows[] = {"24h", "7d", "30d", "90d"};
    for (int w = 0; w < OPA_W_COUNT; w++) quote.change[w] = opa_Number(item["changes"][windows[w]]["percent"]);
    if (demo || isnan(quote.change[OPA_W_24H])){
        const float demoChange = opa_Number(item["change_24h"]);       // Per cent, in the demo answer.
        if (!isnan(demoChange)) quote.change[OPA_W_24H] = demoChange;
    }
    const char* status = item["data_status"];
    const bool stale = (item["stale"] | false) || (status != nullptr && strcmp(status, "stale") == 0);
    quote.state = stale ? OPA_Q_STALE : OPA_Q_CURRENT;
    return true;
}

bool opa_Parse_Quotes(JsonVariantConst root, Opa_Data_t& data, uint32_t now, bool demo){
    bool seen[OPA_MAX_WATCH] = {};
    int found = 0;
    auto take = [&](JsonVariantConst item){
        const char* code = item["code"];
        const int index = code ? opa_Find_Quote(data, code) : -1;
        if (index >= 0 && !seen[index] && opa_Put_Quote(item, data, now, demo)){ seen[index] = true; found++; }
    };
    JsonVariantConst prices = root["data"]["prices"];
    if (prices.is<JsonArrayConst>()) for (JsonVariantConst item : prices.as<JsonArrayConst>()) take(item);
    else if (root["data"]["price"].is<float>() || root["data"]["price"].is<long>()) take(root["data"]);
    if (found == 0) return false;
    // Left out: not on this plan, not in the demo, or stale and withheld from a multi-code answer.
    for (int i = 0; i < data.quoteCount; i++) if (!seen[i]) data.quotes[i].state = OPA_Q_MISSING;
    data.quotesFetched = now;
    data.demo = demo;
    return true;
}

//##############################################################################################################
void opa_History_Filter(JsonDocument& filter){
    filter.clear();
    JsonObject item = filter["data"]["prices"][0].to<JsonObject>();
    item["price"] = true;
    item["created_at"] = true;
    item["updated_at"] = true;
}

// Inserts a point in time order. Full: the oldest goes, unless the new point is older still.
static void opa_Chart_Insert(Opa_Chart_t& chart, uint32_t t, float v){
    int at = chart.count;
    while (at > 0 && chart.points[at - 1].t > t) at--;
    if (at > 0 && chart.points[at - 1].t == t){ chart.points[at - 1].v = v; return; }
    if (chart.count >= OPA_CHART_POINTS){
        if (at == 0) return;
        memmove(chart.points, chart.points + 1, sizeof(Opa_Point_t) * (at - 1));
        chart.points[at - 1] = {t, v};
        return;
    }
    memmove(chart.points + at + 1, chart.points + at, sizeof(Opa_Point_t) * (chart.count - at));
    chart.points[at] = {t, v};
    chart.count++;
}

bool opa_Parse_History(JsonVariantConst root, Opa_Chart_t& chart){
    chart.count = 0;
    JsonVariantConst prices = root["data"]["prices"];
    if (!prices.is<JsonArrayConst>()) return false;
    for (JsonVariantConst item : prices.as<JsonArrayConst>()){
        const float price = opa_Number(item["price"]);
        uint32_t t = opa_Parse_Iso_Time(item["created_at"]);
        if (t == 0) t = opa_Parse_Iso_Time(item["updated_at"]);
        if (!isnan(price) && t != 0) opa_Chart_Insert(chart, t, price);
    }
    return chart.count > 0;
}

//##############################################################################################################
void opa_Curve_Filter(JsonDocument& filter){
    filter.clear();
    JsonObject item = filter["contracts"][0].to<JsonObject>();
    item["contract_month"] = true;
    item["last_price"] = true;
    item["close"] = true;
    JsonObject wrapped = filter["data"]["contracts"][0].to<JsonObject>();
    wrapped["contract_month"] = true;
    wrapped["last_price"] = true;
    wrapped["close"] = true;
}

bool opa_Parse_Curve(JsonVariantConst root, Opa_Curve_t& curve){
    JsonVariantConst contracts = root["contracts"];
    if (!contracts.is<JsonArrayConst>()) contracts = root["data"]["contracts"];
    if (!contracts.is<JsonArrayConst>()) return false;
    curve.count = 0;
    for (JsonVariantConst item : contracts.as<JsonArrayConst>()){
        const char* month = item["contract_month"];
        float price = opa_Number(item["last_price"]);
        if (isnan(price)) price = opa_Number(item["close"]);
        if (month == nullptr || strlen(month) < 7 || isnan(price) || price <= 0) continue;
        // In delivery order, whatever order they came in; later duplicates replace earlier ones.
        int at = 0;
        while (at < curve.count && strncmp(curve.month[at], month, 7) < 0) at++;
        if (at < curve.count && strncmp(curve.month[at], month, 7) == 0){ curve.price[at] = price; continue; }
        if (curve.count >= OPA_CURVE_MAX){
            if (at >= OPA_CURVE_MAX) continue;      // Only the nearest months are kept.
            curve.count--;
        }
        memmove(curve.month[at + 1], curve.month[at], sizeof(curve.month[0]) * (curve.count - at));
        memmove(curve.price + at + 1, curve.price + at, sizeof(float) * (curve.count - at));
        opa_Copy(curve.month[at], sizeof(curve.month[at]), month);
        curve.month[at][7] = '\0';
        curve.price[at] = price;
        curve.count++;
    }
    return curve.count >= 2;
}

//##############################################################################################################
void opa_Rigs_Filter(JsonDocument& filter){
    filter.clear();
    JsonObject data = filter["data"].to<JsonObject>();
    data["report_date"] = true;
    data["us_total"] = true;
    data["week_over_week"] = true;
    data["year_over_year"] = true;
    data["basins"] = true;
}

bool opa_Parse_Rigs(JsonVariantConst root, Opa_Rigs_t& rigs){
    JsonVariantConst data = root["data"];
    const float total = opa_Number(data["us_total"]);
    if (isnan(total)) return false;
    const uint32_t fetched = rigs.fetched;
    memset(&rigs, 0, sizeof(rigs));
    rigs.fetched = fetched;
    rigs.total = (int16_t)lroundf(total);
    rigs.weekChange = (int16_t)(data["week_over_week"] | 0);
    rigs.yearChange = (int16_t)(data["year_over_year"] | 0);
    opa_Copy(rigs.date, sizeof(rigs.date), data["report_date"] | "");
    // The basins with the most rigs, most first.
    int count = 0;
    for (JsonPairConst basin : data["basins"].as<JsonObjectConst>()){
        const int16_t n = (int16_t)(basin.value()["count"] | 0);
        int at = count;
        while (at > 0 && rigs.basinCount[at - 1] < n) at--;
        if (at >= OPA_BASINS) continue;
        const int last = count < OPA_BASINS ? count : OPA_BASINS - 1;
        for (int k = last; k > at; k--){
            memcpy(rigs.basin[k], rigs.basin[k - 1], sizeof(rigs.basin[0]));
            rigs.basinCount[k] = rigs.basinCount[k - 1];
            rigs.basinChange[k] = rigs.basinChange[k - 1];
        }
        opa_Copy(rigs.basin[at], sizeof(rigs.basin[at]), basin.key().c_str());
        for (char* c = rigs.basin[at]; *c; c++) *c = (*c == '_') ? ' ' : (char)toupper((unsigned char)*c);
        rigs.basinCount[at] = n;
        rigs.basinChange[at] = (int16_t)(basin.value()["change"] | 0);
        if (count < OPA_BASINS) count++;
    }
    return true;
}

//##############################################################################################################
void opa_Opec_Filter(JsonDocument& filter){
    filter.clear();
    JsonObject data = filter["data"].to<JsonObject>();
    data["production_month"] = true;
    data["report_month"] = true;
    data["opec_total_mbpd"] = true;
    data["month_over_month"] = true;
    data["headline"] = true;
    JsonObject country = data["countries"][0].to<JsonObject>();
    country["country"] = true;
    country["production_mbpd"] = true;
    country["quota_mbpd"] = true;
}

bool opa_Parse_Opec(JsonVariantConst root, Opa_Opec_t& opec){
    JsonVariantConst data = root["data"];
    const float total = opa_Number(data["opec_total_mbpd"]);
    if (isnan(total)) return false;
    const uint32_t fetched = opec.fetched;
    memset(&opec, 0, sizeof(opec));
    opec.fetched = fetched;
    opec.total = total;
    opec.monthChange = opa_Number(data["month_over_month"]);
    opec.nigeria = NAN;
    opec.nigeriaQuota = NAN;
    const char* month = data["production_month"] | (data["report_month"] | "");
    opa_Copy(opec.month, sizeof(opec.month), month);
    opa_Copy(opec.headline, sizeof(opec.headline), data["headline"] | "");
    for (JsonVariantConst country : data["countries"].as<JsonArrayConst>()){
        const char* name = country["country"];
        if (name == nullptr || strcmp(name, "nigeria") != 0) continue;
        opec.nigeria = opa_Number(country["production_mbpd"]);
        opec.nigeriaQuota = opa_Number(country["quota_mbpd"]);
    }
    return true;
}

//##############################################################################################################
void opa_Error_Filter(JsonDocument& filter){
    filter.clear();
    filter["error"] = true;
    filter["error_code"] = true;
    filter["data"]["error"] = true;
    filter["data"]["invalid_codes"] = true;
}

void opa_Parse_Error(JsonVariantConst root, char* code, size_t size, char* invalid, size_t invalidSize){
    const char* text = root["error"]["code"];
    if (text == nullptr) text = root["error_code"];
    if (text == nullptr) text = root["data"]["error"];
    if (text == nullptr && root["error"].is<const char*>()) text = root["error"];
    opa_Copy(code, size, text ? text : "");
    if (invalid == nullptr || invalidSize == 0) return;
    invalid[0] = '\0';
    size_t used = 0;
    for (JsonVariantConst bad : root["data"]["invalid_codes"].as<JsonArrayConst>()){
        const char* name = bad.as<const char*>();
        if (name == nullptr) continue;
        const int written = snprintf(invalid + used, invalidSize - used, "%s%s", used ? "," : "", name);
        if (written < 0 || used + written >= invalidSize) break;
        used += written;
    }
}
