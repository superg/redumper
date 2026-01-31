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
            LOG("warning: file already exists ({}.dmi)", options.image_name);
        }
        else
        {
            auto manufacturer = read_vector(manufacturer_path);
            if(!manufacturer.empty() && manufacturer.size() == FORM1_DATA_SIZE + sizeof(CMD_ParameterListHeader))
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
            if(!physical.empty() && physical.size() == FORM1_DATA_SIZE + sizeof(CMD_ParameterListHeader))
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


void descramble(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path sdram_path(image_prefix + ".sdram");
    std::filesystem::path iso_path(image_prefix + ".iso");
    if(!std::filesystem::exists(sdram_path))
        return;

    std::ifstream sdram_fs(sdram_path, std::ofstream::binary);
    std::ofstream iso_fs(iso_path, std::ofstream::binary);

    bool nintendo = false;
    std::optional<std::uint8_t> key = std::nullopt;
    std::filesystem::path physical_path(image_prefix + ".physical");
    if(!nintendo && std::filesystem::exists(physical_path))
    {
        auto physical = read_vector(physical_path);
        if(!physical.empty() && physical.size() == FORM1_DATA_SIZE + 4 && physical[sizeof(CMD_ParameterListHeader)] == 0xFF)
            nintendo = true;
    }

    DVD_Scrambler scrambler;
    bool success;
    std::streamsize bytesRead;
    uint32_t main_data_offset = offsetof(DataFrame, main_data);
    std::vector<uint8_t> sector(sizeof(DataFrame));

    // TODO: read/descramble lead-in sectors, write to separate file
    // pressed nintendo discs have no key set during lead-in/lead-out
    sdram_fs.seekg(-DVD_LBA_START * sizeof(DataFrame));
    uint32_t psn = -DVD_LBA_START;

    if(nintendo)
    {
        // nintendo discs have user data at CPR_MAI position
        main_data_offset = offsetof(DataFrame, cpr_mai);
        sdram_fs.read((char *)sector.data(), sector.size());
        bytesRead = sdram_fs.gcount();
        if(bytesRead != sector.size())
            return;
        success = scrambler.descramble(sector.data(), psn, 0);
        if(!success)
            LOG("warning: descramble failed (LBA: {})", psn + DVD_LBA_START);
        iso_fs.write((char *)(sector.data() + main_data_offset), FORM1_DATA_SIZE);
        auto sum = std::accumulate(sector.begin() + 6, sector.begin() + 14, 0);
        key = ((sum >> 4) + sum) & 0xF;
    }

    // TODO: detect lead-out sectors, write to separate file
    while(true)
    {
        sdram_fs.read((char *)sector.data(), sector.size());
        bytesRead = sdram_fs.gcount();
        if(bytesRead != sector.size())
            return;
        psn = ((uint32_t)sector[1] << 16) | ((uint32_t)sector[2] << 8) | ((uint32_t)sector[3]);
        // nintendo discs first ECC block has key (psn >> 4 & 0xF)
        if(nintendo && psn + DVD_LBA_START < ECC_FRAMES)
            success = scrambler.descramble(sector.data(), psn, psn >> 4 & 0xF);
        else
            success = scrambler.descramble(sector.data(), psn, key);
        if(!success)
            LOG("warning: descramble failed (LBA: {})", psn + DVD_LBA_START);
        iso_fs.write((char *)(sector.data() + main_data_offset), FORM1_DATA_SIZE);
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
    descramble(ctx, options);
}

}
