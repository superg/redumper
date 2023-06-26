module;
#include <cstdint>

export module readers.form1_reader;

import cd.cdrom;
import readers.block_reader;



namespace gpsxre
{

export typedef BlockReader<uint32_t, FORM1_DATA_SIZE> Form1Reader;

}
