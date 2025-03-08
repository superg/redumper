module;
#include <algorithm>
#include <fstream>
#include "throw_line.hh"

export module drive.flash.mt1339;

import dump;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

export void redumper_flash_mt1339(Context &ctx, Options &options)
{
    SPTD sptd(options.drive);

    auto firmware_data = read_vector(options.firmware);

    constexpr uint32_t block_size = 0xFC00;

    uint32_t offset = 0;
    while(offset < firmware_data.size())
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset + size);

        FLASH_MT1339_Mode mode = offset == 0 ? FLASH_MT1339_Mode::START : (offset_next < firmware_data.size() ? FLASH_MT1339_Mode::CONTINUE : FLASH_MT1339_Mode::END);

        SPTD::Status status = cmd_flash_mt1339(sptd, &firmware_data[offset], size, 0x01, mode);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    LOGC_RF("");
    LOGC("flashing success");
}

}
