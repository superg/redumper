module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
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
    std::string product_id;
    std::string bootstring_suffix;
    std::string svccode;
};

const std::vector<std::unordered_set<std::string>> MT1959_BOOTSTRING_SUFFIXES{
    { "BUP1" }, // slim (old)
    { "BU5 ", "BU51", "BUP3", "BP32", "BUP5", "BP52" }, // slim
    { "JB8 ", "JB9 ", "JBC6", "JBP6" }, // half-height
};

// OEM firmwares lack the SVC code
const std::vector<DriveEntry> MT1959_SUPPORTED_DRIVES = {
    { "BDDVDRW CH12NS40", "JBC6", "NS40" },
    { "BDDVDRW UH12NS40", "JBC6", "NS40" },
    { "BC-12B1ST b",      "JBC6", "    " },
    { "BC-12D2HT",        "JBC6", "    " },
    { "BD-RE BE16NU50",   "JBP6", "NU50" },
    { "BD-RE BH14NS40",   "JB8 ", "NS50" },
    { "BD-RE WH14NS40",   "JB8 ", "NS50" },
    { "BD-RE BH14NS50",   "JB8 ", "NS50" },
    { "BD-RE WH14NS50",   "JB8 ", "NS50" },
    { "BD-RE BH14NS58",   "JB8 ", "NS50" },
    { "BD-RE WH14NS58",   "JB8 ", "NS50" },
    { "BD-RE BH16NS40",   "JB8 ", "NS50" },
    { "BD-RE WH16NS40",   "JB8 ", "NS50" },
    { "BD-RE WH16NS46",   "JB8 ", "NS50" },
    { "BD-RE BH16NS50",   "JB8 ", "NS50" },
    { "BD-RE WH16NS50",   "JB8 ", "NS50" },
    { "BD-RE BH16NS55",   "JB8 ", "NS50" },
    { "BD-RE WH16NS55",   "JB8 ", "NS50" },
    { "BD-RE BH16NS58",   "JB8 ", "NS50" },
    { "BD-RE WH16NS58",   "JB8 ", "NS50" },
    { "BD-RE WH16NS58",   "JB8 ", "    " }, // Vinpower OEM
    { "BD-RE BH16NS60",   "JB8 ", "NS60" }, // JB9 board
    { "BD-RE WH16NS60",   "JB8 ", "NS60" }, // JB9 board
    { "BD-RE BH-A10AME",  "JB8 ", "    " },
    { "BD-RE BH40N",      "JB8 ", "    " },
    { "BD-RE BH50N",      "JB8 ", "    " },
    { "BW-16D1HT",        "JB8 ", "    " },
    { "BW-16D1H-U",       "JB8 ", "    " },
    { "BW-16D1X-U",       "JB8 ", "    " },
    { "BD-RE BP50NB40",   "BUP3", "NB50" },
    { "BD-RE BP50NB40",   "BP32", "NB52" },
    { "BD-RE WP50NB40",   "BUP3", "NB50" },
    { "BD-RE WP50NB40",   "BP32", "NB52" },
    { "BD-RE BP55EB40",   "BUP3", "NB50" }, // SVC code not a typo here
    { "BD-RE BP55EB40",   "BP32", "EB52" },
    { "BD-RE BP60NB10",   "BUP5", "NB10" },
    { "BD-RE BP60NB10",   "BP52", "NB12" },
    { "BD-RE BU40N",      "BU5 ", "BU40" },
    { "BD-RE BU40N",      "BU5 ", "    " },
    { "BDRE BU40N",       "BU5 ", "    " }, // HP version
    { "BD-RE BU50N",      "BU51", "    " },
    { "BDRE BU50N",       "BU51", "    " }, // HP version
    // { "BD-RE BP71N     ", "", "" }, // needs checking
};


void flash_mt1959(SPTD &sptd, std::span<const uint8_t> firmware_data, uint32_t block_size)
{
    if(auto status = cmd_write_buffer(sptd, nullptr, 0, WRITE_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, 0x67, 0, 0); status.status_code)
    {
        // slim flash unlocked failed, try standard
        status = cmd_write_buffer(sptd, nullptr, 0, WRITE_BUFFER_Mode::VENDOR_SPECIFIC, 0, 0, 0);
        if(status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));
    }

    for(uint32_t offset = 0; offset < firmware_data.size();)
    {
        uint32_t size = std::min(block_size, (uint32_t)(firmware_data.size() - offset));
        uint32_t offset_next = offset + size;

        LOGC_RF("[{:3}%] flashing: [{:08X} .. {:08X})", 100 * offset / (uint32_t)firmware_data.size(), offset, offset_next);

        if(auto status = cmd_write_buffer(sptd, &firmware_data[offset], size, WRITE_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, 0, offset, size); status.status_code)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));

        offset = offset_next;
    }
    LOGC("");
    LOGC("");

    constexpr uint8_t completion_value = 32;
    auto status = cmd_drive_ready(sptd);

    if(!status.status_code)
    {
        // if it's ready immediately the flash failed
        throw_line("failed to flash firmware, firmware file failed drive verification");
    }

    constexpr uint32_t timeout = 60;
    for(uint32_t i = 0; i <= timeout; i++)
    {
        if(i >= timeout)
            throw_line("failed to flash firmware, flash timed out");
        if(status.sense_key != 0x02 || status.asc != 0x04)
            throw_line("failed to flash firmware, SCSI ({})", SPTD::StatusMessage(status));
        LOGC_RF("waiting for flash to complete... [{}/{}]", status.information[1], completion_value);
        if(status = cmd_drive_ready(sptd); !status.status_code)
            break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOGC_RF("");
    LOGC("flashing success");
}


std::string_view extract_firmware_bootstring(std::span<const uint8_t> firmware_data, uint32_t bootstring_offset, uint32_t bootstring_size)
{
    return std::string_view((const char *)&firmware_data[bootstring_offset], bootstring_size);
}


std::string read_mt1959_bootstring(SPTD &sptd, uint32_t bootstring_offset, uint32_t bootstring_size)
{
    std::vector<uint8_t> bootstring(bootstring_size);

    if(auto status = cmd_read_buffer(sptd, bootstring.data(), bootstring.size(), READ_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, 0, bootstring_offset, bootstring_size); status.status_code)
        throw_line("failed to read drive bootstring, SCSI ({})", SPTD::StatusMessage(status));

    return std::string((const char *)bootstring.data(), bootstring_size);
}


std::string read_mt1959_svccode(SPTD &sptd, uint32_t svccode_offset, uint32_t svccode_size)
{
    std::vector<uint8_t> svccode(svccode_size);

    if(auto status = cmd_read_buffer(sptd, svccode.data(), svccode.size(), READ_BUFFER_Mode::DOWNLOAD_MICROCODE_WITH_OFFSETS, 0, svccode_offset, svccode_size); status.status_code)
        throw_line("failed to read drive SVC code, SCSI ({})", SPTD::StatusMessage(status));

    return std::string((const char *)svccode.data(), svccode_size);
}


void modify_firmware(std::span<uint8_t> firmware_data, uint32_t clear_size, uint32_t bootstring_offset, std::string &bootstring)
{
    std::fill(firmware_data.begin(), firmware_data.begin() + clear_size, (uint8_t)0xFF);
    std::copy(bootstring.begin(), bootstring.end(), &firmware_data[bootstring_offset]);
}


export int redumper_flash_mt1959(Context &ctx, Options &options)
{
    int exit_code = 0;

    constexpr uint32_t block_size = 0x4000;
    constexpr uint32_t bootstring_offset = 0x3000;
    constexpr uint32_t bootstring_size = 0x10;
    constexpr std::string_view bootstring_prefix = "MT1959 Boot ";
    constexpr uint32_t svccode_offset = 0x1EC04C;
    constexpr uint32_t svccode_size = 0x4;
    constexpr uint32_t clear_size = 0x10000;

    auto firmware_data = read_vector(options.firmware);
    if(firmware_data.size() < clear_size)
        throw_line("firmware data too small (size: {:#x}, required: {:#x})", firmware_data.size(), clear_size);

    auto drive_bootstring = read_mt1959_bootstring(*ctx.sptd, bootstring_offset, bootstring_size);
    auto drive_svccode = read_mt1959_svccode(*ctx.sptd, svccode_offset, svccode_size);

    if(!options.force_flash)
    {
        auto firmware_bootstring = extract_firmware_bootstring(firmware_data, bootstring_offset, bootstring_size);

        // compare prefix
        if(drive_bootstring.compare(0, bootstring_prefix.length(), bootstring_prefix) != 0)
            throw_line("unexpected drive bootstring prefix (current: {}, expected: {})", drive_bootstring.substr(0, bootstring_prefix.length()), bootstring_prefix);
        if(firmware_bootstring.compare(0, bootstring_prefix.length(), bootstring_prefix) != 0)
            throw_line("unexpected firmware bootstring prefix (current: {}, expected: {})", firmware_bootstring.substr(0, bootstring_prefix.length()), bootstring_prefix);

        auto firmware_bootstring_suffix = firmware_bootstring.substr(bootstring_prefix.length());
        auto drive_bootstring_suffix = drive_bootstring.substr(bootstring_prefix.length());

        bool is_compatible = false;
        for(auto &compatible_suffixes : MT1959_BOOTSTRING_SUFFIXES)
        {
            if(compatible_suffixes.contains(std::string(firmware_bootstring_suffix)) && compatible_suffixes.contains(std::string(drive_bootstring_suffix)))
                is_compatible = true;
        }
        if(!is_compatible)
            throw_line("incompatible firmware bootstring for drive (firmware bootstring: {}, drive bootstring: {})", firmware_bootstring_suffix, drive_bootstring_suffix);
    }


    auto bootstring = std::string(drive_bootstring.substr(bootstring_prefix.length()));
    // if drive is in boot mode we should use the detected string, otherwise we derive string based on product ID and SVC code
    if(ctx.drive_config.product_revision_level != "BOOT")
    {
        for(auto &drive_entry : MT1959_SUPPORTED_DRIVES)
        {
            if(drive_entry.product_id == ctx.drive_config.product_id)
            {
                bootstring = drive_entry.bootstring_suffix;
                // exit loop if SVC code matches, since that guarantees correct bootstring
                if(drive_entry.svccode == drive_svccode)
                    break;
            }
        }
    }
    bootstring = std::string(bootstring_prefix) + bootstring;

    modify_firmware(firmware_data, clear_size, bootstring_offset, bootstring);

    flash_mt1959(*ctx.sptd, firmware_data, block_size);

    return exit_code;
}

}
