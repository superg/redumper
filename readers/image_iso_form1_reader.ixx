module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_iso_form1_reader;

import readers.form1_reader;
import readers.image_simple_reader;



namespace gpsxre
{

export using Image_ISO_Form1Reader = Image_SimpleReader<Form1Reader>;

}
