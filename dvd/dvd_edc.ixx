module;
#include <cstdint>

export module dvd.edc;

import crc.crc;



namespace gpsxre
{

// CRC-32/DVD-ROM-EDC
// check value: 0xB27CE117
export typedef CRC<uint32_t, 0x80000011, 0, 0, false, false, false> DVD_EDC;

}
