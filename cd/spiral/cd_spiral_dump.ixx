module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include "throw_line.hh"



export module cd.spiral;

import cd.cd;
import cd.common;
import cd.subcode;
import common;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.signal;



namespace gpsxre
{

constexpr uint16_t C4_TIMEOUT_MS = 5000;


std::optional<int32_t> position_q_lba(const uint8_t *frame)
{
    auto q = subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE);
    if(!q.isValid() || q.adr != 1 || q.mode1.tno == 0 || !BCDMSF_valid(q.mode1.a_msf))
        return std::nullopt;
    if(q.mode1.tno != CD_LEADOUT_TRACK_NUMBER && (q.mode1.tno > 0x99 || (q.mode1.tno & 0x0f) > 9))
        return std::nullopt;

    return BCDMSF_to_LBA(q.mode1.a_msf);
}


void seek_spiral_start(SPTD &sptd, SignalINT &signal, const Options &options)
{
    constexpr int32_t INWARD_SEEK_STEP = 50;
    constexpr int32_t OUTWARD_SEEK_STEP = 1;
    constexpr uint32_t MAX_SEEKS = 0x7FFF;

    LOG("seeking inner pickup limit");
    SpiralDrive_SeekResult seek_result = SpiralDrive_SeekResult::OK;
    int32_t inward_step = INWARD_SEEK_STEP;
    for(uint32_t i = 0; i < MAX_SEEKS; ++i)
    {
        if(signal.interrupt())
            signal.raiseDefault();

        if(auto status = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, -inward_step).first; status.status_code)
            throw_line("inward SEEK failed (step: {}, SCSI: {})", inward_step, SPTD::StatusMessage(status));

        if(seek_result == SpiralDrive_SeekResult::OK)
        {
            if(options.verbose)
                LOG("inward SEEK success (step: {}, iteration: {})", inward_step, i + 1);
        }
        else if(seek_result == SpiralDrive_SeekResult::FAILED)
        {
            if(inward_step == 1)
                throw_line("inward SEEK failed at minimum step magnitude");

            inward_step /= 2;
            if(options.verbose)
                LOG("inward SEEK failed, reducing magnitude (step: {})", inward_step);
        }
        else if(seek_result == SpiralDrive_SeekResult::INNER_LIMIT)
        {
            LOG("inward SEEK inner limit reached (step: {}, iterations: {})", inward_step, i + 1);
            break;
        }
    }
    if(seek_result != SpiralDrive_SeekResult::INNER_LIMIT)
        throw_line("inward SEEK inner limit was not reached (step: {}, iteration: {})", inward_step, MAX_SEEKS);

    LOG("seeking readable start");
    bool read_success = false;
    std::vector<uint8_t> buffer(CD_RAW_DATA_SIZE);
    for(uint32_t i = 0; i < MAX_SEEKS; ++i)
    {
        if(signal.interrupt())
            signal.raiseDefault();
        
        // seek
        if(i)
        {
            if(auto status = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, OUTWARD_SEEK_STEP).first; status.status_code)
                throw_line("outward SEEK failed (step: {}, SCSI: {})", OUTWARD_SEEK_STEP, SPTD::StatusMessage(status));
            if(seek_result != SpiralDrive_SeekResult::OK)
                throw_line("outward SEEK failed (result: {}, step: {}, iteration: {})", (uint8_t)seek_result, OUTWARD_SEEK_STEP, i + 1);

            if(options.verbose)
                LOG("outward SEEK success (step: {}, iteration: {})", OUTWARD_SEEK_STEP, i + 1);
        }

        // read        
        if(auto status = cmd_spiraldrive(sptd, buffer.data(), (uint32_t)buffer.size(), SpiralDrive_Operation::READ, C4_TIMEOUT_MS, 1).first; status.status_code)
        {
            if(options.verbose)
                LOG("outward READ failed (step: {}, iteration: {}, SCSI: {})", OUTWARD_SEEK_STEP, i + 1, SPTD::StatusMessage(status));
        }
        else
        {
            LOG("outward READ success (step: {}, iterations: {})", OUTWARD_SEEK_STEP, i + 1);
            read_success = true;
            break;
        }
    }

    if(!read_success)
        throw_line("outward SEEK readable start was not reached (step: {}, iteration: {})", OUTWARD_SEEK_STEP, MAX_SEEKS);
}


export int redumper_spiral_dump(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    {
        auto spiraldrive_version = is_spiraldrive_firmware(ctx.drive_config);
        if(spiraldrive_version)
        {
            if(spiraldrive_version < spiraldrive_minimum_version())
                throw_line("spiral::dump requires SpiralDrive firmware {} or newer", spiraldrive_version_string(spiraldrive_minimum_version()));
        }
        else
            throw_line("spiral::dump requires a drive running SpiralDrive firmware");
    }

    auto read_size = (uint32_t)options.spiraldrive_read_size;
    int32_t lba_end = options.lba_end ? *options.lba_end : std::numeric_limits<int32_t>::max();

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    if(!options.overwrite && (std::filesystem::exists(image_prefix + ".scram") || std::filesystem::exists(image_prefix + ".c2") || std::filesystem::exists(image_prefix + ".subcode")))
        throw_line("dump already exists (image name: {})", options.image_name);

    std::fstream fs_scram(image_prefix + ".scram", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_c2(image_prefix + ".c2", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_subcode(image_prefix + ".subcode", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    fs_scram.exceptions(std::ios::badbit | std::ios::failbit);
    fs_c2.exceptions(std::ios::badbit | std::ios::failbit);
    fs_subcode.exceptions(std::ios::badbit | std::ios::failbit);

    // reserve the negative LBAs, with each put cursor left at LBA 0
    std::vector<uint8_t> data_padding(CD_DATA_SIZE, 0);
    std::vector<uint8_t> c2_padding(CD_C2_SIZE, 0xFF);
    std::vector<uint8_t> subcode_padding(CD_SUBCODE_SIZE, 0);
    for(int32_t lba = LBA_START; lba < 0; ++lba)
    {
        uint64_t index = lba - LBA_START;
        write_entry(fs_scram, data_padding.data(), CD_DATA_SIZE, index, 1, 0);
        write_entry(fs_c2, c2_padding.data(), CD_C2_SIZE, index, 1, 0);
        write_entry(fs_subcode, subcode_padding.data(), CD_SUBCODE_SIZE, index, 1, 0);
    }

    SignalINT signal;
    seek_spiral_start(*ctx.sptd, signal, options);

    std::vector<uint8_t> staged_data;
    std::vector<uint8_t> staged_c2;
    std::vector<uint8_t> staged_subcode;
    std::vector<uint8_t> read_buffer(read_size * CD_RAW_DATA_SIZE);
    std::vector<uint8_t> data;
    std::vector<uint8_t> c2;
    std::vector<uint8_t> subcode;

    uint64_t sectors = 0;
    uint64_t scsi_errors = 0;
    uint64_t c2_errors = 0;
    uint64_t q_errors = 0;
    int64_t next_lba = 0;
    std::optional<int32_t> latest_q_lba;
    bool anchored = false;

    while(!signal.interrupt() && (!anchored || next_lba < lba_end))
    {
        data.assign(read_size * CD_DATA_SIZE, 0);
        c2.assign(read_size * CD_C2_SIZE, 0xFF);
        subcode.assign(read_size * CD_SUBCODE_SIZE, 0);
        std::optional<int64_t> zero_index;
        std::string displayed_lba = latest_q_lba ? std::to_string(*latest_q_lba) : "------";

        auto result = cmd_spiraldrive(*ctx.sptd, read_buffer.data(), (uint32_t)read_buffer.size(), SpiralDrive_Operation::READ, C4_TIMEOUT_MS, read_size);
        sectors += read_size;
        if(result.first.status_code || result.second != read_buffer.size())
            scsi_errors += (uint64_t)read_size * CD_DATA_SIZE_SAMPLES;
        else
        {
            for(uint32_t i = 0; i < read_size; ++i)
            {
                const uint8_t *frame = read_buffer.data() + i * CD_RAW_DATA_SIZE;
                std::copy_n(frame, CD_DATA_SIZE, data.data() + i * CD_DATA_SIZE);
                auto frame_state = c2_to_state(frame + CD_DATA_SIZE, State::SUCCESS);
                c2_errors += std::count(frame_state.begin(), frame_state.end(), State::ERROR_C2);
                std::copy_n(frame + CD_DATA_SIZE, CD_C2_SIZE, c2.data() + i * CD_C2_SIZE);
                std::copy_n(frame + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE, subcode.data() + i * CD_SUBCODE_SIZE);

                auto q = subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE);
                if(!q.isValid())
                {
                    ++q_errors;
                    displayed_lba = latest_q_lba ? std::to_string(*latest_q_lba) : "------";
                }
                else if(auto q_lba = position_q_lba(frame))
                {
                    latest_q_lba = q_lba;
                    displayed_lba = std::to_string(*q_lba);
                    if(!anchored && !zero_index && *q_lba >= 0)
                        zero_index = (int64_t)(staged_data.size() / CD_DATA_SIZE) + i - *q_lba;
                }
                else
                    displayed_lba = "------";
            }
        }

        if(!anchored)
        {
            staged_data.insert(staged_data.end(), data.begin(), data.end());
            staged_c2.insert(staged_c2.end(), c2.begin(), c2.end());
            staged_subcode.insert(staged_subcode.end(), subcode.begin(), subcode.end());
            if(zero_index)
            {
                int64_t staged_frames = (int64_t)(staged_data.size() / CD_DATA_SIZE);
                int64_t prefix_frames = std::max<int64_t>(0, *zero_index);
                int64_t first_lba = std::max<int64_t>(0, -*zero_index);
                data.assign(staged_data.begin() + prefix_frames * CD_DATA_SIZE, staged_data.end());
                c2.assign(staged_c2.begin() + prefix_frames * CD_C2_SIZE, staged_c2.end());
                subcode.assign(staged_subcode.begin() + prefix_frames * CD_SUBCODE_SIZE, staged_subcode.end());
                staged_data.resize(prefix_frames * CD_DATA_SIZE);
                staged_c2.resize(prefix_frames * CD_C2_SIZE);
                staged_subcode.resize(prefix_frames * CD_SUBCODE_SIZE);

                // keep C2 raw and mark any missing initial sectors as unavailable
                for(int64_t lba = 0; lba < first_lba; ++lba)
                    write_entry(fs_c2, c2_padding.data(), CD_C2_SIZE, lba - LBA_START, 1, 0);
                next_lba = staged_frames - *zero_index;
                anchored = true;
            }
        }
        else
            next_lba += read_size;

        if(anchored)
        {
            uint64_t count = data.size() / CD_DATA_SIZE;
            uint64_t index = next_lba - (int64_t)count - LBA_START;
            write_entry(fs_scram, data.data(), CD_DATA_SIZE, index, count, (int64_t)ctx.drive_config.read_offset * CD_SAMPLE_SIZE);
            write_entry(fs_c2, c2.data(), CD_C2_SIZE, index, count, 0);
            write_entry(fs_subcode, subcode.data(), CD_SUBCODE_SIZE, index, count, 0);
        }

        LOGC_RF("{} sector: {:6}, LBA: {:6}, errors: {{ SCSIs: {}, C2s: {}, Q: {} }}", spinner_animation(), sectors, displayed_lba, scsi_errors, c2_errors, q_errors);
    }

    if(anchored && !staged_data.empty())
    {
        // the staged prefix ends at LBA 0, with signed offsets allowing write_entry to clip entries before LBA_START
        uint64_t count = staged_data.size() / CD_DATA_SIZE;
        int64_t prefix_offset = (int64_t)count + LBA_START;
        write_entry(fs_scram, staged_data.data(), CD_DATA_SIZE, 0, count, prefix_offset * CD_DATA_SIZE + (int64_t)ctx.drive_config.read_offset * CD_SAMPLE_SIZE);
        write_entry(fs_c2, staged_c2.data(), CD_C2_SIZE, 0, count, prefix_offset * CD_C2_SIZE);
        write_entry(fs_subcode, staged_subcode.data(), CD_SUBCODE_SIZE, 0, count, prefix_offset * CD_SUBCODE_SIZE);
    }

    fs_scram.flush();
    fs_c2.flush();
    fs_subcode.flush();
    fs_scram.close();
    fs_c2.close();
    fs_subcode.close();
    if(anchored)
    {
        int64_t endpoint = std::min<int64_t>(next_lba, lba_end);
        uint64_t entries = (uint64_t)std::max<int64_t>(0, endpoint - LBA_START);
        std::filesystem::resize_file(image_prefix + ".scram", entries * CD_DATA_SIZE);
        std::filesystem::resize_file(image_prefix + ".c2", entries * CD_C2_SIZE);
        std::filesystem::resize_file(image_prefix + ".subcode", entries * CD_SUBCODE_SIZE);
    }

    LOGC_RF("");
    LOGC("");
    LOG("media errors: ");
    LOG("  SCSI: {} samples", scsi_errors);
    LOG("  C2: {} samples", c2_errors);
    LOG("  Q: {}", q_errors);

    if(signal.interrupt())
    {
        if(!anchored)
            LOG("capture interrupted before a positional Q supplied a nonnegative LBA anchor");
        signal.raiseDefault();
    }

    return exit_code;
}

}
