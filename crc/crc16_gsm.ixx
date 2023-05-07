module;
#include <cstdint>

export module crc.crc16_gsm;

import crc.crc;



namespace gpsxre
{

// CRC-16/GSM
// check value: 0xCE3C
export typedef CRC<uint16_t, 0x1021, 0, 0xFFFF, false, false, false> CRC16_GSM;

}
