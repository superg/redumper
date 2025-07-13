module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_raw_reader;

import cd.cd;
import readers.image_simple_data_reader;
import readers.data_reader;



namespace gpsxre
{

export using Image_RAW_Reader = Image_SimpleDataReader<DataReader, CD_DATA_SIZE>;

}
