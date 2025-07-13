module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_raw_reader;

import cd.cd;

// FIXME: deprecated, remove
import readers.image_simple_reader;
import readers.sector_reader;

import readers.image_simple_data_reader;
import readers.data_reader;



namespace gpsxre
{

// FIXME: deprecated, remove
export using Image_RawReader = Image_SimpleReader<SectorReader, CD_DATA_SIZE>;

export using Image_RAW_Reader = Image_SimpleDataReader<DataReader, CD_DATA_SIZE>;

}
