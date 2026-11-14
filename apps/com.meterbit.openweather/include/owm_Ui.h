/**
 * @file owm_Ui.h
 * @brief OpenWeather's own page beside the shared weather pages: the plan the key turned out to have, what each
 *        API gives, and today's calls.
 */
#ifndef OWM_UI_H
#define OWM_UI_H

#include "owm_Model.h"

void owm_Draw_Plan(const Owm_Settings_t& settings, const Mtb_Kit_Wx_Status_t& status);

#endif
