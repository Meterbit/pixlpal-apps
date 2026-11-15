/**
 * @file rss_Provider.h
 * @brief RSS Reader's provider for the shared news engine: the feeds' addresses (kept here, as the engine's feed
 *        entries only hold names), and the reading of each answer with rss_Parse().
 */
#ifndef RSS_PROVIDER_H
#define RSS_PROVIDER_H

#include "rss_Model.h"

const Mtb_Kit_News_Provider_t& rss_Provider(void);
/** @brief The addresses the provider reads; call before handing the engine the matching config. */
void rss_Provider_Set_Feeds(const Rss_Settings_t& settings);

#endif
