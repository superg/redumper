module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_iso_reader;

import cd.cdrom;
import readers.image_simple_data_reader;
import readers.data_reader;



namespace gpsxre
{

export using Image_ISO_Reader = Image_SimpleDataReader<DataReader, FORM1_DATA_SIZE>;

}
