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
import cd.subcode;
import cd.toc;
import drive;
import dump;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

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

    std::vector<uint8_t> subcode_buffer_file(subcode_buffer.size());
    read_entry(fs_subcode, subcode_buffer_file.data(), CD_SUBCODE_SIZE, lba - LBA_START, sectors_count, 0, 0);

    bool write = false;
    for(uint32_t i = 0; i < sectors_count; ++i)
    {
        std::span<const uint8_t> sector_subcode(&subcode_buffer[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);
        std::span<uint8_t> sector_subcode_file(&subcode_buffer_file[i * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
        if(Q.isValid())
        {
            ChannelQ Q_file;
            subcode_extract_channel((uint8_t *)&Q_file, sector_subcode_file.data(), Subchannel::Q);

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


// Plextor firmware blocked LBA ranges:
// BE [-inf .. -20000], (-1000 .. -75)
// D8 [-inf .. -20150], (-1150 .. -75)
//
// BE is having trouble reading LBA close to -1150].
// It is possible to read large negative offset ranges (starting from 0x80000000 - smallest negative 32-bit integer) using BE command with disabled C2 if the disc first track is a data track.
// D8 range boundaries are shifted left by 150 sectors comparing to BE.
//
// Regardless of the read command and a starting point, negative range reads are virtualized by Plextor drive and will always start somewhere in the lead-in TOC area and sequentially read until
// eventually the drive will wrap around and start in the lead-in TOC area again. This process will continue until drive reaches firmware blocked LBA ranges specified here. However, any external
// pause between sequential reads (debugger pause, sleep() call etc.) will lead to another wrap around.
// For multisession CD, there is no direct control which session lead-in will be chosen. Usually it's the first session, sometimes it's the latter ones.
//
// The following range, while preserving the above behavior, is unlocked for both BE and D8 commands with disabled C2:
const std::pair<int32_t, int32_t> PLEXTOR_TOC_RANGE = { -20150, -1150 };


using PlextorLeadIn = std::vector<std::pair<SPTD::Status, std::vector<uint8_t>>>;


PlextorLeadIn plextor_leadin_read(SPTD &sptd, uint32_t tail_size)
{
    PlextorLeadIn sectors;

    int32_t neg_start = PLEXTOR_TOC_RANGE.first + 1;
    int32_t neg_limit = PLEXTOR_TOC_RANGE.second + 1;
    int32_t neg_end = neg_limit;

    for(int32_t neg = neg_start; neg < neg_end; ++neg)
    {
        LOGC_RF("{} [LBA: {:6}]", spinner_animation(), neg);

        std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_SUBCODE_SIZE);
        SPTD::Status status = cmd_read_cdda(sptd, sector_buffer.data(), neg, 1, READ_CDDA_SubCode::DATA_SUB);

        if(!status.status_code)
        {
            std::span<const uint8_t> sector_subcode(&sector_buffer[CD_DATA_SIZE], CD_SUBCODE_SIZE);

            ChannelQ Q;
            subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);

            // DEBUG
            // Logger::get().carriageReturn();
            // LOGC("{}", Q.Decode());

            if(Q.isValid() && Q.adr == 1 && Q.mode1.tno && neg_end == neg_limit)
                neg_end = neg + tail_size;
        }

        sectors.emplace_back(status, sector_buffer);
    }

    LOGC_RF("");

    return sectors;
}


std::optional<std::pair<int32_t, int32_t>> plextor_leadin_identify(const PlextorLeadIn &leadin)
{
    std::optional<std::pair<int32_t, int32_t>> lba_range;

    for(auto const &s : leadin)
    {
        if(s.first.status_code)
            continue;

        std::span<const uint8_t> sector_subcode(&s.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
        if(Q.isValid() && Q.adr == 1 && Q.mode1.tno)
        {
            int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
            if(lba_range)
                lba_range->second = lba;
            else
                lba_range.emplace(lba, lba);
        }
    }

    return lba_range;
}


bool plextor_leadin_match(const PlextorLeadIn &leadin1, const PlextorLeadIn &leadin2)
{
    bool match = true;

    auto sectors_count = (uint32_t)std::min(leadin1.size(), leadin2.size());
    for(uint32_t i = 1; i <= sectors_count; ++i)
    {
        auto const &s1 = leadin1[leadin1.size() - i];
        auto const &s2 = leadin2[leadin2.size() - i];
        if(s1.first.status_code || s2.first.status_code)
            continue;

        std::span<const uint8_t> sector1_data(&s1.second[0], CD_DATA_SIZE);
        std::span<const uint8_t> sector1_subcode(&s1.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);
        std::span<const uint8_t> sector2_data(&s2.second[0], CD_DATA_SIZE);
        std::span<const uint8_t> sector2_subcode(&s2.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);

        // data check
        if(!std::equal(sector1_data.begin(), sector1_data.end(), sector2_data.begin()))
        {
            match = false;
            break;
        }

        ChannelQ Q1;
        subcode_extract_channel((uint8_t *)&Q1, sector1_subcode.data(), Subchannel::Q);
        ChannelQ Q2;
        subcode_extract_channel((uint8_t *)&Q2, sector2_subcode.data(), Subchannel::Q);
        if(!Q1.isValid() || !Q2.isValid())
            continue;

        // subcode check
        if(!std::equal(Q1.raw, Q1.raw + sizeof(Q1.raw), Q2.raw))
        {
            match = false;
            break;
        }
    }

    return match;
}


void plextor_process_leadin(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, std::fstream &fs_subcode, Options &options)
{
    std::vector<std::pair<PlextorLeadIn, uint32_t>> leadins;

    uint32_t retries = options.plextor_leadin_retries + toc.sessions.size();
    for(uint32_t i = 0; i < retries; ++i)
    {
        LOG("PLEXTOR: reading lead-in (retry: {})", i + 1);

        // this helps with getting more consistent sectors count for the first session
        // TODO: need to understand the firmware
        if(i + 1 == toc.sessions.size())
            cmd_read(*ctx.sptd, nullptr, 0, -1, 0, true);

        auto leadin = plextor_leadin_read(*ctx.sptd, 3 * MSF_LIMIT.f);
        auto leadin_range = plextor_leadin_identify(leadin);
        if(leadin_range)
            LOG("PLEXTOR: lead-in found (LBA: [{:6} .. {:6}], sectors: {})", leadin_range->first, leadin_range->second, leadin.size());
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
            LOG("PLEXTOR: lead-in discarded as unverified (LBA: [{:6} .. {:6}])", range->first, range->second);
            continue;
        }

        LOG("PLEXTOR: storing lead-in (LBA: [{:6} .. {:6}], verified: {})", range->first, range->second, l.second ? "yes" : "no");

        int32_t lba = 0;

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

            ChannelQ Q;
            subcode_extract_channel((uint8_t *)&Q, sector_subcode_leadin.data(), Subchannel::Q);
            if(Q.isValid() && Q.adr == 1 && Q.mode1.tno)
            {
                lba = BCDMSF_to_LBA(Q.mode1.a_msf) - i;
            }
        }

        store_data(fs_scram, fs_state, data_buffer, state_buffer, lba_to_sample(lba, -ctx.drive_config.read_offset));
        store_subcode(fs_subcode, subcode_buffer, lba);
    }
}


void asus_process_leadout(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, std::fstream &fs_subcode, Options &options)
{
    ;
}


export void redumper_dump_extra(Context &ctx, Options &options)
{
    image_check_empty(options);

    auto toc = toc_process(ctx, options, false);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    auto mode = std::fstream::out | std::fstream::binary | std::fstream::in;
    std::fstream fs_scram(image_prefix + ".scram", mode);
    std::fstream fs_state(image_prefix + ".state", mode);
    std::fstream fs_subcode(image_prefix + ".subcode", mode);

    if(ctx.drive_config.type == DriveConfig::Type::PLEXTOR)
    {
        if(!options.plextor_skip_leadin)
            plextor_process_leadin(ctx, toc, fs_scram, fs_state, fs_subcode, options);
    }
    else if(drive_is_asus(ctx.drive_config))
    {
        if(!options.asus_skip_leadout)
            asus_process_leadout(ctx, toc, fs_scram, fs_state, fs_subcode, options);
    }
}

}
