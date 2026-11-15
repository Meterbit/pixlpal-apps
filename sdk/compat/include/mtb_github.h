// Pixlpal compat: the firmware's GitHub helpers the apps use. Files come from raw.githubusercontent.com (the app's
// app.json lists it in "net"), public ones only: no token. Flat files need no folders.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WString.h"
static inline bool mtb_Prepare_Flash_File_Path(const char*){ return true; }
bool mtb_Download_Github_File_To_SPIFFS(const String& bucketPath, const String& flashPath, const String account_Owner = "Meterbit",
                                        const String repo_Name = "PXP_X1_STORAGE", const String github_PAT = "");
/** Into a buffer the caller frees with heap_caps_free. */
bool mtb_Download_Github_File_To_PSRAM(const String& bucketPath, uint8_t** outBuffer, size_t* outSize, const String account_Owner = "Meterbit",
                                       const String repo_Name = "PXP_X1_STORAGE", const String github_PAT = "");
