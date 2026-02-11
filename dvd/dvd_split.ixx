module;
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.split;

import cd.cdrom;
import common;
import dvd;
import dvd.xbox;
import dvd.scrambler;
import options;
import range;
import rom_entry;
import scsi.cmd;
import scsi.mmc;
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
            LOG("warning: file already exists ({}.pfi)", options.image_name);
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
        LOG("warning: file already exists ({}.ss)", options.image_name);
    }
    else
    {
        auto security = read_vector(security_path);
        if(!security.empty() && security.size() == FORM1_DATA_SIZE)
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


void extract_iso(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path sdram_path(image_prefix + ".sdram");
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path iso_path(image_prefix + ".iso");
    if(!std::filesystem::exists(sdram_path))
        return;
    if(std::filesystem::exists(iso_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({}.iso)", options.image_name);
        return;
    }

    std::fstream sdram_fs(sdram_path, std::fstream::in | std::fstream::binary);
    if(!sdram_fs.is_open())
        throw_line("unable to open file ({})", sdram_path.filename().string());
    uint64_t sdram_size = std::filesystem::file_size(sdram_path);
    if(sdram_size % sizeof(RecordingFrame) != 0)
        throw_line("unexpected file size ({})", sdram_path.filename().string());

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path.filename().string());

    std::fstream iso_fs(iso_path, std::fstream::out | std::fstream::binary);
    if(!iso_fs.is_open())
        throw_line("unable to open file ({})", iso_path.filename().string());

    DVD_Scrambler scrambler;
    std::streamsize bytes_read;
    std::vector<uint8_t> rf(sizeof(RecordingFrame));
    State state = State::ERROR_SKIP;
    std::optional<std::uint8_t> key = std::nullopt;

    // start extracting ISO from LBA 0
    uint32_t psn = -DVD_LBA_START;
    sdram_fs.seekg(psn * sizeof(RecordingFrame));
    if(sdram_fs.fail())
        throw_line("seek failed");

    for(uint32_t sector_count = sdram_size / sizeof(RecordingFrame); psn < sector_count; ++psn)
    {
        read_entry(sdram_fs, rf.data(), rf.size(), psn, 1, 0, 0);
        read_entry(state_fs, (uint8_t *)&state, sizeof(State), psn, 1, 0, (uint8_t)State::ERROR_SKIP);
        if(state == State::ERROR_SKIP && !options.force_split)
            throw_line("read errors detected, unable to continue");
        auto df = RecordingFrame_to_DataFrame((RecordingFrame &)rf[0]);
        if(df.id.zone_type != ZoneType::DATA_ZONE)
            break;
        if(!scrambler.descramble((uint8_t *)&df, psn, key))
            LOG("warning: descramble failed (LBA: {})", psn + DVD_LBA_START);
        iso_fs.write((char *)((uint8_t *)&df + offsetof(DataFrame, main_data)), FORM1_DATA_SIZE);
    }
}


export void redumper_split_dvd(Context &ctx, Options &options)
{
    // generate .dmi, .pfi, .ss if xbox disc
    generate_extra_xbox(ctx, options);

    // prevent hash generation for ISO with scsi errors
    if(ctx.dump_errors && ctx.dump_errors->scsi && !options.force_split)
        throw_line("{} scsi errors detected, unable to continue", ctx.dump_errors->scsi);

    // descramble and extract user data from raw DVD dumps
    extract_iso(ctx, options);
}

}
