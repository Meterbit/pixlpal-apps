/**
 * @file fbl_Stream.h
 * @brief Walks an API-Football answer as it streams in: reads "errors", then hands each element of "response" to
 *        a callback, parsed alone through a filter. The reader needs read(), readBytes(), unread(c), find(token) and
 *        next_Non_Space(); it is a template so the walk can be tested on a PC against saved answers.
 */
#ifndef FBL_STREAM_H
#define FBL_STREAM_H

#include "fbl_Model.h"

/** @brief Called for each element of "response" (or once with the object, for /status). Return false to stop. */
typedef bool (*Fbl_Item_Fn)(JsonVariantConst item, void* context);

enum Fbl_Walk_t : uint8_t { FBL_WALK_OK = 0, FBL_WALK_API_ERROR, FBL_WALK_PARSE };

/**
 * @param kind   Receives the Fbl_Error_t of the "errors" value; @p error its message.
 * @return FBL_WALK_OK when the whole "response" was read (or the callback stopped it).
 */
template <typename Reader>
uint8_t fbl_Walk(Reader& reader, JsonDocument& doc, JsonDocument& filter, Fbl_Item_Fn onItem, void* context,
                 uint8_t& kind, char* error, size_t errorSize, uint16_t& items){
    items = 0;
    kind = FBL_ERR_NONE;
    // "errors" comes before "response" in every answer.
    if (!reader.find("\"errors\":") || deserializeJson(doc, reader) != DeserializationError::Ok) return FBL_WALK_PARSE;
    kind = fbl_Classify_Errors(doc.as<JsonVariantConst>(), error, errorSize);
    if (kind != FBL_ERR_NONE) return FBL_WALK_API_ERROR;
    if (!reader.find("\"response\":")) return FBL_WALK_PARSE;
    const int first = reader.next_Non_Space();
    if (first == '{'){
        reader.unread(first);
        if (deserializeJson(doc, reader, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return FBL_WALK_PARSE;
        items = 1;
        onItem(doc.as<JsonVariantConst>(), context);
        return FBL_WALK_OK;
    }
    if (first != '[') return FBL_WALK_PARSE;
    for (;;){
        int c = reader.next_Non_Space();
        if (c == ']') return FBL_WALK_OK;
        if (c == ',') c = reader.next_Non_Space();
        if (c != '{') return FBL_WALK_PARSE;
        reader.unread(c);
        if (deserializeJson(doc, reader, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return FBL_WALK_PARSE;
        items++;
        if (!onItem(doc.as<JsonVariantConst>(), context)) return FBL_WALK_OK;
    }
}

#endif
