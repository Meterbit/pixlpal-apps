/**
 * @file oil_Parse.h
 * @brief Streaming parsers for the Oil price data providers.
 *
 * Both parsers read a response one byte at a time and keep only the field being read, so a response of any
 * size is parsed in well under 1 KB of RAM. Neither depends on Arduino or ESP-IDF.
 */
#ifndef OIL_PARSE_H
#define OIL_PARSE_H

#include <stdint.h>
#include <stddef.h>

#define OIL_PARSE_KEY_LEN    32
#define OIL_PARSE_VALUE_LEN  192

/** @brief Where a parser reads from. read() returns the next byte, or -1 at the end of the response. */
class Oil_Byte_Source_t {
public:
    virtual ~Oil_Byte_Source_t() {}
    virtual int read(void) = 0;
};

/** @brief A byte source over a buffer, for tests. */
class Oil_Buffer_Source_t : public Oil_Byte_Source_t {
public:
    Oil_Buffer_Source_t(const char* data, size_t length) : data(data), length(length) {}
    int read(void) override { return position < length ? (uint8_t)data[position++] : -1; }
private:
    const char* data;
    size_t length;
    size_t position = 0;
};

/**
 * @brief Called for each scalar field of a row object.
 * @param value   The text of a string, or the literal text of a number or true/false. Long values are cut.
 * @param isNull  true for a JSON null; @p value is then "".
 */
typedef void (*Oil_Json_Field_Fn)(void* context, const char* key, const char* value, bool isNull);
/** @brief Called after each row object. Returning false stops the parse. */
typedef bool (*Oil_Json_Row_Fn)(void* context);

/**
 * @brief Reads every object of the first array named @p arrayKey, at any depth, e.g. EIA's "data": [ {...}, ... ].
 *        Fields holding an object or an array are skipped.
 * @return The number of rows read, or -1 if the array was not found or the JSON is broken.
 */
int oil_Json_Rows(Oil_Byte_Source_t& in, const char* arrayKey, Oil_Json_Field_Fn onField, Oil_Json_Row_Fn onRow, void* context);

/** @brief Called for each article. Returning false stops the parse. */
typedef bool (*Oil_Article_Fn)(void* context, const char* dateText, uint32_t id, const char* rawTitle);

/**
 * @brief Reads the article list of EIA's Today in Energy page (https://www.eia.gov/todayinenergy/), where each
 *        entry looks like <span class="date">Nov 5, 2026</span> ... <h1><a href="detail.php?id=68424">Title</a></h1>.
 * @return The number of articles found.
 */
int oil_Tie_Articles(Oil_Byte_Source_t& in, Oil_Article_Fn onArticle, void* context);

#endif
