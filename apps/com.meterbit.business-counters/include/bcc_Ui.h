/**
 * @file bcc_Ui.h
 * @brief Business & Creator Counters' 256x128 screen (from y = 10): today's sales with the goal, the creator
 *        counters, a new sale or a milestone over either, and what to set up when nothing is.
 */
#ifndef BCC_UI_H
#define BCC_UI_H

#include "bcc_App.h"

enum Bcc_Page_t : uint8_t { BCC_PAGE_SALES = 0, BCC_PAGE_CREATOR, BCC_PAGE_SETUP };
enum Bcc_Banner_t : uint8_t { BCC_BANNER_NONE = 0, BCC_BANNER_SALE, BCC_BANNER_MILESTONE, BCC_BANNER_RECORD };

struct Bcc_View_t {
    const Bcc_Settings_t* settings;
    const Bcc_Status_t* status;
    const Bcc_Total_t* total;
    const Bcc_Log_t* log;
    uint8_t page;
    uint32_t now;                       /**< Unix */
    uint32_t today;                     /**< Days since 1970 */
    // The numbers as shown while they count up to the real ones.
    double shownMain;                   /**< Minor units of total->money[total->main] */
    double shownCount;
    double shownSubs, shownStars, shownLive;
    // Over the page.
    uint8_t banner;                     /**< Bcc_Banner_t */
    char bannerTitle[32];
    char bannerBig[32];
    char bannerDetail[48];
    uint32_t frame;                     /**< Counts up while a banner shows (its sparkle) */
    bool muted;
};

void bcc_Draw(const Bcc_View_t& v);
/** @brief Only the banner's moving parts (between full draws). */
void bcc_Draw_Banner_Frame(const Bcc_View_t& v);

#endif
