module;
#include <filesystem>
#include <fstream>
#include <string>

export module readers.image_bin_reader;

import cd.cd;
import readers.image_scram_reader;



namespace gpsxre
{

export class Image_BIN_Reader : public Image_SCRAM_Reader
{
public:
    Image_BIN_Reader(const std::string &image_path)
        : _fs(image_path, std::fstream::in | std::fstream::binary)
    {
        init(&_fs, 0, std::filesystem::file_size(image_path) / CD_DATA_SIZE, false);
    }

private:
    std::fstream _fs;
};

}
