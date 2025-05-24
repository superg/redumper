module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_iso_form1_reader;

import cd.cdrom;
import readers.image_simple_reader;
import readers.sector_reader;



namespace gpsxre
{

export using Image_ISO_Form1Reader = Image_SimpleReader<SectorReader, FORM1_DATA_SIZE>;

}
