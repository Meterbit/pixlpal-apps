/**
 * @file mfc_Files.h
 * @brief Where My First Computer keeps the children's files on Pixlpal (mfc_Platform.cpp), for the app task too.
 */
#ifndef MFC_FILES_H
#define MFC_FILES_H

#include <stdint.h>
#include <stddef.h>

#define MFC_DIR "/littlefs/apps/mfc"

/** @brief /littlefs/apps/mfc/c<child>/<pic|txt|bin>/<name>.<png|txt> */
void mfc_File_Path(uint8_t child, uint8_t folder, const char* name, uint8_t kind, char* out, size_t size);
/** @brief A whole file into PSRAM (free it with heap_caps_free). False if missing or bigger than @p max. */
bool mfc_File_Read(const char* path, uint8_t** data, size_t* size, size_t max);

#endif
