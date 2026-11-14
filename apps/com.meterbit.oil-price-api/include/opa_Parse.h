/**
 * @file opa_Parse.h
 * @brief Reads OilPriceAPI answers into the model. Each answer is parsed with ArduinoJson through a filter, so only
 *        the fields used are kept in memory. No network or Arduino code here: testable on a PC.
 *
 * Shapes, from docs.oilpriceapi.com (November 2026):
 *   /v1/prices/latest?by_code=A,B  {"status","data":{"prices":[{code,price,currency,unit,updated_at,data_status,
 *                                   stale,changes:{"24h":{percent},"7d":...}}]}}; a single code gives "data":{code,price...}
 *   /v1/demo/prices?codes=A,B      {"data":{"prices":[{code,price,currency,updated_at,change_24h,stale}]}}
 *   /v1/prices/past_*              {"data":{"prices":[{price,created_at,updated_at}]}}
 *   /v1/futures/ice-brent          {"contracts":[{contract_month,last_price}]}
 *   /v1/ei/rig_counts/latest       {"data":{report_date,us_total,week_over_week,year_over_year,basins:{name:{count,change}}}}
 *   /v1/ei/opec_productions/latest {"data":{production_month,opec_total_mbpd,month_over_month,headline,
 *                                   countries:[{country,production_mbpd,quota_mbpd}]}}
 *   errors                         {"error":{"code"}} or {"error_code"} or {"data":{"error","invalid_codes"}}
 */
#ifndef OPA_PARSE_H
#define OPA_PARSE_H

#include "opa_Model.h"
#include <ArduinoJson.h>

void opa_Quotes_Filter(JsonDocument& filter);
/**
 * @brief Updates every watchlist quote from a latest-prices or demo answer. Quotes the answer leaves out are marked
 *        missing. Returns false if the answer holds no prices at all.
 */
bool opa_Parse_Quotes(JsonVariantConst root, Opa_Data_t& data, uint32_t now, bool demo);

void opa_History_Filter(JsonDocument& filter);
/** @brief Fills @p chart's points, oldest first, keeping the newest OPA_CHART_POINTS. Returns false if none. */
bool opa_Parse_History(JsonVariantConst root, Opa_Chart_t& chart);

void opa_Curve_Filter(JsonDocument& filter);
bool opa_Parse_Curve(JsonVariantConst root, Opa_Curve_t& curve);

void opa_Rigs_Filter(JsonDocument& filter);
bool opa_Parse_Rigs(JsonVariantConst root, Opa_Rigs_t& rigs);

void opa_Opec_Filter(JsonDocument& filter);
bool opa_Parse_Opec(JsonVariantConst root, Opa_Opec_t& opec);

void opa_Error_Filter(JsonDocument& filter);
/**
 * @brief The error code of an error answer ("FORBIDDEN", "EMAIL_CONFIRMATION_REQUIRED", "invalid_code"...), or "".
 * @param invalid  Receives the codes an "invalid_code" answer names, comma-separated, if not null.
 */
void opa_Parse_Error(JsonVariantConst root, char* code, size_t size, char* invalid = nullptr, size_t invalidSize = 0);

#endif
