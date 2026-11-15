/**
 * @file fbl_Crest.h
 * @brief Team crests: API-Football's 150x150 PNGs, shrunk to FBL_CREST px with their transparency kept, stored on
 *        LittleFS so each is downloaded once, and kept in PSRAM for drawing. Loading runs in a task of its own.
 */
#ifndef FBL_CREST_H
#define FBL_CREST_H

#include <stdint.h>
#include <stddef.h>

#define FBL_CREST       30
#define FBL_CREST_SLOTS 16

/** @brief Shrinks an RGBA image to FBL_CREST x FBL_CREST, centred, colours weighted by their alpha. No Arduino. */
void fbl_Crest_Downscale(const uint8_t* rgba, unsigned width, unsigned height, uint16_t* rgb565, uint8_t* alpha);

/** @brief Asks for a team's crest to be loaded (from flash, else the internet). Returns at once. */
void fbl_Crest_Want(uint32_t teamId);
/** @brief Draws the crest blended over @p background. Returns false if it is not loaded (yet). */
bool fbl_Crest_Draw(uint32_t teamId, int16_t x, int16_t y, uint16_t background);
/** @brief Counts crests loaded, so the screen knows to redraw. */
uint32_t fbl_Crest_Version(void);

#endif
