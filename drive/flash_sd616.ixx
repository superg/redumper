module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "throw_line.hh"

export module drive.flash.sd616;

import common;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

export int redumper_flash_sd616(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto firmware_data = read_vector(options.firmware);
    if(firmware_data.size() != 0x20000)
        throw_line("incorrect firmware size");

    constexpr uint32_t block_size = 0x10000;

    std::array<uint8_t, 0x30000> shifted_firmware_data{};
    memcpy(&shifted_firmware_data, &firmware_data[0], block_size);
    memcpy(&shifted_firmware_data[block_size], &firmware_data[(block_size)-0x400], 0x400);
    memcpy(&shifted_firmware_data[block_size + 0x400], &firmware_data[block_size], block_size - 0x800);
    memcpy(&shifted_firmware_data[block_size * 2], &firmware_data[(block_size * 2) - 0x800], 0x800);
    memcpy(&shifted_firmware_data[block_size * 2 + 0x800], &firmware_data[block_size + 0x400], block_size - 0xc00);

    uint32_t offset = 0;
    while(offset < shifted_firmware_data.size())
    {
        uint32_t size = 0xFFFF;
        uint32_t offset_next = offset + block_size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)shifted_firmware_data.size(), offset, offset + size);
        FLASH_SD616_Mode mode = offset == 0 ? FLASH_SD616_Mode::START : (offset_next < shifted_firmware_data.size() ? FLASH_SD616_Mode::CONTINUE : FLASH_SD616_Mode::END);

        SPTD::Status status = cmd_flash_sd616(*ctx.sptd, &shifted_firmware_data[offset], size, 0x01, mode);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");

    return exit_code;
}

}
