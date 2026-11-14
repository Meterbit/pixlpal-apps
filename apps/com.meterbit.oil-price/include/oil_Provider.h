/**
 * @file oil_Provider.h
 * @brief The interface every data source of the Oil price app implements.
 *
 * A provider knows one source's URLs, formats and series. It fills an Oil_Data_t and nothing else: scheduling,
 * caching, back-off and threading belong to the data service (oil_Service.h), and drawing to oil_Ui.h. To use
 * another licensed source, implement this class and pass it to oil_Service_Begin().
 */
#ifndef OIL_PROVIDER_H
#define OIL_PROVIDER_H

#include "oil_Model.h"

enum Oil_Feed_t : uint8_t {
    OIL_FEED_PRICES = 0,    /**< Daily benchmark prices. */
    OIL_FEED_SUPPLY,        /**< Monthly series: production and regional crude streams. */
    OIL_FEED_NEWS,          /**< Headlines. */
    OIL_FEED_COUNT
};

enum Oil_Result_t : uint8_t {
    OIL_OK = 0,
    OIL_ERR_NO_KEY,         /**< The feed needs an API key and none is set. */
    OIL_ERR_NETWORK,        /**< No answer: DNS, TLS, connection or time-out. */
    OIL_ERR_AUTH,           /**< The key was rejected. */
    OIL_ERR_RATE_LIMIT,     /**< The source asked us to slow down. */
    OIL_ERR_SERVER,         /**< Any other HTTP error. */
    OIL_ERR_PARSE,          /**< The answer was not in the expected format. */
    OIL_ERR_NOT_SUPPORTED,  /**< The provider has no such feed. */
};

struct Oil_Fetch_Result_t {
    Oil_Result_t result;
    int httpCode;
    uint32_t retryAfterSec;     /**< From a Retry-After header, or 0. */
};

class Oil_Provider_t {
public:
    virtual ~Oil_Provider_t() {}

    /** @brief Short id stored with the cache. A cache written by another provider is not used. */
    virtual const char* id(void) const = 0;

    /** @brief Fills in the series list and the attribution text of an empty Oil_Data_t. No network. */
    virtual void describe(Oil_Data_t& data) const = 0;

    /** @brief true if @p feed cannot be fetched without an API key. */
    virtual bool needs_Key(Oil_Feed_t feed) const = 0;

    /**
     * @brief Downloads one feed into @p data, which holds everything known so far. Runs in the data service's
     *        worker task. On failure @p data may be half updated; the service then throws it away.
     * @param apiKey   The user's key for this provider, possibly "".
     * @param nowUnix  The current time, already known to be set.
     */
    virtual Oil_Fetch_Result_t fetch(Oil_Feed_t feed, Oil_Data_t& data, const char* apiKey, uint32_t nowUnix) = 0;
};

/** @brief U.S. Energy Information Administration: open data API v2 and the Today in Energy page. */
Oil_Provider_t& oil_Provider_Eia(void);

#endif
