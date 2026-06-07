module;

export module readers.image_disk_reader;

import readers.emulated_sector_reader;



namespace gpsxre
{

export using Image_Disk_Reader = Emulated_Sector_Reader<512>;

}
