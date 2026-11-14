/**
 * @file oil_Model.h
 * @brief Data model of the Oil price app, shared by the data providers, the data service and the screen.
 *
 * Nothing here knows about the network or the panel, so a provider can be replaced without touching the
 * display code, and the helpers can be tested on a PC.
 */
#ifndef OIL_MODEL_H
#define OIL_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define OIL_MAX_SERIES      12
#define OIL_HISTORY_POINTS  140     /**< About six months of trading days. */
#define OIL_MAX_HEADLINES   6
#define OIL_HEADLINE_LEN    140
#define OIL_CODE_LEN        10
#define OIL_NAME_LEN        36
#define OIL_SOURCE_ID_LEN   40
#define OIL_SOURCE_LEN      40

#define OIL_DATA_MAGIC      0x4F494C31UL    // "OIL1"
#define OIL_DATA_LAYOUT     1               // Raise when Oil_Data_t changes, so an old cache file is ignored.

enum Oil_Unit_t : uint8_t {
    OIL_UNIT_USD_BBL = 0,
    OIL_UNIT_USD_GAL,
    OIL_UNIT_USD_MMBTU,
    OIL_UNIT_KBD,           /**< Thousand barrels per day. */
};

enum Oil_Cadence_t : uint8_t {
    OIL_DAILY = 0,
    OIL_MONTHLY,
};

enum Oil_Kind_t : uint8_t {
    OIL_KIND_PRICE = 0,     /**< A daily benchmark: on the board, the strip and the chart. */
    OIL_KIND_REGIONAL,      /**< A price published less often, such as a monthly crude stream. */
    OIL_KIND_SUPPLY,        /**< A production volume. */
};

struct Oil_Point_t {
    int32_t day;            /**< Days since 1970-01-01. A monthly value sits on the first day of its month. */
    float value;
};

/** @brief One time series. Points are oldest first; the newest is the current value. */
struct Oil_Series_t {
    char code[OIL_CODE_LEN];            /**< Short label for the screen, e.g. "BRENT". */
    char name[OIL_NAME_LEN];            /**< What the number is, e.g. "Brent spot FOB, Europe". */
    char sourceId[OIL_SOURCE_ID_LEN];   /**< The provider's own id for it. Empty when the provider has not found it. */
    uint8_t unit;                       /**< Oil_Unit_t */
    uint8_t cadence;                    /**< Oil_Cadence_t */
    uint8_t kind;                       /**< Oil_Kind_t */
    uint8_t unavailable;                /**< Non-zero when the provider looked and does not publish it. */
    uint16_t count;
    uint32_t fetched;                   /**< Unix time this series was last retrieved. */
    Oil_Point_t points[OIL_HISTORY_POINTS];
};

struct Oil_Headline_t {
    char title[OIL_HEADLINE_LEN];
    int32_t day;                        /**< Publication date, days since 1970-01-01; 0 if unknown. */
    uint32_t ref;                       /**< The provider's article number, for building a link. */
    uint8_t score;                      /**< Relevance to oil and gas, see oil_Headline_Score(). */
};

/** @brief Everything the screen shows. Stored in flash, so the app has something to show when it is offline. */
struct Oil_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    char provider[16];                  /**< Id of the provider that filled it. */
    char sourceName[OIL_SOURCE_LEN];    /**< Short attribution, always on screen: "U.S. EIA". */
    char sourceFull[OIL_SOURCE_LEN * 2];/**< Long attribution for the ticker. */
    char newsSource[OIL_SOURCE_LEN];
    uint8_t seriesCount;
    uint8_t headlineCount;
    uint32_t supplyFetched;
    uint32_t newsFetched;
    Oil_Series_t series[OIL_MAX_SERIES];
    Oil_Headline_t headlines[OIL_MAX_HEADLINES];
};

//##############################################################################################################
/** @brief Days since 1970-01-01 of a calendar date (proleptic Gregorian). */
int32_t oil_Days_From_Civil(int year, int month, int day);
void oil_Civil_From_Days(int32_t days, int& year, int& month, int& day);
/** @brief "2026-11-03" or "2026-06" (first of the month). Returns 0 if the text is not a date. */
int32_t oil_Parse_Iso_Date(const char* text);
/** @brief "Nov 5, 2026". Returns 0 if the text is not a date. */
int32_t oil_Parse_Us_Date(const char* text);
/** @brief "2026-11-03", or "2026-11" when @p monthOnly. */
void oil_Format_Iso_Date(char* dest, size_t size, int32_t day, bool monthOnly);
/** @brief "03 Nov", or "Jun 26" for a monthly value. */
void oil_Format_Short_Date(char* dest, size_t size, int32_t day, bool monthly);

/** @brief Adds or replaces the value of a day, keeping the points in order. The oldest point goes when full. */
void oil_Series_Put(Oil_Series_t& series, int32_t day, float value);
const Oil_Point_t* oil_Series_Last(const Oil_Series_t& series);
/** @brief The newest point on or before @p day, or nullptr. */
const Oil_Point_t* oil_Series_At_Or_Before(const Oil_Series_t& series, int32_t day);
/**
 * @brief Change from an earlier value to the newest one.
 * @param daysBack  0 compares with the previous published value (the previous trading day or month);
 *                  otherwise with the value on or before that many days earlier.
 * @return false if the series does not reach back that far.
 */
bool oil_Series_Change(const Oil_Series_t& series, int32_t daysBack, float& change, float& percent);
/** @brief Lowest and highest value of the last @p days days. Returns false if there are none. */
bool oil_Series_Range(const Oil_Series_t& series, int32_t days, float& low, float& high);

/** @brief Index of the series with this code, or -1. */
int oil_Find_Series(const Oil_Data_t& data, const char* code);

/**
 * @brief Relevance of a headline to an oil and gas display: 0 none, 1 energy in general,
 *        2 oil, gas or fuels, 3 OPEC, Nigeria or Africa.
 */
uint8_t oil_Headline_Score(const char* title);

/**
 * @brief Copies text to plain printable ASCII, the only characters the panel fonts have. Decodes HTML
 *        entities, replaces typographic punctuation with its ASCII look-alike, drops other non-ASCII bytes and
 *        tags, and collapses runs of white space.
 */
void oil_Clean_Text(char* dest, size_t size, const char* src);

#endif
