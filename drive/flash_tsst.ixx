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

export int redumper_flash_tsst(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto firmware_data = read_vector(options.firmware);
    FLASH_Tsst_Mode end_mode = FLASH_Tsst_Mode::END_256KB;
    uint32_t block_size = 0xFC00;
    uint32_t read_size = 0xFC00;

    if(firmware_data.size() != 0x20000 && firmware_data.size() != 0x40000)
    {
        throw_line("incorrect firmware size");
    }

    if(firmware_data.size() == 0x20000)
    {
        block_size = 0x10000;
        read_size = 0xFFFF;
        end_mode = FLASH_Tsst_Mode::END_128KB;

        std::vector<uint8_t> shifted_firmware_data{};
        shifted_firmware_data.resize(0x30000);
        std::copy(firmware_data.begin(), std::next(firmware_data.begin(), block_size), shifted_firmware_data.begin());
        std::copy(std::next(firmware_data.begin(), block_size), std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(shifted_firmware_data.begin(), block_size + 0x400));
        std::copy(std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(firmware_data.begin(), (block_size * 2)), std::next(shifted_firmware_data.begin(), block_size * 2));
        std::copy(std::next(firmware_data.begin(), block_size + 0x400), std::next(firmware_data.begin(), (block_size * 2) - 0x800), std::next(shifted_firmware_data.begin(), (block_size * 2) + 0x800));

        firmware_data = shifted_firmware_data;
    }

    uint32_t offset = 0;
    while(offset < firmware_data.size())
    {
        uint32_t next_size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + next_size;
        uint32_t size = std::min(next_size, read_size);

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset + size);

        FLASH_Tsst_Mode mode = offset == 0 ? FLASH_Tsst_Mode::START : (offset_next < firmware_data.size() ? FLASH_Tsst_Mode::CONTINUE : end_mode);

        SPTD::Status status = cmd_flash_tsst(*ctx.sptd, &firmware_data[offset], size, 0x01, mode);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");

    return exit_code;
}

}
