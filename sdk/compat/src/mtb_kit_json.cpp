// The phone-command number helpers, one copy for every app (they were static inline in mtb_kit_json.h: a copy in
// each source file that used them).
#include "mtb_kit_json.h"

/** @brief The number in @p field, or false if it is missing or not a number. */
bool mtb_Kit_Json_Number(JsonVariantConst field, long& value){
    if (field.is<bool>()) value = field.as<bool>() ? 1 : 0;
    else if (field.is<long>()) value = field.as<long>();
    else if (field.is<double>()) value = lround(field.as<double>());
    else if (field.is<const char*>()){
        const char* text = field.as<const char*>();
        char* end = nullptr;
        value = strtol(text, &end, 10);
        if (end == text || *end != '\0') return false;
    } else return false;
    return true;
}

/** @brief A number in [low, high], or false. */
bool mtb_Kit_Json_Range(JsonVariantConst field, long low, long high, long& value){
    return mtb_Kit_Json_Number(field, value) && value >= low && value <= high;
}

