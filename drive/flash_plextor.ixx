module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
#include <string>
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

const std::map<std::string, uint32_t> PLEXTOR_SUPPORTED_DRIVES = {
    { "CD-R PX-W2410A", 0x4000 },
    { "CD-R PX-W4012A", 0x1000 },
    { "CD-R PX-W4012S", 0x1000 },
    { "CD-R PX-W4824A", 0x1000 },
    { "CD-R PX-W5224A", 0x1000 },
    { "CD-R PREMIUM",   0x1000 },
    { "CD-R PREMIUM2",  0x1000 },
    { "DVDR PX-704A",   0x1000 },
    { "DVDR PX-708A",   0x1000 },
    { "DVDR PX-708A2",  0x1000 },
    { "DVDR PX-712A",   0x4000 },
    { "DVDR PX-714A",   0x4000 },
    { "DVDR PX-716A",   0x4000 },
    { "DVDR PX-716AL",  0x4000 },
    { "DVDR PX-755A",   0x4000 },
    { "DVDR PX-760A",   0x4000 }
};

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

    uint32_t block_size = PLEXTOR_SUPPORTED_DRIVES.begin()->second;
    auto it = PLEXTOR_SUPPORTED_DRIVES.find(ctx.drive_config.product_id);
    if(it != PLEXTOR_SUPPORTED_DRIVES.end())
        block_size = it->second;

    if(!options.force_flash)
    {
        if(ctx.drive_config.vendor_id != "PLEXTOR")
            throw_line("drive is not PLEXTOR");

        if(it == PLEXTOR_SUPPORTED_DRIVES.end())
            throw_line("flashing of this drive is unsupported");
    }

    // TODO: verify SCSI commands for flashing PX-712A EPS firmware

    flash_plextor(*ctx.sptd, read_vector(options.firmware), block_size);

    return exit_code;
}

}
