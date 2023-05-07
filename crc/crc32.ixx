module;
#include <cstdint>

export module crc.crc32;

import crc.crc;



namespace gpsxre
{

// CRC-32
// check value: 0xCBF43926
export typedef CRC<uint32_t, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true, true> CRC32;

}
