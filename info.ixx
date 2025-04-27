module;

#include <filesystem>
#include <fstream>
#include <list>
#include <sstream>
#include <string>
#include <utility>
#include "systems/system.hh"
#include "throw_line.hh"

export module info;

import cd.cdrom;
import cd.common;
import common;
import filesystem.iso9660;
import options;
import readers.image_bin_form1_reader;
import readers.image_iso_form1_reader;
import readers.image_raw_reader;
import readers.sector_reader;
import systems.systems;
import utils.hex_bin;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export int redumper_info(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::list<std::pair<std::filesystem::path, TrackType>> tracks;
    if(std::filesystem::exists(image_prefix + ".cue"))
    {
        for(auto const &t : cue_get_entries(image_prefix + ".cue"))
            tracks.emplace_back(std::filesystem::path(options.image_path) / t.first, t.second);
    }
    else if(std::filesystem::exists(image_prefix + ".iso"))
    {
        tracks.emplace_back(image_prefix + ".iso", TrackType::MODE1_2048);
    }
    else
        throw_line("image file not found");

    bool separate_nl = false;
    for(auto const &t : tracks)
    {
        std::shared_ptr<SectorReader> raw_reader;
        std::shared_ptr<SectorReader> form1_reader;

        if(track_type_is_data_iso(t.second))
            form1_reader = std::make_shared<Image_ISO_Reader>(t.first);
        else if(track_type_is_data_raw(t.second))
        {
            raw_reader = std::make_shared<Image_RawReader>(t.first);
            form1_reader = std::make_shared<Image_BIN_Form1Reader>(t.first);
        }

        for(auto const &s : Systems::get())
        {
            auto system = s();

            auto reader = system->getType() == System::Type::ISO ? form1_reader : raw_reader;
            if(!reader)
                continue;

            std::stringstream ss;
            system->printInfo(ss, reader.get(), t.first);

            if(ss.rdbuf()->in_avail())
            {
                if(separate_nl)
                    LOG("");
                separate_nl = true;

                LOG("{} [{}]:", system->getName(), t.first.filename().string());
                LOG_F("{}", ss.str());
            }
        }
    }

    return exit_code;
}

}
