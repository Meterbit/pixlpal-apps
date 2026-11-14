#include "omt_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>

void omt_Draw_Plan(const Omt_Settings_t& s, const Mtb_Kit_Wx_Status_t& status){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    const bool paid = s.apiKey[0] != '\0';
    mtb_Kit_Wx_Band("OPEN-METEO", paid ? "YOUR API KEY" : "FREE API", MTB_KIT_RGB(10, 50, 100));
    char text[128];
    snprintf(text, sizeof(text), "%s", status.plan.known ? status.plan.name : paid ? "Checking your key" : "Free, non-commercial");
    mtb_Kit_Upper(text);
    mtb_Kit_Text(4, 25, Terminal8x12, paid ? KIT_GREEN : KIT_GOLD, text);
    const float perDay = omt_Calls_Per_Day(s.refreshMinutes, s.air);
    snprintf(text, sizeof(text), "%u", (unsigned)status.usedToday);
    const int16_t x = mtb_Kit_Text(4, 42, Terminal10x17, KIT_TEXT, text);
    mtb_Kit_Text(x + 6, 48, Terminal6x8, KIT_LABEL, paid ? "REQUESTS TODAY" : "REQUESTS TODAY OF 10,000 FREE");
    if (!paid) mtb_Kit_Bar(4, 63, 248, 4, status.usedToday / 10000.0f, KIT_GREEN);
    snprintf(text, sizeof(text), "CONDITIONS EVERY %u MIN, FORECAST HOURLY%s: ABOUT %.0f CALLS A DAY",
             (unsigned)s.refreshMinutes, s.air ? ", AIR HOURLY" : "", perDay);
    char lines[4][80];
    int n = mtb_Kit_Wrap(text, Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 2);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 72 + i * 8, Terminal4x6, KIT_LABEL, lines[i]);
    n = mtb_Kit_Wrap(paid ? "YOUR KEY USES OPEN-METEO'S RESERVED SERVERS AND ALLOWS COMMERCIAL USE; CONDITIONS CAN BE READ EVERY 5 MINUTES."
                          : "THE FREE API IS FOR NON-COMMERCIAL USE. A KEY FROM OPEN-METEO.COM/EN/PRICING ADDS COMMERCIAL USE, RESERVED SERVERS AND 5-MINUTE UPDATES.",
                     Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 3);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 92 + i * 8, Terminal4x6, KIT_DIM, lines[i]);
    mtb_Kit_Wx_Credit("WEATHER DATA BY OPEN-METEO.COM");
}
