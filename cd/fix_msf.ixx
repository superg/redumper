module;

#include <filesystem>
#include <fstream>
#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include "systems/system.hh"
#include "throw_line.hh"

export module cd.fix_msf;

import cd.cd;
import cd.cdrom;
import dump;
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

enum class TrackType
{
    DATA,
    AUDIO,
    ISO
};

export void redumper_fix_msf(Context &ctx, Options &options)
{
    image_check_empty(options);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::list<std::filesystem::path> tracks;
    if(std::filesystem::exists(image_prefix + ".cue"))
        for(auto const &t : cue_get_entries(image_prefix + ".cue"))
            if(t.second)
                tracks.push_back(std::filesystem::path(options.image_path) / t.first);

    if(tracks.empty())
        throw_line("no files to process");

    uint32_t msf_errors = 0;

    for(auto const &t : tracks)
    {
        uint32_t sectors_count = std::filesystem::file_size(t) / sizeof(Sector);
        std::fstream fs(t, std::fstream::binary | std::fstream::in | std::fstream::out);

        std::optional<int32_t> lba_base;

        for(uint32_t s = 0; s < sectors_count; ++s)
        {
            Sector sector;
            fs.read((char *)&sector, sizeof(sector));
            if(fs.fail())
                throw_line("read failed");

            if(memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
                continue;

            if(sector.header.mode != 2)
            {
                LOG("warning: unsupported track mode, skipping (track: {}, mode: {})", t.generic_string(), sector.header.mode);
                break;
            }

            int32_t lba = BCDMSF_to_LBA(sector.header.address);
            if(lba_base)
            {
                int32_t lba_expected = *lba_base + s;
                if(lba != lba_expected)
                {
                    sector.header.address = LBA_to_BCDMSF(lba_expected);
                    fs.seekp(s * sizeof(Sector));
                    if(fs.fail())
                        throw_line("read failed");
                    fs.write((char *)&sector, sizeof(sector));
                    if(fs.fail())
                        throw_line("write failed");
                    fs << std::flush;

                    ++msf_errors;
                }
            }
            else
                lba_base = lba;
        }
    }

    LOG("corrected {} sectors", msf_errors);
}

}
