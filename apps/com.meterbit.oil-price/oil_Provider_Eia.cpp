/**
 * U.S. Energy Information Administration data provider.
 *
 * Prices and production come from the EIA open data API v2 (https://www.eia.gov/opendata/), which needs a free
 * key. Headlines come from the Today in Energy page (https://www.eia.gov/todayinenergy/), which needs none. EIA
 * content is in the U.S. public domain; EIA asks to be named as the source, with the publication date, and does
 * not allow its name to suggest that it endorses a product, or its logo to be used. See README.md.
 *
 * EIA's robots.txt disallows /rss, so the RSS feeds are not used. The Today in Energy page is allowed.
 */
#include "oil_Provider.h"
#include "oil_Http.h"
#include "oil_Config.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char TAG[] = "OIL_EIA";
static const char EIA_API[] = "https://api.eia.gov/v2/";
static const char EIA_NEWS_URL[] = "https://www.eia.gov/todayinenergy/";

// "[" and "]" in the query are sent encoded. These pieces go into printf formats, hence "%%".
#define EIA_ARR(name)   name "%%5B%%5D="
#define EIA_SORT_ASC    "&sort%%5B0%%5D%%5Bcolumn%%5D=period&sort%%5B0%%5D%%5Bdirection%%5D=asc"
#define EIA_VALUE       "&data%%5B0%%5D=value"

struct Eia_Price_t {
    const char* code;
    const char* name;
    const char* route;
    const char* series;
    Oil_Unit_t unit;
};

// Daily spot series, checked against the API's series lists on 2026-11-05.
static const Eia_Price_t EIA_PRICES[] = {
    {"BRENT",    "Brent spot FOB, Europe",          "petroleum/pri/spt",   "RBRTE",                      OIL_UNIT_USD_BBL},
    {"WTI",      "WTI spot, Cushing OK",            "petroleum/pri/spt",   "RWTC",                       OIL_UNIT_USD_BBL},
    {"HH GAS",   "Henry Hub natural gas spot",      "natural-gas/pri/fut", "RNGWHHD",                    OIL_UNIT_USD_MMBTU},
    {"GASOLINE", "Gasoline, NY Harbor",             "petroleum/pri/spt",   "EER_EPMRU_PF4_Y35NY_DPG",    OIL_UNIT_USD_GAL},
    {"DIESEL",   "ULS diesel, NY Harbor",           "petroleum/pri/spt",   "EER_EPD2DXL0_PF4_Y35NY_DPG", OIL_UNIT_USD_GAL},
    {"HEAT OIL", "Heating oil, NY Harbor",          "petroleum/pri/spt",   "EER_EPD2F_PF4_Y35NY_DPG",    OIL_UNIT_USD_GAL},
    {"JET",      "Jet fuel, US Gulf Coast",         "petroleum/pri/spt",   "EER_EPJK_PF4_RGC_DPG",       OIL_UNIT_USD_GAL},
    {"PROPANE",  "Propane, Mont Belvieu TX",        "petroleum/pri/spt",   "EER_EPLLPA_PF4_Y44MB_DPG",   OIL_UNIT_USD_GAL},
};
static const char* const EIA_PRICE_ROUTES[] = {"petroleum/pri/spt", "natural-gas/pri/fut"};

// Monthly crude oil production (incl. lease condensate), thousand barrels a day, from the international data set.
static const struct { const char* code; const char* name; const char* region; } EIA_SUPPLY[] = {
    {"NIGERIA", "Nigeria crude output",  "NGA"},
    {"OPEC",    "OPEC crude output",     "OPEC"},
};

// Bonny Light has no EIA spot price. EIA does publish the monthly F.O.B. cost of U.S. crude imports by crude
// stream; the series is looked up by name, because its id could not be checked when this was written.
#define EIA_BONNY_CODE      "BONNY LT"
#define EIA_BONNY_NAME      "Bonny Light, US import FOB"
#define EIA_BONNY_ROUTE     "petroleum/pri/imc2"
#define EIA_BONNY_MATCH     "bonny light"

#define EIA_URL_LEN         1024
#define EIA_MAX_ARTICLES    16
#define EIA_NEWS_MAX_AGE    120     // Days. Older articles are left out.

//##############################################################################################################
// Collects one row's fields, then stores the value in the series the row belongs to.
struct Eia_Rows_t {
    Oil_Data_t* data;
    const char* idField;    // "series" or "countryRegionId"
    uint8_t kind;           // Only series of this kind are matched.
    char period[12];
    char id[OIL_SOURCE_ID_LEN];
    char value[32];
    bool hasValue;
    int stored;
};

static void eia_Field(void* context, const char* key, const char* value, bool isNull){
    Eia_Rows_t* rows = (Eia_Rows_t*)context;
    if (strcmp(key, "period") == 0) snprintf(rows->period, sizeof(rows->period), "%s", value);
    else if (strcmp(key, rows->idField) == 0) snprintf(rows->id, sizeof(rows->id), "%s", value);
    else if (strcmp(key, "value") == 0){
        snprintf(rows->value, sizeof(rows->value), "%s", value);
        rows->hasValue = !isNull;
    }
}

static bool eia_Row(void* context){
    Eia_Rows_t* rows = (Eia_Rows_t*)context;
    const int32_t day = oil_Parse_Iso_Date(rows->period);
    char* end = nullptr;
    const float value = rows->hasValue ? strtof(rows->value, &end) : 0;
    // A withheld value is null or a letter such as "W" or "NA".
    if (day != 0 && rows->hasValue && end != rows->value){
        for (int i = 0; i < rows->data->seriesCount; i++){
            Oil_Series_t& series = rows->data->series[i];
            if (series.kind != rows->kind || strcmp(series.sourceId, rows->id) != 0) continue;
            oil_Series_Put(series, day, value);
            rows->stored++;
            break;
        }
    }
    rows->period[0] = rows->id[0] = rows->value[0] = '\0';
    rows->hasValue = false;
    return true;
}

static bool eia_Data_Body(Oil_Byte_Source_t& body, void* context){
    return oil_Json_Rows(body, "data", eia_Field, eia_Row, context) >= 0;
}

static int eia_Append(char* url, int used, const char* format, ...) __attribute__((format(printf, 3, 4)));
static int eia_Append(char* url, int used, const char* format, ...){
    if (used < 0 || used >= EIA_URL_LEN) return EIA_URL_LEN;
    va_list args;
    va_start(args, format);
    const int added = vsnprintf(url + used, EIA_URL_LEN - used, format, args);
    va_end(args);
    return (added < 0) ? EIA_URL_LEN : used + added;
}

// Earliest date to ask for: the newest value held less a few days, which EIA may have revised, or the full
// history for a series that has none.
static int32_t eia_Start_Day(const Oil_Series_t& series, int32_t today, int32_t coldDays, int32_t overlapDays){
    const Oil_Point_t* last = oil_Series_Last(series);
    return last != nullptr ? last->day - overlapDays : today - coldDays;
}

//##############################################################################################################
class Oil_Provider_Eia_t : public Oil_Provider_t {
public:
    const char* id(void) const override { return "eia"; }

    void describe(Oil_Data_t& data) const override {
        snprintf(data.provider, sizeof(data.provider), "%s", id());
        snprintf(data.sourceName, sizeof(data.sourceName), "U.S. EIA");
        snprintf(data.sourceFull, sizeof(data.sourceFull), "U.S. Energy Information Administration");
        snprintf(data.newsSource, sizeof(data.newsSource), "EIA Today in Energy");
        data.seriesCount = 0;
        for (const Eia_Price_t& price : EIA_PRICES) add(data, price.code, price.name, price.series, price.unit, OIL_DAILY, OIL_KIND_PRICE);
        add(data, EIA_BONNY_CODE, EIA_BONNY_NAME, "", OIL_UNIT_USD_BBL, OIL_MONTHLY, OIL_KIND_REGIONAL);
        for (const auto& supply : EIA_SUPPLY) add(data, supply.code, supply.name, supply.region, OIL_UNIT_KBD, OIL_MONTHLY, OIL_KIND_SUPPLY);
    }

    bool needs_Key(Oil_Feed_t feed) const override { return feed != OIL_FEED_NEWS; }

    Oil_Fetch_Result_t fetch(Oil_Feed_t feed, Oil_Data_t& data, const char* apiKey, uint32_t nowUnix) override {
        const int32_t today = (int32_t)(nowUnix / 86400);
        switch (feed){
            case OIL_FEED_PRICES: return fetch_Prices(data, apiKey, nowUnix, today);
            case OIL_FEED_SUPPLY: return fetch_Supply(data, apiKey, nowUnix, today);
            case OIL_FEED_NEWS:   return fetch_News(data, nowUnix, today);
            default:              return {OIL_ERR_NOT_SUPPORTED, 0, 0};
        }
    }

private:
    static void add(Oil_Data_t& data, const char* code, const char* name, const char* sourceId, Oil_Unit_t unit, Oil_Cadence_t cadence, Oil_Kind_t kind){
        Oil_Series_t& s = data.series[data.seriesCount++];
        memset(&s, 0, sizeof(s));
        snprintf(s.code, sizeof(s.code), "%s", code);
        snprintf(s.name, sizeof(s.name), "%s", name);
        snprintf(s.sourceId, sizeof(s.sourceId), "%s", sourceId);
        s.unit = unit;
        s.cadence = cadence;
        s.kind = kind;
    }

    // One request per API route, for all of that route's series.
    Oil_Fetch_Result_t fetch_Prices(Oil_Data_t& data, const char* apiKey, uint32_t nowUnix, int32_t today){
        char* url = (char*)heap_caps_malloc(EIA_URL_LEN, MALLOC_CAP_SPIRAM);
        if (url == nullptr) return {OIL_ERR_NETWORK, -1, 0};
        Oil_Fetch_Result_t outcome = {OIL_OK, 200, 0};
        for (const char* route : EIA_PRICE_ROUTES){
            int32_t start = today;
            int used = snprintf(url, EIA_URL_LEN, "%s%s/data/?api_key=%s&frequency=daily" EIA_VALUE, EIA_API, route, apiKey);
            for (const Eia_Price_t& price : EIA_PRICES){
                if (strcmp(price.route, route) != 0) continue;
                const int index = oil_Find_Series(data, price.code);
                if (index < 0) continue;
                const int32_t from = eia_Start_Day(data.series[index], today, OIL_COLD_HISTORY_DAYS, OIL_OVERLAP_DAYS);
                if (from < start) start = from;
                used = eia_Append(url, used, "&" EIA_ARR("facets%%5Bseries%%5D") "%s", price.series);
            }
            char startText[12];
            oil_Format_Iso_Date(startText, sizeof(startText), start, false);
            used = eia_Append(url, used, "&start=%s" EIA_SORT_ASC "&length=5000", startText);
            if (used >= EIA_URL_LEN){ outcome = {OIL_ERR_PARSE, 0, 0}; break; }

            Eia_Rows_t rows = {};
            rows.data = &data;
            rows.idField = "series";
            rows.kind = OIL_KIND_PRICE;
            outcome = oil_Http_Get(url, route, eia_Data_Body, &rows);
            if (outcome.result != OIL_OK) break;
            ESP_LOGI(TAG, "%s: %d values since %s", route, rows.stored, startText);
            for (const Eia_Price_t& price : EIA_PRICES){
                const int index = oil_Find_Series(data, price.code);
                if (index >= 0 && strcmp(price.route, route) == 0) data.series[index].fetched = nowUnix;
            }
        }
        heap_caps_free(url);
        return outcome;
    }

    Oil_Fetch_Result_t fetch_Supply(Oil_Data_t& data, const char* apiKey, uint32_t nowUnix, int32_t today){
        char* url = (char*)heap_caps_malloc(EIA_URL_LEN, MALLOC_CAP_SPIRAM);
        if (url == nullptr) return {OIL_ERR_NETWORK, -1, 0};

        int32_t start = today;
        int used = snprintf(url, EIA_URL_LEN, "%sinternational/data/?api_key=%s&frequency=monthly" EIA_VALUE
                            "&" EIA_ARR("facets%%5BproductId%%5D") "57&" EIA_ARR("facets%%5BactivityId%%5D") "1"
                            "&" EIA_ARR("facets%%5Bunit%%5D") "TBPD", EIA_API, apiKey);
        for (const auto& supply : EIA_SUPPLY){
            const int index = oil_Find_Series(data, supply.code);
            if (index < 0) continue;
            const int32_t from = eia_Start_Day(data.series[index], today, OIL_SUPPLY_MONTHS * 31, 100);
            if (from < start) start = from;
            used = eia_Append(url, used, "&" EIA_ARR("facets%%5BcountryRegionId%%5D") "%s", supply.region);
        }
        char startText[12];
        oil_Format_Iso_Date(startText, sizeof(startText), start, true);
        used = eia_Append(url, used, "&start=%s" EIA_SORT_ASC "&length=5000", startText);

        Eia_Rows_t rows = {};
        rows.data = &data;
        rows.idField = "countryRegionId";
        rows.kind = OIL_KIND_SUPPLY;
        Oil_Fetch_Result_t outcome = (used < EIA_URL_LEN) ? oil_Http_Get(url, "international", eia_Data_Body, &rows) : Oil_Fetch_Result_t{OIL_ERR_PARSE, 0, 0};
        if (outcome.result == OIL_OK){
            for (const auto& supply : EIA_SUPPLY){
                const int index = oil_Find_Series(data, supply.code);
                if (index >= 0) data.series[index].fetched = nowUnix;
            }
            data.supplyFetched = nowUnix;
            // The regional price is extra: its failure leaves the production figures standing.
            const Oil_Fetch_Result_t bonny = fetch_Bonny(data, apiKey, url, nowUnix, today);
            if (bonny.result == OIL_ERR_AUTH || bonny.result == OIL_ERR_RATE_LIMIT) outcome = bonny;
        }
        heap_caps_free(url);
        return outcome;
    }

    // Finds the Bonny Light series in the route's series list, once, then reads its monthly values.
    Oil_Fetch_Result_t fetch_Bonny(Oil_Data_t& data, const char* apiKey, char* url, uint32_t nowUnix, int32_t today){
        const int index = oil_Find_Series(data, EIA_BONNY_CODE);
        if (index < 0) return {OIL_OK, 0, 0};
        Oil_Series_t& bonny = data.series[index];
        if (bonny.unavailable && nowUnix - bonny.fetched < OIL_CATALOGUE_REFRESH_SEC) return {OIL_OK, 0, 0};

        if (bonny.sourceId[0] == '\0'){
            snprintf(url, EIA_URL_LEN, "%s" EIA_BONNY_ROUTE "/facet/series?api_key=%s", EIA_API, apiKey);
            Eia_Facets_t facets = {};
            const Oil_Fetch_Result_t found = oil_Http_Get(url, "imc2 series", eia_Facet_Body, &facets);
            if (found.result == OIL_ERR_NETWORK || found.result == OIL_ERR_AUTH || found.result == OIL_ERR_RATE_LIMIT) return found;
            bonny.fetched = nowUnix;
            if (found.result != OIL_OK || facets.match[0] == '\0'){
                ESP_LOGI(TAG, "No Bonny Light series in " EIA_BONNY_ROUTE "; looking again in 30 days");
                bonny.unavailable = 1;
                return {OIL_OK, 0, 0};
            }
            snprintf(bonny.sourceId, sizeof(bonny.sourceId), "%s", facets.match);
            bonny.unavailable = 0;
        }

        char startText[12];
        oil_Format_Iso_Date(startText, sizeof(startText), eia_Start_Day(bonny, today, OIL_SUPPLY_MONTHS * 31, 100), true);
        const int used = snprintf(url, EIA_URL_LEN, "%s" EIA_BONNY_ROUTE "/data/?api_key=%s&frequency=monthly" EIA_VALUE
                                  "&" EIA_ARR("facets%%5Bseries%%5D") "%s&start=%s" EIA_SORT_ASC "&length=500", EIA_API, apiKey, bonny.sourceId, startText);
        if (used >= EIA_URL_LEN) return {OIL_OK, 0, 0};
        Eia_Rows_t rows = {};
        rows.data = &data;
        rows.idField = "series";
        rows.kind = OIL_KIND_REGIONAL;
        const Oil_Fetch_Result_t outcome = oil_Http_Get(url, "imc2 data", eia_Data_Body, &rows);
        if (outcome.result == OIL_OK) bonny.fetched = nowUnix;
        else if (outcome.result == OIL_ERR_SERVER || outcome.result == OIL_ERR_PARSE){
            bonny.sourceId[0] = '\0';       // Look the series up again next time.
            bonny.fetched = nowUnix;
            bonny.unavailable = 1;
        }
        return outcome;
    }

    struct Eia_Facets_t {
        char id[OIL_SOURCE_ID_LEN];
        char name[OIL_PARSE_VALUE_LEN];
        char match[OIL_SOURCE_ID_LEN];
    };

    static void eia_Facet_Field(void* context, const char* key, const char* value, bool){
        Eia_Facets_t* facets = (Eia_Facets_t*)context;
        if (strcmp(key, "id") == 0) snprintf(facets->id, sizeof(facets->id), "%s", value);
        else if (strcmp(key, "name") == 0) snprintf(facets->name, sizeof(facets->name), "%s", value);
    }

    static bool eia_Facet_Row(void* context){
        Eia_Facets_t* facets = (Eia_Facets_t*)context;
        char lower[OIL_PARSE_VALUE_LEN];
        size_t i = 0;
        for (; facets->name[i] && i + 1 < sizeof(lower); i++) lower[i] = (char)tolower((unsigned char)facets->name[i]);
        lower[i] = '\0';
        if (facets->match[0] == '\0' && strstr(lower, EIA_BONNY_MATCH) != nullptr) snprintf(facets->match, sizeof(facets->match), "%s", facets->id);
        facets->id[0] = facets->name[0] = '\0';
        return facets->match[0] == '\0';
    }

    static bool eia_Facet_Body(Oil_Byte_Source_t& body, void* context){
        return oil_Json_Rows(body, "facets", eia_Facet_Field, eia_Facet_Row, context) >= 0;
    }

    //##########################################################################################################
    struct Eia_News_t {
        Oil_Headline_t* found;
        int count;
        int32_t oldestDay;
    };

    static bool eia_Article(void* context, const char* dateText, uint32_t id, const char* rawTitle){
        Eia_News_t* news = (Eia_News_t*)context;
        Oil_Headline_t& headline = news->found[news->count];
        oil_Clean_Text(headline.title, sizeof(headline.title), rawTitle);
        headline.day = oil_Parse_Us_Date(dateText);
        headline.ref = id;
        headline.score = oil_Headline_Score(headline.title);
        if (headline.title[0] != '\0' && (headline.day == 0 || headline.day >= news->oldestDay)) news->count++;
        return news->count < EIA_MAX_ARTICLES;
    }

    static bool eia_News_Body(Oil_Byte_Source_t& body, void* context){
        return oil_Tie_Articles(body, eia_Article, context) > 0;
    }

    // Oil and gas first (OPEC, Nigeria and Africa before the rest), newest first within each group.
    static int eia_Rank(const Oil_Headline_t& h){ return h.score >= 3 ? 0 : h.score == 2 ? 1 : 2; }

    Oil_Fetch_Result_t fetch_News(Oil_Data_t& data, uint32_t nowUnix, int32_t today){
        Eia_News_t news = {};
        news.found = (Oil_Headline_t*)heap_caps_calloc(EIA_MAX_ARTICLES, sizeof(Oil_Headline_t), MALLOC_CAP_SPIRAM);
        if (news.found == nullptr) return {OIL_ERR_NETWORK, -1, 0};
        news.oldestDay = today - EIA_NEWS_MAX_AGE;
        Oil_Fetch_Result_t outcome = oil_Http_Get(EIA_NEWS_URL, "todayinenergy", eia_News_Body, &news);
        if (outcome.result == OIL_OK && news.count == 0) outcome.result = OIL_ERR_PARSE;  // The page layout changed.
        if (outcome.result == OIL_OK){
            // Insertion sort: at most EIA_MAX_ARTICLES entries.
            for (int i = 1; i < news.count; i++){
                Oil_Headline_t moving = news.found[i];
                int j = i - 1;
                while (j >= 0 && (eia_Rank(news.found[j]) > eia_Rank(moving) || (eia_Rank(news.found[j]) == eia_Rank(moving) && news.found[j].day < moving.day))){
                    news.found[j + 1] = news.found[j];
                    j--;
                }
                news.found[j + 1] = moving;
            }
            data.headlineCount = (uint8_t)(news.count < OIL_MAX_HEADLINES ? news.count : OIL_MAX_HEADLINES);
            memcpy(data.headlines, news.found, data.headlineCount * sizeof(Oil_Headline_t));
            data.newsFetched = nowUnix;
        }
        heap_caps_free(news.found);
        return outcome;
    }
};

Oil_Provider_t& oil_Provider_Eia(void){
    static Oil_Provider_Eia_t provider;
    return provider;
}
