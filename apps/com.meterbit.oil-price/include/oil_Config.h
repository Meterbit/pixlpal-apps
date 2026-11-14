/**
 * @file oil_Config.h
 * @brief Build-time settings of the Oil price app. The refresh intervals, the page time and the API key can
 *        also be changed at run time from the Pixlpal mobile app (see README.md); these are the defaults and limits.
 */
#ifndef OIL_CONFIG_H
#define OIL_CONFIG_H

// Sent with every request so the data owners can see who is calling. Add a contact URL for the product when it
// has one, e.g. "Pixlpal-OilPrice/1.0 (+https://example.com/pixlpal)".
#define OIL_USER_AGENT                  "Pixlpal-OilPrice/1.0 (ESP32 LED display)"

// How often each feed is refreshed. EIA publishes daily spot prices on business days, usually one to two days in
// arrears, so refreshing more often than every few hours brings nothing new.
#define OIL_PRICE_REFRESH_MIN_DEFAULT   180
#define OIL_PRICE_REFRESH_MIN_LOWEST    60
#define OIL_PRICE_REFRESH_MIN_HIGHEST   1440
#define OIL_NEWS_REFRESH_MIN_DEFAULT    360
#define OIL_NEWS_REFRESH_MIN_LOWEST     120
#define OIL_NEWS_REFRESH_MIN_HIGHEST    2880
#define OIL_SUPPLY_REFRESH_SEC          (7UL * 86400)       // Monthly data.
#define OIL_CATALOGUE_REFRESH_SEC       (30UL * 86400)      // How often a series that was not found is looked for again.

// Each device adds a random delay of up to this much to every refresh, so many devices never call at once.
#define OIL_JITTER_SEC                  900
// After a failure: 5 min, 10, 20, ... up to 6 h. A Retry-After from the server is honoured up to a day.
#define OIL_BACKOFF_FIRST_SEC           300
#define OIL_BACKOFF_MAX_SEC             (6UL * 3600)
#define OIL_RETRY_AFTER_MAX_SEC         (24UL * 3600)
#define OIL_MANUAL_REFRESH_GAP_SEC      600                 // A double click cannot refresh more often than this.

// History kept: the first download reaches this far back; later ones only re-read the last few days, which EIA
// sometimes revises.
#define OIL_COLD_HISTORY_DAYS           200
#define OIL_OVERLAP_DAYS                10
#define OIL_SUPPLY_MONTHS               25

#define OIL_PAGE_SEC_DEFAULT            10
#define OIL_PAGE_SEC_LOWEST             4
#define OIL_PAGE_SEC_HIGHEST            60
#define OIL_DEFAULT_SERIES              "BRENT"

// The fetch runs in a short-lived task of its own, so the screen never waits on the network. TLS needs about
// 40 KB of internal RAM while a connection is open; the fetch is postponed if less is free.
#define OIL_WORKER_STACK                10240
#define OIL_WORKER_MIN_INTERNAL_HEAP    (48 * 1024)
#define OIL_CONNECT_TIMEOUT_MS          8000
#define OIL_READ_TIMEOUT_MS             10000
#define OIL_MAX_RESPONSE_BYTES          (512UL * 1024)

#define OIL_CACHE_FILE                  "/oilPrice/cache.bin"
#define OIL_NVS_KEY                     "oilPriceCfg"

#endif
