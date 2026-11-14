/**
 * @file bcc_Model.h
 * @brief Business & Creator Counters {16,0}: the settings, today's sales from each shop, the creator counts, money
 *        and number formatting, and milestones. No Arduino, so it can be tested on a PC.
 */
#ifndef BCC_MODEL_H
#define BCC_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define BCC_NVS_KEY         "bccSet"
#define BCC_LOG_KEY         "bccLog"
#define BCC_SETTINGS_MAGIC  0xBCC16001u

// The sources, in the settings' "on" order.
enum Bcc_Source_t : uint8_t { BCC_STRIPE = 0, BCC_PAYSTACK, BCC_SHOPIFY, BCC_YOUTUBE, BCC_GITHUB, BCC_PLAUSIBLE, BCC_SOURCES };
#define BCC_SHOPS           3           /**< The first three sources sell. */
#define BCC_REPOS           3
#define BCC_CURRENCIES      4           /**< Currencies kept per shop and in the day's total. */

// What a source is doing, for the screen and the phone.
enum Bcc_State_t : uint8_t {
    BCC_OFF = 0,        /**< Not switched on, or not filled in. */
    BCC_WAIT,           /**< First answer on its way. */
    BCC_OK,
    BCC_KEY,            /**< The key, token or store was refused. */
    BCC_LIMIT,          /**< Rate limit or quota: waiting. */
    BCC_PLAN,           /**< The provider's plan does not include this (Plausible's Stats API). */
    BCC_ERROR,          /**< Network or an unexpected answer: tried again later. */
};

enum Bcc_Chime_t : uint8_t { BCC_CHIME_OFF = 0, BCC_CHIME_SALES, BCC_CHIME_ALL };

struct Bcc_Settings_t {
    uint32_t magic;
    uint8_t on[BCC_SOURCES];
    // Shops.
    char stripeKey[128];                /**< sk_live_/rk_live_ (a restricted key with Charges: Read is best). Secret. */
    char paystackKey[80];               /**< sk_live_... Secret. */
    char shopStore[64];                 /**< "my-shop" or "my-shop.myshopify.com". */
    char shopClientId[48];              /**< A Dev Dashboard app's client ID (with shopSecret), or empty. */
    char shopSecret[80];                /**< Its client secret, or a legacy custom app's shpat_ token. Secret. */
    // Creators.
    char ytKey[48];                     /**< A YouTube Data API v3 key. Secret. */
    char ytChannel[64];                 /**< "@handle" or "UC..." */
    char ghToken[100];                  /**< Optional GitHub token (5,000 requests an hour instead of 60). Secret. */
    char ghRepos[BCC_REPOS][64];        /**< "owner/repo" */
    char ghUser[40];                    /**< Followers of this user or organisation, or empty. */
    char plKey[80];                     /**< A Plausible Stats API key. Secret. */
    char plSite[64];                    /**< The site's domain in Plausible: "example.com". */
    char plHost[64];                    /**< "plausible.io", or a self-hosted server. */
    // The day.
    uint32_t goal;                      /**< Today's goal in goalCurrency's main unit; 0: none. */
    char goalCurrency[4];               /**< "USD"; empty: the currency most sales are in. */
    uint8_t chime;                      /**< Bcc_Chime_t */
    uint8_t announce;                   /**< Say "New sale, 49 dollars 50". */
    uint8_t showAmounts;                /**< 0: counts only (customers can see the screen). */
    uint8_t quietOn;
    uint16_t quietFrom, quietTo;        /**< Minutes after midnight: no sound in between. */
    uint8_t cycle;                      /**< Seconds per page when both pages are in use; 0: stay. */
    uint8_t volume;                     /**< 1-10 */
};

void bcc_Settings_Default(Bcc_Settings_t& s);
void bcc_Settings_Clamp(Bcc_Settings_t& s);
/** @brief The source is on and has what it needs to ask. */
bool bcc_Source_Ready(const Bcc_Settings_t& s, uint8_t source);
const char* bcc_Source_Name(uint8_t source);
/** @brief "my-shop" or "https://my-shop.myshopify.com/" into "my-shop.myshopify.com"; false if not a store name. */
bool bcc_Shop_Domain(const char* text, char* dest, size_t size);
/** @brief "owner/repo" (also from a github.com address); false if not one. */
bool bcc_Repo_Name(const char* text, char* dest, size_t size);
/** @brief "a/b, c/d" into the settings' repos (at most three); false if any is not a repo. */
bool bcc_Repos_Parse(const char* text, char repos[BCC_REPOS][64]);

// Money ---------------------------------------------------------------------------------------------------------
struct Bcc_Money_t {
    char code[4];                       /**< "USD" */
    int64_t minor;                      /**< In the currency's smallest unit (cents, kobo). */
};

/** @brief Digits after the point for @p code: 0 for JPY, KRW, UGX..., 3 for BHD, KWD..., else 2. */
uint8_t bcc_Decimals(const char* code);
/** @brief Adds @p minor of @p code to a list of at most BCC_CURRENCIES; false if the list is full. */
bool bcc_Money_Add(Bcc_Money_t* list, uint8_t& count, const char* code, int64_t minor);
/** @brief "49.5" in @p code's units to minor units (490 for 4.9 in a 2-decimal currency); false if not a number. */
bool bcc_Parse_Amount(const char* text, const char* code, int64_t& minor);
/**
 * @brief "$1,234.50", "NGN 120,000", "EUR 12.30M" (compact: more than 99,999 shown as K or M).
 *        Cents are left out from 10,000 up, and in compact form.
 */
void bcc_Format_Money(char* dest, size_t size, const char* code, int64_t minor, bool compact);
/** @brief "1,234,567", or "1.23M" / "123K" when @p compact and 10,000 or more. */
void bcc_Format_Count(char* dest, size_t size, int64_t value, bool compact);
/** @brief Words for speech: "49 dollars 50", "120000 naira". */
void bcc_Speak_Money(char* dest, size_t size, const char* code, int64_t minor);

// A shop's day ---------------------------------------------------------------------------------------------------
struct Bcc_Sales_t {
    uint8_t state;                      /**< Bcc_State_t */
    uint8_t test;                       /**< A test key or a development store. */
    uint8_t more;                       /**< More sales today than were counted (page limit). */
    uint16_t count;                     /**< Paid sales today, not refunded in full. */
    Bcc_Money_t money[BCC_CURRENCIES];  /**< Revenue by currency, refunds taken off. */
    uint8_t moneyCount;
    Bcc_Money_t last;                   /**< The newest sale. */
    char lastLabel[24];                 /**< "#1042" (Shopify's order name), or "". */
    uint32_t lastAt;                    /**< Unix */
    char plan[32];                      /**< "Shopify Basic", "Live mode" */
    char text[64];                      /**< Why it stopped, for the phone. */
    uint32_t day;                       /**< The local day it counts (days since 1970). */
    uint32_t updated;                   /**< Unix of the last answer. */
};

// Creators -------------------------------------------------------------------------------------------------------
struct Bcc_Creator_t {
    uint8_t state[BCC_SOURCES];         /**< For BCC_YOUTUBE, BCC_GITHUB, BCC_PLAUSIBLE. */
    char text[BCC_SOURCES][64];
    uint32_t updated[BCC_SOURCES];
    // YouTube.
    int64_t subs, views;
    uint32_t videos;
    uint8_t subsHidden;
    char channel[40];
    // GitHub.
    uint32_t stars, forks, followers;   /**< Stars and forks: all the repos. */
    uint32_t repoStars[BCC_REPOS];
    char repoName[BCC_REPOS][40];
    uint8_t repoCount;
    uint16_t ghLimit;                   /**< Requests an hour GitHub allows (60 without a token). */
    // Plausible.
    int32_t live;                       /**< Visitors in the last 5 minutes; -1 not known. */
    int32_t visitors, pageviews;        /**< Today's; -1 not known. */
};

/** @brief The day's totals over the shops: revenue by currency, and the count. */
struct Bcc_Total_t {
    uint32_t count;
    Bcc_Money_t money[BCC_CURRENCIES];
    uint8_t moneyCount;
    int8_t main;                        /**< The currency shown large (the goal's, else the one with most); -1 none. */
    float goalFraction;                 /**< Of the goal in the main currency; -1 no goal. */
};
void bcc_Total(const Bcc_Settings_t& s, const Bcc_Sales_t* shops, uint32_t today, Bcc_Total_t& total);

// Milestones -----------------------------------------------------------------------------------------------------
/** @brief The milestone (10, 25, 50, 100, 250, 500, 1,000 ... 1, 2.5, 5 times a power of ten) passed going from @p before
 *         to @p after, the largest if several; 0 if none. */
int64_t bcc_Milestone(int64_t before, int64_t after);

/** @brief What is kept across restarts: the best day, and the counts already celebrated. */
struct Bcc_Log_t {
    uint32_t magic;
    Bcc_Money_t best;                   /**< The best day's revenue in the goal (main) currency. */
    uint32_t bestDay;
    int64_t seenSubs;                   /**< Milestones are celebrated only past these. */
    uint32_t seenStars, seenFollowers;
};
void bcc_Log_Clear(Bcc_Log_t& log);

/** @brief Minutes after midnight @p minute inside [from, to) (across midnight when to < from). */
bool bcc_In_Window(uint16_t minute, uint16_t from, uint16_t to);

#endif
