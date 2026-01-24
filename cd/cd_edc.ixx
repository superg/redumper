module;
#include <algorithm>
#include <cstdint>

export module cd.edc;

import crc.crc;



namespace gpsxre
{

// CRC-32/CD-ROM-EDC
// check value: 0x6EC2EDC4
export typedef CRC<uint32_t, 0x8001801B, 0, 0, true, true, true> EDC;

}
