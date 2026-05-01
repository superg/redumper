module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
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
import utils.hex_bin;
import utils.logger;



namespace gpsxre
{

struct DriveEntry
{
    std::string vendor_id;
    std::string product_id;
    std::string config_id;
};

const std::vector<DriveEntry> MT1959_SUPPORTED_DRIVES = {
    { "HL-DT-ST", "BD-RE BU40N", "MT1959 Boot BU5 " },
    { "ASUS",     "BW-16D1HT",   "MT1959 Boot JB8 " }
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


std::vector<uint8_t> extract_firmware_config(std::span<const uint8_t> firmware_data, uint32_t config_offset, uint32_t config_size)
{
    return std::vector<uint8_t>(&firmware_data[config_offset], &firmware_data[config_offset + config_size]);
}


std::vector<uint8_t> read_mt1959_config(SPTD &sptd, uint32_t config_offset, uint32_t config_size)
{
    std::vector<uint8_t> config(config_size);

    if(auto status = cmd_read_buffer(sptd, config.data(), config.size(), READ_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, config_offset, config.size()); status.status_code)
        throw_line("failed to read drive patch configuration");

    return config;
}


void modify_firmware(std::span<uint8_t> firmware_data, uint32_t clear_size, uint32_t config_offset, std::span<const uint8_t> drive_config)
{
    std::fill(firmware_data.begin(), firmware_data.begin() + clear_size, (uint8_t)0xFF);
    std::copy(drive_config.begin(), drive_config.end(), &firmware_data[config_offset]);
}


export int redumper_flash_mt1959(Context &ctx, Options &options)
{
    int exit_code = 0;

    constexpr uint32_t block_size = 0x4000;
    constexpr uint32_t config_offset = 0x3000;
    constexpr uint32_t config_size = 0x20;
    constexpr uint32_t id_size = 0x10;
    constexpr uint32_t clear_size = 0x10000;

    auto firmware_data = read_vector(options.firmware);
    if(firmware_data.size() < clear_size)
        throw_line("firmware data too small (size: {:#x}, required: {:#x})", firmware_data.size(), clear_size);
    auto firmware_config = extract_firmware_config(firmware_data, config_offset, config_size);
    std::string_view firmware_id((const char *)firmware_config.data(), id_size);

    if(!options.force_flash)
    {
        auto it = std::find_if(MT1959_SUPPORTED_DRIVES.begin(), MT1959_SUPPORTED_DRIVES.end(),
            [&](const auto &d) { return d.vendor_id == ctx.drive_config.vendor_id && d.product_id == ctx.drive_config.product_id; });
        if(it == MT1959_SUPPORTED_DRIVES.end())
            throw_line("flashing of this drive is unsupported");
        else if(firmware_id != it->config_id)
            throw_line("unexpected firmware config id (current: {}, expected: {})", firmware_id, it->config_id);
    }

    auto drive_config = read_mt1959_config(*ctx.sptd, config_offset, config_size);
    std::string_view drive_id((const char *)drive_config.data(), id_size);
    if(drive_id != firmware_id)
        throw_line("firmware id mismatch (drive: {}, firmware: {})", drive_id, firmware_id);

    LOG("drive patch configuration (offset: 0x{:04X}, size: 0x{:X}): ", config_offset, config_size);
    LOG("{}", hexdump(drive_config.data(), 0, (uint32_t)drive_config.size()));

    modify_firmware(firmware_data, clear_size, config_offset, drive_config);

    flash_mt1959(*ctx.sptd, firmware_data, block_size);

    return exit_code;
}

}
