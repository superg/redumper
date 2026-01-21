module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.dump_extra;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import drive;
import drive.mediatek;
import drive.plextor;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

constexpr uint32_t MEDIATEK_LEADOUT_DISCARD_COUNT = 2;


void store_data(std::fstream &fs_scram, std::fstream &fs_state, std::span<const uint8_t> data_buffer, std::span<const State> state_buffer, int32_t sample)
{
    uint32_t sample_index = sample_offset_r2a(sample);
    uint32_t samples_count = state_buffer.size();

    if(samples_count * CD_SAMPLE_SIZE != data_buffer.size())
        throw_line("data / state buffer size mismatch (data size: {}, state size: {})", samples_count, data_buffer.size());

    std::vector<State> state_buffer_file(samples_count);
    std::vector<uint8_t> data_buffer_file(samples_count * CD_SAMPLE_SIZE);
    read_entry(fs_state, (uint8_t *)state_buffer_file.data(), sizeof(State), sample_index, samples_count, 0, 0);
    read_entry(fs_scram, data_buffer_file.data(), CD_SAMPLE_SIZE, sample_index, samples_count, 0, 0);

    bool write = false;
    for(uint32_t i = 0; i < samples_count; ++i)
    {
        if(state_buffer_file[i] < state_buffer[i])
        {
            state_buffer_file[i] = state_buffer[i];
            ((uint32_t *)data_buffer_file.data())[i] = ((uint32_t *)data_buffer.data())[i];

            write = true;
        }
    }

    if(write)
    {
        write_entry(fs_state, (uint8_t *)state_buffer_file.data(), sizeof(State), sample_index, samples_count, 0);
        write_entry(fs_scram, data_buffer_file.data(), CD_SAMPLE_SIZE, sample_index, samples_count, 0);
    }
}


void store_subcode(std::fstream &fs_subcode, std::span<const uint8_t> subcode_buffer, int32_t lba)
{
    if(subcode_buffer.size() % CD_SUBCODE_SIZE)
        throw_line("unaligned subcode buffer size (size: {})", subcode_buffer.size());

    uint32_t sectors_count = subcode_buffer.size() / CD_SUBCODE_SIZE;

    std::vector<uint8_t> subcode_buffer_file(sectors_count * CD_SUBCODE_SIZE);
    read_entry(fs_subcode, subcode_buffer_file.data(), CD_SUBCODE_SIZE, lba - LBA_START, sectors_count, 0, 0);

    bool write = false;
    for(uint32_t i = 0; i < sectors_count; ++i)
    {
        std::span<const uint8_t> sector_subcode(&subcode_buffer[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);
        std::span<uint8_t> sector_subcode_file(&subcode_buffer_file[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);

        ChannelQ Q = subcode_extract_q(sector_subcode.data());
        if(Q.isValid())
        {
            ChannelQ Q_file = subcode_extract_q(sector_subcode_file.data());

            if(!Q_file.isValid())
            {
                std::copy(sector_subcode.begin(), sector_subcode.end(), sector_subcode_file.begin());

                write = true;
            }
        }
    }

    if(write)
        write_entry(fs_subcode, subcode_buffer_file.data(), CD_SUBCODE_SIZE, lba - LBA_START, sectors_count, 0);
}


void plextor_process_leadin(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, std::fstream &fs_subcode, Options &options)
{
    std::vector<std::pair<PlextorLeadIn, uint32_t>> leadins;

    constexpr uint32_t leadin_overlap = 16;

    uint32_t retries = options.plextor_leadin_retries + toc.sessions.size();
    for(uint32_t i = 0; i < retries; ++i)
    {
        LOG("PLEXTOR: reading lead-in (retry: {})", i + 1);

        // this helps with getting more consistent sectors count for the first session
        // TODO: need to understand the firmware
        if(i + 1 == toc.sessions.size())
            cmd_read(*ctx.sptd, nullptr, 0, -1, 0, true);

        auto leadin = plextor_leadin_read(*ctx.sptd, CD_PREGAP_SIZE + leadin_overlap);
        auto leadin_range = plextor_leadin_identify(leadin);
        if(leadin_range)
            LOG("PLEXTOR: lead-in found (LBA: [{} .. {}], sectors: {})", leadin_range->first, leadin_range->second, leadin.size());
        else
        {
            LOG("PLEXTOR: lead-in not identified");
            continue;
        }

        bool found = false;
        for(auto &l : leadins)
        {
            auto range = plextor_leadin_identify(l.first);
            if(!range)
                throw_line("unexpected");

            // overlap check
            if(leadin_range->first <= range->second && range->first <= leadin_range->second)
            {
                found = true;

                // match
                if(plextor_leadin_match(l.first, leadin))
                {
                    // prefer the shorter one as it's fully verified
                    if(leadin.size() < l.first.size())
                        l.first.swap(leadin);

                    ++l.second;
                }
                // mismatch, prefer newest for the next comparison
                else
                    l.first.swap(leadin);

                break;
            }
        }

        if(!found)
            leadins.emplace_back(leadin, 0);

        // exit criteria: 1st session lead-in is verified
        std::map<int32_t, uint32_t> verified;
        for(auto &l : leadins)
        {
            auto range = plextor_leadin_identify(l.first);
            if(!range)
                throw_line("unexpected");

            verified[range->first] = l.second;
        }
        if(auto it = verified.begin(); it != verified.end() && it->second)
            break;
    }

    for(auto const &l : leadins)
    {
        auto range = plextor_leadin_identify(l.first);
        if(!range)
            throw_line("unexpected");

        if(!l.second && !options.plextor_leadin_force_store)
        {
            LOG("PLEXTOR: lead-in discarded as unverified (LBA: [{} .. {}])", range->first, range->second);
            continue;
        }

        LOG("PLEXTOR: storing lead-in (LBA: [{} .. {}], verified: {})", range->first, range->second, l.second ? "yes" : "no");

        auto &leadin = l.first;
        std::vector<State> state_buffer(leadin.size() * CD_DATA_SIZE_SAMPLES);
        std::vector<uint8_t> data_buffer(leadin.size() * CD_DATA_SIZE);
        std::vector<uint8_t> subcode_buffer(leadin.size() * CD_SUBCODE_SIZE);

        for(uint32_t i = 0; i < leadin.size(); ++i)
        {
            if(!leadin[i].first.status_code)
            {
                std::span<State> sector_state(&state_buffer[i * CD_DATA_SIZE_SAMPLES], CD_DATA_SIZE_SAMPLES);
                std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS_C2_OFF);
            }

            std::span<uint8_t> sector_data(&data_buffer[i * CD_DATA_SIZE], CD_DATA_SIZE);
            std::span<uint8_t> sector_subcode(&subcode_buffer[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);
            std::span<const uint8_t> sector_data_leadin(&leadin[i].second[0], CD_DATA_SIZE);
            std::span<const uint8_t> sector_subcode_leadin(&leadin[i].second[CD_DATA_SIZE], CD_SUBCODE_SIZE);
            std::copy(sector_data_leadin.begin(), sector_data_leadin.end(), sector_data.begin());
            std::copy(sector_subcode_leadin.begin(), sector_subcode_leadin.end(), sector_subcode.begin());
        }

        int32_t lba = plextor_leadin_find_start(leadin, fs_subcode, leadin_overlap);

        store_data(fs_scram, fs_state, data_buffer, state_buffer, lba_to_sample(lba, -ctx.drive_config.read_offset));
        store_subcode(fs_subcode, subcode_buffer, lba);
    }
}


void mediatek_process_leadout(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, std::fstream &fs_subcode, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

    for(auto const &s : toc.sessions)
    {
        int32_t lba = s.tracks.back().lba_start - 1;

        std::vector<uint8_t> cache;
        for(uint32_t i = 0; i < options.mediatek_leadout_retries; ++i)
        {
            // dummy read to cache lead-out
            std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
            bool all_types = false;
            SPTD::Status status = read_sector_new(*ctx.sptd, sector_buffer.data(), all_types, ctx.drive_config, lba);
            if(status.status_code && options.verbose)
                LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));

            status = mediatek_cache_read(*ctx.sptd, cache, 1024 * 1024 * mediatek_get_config(ctx.drive_config.type).size_mb);
            if(status.status_code)
                throw_line("read cache failed, SCSI ({})", SPTD::StatusMessage(status));

            uint32_t sectors_count = (uint32_t)mediatek_cache_extract(cache, lba, LEADOUT_OVERREAD_COUNT, ctx.drive_config.type).size() / CD_RAW_DATA_SIZE;

            LOG_R("MEDIATEK: preloading cache (LBA: {:6}, sectors: {:3}, retry: {})", lba, sectors_count, i + 1);
            if(sectors_count == LEADOUT_OVERREAD_COUNT)
                break;
        }

        // dump full cache to file
        std::string session_message;
        if(toc.sessions.size() > 1)
            session_message = std::format(".{}", s.session_number);
        write_vector(std::format("{}{}.cache", image_prefix, session_message), cache);

        auto leadout = mediatek_cache_extract(cache, lba, LEADOUT_OVERREAD_COUNT, ctx.drive_config.type);

        uint32_t sectors_count = (uint32_t)leadout.size() / CD_RAW_DATA_SIZE;

        // discard couple last sectors as there is a chance that they are incomplete
        sectors_count = sectors_count >= MEDIATEK_LEADOUT_DISCARD_COUNT ? sectors_count - MEDIATEK_LEADOUT_DISCARD_COUNT : 0;

        if(sectors_count)
            LOG("MEDIATEK: storing lead-out (LBA: {:6}, sectors: {})", lba, sectors_count);
        else
            LOG("MEDIATEK: lead-out not found");

        std::vector<State> state_buffer(sectors_count * CD_DATA_SIZE_SAMPLES);
        std::vector<uint8_t> data_buffer(sectors_count * CD_DATA_SIZE);
        std::vector<uint8_t> subcode_buffer(sectors_count * CD_SUBCODE_SIZE);

        std::vector<uint8_t> sector_c2_leadout_backup(CD_C2_SIZE);
        for(uint32_t i = 0; i < sectors_count; ++i)
        {
            std::span<const uint8_t> sector_leadout(&leadout[CD_RAW_DATA_SIZE * i], CD_RAW_DATA_SIZE);
            std::span<const uint8_t> sector_data_leadout(&sector_leadout[0], CD_DATA_SIZE);
            std::span<const uint8_t> sector_c2_leadout(&sector_leadout[CD_DATA_SIZE], CD_C2_SIZE);
            std::span<const uint8_t> sector_subcode_leadout(&sector_leadout[CD_DATA_SIZE + CD_C2_SIZE], CD_SUBCODE_SIZE);

            std::span<State> sector_state(&state_buffer[i * CD_DATA_SIZE_SAMPLES], CD_DATA_SIZE_SAMPLES);
            std::span<uint8_t> sector_data(&data_buffer[i * CD_DATA_SIZE], CD_DATA_SIZE);
            std::span<uint8_t> sector_subcode(&subcode_buffer[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);

            auto sector_state_leadout = c2_to_state(sector_c2_leadout.data(), State::SUCCESS_SCSI_OFF);
            std::copy(sector_state_leadout.begin(), sector_state_leadout.end(), sector_state.begin());
            std::copy(sector_data_leadout.begin(), sector_data_leadout.end(), sector_data.begin());
            std::copy(sector_subcode_leadout.begin(), sector_subcode_leadout.end(), sector_subcode.begin());

            uint32_t c2_bits = c2_bits_count(sector_c2_leadout);
            if(c2_bits && options.verbose)
            {
                std::string difference_message;
                if(i)
                {
                    bool c2_match = std::equal(sector_c2_leadout.begin(), sector_c2_leadout.end(), sector_c2_leadout_backup.begin());
                    difference_message = std::format(", difference: {}", c2_match ? "-" : "+");
                }

                sector_c2_leadout_backup.assign(sector_c2_leadout.begin(), sector_c2_leadout.end());

                LOG_R("[LBA: {:6}] C2 error (bits: {:4}{})", lba + i, c2_bits, difference_message);
            }
        }

        store_data(fs_scram, fs_state, data_buffer, state_buffer, lba_to_sample(lba, -ctx.drive_config.read_offset));
        store_subcode(fs_subcode, subcode_buffer, lba);
    }
}


export int redumper_dump_extra(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    auto toc = toc_process(ctx, options, false);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    auto mode = std::fstream::out | std::fstream::binary | std::fstream::in;
    std::fstream fs_scram(image_prefix + ".scram", mode);
    std::fstream fs_state(image_prefix + ".state", mode);
    std::fstream fs_subcode(image_prefix + ".subcode", mode);

    if(ctx.drive_config.type == Type::PLEXTOR)
    {
        if(!options.plextor_skip_leadin)
            plextor_process_leadin(ctx, toc, fs_scram, fs_state, fs_subcode, options);
    }
    else if(drive_is_mediatek(ctx.drive_config))
    {
        if(!options.mediatek_skip_leadout && !ctx.drive_config.omnidrive)
            mediatek_process_leadout(ctx, toc, fs_scram, fs_state, fs_subcode, options);
    }

    return exit_code;
}

}
