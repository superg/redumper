module;
#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "throw_line.hh"

export module cd.dump;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import drive;
import options;
import range;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.signal;
import utils.strings;



// positive disc write offset shifts data to the right (into lead-out)
// negative disc write offset shifts data to the left (into lead-in)
// positive drive read offset reads data earlier than requested (reads into lead-in)
// negative drive read offset reads data later than requested (reads into lead-out)
// .scram LBA 0 starts at 0x6545FA0 byte offset



namespace gpsxre
{

const uint32_t SLOW_SECTOR_TIMEOUT = 4;
const uint32_t SUBCODE_BYTE_DESYNC_COUNT = 5;


void protection_fill(std::vector<uint8_t> &sector_protection, const std::vector<Range<int32_t>> &ranges, int32_t sample_base)
{
    for(int32_t i = 0; i < sector_protection.size(); ++i)
        sector_protection[i] = find_range(ranges, sample_base + i) != nullptr;
}


bool sector_data_complete(std::span<const State> sector_state, std::span<const uint8_t> sector_protection)
{
    bool complete = true;

    for(int32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
    {
        if(!sector_protection[i] && sector_state[i] != State::SUCCESS)
        {
            complete = false;
            break;
        }
    }

    return complete;
}


bool sector_data_state_update(std::span<State> sector_state, std::span<uint8_t> sector_data, std::span<const State> sector_state_in, std::span<const uint8_t> sector_data_in,
    std::span<const uint8_t> sector_protection)
{
    bool updated = false;

    for(uint32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
    {
        if(sector_protection[i])
            continue;

        if(sector_state[i] < sector_state_in[i])
        {
            sector_state[i] = sector_state_in[i];
            ((uint32_t *)sector_data.data())[i] = ((uint32_t *)sector_data_in.data())[i];

            updated = true;
        }
    }

    return updated;
}


bool sector_subcode_update(std::span<uint8_t> sector_subcode, std::span<const uint8_t> sector_subcode_in)
{
    bool updated = false;

    ChannelQ Q = subcode_extract_q(sector_subcode.data());
    ChannelQ Q_in = subcode_extract_q(sector_subcode_in.data());

    if(std::all_of((uint8_t *)&Q, (uint8_t *)&Q + sizeof(Q), [](uint8_t s) { return s == 0; }) || !Q.isValid() && Q_in.isValid())
    {
        std::copy(sector_subcode_in.begin(), sector_subcode_in.end(), sector_subcode.begin());

        updated = true;
    }

    return updated;
}


bool check_subcode_shift(int32_t &subcode_shift, int32_t lba, std::span<const uint8_t> sector_subcode, const Options &options)
{
    bool skip = false;

    ChannelQ Q = subcode_extract_q(sector_subcode.data());
    if(Q.isValid())
    {
        if(Q.adr == 1 && Q.mode1.tno)
        {
            int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

            int32_t shift = lbaq - lba;
            if(subcode_shift != shift)
            {
                subcode_shift = shift;

                if(options.verbose)
                    LOG_R("[LBA: {:6}] subcode desync (shift: {:+})", lba, subcode_shift);

                if(subcode_shift && options.skip_subcode_desync)
                    skip = true;
            }
        }
    }

    return skip;
}


void check_fix_byte_desync(Context &ctx, uint32_t &subcode_byte_desync_counter, int32_t lba, std::span<const uint8_t> sector_subcode)
{
    if(subcode_extract_q(sector_subcode.data()).isValid())
    {
        subcode_byte_desync_counter = 0;
    }
    else
    {
        ++subcode_byte_desync_counter;

        // PLEXTOR: some drives byte desync on subchannel after mass C2 errors with high bit count on high speed
        // prevent this by flushing drive cache after C2 error range (flush cache on 5 consecutive Q errors)
        if(subcode_byte_desync_counter > SUBCODE_BYTE_DESYNC_COUNT)
        {
            cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);
            subcode_byte_desync_counter = 0;
        }
    }
}


std::optional<int32_t> find_disc_offset(const TOC &toc, std::fstream &fs_state, std::fstream &fs_scram)
{
    for(auto &s : toc.sessions)
    {
        for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
        {
            auto &t = s.tracks[i];
            auto &t_next = s.tracks[i + 1];

            if(t.control & (uint8_t)ChannelQ::Control::DATA)
            {
                auto track_offset = track_offset_by_sync(t.lba_start, t_next.lba_start, fs_state, fs_scram);
                if(track_offset)
                    return track_offset;
            }
        }
    }

    return std::nullopt;
}


void refine_init_errors(Errors &errors, std::fstream &fs_state, std::fstream &fs_subcode, int32_t lba_start, int32_t lba_end, int32_t offset, int32_t data_offset)
{
    std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);

    uint32_t file_sample_start = sample_offset_r2a(std::min(lba_to_sample(lba_start, offset), lba_to_sample(lba_start, data_offset)));
    uint32_t file_sample_end = sample_offset_r2a(std::max(lba_to_sample(lba_end, offset), lba_to_sample(lba_end, data_offset)));

    for(uint32_t i = file_sample_start; i < file_sample_end;)
    {
        uint32_t size = std::min(CD_DATA_SIZE_SAMPLES, file_sample_end - i);
        std::span ss(sector_state.begin(), sector_state.begin() + size);

        read_entry(fs_state, (uint8_t *)ss.data(), sizeof(State), i, size, 0, (uint8_t)State::ERROR_SKIP);

        errors.scsi += std::count(ss.begin(), ss.end(), State::ERROR_SKIP);
        errors.c2 += std::count(ss.begin(), ss.end(), State::ERROR_C2);

        i += size;
    }

    for(int32_t lba = lba_start; lba < lba_end; ++lba)
    {
        int32_t lba_index = lba - LBA_START;

        read_entry(fs_subcode, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
        if(!subcode_extract_q(sector_subcode.data()).isValid())
            ++errors.q;
    }
}


export bool redumper_dump_cd(Context &ctx, const Options &options, DumpMode dump_mode)
{
    if(dump_mode == DumpMode::DUMP)
    {
        image_check_overwrite(options);

        if(!options.image_path.empty())
            std::filesystem::create_directories(options.image_path);
    }

    auto toc = toc_process(ctx, options, dump_mode == DumpMode::DUMP);
    if(dump_mode == DumpMode::DUMP)
    {
        LOG("disc TOC:");
        print_toc(toc);
        LOG("");
    }

    // do not dump user data if requesting metadata only
    if(options.metadata_only)
        return false;

    int32_t lba_start = options.lba_start ? *options.lba_start : ctx.drive_config.pregap_start;
    int32_t lba_end = options.lba_end ? *options.lba_end : toc.sessions.back().tracks.back().lba_end;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    auto mode = std::fstream::out | std::fstream::binary | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in);
    std::fstream fs_scram(image_prefix + ".scram", mode);
    std::fstream fs_state(image_prefix + ".state", mode);
    std::fstream fs_subcode(image_prefix + ".subcode", mode);

    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
    std::vector<uint8_t> sector_c2_backup(CD_C2_SIZE);
    std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
    std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);
    std::span<const uint8_t> sector_subcode(sector_buffer.begin() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

    std::vector<uint8_t> sector_data_file_a(CD_DATA_SIZE);
    std::vector<uint8_t> sector_data_file_d(CD_DATA_SIZE);
    std::vector<State> sector_state_file_a(CD_DATA_SIZE_SAMPLES);
    std::vector<State> sector_state_file_d(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_protection_a(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_protection_d(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);

    bool data_unscrambled_message = false;

    int32_t data_drive_offset = ctx.drive_config.read_offset;
    if(options.dump_write_offset)
        data_drive_offset = -*options.dump_write_offset;
    else if(dump_mode != DumpMode::DUMP)
    {
        auto disc_offset = find_disc_offset(toc, fs_state, fs_scram);
        if(disc_offset)
        {
            data_drive_offset = -*disc_offset;
            LOG("disc write offset: {:+}", *disc_offset);
            LOG("");
        }
    }

    std::vector<Range<int32_t>> protection;
    protection_to_ranges(protection, ctx.protection);
    protection_ranges_from_lba_ranges(protection, string_to_ranges(options.skip), -ctx.drive_config.read_offset);

    std::vector<Range<int32_t>> session_gaps;
    for(uint32_t i = 1; i < toc.sessions.size(); ++i)
    {
        Range r{ toc.sessions[i - 1].tracks.back().lba_end, toc.sessions[i].tracks.front().indices.front() + ctx.drive_config.pregap_start };
        if(!insert_range(session_gaps, r))
            throw_line("invalid session gap configuration");
    }

    Errors errors_initial = {};
    if(dump_mode != DumpMode::DUMP)
        refine_init_errors(errors_initial, fs_state, fs_subcode, lba_start, lba_end, -ctx.drive_config.read_offset, -data_drive_offset);
    Errors errors = errors_initial;

    int32_t subcode_shift = 0;
    uint32_t subcode_byte_desync_counter = 0;

    SignalINT signal;

    int32_t lba_overread = lba_end;
    for(int32_t lba = lba_start; lba < lba_overread; ++lba)
    {
        auto status_update = [dump_mode, lba, lba_start, lba_overread, errors](std::string_view status_message)
        {
            LOGC_RF("{} [{:3}%] LBA: {:6}/{}, errors: {{ SCSI{}: {}, C2{}: {}, Q: {} }}{}", spinner_animation(), std::min(100 * (lba - lba_start) / (lba_overread - lba_start), 100), lba, lba_overread,
                dump_mode == DumpMode::DUMP ? "" : "s", errors.scsi, dump_mode == DumpMode::DUMP ? "" : "s", errors.c2, errors.q, status_message);
        };

        if(signal.interrupt())
        {
            LOG_R("[LBA: {:6}] forced stop ", lba);
            LOG("");
            break;
        }

        int32_t lba_index = lba - LBA_START;

        read_entry(fs_state, (uint8_t *)sector_state_file_a.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
        read_entry(fs_state, (uint8_t *)sector_state_file_d.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, data_drive_offset, (uint8_t)State::ERROR_SKIP);
        read_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);

        protection_fill(sector_protection_a, protection, lba_to_sample(lba, -ctx.drive_config.read_offset));
        protection_fill(sector_protection_d, protection, lba_to_sample(lba, -data_drive_offset));

        if(sector_data_complete(sector_state_file_a, sector_protection_a) && sector_data_complete(sector_state_file_d, sector_protection_d)
            && (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
            continue;

        status_update("");

        read_entry(fs_scram, sector_data_file_a.data(), CD_DATA_SIZE, lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE, 0);
        read_entry(fs_scram, sector_data_file_d.data(), CD_DATA_SIZE, lba_index, 1, data_drive_offset * CD_SAMPLE_SIZE, 0);

        uint32_t retries = dump_mode == DumpMode::REFINE ? options.retries : 0;

        std::optional<bool> read_as_data;
        for(uint32_t r = 0; r <= retries; ++r)
        {
            if(signal.interrupt())
            {
                break;
            }

            std::string status_message;
            if(r)
            {
                std::string data_message;
                if(read_as_data)
                {
                    std::span<const State> sector_state_file(*read_as_data ? sector_state_file_d : sector_state_file_a);
                    uint32_t samples_good = std::count(sector_state_file.begin(), sector_state_file.end(), State::SUCCESS);
                    data_message = std::format(", data: {:3}%", 100 * samples_good / CD_DATA_SIZE_SAMPLES);
                }

                status_message = std::format(", retry: {}{}", r, data_message);
            }

            status_update(status_message);

            // flush cache
            if(r)
                cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);

            // read sector
            auto read_time_start = std::chrono::high_resolution_clock::now();
            bool unscrambled = options.force_unscrambled;
            auto status = read_sector_new(*ctx.sptd, sector_buffer.data(), unscrambled, ctx.drive_config, lba);
            auto read_time_stop = std::chrono::high_resolution_clock::now();

            if(!data_unscrambled_message && unscrambled)
            {
                LOG("warning: unscrambled sector read");
                data_unscrambled_message = true;
            }

            bool slow_sector = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;
            auto session_gap_range = find_range(session_gaps, lba);
            if(session_gap_range != nullptr && slow_sector)
            {
                LOG_R("[LBA: {:6}] session gap jump (LBA: {:6})", lba, session_gap_range->end);

                lba = session_gap_range->end - 1;
                break;
            }

            if(status.status_code || check_subcode_shift(subcode_shift, lba, sector_subcode, options))
            {
                if(session_gap_range == nullptr && lba < lba_end)
                {
                    if(dump_mode != DumpMode::REFINE)
                        ++errors.scsi;

                    if(options.verbose)
                        LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
                }
            }
            else
            {
                // grow lead-out overread if we still can read
                if(lba + 1 == lba_overread && !slow_sector && !options.lba_end && (lba_overread - lba_end < LEADOUT_OVERREAD_COUNT || options.overread_leadout))
                    ++lba_overread;

                if(!retries)
                    check_fix_byte_desync(ctx, subcode_byte_desync_counter, lba, sector_subcode);

                bool subcode_updated = sector_subcode_update(sector_subcode_file, sector_subcode);
                if(subcode_updated)
                {
                    write_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);

                    bool subcode_valid = subcode_extract_q(sector_subcode_file.data()).isValid();

                    if(dump_mode == DumpMode::REFINE)
                    {
                        if(subcode_valid && lba < lba_end)
                            --errors.q;
                    }
                    else
                    {
                        if(!subcode_valid)
                            ++errors.q;
                    }
                }

                if(!read_as_data)
                    read_as_data = unscrambled;

                if(*read_as_data == unscrambled)
                {
                    std::span<const uint8_t> sector_protection(unscrambled ? sector_protection_d : sector_protection_a);

                    uint32_t c2_bits = c2_bits_count(sector_c2);
                    if(c2_bits)
                    {
                        if(dump_mode != DumpMode::REFINE)
                            ++errors.c2;

                        if(options.verbose)
                        {
                            std::string difference_message;
                            if(r)
                            {
                                bool c2_match = std::equal(sector_c2.begin(), sector_c2.end(), sector_c2_backup.begin());
                                difference_message = std::format(", difference: {}", c2_match ? "-" : "+");
                            }

                            sector_c2_backup.assign(sector_c2.begin(), sector_c2.end());

                            LOG_R("[LBA: {:6}] C2 error (bits: {:4}{})", lba, c2_bits, difference_message);
                        }
                    }

                    std::span<State> sector_state_file(unscrambled ? sector_state_file_d : sector_state_file_a);
                    std::span<uint8_t> sector_data_file(unscrambled ? sector_data_file_d : sector_data_file_a);

                    uint32_t scsi_before = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_SKIP);
                    uint32_t c2_before = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_C2);

                    bool allow_update = dump_mode != DumpMode::REFINE || !options.refine_sector_mode || !c2_bits;

                    bool data_updated = allow_update && sector_data_state_update(sector_state_file, sector_data_file, c2_to_state(sector_c2.data(), State::SUCCESS), sector_data, sector_protection);
                    if(data_updated)
                    {
                        int32_t offset = unscrambled ? data_drive_offset : ctx.drive_config.read_offset;
                        write_entry(fs_scram, sector_data_file.data(), CD_DATA_SIZE, lba_index, 1, offset * CD_SAMPLE_SIZE);
                        write_entry(fs_state, (uint8_t *)sector_state_file.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset);

                        if(dump_mode == DumpMode::REFINE && lba < lba_end)
                        {
                            uint32_t scsi_after = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_SKIP);
                            uint32_t c2_after = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_C2);

                            errors.scsi -= scsi_before - scsi_after;
                            errors.c2 -= c2_before - c2_after;
                        }
                    }

                    if(sector_data_complete(sector_state_file, sector_protection) && (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
                    {
                        if(dump_mode == DumpMode::REFINE && lba < lba_end && (data_updated || subcode_updated))
                        {
                            if(options.verbose)
                                LOG_R("[LBA: {:6}] correction success", lba);
                        }

                        break;
                    }
                }
                else
                {
                    LOG_R("[LBA: {:6}] unexpected read type on retry (retry: {}, read type: {})", lba, r, unscrambled ? "DATA" : "AUDIO");
                }
            }

            if(dump_mode == DumpMode::REFINE && r == options.retries && options.verbose && lba < lba_end)
            {
                bool failure = false;

                std::string data_message;
                if(read_as_data)
                {
                    std::span<const State> sector_state_file(*read_as_data ? sector_state_file_d : sector_state_file_a);
                    uint32_t samples_good = std::count(sector_state_file.begin(), sector_state_file.end(), State::SUCCESS);
                    if(samples_good != CD_DATA_SIZE_SAMPLES)
                    {
                        data_message = std::format(", data: {:3}%", 100 * samples_good / CD_DATA_SIZE_SAMPLES);
                        failure = true;
                    }
                    else if(options.refine_subchannel && !subcode_extract_q(sector_subcode_file.data()).isValid())
                        failure = true;
                }
                else
                    failure = true;

                if(failure)
                    LOG_R("[LBA: {:6}] correction failure{}", lba, data_message);
            }
        }
    }

    if(!signal.interrupt())
    {
        LOGC_RF("");
        LOGC("");
    }

    if(dump_mode == DumpMode::DUMP)
    {
        ctx.dump_errors = errors;

        LOG("media errors: ");
        LOG("  SCSI: {}", errors.scsi);
        LOG("  C2: {}", errors.c2);
        LOG("  Q: {}", errors.q);
    }
    else if(dump_mode == DumpMode::REFINE)
    {
        LOG("correction statistics: ");
        LOG("  SCSI: {} samples", errors_initial.scsi - errors.scsi);
        LOG("  C2: {} samples", errors_initial.c2 - errors.c2);
        LOG("  Q: {} sectors", errors_initial.q - errors.q);
    }

    if(signal.interrupt())
        signal.raiseDefault();

    return errors.scsi || errors.c2;
}

}
