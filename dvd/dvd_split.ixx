module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.split;

import bd;
import bd.scrambler;
import cd.cdrom;
import common;
import dvd;
import dvd.nintendo;
import dvd.scrambler;
import dvd.xbox;
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


void dvd_extract_iso(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path sdram_path(image_prefix + ".sdram");
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path iso_path(image_prefix + ".iso");
    std::filesystem::path physical_path(image_prefix + ".physical");
    if(!std::filesystem::exists(sdram_path))
        return;
    if(std::filesystem::exists(iso_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({})", iso_path.filename().string());
        return;
    }

    uint64_t sdram_size = std::filesystem::file_size(sdram_path);
    if(sdram_size % sizeof(RecordingFrame) != 0)
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

    dvd::Scrambler scrambler;
    std::vector<uint8_t> rf(sizeof(RecordingFrame));
    std::optional<std::uint8_t> key;
    std::vector<std::pair<int32_t, int32_t>> descramble_errors;
    uint32_t main_data_offset = offsetof(DataFrame, main_data);

    // start extracting ISO from LBA 0
    sdram_fs.seekg(-DVD_LBA_START * sizeof(RecordingFrame));
    if(sdram_fs.fail())
        throw_line("seek failed");

    bool nintendo = false;
    uint8_t nintendo_key;
    if(std::filesystem::exists(physical_path))
    {
        auto physical = read_vector(physical_path);
        if(physical.size() == FORM1_DATA_SIZE + sizeof(CMD_ParameterListHeader) && physical[sizeof(CMD_ParameterListHeader)] == 0xFF)
            nintendo = true;
    }

    if(nintendo)
    {
        key = 0;
        main_data_offset = offsetof(DataFrame, cpr_mai);
    }

    uint32_t sector_count = sdram_size / sizeof(RecordingFrame) + DVD_LBA_START;
    for(uint32_t lba = 0; lba < sector_count; ++lba)
    {
        read_entry(sdram_fs, rf.data(), rf.size(), lba - DVD_LBA_START, 1, 0, 0);
        State state;
        read_entry(state_fs, (uint8_t *)&state, sizeof(State), lba - DVD_LBA_START, 1, 0, (uint8_t)State::ERROR_SKIP);
        if(state == State::ERROR_SKIP && !options.force_split)
            throw_line("read errors detected, unable to continue");
        auto df = RecordingFrame_to_DataFrame((RecordingFrame &)rf[0]);
        if(df.id.id.zone_type == ZoneType::LEADOUT_ZONE)
            break;

        if(!scrambler.descramble(df, key))
        {
            if(descramble_errors.empty() || descramble_errors.back().second + 1 != lba)
                descramble_errors.emplace_back(lba, lba);
            else
                descramble_errors.back().second = lba;
        }

        iso_fs.write((char *)&df + main_data_offset, FORM1_DATA_SIZE);
        if(iso_fs.fail())
            throw_line("write failed ({})", iso_path.filename().string());

        if(nintendo)
        {
            if(lba == 0)
                nintendo_key = nintendo::derive_key(std::span(df.cpr_mai, df.cpr_mai + 8));
            else if(lba == DVD_ECC_FRAMES - 1)
                key = nintendo_key;
        }
    }

    for(auto const &d : descramble_errors)
    {
        if(d.first == d.second)
            LOG("warning: descramble failed (LBA: {})", d.first);
        else
            LOG("warning: descramble failed (LBA: [{} .. {}])", d.first, d.second);
    }
}


void bd_extract_iso(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path sbram_path(image_prefix + ".sbram");
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path iso_path(image_prefix + ".iso");
    std::filesystem::path physical_path(image_prefix + ".physical");
    if(!std::filesystem::exists(sbram_path))
        return;
    if(std::filesystem::exists(iso_path) && !options.overwrite)
    {
        LOG("warning: file already exists ({})", iso_path.filename().string());
        return;
    }

    uint64_t sbram_size = std::filesystem::file_size(sbram_path);
    if(sbram_size % sizeof(BlurayDataFrame) != 0)
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

    std::vector<uint8_t> sector(sizeof(BlurayDataFrame));
    std::vector<std::pair<int32_t, int32_t>> descramble_errors;

    // start extracting ISO from LBA 0
    sbram_fs.seekg(-BD_LBA_START * sizeof(BlurayDataFrame));
    if(sbram_fs.fail())
        throw_line("seek failed");

    uint32_t sector_count = sbram_size / sizeof(BlurayDataFrame) + BD_LBA_START;
    for(uint32_t lba = 0; lba < sector_count; ++lba)
    {
        read_entry(sbram_fs, sector.data(), sector.size(), lba - BD_LBA_START, 1, 0, 0);
        State state;
        read_entry(state_fs, (uint8_t *)&state, sizeof(State), lba - BD_LBA_START, 1, 0, (uint8_t)State::ERROR_SKIP);
        if(state == State::ERROR_SKIP && !options.force_split)
            throw_line("read errors detected, unable to continue");
        auto bdf = (BlurayDataFrame &)sector[0];

        if(!bd::descramble(bdf, lba - BD_LBA_START))
        {
            if(descramble_errors.empty() || descramble_errors.back().second + 1 != lba)
                descramble_errors.emplace_back(lba, lba);
            else
                descramble_errors.back().second = lba;
        }

        iso_fs.write((char *)&bdf, FORM1_DATA_SIZE);
        if(iso_fs.fail())
            throw_line("write failed ({})", iso_path.filename().string());
    }

    for(auto const &d : descramble_errors)
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

    // descramble and extract user data from raw BD/DVD dumps
    bd_extract_iso(ctx, options);
    dvd_extract_iso(ctx, options);
}

}
