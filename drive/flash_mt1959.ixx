module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "throw_line.hh"

export module drive.flash.mt1959;

import common;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

const std::vector<std::pair<std::string, std::string>> MT1959_SUPPORTED_DRIVES = {
    // { "HL-DT-ST", "BD-RE BU40N" },
    { "ASUS", "BW-16D1HT" }
};


void flash_mt1959(SPTD &sptd, std::span<const uint8_t> firmware_data, uint32_t block_size)
{
    if(auto status = cmd_write_buffer(sptd, nullptr, 0, WRITE_BUFFER_Mode::VENDOR_SPECIFIC, 0, 0); status.status_code)
        throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

    for(uint32_t offset = 0; offset < firmware_data.size();)
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset_next);

        if(auto status = cmd_write_buffer(sptd, &firmware_data[offset], size, WRITE_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, offset, size); status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }

    cmd_drive_ready(sptd);

    LOGC_RF("");
    LOGC("flashing success");
}


std::vector<uint8_t> read_mt1959_config(SPTD &sptd, uint32_t config_offset, uint32_t config_size)
{
    std::vector<uint8_t> config(config_size);

    if(auto status = cmd_read_buffer(sptd, config.data(), config.size(), READ_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, config_offset, config.size()); status.status_code)
        throw_line("failed to read drive patch configuration");

    return config;
}


void modify_firmware(std::span<uint8_t> firmware_data, uint32_t config_offset, std::span<const uint8_t> drive_config)
{
    std::fill(firmware_data.begin(), firmware_data.begin() + 0x10000, (uint8_t)0xFF);
    std::copy(drive_config.begin(), drive_config.end(), &firmware_data[config_offset]);
}


export int redumper_flash_mt1959(Context &ctx, Options &options)
{
    int exit_code = 0;

    uint32_t block_size = 0x4000;

    if(!options.force_flash
        && std::find_if(MT1959_SUPPORTED_DRIVES.begin(), MT1959_SUPPORTED_DRIVES.end(), [&](const auto &d) { return d.first == ctx.drive_config.vendor_id && d.second == ctx.drive_config.product_id; })
               == MT1959_SUPPORTED_DRIVES.end())
        throw_line("flashing of this drive is unsupported");

    uint32_t config_offset = 0x3000;
    uint32_t config_size = 0x20;
    auto drive_config = read_mt1959_config(*ctx.sptd, config_offset, config_size);

    auto firmware_data = read_vector(options.firmware);
    modify_firmware(firmware_data, config_offset, drive_config);

    flash_mt1959(*ctx.sptd, firmware_data, block_size);

    return exit_code;
}

}
