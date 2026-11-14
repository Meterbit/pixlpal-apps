/**
 * @file omt_Ui.h
 * @brief Open-Meteo's own page beside the shared weather pages: the plan, today's calls against the free limit, and
 *        the pace.
 */
#ifndef OMT_UI_H
#define OMT_UI_H

#include "omt_Model.h"

void omt_Draw_Plan(const Omt_Settings_t& settings, const Mtb_Kit_Wx_Status_t& status);

#endif
