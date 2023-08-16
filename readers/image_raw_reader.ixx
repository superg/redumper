module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_raw_reader;

import readers.image_simple_reader;
import readers.raw_reader;



namespace gpsxre
{

export using Image_RawReader = Image_SimpleReader<RawReader>;

}
