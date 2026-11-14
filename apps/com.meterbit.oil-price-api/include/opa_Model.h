/**
 * @file opa_Model.h
 * @brief Data model of the Oil Price API app, shared by the API client, the data service and the screen.
 *
 * Nothing here knows about the network or the panel, so it can be tested on a PC.
 */
#ifndef OPA_MODEL_H
#define OPA_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define OPA_MAX_WATCH       12      /**< Codes on the watchlist. All go in one request (the API takes up to 20). */
#define OPA_CODE_LEN        28      /**< Longest catalogue code is PROPANE_MONT_BELVIEU_USD. */
#define OPA_LOCAL_POINTS    96      /**< Prices the device keeps itself, for the chart on plans without history. */
#define OPA_LOCAL_GAP_SEC   (15UL * 60)
#define OPA_CHART_POINTS    128
#define OPA_CURVE_MAX       12
#define OPA_BASINS          4
#define OPA_TEXT_LEN        96
#define OPA_KEY_LEN         96

#define OPA_DATA_MAGIC      0x4F504131UL    // "OPA1"
#define OPA_DATA_LAYOUT     1               // Raise when Opa_Data_t changes, so an old cache file is ignored.

struct Opa_Point_t {
    uint32_t t;             /**< Unix time. */
    float v;
};

/** @brief The change windows OilPriceAPI reports in "changes". Which ones come depends on the plan. */
enum Opa_Window_t : uint8_t { OPA_W_24H = 0, OPA_W_7D, OPA_W_30D, OPA_W_90D, OPA_W_COUNT };

enum Opa_Quote_State_t : uint8_t {
    OPA_Q_NONE = 0,         /**< Never received. */
    OPA_Q_CURRENT,
    OPA_Q_STALE,            /**< The API marked it stale: its source has not published for longer than usual. */
    OPA_Q_MISSING,          /**< Asked for and not returned: not on this plan, not in the demo, or withdrawn. */
};

struct Opa_Quote_t {
    char code[OPA_CODE_LEN];
    float price;                        /**< NAN until received. */
    float change[OPA_W_COUNT];          /**< Per cent, as reported; NAN when not reported. */
    uint32_t observed;                  /**< When the source published the price (updated_at). */
    uint32_t fetched;                   /**< When this device received it. */
    char currency[4];
    char unit[10];                      /**< As the API names it: "barrel", "gallon", "mmBtu"... */
    uint8_t state;                      /**< Opa_Quote_State_t */
    uint8_t localCount;
    Opa_Point_t local[OPA_LOCAL_POINTS];    /**< Oldest first, at least OPA_LOCAL_GAP_SEC apart. */
};

/** @brief Chart ranges, and the history endpoint each one uses. */
enum Opa_Range_t : uint8_t { OPA_RANGE_DAY = 0, OPA_RANGE_WEEK, OPA_RANGE_MONTH, OPA_RANGE_YEAR, OPA_RANGE_COUNT };

struct Opa_Chart_t {
    char code[OPA_CODE_LEN];
    uint8_t range;                      /**< Opa_Range_t */
    uint16_t count;
    uint32_t fetched;
    Opa_Point_t points[OPA_CHART_POINTS];   /**< Oldest first. */
};

struct Opa_Curve_t {                    /**< ICE Brent futures, by delivery month. */
    uint32_t fetched;
    uint8_t count;
    char month[OPA_CURVE_MAX][8];       /**< "2027-01" */
    float price[OPA_CURVE_MAX];
};

struct Opa_Rigs_t {                     /**< Baker Hughes US rig count. */
    uint32_t fetched;
    char date[12];
    int16_t total, weekChange, yearChange;
    char basin[OPA_BASINS][12];
    int16_t basinCount[OPA_BASINS], basinChange[OPA_BASINS];
};

struct Opa_Opec_t {                     /**< OPEC crude output from the monthly OPEC report. */
    uint32_t fetched;
    char month[8];
    float total, monthChange;           /**< Million barrels a day. */
    float nigeria, nigeriaQuota;        /**< NAN when not reported. */
    char headline[OPA_TEXT_LEN];
};

enum Opa_Quota_Window_t : uint8_t { OPA_QUOTA_UNKNOWN = 0, OPA_QUOTA_DAY, OPA_QUOTA_MONTH, OPA_QUOTA_TRIAL, OPA_QUOTA_DEMO };

/** @brief The account's quota, as the X-RateLimit-* headers of the last answer gave it. */
struct Opa_Account_t {
    uint32_t seen;                      /**< When the headers were read; 0 never. */
    uint32_t limit;
    uint32_t remaining;
    uint32_t reset;                     /**< Unix time the quota starts again. */
    uint8_t window;                     /**< Opa_Quota_Window_t */
    uint8_t exhausted;                  /**< The server refused for lack of quota until reset. */
    char tier[16];                      /**< X-RateLimit-Tier, e.g. "free", "developer". */
};

/** @brief Paid features, which the app finds out about by asking: a plan without one answers 403. */
enum Opa_Feature_t : uint8_t { OPA_F_HISTORY = 0, OPA_F_FUTURES, OPA_F_RIGS, OPA_F_OPEC, OPA_F_COUNT };
enum Opa_Access_State_t : uint8_t { OPA_ACCESS_UNKNOWN = 0, OPA_ACCESS_OK, OPA_ACCESS_LOCKED };

struct Opa_Access_t {
    uint8_t state;                      /**< Opa_Access_State_t */
    uint32_t retryAt;                   /**< When LOCKED: when to ask again. */
    uint32_t lockedLimit;               /**< The quota limit when it was refused: a new plan asks again at once. */
};

struct Opa_Data_t {
    uint32_t magic;
    uint16_t layout;
    uint16_t size;
    uint8_t quoteCount;
    uint8_t demo;                       /**< The quotes came from the keyless demo feed. */
    uint32_t quotesFetched;
    Opa_Quote_t quotes[OPA_MAX_WATCH];
    Opa_Chart_t chart;
    Opa_Curve_t curve;
    Opa_Rigs_t rigs;
    Opa_Opec_t opec;
    Opa_Account_t account;
    Opa_Access_t access[OPA_F_COUNT];
};

/** @brief A code the app knows: what to call it on the screen. */
struct Opa_Commodity_t {
    const char* code;
    const char* label;      /**< At most 8 characters. */
    const char* name;
    const char* unitText;   /**< Shown until the API gives the unit. */
};

const Opa_Commodity_t* opa_Catalogue(size_t& count);
const Opa_Commodity_t* opa_Find_Commodity(const char* code);
/** @brief The screen label of a code: from the catalogue, or the code itself. */
const char* opa_Label(const char* code);

/** @brief "2026-07-03T13:43:01.099Z", "2026-11-05T00:10:46Z" or "2025-01-10" (UTC). Returns 0 if not a date. */
uint32_t opa_Parse_Iso_Time(const char* text);
int32_t opa_Days_From_Civil(int y, int m, int d);

int opa_Find_Quote(const Opa_Data_t& data, const char* code);

/**
 * @brief Records a received price: the current value, and a local sample if the price has a new time and the last
 *        sample is at least OPA_LOCAL_GAP_SEC older.
 */
void opa_Quote_Put(Opa_Quote_t& quote, float price, uint32_t observed, uint32_t fetched);

/**
 * @brief The change over a window, in per cent: as reported, else worked out from the chart (if it is of this code)
 *        or the local samples. Returns false when there is not enough history.
 */
bool opa_Quote_Change(const Opa_Data_t& data, const Opa_Quote_t& quote, uint8_t window, float& percent);

/** @brief "$/bbl", "EUR/MWh", "GBP/thm", or "" for an exchange rate. */
void opa_Unit_Text(char* dest, size_t size, const Opa_Quote_t& quote);
/** @brief "99.33", "2.760", "1.1219", "4088.7". */
void opa_Format_Price(char* dest, size_t size, float value);
/** @brief "+4.75", "-0.082": a change in price, with as many decimals as @p reference has in opa_Format_Price(). */
void opa_Format_Change(char* dest, size_t size, float change, float reference);
/** @brief "+0.34%", or "--" for NAN. */
void opa_Format_Percent(char* dest, size_t size, float percent);

/**
 * @brief How long to wait between two quote requests so the quota lasts until it resets.
 *
 * What is left of the quota, less the share kept back for the key's other uses and OPA_RESERVE_REQUESTS, is spread
 * evenly over the time to the reset, after setting aside @p extrasPerDay requests a day (history, futures...) but
 * never more than half for them.
 * @param exhausted  Set when nothing is left: the result is then the time to the reset.
 */
uint32_t opa_Plan_Quote_Interval(const Opa_Account_t& account, uint32_t now, uint8_t sharePercent, uint16_t floorMinutes,
                                 uint32_t extrasPerDay, bool& exhausted);

/** @brief Requests this device may still make in the current quota window, after the share kept back. */
uint32_t opa_Plan_Available(const Opa_Account_t& account, uint32_t now, uint8_t sharePercent);

/** @brief The account pays for (or is trying) more than the Free plan: paid features are worth asking for. */
bool opa_Plan_Is_Paid(const Opa_Account_t& account);

#endif
