#pragma once



#include <cstdint>



// CRC-16/GSM
// width=16 poly=0x1021 init=0x0000 refin=false refout=false xorout=0xffff check=0xce3c residue=0x1d0f name="CRC-16/GSM"
// ECMA standard ECMA-130



namespace gpsxre
{

uint32_t crc16_seed();
uint16_t crc16_gsm(const uint8_t *data, uint64_t size, uint16_t crc);
uint16_t crc16_gsm_final(uint16_t crc);
uint16_t crc16_gsm(const uint8_t *data, uint64_t size);

}
