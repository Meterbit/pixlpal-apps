// Pixlpal compat: the ROM's CRC-32 (the same polynomial and convention: esp_rom_crc32_le(0, data, n) as usual).
#pragma once
#include <stdint.h>
#include <stddef.h>
static inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len){
    crc = ~crc;
    while (len--){
        crc ^= *buf++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
