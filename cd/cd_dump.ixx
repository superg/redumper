module;
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <set>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.dump;

import cd.cd;
import cd.scrambler;
import cd.split;
import cd.subcode;
import cd.toc;
import crc.crc32;
import drive;
import dump;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.signal;
import utils.strings;



namespace gpsxre
{

struct LeadInEntry
{
    std::vector<uint8_t> data;
    int32_t lba_start;
    bool verified;
};

constexpr uint32_t SLOW_SECTOR_TIMEOUT = 5;


uint32_t plextor_leadin_compare(const std::vector<uint8_t> &leadin1, const std::vector<uint8_t> &leadin2)
{
    uint32_t count = 0;

    uint32_t leadin1_count = (uint32_t)leadin1.size() / PLEXTOR_LEADIN_ENTRY_SIZE;
    uint32_t leadin2_count = (uint32_t)leadin2.size() / PLEXTOR_LEADIN_ENTRY_SIZE;

    uint32_t shared_count = std::min(leadin1_count, leadin2_count);
    for(; count < shared_count; ++count)
    {
        auto sector1 = &leadin1[(leadin1_count - 1 - count) * PLEXTOR_LEADIN_ENTRY_SIZE];
        auto sector2 = &leadin2[(leadin2_count - 1 - count) * PLEXTOR_LEADIN_ENTRY_SIZE];

        if(memcmp(sector1 + sizeof(SPTD::Status), sector2 + sizeof(SPTD::Status), CD_DATA_SIZE))
            break;
    }

    return count * PLEXTOR_LEADIN_ENTRY_SIZE;
}


bool refine_needed(std::fstream &fs_state, int32_t lba_start, int32_t lba_end, int32_t read_offset)
{
    std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);

    for(int32_t lba = lba_start; lba < lba_end; ++lba)
    {
        read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, read_offset, (uint8_t)State::ERROR_SKIP);
        for(auto const &ss : sector_state)
            if(ss == State::ERROR_SKIP || ss == State::ERROR_C2)
                return true;
    }

    return false;
}


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


bool is_data_track(int32_t lba, const TOC &toc)
{
    bool data_track = false;

    for(auto const &s : toc.sessions)
        for(auto const &t : s.tracks)
            if(lba >= t.lba_start && lba < t.lba_end)
            {
                data_track = t.control & (uint8_t)ChannelQ::Control::DATA;
                break;
            }

    return data_track;
}


uint32_t state_from_c2(std::vector<State> &state, const uint8_t *c2_data)
{
    uint32_t c2_count = 0;

    // group 4 C2 consecutive errors into 1 state, this way it aligns to the drive offset
    // and covers the case where for 1 C2 bit there are 2 damaged sector bytes (scrambled data bytes, usually)
    for(uint32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
    {
        uint8_t c2_quad = c2_data[i / 2];
        if(i % 2)
            c2_quad &= 0x0F;
        else
            c2_quad >>= 4;

        if(c2_quad)
        {
            state[i] = State::ERROR_C2;
            c2_count += std::popcount(c2_quad);
        }
    }

    return c2_count;
}


uint32_t percentage(int32_t value, uint32_t value_max)
{
    if(value < 0)
        return 0;
    else if(!value_max || (uint32_t)value >= value_max)
        return 100;
    else
        return value * 100 / value_max;
}


export bool redumper_dump_cd(Context &ctx, const Options &options, bool refine)
{
    image_check_empty(options);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    // don't use .replace_extension() as it messes up paths with dot
    std::filesystem::path scm_path(image_prefix + ".scram");
    std::filesystem::path scp_path(image_prefix + ".scrap");
    std::filesystem::path sub_path(image_prefix + ".subcode");
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path toc_path(image_prefix + ".toc");
    std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
    std::filesystem::path cdtext_path(image_prefix + ".cdtext");
    std::filesystem::path asus_path(image_prefix + ".asus");

    if(!refine)
        image_check_overwrite(options);

    std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip); // FIXME: transition to samples?
    std::vector<std::pair<int32_t, int32_t>> error_ranges;

    int32_t lba_start = ctx.drive_config.pregap_start;
    int32_t lba_end = MSF_to_LBA(MSF{ 74, 0, 0 }); // default: 74min / 650Mb

    std::vector<uint8_t> toc_buffer = cmd_read_toc(*ctx.sptd);
    std::vector<uint8_t> full_toc_buffer = cmd_read_full_toc(*ctx.sptd);
    auto toc = choose_toc(toc_buffer, full_toc_buffer);

    if(!refine)
    {
        LOG("disc TOC:");
        print_toc(toc);
        LOG("");
    }

    auto layout = sector_order_layout(ctx.drive_config.sector_order);
    bool subcode = layout.subcode_offset != CD_RAW_DATA_SIZE;

    // BE read mode
    bool scrap = false;
    if(ctx.drive_config.read_method == DriveConfig::ReadMethod::BE)
    {
        bool data_tracks = false;
        bool audio_tracks = false;
        for(auto &s : toc.sessions)
        {
            for(auto &t : s.tracks)
            {
                if(t.control & (uint8_t)ChannelQ::Control::DATA)
                    data_tracks = true;
                else
                    audio_tracks = true;
            }
        }

        if(data_tracks)
        {
            // by default don't allow BE mode for mixed data/audio discs
            // can be overriden with specifying any drive type in the options
            if(!options.drive_type && audio_tracks)
            {
                print_supported_drives();
                throw_line("unsupported drive read method for mixed data/audio");
            }

            LOG("warning: unsupported drive read method");

            scrap = true;
        }
    }

    if(refine && (std::filesystem::exists(scm_path) && scrap || std::filesystem::exists(scp_path) && !scrap))
        throw_line("refine using mixed read methods is unsupported");

    if(!refine && !options.image_path.empty())
        std::filesystem::create_directories(options.image_path);

    // delete remnants of other dump mode
    if(std::filesystem::exists(scrap ? scm_path : scp_path))
        std::filesystem::remove(scrap ? scm_path : scp_path);

    std::fstream fs_scm(scrap ? scp_path : scm_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);
    std::fstream fs_sub;
    if(subcode)
        fs_sub.open(sub_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);
    std::fstream fs_state(state_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);

    // fake TOC
    // [PSX] Breaker Pro
    if(toc.sessions.back().tracks.back().lba_end < 0)
        LOG("warning: fake TOC detected, using default 74min disc size");
    // last session last track end
    else
        lba_end = toc.sessions.back().tracks.back().lba_end;

    // multisession gaps
    for(uint32_t i = 1; i < toc.sessions.size(); ++i)
        error_ranges.emplace_back(toc.sessions[i - 1].tracks.back().lba_end, toc.sessions[i].tracks.front().indices.front() + ctx.drive_config.pregap_start);

    // compare disc / file TOC to make sure it's the same disc
    if(refine)
    {
        if(!options.force_refine)
        {
            std::vector<uint8_t> toc_buffer_file = read_vector(toc_path);
            if(toc_buffer != toc_buffer_file)
                throw_line("disc / file TOC don't match, refining from a different disc?");
        }
    }
    // store TOC
    else
    {
        write_vector(toc_path, toc_buffer);
        if(!full_toc_buffer.empty())
            write_vector(fulltoc_path, full_toc_buffer);

        // CD-TEXT
        std::vector<uint8_t> cd_text_buffer;
        if(toc_enable_cdtext(ctx, toc, options))
        {
            auto status = cmd_read_cd_text(*ctx.sptd, cd_text_buffer);
            if(status.status_code)
                LOG("warning: unable to read CD-TEXT, SCSI ({})", SPTD::StatusMessage(status));
        }
        else
            LOG("warning: CD-TEXT disabled");

        if(!cd_text_buffer.empty())
            write_vector(cdtext_path, cd_text_buffer);
    }

    // read lead-in early as it improves the chance of extracting both sessions at once
    if(ctx.drive_config.type == DriveConfig::Type::PLEXTOR && !options.plextor_skip_leadin)
    {
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
    }

    // override using options
    if(options.lba_start)
        lba_start = *options.lba_start;
    if(options.lba_end)
        lba_end = *options.lba_end;

    uint32_t errors_scsi = 0;
    uint32_t errors_c2 = 0;
    uint32_t errors_q = 0;

    // buffers
    std::vector<uint8_t> sector_data(CD_DATA_SIZE);
    std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);
    std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);

    int32_t subcode_shift = 0;

    // drive specific
    std::vector<uint8_t> asus_leadout_buffer;

    int32_t lba_refine = LBA_START - 1;
    uint32_t refine_counter = 0;
    uint32_t refine_processed = 0;
    uint32_t refine_count = 0;
    uint32_t refine_retries = options.retries ? options.retries : 1;

    if(refine)
    {
        for(int32_t lba = lba_start; lba < lba_end; ++lba)
        {
            int32_t lba_index = lba - LBA_START;

            if(inside_range(lba, skip_ranges) != nullptr || inside_range(lba, error_ranges) != nullptr)
                continue;

            bool refine_sector = false;

            bool scsi_exists = false;
            bool c2_exists = false;
            read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
            for(auto const &ss : sector_state)
            {
                if(ss == State::ERROR_SKIP)
                {
                    scsi_exists = true;
                    break;
                }
                else if(ss == State::ERROR_C2)
                    c2_exists = true;
            }

            if(scsi_exists)
            {
                ++errors_scsi;
                refine_sector = true;
            }
            else if(c2_exists)
            {
                ++errors_c2;
                refine_sector = true;
            }

            if(subcode)
            {
                read_entry(fs_sub, (uint8_t *)sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
                ChannelQ Q;
                subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
                if(!Q.isValid())
                {
                    ++errors_q;
                    if(options.refine_subchannel)
                        refine_sector = true;
                }
            }

            if(refine_sector)
                ++refine_count;
        }
    }

    uint32_t errors_q_last = errors_q;

    SignalINT signal;

    int32_t lba_next = 0;
    int32_t lba_overread = lba_end;
    for(int32_t lba = lba_start; lba < lba_overread; lba = lba_next)
    {
        if(auto r = inside_range(lba, skip_ranges); r != nullptr)
        {
            lba_next = r->second;
            continue;
        }
        else
            lba_next = lba + 1;

        int32_t lba_index = lba - LBA_START;

        bool read = true;
        bool flush = false;
        bool store = false;

        // mirror lead-out
        if(drive_is_asus(ctx.drive_config) && !options.asus_skip_leadout)
        {
            // initial cache read
            auto r = inside_range(lba, error_ranges);
            if(r != nullptr && lba == r->first || lba == lba_end)
            {
                // dummy read to cache lead-out
                if(refine)
                {
                    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
                    read_sector(*ctx.sptd, sector_buffer.data(), ctx.drive_config, lba - 1);
                }

                LOG_R("LG/ASUS: searching lead-out in cache (LBA: {:6})", lba);
                {
                    auto cache = asus_cache_read(*ctx.sptd, ctx.drive_config.type);
                    write_vector(asus_path, cache);

                    asus_leadout_buffer = asus_cache_extract(cache, lba, 100, ctx.drive_config.type);
                }

                uint32_t entries_count = (uint32_t)asus_leadout_buffer.size() / CD_RAW_DATA_SIZE;

                if(entries_count)
                    LOG_R("LG/ASUS: lead-out found (LBA: {:6}, sectors: {})", lba, entries_count);
                else
                    LOG_R("LG/ASUS: lead-out not found");
            }

            if(r != nullptr && lba >= r->first || lba >= lba_end)
            {
                uint32_t leadout_index = lba - (r == nullptr ? lba_end : r->first);
                if(leadout_index < asus_leadout_buffer.size() / CD_RAW_DATA_SIZE)
                {
                    uint8_t *entry = &asus_leadout_buffer[CD_RAW_DATA_SIZE * leadout_index];

                    memcpy(sector_data.data(), entry, CD_DATA_SIZE);
                    memcpy(sector_subcode.data(), entry + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
                    uint8_t *sector_c2 = entry + CD_DATA_SIZE;

                    std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS_SCSI_OFF);
                    auto c2_count = state_from_c2(sector_state, sector_c2);
                    if(c2_count)
                    {
                        if(!refine)
                            ++errors_c2;

                        if(options.verbose)
                        {
                            uint32_t data_crc = CRC32().update(sector_data.data(), CD_DATA_SIZE).final();
                            uint32_t c2_crc = CRC32().update(sector_c2, CD_C2_SIZE).final();

                            std::string status_retries;
                            if(refine)
                                status_retries = std::format(", retry: {}", refine_counter + 1);
                            LOG_R("[LBA: {:6}] C2 error (bits: {:4}, data crc: {:08X}, C2 crc: {:08X}{})", lba, c2_count, data_crc, c2_crc, status_retries);
                        }

                        // DEBUG
                        //                        debug_print_c2_scm_offsets(sector_c2, lba_index, LBA_START, ctx.drive_config.read_offset);
                    }

                    store = true;
                    read = false;
                }
            }
        }

        if(refine && read)
        {
            read = false;

            bool c2_exists = false;
            bool skip_exists = false;
            read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
            for(auto const &ss : sector_state)
            {
                if(ss == State::ERROR_C2)
                    c2_exists = true;
                else if(ss == State::ERROR_SKIP)
                {
                    skip_exists = true;
                    break;
                }
            }

            if(c2_exists || skip_exists)
                read = true;
            if(c2_exists)
                flush = true;

            // refine subchannel (based on Q crc)
            if(options.refine_subchannel && subcode && !read)
            {
                read_entry(fs_sub, (uint8_t *)sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
                ChannelQ Q;
                subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
                if(!Q.isValid())
                    read = true;
            }

            // read sector
            if(read)
            {
                if(lba_refine == lba)
                {
                    ++refine_counter;
                    if(refine_counter < refine_retries)
                        lba_next = lba;
                    // maximum retries reached
                    else
                    {
                        if(options.verbose)
                            LOG_R("[LBA: {:6}] correction failure", lba);
                        read = false;
                        ++refine_processed;
                        refine_counter = 0;
                    }
                }
                // initial read
                else
                {
                    lba_refine = lba;
                    lba_next = lba;
                }
            }
            // sector is fixed
            else if(lba_refine == lba)
            {
                if(options.verbose)
                    LOG_R("[LBA: {:6}] correction success", lba);
                ++refine_processed;
                refine_counter = 0;
            }
        }

        if(read)
        {
            std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);

            if(flush)
                cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);

            auto read_time_start = std::chrono::high_resolution_clock::now();
            auto status = read_sector(*ctx.sptd, sector_buffer.data(), ctx.drive_config, lba);
            auto read_time_stop = std::chrono::high_resolution_clock::now();
            bool slow = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

            // PLEXTOR: multisession lead-out overread
            // usually there are couple of slow sectors before SCSI error is generated
            // some models (PX-708UF) exit on I/O semaphore timeout on such slow sectors
            if((ctx.drive_config.type == DriveConfig::Type::PLEXTOR || drive_is_plextor4824(ctx.drive_config)) && slow && inside_range(lba, error_ranges) != nullptr)
            {
                // skip sector in refine mode
                //                lba_next = lba + 1; //FIXME:
            }
            else if(status.status_code)
            {
                // don't log lead-out overread SCSI error
                if(inside_range(lba, error_ranges) == nullptr && lba < lba_end)
                {
                    if(!refine)
                        ++errors_scsi;

                    if(options.verbose)
                    {
                        std::string status_retries;
                        if(refine)
                            status_retries = std::format(", retry: {}", refine_counter + 1);
                        LOG_R("[LBA: {:6}] SCSI error ({}{})", lba, SPTD::StatusMessage(status), status_retries);
                    }
                }
            }
            else
            {
                memcpy(sector_data.data(), sector_buffer.data(), CD_DATA_SIZE);
                memcpy(sector_subcode.data(), sector_buffer.data() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
                uint8_t *sector_c2 = sector_buffer.data() + CD_DATA_SIZE;

                std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS);
                auto c2_count = state_from_c2(sector_state, sector_c2);
                if(c2_count)
                {
                    if(!refine)
                        ++errors_c2;

                    if(options.verbose)
                    {
                        uint32_t data_crc = CRC32().update(sector_data.data(), CD_DATA_SIZE).final();
                        uint32_t c2_crc = CRC32().update(sector_c2, CD_C2_SIZE).final();

                        std::string status_retries;
                        if(refine)
                            status_retries = std::format(", retry: {}", refine_counter + 1);
                        LOG_R("[LBA: {:6}] C2 error (bits: {:4}, data crc: {:08X}, C2 crc: {:08X}{})", lba, c2_count, data_crc, c2_crc, status_retries);
                    }

                    // DEBUG
                    //                    debug_print_c2_scm_offsets(sector_c2, lba_index, LBA_START, ctx.drive_config.read_offset);
                }

                store = true;

                if (lba == lba_end && drive_is_asus_ribshark(ctx.drive_config))
                    LOG_R("RibShark FW: Reading lead-out");
            }
        }

        if(store)
        {
            // some drives desync at a random sector
            if(subcode)
            {
                ChannelQ Q;
                subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
                if(Q.isValid())
                {
                    if(Q.adr == 1 && Q.mode1.tno)
                    {
                        int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

                        int32_t shift = lbaq - lba;
                        if(subcode_shift != shift)
                        {
                            subcode_shift = shift;
                            LOG_R("[LBA: {:6}] subcode desync (shift: {:+})", lba, subcode_shift);
                        }
                    }
                }
            }

            if(refine)
            {
                std::vector<State> sector_state_file(CD_DATA_SIZE_SAMPLES);
                std::vector<uint8_t> sector_data_file(CD_DATA_SIZE);
                read_entry(fs_state, (uint8_t *)sector_state_file.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
                read_entry(fs_scm, sector_data_file.data(), CD_DATA_SIZE, lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE, 0);

                bool update = false;
                bool scsi_exists_file = false;
                bool c2_exists_file = false;
                bool scsi_exists = false;
                bool c2_exists = false;
                for(uint32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
                {
                    if(sector_state_file[i] == State::ERROR_SKIP)
                        scsi_exists_file = true;
                    else if(sector_state_file[i] == State::ERROR_C2)
                        c2_exists_file = true;

                    // new data is improved
                    if(sector_state[i] > sector_state_file[i])
                        update = true;

                    // inherit older data if state is better
                    if(sector_state_file[i] > sector_state[i])
                    {
                        sector_state[i] = sector_state_file[i];
                        ((uint32_t *)sector_data.data())[i] = ((uint32_t *)sector_data_file.data())[i];
                    }

                    if(sector_state[i] == State::ERROR_SKIP)
                        scsi_exists = true;
                    else if(sector_state[i] == State::ERROR_C2)
                        c2_exists = true;
                }

                if(update)
                {
                    write_entry(fs_scm, sector_data.data(), CD_DATA_SIZE, lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE);
                    write_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset);

                    if(inside_range(lba, error_ranges) == nullptr && lba < lba_end)
                    {
                        if(scsi_exists_file && !scsi_exists)
                        {
                            --errors_scsi;
                            if(c2_exists)
                                ++errors_c2;
                        }
                        else if(c2_exists_file && !c2_exists)
                            --errors_c2;
                    }
                }

                if(subcode)
                {
                    ChannelQ Q;
                    subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
                    if(Q.isValid())
                    {
                        std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);
                        read_entry(fs_sub, (uint8_t *)sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
                        ChannelQ Q_file;
                        subcode_extract_channel((uint8_t *)&Q_file, sector_subcode_file.data(), Subchannel::Q);
                        if(!Q_file.isValid())
                        {
                            write_entry(fs_sub, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);
                            if(inside_range(lba, error_ranges) == nullptr)
                                --errors_q;
                        }
                    }
                }
            }
            else
            {
                write_entry(fs_scm, sector_data.data(), CD_DATA_SIZE, lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE);

                if(subcode)
                {
                    write_entry(fs_sub, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);

                    ChannelQ Q;
                    subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
                    if(Q.isValid())
                    {
                        errors_q_last = errors_q;
                    }
                    else
                    {
                        // PLEXTOR: some drives byte desync on subchannel after mass C2 errors with high bit count on high speed
                        // prevent this by flushing drive cache after C2 error range (flush cache on 5 consecutive Q errors)
                        if(errors_q - errors_q_last > 5)
                        {
                            cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);
                            errors_q_last = errors_q;
                        }

                        ++errors_q;
                    }
                }

                write_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset);
            }

            // grow lead-out overread if we still can read
            if(lba + 1 == lba_overread && !options.lba_end && (lba_overread - lba_end <= 100 || options.overread_leadout))
                ++lba_overread;
        }
        else
        {
            // past last session (disc) lead-out
            if(lba + 1 == lba_overread)
                lba_overread = lba;
            // between sessions
            else if(auto r = inside_range(lba, error_ranges); r != nullptr)
                lba_next = r->second;
        }

        if(signal.interrupt())
        {
            LOG_R("[LBA: {:6}] forced stop ", lba);
            lba_overread = lba;
        }

        if(refine)
        {
            if(lba == lba_refine)
            {
                LOGC_RF("{} [{:3}%] LBA: {:6}/{}, errors: {{ SCSI: {}, C2: {}, Q: {} }}", spinner_animation(),
                    percentage(refine_processed * refine_retries + refine_counter, refine_count * refine_retries), lba, lba_overread, errors_scsi, errors_c2, errors_q);
            }
        }
        else
        {
            LOGC_RF("{} [{:3}%] LBA: {:6}/{}, errors: {{ SCSI: {}, C2: {}, Q: {} }}", spinner_animation(), percentage(lba, lba_overread - 1), lba, lba_overread, errors_scsi, errors_c2, errors_q);
        }
    }
    LOGC_RF("");
    LOG("");

    LOG("media errors: ");
    LOG("  SCSI: {}", errors_scsi);
    LOG("  C2: {}", errors_c2);
    LOG("  Q: {}", errors_q);

    if(signal.interrupt())
        signal.raiseDefault();

    // always refine once if LG/ASUS to improve chances of capturing enough lead-out sectors
    return errors_scsi || errors_c2 || drive_is_asus(ctx.drive_config) && !options.asus_skip_leadout;
}

}
