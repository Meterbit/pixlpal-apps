// Fetching for the FAITH apps.
#include "fth_Net.h"
#include "mtb_kit_http.h"
#include "esp_heap_caps.h"
#include <stdio.h>

#define UA "PIXLPAL-L2/1.0 (Pixlpal Faith)"

void fth_Http_Error(char* out, size_t size, const char* who, int code){
    if (code <= 0) snprintf(out, size, "Cannot reach %s", who);
    else if (code == 429) snprintf(out, size, "%s is busy: trying again later", who);
    else if (code == 404) snprintf(out, size, "%s does not have that verse", who);
    else snprintf(out, size, "%s answered HTTP %d", who, code);
}

bool fth_Fetch_Bible(const char* reference, const char* translation, Fth_Verse_t& out, char* error, size_t errorSize){
    char url[256];
    if (error && errorSize) error[0] = '\0';
    if (!fth_Bible_Url(reference, translation, url, sizeof(url))) return false;
    Mtb_Kit_Http_t r;
    char* body = mtb_Kit_Http_Get(url, nullptr, nullptr, 0, 32 * 1024, r, UA);
    JsonDocument doc;
    bool ok = false;
    if (body && r.code == 200 && !deserializeJson(doc, body, r.length)) ok = fth_Parse_Bible(doc, translation, out);
    if (!ok && error){
        if (r.code == 200) fth_Copy(error, errorSize, "bible-api.com's answer was not understood");
        else fth_Http_Error(error, errorSize, "bible-api.com", r.code);
    }
    heap_caps_free(body);
    return ok;
}
