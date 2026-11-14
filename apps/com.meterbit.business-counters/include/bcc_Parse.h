/**
 * @file bcc_Parse.h
 * @brief Reading each provider's answers into the counters. No Arduino, so it can be tested on a PC against saved
 *        answers. Each returns a Bcc_State_t (BCC_OK when the answer was read) and, if not OK, a reason in @p why.
 *
 *  Stripe     GET  https://api.stripe.com/v1/charges?created[gte]=<midnight>&limit=100[&starting_after=<id>]
 *  Paystack   GET  https://api.paystack.co/transaction?from=<ISO midnight>&perPage=100&page=<n>
 *  Shopify    POST https://<store>.myshopify.com/admin/api/2026-10/graphql.json (orders created today, the shop's plan)
 *             POST https://<store>.myshopify.com/admin/oauth/access_token (client credentials, a 24-hour token)
 *  YouTube    GET  https://www.googleapis.com/youtube/v3/channels?part=snippet,statistics&forHandle=@x|id=UC..&key=
 *  GitHub     GET  https://api.github.com/repos/<owner>/<repo>, /users/<name>
 *  Plausible  GET  https://<host>/api/v1/stats/realtime/visitors?site_id=   (a plain number)
 *             POST https://<host>/api/v2/query {"site_id", "metrics": ["visitors", "pageviews"], "date_range": "day"}
 */
#ifndef BCC_PARSE_H
#define BCC_PARSE_H

#include "bcc_Model.h"

/** @brief Where the next page starts, if there is one. */
struct Bcc_Page_t {
    bool more;
    char cursor[80];                    /**< Stripe: the last charge's id. Shopify: endCursor. Paystack: unused. */
    uint16_t items;                     /**< Items on this page. */
};

/** @brief A page of today's payments, added to @p sales (count, revenue, the newest). */
uint8_t bcc_Parse_Stripe(const char* body, size_t length, int httpCode, Bcc_Sales_t& sales, Bcc_Page_t& page, char* why, size_t whySize);
uint8_t bcc_Parse_Paystack(const char* body, size_t length, int httpCode, int pageNumber, Bcc_Sales_t& sales, Bcc_Page_t& page,
                           char* why, size_t whySize);
/** @brief A page of today's orders; the first page also brings the shop's plan (into sales.plan, sales.test). */
uint8_t bcc_Parse_Shopify(const char* body, size_t length, int httpCode, Bcc_Sales_t& sales, Bcc_Page_t& page, char* why, size_t whySize);
/** @brief The client-credentials answer: the token and how long it lasts. */
uint8_t bcc_Parse_Shopify_Token(const char* body, size_t length, int httpCode, char* token, size_t tokenSize, uint32_t& expiresIn,
                                char* why, size_t whySize);
/** @brief The GraphQL request body for today's orders after @p cursor ("" for the first page). */
size_t bcc_Shopify_Query(char* dest, size_t size, const char* sinceIso, const char* cursor);

uint8_t bcc_Parse_Youtube(const char* body, size_t length, int httpCode, Bcc_Creator_t& c, char* why, size_t whySize);
/** @brief A repo (into slot @p repo) or, with @p repo -1, a user's followers. */
uint8_t bcc_Parse_Github(const char* body, size_t length, int httpCode, int repo, Bcc_Creator_t& c, char* why, size_t whySize);
uint8_t bcc_Parse_Plausible_Live(const char* body, size_t length, int httpCode, Bcc_Creator_t& c, char* why, size_t whySize);
uint8_t bcc_Parse_Plausible_Today(const char* body, size_t length, int httpCode, Bcc_Creator_t& c, char* why, size_t whySize);

/** @brief "2026-11-08T10:15:00Z", with fractions or an offset ("+01:00"), as Unix seconds; 0 if not a time. */
uint32_t bcc_Parse_Iso(const char* text);
/** @brief Unix seconds as "2026-11-08T23:00:00Z". */
void bcc_Iso(char* dest, size_t size, uint32_t unix);

#endif
