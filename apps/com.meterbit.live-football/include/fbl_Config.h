/**
 * @file fbl_Config.h
 * @brief Build-time settings of the Live Football app. Leagues, favourite team, table league, goal alerts, the
 *        fastest live refresh, the pages and the API key can also be changed from the Pixlpal mobile app.
 */
#ifndef FBL_CONFIG_H
#define FBL_CONFIG_H

#define FBL_HOST                        "https://v3.football.api-sports.io"
#define FBL_CREST_URL                   "https://media.api-sports.io/football/teams/%u.png"
#define FBL_USER_AGENT                  "Pixlpal-LiveFootball/2.0 (ESP32 LED display)"

// Live updates are paced so the day's quota lasts through the day's matches (fbl_Plan_Live_Interval()).
#define FBL_LIVE_MIN_DEFAULT            2       // Fastest live refresh, minutes.
#define FBL_LIVE_MIN_LOWEST             1
#define FBL_LIVE_MIN_HIGHEST            15
#define FBL_LIVE_SLOWEST_SEC            (20UL * 60)
#define FBL_RESERVE_REQUESTS            8       // Kept back each day for the day lists and "refresh now".
#define FBL_FREE_LIMIT                  100     // Requests a day on the Free plan, assumed until /status says.
#define FBL_MATCH_LENGTH_SEC            (135UL * 60)    // Kick-off to final whistle, with a margin.

// Day lists: yesterday, today and tomorrow (UTC), all matches, of which the app keeps your leagues' and team's.
#define FBL_TODAY_REFRESH_SEC           (6UL * 3600)
#define FBL_FINAL_DELAY_SEC             (20UL * 60)     // After a match leaves the live feed, today's list is read
                                                        // again for its final result.
#define FBL_STATUS_REFRESH_SEC          (30UL * 60)     // /status does not count against the quota.

// Paid plans only (the Free plan refuses the current season).
#define FBL_TABLE_REFRESH_SEC           (6UL * 3600)
#define FBL_SCORERS_REFRESH_SEC         (24UL * 3600)
#define FBL_UPCOMING_REFRESH_SEC        (12UL * 3600)
#define FBL_LOCKED_RETRY_SEC            (7UL * 86400)

#define FBL_BACKOFF_FIRST_SEC           120
#define FBL_BACKOFF_MAX_SEC             (3UL * 3600)
#define FBL_MANUAL_REFRESH_GAP_SEC      120

#define FBL_PAGE_SEC_DEFAULT            10
#define FBL_PAGE_SEC_LOWEST             4
#define FBL_PAGE_SEC_HIGHEST            60
#define FBL_GOAL_SHOW_MS                9000
#define FBL_BANNER_SHOW_MS              6000

#define FBL_WORKER_STACK                12288
#define FBL_WORKER_MIN_INTERNAL_HEAP    (48 * 1024)
#define FBL_CONNECT_TIMEOUT_MS          8000
#define FBL_READ_TIMEOUT_MS             15000
#define FBL_MAX_RESPONSE_BYTES          (6UL * 1024 * 1024)     // A busy Saturday's day list is about 3 MB.

#define FBL_CACHE_FILE                  "/football/cache.bin"
#define FBL_CREST_DIR                   "/football/crests"
#define FBL_NVS_KEY                     "footballCfg"

#endif
