module;
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.protection;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import filesystem.iso9660;
import options;
import readers.image_scram_reader;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

std::string detect_datel(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, uint32_t sectors_count, Options &options)
{
    std::string protection;

    // single session with one track
    if(toc.sessions.size() != 1 || toc.sessions.front().tracks.size() != 2)
        return protection;

    // track is data track
    if(!(toc.sessions.front().tracks[0].control & (uint8_t)ChannelQ::Control::DATA))
        return protection;

    int32_t track1_start = toc.sessions.front().tracks[0].lba_start;
    int32_t track1_end = std::min(toc.sessions.front().tracks[1].lba_start, (int32_t)sectors_count + LBA_START);

    auto write_offset = track_offset_by_sync(track1_start, track1_end, fs_state, fs_scram);
    if(!write_offset)
        return protection;

    constexpr uint32_t first_file_lba = 23;

    std::string protected_filename;
    {
        uint32_t file_offset = (track1_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
        auto form1_reader = std::make_unique<Image_SCRAM_Reader>(fs_scram, file_offset, track1_end - track1_start);

        iso9660::PrimaryVolumeDescriptor pvd;
        if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
        {
            auto root_directory = iso9660::Browser::rootDirectory(form1_reader.get(), pvd);

            static const std::string datel_files[] = { "DATA.DAT", "BIG.DAT", "DUMMY.ZIP" };
            for(auto const &f : datel_files)
            {
                // protection file exists
                auto entry = root_directory->subEntry(f);
                if(!entry)
                    continue;

                // first file on disc and starts from LBA 23
                if(entry->sectorsLBA() == first_file_lba)
                {
                    protected_filename = entry->name();
                    break;
                }
            }
        }
    }

    if(!protected_filename.empty())
    {
        std::pair<int32_t, int32_t> range(0, 0);
        for(int32_t lba = first_file_lba, lba_end = std::min(track1_end, 5000); lba < lba_end; ++lba)
        {
            std::vector<State> state(CD_DATA_SIZE_SAMPLES);
            read_entry(fs_state, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -*write_offset, (uint8_t)State::ERROR_SKIP);

            if(std::any_of(state.begin(), state.end(), [](State s) { return s == State::ERROR_C2; }))
            {
                if(!range.first)
                    range.first = lba;
                range.second = lba + 1;
            }
            else
            {
                if(range.first)
                    break;
            }
        }

        if(range.second > range.first)
        {
            protection = std::format("PS2/Datel {}, C2: {}, range: {}-{}", protected_filename, range.second - range.first, range.first, range.second - 1);

            ctx.protection.emplace_back(lba_to_sample(range.first, *write_offset), lba_to_sample(range.second, *write_offset));
        }
    }

    return protection;
}


std::string detect_safedisc(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, uint32_t sectors_count, Options &options)
{
    std::string protection;

    // single session with at least one track
    if(toc.sessions.size() != 1 || toc.sessions.front().tracks.size() < 2)
        return protection;

    // first track is data
    if(!(toc.sessions.front().tracks[0].control & (uint8_t)ChannelQ::Control::DATA))
        return protection;

    int32_t track1_start = toc.sessions.front().tracks[0].lba_start;
    int32_t track1_end = std::min(toc.sessions.front().tracks[1].lba_start, (int32_t)sectors_count + LBA_START);

    auto write_offset = track_offset_by_sync(track1_start, track1_end, fs_state, fs_scram);
    if(!write_offset)
        return protection;

    uint32_t file_offset = (track1_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
    auto form1_reader = std::make_unique<Image_SCRAM_Reader>(fs_scram, file_offset, track1_end - track1_start);

    iso9660::PrimaryVolumeDescriptor pvd;
    if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
    {
        auto root_directory = iso9660::Browser::rootDirectory(form1_reader.get(), pvd);

        auto entry = root_directory->subEntry("00000001.TMP");

        bool safedisc_lite = false;
        if(!entry)
        {
            entry = root_directory->subEntry("00000001.LT1");
            safedisc_lite = true;
        }

        if(entry)
        {
            int32_t lba_start = entry->sectorsLBA() + entry->sectorsSize();

            // limit error search to the max gap size if next file is not found
            int32_t lba_end = lba_start + (safedisc_lite ? 1500 : 10000);

            auto entries = root_directory->entries();
            for(auto &e : entries)
            {
                if(e->isDirectory())
                    continue;

                auto entry_offset = e->sectorsLBA();
                if(entry_offset <= lba_start)
                    continue;

                if(entry_offset < lba_end)
                    lba_end = entry_offset;
            }

            std::vector<int32_t> errors;

            for(int32_t lba = lba_start; lba < lba_end; ++lba)
            {
                std::vector<State> state(CD_DATA_SIZE_SAMPLES);
                read_entry(fs_state, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -*write_offset, (uint8_t)State::ERROR_SKIP);

                if(std::any_of(state.begin(), state.end(), [](State s) { return s == State::ERROR_C2; }))
                    errors.push_back(lba);
            }

            if(!errors.empty())
            {
                protection = std::format("SafeDisc {}{}, C2: {}, gap range: {}-{}", safedisc_lite ? "Lite " : "", entry->name(), errors.size(), lba_start, lba_end - 1);

                for(auto e : errors)
                    ctx.protection.emplace_back(lba_to_sample(e, *write_offset), lba_to_sample(e + 1, *write_offset));
            }
        }
    }

    return protection;
}


std::string detect_datel_faketoc(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, uint32_t sectors_count, Options &options)
{
    std::string protection;

    // single session with one track
    if(toc.sessions.size() != 1 || toc.sessions.front().tracks.size() != 2)
        return protection;

    // track is data
    if(!(toc.sessions.front().tracks[0].control & (uint8_t)ChannelQ::Control::DATA))
        return protection;

    int32_t track1_start = toc.sessions.front().tracks[0].lba_start;
    int32_t track1_end = std::min(toc.sessions.front().tracks[1].lba_start, (int32_t)sectors_count + LBA_START);

    auto write_offset = track_offset_by_sync(track1_start, track1_end, fs_state, fs_scram);
    if(!write_offset)
        return protection;

    uint32_t file_offset = (track1_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
    auto form1_reader = std::make_unique<Image_SCRAM_Reader>(fs_scram, file_offset, track1_end - track1_start);

    int32_t leadout_lba = toc.sessions.front().tracks.back().lba_start;

    iso9660::PrimaryVolumeDescriptor pvd;
    if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
    {
        // most (or all) Datel PS2 discs are FakeTOC, usually it's not a problem and we just get a track that is padded with zeroed lead-out sectors in the end
        if(pvd.volume_space_size.lsb != leadout_lba)
        {
            protection = std::format("PS2/Datel FakeTOC, lead-out: {}, ISO9660 size: {}", leadout_lba, pvd.volume_space_size.lsb);

            // however sometimes, late sectors are unreadable due to being on the disc edge
            // common lead-out values that may require trimming: 305571, 323849, 368849, 449849

            // trim track to ISO9660 size for the lead-out values that guarantee the errors
            const std::set<int32_t> FAKE_LEADOUT_LBA{ 368849, 449849 };
            if(FAKE_LEADOUT_LBA.find(leadout_lba) != FAKE_LEADOUT_LBA.end())
                ctx.protection_trim = true;

            // other values may require a manually specified option if there are errors in the end
        }
    }

    return protection;
}


std::string detect_breakerpro_faketoc(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, uint32_t sectors_count, Options &options)
{
    std::string protection;

    // single session with one track
    if(toc.sessions.size() != 1 || toc.sessions.front().tracks.size() != 2)
        return protection;

    // track is data
    if(!(toc.sessions.front().tracks[0].control & (uint8_t)ChannelQ::Control::DATA))
        return protection;

    int32_t track1_start = toc.sessions.front().tracks[0].lba_start;
    int32_t track1_end = std::min(toc.sessions.front().tracks[1].lba_start, (int32_t)sectors_count + LBA_START);

    auto write_offset = track_offset_by_sync(track1_start, track1_end, fs_state, fs_scram);
    if(!write_offset)
        return protection;

    uint32_t file_offset = (track1_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
    auto form1_reader = std::make_unique<Image_SCRAM_Reader>(fs_scram, file_offset, track1_end - track1_start);

    int32_t leadout_lba = toc.sessions.front().tracks.back().lba_start;

    iso9660::PrimaryVolumeDescriptor pvd;
    if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
    {
        auto volume_identifier = iso9660::identifier_to_string(pvd.volume_identifier);
        if(volume_identifier == "BREAKER PRO" && leadout_lba == 449849)
        {
            protection = std::format("PSX/BreakerPro FakeTOC, lead-out: {}, ISO9660 size: {}", leadout_lba, pvd.volume_space_size.lsb);

            ctx.protection_trim = true;
        }
    }

    return protection;
}


export int redumper_protection(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

    if(std::filesystem::exists(image_prefix + ".iso") || std::filesystem::exists(image_prefix + ".sdram"))
        return exit_code;

    std::vector<uint8_t> toc_buffer = read_vector(image_prefix + ".toc");
    std::vector<uint8_t> full_toc_buffer;
    if(std::filesystem::exists(image_prefix + ".fulltoc"))
        full_toc_buffer = read_vector(image_prefix + ".fulltoc");

    auto toc = toc_choose(toc_buffer, full_toc_buffer);

    std::fstream fs_scram(image_prefix + ".scram", std::fstream::in | std::fstream::binary);
    if(!fs_scram.is_open())
        throw_line("unable to open file ({})", image_prefix + ".scram");

    std::fstream fs_state(image_prefix + ".state", std::fstream::in | std::fstream::binary);
    if(!fs_state.is_open())
        throw_line("unable to open file ({})", image_prefix + ".state");

    auto sectors_count = (uint32_t)(std::filesystem::file_size(image_prefix + ".state") / CD_DATA_SIZE_SAMPLES);

    std::vector<std::string> protections;

    auto datel = detect_datel(ctx, toc, fs_scram, fs_state, sectors_count, options);
    // check other protections
    if(datel.empty())
    {
        auto breakerpro_faketoc = detect_breakerpro_faketoc(ctx, toc, fs_scram, fs_state, sectors_count, options);
        if(!breakerpro_faketoc.empty())
            protections.push_back(breakerpro_faketoc);

        auto safedisc = detect_safedisc(ctx, toc, fs_scram, fs_state, sectors_count, options);
        if(!safedisc.empty())
            protections.push_back(safedisc);
    }
    // Datel detected
    else
    {
        protections.push_back(datel);

        auto datel_faketoc = detect_datel_faketoc(ctx, toc, fs_scram, fs_state, sectors_count, options);
        if(!datel_faketoc.empty())
            protections.push_back(datel_faketoc);
    }

    if(protections.size() <= 1)
        LOG("protection: {}", protections.empty() ? "none" : protections.front());
    else
    {
        LOG("protections: ");
        for(auto const &p : protections)
            LOG("  {}", p);
    }

    return exit_code;
}

}
