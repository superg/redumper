module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <vector>
#include "throw_line.hh"

export module drive.flash.plextor;

import common;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

export void flash_plextor(SPTD &sptd, const std::span<const uint8_t> firmware_data, uint32_t block_size)
{
    for(uint32_t offset = 0; offset < firmware_data.size();)
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset_next);

        // PLEXTOR original flasher does a drive ready check before each block write, not sure this is needed though
        cmd_drive_ready(sptd);

        auto mode = offset_next < firmware_data.size() ? WRITE_BUFFER_Mode::DOWNLOAD_MICROCODE : WRITE_BUFFER_Mode::DOWNLOAD_MICROCODE_SAVE;
        SPTD::Status status = cmd_write_buffer(sptd, &firmware_data[offset], size, mode, offset, size);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");
}

export int redumper_flash_plextor(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.drive_config.vendor_id != "PLEXTOR")
        throw_line("drive is not PLEXTOR");

    if(ctx.drive_config.c2_shift != 294 && ctx.drive_config.c2_shift != 295)
        throw_line("this PLEXTOR drive is unsupported");

    // according to the original flasher, older drives seem to use 4KB, newer 16KB, C2 shift is a handy way to distinguish between them
    uint32_t block_size = ctx.drive_config.c2_shift == 294 ? 0x1000 : 0x4000;

    // TODO: implement inquiry patching for drives with same hardware but different product id

    flash_plextor(*ctx.sptd, read_vector(options.firmware), block_size);

    return exit_code;
}

}
