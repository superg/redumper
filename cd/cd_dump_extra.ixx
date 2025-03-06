module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
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
import utils.logger;



namespace gpsxre
{
/*
bool plextor_assign_leadin_session(std::vector<LeadInEntry> &entries, std::vector<uint8_t> leadin, int32_t drive_pregap_start)
{
    bool session_found = false;

    uint32_t entries_count = (uint32_t)leadin.size() / PLEXTOR_LEADIN_ENTRY_SIZE;
    for(uint32_t j = entries_count; j > 0; --j)
    {
        auto entry = &leadin[(j - 1) * PLEXTOR_LEADIN_ENTRY_SIZE];
        auto status = *(SPTD::Status *)entry;

        if(status.status_code)
            continue;

        auto sub_data = entry + sizeof(SPTD::Status) + CD_DATA_SIZE;

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

        if(Q.isValid())
        {
            if(Q.adr == 1 && Q.mode1.tno)
            {
                int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
                for(uint32_t s = 0; s < (uint32_t)entries.size(); ++s)
                {
                    int32_t pregap_end = entries[s].lba_start + (drive_pregap_start - MSF_LBA_SHIFT);
                    if(lba >= entries[s].lba_start && lba < pregap_end)
                    {
                        uint32_t trim_count = j - 1 + pregap_end - lba;

                        if(trim_count > entries_count)
                        {
                            LOG("PLEXTOR: lead-in incomplete (session: {})", s + 1);
                        }
                        else
                        {
                            LOG("PLEXTOR: lead-in found (session: {}, sectors: {})", s + 1, trim_count);

                            if(trim_count < entries_count)
                                leadin.resize(trim_count * PLEXTOR_LEADIN_ENTRY_SIZE);

                            // initial add
                            if(entries[s].data.empty())
                                entries[s].data = leadin;
                            else
                            {
                                auto size = plextor_leadin_compare(entries[s].data, leadin);

                                // match
                                if(size == std::min(entries[s].data.size(), leadin.size()))
                                {
                                    // prefer smaller size
                                    if(leadin.size() < entries[s].data.size())
                                        entries[s].data.swap(leadin);

                                    entries[s].verified = true;
                                }
                                // mismatch, prefer newest for the next comparison
                                else
                                    entries[s].data.swap(leadin);
                            }
                        }

                        session_found = true;
                        break;
                    }
                }

                if(session_found)
                    break;
            }
        }
    }

    return session_found;
}
*/

/*
void plextor_store_sessions_leadin(std::fstream &fs_scm, std::fstream &fs_sub, std::fstream &fs_state, SPTD &sptd, const std::vector<int32_t> &session_lba_start, const DriveConfig &drive_config,
    const Options &options)
{
    std::vector<LeadInEntry> entries;
    for(auto s : session_lba_start)
        entries.push_back({ std::vector<uint8_t>(), s, false });

    if(entries.empty())
        return;

    uint32_t retries = options.plextor_leadin_retries + entries.size();
    for(uint32_t i = 0; i < retries && !entries.front().verified; ++i)
    {
        LOG("PLEXTOR: reading lead-in (retry: {})", i + 1);

        // this helps with getting more consistent sectors count for the first session
        if(i == entries.size() - 1)
            cmd_read(sptd, nullptr, 0, -1, 0, true);

        auto leadin = plextor_read_leadin(sptd, drive_config.pregap_start - MSF_LBA_SHIFT);

        if(!plextor_assign_leadin_session(entries, leadin, drive_config.pregap_start))
            LOG("PLEXTOR: lead-in session not identified");
    }

    // store
    for(uint32_t s = 0; s < entries.size(); ++s)
    {
        auto &leadin = entries[s].data;

        if(!leadin.empty())
        {
            // don't store unverified lead-in for the first session, it's always wrong
            if(s == 0 && !entries[s].verified)
            {
                leadin.clear();
                LOG("PLEXTOR: lead-in discarded as unverified (session: {})", s + 1);
            }
            else
                LOG("PLEXTOR: storing lead-in (session: {}, verified: {})", s + 1, entries[s].verified ? "yes" : "no");
        }

        for(uint32_t i = 0, n = (uint32_t)leadin.size() / PLEXTOR_LEADIN_ENTRY_SIZE; i < n; ++i)
        {
            int32_t lba = entries[s].lba_start + (drive_config.pregap_start - MSF_LBA_SHIFT) - (n - i);
            int32_t lba_index = lba - LBA_START;

            uint8_t *entry = &leadin[i * PLEXTOR_LEADIN_ENTRY_SIZE];
            auto status = *(SPTD::Status *)entry;

            if(status.status_code)
            {
                if(options.verbose)
                    LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
            }
            else
            {
                // data
                std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
                read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
                for(auto const &s : sector_state)
                {
                    // new data is improved
                    if(s < State::SUCCESS_C2_OFF)
                    {
                        uint8_t *sector_data = entry + sizeof(SPTD::Status);
                        std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS_C2_OFF);

                        write_entry(fs_scm, sector_data, CD_DATA_SIZE, lba_index, 1, drive_config.read_offset * CD_SAMPLE_SIZE);
                        write_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, drive_config.read_offset);

                        break;
                    }
                }

                // subcode
                std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);
                read_entry(fs_sub, (uint8_t *)sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
                ChannelQ Q_file;
                subcode_extract_channel((uint8_t *)&Q_file, sector_subcode_file.data(), Subchannel::Q);
                if(!Q_file.isValid())
                {
                    uint8_t *sector_subcode = entry + sizeof(SPTD::Status) + CD_DATA_SIZE;
                    write_entry(fs_sub, sector_subcode, CD_SUBCODE_SIZE, lba_index, 1, 0);
                }
            }
        }
    }
}
*/

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


std::vector<std::pair<SPTD::Status, std::vector<uint8_t>>> plextor_read_leadin(SPTD &sptd, uint32_t tail_size)
{
    std::vector<std::pair<SPTD::Status, std::vector<uint8_t>>> sectors;

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
            std::span<const uint8_t> sector_subcode(sector_buffer.begin() + CD_DATA_SIZE, CD_SUBCODE_SIZE);

            ChannelQ Q;
            subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);

            // DEBUG
            Logger::get().carriageReturn();
            LOGC("{}", Q.Decode());

            if(Q.isValid() && Q.adr == 1 && Q.mode1.tno && neg_end == neg_limit)
                neg_end = neg + tail_size;
        }

        sectors.emplace_back(status, sector_buffer);
    }

    LOGC_RF("");

    return sectors;
}


void plextor_process_leadin(Context &ctx, const TOC &toc, std::fstream &fs_scram, std::fstream &fs_state, std::fstream &fs_subcode, Options &options)
{


    uint32_t retries = options.plextor_leadin_retries + toc.sessions.size();
    for(uint32_t i = 0; i < retries; ++i)
    {
        LOG("PLEXTOR: reading lead-in (retry: {})", i + 1);

        // this helps with getting more consistent sectors count for the first session
        if(i + 1 == toc.sessions.size())
            cmd_read(*ctx.sptd, nullptr, 0, -1, 0, true);

        auto sectors = plextor_read_leadin(*ctx.sptd, 3 * MSF_LIMIT.f);

        //DEBUG
        LOG("");
        //        if(!plextor_assign_leadin_session(entries, leadin, drive_config.pregap_start))
//            LOG("PLEXTOR: lead-in session not identified");
    }

    /*
    bool read = !refine;

    std::vector<int32_t> session_lba_start;
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        int32_t lba_start = i ? toc.sessions[i].tracks.front().indices.front() : 0;
        session_lba_start.push_back(lba_start + MSF_LBA_SHIFT);

        // check gaps in all sessions
        read = read || refine_needed(fs_state, lba_start + MSF_LBA_SHIFT, lba_start + ctx.drive_config.pregap_start, ctx.drive_config.read_offset);
    }

    if(read)
        plextor_store_sessions_leadin(fs_scm, fs_sub, fs_state, *ctx.sptd, session_lba_start, ctx.drive_config, options);
*/
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
