/**
 * @file tdw_App.h
 * @brief Tides & Water's worker (tdw_Net.cpp), its screens (tdw_Ui.cpp) and the app task (tides.cpp).
 */
#ifndef TDW_APP_H
#define TDW_APP_H

#include "tdw_Model.h"

#define TDW_PLACES_MAX  8

struct Tdw_Places_t {
    char searchId[12];                  /**< the searchPicker that asked: "places" or "rivers" */
    char query[64];
    int count;                          /**< -1: the search failed */
    char names[TDW_PLACES_MAX][TDW_PLACE_LEN];
    char details[TDW_PLACES_MAX][48];
    double lats[TDW_PLACES_MAX], lons[TDW_PLACES_MAX];
};

struct Tdw_Station_t { uint8_t checked; char id[12]; char name[40]; float km; };

bool tdw_Net_Start(const Tdw_Settings_t& settings);
void tdw_Net_Stop(void);
void tdw_Net_Set_Settings(const Tdw_Settings_t& settings);
/** @brief Read everything again now (paid tide providers only if their data is over 3 hours old). */
void tdw_Net_Refresh(void);
bool tdw_Net_Copy(Tdw_Data_t& data, uint32_t& version);
/** @brief Search places by name (Open-Meteo geocoding) for the picker @p searchId; the answer comes from Take_Places. */
bool tdw_Net_Search(const char* searchId, const char* query);
bool tdw_Net_Take_Places(Tdw_Places_t& places);
/** @brief The NOAA station found for the place, once, so the app can keep it in the settings. */
bool tdw_Net_Take_Station(Tdw_Station_t& station);
/** @brief WorldTides credits spent, once after each call that spent some, for the settings' monthly count. */
bool tdw_Net_Take_Credits(uint32_t& credits);

// Screens --------------------------------------------------------------------------------------------------------
struct Tdw_View_t {
    uint8_t page;                       /**< Tdw_Page_t */
    const Tdw_Settings_t* s;
    const Tdw_Data_t* d;
    uint32_t now;
    const char* message;                /**< instead of the page, when set */
};
void tdw_Draw(const Tdw_View_t& v);

#endif
