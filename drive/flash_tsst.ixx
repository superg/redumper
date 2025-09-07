module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
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

export void flash_tsst(SPTD &sptd, const std::span<const uint8_t> firmware_data, uint32_t block_size, FLASH_TSST_Mode end_mode)
{
    for(uint32_t offset = 0; offset < firmware_data.size();)
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset_next);

        FLASH_TSST_Mode mode = offset == 0 ? FLASH_TSST_Mode::START : (offset_next < firmware_data.size() ? FLASH_TSST_Mode::CONTINUE : end_mode);

        SPTD::Status status = cmd_flash_tsst(sptd, &firmware_data[offset], size, 0x01, mode);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");
}

export int redumper_flash_tsst(Context &ctx, Options &options)
{
    int exit_code = 0;

    // block size is how much data is sent in one command, potentially it can vary but current value is taken from the original flasher
    constexpr uint32_t block_size = 0xFC00;

    flash_tsst(*ctx.sptd, read_vector(options.firmware), block_size, FLASH_TSST_Mode::END_256KB);

    return exit_code;
}

}
