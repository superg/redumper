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
            if(!manufacturer.empty() && manufacturer.size() == FORM1_DATA_SIZE + 4)
            {
                manufacturer.erase(manufacturer.begin(), manufacturer.begin() + 4);
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
            if(!physical.empty() && physical.size() == FORM1_DATA_SIZE + 4)
            {
                physical.erase(physical.begin(), physical.begin() + 4);
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


void descramble_nintendo(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path scram_path(image_prefix + ".iso");
    if(!std::filesystem::exists(scram_path))
        return;

    std::ifstream ifs(scram_path, std::ofstream::binary);
    std::ofstream ofs(image_prefix + ".dec.iso", std::ofstream::binary);
    DVD_Scrambler scrambler;
    std::vector<uint8_t> sector(DATA_FRAME_SIZE);
    ifs.read((char *)sector.data(), sector.size());
    // pressed discs have no key set during lead-in/lead-out
    int32_t psn = -DVD_LBA_START;
    bool success = scrambler.descramble(sector.data(), psn, 0);
    auto bytesRead = ifs.gcount();
    auto sum = std::accumulate(sector.begin() + 6, sector.begin() + 14, 0);
    uint8_t key = ((sum >> 4) ^ sum) & 0xF;
    while(bytesRead == sector.size())
    {
        ofs.write((char *)(sector.data() + 6), FORM1_DATA_SIZE);
        ifs.read((char *)sector.data(), sector.size());
        psn += 1;
        // first ECC block has key (psn >> 4 & 0xF)
        if(psn - DVD_LBA_START < ECC_FRAMES)
            success &= scrambler.descramble(sector.data(), psn, psn >> 4 & 0xF);
        else
            success &= scrambler.descramble(sector.data(), psn, key);
        bytesRead = ifs.gcount();
    }
    if(!success)
        LOG("warning: some/all sectors failed to descrambled");
}


export void redumper_split_dvd(Context &ctx, Options &options)
{
    // generate .dmi, .pfi, .ss if xbox disc
    generate_extra_xbox(ctx, options);

    // prevent hash generation for ISO with scsi errors
    if(ctx.dump_errors && ctx.dump_errors->scsi && !options.force_split)
        throw_line("{} scsi errors detected, unable to continue", ctx.dump_errors->scsi);

    // descramble and extract user data from raw nintendo dumps
    if(ctx.nintendo)
        descramble_nintendo(ctx, options);
}

}
