/**
 * @file mtb_kit_json.h
 * @brief Reading phone commands: numbers the phone may send as 30, 30.0, true or "30".
 */
#ifndef MTB_KIT_JSON_H
#define MTB_KIT_JSON_H

#include <ArduinoJson.h>
#include <math.h>
#include <stdlib.h>

/** @brief The number in @p field, or false if it is missing or not a number. */
bool mtb_Kit_Json_Number(JsonVariantConst field, long& value);

/** @brief A number in [low, high], or false. */
bool mtb_Kit_Json_Range(JsonVariantConst field, long low, long high, long& value);

#endif
