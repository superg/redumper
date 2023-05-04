#pragma once



#include <filesystem>
#include <string>
#include "options.hh"

import sptd;
import drive;



namespace gpsxre
{

DriveConfig drive_init(SPTD &sptd, const Options &options);
std::string image_init(const Options &options);
void image_check_overwrite(std::filesystem::path state_path, const Options &options);
void strip_toc_response(std::vector<uint8_t> &data);

}
