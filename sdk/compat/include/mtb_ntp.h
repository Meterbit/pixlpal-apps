// Pixlpal compat: the firmware's clock service. Installed apps have the time through pxl_time (its UTC offset); the
// owner's time zone name is not shared, so ntp_TimeZone stays empty.
#pragma once
inline char ntp_TimeZone[64] = "";
