module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.split;

import bd;
import cd.cdrom;
import common;
import dvd;
import dvd.nintendo;
import dvd.xbox;
import options;
import range;
import rom_entry;
import scsi.cmd;
import scsi.mmc;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.misc;



namespace gpsxre
{

void generate_extra_xbox(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    // do not attempt to generate .ss, .dmi or .pfi for non-xbox discs (dumps without .security)
    std::filesystem::path security_path(image_prefix + ".security");
    if(!std::filesystem::exists(security_path))
        return;

    // trim the 4 byte header from .manufacturer and write it to a .dmi (if it doesn't exist)
    std::filesystem::path manufacturer_path(image_prefix + ".manufacturer");
    std::filesystem::path dmi_path(image_prefix + ".dmi");
    if(std::filesystem::exists(manufacturer_path))
    {
        if(std::filesystem::exists(dmi_path) && !options.overwrite)
        {
            LOG("warning: file already exists ({})", dmi_path.filename().string());
        }
        else
        {
            auto manufacturer = read_vector(manufacturer_path);
            if(manufacturer.size() == FORM1_DATA_SIZE + sizeof(CMD_ParameterListHeader))
            {
                strip_response_header(manufacturer);
                write_vector(dmi_path, manufacturer);

                ROMEntry dmi_rom_entry(dmi_path.filename().string());
                dmi_rom_entry.update(manufacturer.data(), FORM1_DATA_SIZE);
                if(ctx.dat.has_value())
                    ctx.dat->push_back(dmi_rom_entry.xmlLine());
            }
            else
            {
                LOG("warning: could not generate DMI, unexpected file size ({})", manufacturer_path.filename().string());
            }
        }
    }

    // trim the 4 byte header from .physical and write it to a .pfi (if it doesn't exist)
    std::filesystem::path physical_path(image_prefix + ".physical");
    std::filesystem::path pfi_path(image_prefix + ".pfi");
    if(std::filesystem::exists(physical_path))
    {
        if(std::filesystem::exists(pfi_path) && !options.overwrite)
        {
            LOG("warning: file already exists ({})", pfi_path.filename().string());
        }
        else
        {
            auto physical = read_vector(physical_path);
            if(physical.size() == FORM1_DATA_SIZE + sizeof(CMD_ParameterListHeader))
            {
                strip_response_header(physical);
                write_vector(pfi_path, physical);

                ROMEntry pfi_rom_entry(pfi_path.filename().string());
                pfi_rom_entry.update(physical.data(), FORM1_DATA_SIZE);
                if(ctx.dat.has_value())
                    ctx.dat->push_back(pfi_rom_entry.xmlLine());
            }
            else
            {
                LOG("warning: could not generate PFI, unexpected file size ({})", physical_path.filename().string());
            }
        }
    }

    // clean the .security and write it to a .ss (if it doesn't exist)
    std::filesystem::path ss_path(image_prefix + ".ss");
    if(std::filesystem::exists(ss_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({})", ss_path.filename().string());
    }
    else
    {
        auto security = read_vector(security_path);
        if(security.size() == FORM1_DATA_SIZE)
        {
            xbox::clean_security_sector(security);
            write_vector(ss_path, security);

            ROMEntry ss_rom_entry(ss_path.filename().string());
            ss_rom_entry.update(security.data(), FORM1_DATA_SIZE);
            if(ctx.dat.has_value())
                ctx.dat->push_back(ss_rom_entry.xmlLine());
        }
        else
        {
            LOG("warning: could not generate SS, unexpected file size ({})", security_path.filename().string());
        }
    }
}


void progress(uint64_t sector, uint64_t sectors_count)
{
    char animation = sector == sectors_count ? '*' : spinner_animation();

    LOGC_RF("{} [{:3}%] splitting", animation, sector * 100 / sectors_count);
}


void dvd_extract_iso(Context &ctx, std::filesystem::path sdram_path, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path iso_path(image_prefix + ".iso");
    std::filesystem::path physical_path(image_prefix + ".physical");
    if(std::filesystem::exists(iso_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({})", iso_path.filename().string());
        return;
    }

    uint64_t sdram_size = std::filesystem::file_size(sdram_path);
    if(sdram_size % sizeof(dvd::RecordingFrame) != 0)
        throw_line("unexpected file size ({})", sdram_path.filename().string());
    std::fstream sdram_fs(sdram_path, std::fstream::in | std::fstream::binary);
    if(!sdram_fs.is_open())
        throw_line("unable to open file ({})", sdram_path.filename().string());

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path.filename().string());

    std::fstream iso_fs(iso_path, std::fstream::out | std::fstream::binary);
    if(!iso_fs.is_open())
        throw_line("unable to open file ({})", iso_path.filename().string());

    std::optional<uint8_t> nintendo_key;
    std::vector<std::pair<int32_t, int32_t>> invalid_data_frames;
    uint32_t main_data_offset = offsetof(dvd::DataFrame, main_data);

    // start extracting ISO from LBA 0
    sdram_fs.seekg(-dvd::LBA_START * sizeof(dvd::RecordingFrame));
    if(sdram_fs.fail())
        throw_line("seek failed");

    if(std::filesystem::exists(physical_path))
    {
        auto physical = read_vector(physical_path);
        if(physical.size() > sizeof(CMD_ParameterListHeader) && physical[sizeof(CMD_ParameterListHeader)] == 0xFF)
            nintendo_key = 0;
    }

    if(nintendo_key)
        main_data_offset = offsetof(dvd::DataFrame, cpr_mai);

    uint32_t sectors_count = sdram_size / sizeof(dvd::RecordingFrame) + dvd::LBA_START;
    for(uint32_t lba = 0; lba < sectors_count; ++lba)
    {
        progress(lba, sectors_count);

        State state;
        read_entry(state_fs, (uint8_t *)&state, sizeof(State), lba - dvd::LBA_START, 1, 0, (uint8_t)State::ERROR_SKIP);
        if(state == State::ERROR_SKIP && !options.force_split)
            throw_line("read errors detected, unable to continue");

        dvd::RecordingFrame rf;
        read_entry(sdram_fs, (uint8_t *)&rf, sizeof(rf), lba - dvd::LBA_START, 1, 0, 0);
        auto df = RecordingFrame_to_DataFrame(rf);
        if(df.id.id.zone_type == dvd::ZoneType::LEADOUT_ZONE)
            break;

        std::span<uint8_t> data((uint8_t *)&df + main_data_offset, FORM1_DATA_SIZE);

        auto key = nintendo::get_key(nintendo_key, lba, df);
        bool valid = df.valid(key);
        df.descramble(key);

        if(!valid)
        {
            if(!options.leave_unchanged)
                std::fill(data.begin(), data.end(), 0);

            if(invalid_data_frames.empty() || invalid_data_frames.back().second + 1 != lba)
                invalid_data_frames.emplace_back(lba, lba);
            else
                invalid_data_frames.back().second = lba;
        }

        iso_fs.write((char *)&data[0], data.size());
        if(iso_fs.fail())
            throw_line("write failed ({})", iso_path.filename().string());
    }

    progress(sectors_count, sectors_count);
    LOG("");
    LOG("");

    for(auto const &d : invalid_data_frames)
    {
        if(d.first == d.second)
            LOG("warning: invalid data frame (LBA: {})", d.first);
        else
            LOG("warning: invalid data frame (LBA: [{} .. {}])", d.first, d.second);
    }
}


void bd_extract_iso(Context &ctx, std::filesystem::path sbram_path, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path iso_path(image_prefix + ".iso");
    std::filesystem::path physical_path(image_prefix + ".physical");
    if(std::filesystem::exists(iso_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({})", iso_path.filename().string());
        return;
    }

    uint64_t sbram_size = std::filesystem::file_size(sbram_path);
    if(sbram_size % sizeof(bd::DataFrame) != 0)
        throw_line("unexpected file size ({})", sbram_path.filename().string());
    std::fstream sbram_fs(sbram_path, std::fstream::in | std::fstream::binary);
    if(!sbram_fs.is_open())
        throw_line("unable to open file ({})", sbram_path.filename().string());

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path.filename().string());

    std::fstream iso_fs(iso_path, std::fstream::out | std::fstream::binary);
    if(!iso_fs.is_open())
        throw_line("unable to open file ({})", iso_path.filename().string());

    std::vector<std::pair<int32_t, int32_t>> invalid_data_frames;

    // start extracting ISO from LBA 0
    sbram_fs.seekg(-bd::LBA_START * sizeof(bd::DataFrame));
    if(sbram_fs.fail())
        throw_line("seek failed");

    uint32_t sectors_count = sbram_size / sizeof(bd::DataFrame) + bd::LBA_START;
    for(uint32_t lba = 0; lba < sectors_count; ++lba)
    {
        progress(lba, sectors_count);

        State state;
        read_entry(state_fs, (uint8_t *)&state, sizeof(State), lba - bd::LBA_START, 1, 0, (uint8_t)State::ERROR_SKIP);
        if(state == State::ERROR_SKIP && !options.force_split)
            throw_line("read errors detected, unable to continue");

        bd::DataFrame df;
        read_entry(sbram_fs, (uint8_t *)&df, sizeof(df), lba - bd::LBA_START, 1, 0, 0);

        std::span<uint8_t> data(df.main_data, FORM1_DATA_SIZE);

        bool valid = df.valid(lba);
        df.descramble(lba);

        if(!valid)
        {
            if(!options.leave_unchanged)
                std::fill(data.begin(), data.end(), 0);

            if(invalid_data_frames.empty() || invalid_data_frames.back().second + 1 != lba)
                invalid_data_frames.emplace_back(lba, lba);
            else
                invalid_data_frames.back().second = lba;
        }

        iso_fs.write((char *)&data[0], data.size());
        if(iso_fs.fail())
            throw_line("write failed ({})", iso_path.filename().string());
    }

    progress(sectors_count, sectors_count);
    LOG("");
    LOG("");

    for(auto const &d : invalid_data_frames)
    {
        if(d.first == d.second)
            LOG("warning: descramble failed (LBA: {})", d.first);
        else
            LOG("warning: descramble failed (LBA: [{} .. {}])", d.first, d.second);
    }
}


export void redumper_split_dvd(Context &ctx, Options &options)
{
    // generate .dmi, .pfi, .ss if xbox disc
    generate_extra_xbox(ctx, options);

    // prevent hash generation for dumps with scsi errors
    if(ctx.dump_errors && ctx.dump_errors->scsi && !options.force_split)
        throw_line("{} scsi errors detected, unable to continue", ctx.dump_errors->scsi);

    // descramble and extract user data from raw DVD/BD dumps
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    if(std::filesystem::path sdram_path(image_prefix + ".sdram"); std::filesystem::exists(sdram_path))
        dvd_extract_iso(ctx, sdram_path, options);
    else if(std::filesystem::path sbram_path(image_prefix + ".sbram"); std::filesystem::exists(sbram_path))
        bd_extract_iso(ctx, sbram_path, options);
}

}
