/**
 * @file opa_Config.h
 * @brief Build-time settings of the Oil Price API app. The watchlist, the chart range, the share of the plan's
 *        quota, the fastest refresh, the pages and the API key can also be changed from the Pixlpal mobile app;
 *        these are the defaults and limits.
 */
#ifndef OPA_CONFIG_H
#define OPA_CONFIG_H

// Sent with every request so OilPriceAPI can see who is calling.
#define OPA_USER_AGENT                  "Pixlpal-OilPriceAPI/1.0 (ESP32 LED display)"
#define OPA_HOST                        "https://api.oilpriceapi.com"

// Without a key the app shows the keyless demo feed (25 requests a day per IP address, 37 codes), so the user
// sees prices before signing up. It is asked at most this often. Set to 0 to show nothing until a key is set.
#define OPA_DEMO_ENABLED                1
#define OPA_DEMO_INTERVAL_SEC           (90UL * 60)

// Quote pacing. The app spreads what is left of the plan's quota evenly until the quota resets (see
// opa_Plan_Quote_Interval() in opa_Model.cpp), never faster than the user's "fastest refresh" and never slower
// than OPA_SLOWEST_SEC. Until the first answer tells it the plan, it assumes the Free plan.
#define OPA_FLOOR_MIN_DEFAULT           5
#define OPA_FLOOR_MIN_LOWEST            2       // Brent moves about every 2.5 minutes on OilPriceAPI.
#define OPA_FLOOR_MIN_HIGHEST           60
#define OPA_SHARE_DEFAULT               90      // Per cent of the quota this Pixlpal may use. The rest is left for
#define OPA_SHARE_LOWEST                10      // the user's other uses of the same key.
#define OPA_SHARE_HIGHEST               100
#define OPA_RESERVE_REQUESTS            2       // Kept back in every quota window for "refresh now".
#define OPA_SLOWEST_SEC                 (6UL * 3600)
#define OPA_FREE_LIMIT                  50      // Requests a day on the Free plan, assumed until the server says.

// Extras. Each costs one request when due, and is only asked for while the quota is a paid or trial one.
#define OPA_HISTORY_MIN_GAP_SEC         600     // A changed chart cannot be fetched more often than this.
#define OPA_FUTURES_REFRESH_SEC         (6UL * 3600)
#define OPA_RIGS_REFRESH_SEC            (24UL * 3600)   // Weekly data (Fridays).
#define OPA_OPEC_REFRESH_SEC            (24UL * 3600)   // Monthly data.
#define OPA_LOCKED_RETRY_SEC            (7UL * 86400)   // A feature the plan refused is tried again after this,
                                                        // or as soon as the key or the plan changes.
#define OPA_MANUAL_REFRESH_GAP_SEC      300

// After a failure: 5 min, 10, 20, ... up to 6 h. A Retry-After or quota reset from the server is honoured up to 2 days.
#define OPA_BACKOFF_FIRST_SEC           300
#define OPA_BACKOFF_MAX_SEC             (6UL * 3600)
#define OPA_WAIT_MAX_SEC                (2UL * 86400)
#define OPA_JITTER_SEC                  120     // Random delay added to every request, so devices never call at once.

#define OPA_PAGE_SEC_DEFAULT            10
#define OPA_PAGE_SEC_LOWEST             4
#define OPA_PAGE_SEC_HIGHEST            60

// The fetch runs in a short-lived task of its own, so the screen never waits on the network. TLS needs about
// 40 KB of internal RAM while a connection is open; the fetch is postponed if less is free.
#define OPA_WORKER_STACK                12288
#define OPA_WORKER_MIN_INTERNAL_HEAP    (48 * 1024)
#define OPA_CONNECT_TIMEOUT_MS          8000
#define OPA_READ_TIMEOUT_MS             10000
#define OPA_MAX_RESPONSE_BYTES          (256UL * 1024)

#define OPA_CACHE_FILE                  "/oilPriceApi/cache.bin"
#define OPA_NVS_KEY                     "oilPriceApiCfg"

#endif
