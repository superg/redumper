#pragma once



#include <cstdint>



namespace gpsxre
{

uint32_t crc32_seed();
uint32_t crc32(const uint8_t *data, uint64_t size, uint32_t crc);
uint32_t crc32_final(uint32_t crc);
uint32_t crc32(const uint8_t *data, uint64_t size);

}
