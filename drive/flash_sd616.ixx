module;
#include <algorithm>
#include <cstdint>
#include <vector>
#include "throw_line.hh"

export module drive.flash.sd616;

import common;
import drive.flash.tsst;
import options;
import scsi.mmc;
import utils.file_io;



namespace gpsxre
{

export int redumper_flash_sd616(Context &ctx, Options &options)
{
    int exit_code = 0;

    constexpr uint32_t block_size = 0x10000;

    auto firmware_file = read_vector(options.firmware);
    if(firmware_file.size() != 0x20000)
        throw_line("failed to flash firmware, file is not 128KB");

    // rearrange firmware data to match the expected layout
    std::vector<uint8_t> firmware_data(0x30000);
    std::copy(firmware_file.begin(), firmware_file.begin() + block_size, firmware_data.begin());
    std::copy(firmware_file.begin() + block_size, firmware_file.begin() + block_size * 2 - 0x800, firmware_data.begin() + block_size + 0x400);
    std::copy(firmware_file.begin() + block_size * 2 - 0x800, firmware_file.begin() + block_size * 2, firmware_data.begin() + block_size * 2);
    std::copy(firmware_file.begin() + block_size + 0x400, firmware_file.begin() + block_size * 2 - 0x800, firmware_data.begin() + block_size * 2 + 0x800);

    flash_tsst(*ctx.sptd, firmware_data, block_size, FLASH_TSST_Mode::END_128KB);

    return exit_code;
}

}
