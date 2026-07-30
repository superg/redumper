module;
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <vector>
#include "throw_line.hh"

export module hash;

import cd.cd;
import cd.common;
import common;
import options;
import rom_entry;
import utils.animation;
import utils.logger;
import utils.misc;



namespace gpsxre
{

ROMEntry calculate_file_hash(const std::filesystem::path &f)
{
    ROMEntry rom_entry(f.filename().string());

    std::fstream fs(f, std::fstream::in | std::fstream::binary);
    if(!fs.is_open())
        throw_line("unable to open file ({})", f.filename().string());

    std::vector<uint8_t> data(CHUNK_1MB); // 1Mb chunk
    batch_process_range<uint64_t>(std::pair(0, std::filesystem::file_size(f)), data.size(),
        [&](uint64_t offset, uint64_t size) -> bool
        {
            fs.read((char *)data.data(), size);
            if(fs.fail())
                throw_line("read failed ({})", f.filename().string());

            rom_entry.update(data.data(), size);

            return false;
        });

    return rom_entry;
}


void progress_output(uint64_t byte, uint64_t bytes_count)
{
    char animation = byte == bytes_count ? '*' : spinner_animation();

    LOGC_RF("{} [{:3}%] hashing", animation, byte * 100 / bytes_count);
}


export int redumper_hash(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    if(ctx.dat.empty())
    {
        std::vector<std::filesystem::path> files;
        if(std::filesystem::exists(image_prefix + ".cue"))
        {
            for(auto const &t : cue_get_entries(image_prefix + ".cue"))
                files.push_back(std::filesystem::path(options.image_path) / t.first);
        }
        else if(std::filesystem::exists(image_prefix + ".iso"))
        {
            files.push_back(image_prefix + ".iso");
        }
        else
            throw_line("image file not found");

        if(!files.empty())
        {
            uint64_t bytes = 0;
            uint64_t bytes_count = 0;
            for(auto f : files)
                bytes_count += std::filesystem::file_size(f);

            for(auto f : files)
            {
                progress_output(bytes, bytes_count);

                auto rom_entry = calculate_file_hash(f);
                bytes += rom_entry.getSize();

                ctx.dat.push_back(rom_entry.xmlLine());
            }

            progress_output(bytes_count, bytes_count);
            LOG("");
            LOG("");
        }
    }

    if(ctx.dat_extra.empty())
    {
        std::list<std::filesystem::path> files;

        // hash CD extras
        if(std::filesystem::exists(image_prefix + ".cue"))
        {
            auto image_path = std::filesystem::path(options.image_path);
            if(image_path.empty())
                image_path = ".";

            auto cue_entries = cue_get_entries(image_prefix + ".cue");
            auto track_prefix = std::filesystem::path(image_prefix).filename().string() + " (Track ";

            for(auto const &entry : std::filesystem::directory_iterator(image_path))
            {
                auto filename = entry.path().filename().string();
                if(!filename.starts_with(track_prefix) || !filename.ends_with(").bin"))
                    continue;

                if(std::none_of(cue_entries.begin(), cue_entries.end(), [&](auto const &t) { return std::filesystem::path(t.first) == filename; }))
                    files.push_back(entry.path());
            }
        }
        // hash xbox extras
        else if(std::filesystem::exists(image_prefix + ".iso"))
        {
            if(std::filesystem::exists(image_prefix + ".dmi"))
                files.push_back(image_prefix + ".dmi");
            if(std::filesystem::exists(image_prefix + ".pfi"))
                files.push_back(image_prefix + ".pfi");
            if(std::filesystem::exists(image_prefix + ".ss"))
                files.push_back(image_prefix + ".ss");
        }

        files.sort();
        for(auto f : files)
            ctx.dat_extra.push_back(calculate_file_hash(f).xmlLine());
    }

    if(!ctx.dat.empty())
    {
        LOG("dat:");
        for(auto l : ctx.dat)
            LOG("{}", l);
    }

    if(!ctx.dat_extra.empty())
    {
        LOG("dat (extra):");
        for(auto l : ctx.dat_extra)
            LOG("{}", l);
    }

    return exit_code;
}

}
