#include "owm_Ui.h"
#include "mtb_kit_ui.h"
#include <stdio.h>

void owm_Draw_Plan(const Owm_Settings_t& s, const Mtb_Kit_Wx_Status_t& status){
    mtb_Panel_Fill_Rect(0, 10, PANEL_RES_X - 1, PANEL_RES_Y - 1, BLACK);
    mtb_Kit_Wx_Band("OPENWEATHER", "YOUR PLAN", MTB_KIT_RGB(10, 50, 100));
    const Mtb_Kit_Wx_Plan_t& plan = status.plan;
    char text[128];
    snprintf(text, sizeof(text), "%s", (plan.flags & OWM_HAS_KEY) ? plan.name : "Checking your key");
    mtb_Kit_Upper(text);
    mtb_Kit_Text(4, 25, Terminal8x12, plan.paid ? KIT_GREEN : KIT_GOLD, text);
    // What each API gives, ticked when the key has it.
    struct Row_t { const char* name; const char* gives; uint32_t flag; bool on; };
    const Row_t rows[4] = {
        {"CURRENT, 5 DAY / 3 HOUR, AIR", "EVERY KEY", OWM_HAS_KEY, true},
        {"16-DAY FORECAST", "STARTUP PLAN AND UP", OWM_HAS_DAILY, true},
        {"4-DAY HOURLY FORECAST", "DEVELOPER PLAN AND UP", OWM_HAS_HOURLY, true},
        {"ONE CALL 3.0: 48 H, ALERTS", "ONE CALL BY CALL", OWM_HAS_ONECALL, s.oneCall != 0},
    };
    for (int i = 0; i < 4; i++){
        const int16_t y = 42 + i * 12;
        const bool has = plan.flags & rows[i].flag;
        const bool tried = rows[i].flag == OWM_HAS_KEY ? (plan.flags & OWM_HAS_KEY) != 0 : (plan.tested & rows[i].flag) != 0;
        const uint16_t color = has ? KIT_GREEN : tried ? KIT_DIM : KIT_LABEL;
        mtb_Kit_Text(4, y, Terminal6x8, color, has ? "+" : tried ? "-" : "?");
        mtb_Kit_Text(14, y, Terminal6x8, has ? KIT_TEXT : KIT_LABEL, rows[i].name);
        mtb_Kit_Text_Right(252, y + 1, Terminal4x6, rows[i].on ? KIT_DIM : KIT_ORANGE, rows[i].on ? rows[i].gives : "OFF IN SETTINGS");
    }
    snprintf(text, sizeof(text), "%u CALLS TODAY; ONE CALL AT MOST 96 A DAY, INSIDE ITS 1,000 FREE", (unsigned)status.usedToday);
    char lines[2][80];
    const int n = mtb_Kit_Wrap(text, Terminal4x6, 248, &lines[0][0], sizeof(lines[0]), 2);
    for (int i = 0; i < n; i++) mtb_Kit_Text(4, 94 + i * 8, Terminal4x6, KIT_LABEL, lines[i]);
    mtb_Kit_Wx_Credit("WEATHER DATA (C) OPENWEATHER");
}
