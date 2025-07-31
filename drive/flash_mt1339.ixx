module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "throw_line.hh"

export module drive.flash.tsst;

import common;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

export int flash_mt1339(SPTD &sptd, const std::span<const uint8_t> firmware_data, uint32_t block_size, FLASH_Tsst_Mode end_mode)
{
    int exit_code = 0;

    uint32_t offset = 0;
    while(offset < firmware_data.size())
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset + size);

        FLASH_Tsst_Mode mode = offset == 0 ? FLASH_Tsst_Mode::START : (offset_next < firmware_data.size() ? FLASH_Tsst_Mode::CONTINUE : end_mode);

        SPTD::Status status = cmd_flash_tsst(sptd, &firmware_data[offset], size, 0x01, mode);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");

    return exit_code;
}

export int redumper_flash_mt1339(Context &ctx, Options &options)
{
    return flash_mt1339(*ctx.sptd, read_vector(options.firmware), 0xFC00, FLASH_Tsst_Mode::END);
}

export int redumper_flash_sd616(Context &ctx, Options &options)
{
    uint32_t block_size = 0x10000;
    auto firmware_data = read_vector(options.firmware);
    std::vector<uint8_t> shifted_firmware_data{};
    shifted_firmware_data.resize(0x30000);
    std::copy(firmware_data.begin(), std::next(firmware_data.begin(), block_size), shifted_firmware_data.begin());
    std::copy(std::next(firmware_data.begin(), block_size), std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(shifted_firmware_data.begin(), block_size + 0x400));
    std::copy(std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(firmware_data.begin(), (block_size * 2)), std::next(shifted_firmware_data.begin(), block_size * 2));
    std::copy(std::next(firmware_data.begin(), block_size + 0x400), std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(shifted_firmware_data.begin(), (block_size * 2) + 0x800));
    return flash_mt1339(*ctx.sptd, shifted_firmware_data, block_size, FLASH_Tsst_Mode::END_SAMSUNG);
}

}
