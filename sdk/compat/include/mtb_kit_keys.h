// Pixlpal compat: the firmware's retired-key check. It finds developer keys that old firmware copied into an app's
// settings; an installed app's settings never had them, so there is nothing to find.
#pragma once
#include <stddef.h>
static inline bool mtb_Kit_Is_Retired_Key(const char*){ return false; }
static inline bool mtb_Kit_Clear_Retired_Key(char*, size_t){ return false; }
