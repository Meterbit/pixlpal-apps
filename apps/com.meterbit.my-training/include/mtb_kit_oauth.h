/**
 * @file mtb_kit_oauth.h
 * @brief An account linked from the phone: the phone signs the user in (OAuth 2.0, in its browser) and hands Pixlpal a
 *        refresh token over BLE; Pixlpal keeps it in NVS and trades it for short-lived access tokens when it needs one.
 *
 * No client secret is ever on the device. Microsoft (public client, a scope on each refresh, refresh tokens that
 * rotate) and Spotify (PKCE public client, rotating) refresh with the provider directly. Google, Oura, WHOOP and Withings
 * need a client secret, so they refresh through the Pixlpal account server (CONFIG_PXP_OAUTH_BROKER_URL), which adds it:
 * POST <broker>/v1/oauth/<provider>/refresh, answered like the provider's own token endpoint (docs/OAUTH_BROKER.md).
 * A rotated refresh token is saved at once. A refresh token
 * the provider refuses (revoked, expired, password changed) puts the account in RELINK; network and server failures
 * are retried with a growing pause. Tokens are never logged.
 *
 * Threads: one owner task calls Begin/Access/Reject/End. Store/Forget may run on another task (the BLE task): they write
 * NVS and bump a counter the owner sees on its next Access, which then reloads.
 */
#ifndef MTB_KIT_OAUTH_H
#define MTB_KIT_OAUTH_H

#include <stdint.h>
#include <stddef.h>

#define MTB_KIT_OAUTH_REFRESH_MAX   2560    /**< Microsoft's refresh tokens run to about 2 KB. */
#define MTB_KIT_OAUTH_ACCESS_MAX    4096    /**< Microsoft's access tokens (JWTs) run to about 3 KB. */

enum Mtb_Kit_OAuth_State_t : uint8_t {
    MTB_OAUTH_UNLINKED = 0,     /**< No account: link one from the phone. */
    MTB_OAUTH_OK,               /**< An access token is at hand. */
    MTB_OAUTH_RETRY,            /**< The last refresh failed for a passing reason; tried again later. */
    MTB_OAUTH_RELINK,           /**< The provider refused the link: link the account again from the phone. */
};

struct Mtb_Kit_OAuth_Provider_t {
    const char* tokenUrl;       /**< "https://oauth2.googleapis.com/token" */
    const char* clientId;       /**< Public client ID; unused (may be "") for a broker provider. */
    const char* broker;         /**< The account server's name for the provider ("google", "oura", "whoop", "withings")
                                     when it needs a client secret; nullptr for a public client (Microsoft, Spotify). */
    const char* scope;          /**< Sent with each refresh (Microsoft), or nullptr. */
    const char* nvsKey;         /**< Up to 15 characters. */
    const char* userAgent;
    const char* extraField;     /**< Added to each refresh as is ("action=requesttoken" for Withings), or nullptr. */
};

struct Mtb_Kit_OAuth_t {
    const Mtb_Kit_OAuth_Provider_t* provider;
    char* refresh;              /**< PSRAM, MTB_KIT_OAUTH_REFRESH_MAX */
    char* access;               /**< PSRAM, MTB_KIT_OAUTH_ACCESS_MAX */
    uint32_t expiresAt;         /**< Monotonic seconds. */
    uint32_t retryAt;
    uint8_t failures;
    uint8_t state;              /**< Mtb_Kit_OAuth_State_t */
    uint32_t generation;        /**< Of the stored token loaded. */
    char error[80];             /**< Why the last refresh failed, for the phone ("invalid_grant", "HTTP 503"). */
};

/** @brief The account server's base URL (CONFIG_PXP_OAUTH_BROKER_URL), or "" when there is none. */
const char* mtb_Kit_OAuth_Broker_Url(void);
/** @brief The provider can refresh: a public client with an ID, or a broker provider and an account server. */
bool mtb_Kit_OAuth_Configured(const Mtb_Kit_OAuth_Provider_t& provider);

/** @brief Seconds since boot (the clock the token expiries use). */
uint32_t mtb_Kit_OAuth_Now(void);

/** @brief Loads the stored refresh token. False if memory ran out. */
bool mtb_Kit_OAuth_Begin(Mtb_Kit_OAuth_t& oauth, const Mtb_Kit_OAuth_Provider_t& provider);
/** @brief Wipes and frees the tokens held in memory (the stored one stays). */
void mtb_Kit_OAuth_End(Mtb_Kit_OAuth_t& oauth);

/** @brief Saves a refresh token from the phone (any task). False if it is empty, too long or not writable. */
bool mtb_Kit_OAuth_Store(const char* nvsKey, const char* refreshToken);
/** @brief Forgets the stored refresh token (any task): the account is unlinked. */
void mtb_Kit_OAuth_Forget(const char* nvsKey);
/** @brief A refresh token is stored (any task). */
bool mtb_Kit_OAuth_Is_Stored(const char* nvsKey);

/**
 * @brief A valid access token, refreshed when it has under a minute left; nullptr when there is none (see state):
 *        unlinked, waiting to retry, or needing a new link. Blocks during a refresh (one HTTPS request).
 */
const char* mtb_Kit_OAuth_Access(Mtb_Kit_OAuth_t& oauth);
/** @brief The API answered 401 with the current access token: the next Access refreshes. */
void mtb_Kit_OAuth_Reject(Mtb_Kit_OAuth_t& oauth);
/** @brief "Bearer <token>" for an Authorization header, into @p dest (MTB_KIT_OAUTH_ACCESS_MAX + 8). */
bool mtb_Kit_OAuth_Bearer(Mtb_Kit_OAuth_t& oauth, char* dest, size_t size);

/** @brief What a token endpoint answered to a refresh (host-testable). */
struct Mtb_Kit_OAuth_Answer_t {
    bool ok;                    /**< An access token was given (copied to @p access). */
    bool refused;               /**< The refresh token is no good (link again). */
    bool rotated;               /**< A new refresh token was given (copied to @p rotated). */
    uint32_t expiresIn;
    char error[64];
};
/**
 * @brief Reads a token endpoint's JSON answer (status @p code) to a refresh. The tokens are copied to @p access and
 *        @p rotated; one that does not fit counts as not given.
 */
bool mtb_Kit_OAuth_Parse_Answer(int code, const char* body, size_t length, char* access, size_t accessSize,
                                char* rotated, size_t rotatedSize, Mtb_Kit_OAuth_Answer_t& answer);

#endif
