module;
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.split;

import dump;
import options;
import rom_entry;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.xbox;



namespace gpsxre
{


void generate_extra_xbox(Options &options)
{
    image_check_empty(options);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    // do not attempt to generate .ss, .dmi or .pfi for non-xbox discs (dumps without .security)
    std::filesystem::path security_path(image_prefix + ".security");
    if(std::filesystem::exists(security_path))
    {
        // trim the 4 byte header from .manufacturer and write it to a .dmi (if it doesn't exist)
        std::filesystem::path manufacturer_path(image_prefix + ".manufacturer");
        std::filesystem::path dmi_path(image_prefix + ".dmi");
        if(std::filesystem::exists(manufacturer_path))
        {
            if (std::filesystem::exists(dmi_path) && !options.overwrite)
            {
                LOG("warning: file already exists ({}.dmi)", options.image_name);
            }
            else
            {
                auto manufacturer = read_vector(manufacturer_path);
                if(!manufacturer.empty() && manufacturer.size() == 2052)
                {
                    manufacturer.erase(manufacturer.begin(), manufacturer.begin() + 4);
                    write_vector(dmi_path, manufacturer);

                    ROMEntry dmi_rom_entry(dmi_path.filename().string());
                    dmi_rom_entry.update(manufacturer.data(), FORM1_DATA_SIZE);
                    if (!ctx.dat.has_value())
                        ctx.dat = std::vector<std::string>();
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
            if (std::filesystem::exists(pfi_path) && !options.overwrite)
            {
                LOG("warning: file already exists ({}.pfi)", options.image_name);
            }
            else
            {
                auto physical = read_vector(physical_path);
                if(!physical.empty() && physical.size() == 2052)
                {
                    physical.erase(physical.begin(), physical.begin() + 4);
                    write_vector(pfi_path, physical);

                    ROMEntry pfi_rom_entry(pfi_path.filename().string());
                    pfi_rom_entry.update(physical.data(), FORM1_DATA_SIZE);
                    if (!ctx.dat.has_value())
                        ctx.dat = std::vector<std::string>();
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
        if(!std::filesystem::exists(ss_path) && !options.overwrite)
        {
            LOG("warning: file already exists ({}.ss)", options.image_name);
        }
        else
        {
            auto security = read_vector(security_path);
            if(!security.empty() && security.size() == 2048)
            {
                clean_xbox_security_sector(security);
                write_vector(ss_path, security);

                ROMEntry ss_rom_entry(ss_path.filename().string());
                ss_rom_entry.update(security.data(), FORM1_DATA_SIZE);
                if (!ctx.dat.has_value())
                    ctx.dat = std::vector<std::string>();
                ctx.dat->push_back(ss_rom_entry.xmlLine());

                LOG("security sector ranges:");
                auto security_ranges = get_security_sector_ranges((READ_DVD_STRUCTURE_LayerDescriptor &)security[0]);
                for(const auto &range : security_ranges)
                {
                    LOG("  {}-{}", range.first, range.second);
                }
            }
            else
            {
                LOG("warning: could not generate SS, unexpected file size ({})", security_path.filename().string());
            }
        }
    }
}


export void redumper_split_dvd(Context &ctx, Options &options)
{
    //image_check_empty(options);
    //auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    //std::filesystem::path iso_path(image_prefix + ".iso");
    //std::filesystem::path state_path(image_prefix + ".state");

    // generate .dmi, .pfi, .ss if requested
    if(options.generate_extra_xbox)
        generate_extra_xbox(options);

    // prevent hash generation for ISO with scsi errors
    if(ctx.dump_errors->scsi && !options.force_split)
        throw_line("{} scsi errors detected, unable to continue (use --force-split to ignore)", ctx.dump_errors->scsi);
}

}
